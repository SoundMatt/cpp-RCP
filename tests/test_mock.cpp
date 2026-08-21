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
// fusa:test REQ-MOCK-013
// fusa:test REQ-MOCK-014
// fusa:test REQ-MOCK-015
// fusa:test REQ-MOCK-016
// fusa:test REQ-MOCK-017
// fusa:test REQ-MOCK-018
// fusa:test REQ-MOCK-019
// fusa:test REQ-MOCK-020
// fusa:test REQ-MOCK-021
// fusa:test REQ-MOCK-022
// fusa:test REQ-MOCK-023
// fusa:test REQ-MOCK-024
// fusa:test REQ-MOCK-025
// fusa:test REQ-MOCK-026

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

TEST_CASE("Server starts HW_UNCONFIGURED with a ten-endpoint register map",
          "[mock][REQ-MOCK-001]") {
    mock::Server server;
    REQUIRE(server.lifecycle().state() == lifecycle::ServerState::HwUnconfigured);
    REQUIRE(server.registers().endpoint_count == 10);
    REQUIRE(server.registers().generic_configs.size() == 10);
    REQUIRE(server.registers().functional_configs.size() == 10);
    REQUIRE(server.registers().ep_id_mapping.size() == 10);
    REQUIRE(server.registers().ep_id_mapping[0].ep_id == mock::kGpioEndpointId);
    REQUIRE(server.registers().ep_id_mapping[0].byte_bus_id == mock::kGpioByteBusId);
    REQUIRE(server.registers().ep_id_mapping[1].ep_id == mock::kSpiEndpointId);
    REQUIRE(server.registers().ep_id_mapping[1].byte_bus_id == mock::kSpiByteBusId);
    REQUIRE(server.registers().ep_id_mapping[2].ep_id == mock::kI2cEndpointId);
    REQUIRE(server.registers().ep_id_mapping[2].byte_bus_id == mock::kI2cByteBusId);
    REQUIRE(server.registers().ep_id_mapping[3].ep_id == mock::kAdcEndpointId);
    REQUIRE(server.registers().ep_id_mapping[3].byte_bus_id == mock::kAdcByteBusId);
    REQUIRE(server.registers().ep_id_mapping[4].ep_id == mock::kPwmInEndpointId);
    REQUIRE(server.registers().ep_id_mapping[4].byte_bus_id == mock::kPwmInByteBusId);
    REQUIRE(server.registers().ep_id_mapping[5].ep_id == mock::kLinEndpointId);
    REQUIRE(server.registers().ep_id_mapping[5].byte_bus_id == mock::kLinByteBusId);
    REQUIRE(server.registers().ep_id_mapping[6].ep_id == mock::kCanEndpointId);
    REQUIRE(server.registers().ep_id_mapping[6].byte_bus_id == mock::kCanByteBusId);
    REQUIRE(server.registers().ep_id_mapping[7].ep_id == mock::kUartEndpointId);
    REQUIRE(server.registers().ep_id_mapping[7].byte_bus_id == mock::kUartByteBusId);
    REQUIRE(server.registers().ep_id_mapping[8].ep_id == mock::kIseledEndpointId);
    REQUIRE(server.registers().ep_id_mapping[8].byte_bus_id == mock::kIseledByteBusId);
    REQUIRE(server.registers().ep_id_mapping[9].ep_id == mock::kMdioEndpointId);
    REQUIRE(server.registers().ep_id_mapping[9].byte_bus_id == mock::kMdioByteBusId);
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

// ── ADC ───────────────────────────────────────────────────────────────────────
// Table 30/33 Row 2 evt[2:0] validation, second endpoint type after I2C:
// AdcEndpoint wired into dispatch() at byte_bus_id 4.

TEST_CASE("ADC plain request (evt[2:0]==000b) answers with the scripted sample value",
          "[mock][REQ-MOCK-013]") {
    mock::Server server;
    REQUIRE_FALSE(server.advance_to_rcp_configured());

    server.set_adc_response({0xBEEF});

    auto req = standard_request(mock::kAdcByteBusId, /*write=*/false, /*evt_op=*/0);
    acf::AcfMessageInfo resp;
    std::vector<uint8_t> resp_payload;
    REQUIRE_FALSE(server.dispatch(0, req, {}, resp, resp_payload));
    REQUIRE(acf::response_kind_of(resp) == acf::ResponseKind::ReadResponse);
    REQUIRE(resp_payload == std::vector<uint8_t>{0xBE, 0xEF});
}

TEST_CASE("ADC plain requests consume scripted samples in FIFO order without auto-refill",
          "[mock][REQ-MOCK-013]") {
    mock::Server server;
    REQUIRE_FALSE(server.advance_to_rcp_configured());

    server.set_adc_response({0x0001, 0x0002});

    auto req = standard_request(mock::kAdcByteBusId, /*write=*/false, /*evt_op=*/0);
    acf::AcfMessageInfo resp;
    std::vector<uint8_t> resp_payload;

    REQUIRE_FALSE(server.dispatch(0, req, {}, resp, resp_payload));
    REQUIRE(resp_payload == std::vector<uint8_t>{0x00, 0x01});

    REQUIRE_FALSE(server.dispatch(0, req, {}, resp, resp_payload));
    REQUIRE(resp_payload == std::vector<uint8_t>{0x00, 0x02});

    // Third dispatch: no scripted sample left -> AdcErrc::no_signal.
    auto ec = server.dispatch(0, req, {}, resp, resp_payload);
    REQUIRE(ec == adc::make_error_code(adc::AdcErrc::no_signal));
    REQUIRE(acf::response_kind_of(resp) == acf::ResponseKind::ErrorResponse);
}

TEST_CASE("ADC request with a reserved evt[2:0] (001b-110b) is rejected with wire error code "
          "UNSUPPORTED_CMD and does not consume a scripted sample",
          "[mock][REQ-MOCK-013]") {
    mock::Server server;
    REQUIRE_FALSE(server.advance_to_rcp_configured());
    server.set_adc_response({0x1234});

    for (uint8_t evt_op = 1; evt_op <= 6; ++evt_op) {
        auto req = standard_request(mock::kAdcByteBusId, /*write=*/false, evt_op);
        acf::AcfMessageInfo resp;
        std::vector<uint8_t> resp_payload;
        auto ec = server.dispatch(0, req, {}, resp, resp_payload);
        REQUIRE(ec == endpoint::make_error_code(endpoint::EndpointErrc::reserved_evt_row2));
        REQUIRE(acf::response_kind_of(resp) == acf::ResponseKind::ErrorResponse);
        REQUIRE(resp_payload ==
                std::vector<uint8_t>{static_cast<uint8_t>(acf::WireErrorCode::UnsupportedCmd)});
    }

    // The scripted sample must still be there — no reserved evt consumed it.
    auto req = standard_request(mock::kAdcByteBusId, /*write=*/false, /*evt_op=*/0);
    acf::AcfMessageInfo resp;
    std::vector<uint8_t> resp_payload;
    REQUIRE_FALSE(server.dispatch(0, req, {}, resp, resp_payload));
    REQUIRE(resp_payload == std::vector<uint8_t>{0x12, 0x34});
}

TEST_CASE("ADC request with evt[2:0]==111b (config-write) and a malformed payload is rejected with "
          "wire error code INVALID_PARAMETER rather than crashing or being treated as a plain read",
          "[mock][REQ-MOCK-013]") {
    // Phase 3: adc.hpp's evt[2:0]==111b configuration-write path
    // (adc::apply_reconfig) is now genuinely implemented, so this mock's
    // own dispatch_adc() routes ConfigWrite requests there instead of
    // rejecting them outright — see dispatch_adc's own comment. A payload
    // with no address+data octet is still rejected, now with
    // AdcErrc::reconfig_short / INVALID_PARAMETER rather than the old
    // (pre-Phase-3) "not implemented at all" UNSUPPORTED_CMD.
    mock::Server server;
    REQUIRE_FALSE(server.advance_to_rcp_configured());

    auto req = standard_request(mock::kAdcByteBusId, /*write=*/true, /*evt_op=*/7);
    acf::AcfMessageInfo resp;
    std::vector<uint8_t> resp_payload;
    auto ec = server.dispatch(0, req, {}, resp, resp_payload);
    REQUIRE(ec == adc::make_error_code(adc::AdcErrc::reconfig_short));
    REQUIRE(acf::response_kind_of(resp) == acf::ResponseKind::ErrorResponse);
    REQUIRE(resp_payload ==
            std::vector<uint8_t>{static_cast<uint8_t>(acf::WireErrorCode::InvalidParameter)});
}

TEST_CASE("ADC request with evt[2:0]==111b (config-write) and a well-formed payload actually "
          "patches this server's own ADC functional config",
          "[mock][REQ-MOCK-013]") {
    mock::Server server;
    REQUIRE_FALSE(server.advance_to_rcp_configured());

    // Address 0x0008 (adc_base_clk_divider) + one data octet.
    std::vector<uint8_t> payload{0x00, 0x08, 0x09};
    auto req = standard_request(mock::kAdcByteBusId, /*write=*/true, /*evt_op=*/7);
    acf::AcfMessageInfo resp;
    std::vector<uint8_t> resp_payload;
    REQUIRE_FALSE(server.dispatch(0, req, payload, resp, resp_payload));
    REQUIRE(acf::response_kind_of(resp) == acf::ResponseKind::WriteResponse);
}

TEST_CASE("ADC request is rejected before RCP_CONFIGURED, same operational gating as GPIO/SPI/I2C",
          "[mock][REQ-MOCK-014]") {
    mock::Server server;
    REQUIRE(server.lifecycle().state() == lifecycle::ServerState::HwUnconfigured);

    auto req = standard_request(mock::kAdcByteBusId, /*write=*/false, /*evt_op=*/0);
    acf::AcfMessageInfo resp;
    std::vector<uint8_t> resp_payload;
    auto ec = server.dispatch(0, req, {}, resp, resp_payload);
    REQUIRE(ec == regmap::make_error_code(regmap::RegMapErrc::request_rejected));
    REQUIRE(acf::response_kind_of(resp) == acf::ResponseKind::ErrorResponse);
}

// ── PWM_IN ────────────────────────────────────────────────────────────────────
// Table 30/33 Row 2 evt[2:0] validation, third endpoint type after I2C and
// ADC: PwmInEndpoint wired into dispatch() at byte_bus_id 5.

TEST_CASE("PWM_IN plain request (evt[2:0]==000b) answers with the scripted measurement",
          "[mock][REQ-MOCK-015]") {
    mock::Server server;
    REQUIRE_FALSE(server.advance_to_rcp_configured());

    server.set_pwm_in_response({/*period=*/0x1234, /*active_duration=*/0x0056});

    auto req = standard_request(mock::kPwmInByteBusId, /*write=*/false, /*evt_op=*/0);
    acf::AcfMessageInfo resp;
    std::vector<uint8_t> resp_payload;
    REQUIRE_FALSE(server.dispatch(0, req, {}, resp, resp_payload));
    REQUIRE(acf::response_kind_of(resp) == acf::ResponseKind::ReadResponse);
    REQUIRE(resp_payload == std::vector<uint8_t>{0x12, 0x34, 0x00, 0x56});
}

TEST_CASE("PWM_IN plain requests keep answering the same scripted measurement (not consumed)",
          "[mock][REQ-MOCK-015]") {
    mock::Server server;
    REQUIRE_FALSE(server.advance_to_rcp_configured());

    server.set_pwm_in_response({100, 50});

    auto req = standard_request(mock::kPwmInByteBusId, /*write=*/false, /*evt_op=*/0);
    acf::AcfMessageInfo resp;
    std::vector<uint8_t> resp_payload;

    REQUIRE_FALSE(server.dispatch(0, req, {}, resp, resp_payload));
    REQUIRE(resp_payload == std::vector<uint8_t>{0x00, 0x64, 0x00, 0x32});

    // A second dispatch against the same scripted value answers identically
    // — unlike ADC's FIFO queue, PWM_IN's read model is not consume-once.
    REQUIRE_FALSE(server.dispatch(0, req, {}, resp, resp_payload));
    REQUIRE(resp_payload == std::vector<uint8_t>{0x00, 0x64, 0x00, 0x32});
}

TEST_CASE("PWM_IN plain request reports PwmInNoSignal before any measurement is scripted",
          "[mock][REQ-MOCK-015]") {
    mock::Server server;
    REQUIRE_FALSE(server.advance_to_rcp_configured());

    auto req = standard_request(mock::kPwmInByteBusId, /*write=*/false, /*evt_op=*/0);
    acf::AcfMessageInfo resp;
    std::vector<uint8_t> resp_payload;
    auto ec = server.dispatch(0, req, {}, resp, resp_payload);
    REQUIRE(ec == pwm::make_error_code(pwm::PwmErrc::no_signal));
    REQUIRE(acf::response_kind_of(resp) == acf::ResponseKind::ErrorResponse);
    REQUIRE(resp_payload ==
            std::vector<uint8_t>{static_cast<uint8_t>(acf::WireErrorCode::PwmInNoSignal)});
}

TEST_CASE("PWM_IN request with a reserved evt[2:0] (001b-110b) is rejected with wire error code "
          "UNSUPPORTED_CMD and does not disturb the scripted measurement",
          "[mock][REQ-MOCK-015]") {
    mock::Server server;
    REQUIRE_FALSE(server.advance_to_rcp_configured());
    server.set_pwm_in_response({200, 75});

    for (uint8_t evt_op = 1; evt_op <= 6; ++evt_op) {
        auto req = standard_request(mock::kPwmInByteBusId, /*write=*/false, evt_op);
        acf::AcfMessageInfo resp;
        std::vector<uint8_t> resp_payload;
        auto ec = server.dispatch(0, req, {}, resp, resp_payload);
        REQUIRE(ec == endpoint::make_error_code(endpoint::EndpointErrc::reserved_evt_row2));
        REQUIRE(acf::response_kind_of(resp) == acf::ResponseKind::ErrorResponse);
        REQUIRE(resp_payload ==
                std::vector<uint8_t>{static_cast<uint8_t>(acf::WireErrorCode::UnsupportedCmd)});
    }

    // The scripted measurement must still be there — no reserved evt disturbed it.
    auto req = standard_request(mock::kPwmInByteBusId, /*write=*/false, /*evt_op=*/0);
    acf::AcfMessageInfo resp;
    std::vector<uint8_t> resp_payload;
    REQUIRE_FALSE(server.dispatch(0, req, {}, resp, resp_payload));
    REQUIRE(resp_payload == std::vector<uint8_t>{0x00, 0xC8, 0x00, 0x4B});
}

TEST_CASE("PWM_IN request with evt[2:0]==111b (config-write) is rejected with wire error code "
          "UNSUPPORTED_CMD rather than crashing or being treated as a plain read",
          "[mock][REQ-MOCK-015]") {
    mock::Server server;
    REQUIRE_FALSE(server.advance_to_rcp_configured());

    auto req = standard_request(mock::kPwmInByteBusId, /*write=*/false, /*evt_op=*/7);
    acf::AcfMessageInfo resp;
    std::vector<uint8_t> resp_payload;
    auto ec = server.dispatch(0, req, {}, resp, resp_payload);
    REQUIRE(ec == pwm::make_error_code(pwm::PwmErrc::config_write_not_supported));
    REQUIRE(acf::response_kind_of(resp) == acf::ResponseKind::ErrorResponse);
    REQUIRE(resp_payload ==
            std::vector<uint8_t>{static_cast<uint8_t>(acf::WireErrorCode::UnsupportedCmd)});
}

TEST_CASE("PWM_IN request is rejected before RCP_CONFIGURED, same operational gating as GPIO/SPI/I2C/ADC",
          "[mock][REQ-MOCK-016]") {
    mock::Server server;
    REQUIRE(server.lifecycle().state() == lifecycle::ServerState::HwUnconfigured);

    auto req = standard_request(mock::kPwmInByteBusId, /*write=*/false, /*evt_op=*/0);
    acf::AcfMessageInfo resp;
    std::vector<uint8_t> resp_payload;
    auto ec = server.dispatch(0, req, {}, resp, resp_payload);
    REQUIRE(ec == regmap::make_error_code(regmap::RegMapErrc::request_rejected));
    REQUIRE(acf::response_kind_of(resp) == acf::ResponseKind::ErrorResponse);
}

// ── LIN ───────────────────────────────────────────────────────────────────────
// Table 30/33 Row 2 evt[2:0] validation, fourth endpoint type after I2C,
// ADC, and PWM_IN: LinEndpoint wired into dispatch() at byte_bus_id 6.

TEST_CASE("LIN plain request (evt[2:0]==000b) answers with the scripted response bytes",
          "[mock][REQ-MOCK-017]") {
    mock::Server server;
    REQUIRE_FALSE(server.advance_to_rcp_configured());

    server.set_lin_response({0xAA, 0xBB});

    auto req = standard_request(mock::kLinByteBusId, /*write=*/true, /*evt_op=*/0);
    std::vector<uint8_t> out_bytes{0x55, 0x21};
    acf::AcfMessageInfo resp;
    std::vector<uint8_t> resp_payload;
    REQUIRE_FALSE(server.dispatch(0, req, out_bytes, resp, resp_payload));
    REQUIRE(acf::response_kind_of(resp) == acf::ResponseKind::ReadResponse);
    REQUIRE(resp_payload == std::vector<uint8_t>{0xAA, 0xBB});
    REQUIRE(server.lin().last_sent() == out_bytes);
}

TEST_CASE("LIN request with a reserved evt[2:0] (001b-110b) is rejected with wire error code "
          "UNSUPPORTED_CMD and does not touch endpoint state",
          "[mock][REQ-MOCK-017]") {
    mock::Server server;
    REQUIRE_FALSE(server.advance_to_rcp_configured());

    for (uint8_t evt_op = 1; evt_op <= 6; ++evt_op) {
        auto req = standard_request(mock::kLinByteBusId, /*write=*/true, evt_op);
        acf::AcfMessageInfo resp;
        std::vector<uint8_t> resp_payload;
        auto ec = server.dispatch(0, req, {0x55}, resp, resp_payload);
        REQUIRE(ec == endpoint::make_error_code(endpoint::EndpointErrc::reserved_evt_row2));
        REQUIRE(acf::response_kind_of(resp) == acf::ResponseKind::ErrorResponse);
        REQUIRE(resp_payload ==
                std::vector<uint8_t>{static_cast<uint8_t>(acf::WireErrorCode::UnsupportedCmd)});
        REQUIRE(server.lin().last_sent().empty());
    }
}

TEST_CASE("LIN request with evt[2:0]==111b (config-write) is rejected with wire error code "
          "UNSUPPORTED_CMD rather than crashing or being treated as a plain transfer",
          "[mock][REQ-MOCK-017]") {
    mock::Server server;
    REQUIRE_FALSE(server.advance_to_rcp_configured());

    auto req = standard_request(mock::kLinByteBusId, /*write=*/true, /*evt_op=*/7);
    acf::AcfMessageInfo resp;
    std::vector<uint8_t> resp_payload;
    auto ec = server.dispatch(0, req, {0x00, 0xAB}, resp, resp_payload);
    REQUIRE(ec == lin::make_error_code(lin::LinErrc::config_write_not_supported));
    REQUIRE(acf::response_kind_of(resp) == acf::ResponseKind::ErrorResponse);
    REQUIRE(resp_payload ==
            std::vector<uint8_t>{static_cast<uint8_t>(acf::WireErrorCode::UnsupportedCmd)});
    REQUIRE(server.lin().last_sent().empty());
}

TEST_CASE("LIN request is rejected before RCP_CONFIGURED, same operational gating as GPIO/SPI/I2C/ADC/PWM_IN",
          "[mock][REQ-MOCK-018]") {
    mock::Server server;
    REQUIRE(server.lifecycle().state() == lifecycle::ServerState::HwUnconfigured);

    auto req = standard_request(mock::kLinByteBusId, /*write=*/true, /*evt_op=*/0);
    acf::AcfMessageInfo resp;
    std::vector<uint8_t> resp_payload;
    auto ec = server.dispatch(0, req, {0x55}, resp, resp_payload);
    REQUIRE(ec == regmap::make_error_code(regmap::RegMapErrc::request_rejected));
    REQUIRE(acf::response_kind_of(resp) == acf::ResponseKind::ErrorResponse);
}

// ── CAN ───────────────────────────────────────────────────────────────────────
// Table 30/33 Row 2 evt[2:0] validation, fifth endpoint type after I2C,
// ADC, PWM_IN, and LIN: CanEndpoint wired into dispatch() at byte_bus_id 7.
// Unlike I2C/LIN, CAN's Plain request is a fire-a-frame TX with no
// read-back data — see dispatch_can's and set_can_response's own comments
// in rcp/mock.hpp for why there is no set_can_response() to script and why
// a successful Plain request answers WriteResponse with an empty payload.

TEST_CASE("CAN plain request (evt[2:0]==000b) transmits the request payload as frame data and "
          "answers WriteResponse with an empty payload",
          "[mock][REQ-MOCK-019]") {
    mock::Server server;
    REQUIRE_FALSE(server.advance_to_rcp_configured());

    auto req = standard_request(mock::kCanByteBusId, /*write=*/true, /*evt_op=*/0);
    std::vector<uint8_t> data{0xDE, 0xAD, 0xBE, 0xEF};
    acf::AcfMessageInfo resp;
    std::vector<uint8_t> resp_payload;
    REQUIRE_FALSE(server.dispatch(0, req, data, resp, resp_payload));
    REQUIRE(acf::response_kind_of(resp) == acf::ResponseKind::WriteResponse);
    REQUIRE(resp_payload.empty());
    REQUIRE(server.can().last_transmitted().data == data);
}

TEST_CASE("CAN request with a reserved evt[2:0] (001b-110b) is rejected with wire error code "
          "UNSUPPORTED_CMD and does not touch endpoint state",
          "[mock][REQ-MOCK-019]") {
    mock::Server server;
    REQUIRE_FALSE(server.advance_to_rcp_configured());

    for (uint8_t evt_op = 1; evt_op <= 6; ++evt_op) {
        auto req = standard_request(mock::kCanByteBusId, /*write=*/true, evt_op);
        acf::AcfMessageInfo resp;
        std::vector<uint8_t> resp_payload;
        auto ec = server.dispatch(0, req, {0xAA}, resp, resp_payload);
        REQUIRE(ec == endpoint::make_error_code(endpoint::EndpointErrc::reserved_evt_row2));
        REQUIRE(acf::response_kind_of(resp) == acf::ResponseKind::ErrorResponse);
        REQUIRE(resp_payload ==
                std::vector<uint8_t>{static_cast<uint8_t>(acf::WireErrorCode::UnsupportedCmd)});
        REQUIRE(server.can().last_transmitted().data.empty());
    }
}

TEST_CASE("CAN request with evt[2:0]==111b (config-write) is rejected with wire error code "
          "UNSUPPORTED_CMD rather than crashing or being treated as a plain transmit",
          "[mock][REQ-MOCK-019]") {
    mock::Server server;
    REQUIRE_FALSE(server.advance_to_rcp_configured());

    auto req = standard_request(mock::kCanByteBusId, /*write=*/true, /*evt_op=*/7);
    acf::AcfMessageInfo resp;
    std::vector<uint8_t> resp_payload;
    auto ec = server.dispatch(0, req, {0x00, 0xAB}, resp, resp_payload);
    REQUIRE(ec == can::make_error_code(can::CanErrc::config_write_not_supported));
    REQUIRE(acf::response_kind_of(resp) == acf::ResponseKind::ErrorResponse);
    REQUIRE(resp_payload ==
            std::vector<uint8_t>{static_cast<uint8_t>(acf::WireErrorCode::UnsupportedCmd)});
    REQUIRE(server.can().last_transmitted().data.empty());
}

TEST_CASE("CAN request is rejected before RCP_CONFIGURED, same operational gating as GPIO/SPI/I2C/ADC/PWM_IN/LIN",
          "[mock][REQ-MOCK-020]") {
    mock::Server server;
    REQUIRE(server.lifecycle().state() == lifecycle::ServerState::HwUnconfigured);

    auto req = standard_request(mock::kCanByteBusId, /*write=*/true, /*evt_op=*/0);
    acf::AcfMessageInfo resp;
    std::vector<uint8_t> resp_payload;
    auto ec = server.dispatch(0, req, {0xAA}, resp, resp_payload);
    REQUIRE(ec == regmap::make_error_code(regmap::RegMapErrc::request_rejected));
    REQUIRE(acf::response_kind_of(resp) == acf::ResponseKind::ErrorResponse);
}

// ── UART ──────────────────────────────────────────────────────────────────────
// Table 30/33 Row 2 evt[2:0] validation, sixth endpoint type after I2C, ADC,
// PWM_IN, LIN, and CAN: UartEndpoint wired into dispatch() at byte_bus_id 8
// for the FIRST time (this file had zero UART wiring before this pass — see
// mock.hpp's own header comment and dispatch_uart's own comment). Unlike
// every other Row 2 endpoint type's single unified transfer()/transmit()
// call, UART routes a Plain request on req.op — write enqueues onto the TX
// queue, read drains the RX FIFO (see dispatch_uart's own comment for why
// there is no set_uart_response() hook and why elapsed_ms/uart_timeout_ms
// are both hardcoded to 0 instead).

TEST_CASE("UART plain write request (evt[2:0]==000b) enqueues the payload onto the TX queue and "
          "answers WriteResponse with an empty payload",
          "[mock][REQ-MOCK-021]") {
    mock::Server server;
    REQUIRE_FALSE(server.advance_to_rcp_configured());

    auto req = standard_request(mock::kUartByteBusId, /*write=*/true, /*evt_op=*/0);
    std::vector<uint8_t> data{0xDE, 0xAD, 0xBE, 0xEF};
    acf::AcfMessageInfo resp;
    std::vector<uint8_t> resp_payload;
    REQUIRE_FALSE(server.dispatch(0, req, data, resp, resp_payload));
    REQUIRE(acf::response_kind_of(resp) == acf::ResponseKind::WriteResponse);
    REQUIRE(resp_payload.empty());
    REQUIRE(server.uart().drain_tx() == data);
}

TEST_CASE("UART plain read request (evt[2:0]==000b) drains the RX FIFO and answers ReadResponse "
          "with the drained bytes",
          "[mock][REQ-MOCK-021]") {
    mock::Server server;
    REQUIRE_FALSE(server.advance_to_rcp_configured());
    REQUIRE_FALSE(server.uart().rx_fill({0x01, 0x02, 0x03}));

    auto req = standard_request(mock::kUartByteBusId, /*write=*/false, /*evt_op=*/0);
    req.read_size_or_segment_num = 3;
    acf::AcfMessageInfo resp;
    std::vector<uint8_t> resp_payload;
    REQUIRE_FALSE(server.dispatch(0, req, {}, resp, resp_payload));
    REQUIRE(acf::response_kind_of(resp) == acf::ResponseKind::ReadResponse);
    REQUIRE(resp_payload == std::vector<uint8_t>{0x01, 0x02, 0x03});
    REQUIRE(server.uart().rx_available() == 0);
}

TEST_CASE("UART write request with a reserved evt[2:0] (001b-110b) is rejected with wire error "
          "code UNSUPPORTED_CMD and does not touch the TX queue",
          "[mock][REQ-MOCK-021]") {
    mock::Server server;
    REQUIRE_FALSE(server.advance_to_rcp_configured());

    for (uint8_t evt_op = 1; evt_op <= 6; ++evt_op) {
        auto req = standard_request(mock::kUartByteBusId, /*write=*/true, evt_op);
        acf::AcfMessageInfo resp;
        std::vector<uint8_t> resp_payload;
        auto ec = server.dispatch(0, req, {0xAA}, resp, resp_payload);
        REQUIRE(ec == endpoint::make_error_code(endpoint::EndpointErrc::reserved_evt_row2));
        REQUIRE(acf::response_kind_of(resp) == acf::ResponseKind::ErrorResponse);
        REQUIRE(resp_payload ==
                std::vector<uint8_t>{static_cast<uint8_t>(acf::WireErrorCode::UnsupportedCmd)});
        REQUIRE(server.uart().drain_tx().empty());
    }
}

TEST_CASE("UART read request with a reserved evt[2:0] (001b-110b) is rejected with wire error "
          "code UNSUPPORTED_CMD and does not touch the RX FIFO",
          "[mock][REQ-MOCK-021]") {
    mock::Server server;
    REQUIRE_FALSE(server.advance_to_rcp_configured());
    REQUIRE_FALSE(server.uart().rx_fill({0x01, 0x02}));

    for (uint8_t evt_op = 1; evt_op <= 6; ++evt_op) {
        auto req = standard_request(mock::kUartByteBusId, /*write=*/false, evt_op);
        req.read_size_or_segment_num = 2;
        acf::AcfMessageInfo resp;
        std::vector<uint8_t> resp_payload;
        auto ec = server.dispatch(0, req, {}, resp, resp_payload);
        REQUIRE(ec == endpoint::make_error_code(endpoint::EndpointErrc::reserved_evt_row2));
        REQUIRE(acf::response_kind_of(resp) == acf::ResponseKind::ErrorResponse);
        REQUIRE(resp_payload ==
                std::vector<uint8_t>{static_cast<uint8_t>(acf::WireErrorCode::UnsupportedCmd)});
        // A rejected reserved evt must not touch the RX FIFO — the bytes
        // scripted above are still fully buffered.
        REQUIRE(server.uart().rx_available() == 2);
    }
}

TEST_CASE("UART request with evt[2:0]==111b (config-write) is rejected with wire error code "
          "UNSUPPORTED_CMD rather than crashing or being treated as a plain TX/RX operation",
          "[mock][REQ-MOCK-021]") {
    mock::Server server;
    REQUIRE_FALSE(server.advance_to_rcp_configured());

    auto req = standard_request(mock::kUartByteBusId, /*write=*/true, /*evt_op=*/7);
    acf::AcfMessageInfo resp;
    std::vector<uint8_t> resp_payload;
    auto ec = server.dispatch(0, req, {0x00, 0xAB}, resp, resp_payload);
    REQUIRE(ec == uart::make_error_code(uart::UartErrc::config_write_not_supported));
    REQUIRE(acf::response_kind_of(resp) == acf::ResponseKind::ErrorResponse);
    REQUIRE(resp_payload ==
            std::vector<uint8_t>{static_cast<uint8_t>(acf::WireErrorCode::UnsupportedCmd)});
    REQUIRE(server.uart().drain_tx().empty());
}

TEST_CASE("UART request is rejected before RCP_CONFIGURED, same operational gating as "
          "GPIO/SPI/I2C/ADC/PWM_IN/LIN/CAN",
          "[mock][REQ-MOCK-022]") {
    mock::Server server;
    REQUIRE(server.lifecycle().state() == lifecycle::ServerState::HwUnconfigured);

    auto req = standard_request(mock::kUartByteBusId, /*write=*/true, /*evt_op=*/0);
    acf::AcfMessageInfo resp;
    std::vector<uint8_t> resp_payload;
    auto ec = server.dispatch(0, req, {0xAA}, resp, resp_payload);
    REQUIRE(ec == regmap::make_error_code(regmap::RegMapErrc::request_rejected));
    REQUIRE(acf::response_kind_of(resp) == acf::ResponseKind::ErrorResponse);
}

// ── ISELED ────────────────────────────────────────────────────────────────────
// Table 30/33 Row 2 evt[2:0] validation, seventh endpoint type after I2C, ADC,
// PWM_IN, LIN, CAN, and UART: IseledEndpoint wired into dispatch() at
// byte_bus_id 9. Phase 3's rcp/iseled.hpp rewrite replaced its earlier
// structured Address/Data ACF-payload model with the same raw-byte-stream
// codec I2C/LIN already use — see rcp/iseled.hpp's own header comment —
// so, like dispatch_i2c/dispatch_lin, dispatch_iseled passes the raw
// byte_msg_payload straight to IseledEndpoint::handle_request rather than
// decoding/encoding it through a struct-based codec.

TEST_CASE("ISELED plain request (evt[2:0]==000b) records the sent bytes, transacts against the "
          "scripted response, and answers ReadResponse with the scripted response bytes",
          "[mock][REQ-MOCK-023]") {
    mock::Server server;
    REQUIRE_FALSE(server.advance_to_rcp_configured());

    const std::vector<uint8_t> scripted{0x01, 0x02, 0xBE, 0xEF};
    server.set_iseled_response(scripted);

    const std::vector<uint8_t> payload{0x03, 0x01, 0x02, 0x11, 0x22};

    auto req = standard_request(mock::kIseledByteBusId, /*write=*/true, /*evt_op=*/0);
    acf::AcfMessageInfo resp;
    std::vector<uint8_t> resp_payload;
    REQUIRE_FALSE(server.dispatch(0, req, payload, resp, resp_payload));
    REQUIRE(acf::response_kind_of(resp) == acf::ResponseKind::ReadResponse);
    REQUIRE(resp_payload == scripted);
    REQUIRE(server.iseled().last_sent() == payload);
    REQUIRE(server.iseled().last_received() == scripted);
}

TEST_CASE("ISELED request with a reserved evt[2:0] (001b-110b) is rejected with wire error code "
          "UNSUPPORTED_CMD and does not touch endpoint state",
          "[mock][REQ-MOCK-023]") {
    mock::Server server;
    REQUIRE_FALSE(server.advance_to_rcp_configured());

    const std::vector<uint8_t> payload{0x00, 0x10};

    for (uint8_t evt_op = 1; evt_op <= 6; ++evt_op) {
        auto req = standard_request(mock::kIseledByteBusId, /*write=*/true, evt_op);
        acf::AcfMessageInfo resp;
        std::vector<uint8_t> resp_payload;
        auto ec = server.dispatch(0, req, payload, resp, resp_payload);
        REQUIRE(ec == endpoint::make_error_code(endpoint::EndpointErrc::reserved_evt_row2));
        REQUIRE(acf::response_kind_of(resp) == acf::ResponseKind::ErrorResponse);
        REQUIRE(resp_payload ==
                std::vector<uint8_t>{static_cast<uint8_t>(acf::WireErrorCode::UnsupportedCmd)});
        REQUIRE(server.iseled().last_sent().empty());
        REQUIRE(server.iseled().last_received().empty());
    }
}

TEST_CASE("ISELED request with evt[2:0]==111b (config-write) is rejected with wire error code "
          "UNSUPPORTED_CMD rather than crashing or being treated as a plain transaction",
          "[mock][REQ-MOCK-023]") {
    mock::Server server;
    REQUIRE_FALSE(server.advance_to_rcp_configured());

    const std::vector<uint8_t> payload{0x00, 0x10};

    auto req = standard_request(mock::kIseledByteBusId, /*write=*/true, /*evt_op=*/7);
    acf::AcfMessageInfo resp;
    std::vector<uint8_t> resp_payload;
    auto ec = server.dispatch(0, req, payload, resp, resp_payload);
    REQUIRE(ec == iseled::make_error_code(iseled::IseledErrc::config_write_not_supported));
    REQUIRE(acf::response_kind_of(resp) == acf::ResponseKind::ErrorResponse);
    REQUIRE(resp_payload ==
            std::vector<uint8_t>{static_cast<uint8_t>(acf::WireErrorCode::UnsupportedCmd)});
    REQUIRE(server.iseled().last_sent().empty());
}

TEST_CASE("ISELED request is rejected before RCP_CONFIGURED, same operational gating as "
          "GPIO/SPI/I2C/ADC/PWM_IN/LIN/CAN/UART",
          "[mock][REQ-MOCK-024]") {
    mock::Server server;
    REQUIRE(server.lifecycle().state() == lifecycle::ServerState::HwUnconfigured);

    const std::vector<uint8_t> payload{};
    auto req = standard_request(mock::kIseledByteBusId, /*write=*/true, /*evt_op=*/0);
    acf::AcfMessageInfo resp;
    std::vector<uint8_t> resp_payload;
    auto ec = server.dispatch(0, req, payload, resp, resp_payload);
    REQUIRE(ec == regmap::make_error_code(regmap::RegMapErrc::request_rejected));
    REQUIRE(acf::response_kind_of(resp) == acf::ResponseKind::ErrorResponse);
}

// ── MDIO ──────────────────────────────────────────────────────────────────────
// Table 30/33 Row 2 evt[2:0] validation, EIGHTH and LAST endpoint type after
// I2C, ADC, PWM_IN, LIN, CAN, UART, and ISELED: MdioEndpoint wired into
// dispatch() at byte_bus_id 10 for the FIRST time (this file had zero MDIO
// wiring before this pass — see mock.hpp's own header comment and
// dispatch_mdio's own comment). Unlike ISELED, no MDIO byte-level wire codec
// exists in this codebase, so dispatch_mdio fixes mode/mdio_address and
// packs/unpacks mdio_payload via avtp::detail::put_u16/get_u16 purely for
// this mock's own round-trippable wire shape — see dispatch_mdio's own
// comment in rcp/mock.hpp for why. There is no set_mdio_response() hook:
// MDIO's register model is self-contained (a write round-trips straight
// back out on a subsequent read), unlike I2C/LIN/ISELED's external-bus
// scripting hooks.

TEST_CASE("MDIO plain write then read round-trips the value through avtp::detail's u16 packing",
          "[mock][REQ-MOCK-025]") {
    mock::Server server;
    REQUIRE_FALSE(server.advance_to_rcp_configured());

    std::vector<uint8_t> write_payload(2);
    avtp::detail::put_u16(write_payload.data(), 0xBEEF);
    auto write_req = standard_request(mock::kMdioByteBusId, /*write=*/true, /*evt_op=*/0);
    acf::AcfMessageInfo write_resp;
    std::vector<uint8_t> write_resp_payload;
    REQUIRE_FALSE(server.dispatch(0, write_req, write_payload, write_resp, write_resp_payload));
    REQUIRE(acf::response_kind_of(write_resp) == acf::ResponseKind::WriteResponse);
    REQUIRE(write_resp_payload.empty());
    REQUIRE(server.mdio().last_request().is_write);
    REQUIRE(server.mdio().last_request().mdio_payload == 0xBEEF);

    auto read_req = standard_request(mock::kMdioByteBusId, /*write=*/false, /*evt_op=*/0);
    acf::AcfMessageInfo read_resp;
    std::vector<uint8_t> read_resp_payload;
    REQUIRE_FALSE(server.dispatch(0, read_req, {}, read_resp, read_resp_payload));
    REQUIRE(acf::response_kind_of(read_resp) == acf::ResponseKind::ReadResponse);
    REQUIRE(read_resp_payload.size() == 2);
    REQUIRE(avtp::detail::get_u16(read_resp_payload.data()) == 0xBEEF);
}

TEST_CASE("MDIO request with a reserved evt[2:0] (001b-110b) is rejected with wire error code "
          "UNSUPPORTED_CMD and does not touch endpoint state",
          "[mock][REQ-MOCK-025]") {
    mock::Server server;
    REQUIRE_FALSE(server.advance_to_rcp_configured());

    std::vector<uint8_t> payload(2);
    avtp::detail::put_u16(payload.data(), 0x1234);

    for (uint8_t evt_op = 1; evt_op <= 6; ++evt_op) {
        auto req = standard_request(mock::kMdioByteBusId, /*write=*/true, evt_op);
        acf::AcfMessageInfo resp;
        std::vector<uint8_t> resp_payload;
        auto ec = server.dispatch(0, req, payload, resp, resp_payload);
        REQUIRE(ec == endpoint::make_error_code(endpoint::EndpointErrc::reserved_evt_row2));
        REQUIRE(acf::response_kind_of(resp) == acf::ResponseKind::ErrorResponse);
        REQUIRE(resp_payload ==
                std::vector<uint8_t>{static_cast<uint8_t>(acf::WireErrorCode::UnsupportedCmd)});
        REQUIRE(server.mdio().last_request().mdio_address == 0);
        REQUIRE_FALSE(server.mdio().last_request().is_write);
    }

    // Confirm the reserved-evt requests above genuinely never wrote anything
    // — a subsequent Plain read at the same (fixed) address reads back 0.
    auto read_req = standard_request(mock::kMdioByteBusId, /*write=*/false, /*evt_op=*/0);
    acf::AcfMessageInfo read_resp;
    std::vector<uint8_t> read_resp_payload;
    REQUIRE_FALSE(server.dispatch(0, read_req, {}, read_resp, read_resp_payload));
    REQUIRE(avtp::detail::get_u16(read_resp_payload.data()) == 0);
}

TEST_CASE("MDIO request with evt[2:0]==111b (config-write) is rejected with wire error code "
          "UNSUPPORTED_CMD rather than crashing or being treated as a plain transaction",
          "[mock][REQ-MOCK-025]") {
    mock::Server server;
    REQUIRE_FALSE(server.advance_to_rcp_configured());

    std::vector<uint8_t> payload(2);
    avtp::detail::put_u16(payload.data(), 0x00FF);

    auto req = standard_request(mock::kMdioByteBusId, /*write=*/true, /*evt_op=*/7);
    acf::AcfMessageInfo resp;
    std::vector<uint8_t> resp_payload;
    auto ec = server.dispatch(0, req, payload, resp, resp_payload);
    REQUIRE(ec == mdio::make_error_code(mdio::MdioErrc::config_write_not_supported));
    REQUIRE(acf::response_kind_of(resp) == acf::ResponseKind::ErrorResponse);
    REQUIRE(resp_payload ==
            std::vector<uint8_t>{static_cast<uint8_t>(acf::WireErrorCode::UnsupportedCmd)});
    REQUIRE(server.mdio().last_request().mdio_address == 0);
}

TEST_CASE("MDIO request is rejected before RCP_CONFIGURED, same operational gating as "
          "GPIO/SPI/I2C/ADC/PWM_IN/LIN/CAN/UART/ISELED",
          "[mock][REQ-MOCK-026]") {
    mock::Server server;
    REQUIRE(server.lifecycle().state() == lifecycle::ServerState::HwUnconfigured);

    auto req = standard_request(mock::kMdioByteBusId, /*write=*/true, /*evt_op=*/0);
    acf::AcfMessageInfo resp;
    std::vector<uint8_t> resp_payload;
    auto ec = server.dispatch(0, req, {0xAA, 0xBB}, resp, resp_payload);
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
