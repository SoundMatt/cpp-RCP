// fusa:test REQ-MOCK-001
// fusa:test REQ-MOCK-002
// fusa:test REQ-MOCK-003
// fusa:test REQ-MOCK-004
// fusa:test REQ-MOCK-005
// fusa:test REQ-MOCK-006
// fusa:test REQ-MOCK-007
// fusa:test REQ-MOCK-008
// fusa:test REQ-MOCK-009
// fusa:test REQ-MOCK-010
// fusa:test REQ-MOCK-011
// fusa:test REQ-MOCK-012

// Tests for rcp/mock.hpp — the in-process RC Server simulator (ROADMAP.md
// milestone 56, "Test & Simulation Harness Rebuild", v2.12.0). See
// tests/test_legacy_mock.cpp for the pre-replacement Controller/Registry
// coverage that used to live in this file.

#include <catch2/catch_test_macros.hpp>
#include <rcp/mock.hpp>

using namespace rcp;
using rcp::endpoint::WriteSemantics;

namespace {

acf::AcfMessageInfo standard_request(avtp::ByteBusId bus_id, bool write, uint8_t evt_op = 0,
                                      bool evt_ack = false) {
    auto req = acf::make_standard_request(bus_id, /*transaction_num=*/1, write, /*read_size=*/0);
    req.evt_op  = evt_op;
    req.evt_ack = evt_ack;
    return req;
}

} // namespace

// ── Construction / register map ──────────────────────────────────────────────

TEST_CASE("Server starts HW_UNCONFIGURED with a three-endpoint register map",
          "[mock][REQ-MOCK-001]") {
    mock::Server server;
    REQUIRE(server.lifecycle().state() == lifecycle::ServerState::HwUnconfigured);
    REQUIRE(server.registers().endpoint_count == 3);
    REQUIRE(server.registers().generic_configs.size() == 3);
    REQUIRE(server.registers().functional_configs.size() == 3);
    REQUIRE(server.registers().ep_id_mapping.size() == 3);
    REQUIRE(server.registers().ep_id_mapping[0].ep_id == mock::kGpioEndpointId);
    REQUIRE(server.registers().ep_id_mapping[0].byte_bus_id == mock::kGpioByteBusId);
    REQUIRE(server.registers().ep_id_mapping[1].ep_id == mock::kSpiEndpointId);
    REQUIRE(server.registers().ep_id_mapping[1].byte_bus_id == mock::kSpiByteBusId);
    REQUIRE(server.registers().ep_id_mapping[2].ep_id == mock::kI2cEndpointId);
    REQUIRE(server.registers().ep_id_mapping[2].byte_bus_id == mock::kI2cByteBusId);
}

TEST_CASE("advance_to_rcp_configured drives the lifecycle straight to RCP_CONFIGURED",
          "[mock][REQ-MOCK-002]") {
    mock::Server server;
    REQUIRE_FALSE(server.advance_to_rcp_configured());
    REQUIRE(server.lifecycle().state() == lifecycle::ServerState::RcpConfigured);
}

// ── EP0 ───────────────────────────────────────────────────────────────────────

TEST_CASE("EP0 read answers the register map's magic number for any client",
          "[mock][REQ-MOCK-003]") {
    mock::Server server;
    for (size_t client : {size_t{0}, size_t{1}, size_t{42}}) {
        auto req = standard_request(regmap::kEp0, /*write=*/false);
        acf::AcfMessageInfo resp;
        std::vector<uint8_t> resp_payload;
        auto ec = server.dispatch(client, req, {}, resp, resp_payload);
        REQUIRE_FALSE(ec);
        REQUIRE(acf::response_kind_of(resp) == acf::ResponseKind::ReadResponse);
        REQUIRE(resp_payload.size() == mock::kEp0PartialReadLen);
        REQUIRE(avtp::detail::get_u32(resp_payload.data()) == server.registers().magic);
    }
}

