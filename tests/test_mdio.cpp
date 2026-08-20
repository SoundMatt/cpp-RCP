// fusa:test REQ-MDIO-001
// fusa:test REQ-MDIO-002
// fusa:test REQ-MDIO-003
// fusa:test REQ-MDIO-004
// fusa:test REQ-MDIO-005
// fusa:test REQ-MDIO-006
// fusa:test REQ-MDIO-007

// Tests for rcp/mdio.hpp — the MDIO endpoint type (ROADMAP.md milestone 51,
// "Remaining Endpoint Types — LIN, CAN (incl. CAN XL), ISELED, MDIO, Wakeup
// Control", v2.7.0).

#include <catch2/catch_test_macros.hpp>
#include <rcp/endpoint.hpp>
#include <rcp/mdio.hpp>

using namespace rcp::mdio;

// ── ep_type id ────────────────────────────────────────────────────────────────

TEST_CASE("MDIO's ep_type id is 0x0D", "[mdio][REQ-MDIO-001]") {
    REQUIRE(rcp::endpoint::kEndpointTypeMdio == 0x0D);
}

// ── mdio_payload width rule (Table 57) ───────────────────────────────────────

TEST_CASE("payload_width_bits is 16 for both MMD sub-modes regardless of mms_is_0_or_1",
          "[mdio][REQ-MDIO-002]") {
    REQUIRE(payload_width_bits(MdioMode::MmdSingleWord, false) == 16);
    REQUIRE(payload_width_bits(MdioMode::MmdSingleWord, true) == 16);
    REQUIRE(payload_width_bits(MdioMode::MmdMultiWord, false) == 16);
    REQUIRE(payload_width_bits(MdioMode::MmdMultiWord, true) == 16);
}

TEST_CASE("payload_width_bits is 16 for MMS single word access", "[mdio][REQ-MDIO-002]") {
    REQUIRE(payload_width_bits(MdioMode::MmsSingleWord, false) == 16);
    REQUIRE(payload_width_bits(MdioMode::MmsSingleWord, true) == 16);
}

TEST_CASE("payload_width_bits is 32 for MMS multi-word access only on MMS0/MMS1",
          "[mdio][REQ-MDIO-002]") {
    REQUIRE(payload_width_bits(MdioMode::MmsMultiWord, true) == 32);
    REQUIRE(payload_width_bits(MdioMode::MmsMultiWord, false) == 16);
}

TEST_CASE("validate_request accepts a payload within its mode's width", "[mdio][REQ-MDIO-002]") {
    MdioRequest req;
    req.mode         = MdioMode::MmdSingleWord;
    req.mdio_payload = 0xFFFF;
    REQUIRE_FALSE(validate_request(req));

    req.mode           = MdioMode::MmsMultiWord;
    req.mms_is_0_or_1  = true;
    req.mdio_payload   = 0xFFFFFFFFu;
    REQUIRE_FALSE(validate_request(req));
}

TEST_CASE("validate_request rejects a payload exceeding its mode's width", "[mdio][REQ-MDIO-002]") {
    MdioRequest req;
    req.mode         = MdioMode::MmdSingleWord;
    req.mdio_payload = 0x10000; // exceeds 16 bits
    REQUIRE(validate_request(req) == make_error_code(MdioErrc::payload_exceeds_mode_width));

    MdioRequest req2;
    req2.mode          = MdioMode::MmsMultiWord;
    req2.mms_is_0_or_1 = false; // 16-bit width for non-MMS0/1
    req2.mdio_payload  = 0x10000;
    REQUIRE(validate_request(req2) == make_error_code(MdioErrc::payload_exceeds_mode_width));
}

// ── Register write-then-read round-trip, keyed on (mode, mdio_address) ──────

TEST_CASE("MdioEndpoint::transact write then read round-trips the value",
          "[mdio][REQ-MDIO-003]") {
    MdioEndpoint ep;
    MdioRequest write_req;
    write_req.mode         = MdioMode::MmdSingleWord;
    write_req.mdio_address = 5;
    write_req.is_write     = true;
    write_req.mdio_payload = 0xBEEF;

    MdioResponse out;
    REQUIRE_FALSE(ep.transact(write_req, out));
    REQUIRE(out.mdio_payload == 0xBEEF);

    MdioRequest read_req  = write_req;
    read_req.is_write      = false;
    read_req.mdio_payload  = 0;
    REQUIRE_FALSE(ep.transact(read_req, out));
    REQUIRE(out.mdio_payload == 0xBEEF);
}

