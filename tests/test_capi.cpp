// fusa:test REQ-CAPI-001
// fusa:test REQ-CAPI-002
// fusa:test REQ-CAPI-003
// fusa:test REQ-CAPI-004
// fusa:test REQ-CAPI-005
// fusa:test REQ-CAPI-006
// fusa:test REQ-CAPI-007
// fusa:test REQ-CAPI-008
// fusa:test REQ-CAPI-009

// ROADMAP.md milestone 60 (v2.16.0): entirely rewritten against the v2.16.0
// C ABI (server+endpoint addressing, rcp_request_fn_t) — see rcp/capi.h's
// and rcp/capi_impl.hpp's own header comments.
#include <catch2/catch_test_macros.hpp>

#include "rcp/acf.hpp"
#include "rcp/capi_impl.hpp"
#include "rcp/mock.hpp"

#include <cstring>

// ── Portable alignment helper ─────────────────────────────────────────────────
// Placement-new into a uint8_t[] needs the buffer aligned to at least
// alignof(std::max_align_t) (typically 16 bytes).
// MSVC uses __declspec(align(N)); GCC/Clang use __attribute__((aligned(N))).
#if defined(_MSC_VER)
#  define RCP_ALIGNED_BUF(type, name, size) \
    static __declspec(align(16)) type name[size]
#else
#  define RCP_ALIGNED_BUF(type, name, size) \
    static type name[size] __attribute__((aligned(16)))
#endif

RCP_ALIGNED_BUF(uint8_t, reg_buf,  512);
RCP_ALIGNED_BUF(uint8_t, ctrl_buf,  64);

namespace {

// echo_request_fn is a minimal rcp_request_fn_t test double: it copies the
// request's own info/payload back as the response, so a round trip can be
// checked without any real backend.
rcp_err_t echo_request_fn(void* /*userdata*/, const rcp_request_t* req,
                           rcp_response_t* resp, uint32_t /*timeout_ms*/) {
    resp->info = req->info;
    resp->info.rsp = 1;
    uint32_t n = req->payload_len < resp->payload_cap ? req->payload_len : resp->payload_cap;
    if (n > 0 && req->payload) std::memcpy(resp->payload, req->payload, n);
    resp->payload_len = n;
    return RCP_OK;
}

// overrun_request_fn deliberately violates the rcp_request_fn_t contract by
// reporting more bytes written than resp->payload_cap allowed.
rcp_err_t overrun_request_fn(void* /*userdata*/, const rcp_request_t* /*req*/,
                              rcp_response_t* resp, uint32_t /*timeout_ms*/) {
    resp->payload_len = resp->payload_cap + 1;
    return RCP_OK;
}

// not_found_request_fn always reports RCP_ERR_NOT_FOUND, unconditionally.
rcp_err_t not_found_request_fn(void* /*userdata*/, const rcp_request_t* /*req*/,
                                rcp_response_t* /*resp*/, uint32_t /*timeout_ms*/) {
    return RCP_ERR_NOT_FOUND;
}

// mock_server_request_fn bridges an rcp::mock::Server::dispatch() call
// (v2.12.0) into rcp_request_fn_t shape, demonstrating the real-backend
// wiring rcp/capi_impl.hpp's own header comment describes.
rcp_err_t mock_server_request_fn(void* userdata, const rcp_request_t* req,
                                  rcp_response_t* resp, uint32_t /*timeout_ms*/) {
    auto* srv = static_cast<rcp::mock::Server*>(userdata);

    rcp::acf::AcfMessageInfo in_info{};
    in_info.acf_msg_type            = req->info.acf_msg_type;
    in_info.byte_bus_id              = req->info.byte_bus_id;
    in_info.evt_op                   = req->info.evt_op;
    in_info.op                       = req->info.op != 0;
    in_info.transaction_num          = req->info.transaction_num;
    in_info.read_size_or_segment_num = req->info.read_size_or_segment_num;

    std::vector<uint8_t> in_payload(req->payload, req->payload + req->payload_len);
    rcp::acf::AcfMessageInfo out_info;
    std::vector<uint8_t> out_payload;
    auto ec = srv->dispatch(0, in_info, in_payload, out_info, out_payload);
    if (ec) return RCP_ERR_INVALID;

    if (out_payload.size() > resp->payload_cap) return RCP_ERR_NOMEM;
    std::memcpy(resp->payload, out_payload.data(), out_payload.size());
    resp->payload_len       = static_cast<uint32_t>(out_payload.size());
    resp->info.acf_msg_type = out_info.acf_msg_type;
    resp->info.byte_bus_id   = out_info.byte_bus_id;
    resp->info.rsp           = out_info.rsp ? 1 : 0;
    resp->info.err           = out_info.err ? 1 : 0;
    resp->info.op             = out_info.op ? 1 : 0;
    return RCP_OK;
}

} // namespace