TEST_CASE("EP0 write via dispatch() is always rejected", "[mock][REQ-MOCK-004]") {
    mock::Server server;
    REQUIRE_FALSE(server.ep0().claim_root_client(/*client=*/1));

    auto req = standard_request(regmap::kEp0, /*write=*/true);
    acf::AcfMessageInfo resp;
    std::vector<uint8_t> resp_payload;
    auto ec = server.dispatch(/*client=*/1, req, {}, resp, resp_payload);
    REQUIRE(ec == regmap::make_error_code(regmap::RegMapErrc::request_rejected));
    REQUIRE(acf::response_kind_of(resp) == acf::ResponseKind::ErrorResponse);
    // cpp-RCP-02: the error response's byte_msg_payload must carry Table 27's
    // numeric error code (RequestRejected = 11), not be left empty.
    REQUIRE(resp_payload == std::vector<uint8_t>{static_cast<uint8_t>(acf::WireErrorCode::RequestRejected)});
}

TEST_CASE("write_whole_map requires the root client", "[mock][REQ-MOCK-005]") {
    mock::Server server;
    REQUIRE_FALSE(server.ep0().claim_root_client(/*client=*/1));
    auto claim_ec = server.ep0().claim_root_client(/*client=*/2);
    REQUIRE(claim_ec == regmap::make_error_code(regmap::RegMapErrc::request_rejected));

    regmap::RegisterMap replacement = server.registers();
    replacement.vendor_id = 0x1234;

    auto write_ec = server.ep0().write_whole_map(/*client=*/2, replacement);
    REQUIRE(write_ec == regmap::make_error_code(regmap::RegMapErrc::unauthorized_access));
    REQUIRE_FALSE(server.ep0().write_whole_map(/*client=*/1, replacement));
    REQUIRE(server.registers().vendor_id == 0x1234);
}

// ── Operational-request gating ───────────────────────────────────────────────

TEST_CASE("GPIO/SPI requests are rejected before RCP_CONFIGURED", "[mock][REQ-MOCK-006]") {
    mock::Server server;
    REQUIRE(server.lifecycle().state() == lifecycle::ServerState::HwUnconfigured);
    auto rejected = regmap::make_error_code(regmap::RegMapErrc::request_rejected);

    auto gpio_req = standard_request(mock::kGpioByteBusId, /*write=*/false);
    acf::AcfMessageInfo resp;
    std::vector<uint8_t> resp_payload;
    REQUIRE(server.dispatch(0, gpio_req, {}, resp, resp_payload) == rejected);
    REQUIRE(acf::response_kind_of(resp) == acf::ResponseKind::ErrorResponse);

    auto spi_req = standard_request(mock::kSpiByteBusId, /*write=*/false);
    REQUIRE(server.dispatch(0, spi_req, {}, resp, resp_payload) == rejected);
}

// ── GPIO ──────────────────────────────────────────────────────────────────────

