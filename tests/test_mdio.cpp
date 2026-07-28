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

// ── Address-range validation ──────────────────────────────────────────────────

TEST_CASE("validate_request rejects an out-of-range PHY address", "[mdio][REQ-MDIO-002]") {
    MdioRequest req;
    req.phy_address = kMaxPhyAddress + 1;
    REQUIRE(validate_request(req) == make_error_code(MdioErrc::phy_address_out_of_range));
}

TEST_CASE("validate_request rejects an out-of-range device/register address", "[mdio][REQ-MDIO-002]") {
    MdioRequest req;
    req.device_or_reg = kMaxDeviceOrRegField + 1;
    REQUIRE(validate_request(req) == make_error_code(MdioErrc::device_or_reg_out_of_range));
}

TEST_CASE("validate_request accepts every in-range field", "[mdio][REQ-MDIO-002]") {
    MdioRequest req;
    req.phy_address   = kMaxPhyAddress;
    req.device_or_reg = kMaxDeviceOrRegField;
    REQUIRE_FALSE(validate_request(req));
}

// ── Clause 22: flat register write-then-read round-trip ─────────────────────

TEST_CASE("MdioEndpoint::handle_request Clause22 write then read round-trips the value",
          "[mdio][REQ-MDIO-003]") {
    MdioEndpoint ep;
    MdioRequest write_req;
    write_req.clause        = MdioClause::Clause22;
    write_req.phy_address   = 3;
    write_req.device_or_reg = 5; // Clause22 register address
    write_req.is_write      = true;
    write_req.write_value   = 0xBEEF;

    MdioResponse out;
    REQUIRE_FALSE(ep.handle_request(write_req, out));
    REQUIRE(out.value == 0xBEEF);

    MdioRequest read_req = write_req;
    read_req.is_write     = false;
    read_req.write_value  = 0;
    REQUIRE_FALSE(ep.handle_request(read_req, out));
    REQUIRE(out.value == 0xBEEF);
}

TEST_CASE("MdioEndpoint::handle_request reads an unwritten register as zero", "[mdio][REQ-MDIO-003]") {
    MdioEndpoint ep;
    MdioRequest req;
    req.clause        = MdioClause::Clause22;
    req.phy_address    = 1;
    req.device_or_reg  = 1;
    req.is_write       = false;

    MdioResponse out;
    REQUIRE_FALSE(ep.handle_request(req, out));
    REQUIRE(out.value == 0);
}

// ── Clause 45: device+register addressing distinct from Clause 22 ───────────

TEST_CASE("MdioEndpoint::handle_request Clause45 keys on register_address in addition to "
          "device_or_reg, distinct from Clause22's flat address space",
          "[mdio][REQ-MDIO-004]") {
    MdioEndpoint ep;

    MdioRequest c22_write;
    c22_write.clause        = MdioClause::Clause22;
    c22_write.phy_address    = 2;
    c22_write.device_or_reg  = 4;
    c22_write.is_write       = true;
    c22_write.write_value    = 0x1111;
    MdioResponse out;
    REQUIRE_FALSE(ep.handle_request(c22_write, out));

    // Same phy_address/device_or_reg bit pattern, but Clause45 with a
    // distinct register_address — must not read back Clause22's value.
    MdioRequest c45_read;
    c45_read.clause           = MdioClause::Clause45;
    c45_read.phy_address      = 2;
    c45_read.device_or_reg    = 4;
    c45_read.register_address = 0x0010;
    c45_read.is_write         = false;
    REQUIRE_FALSE(ep.handle_request(c45_read, out));
    REQUIRE(out.value == 0);

    MdioRequest c45_write = c45_read;
    c45_write.is_write     = true;
    c45_write.write_value  = 0x2222;
    REQUIRE_FALSE(ep.handle_request(c45_write, out));
    REQUIRE(out.value == 0x2222);

    // Clause22's earlier write is unaffected.
    MdioRequest c22_read = c22_write;
    c22_read.is_write     = false;
    REQUIRE_FALSE(ep.handle_request(c22_read, out));
    REQUIRE(out.value == 0x1111);
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
    auto ec = make_error_code(MdioErrc::phy_address_out_of_range);
    REQUIRE(ec.category() == mdio_category());
    REQUIRE_FALSE(ec.message().empty());
}