TEST_CASE("MdioEndpoint::transact reads an unwritten register as zero", "[mdio][REQ-MDIO-003]") {
    MdioEndpoint ep;
    MdioRequest req;
    req.mode         = MdioMode::MmsSingleWord;
    req.mdio_address = 1;
    req.is_write      = false;

    MdioResponse out;
    REQUIRE_FALSE(ep.transact(req, out));
    REQUIRE(out.mdio_payload == 0);
}

// ── mdio_mode keeps each mode's register space distinct ─────────────────────

TEST_CASE("MdioEndpoint::transact keys on mdio_mode in addition to mdio_address, "
          "so different modes at the same address never collide",
          "[mdio][REQ-MDIO-004]") {
    MdioEndpoint ep;

    MdioRequest mmd_write;
    mmd_write.mode         = MdioMode::MmdSingleWord;
    mmd_write.mdio_address = 4;
    mmd_write.is_write     = true;
    mmd_write.mdio_payload = 0x1111;
    MdioResponse out;
    REQUIRE_FALSE(ep.transact(mmd_write, out));

    // Same mdio_address, but MmsSingleWord mode — must not read back the
    // MMD write above.
    MdioRequest mms_read;
    mms_read.mode         = MdioMode::MmsSingleWord;
    mms_read.mdio_address = 4;
    mms_read.is_write      = false;
    REQUIRE_FALSE(ep.transact(mms_read, out));
    REQUIRE(out.mdio_payload == 0);

    MdioRequest mms_write = mms_read;
    mms_write.is_write     = true;
    mms_write.mdio_payload = 0x2222;
    REQUIRE_FALSE(ep.transact(mms_write, out));
    REQUIRE(out.mdio_payload == 0x2222);

    // The earlier MMD write is unaffected.
    MdioRequest mmd_read = mmd_write;
    mmd_read.is_write     = false;
    REQUIRE_FALSE(ep.transact(mmd_read, out));
    REQUIRE(out.mdio_payload == 0x1111);
}

TEST_CASE("MdioEndpoint::transact fires TransferComplete on every request", "[mdio][REQ-MDIO-004]") {
    MdioEndpoint ep;
    ep.triggers().enable(mdio_signal_id(MdioSignal::TransferComplete));

    MdioRequest req;
    MdioResponse out;
    REQUIRE_FALSE(ep.transact(req, out));

    auto drained = ep.triggers().drain();
    REQUIRE(drained.size() == 1);
    REQUIRE(drained[0] == mdio_signal_id(MdioSignal::TransferComplete));
}

// ── MdioErrc category sanity ───────────────────────────────────────────────────

TEST_CASE("MdioErrc reports a non-empty message in its own category", "[mdio][REQ-MDIO-005]") {
    auto ec = make_error_code(MdioErrc::payload_exceeds_mode_width);
    REQUIRE(ec.category() == mdio_category());
    REQUIRE_FALSE(ec.message().empty());
}

// ── Table 33 Row 2 evt[2:0] validation (handle_request) ─────────────────────

TEST_CASE("MdioEndpoint::handle_request delegates a Plain (evt[2:0]==000b) request to transact()",
          "[mdio][REQ-MDIO-006]") {
    MdioEndpoint ep;
    MdioRequest req;
    req.mode         = MdioMode::MmdSingleWord;
    req.mdio_address = 5;
    req.is_write      = true;
    req.mdio_payload  = 0xBEEF;

    MdioResponse out;
    auto ec = ep.handle_request(/*evt_op=*/0, req, out);
    REQUIRE_FALSE(ec);
    REQUIRE(out.mdio_payload == 0xBEEF);
    REQUIRE(ep.last_request().mdio_address == 5);
}

