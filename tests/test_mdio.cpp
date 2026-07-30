// fusa:test REQ-MDIO-001
// fusa:test REQ-MDIO-002
// fusa:test REQ-MDIO-003
// fusa:test REQ-MDIO-004
// fusa:test REQ-MDIO-005

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

TEST_CASE("MdioEndpoint::handle_request write then read round-trips the value",
          "[mdio][REQ-MDIO-003]") {
    MdioEndpoint ep;
    MdioRequest write_req;
    write_req.mode         = MdioMode::MmdSingleWord;
    write_req.mdio_address = 5;
    write_req.is_write     = true;
    write_req.mdio_payload = 0xBEEF;

    MdioResponse out;
    REQUIRE_FALSE(ep.handle_request(write_req, out));
    REQUIRE(out.mdio_payload == 0xBEEF);

    MdioRequest read_req  = write_req;
    read_req.is_write      = false;
    read_req.mdio_payload  = 0;
    REQUIRE_FALSE(ep.handle_request(read_req, out));
    REQUIRE(out.mdio_payload == 0xBEEF);
}

TEST_CASE("MdioEndpoint::handle_request reads an unwritten register as zero", "[mdio][REQ-MDIO-003]") {
    MdioEndpoint ep;
    MdioRequest req;
    req.mode         = MdioMode::MmsSingleWord;
    req.mdio_address = 1;
    req.is_write      = false;

    MdioResponse out;
    REQUIRE_FALSE(ep.handle_request(req, out));
    REQUIRE(out.mdio_payload == 0);
}

// ── mdio_mode keeps each mode's register space distinct ─────────────────────

TEST_CASE("MdioEndpoint::handle_request keys on mdio_mode in addition to mdio_address, "
          "so different modes at the same address never collide",
          "[mdio][REQ-MDIO-004]") {
    MdioEndpoint ep;

    MdioRequest mmd_write;
    mmd_write.mode         = MdioMode::MmdSingleWord;
    mmd_write.mdio_address = 4;
    mmd_write.is_write     = true;
    mmd_write.mdio_payload = 0x1111;
    MdioResponse out;
    REQUIRE_FALSE(ep.handle_request(mmd_write, out));

    // Same mdio_address, but MmsSingleWord mode — must not read back the
    // MMD write above.
    MdioRequest mms_read;
    mms_read.mode         = MdioMode::MmsSingleWord;
    mms_read.mdio_address = 4;
    mms_read.is_write      = false;
    REQUIRE_FALSE(ep.handle_request(mms_read, out));
    REQUIRE(out.mdio_payload == 0);

    MdioRequest mms_write = mms_read;
    mms_write.is_write     = true;
    mms_write.mdio_payload = 0x2222;
    REQUIRE_FALSE(ep.handle_request(mms_write, out));
    REQUIRE(out.mdio_payload == 0x2222);

    // The earlier MMD write is unaffected.
    MdioRequest mmd_read = mmd_write;
    mmd_read.is_write     = false;
    REQUIRE_FALSE(ep.handle_request(mmd_read, out));
    REQUIRE(out.mdio_payload == 0x1111);
}

TEST_CASE("MdioEndpoint::handle_request fires TransferComplete on every request", "[mdio][REQ-MDIO-004]") {
    MdioEndpoint ep;
    ep.triggers().enable(mdio_signal_id(MdioSignal::TransferComplete));

    MdioRequest req;
    MdioResponse out;
    REQUIRE_FALSE(ep.handle_request(req, out));

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
