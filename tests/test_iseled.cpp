// fusa:test REQ-ISELED-001
// fusa:test REQ-ISELED-002
// fusa:test REQ-ISELED-003
// fusa:test REQ-ISELED-004
// fusa:test REQ-ISELED-005

// Tests for rcp/iseled.hpp — the ISELED endpoint type (ROADMAP.md milestone
// 51, "Remaining Endpoint Types — LIN, CAN (incl. CAN XL), ISELED, MDIO,
// Wakeup Control", v2.7.0).

#include <catch2/catch_test_macros.hpp>
#include <rcp/endpoint.hpp>
#include <rcp/iseled.hpp>

using namespace rcp::iseled;

// ── ep_type id ────────────────────────────────────────────────────────────────

TEST_CASE("ISELED's ep_type id is 0x0C", "[iseled][REQ-ISELED-001]") {
    REQUIRE(rcp::endpoint::kEndpointTypeIseled == 0x0C);
}

// ── Instruction/Address/Data request, Address/Data(/CRC) response shape ─────

TEST_CASE("IseledEndpoint::transact records the exact request and response fields",
          "[iseled][REQ-ISELED-002]") {
    IseledEndpoint ep;
    IseledRequest req;
    req.instruction = 0x03;
    req.address      = 0x0102;
    req.data         = {0xDE, 0xAD};

    IseledResponse resp;
    resp.address = 0x0102;
    resp.data    = {0xBE, 0xEF};
    // no native_crc — optional field left absent

    REQUIRE_FALSE(ep.transact(req, resp));
    REQUIRE(ep.last_request().instruction == 0x03);
    REQUIRE(ep.last_request().address == 0x0102);
    REQUIRE(ep.last_request().data == std::vector<uint8_t>{0xDE, 0xAD});
    REQUIRE(ep.last_response().address == 0x0102);
    REQUIRE(ep.last_response().data == std::vector<uint8_t>{0xBE, 0xEF});
    REQUIRE_FALSE(ep.last_response().native_crc.has_value());
}

// ── Native CRC: additional to, not a replacement for, the general E2E CRC ───

TEST_CASE("compute_native_crc8/verify_native_crc round-trip a correct native CRC",
          "[iseled][REQ-ISELED-003]") {
    IseledResponse resp;
    resp.address = 0x00AB;
    resp.data    = {0x01, 0x02, 0x03};
    resp.native_crc = compute_native_crc8(crc_coverage_bytes(resp));

    REQUIRE_FALSE(verify_native_crc(resp));
}

TEST_CASE("verify_native_crc reports native_crc_mismatch for a tampered native CRC",
          "[iseled][REQ-ISELED-003]") {
    IseledResponse resp;
    resp.address = 0x00AB;
    resp.data    = {0x01, 0x02, 0x03};
    resp.native_crc = static_cast<uint8_t>(compute_native_crc8(crc_coverage_bytes(resp)) ^ 0xFFu);

    REQUIRE(verify_native_crc(resp) == make_error_code(IseledErrc::native_crc_mismatch));
}

TEST_CASE("verify_native_crc succeeds trivially when native_crc is absent (it is optional)",
          "[iseled][REQ-ISELED-003]") {
    IseledResponse resp;
    resp.address = 0x0001;
    resp.data    = {0xFF};
    REQUIRE_FALSE(verify_native_crc(resp));
}

TEST_CASE("IseledEndpoint::transact propagates a native CRC mismatch and fires NativeCrcError",
          "[iseled][REQ-ISELED-003]") {
    IseledEndpoint ep;
    ep.triggers().enable(iseled_signal_id(IseledSignal::TransferComplete));
    ep.triggers().enable(iseled_signal_id(IseledSignal::NativeCrcError));

    IseledRequest req;
    IseledResponse resp;
    resp.address = 0x0010;
    resp.data    = {0x22};
    resp.native_crc = static_cast<uint8_t>(compute_native_crc8(crc_coverage_bytes(resp)) ^ 0x01u);

    auto ec = ep.transact(req, resp);
    REQUIRE(ec == make_error_code(IseledErrc::native_crc_mismatch));

    auto drained = ep.triggers().drain();
    REQUIRE(drained.size() == 1);
    REQUIRE(drained[0] == iseled_signal_id(IseledSignal::NativeCrcError));
}

// ── Trigger signals ───────────────────────────────────────────────────────────

TEST_CASE("IseledEndpoint::transact fires only TransferComplete on a clean transaction",
          "[iseled][REQ-ISELED-004]") {
    IseledEndpoint ep;
    ep.triggers().enable(iseled_signal_id(IseledSignal::TransferComplete));
    ep.triggers().enable(iseled_signal_id(IseledSignal::NativeCrcError));

    IseledRequest req;
    IseledResponse resp;
    resp.address = 0x0010;
    resp.data    = {0x22};
    resp.native_crc = compute_native_crc8(crc_coverage_bytes(resp));

    REQUIRE_FALSE(ep.transact(req, resp));

    auto drained = ep.triggers().drain();
    REQUIRE(drained.size() == 1);
    REQUIRE(drained[0] == iseled_signal_id(IseledSignal::TransferComplete));
}

// ── IseledErrc category sanity ────────────────────────────────────────────────

TEST_CASE("IseledErrc reports a non-empty message in its own category", "[iseled][REQ-ISELED-005]") {
    auto ec = make_error_code(IseledErrc::native_crc_mismatch);
    REQUIRE(ec.category() == iseled_category());
    REQUIRE_FALSE(ec.message().empty());
}