TEST_CASE("MdioEndpoint::handle_request rejects every reserved evt[2:0] value (001b-110b) "
          "without recording anything",
          "[mdio][REQ-MDIO-006]") {
    for (uint8_t evt_op = 1; evt_op <= 6; ++evt_op) {
        MdioEndpoint ep;
        MdioRequest req;
        req.mode          = MdioMode::MmdSingleWord;
        req.mdio_address  = 9;
        req.is_write      = true;
        req.mdio_payload  = 0x1234;

        MdioResponse out;
        auto ec = ep.handle_request(evt_op, req, out);
        REQUIRE(ec == rcp::endpoint::make_error_code(rcp::endpoint::EndpointErrc::reserved_evt_row2));
        // A rejected reserved evt must not record anything — last_request()
        // stays at its default-constructed value, no register is written,
        // and no trigger fires.
        REQUIRE(ep.last_request().mdio_address == 0);
        REQUIRE_FALSE(ep.triggers().has_pending());

        // The register that would have been written must genuinely be
        // untouched: a subsequent Plain read at the same address must not
        // observe req.mdio_payload above.
        MdioRequest read_back = req;
        read_back.is_write     = false;
        read_back.mdio_payload = 0;
        REQUIRE_FALSE(ep.handle_request(/*evt_op=*/0, read_back, out));
        REQUIRE(out.mdio_payload == 0);
    }
}

TEST_CASE("MdioEndpoint::handle_request reports config_write_not_supported for evt[2:0]==111b "
          "without crashing or recording anything",
          "[mdio][REQ-MDIO-007]") {
    MdioEndpoint ep;
    MdioRequest req;
    req.mode          = MdioMode::MmsSingleWord;
    req.mdio_address  = 2;
    req.is_write      = true;
    req.mdio_payload  = 0x00FF;

    MdioResponse out;
    auto ec = ep.handle_request(/*evt_op=*/7, req, out);
    REQUIRE(ec == make_error_code(MdioErrc::config_write_not_supported));
    REQUIRE(ep.last_request().mdio_address == 0);
    REQUIRE_FALSE(ep.triggers().has_pending());
}

TEST_CASE("MdioEndpoint::handle_request masks evt_op down to 3 bits before classifying",
          "[mdio][REQ-MDIO-006]") {
    MdioEndpoint ep;
    MdioRequest req;
    MdioResponse out;
    REQUIRE_FALSE(ep.handle_request(/*evt_op=*/0xF8, req, out)); // low 3 bits 000 -> Plain
    auto ec = ep.handle_request(/*evt_op=*/0xF9, req, out);      // low 3 bits 001 -> Reserved
    REQUIRE(ec == rcp::endpoint::make_error_code(rcp::endpoint::EndpointErrc::reserved_evt_row2));
}

TEST_CASE("MdioEndpoint::handle_request Reserved/ConfigWrite classification is independent of "
          "mode/mdio_address/mdio_payload — evt[2:0] carries no field-value selector",
          "[mdio][REQ-MDIO-006]") {
    // Guards against confusing Table 33's evt[2:0] classification with the
    // request's own mode/mdio_address/mdio_payload fields (see
    // handle_request's own header comment): a Reserved/ConfigWrite evt is
    // rejected identically no matter what those fields carry.
    MdioEndpoint ep;
    MdioRequest req;
    req.mode          = MdioMode::MmsMultiWord;
    req.mms_is_0_or_1 = true;
    req.mdio_address  = 0xFFFF;
    req.is_write      = true;
    req.mdio_payload  = 0xFFFFFFFFu;

    MdioResponse out;
    REQUIRE(ep.handle_request(/*evt_op=*/3, req, out) ==
            rcp::endpoint::make_error_code(rcp::endpoint::EndpointErrc::reserved_evt_row2));
    REQUIRE(ep.handle_request(/*evt_op=*/7, req, out) ==
            make_error_code(MdioErrc::config_write_not_supported));
}

TEST_CASE("MdioErrc::config_write_not_supported reports a non-empty message in its own category",
          "[mdio][REQ-MDIO-007]") {
    auto ec = make_error_code(MdioErrc::config_write_not_supported);
    REQUIRE(ec.category() == mdio_category());
    REQUIRE_FALSE(ec.message().empty());
}