TEST_CASE("GPIO write applies evt[2:0] semantics and read reflects the new state",
          "[mock][REQ-MOCK-007]") {
    mock::Server server;
    REQUIRE_FALSE(server.advance_to_rcp_configured());

    // Pins 0-3 must be configured as output before a write to them can take
    // effect (REQ-GPIO-009) — every pin defaults to input.
    auto reconfig_req = standard_request(mock::kGpioByteBusId, /*write=*/true,
                                          static_cast<uint8_t>(WriteSemantics::Reconfigure));
    auto reconfig_payload = gpio::encode_gpio_payload(0x0000'000F);
    acf::AcfMessageInfo reconfig_resp;
    std::vector<uint8_t> reconfig_resp_payload;
    REQUIRE_FALSE(
        server.dispatch(0, reconfig_req, reconfig_payload, reconfig_resp, reconfig_resp_payload));

    auto write_req = standard_request(mock::kGpioByteBusId, /*write=*/true,
                                       static_cast<uint8_t>(WriteSemantics::Or));
    auto payload = gpio::encode_gpio_payload(0x0000'000F);
    acf::AcfMessageInfo resp;
    std::vector<uint8_t> resp_payload;
    REQUIRE_FALSE(server.dispatch(0, write_req, payload, resp, resp_payload));
    REQUIRE(acf::response_kind_of(resp) == acf::ResponseKind::WriteResponse);
    gpio::PinMask written = 0;
    REQUIRE_FALSE(gpio::decode_gpio_payload(resp_payload.data(), resp_payload.size(), written));
    REQUIRE(written == 0x0000'000F);

    auto read_req = standard_request(mock::kGpioByteBusId, /*write=*/false);
    REQUIRE_FALSE(server.dispatch(0, read_req, {}, resp, resp_payload));
    REQUIRE(acf::response_kind_of(resp) == acf::ResponseKind::ReadResponse);
    gpio::PinMask read_back = 0;
    REQUIRE_FALSE(gpio::decode_gpio_payload(resp_payload.data(), resp_payload.size(), read_back));
    REQUIRE(read_back == 0x0000'000F);
    REQUIRE(server.gpio().read() == 0x0000'000F);
}

TEST_CASE("GPIO write with evt_ack set produces an Acknowledge response",
          "[mock][REQ-MOCK-008]") {
    mock::Server server;
    REQUIRE_FALSE(server.advance_to_rcp_configured());

    auto req = standard_request(mock::kGpioByteBusId, /*write=*/true,
                                 static_cast<uint8_t>(WriteSemantics::Replace), /*evt_ack=*/true);
    auto payload = gpio::encode_gpio_payload(0x1);
    acf::AcfMessageInfo resp;
    std::vector<uint8_t> resp_payload;
    REQUIRE_FALSE(server.dispatch(0, req, payload, resp, resp_payload));
    REQUIRE(acf::response_kind_of(resp) == acf::ResponseKind::Acknowledge);
}

// ── SPI ───────────────────────────────────────────────────────────────────────

TEST_CASE("SPI transfer answers with the scripted POCI-in bytes for the addressed channel",
          "[mock][REQ-MOCK-009]") {
    mock::Server server;
    REQUIRE_FALSE(server.advance_to_rcp_configured());

    server.set_spi_poci(/*channel=*/2, {0xDE, 0xAD, 0xBE, 0xEF});

    auto req = standard_request(mock::kSpiByteBusId, /*write=*/true, /*evt_op=*/2);
    std::vector<uint8_t> pico_out{0x01, 0x02};
    acf::AcfMessageInfo resp;
    std::vector<uint8_t> resp_payload;
    REQUIRE_FALSE(server.dispatch(0, req, pico_out, resp, resp_payload));
    REQUIRE(acf::response_kind_of(resp) == acf::ResponseKind::ReadResponse);
    REQUIRE(resp_payload == std::vector<uint8_t>{0xDE, 0xAD, 0xBE, 0xEF});
    REQUIRE(server.spi().last_sent(2) == pico_out);
}

// ── I2C ───────────────────────────────────────────────────────────────────────
// Table 30/33 Row 2 evt[2:0] validation pilot: I2cEndpoint wired into
// dispatch() at byte_bus_id 3.

TEST_CASE("I2C plain request (evt[2:0]==000b) answers with the scripted response bytes",
          "[mock][REQ-MOCK-011]") {
    mock::Server server;
    REQUIRE_FALSE(server.advance_to_rcp_configured());

    server.set_i2c_response({0xDE, 0xAD});

    auto req = standard_request(mock::kI2cByteBusId, /*write=*/true, /*evt_op=*/0);
    std::vector<uint8_t> out_bytes{0xA0, 0x10};
    acf::AcfMessageInfo resp;
    std::vector<uint8_t> resp_payload;
    REQUIRE_FALSE(server.dispatch(0, req, out_bytes, resp, resp_payload));
    REQUIRE(acf::response_kind_of(resp) == acf::ResponseKind::ReadResponse);
    REQUIRE(resp_payload == std::vector<uint8_t>{0xDE, 0xAD});
    REQUIRE(server.i2c().last_sent() == out_bytes);
}

TEST_CASE("I2C request with a reserved evt[2:0] (001b-110b) is rejected with wire error code "
          "UNSUPPORTED_CMD and does not touch endpoint state",
          "[mock][REQ-MOCK-011]") {
    mock::Server server;
    REQUIRE_FALSE(server.advance_to_rcp_configured());

    for (uint8_t evt_op = 1; evt_op <= 6; ++evt_op) {
        auto req = standard_request(mock::kI2cByteBusId, /*write=*/true, evt_op);
        acf::AcfMessageInfo resp;
        std::vector<uint8_t> resp_payload;
        auto ec = server.dispatch(0, req, {0xA0}, resp, resp_payload);
        REQUIRE(ec == endpoint::make_error_code(endpoint::EndpointErrc::reserved_evt_row2));
        REQUIRE(acf::response_kind_of(resp) == acf::ResponseKind::ErrorResponse);
        REQUIRE(resp_payload ==
                std::vector<uint8_t>{static_cast<uint8_t>(acf::WireErrorCode::UnsupportedCmd)});
        REQUIRE(server.i2c().last_sent().empty());
    }
}

TEST_CASE("I2C request with evt[2:0]==111b (config-write) is rejected with wire error code "
          "UNSUPPORTED_CMD rather than crashing or being treated as a plain transfer",
          "[mock][REQ-MOCK-011]") {
    mock::Server server;
    REQUIRE_FALSE(server.advance_to_rcp_configured());

    auto req = standard_request(mock::kI2cByteBusId, /*write=*/true, /*evt_op=*/7);
    acf::AcfMessageInfo resp;
    std::vector<uint8_t> resp_payload;
    auto ec = server.dispatch(0, req, {0x00, 0xAB}, resp, resp_payload);
    REQUIRE(ec == i2c::make_error_code(i2c::I2cErrc::config_write_not_supported));
    REQUIRE(acf::response_kind_of(resp) == acf::ResponseKind::ErrorResponse);
    REQUIRE(resp_payload ==
            std::vector<uint8_t>{static_cast<uint8_t>(acf::WireErrorCode::UnsupportedCmd)});
    REQUIRE(server.i2c().last_sent().empty());
}

TEST_CASE("I2C request is rejected before RCP_CONFIGURED, same operational gating as GPIO/SPI",
          "[mock][REQ-MOCK-012]") {
    mock::Server server;
    REQUIRE(server.lifecycle().state() == lifecycle::ServerState::HwUnconfigured);

    auto req = standard_request(mock::kI2cByteBusId, /*write=*/true, /*evt_op=*/0);
    acf::AcfMessageInfo resp;
    std::vector<uint8_t> resp_payload;
    auto ec = server.dispatch(0, req, {0xA0}, resp, resp_payload);
    REQUIRE(ec == regmap::make_error_code(regmap::RegMapErrc::request_rejected));
    REQUIRE(acf::response_kind_of(resp) == acf::ResponseKind::ErrorResponse);
}

// ── Unmapped endpoints ────────────────────────────────────────────────────────

TEST_CASE("dispatch to an unmapped byte_bus_id reports invalid_parameter",
          "[mock][REQ-MOCK-010]") {
    mock::Server server;
    REQUIRE_FALSE(server.advance_to_rcp_configured());

    auto req = standard_request(/*bus_id=*/99, /*write=*/false);
    acf::AcfMessageInfo resp;
    std::vector<uint8_t> resp_payload;
    auto ec = server.dispatch(0, req, {}, resp, resp_payload);
    REQUIRE(ec == regmap::make_error_code(regmap::RegMapErrc::invalid_parameter));
    REQUIRE(acf::response_kind_of(resp) == acf::ResponseKind::ErrorResponse);
    REQUIRE(resp_payload == std::vector<uint8_t>{static_cast<uint8_t>(acf::WireErrorCode::InvalidParameter)});
}

TEST_CASE("A GPIO write with a mis-sized payload is rejected with wire error code INVALID_PARAMETER",
          "[mock][REQ-MOCK-010][REQ-GPIO-001]") {
    // cpp-RCP-02 + cpp-RCP-05-fresh together: decode_gpio_payload now
    // rejects a non-exactly-4-byte buffer, and that rejection's wire
    // error-response payload must carry Table 27's INVALID_PARAMETER (15).
    mock::Server server;
    REQUIRE_FALSE(server.advance_to_rcp_configured());

    auto req = standard_request(mock::kGpioByteBusId, /*write=*/true,
                                 static_cast<uint8_t>(WriteSemantics::Or));
    std::vector<uint8_t> too_long_payload{0x00, 0x00, 0x00, 0x0F, 0xFF};
    acf::AcfMessageInfo   resp;
    std::vector<uint8_t>  resp_payload;
    auto ec = server.dispatch(0, req, too_long_payload, resp, resp_payload);
    REQUIRE(ec);
    REQUIRE(acf::response_kind_of(resp) == acf::ResponseKind::ErrorResponse);
    REQUIRE(resp_payload == std::vector<uint8_t>{static_cast<uint8_t>(acf::WireErrorCode::InvalidParameter)});
}