TEST_CASE("capi: registry init succeeds with adequate buffer", "[capi]") {
    rcp_registry_h reg = nullptr;
    rcp_err_t err = rcp_registry_init(reg_buf, sizeof(reg_buf), &reg);
    REQUIRE(err == RCP_OK);
    REQUIRE(reg != nullptr);
    rcp_registry_close(reg);
}

TEST_CASE("capi: registry init fails with small buffer", "[capi]") {
    uint8_t small[4];
    rcp_registry_h reg = nullptr;
    rcp_err_t err = rcp_registry_init(small, sizeof(small), &reg);
    REQUIRE(err == RCP_ERR_NOMEM);
}

TEST_CASE("capi: ctrl init succeeds", "[capi]") {
    rcp_ctrl_h ctrl = nullptr;
    rcp_err_t err = rcp_ctrl_init(0x1122334455667788ULL, 1, echo_request_fn, nullptr,
                                   ctrl_buf, sizeof(ctrl_buf), &ctrl);
    REQUIRE(err == RCP_OK);
    REQUIRE(ctrl != nullptr);
}

TEST_CASE("capi: ctrl init fails with small buffer", "[capi]") {
    uint8_t tiny[1];
    rcp_ctrl_h ctrl = nullptr;
    rcp_err_t err = rcp_ctrl_init(0, 1, echo_request_fn, nullptr, tiny, sizeof(tiny), &ctrl);
    REQUIRE(err == RCP_ERR_NOMEM);
}

TEST_CASE("capi: ctrl init fails with a null request fn", "[capi]") {
    RCP_ALIGNED_BUF(uint8_t, buf, 64);
    rcp_ctrl_h ctrl = nullptr;
    rcp_err_t err = rcp_ctrl_init(0, 1, nullptr, nullptr, buf, sizeof(buf), &ctrl);
    REQUIRE(err == RCP_ERR_INVALID);
}

TEST_CASE("capi: registry_add and send round-trips via echo_request_fn", "[capi]") {
    rcp_registry_h reg  = nullptr;
    rcp_ctrl_h     ctrl = nullptr;

    RCP_ALIGNED_BUF(uint8_t, reg_buf2,  512);
    RCP_ALIGNED_BUF(uint8_t, ctrl_buf2,  64);

    REQUIRE(rcp_registry_init(reg_buf2, sizeof(reg_buf2), &reg) == RCP_OK);
    REQUIRE(rcp_ctrl_init(42, 1, echo_request_fn, nullptr, ctrl_buf2, sizeof(ctrl_buf2), &ctrl) == RCP_OK);
    REQUIRE(rcp_registry_add(reg, ctrl) == RCP_OK);

    rcp_ctrl_h found = nullptr;
    REQUIRE(rcp_registry_lookup(reg, 42, 1, &found) == RCP_OK);
    REQUIRE(found == ctrl);

    uint8_t in[2]  = {0xAB, 0xCD};
    uint8_t out[2] = {0, 0};
    rcp_request_t req{};
    req.stream_key       = 42;
    req.info.byte_bus_id = 1;
    req.info.op          = 1; // write
    req.payload           = in;
    req.payload_len        = sizeof(in);

    rcp_response_t resp{};
    resp.payload     = out;
    resp.payload_cap = sizeof(out);

    auto err = rcp_send(ctrl, &req, &resp, 100);
    REQUIRE(err == RCP_OK);
    REQUIRE(resp.payload_len == 2);
    REQUIRE(out[0] == 0xAB);
    REQUIRE(out[1] == 0xCD);
    REQUIRE(resp.info.rsp == 1);

    rcp_registry_close(reg);
}

TEST_CASE("capi: registry_add rejects a duplicate (stream_key, byte_bus_id)", "[capi]") {
    RCP_ALIGNED_BUF(uint8_t, reg_buf3,  512);
    RCP_ALIGNED_BUF(uint8_t, ctrl_a,     64);
    RCP_ALIGNED_BUF(uint8_t, ctrl_b,     64);

    rcp_registry_h reg = nullptr;
    rcp_ctrl_h a = nullptr, b = nullptr;
    REQUIRE(rcp_registry_init(reg_buf3, sizeof(reg_buf3), &reg) == RCP_OK);
    REQUIRE(rcp_ctrl_init(7, 2, echo_request_fn, nullptr, ctrl_a, sizeof(ctrl_a), &a) == RCP_OK);
    REQUIRE(rcp_ctrl_init(7, 2, echo_request_fn, nullptr, ctrl_b, sizeof(ctrl_b), &b) == RCP_OK);

    REQUIRE(rcp_registry_add(reg, a) == RCP_OK);
    REQUIRE(rcp_registry_add(reg, b) == RCP_ERR_BUSY);

    rcp_registry_close(reg);
}

