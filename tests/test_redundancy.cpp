// fusa:test REQ-RED-001
// fusa:test REQ-RED-002
// fusa:test REQ-RED-003
// fusa:test REQ-RED-004
// fusa:test REQ-RED-005
// fusa:test REQ-RED-006
// fusa:test REQ-RED-007
// fusa:test REQ-RED-008
//
// Rebound (cpp-RCP-FS-03/#86): redundancy::RedundantRequestFn now holds a
// pair of rcp::RequestFn (rcp/adapt.hpp) instead of decorating the retired
// rcp::Controller — see rcp/redundancy.hpp's own header comment. The old
// REQ-RED-006/REQ-RED-008 cases asserted Controller::close()/zone()
// passthrough, neither of which has an analog on a plain RequestFn (a
// RequestFn owns no closeable resource of its own, the same reasoning
// rcp/adapt.hpp's own close() note documents); they are replaced below with
// cases covering RedundantRequestFn's own usability as an rcp::RequestFn.
#include <catch2/catch_test_macros.hpp>

#include "rcp/mock.hpp"
#include "rcp/redundancy.hpp"

#include <functional>
#include <memory>

using namespace rcp;
using namespace std::chrono_literals;

namespace {

RequestFn make_mock_fn(std::shared_ptr<mock::Server> srv) {
    return [srv](const Context&, const acf::AcfMessageInfo& req,
                 const std::vector<uint8_t>& payload,
                 acf::AcfMessageInfo& out, std::vector<uint8_t>& out_payload) {
        return srv->dispatch(0, req, payload, out, out_payload);
    };
}

std::shared_ptr<mock::Server> make_server() {
    auto srv = std::make_shared<mock::Server>();
    srv->advance_to_rcp_configured();
    return srv;
}

// fail_fn always fails with a fixed error code, regardless of the request.
// Used to deterministically trigger redundant failover.
RequestFn fail_fn(std::error_code ec) {
    return [ec](const Context&, const acf::AcfMessageInfo&, const std::vector<uint8_t>&,
                acf::AcfMessageInfo&, std::vector<uint8_t>&) { return ec; };
}

acf::AcfMessageInfo gpio_read_request() {
    return acf::make_standard_request(mock::kGpioByteBusId, 0, false, 0);
}

} // namespace

TEST_CASE("redundancy: primary succeeds, standby unused", "[redundancy]") {
    redundancy::RedundantRequestFn rr(make_mock_fn(make_server()), make_mock_fn(make_server()));
    REQUIRE(rr.is_primary_active());

    auto req = gpio_read_request();
    acf::AcfMessageInfo out;
    std::vector<uint8_t> out_payload;
    REQUIRE_FALSE(rr.send(Context{}, req, {}, out, out_payload));
    REQUIRE(rr.is_primary_active());
}

TEST_CASE("redundancy: manual promote switches to standby", "[redundancy]") {
    redundancy::RedundantRequestFn rr(make_mock_fn(make_server()), make_mock_fn(make_server()));

    rr.promote();
    REQUIRE_FALSE(rr.is_primary_active());

    auto req = gpio_read_request();
    acf::AcfMessageInfo out;
    std::vector<uint8_t> out_payload;
    REQUIRE_FALSE(rr.send(Context{}, req, {}, out, out_payload));
}

TEST_CASE("redundancy: double promote returns to primary", "[redundancy]") {
    redundancy::RedundantRequestFn rr(make_mock_fn(make_server()), make_mock_fn(make_server()));

    rr.promote();
    rr.promote();
    REQUIRE(rr.is_primary_active());
}

TEST_CASE("redundancy: auto-promotes standby on ErrClosed", "[redundancy][REQ-RED-002]") {
    redundancy::RedundantRequestFn rr(fail_fn(ErrClosed), make_mock_fn(make_server()));
    REQUIRE(rr.is_primary_active());

    auto req = gpio_read_request();
    acf::AcfMessageInfo out;
    std::vector<uint8_t> out_payload;
    REQUIRE_FALSE(rr.send(Context{}, req, {}, out, out_payload)); // succeeds via standby
    REQUIRE_FALSE(rr.is_primary_active());                         // standby is now active
}

TEST_CASE("redundancy: auto-promotes standby on ErrTimeout", "[redundancy][REQ-RED-003]") {
    redundancy::RedundantRequestFn rr(fail_fn(ErrTimeout), make_mock_fn(make_server()));

    auto req = gpio_read_request();
    acf::AcfMessageInfo out;
    std::vector<uint8_t> out_payload;
    REQUIRE_FALSE(rr.send(Context{}, req, {}, out, out_payload));
    REQUIRE_FALSE(rr.is_primary_active());
}

TEST_CASE("redundancy: auto_promote=false disables automatic failover",
          "[redundancy][REQ-RED-007]") {
    redundancy::Config cfg;
    cfg.auto_promote = false;
    redundancy::RedundantRequestFn rr(fail_fn(ErrClosed), make_mock_fn(make_server()), cfg);

    auto req = gpio_read_request();
    acf::AcfMessageInfo out;
    std::vector<uint8_t> out_payload;
    REQUIRE(rr.send(Context{}, req, {}, out, out_payload) == ErrClosed); // error surfaced, no failover
    REQUIRE(rr.is_primary_active());                                     // still on primary
}

TEST_CASE("redundancy: still on standby after a second consecutive primary failure",
          "[redundancy][REQ-RED-006]") {
    redundancy::RedundantRequestFn rr(fail_fn(ErrClosed), make_mock_fn(make_server()));

    auto req = gpio_read_request();
    acf::AcfMessageInfo out;
    std::vector<uint8_t> out_payload;
    REQUIRE_FALSE(rr.send(Context{}, req, {}, out, out_payload)); // promotes to standby
    REQUIRE_FALSE(rr.is_primary_active());
    REQUIRE_FALSE(rr.send(Context{}, req, {}, out, out_payload)); // standby still serves
    REQUIRE_FALSE(rr.is_primary_active());
}

TEST_CASE("redundancy: RedundantRequestFn is itself usable as an rcp::RequestFn via Adapt()",
          "[redundancy][REQ-RED-008]") {
    redundancy::RedundantRequestFn rr(make_mock_fn(make_server()), make_mock_fn(make_server()));
    // RedundantRequestFn holds a std::mutex, so it is not copyable;
    // Adapt()'s RequestFn parameter would otherwise try to copy it into a
    // std::function. std::ref keeps this call-by-reference.
    auto caller = Adapt(std::ref(rr));

    relay::Message req;
    req.protocol       = relay::Protocol::RCP;
    req.id             = endpoint_id_to_relay_id(mock::kGpioByteBusId);
    req.meta["rcp.op"] = "read";

    auto [resp, ec] = caller->call(relay::Context::with_timeout(1s), req);
    REQUIRE_FALSE(ec);
    REQUIRE(resp.id == req.id);
}
