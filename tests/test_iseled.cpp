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

// ── Instruction/Address/Data request, Address/Data response shape ───────────

TEST_CASE("IseledEndpoint::transact records the exact request and response fields",
          "[iseled][REQ-ISELED-002]") {
    IseledEndpoint ep;
    IseledRequest req;
    req.instruction = 0x03;
    req.address      = 0x0102;
    req.data         = {0xDE, 0xAD};

    IseledResponse resp;
    resp.address = 0x0102;
    resp.data    = 0x0BEF;

    REQUIRE_FALSE(ep.transact(req, resp));
    REQUIRE(ep.last_request().instruction == 0x03);
    REQUIRE(ep.last_request().address == 0x0102);
    REQUIRE(ep.last_request().data == std::vector<uint8_t>{0xDE, 0xAD});
    REQUIRE(ep.last_response().address == 0x0102);
    REQUIRE(ep.last_response().data == 0x0BEF);
}

// ── 12-bit address / 12-bit data field-width fix ─────────────────────────────
// Verified against the spec's "iseled request format" (Figure 40) and
// "iseled response format" (Figure 41): the address field is 12 bits wide
// in both request and response, and a response's data is a single 12-bit
// value, not an open byte vector.

TEST_CASE("kIseledFieldMask is 12 bits and kIseledInstructionMask is 4 bits",
          "[iseled][REQ-ISELED-003]") {
    REQUIRE(kIseledFieldMask == 0x0FFF);
    REQUIRE(kIseledInstructionMask == 0x0F);
}

TEST_CASE("validate_request accepts an in-range instruction and address",
          "[iseled][REQ-ISELED-003]") {
    IseledRequest req;
    req.instruction = kIseledInstructionMask;
    req.address      = kIseledFieldMask;
    REQUIRE_FALSE(validate_request(req));
}

TEST_CASE("validate_request rejects an instruction wider than 4 bits", "[iseled][REQ-ISELED-003]") {
    IseledRequest req;
    req.instruction = static_cast<uint8_t>(kIseledInstructionMask + 1);
    REQUIRE(validate_request(req) == make_error_code(IseledErrc::field_out_of_range));
}

TEST_CASE("validate_request rejects an address wider than 12 bits", "[iseled][REQ-ISELED-003]") {
    IseledRequest req;
    req.address = static_cast<uint16_t>(kIseledFieldMask + 1);
    REQUIRE(validate_request(req) == make_error_code(IseledErrc::field_out_of_range));
}

TEST_CASE("validate_response accepts an in-range address and data", "[iseled][REQ-ISELED-003]") {
    IseledResponse resp;
    resp.address = kIseledFieldMask;
    resp.data    = kIseledFieldMask;
    REQUIRE_FALSE(validate_response(resp));
}

TEST_CASE("validate_response rejects an address wider than 12 bits", "[iseled][REQ-ISELED-003]") {
    IseledResponse resp;
    resp.address = static_cast<uint16_t>(kIseledFieldMask + 1);
    REQUIRE(validate_response(resp) == make_error_code(IseledErrc::field_out_of_range));
}

TEST_CASE("validate_response rejects a data value wider than 12 bits", "[iseled][REQ-ISELED-003]") {
    IseledResponse resp;
    resp.data = static_cast<uint16_t>(kIseledFieldMask + 1);
    REQUIRE(validate_response(resp) == make_error_code(IseledErrc::field_out_of_range));
}

TEST_CASE("IseledEndpoint::transact rejects an out-of-range field without recording it",
          "[iseled][REQ-ISELED-003]") {
    IseledEndpoint ep;
    IseledRequest req;
    req.address = static_cast<uint16_t>(kIseledFieldMask + 1);

    IseledResponse resp;
    resp.address = 0x0001;
    resp.data    = 0x0001;

    auto ec = ep.transact(req, resp);
    REQUIRE(ec == make_error_code(IseledErrc::field_out_of_range));
    // Nothing recorded, and no trigger fired for a rejected transaction.
    REQUIRE_FALSE(ep.triggers().has_pending());
}

// ── Trigger signals ───────────────────────────────────────────────────────────

TEST_CASE("IseledEndpoint::transact fires TransferComplete on a valid transaction",
          "[iseled][REQ-ISELED-004]") {
    IseledEndpoint ep;
    ep.triggers().enable(iseled_signal_id(IseledSignal::TransferComplete));

    IseledRequest req;
    IseledResponse resp;
    resp.address = 0x0010;
    resp.data    = 0x0022;

    REQUIRE_FALSE(ep.transact(req, resp));

    auto drained = ep.triggers().drain();
    REQUIRE(drained.size() == 1);
    REQUIRE(drained[0] == iseled_signal_id(IseledSignal::TransferComplete));
}

// ── IseledErrc category sanity ────────────────────────────────────────────────

TEST_CASE("IseledErrc reports a non-empty message in its own category", "[iseled][REQ-ISELED-005]") {
    auto ec = make_error_code(IseledErrc::field_out_of_range);
    REQUIRE(ec.category() == iseled_category());
    REQUIRE_FALSE(ec.message().empty());
}