TEST_CASE("capi: registry_lookup returns RCP_ERR_NOT_FOUND for an unregistered endpoint", "[capi]") {
    RCP_ALIGNED_BUF(uint8_t, reg_buf4, 512);
    rcp_registry_h reg = nullptr;
    REQUIRE(rcp_registry_init(reg_buf4, sizeof(reg_buf4), &reg) == RCP_OK);

    rcp_ctrl_h out = nullptr;
    REQUIRE(rcp_registry_lookup(reg, 1, 1, &out) == RCP_ERR_NOT_FOUND);

    rcp_registry_close(reg);
}

TEST_CASE("capi: rcp_send with null ctrl/req/resp returns invalid", "[capi]") {
    RCP_ALIGNED_BUF(uint8_t, buf, 64);
    rcp_ctrl_h ctrl = nullptr;
    REQUIRE(rcp_ctrl_init(0, 1, echo_request_fn, nullptr, buf, sizeof(buf), &ctrl) == RCP_OK);

    rcp_request_t  req{};
    rcp_response_t resp{};
    REQUIRE(rcp_send(nullptr, &req, &resp, 100) == RCP_ERR_INVALID);
    REQUIRE(rcp_send(ctrl, nullptr, &resp, 100) == RCP_ERR_INVALID);
    REQUIRE(rcp_send(ctrl, &req, nullptr, 100) == RCP_ERR_INVALID);
}

TEST_CASE("capi: rcp_send propagates the callback's own error code", "[capi]") {
    RCP_ALIGNED_BUF(uint8_t, buf, 64);
    rcp_ctrl_h ctrl = nullptr;
    REQUIRE(rcp_ctrl_init(0, 1, not_found_request_fn, nullptr, buf, sizeof(buf), &ctrl) == RCP_OK);

    rcp_request_t  req{};
    rcp_response_t resp{};
    REQUIRE(rcp_send(ctrl, &req, &resp, 0) == RCP_ERR_NOT_FOUND);
}

TEST_CASE("capi: rcp_send rejects a callback that overruns payload_cap", "[capi][REQ-CAPI-009]") {
    RCP_ALIGNED_BUF(uint8_t, buf, 64);
    rcp_ctrl_h ctrl = nullptr;
    REQUIRE(rcp_ctrl_init(0, 1, overrun_request_fn, nullptr, buf, sizeof(buf), &ctrl) == RCP_OK);

    uint8_t out[4];
    rcp_request_t  req{};
    rcp_response_t resp{};
    resp.payload     = out;
    resp.payload_cap = sizeof(out);
    REQUIRE(rcp_send(ctrl, &req, &resp, 0) == RCP_ERR_INVALID);
}

TEST_CASE("capi: sizeof queries return nonzero", "[capi]") {
    REQUIRE(rcp_registry_sizeof() > 0);
    REQUIRE(rcp_ctrl_sizeof() > 0);
}

// ── Real-backend interop: rcp::mock::Server (v2.12.0) via a caller-written
// rcp_request_fn_t adapter ──────────────────────────────────────────────────

TEST_CASE("capi: rcp_send against a real rcp::mock::Server GPIO endpoint", "[capi][mock]") {
    rcp::mock::Server srv;
    REQUIRE_FALSE(srv.advance_to_rcp_configured());

    RCP_ALIGNED_BUF(uint8_t, buf, 64);
    rcp_ctrl_h ctrl = nullptr;
    REQUIRE(rcp_ctrl_init(0, rcp::mock::kGpioByteBusId, mock_server_request_fn, &srv,
                          buf, sizeof(buf), &ctrl) == RCP_OK);

    // Write pins 0x00000001 (big-endian 4-byte GPIO payload), then read back.
    uint8_t write_payload[4] = {0x00, 0x00, 0x00, 0x01};
    uint8_t out[8] = {};

    rcp_request_t req{};
    req.info.byte_bus_id = rcp::mock::kGpioByteBusId;
    req.info.op           = 1; // write
    req.payload            = write_payload;
    req.payload_len         = sizeof(write_payload);

    rcp_response_t resp{};
    resp.payload     = out;
    resp.payload_cap = sizeof(out);
    REQUIRE(rcp_send(ctrl, &req, &resp, 0) == RCP_OK);
    REQUIRE(resp.payload_len == 4);
    REQUIRE(resp.info.err == 0);

    rcp_request_t read_req{};
    read_req.info.byte_bus_id = rcp::mock::kGpioByteBusId;
    read_req.info.op           = 0; // read

    rcp_response_t read_resp{};
    read_resp.payload     = out;
    read_resp.payload_cap = sizeof(out);
    REQUIRE(rcp_send(ctrl, &read_req, &read_resp, 0) == RCP_OK);
    REQUIRE(read_resp.payload_len == 4);
    REQUIRE(out[3] == 0x01);
}
