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

// ── ACF byte_msg_payload codec (Figure 40/41; issue cpp-RCP-A4-iseled) ───────

TEST_CASE("encode_iseled_request / decode_iseled_request round-trip Instruction/Address/Data",
          "[iseled][REQ-ISELED-002]") {
    IseledRequest req;
    req.instruction = 0x0A; // 4 bits
    req.address      = 0x0ABC; // 12 bits
    req.data         = {0xDE, 0xAD, 0xBE};

    auto buf = encode_iseled_request(req);
    REQUIRE(buf.size() == kIseledRequestFixedLen + req.data.size());

    IseledRequest out;
    REQUIRE_FALSE(decode_iseled_request(buf.data(), buf.size(), out));
    REQUIRE(out.instruction == req.instruction);
    REQUIRE(out.address == req.address);
    REQUIRE(out.data == req.data);
}

TEST_CASE("encode_iseled_request hand-computed expected byte sequence", "[iseled][REQ-ISELED-002]") {
    IseledRequest req;
    req.instruction = 0x3;    // 0b0011
    req.address      = 0x0102; // 0b0001_0000_0010
    req.data         = {0x11, 0x22};

    // byte0 = (instruction[3:0] << 4) | address[11:8] = (0x3 << 4) | 0x1 = 0x31
    // byte1 = address[7:0] = 0x02
    const std::vector<uint8_t> expected{0x31, 0x02, 0x11, 0x22};
    REQUIRE(encode_iseled_request(req) == expected);
}

TEST_CASE("decode_iseled_request rejects a buffer shorter than Instruction+Address",
          "[iseled][REQ-ISELED-002]") {
    uint8_t short_buf[1] = {0x00};
    IseledRequest out;
    auto ec = decode_iseled_request(short_buf, sizeof(short_buf), out);
    REQUIRE(ec == rcp::avtp::make_error_code(rcp::avtp::AvtpErrc::short_buffer));
}

TEST_CASE("encode_iseled_response / decode_iseled_response round-trip Address/Data[11:0]",
          "[iseled][REQ-ISELED-002]") {
    IseledResponse resp;
    resp.address = kIseledFieldMask;    // max 12-bit value
    resp.data    = 0x0ABC;

    auto buf = encode_iseled_response(resp);
    REQUIRE(buf.size() == kIseledResponseLen);

    IseledResponse out;
    REQUIRE_FALSE(decode_iseled_response(buf.data(), buf.size(), out));
    REQUIRE(out.address == resp.address);
    REQUIRE(out.data == resp.data);
}

TEST_CASE("encode_iseled_response hand-computed expected byte sequence", "[iseled][REQ-ISELED-002]") {
    IseledResponse resp;
    resp.address = 0x0102; // 0b0001_0000_0010
    resp.data    = 0x0BEF; // 0b1011_1110_1111

    // byte0 = address[11:4] = 0b0001_0000 = 0x10
    // byte1 = (address[3:0] << 4) | data[11:8] = (0x2 << 4) | 0xB = 0x2B
    // byte2 = data[7:0] = 0xEF
    const std::vector<uint8_t> expected{0x10, 0x2B, 0xEF};
    REQUIRE(encode_iseled_response(resp) == expected);
}

TEST_CASE("decode_iseled_response rejects a buffer not exactly 3 bytes", "[iseled][REQ-ISELED-002]") {
    uint8_t short_buf[2] = {0x00, 0x00};
    IseledResponse out;
    REQUIRE(decode_iseled_response(short_buf, sizeof(short_buf), out));

    uint8_t long_buf[4] = {0x00, 0x00, 0x00, 0x00};
    REQUIRE(decode_iseled_response(long_buf, sizeof(long_buf), out));
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
