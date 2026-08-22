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
    REQUIRE(server.registers().general.svr_ep_count == 10);
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

    // Bug fix (Phase 4/Phase 17 batch B): every seeded row's own
    // request_stream_index must be REQ-RMAP-054's power-on default (1),
    // never left at EpIdMappingEntry's own struct default (0) -- 0 is the
    // end-of-table sentinel regmap::ep_id_map::effective_count() reads,
    // so leaving row 0 at 0 would report this whole ten-row table as
    // zero effective rows.
    for (const auto& entry : server.registers().ep_id_mapping) {
        REQUIRE(entry.request_stream_index == 1);
    }
    REQUIRE(regmap::ep_id_map::effective_count(server.registers().ep_id_mapping.data(),
                                                 server.registers().ep_id_mapping.size()) == 10);
    REQUIRE(server.registers().general.svr_ep_bytebus_id_map_capacity == 10);
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
        REQUIRE(avtp::detail::get_u32(resp_payload.data()) == server.registers().general.magic);
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
    replacement.general.vendor_id = 0x1234;

    auto write_ec = server.ep0().write_whole_map(/*client=*/2, replacement);
    REQUIRE(write_ec == regmap::make_error_code(regmap::RegMapErrc::unauthorized_access));
    REQUIRE_FALSE(server.ep0().write_whole_map(/*client=*/1, replacement));
    REQUIRE(server.registers().general.vendor_id == 0x1234);
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

// ── Admission gating (Phase 4/Phase 17 batch A, cpp-RCP issue #129) ───────────
// Every dispatch_*() above now routes through the addressed endpoint's own
// rcp::server::Endpoint admission queue before invoking its handler body —
// these cases cover that gate itself, ported from the relevant slice of
// c-RCP's tests/test_mock.c (its own admission-related coverage) reduced to
// what THIS mock's Standard-request-only dispatch() can reach (see
// admit_and_classify()'s own doc comment, rcp/mock.hpp).

TEST_CASE("A disabled endpoint queues a request instead of executing it, and reports "
          "DispatchErrc::queued when no acknowledge was requested",
          "[mock][admission][REQ-SRV-015]") {
    mock::Server server;
    REQUIRE_FALSE(server.advance_to_rcp_configured());
    server.admission(mock::kGpioByteBusId)->set_enable(false);

    auto req = standard_request(mock::kGpioByteBusId, /*write=*/true,
                                 static_cast<uint8_t>(WriteSemantics::Or));
    auto payload = gpio::encode_gpio_payload(0x0000'000F);
    acf::AcfMessageInfo resp;
    std::vector<uint8_t> resp_payload;
    auto ec = server.dispatch(0, req, payload, resp, resp_payload);

    REQUIRE(ec == mock::make_error_code(mock::DispatchErrc::queued));
    REQUIRE_FALSE(resp.rsp); // no wire response at all — evt[3] never asked for one
    REQUIRE(resp_payload.empty());
    // The handler itself must never have run.
    REQUIRE(server.gpio().read() == 0);
    REQUIRE(server.admission(mock::kGpioByteBusId)->queue_len() == 1);
}

TEST_CASE("A disabled endpoint's queued request produces a genuine Acknowledge when evt[3] "
          "asked for one, and still does not execute the handler",
          "[mock][admission][REQ-SRV-016]") {
    mock::Server server;
    REQUIRE_FALSE(server.advance_to_rcp_configured());
    server.admission(mock::kGpioByteBusId)->set_enable(false);

    auto req = standard_request(mock::kGpioByteBusId, /*write=*/true,
                                 static_cast<uint8_t>(WriteSemantics::Or), /*evt_ack=*/true);
    auto payload = gpio::encode_gpio_payload(0x0000'000F);
    acf::AcfMessageInfo resp;
    std::vector<uint8_t> resp_payload;
    auto ec = server.dispatch(0, req, payload, resp, resp_payload);

    REQUIRE_FALSE(ec); // a real response WAS built — success, per dispatch()'s own contract
    REQUIRE(acf::response_kind_of(resp) == acf::ResponseKind::Acknowledge);
    REQUIRE_FALSE(resp.err);
    REQUIRE(server.gpio().read() == 0); // still not executed
}

TEST_CASE("A disabled endpoint's config-write (evt[2:0]==111b) request executes immediately, "
          "bypassing the queue entirely (REQ-SRV-015)",
          "[mock][admission][REQ-SRV-015]") {
    mock::Server server;
    REQUIRE_FALSE(server.advance_to_rcp_configured());
    server.admission(mock::kAdcByteBusId)->set_enable(false);

    // Address 0x0008 (adc_base_clk_divider) + one data octet — same
    // well-formed config-write payload the plain (enabled-endpoint) ADC
    // config-write test above uses.
    std::vector<uint8_t> payload{0x00, 0x08, 0x09};
    auto req = standard_request(mock::kAdcByteBusId, /*write=*/true, /*evt_op=*/7);
    acf::AcfMessageInfo resp;
    std::vector<uint8_t> resp_payload;
    auto ec = server.dispatch(0, req, payload, resp, resp_payload);

    REQUIRE_FALSE(ec);
    REQUIRE(acf::response_kind_of(resp) == acf::ResponseKind::WriteResponse);
    REQUIRE(server.admission(mock::kAdcByteBusId)->queue_len() == 0); // never queued
}

TEST_CASE("admission_suspended() rejects a request without inspecting it at all, and never "
          "builds a wire response regardless of evt[3] (REQ-PWRMODE-028)",
          "[mock][admission][REQ-PWRMODE-028]") {
    mock::Server server;
    REQUIRE_FALSE(server.advance_to_rcp_configured());
    server.admission(mock::kGpioByteBusId)->set_admission_suspended(true);

    auto req = standard_request(mock::kGpioByteBusId, /*write=*/false, /*evt_op=*/0, /*evt_ack=*/true);
    acf::AcfMessageInfo resp;
    std::vector<uint8_t> resp_payload;
    auto ec = server.dispatch(0, req, {}, resp, resp_payload);

    REQUIRE(ec == mock::make_error_code(mock::DispatchErrc::suspended));
    REQUIRE_FALSE(resp.rsp);
    REQUIRE(resp_payload.empty());
    REQUIRE(server.admission(mock::kGpioByteBusId)->queue_len() == 0); // not even queued
}

TEST_CASE("A response frame (rsp=1) dispatched as a request is rejected per REQ-ACF-021, "
          "Acknowledge-rejected shape when evt[3] was set",
          "[mock][admission][REQ-ACF-021]") {
    mock::Server server;
    REQUIRE_FALSE(server.advance_to_rcp_configured());

    auto req = standard_request(mock::kGpioByteBusId, /*write=*/false, /*evt_op=*/0, /*evt_ack=*/true);
    req.rsp = true;
    acf::AcfMessageInfo resp;
    std::vector<uint8_t> resp_payload;
    auto ec = server.dispatch(0, req, {}, resp, resp_payload);

    REQUIRE(ec == regmap::make_error_code(regmap::RegMapErrc::request_rejected));
    REQUIRE(acf::response_kind_of(resp) == acf::ResponseKind::Acknowledge);
    REQUIRE(resp.err);
    REQUIRE(resp_payload == std::vector<uint8_t>{static_cast<uint8_t>(acf::WireErrorCode::InvalidParameter)});
}

TEST_CASE("A response frame (rsp=1) dispatched as a request without evt[3] set produces no "
          "wire response at all",
          "[mock][admission][REQ-ACF-021]") {
    mock::Server server;
    REQUIRE_FALSE(server.advance_to_rcp_configured());

    auto req = standard_request(mock::kGpioByteBusId, /*write=*/false);
    req.rsp = true;
    acf::AcfMessageInfo resp;
    std::vector<uint8_t> resp_payload;
    auto ec = server.dispatch(0, req, {}, resp, resp_payload);

    REQUIRE(ec == regmap::make_error_code(regmap::RegMapErrc::request_rejected));
    REQUIRE_FALSE(resp.rsp);
    REQUIRE(resp_payload.empty());
}

TEST_CASE("drain_one() dequeues a request queued while disabled once the endpoint is "
          "re-enabled, and the caller can redispatch it to actually execute",
          "[mock][admission][REQ-SRV-017]") {
    mock::Server server;
    REQUIRE_FALSE(server.advance_to_rcp_configured());

    // Pins 0-1 must be configured as output before a write to them can take
    // effect (REQ-GPIO-009) — done while still enabled, same precondition
    // the plain GPIO write test above establishes.
    auto reconfig_req = standard_request(mock::kGpioByteBusId, /*write=*/true,
                                          static_cast<uint8_t>(WriteSemantics::Reconfigure));
    auto reconfig_payload = gpio::encode_gpio_payload(0x0000'0003);
    acf::AcfMessageInfo reconfig_resp;
    std::vector<uint8_t> reconfig_resp_payload;
    REQUIRE_FALSE(
        server.dispatch(0, reconfig_req, reconfig_payload, reconfig_resp, reconfig_resp_payload));

    server.admission(mock::kGpioByteBusId)->set_enable(false);

    auto req = standard_request(mock::kGpioByteBusId, /*write=*/true,
                                 static_cast<uint8_t>(WriteSemantics::Or));
    auto payload = gpio::encode_gpio_payload(0x0000'0003);
    acf::AcfMessageInfo resp;
    std::vector<uint8_t> resp_payload;
    REQUIRE(server.dispatch(0, req, payload, resp, resp_payload) ==
            mock::make_error_code(mock::DispatchErrc::queued));

    // Still disabled: drain_one() refuses to dequeue anything.
    std::vector<uint8_t> drained;
    REQUIRE_FALSE(server.drain_one(mock::kGpioByteBusId, drained));

    server.admission(mock::kGpioByteBusId)->set_enable(true);
    REQUIRE(server.drain_one(mock::kGpioByteBusId, drained));
    REQUIRE_FALSE(drained.empty());

    acf::AcfMessageInfo decoded_req;
    std::vector<uint8_t> decoded_payload;
    REQUIRE_FALSE(acf::decode_acf_abb(drained.data(), drained.size(), decoded_req, decoded_payload));

    acf::AcfMessageInfo redispatch_resp;
    std::vector<uint8_t> redispatch_resp_payload;
    REQUIRE_FALSE(
        server.dispatch(0, decoded_req, decoded_payload, redispatch_resp, redispatch_resp_payload));
    REQUIRE(acf::response_kind_of(redispatch_resp) == acf::ResponseKind::WriteResponse);
    REQUIRE(server.gpio().read() == 0x0000'0003);
}

TEST_CASE("pending_count()/watchdog_purge() report a directly-admitted Triggered request's own "
          "conditional-request store, independent of dispatch()'s own Standard-only surface",
          "[mock][admission][REQ-MOCK-027]") {
    mock::Server server;
    REQUIRE_FALSE(server.advance_to_rcp_configured());

    request::TriggeredStep step;
    step.trigger_source_ep = 1;
    step.trigger_signal_nr = 0;
    step.trigger_threshold = 0;
    auto frame = request::encode_triggered_request(request::RequestTypeOpcode::Triggered,
                                                     mock::kGpioByteBusId, step, /*transaction_num=*/7);

    std::optional<request::RequestTypeOpcode> request_type;
    std::optional<acf::WireErrorCode>           admit_error;
    auto outcome = server.admission(mock::kGpioByteBusId)
                       ->admit(frame.data(), frame.size(), /*now=*/0, /*tv=*/false,
                               /*avtp_timestamp=*/0, /*gptp_reference_now=*/0, request_type,
                               /*out_index=*/nullptr, &admit_error);
    REQUIRE(outcome == server::AdmitOutcome::Pending);
    REQUIRE(request_type == request::RequestTypeOpcode::Triggered);
    REQUIRE(server.pending_count(mock::kGpioByteBusId) == 1);

    // Triggered is not one of TC18's three safety-tagged (0x8x) opcodes, so
    // a watchdog purge removes it.
    REQUIRE(server.watchdog_purge(mock::kGpioByteBusId) == 1);
    REQUIRE(server.pending_count(mock::kGpioByteBusId) == 0);
}

TEST_CASE("notify_trigger()/notify_gptp_lock_state() broadcast a trigger occurrence to a "
          "directly-admitted Triggered request across this mock's own endpoint set",
          "[mock][admission][REQ-SRV-018]") {
    mock::Server server;
    REQUIRE_FALSE(server.advance_to_rcp_configured());

    // Watches Table 37 signal 1 (gPTP lock LOST) from source_ep 99 (this
    // deployment's own arbitrary convention for "the RC Server itself").
    request::TriggeredStep step;
    step.trigger_source_ep = 99;
    step.trigger_signal_nr = server::kGptpTriggerLost;
    step.trigger_threshold = 0;
    auto frame = request::encode_triggered_request(request::RequestTypeOpcode::Triggered,
                                                     mock::kSpiByteBusId, step, /*transaction_num=*/3);
    std::optional<request::RequestTypeOpcode> request_type;
    REQUIRE(server.admission(mock::kSpiByteBusId)
                ->admit(frame.data(), frame.size(), 0, false, 0, 0, request_type, nullptr, nullptr) ==
            server::AdmitOutcome::Pending);

    // The very first observation is never an edge, whatever value it is.
    REQUIRE(server.notify_gptp_lock_state(/*locked=*/true, /*source_ep=*/99) == 0);
    // Established -> Lost is a genuine edge, matching the stored request.
    REQUIRE(server.notify_gptp_lock_state(/*locked=*/false, /*source_ep=*/99) == 1);
    // Unchanged observation: no edge.
    REQUIRE(server.notify_gptp_lock_state(/*locked=*/false, /*source_ep=*/99) == 0);

    // A direct notify_trigger() call reaches the same stored request.
    REQUIRE(server.notify_trigger(/*source_ep=*/99, server::kGptpTriggerLost) == 1);
}

TEST_CASE("tick() surfaces a Standard request stored under a TSCF presentation-time gate "
          "once its gate opens (REQ-TIMED-012)",
          "[mock][admission][REQ-TIMED-012]") {
    mock::Server server;
    REQUIRE_FALSE(server.advance_to_rcp_configured());

    auto req     = standard_request(mock::kGpioByteBusId, /*write=*/false);
    auto frame   = acf::encode_acf_abb(req, {});
    std::optional<request::RequestTypeOpcode> request_type;
    size_t index = 0;
    REQUIRE(server.admission(mock::kGpioByteBusId)
                ->admit(frame.data(), frame.size(), /*now=*/0, /*tv=*/true, /*avtp_timestamp=*/1000,
                        /*gptp_reference_now=*/1000, request_type, &index, nullptr) ==
            server::AdmitOutcome::Pending);
    REQUIRE_FALSE(request_type.has_value()); // Standard: no repurposed opcode

    server::TickContext ctx;
    ctx.now          = 0;
    ctx.gptp_now     = 2000; // past the resolved 1000ns presentation gate
    ctx.gptp_locked  = true;

    std::vector<uint8_t> due_frame;
    REQUIRE(server.tick(mock::kGpioByteBusId, ctx, due_frame));
    REQUIRE(due_frame == frame);
    REQUIRE(server.pending_count(mock::kGpioByteBusId) == 0); // complete() released it
}

TEST_CASE("tick() reports nothing due while gPTP time is unlocked, even past the presentation "
          "time (REQ-TIMED-012 fail-closed)",
          "[mock][admission][REQ-TIMED-012]") {
    mock::Server server;
    REQUIRE_FALSE(server.advance_to_rcp_configured());

    auto req   = standard_request(mock::kGpioByteBusId, /*write=*/false);
    auto frame = acf::encode_acf_abb(req, {});
    std::optional<request::RequestTypeOpcode> request_type;
    REQUIRE(server.admission(mock::kGpioByteBusId)
                ->admit(frame.data(), frame.size(), 0, true, 1000, 1000, request_type, nullptr,
                        nullptr) == server::AdmitOutcome::Pending);

    server::TickContext ctx;
    ctx.gptp_now    = 2000;
    ctx.gptp_locked = false; // never established

    std::vector<uint8_t> due_frame;
    REQUIRE_FALSE(server.tick(mock::kGpioByteBusId, ctx, due_frame));
    REQUIRE(server.pending_count(mock::kGpioByteBusId) == 1); // still stored
}

// ── Request-stream-cfg / EP-ID-map storage (Phase 4/Phase 17 batch B) ────────
// set_request_stream_cfg()/set_ep_id_map() mirror c-RCP's own
// rcp_mock_server_set_request_stream_cfg()/_set_ep_id_map() (mock.c:
// 444-460/565-580) bounds-checked wholesale-replace + capacity-register-sync
// convention.

TEST_CASE("set_request_stream_cfg replaces the table wholesale and syncs "
          "svr_request_stream_cfg_capacity, bounded by kMaxEntries",
          "[mock][REQ-RMAP-034][REQ-RMAP-047]") {
    mock::Server server;
    REQUIRE(server.request_stream_cfg().empty());
    REQUIRE(server.registers().general.svr_request_stream_cfg_capacity == 0);

    std::vector<regmap::RequestStreamConfig> cfg(2);
    cfg[0].stream_id = avtp::StreamId::from_u64(0x1111);
    cfg[1].stream_id = avtp::StreamId::from_u64(0x2222);
    REQUIRE(server.set_request_stream_cfg(cfg));
    REQUIRE(server.request_stream_cfg().size() == 2);
    REQUIRE(server.request_stream_cfg()[1].stream_id.to_u64() == 0x2222);
    REQUIRE(server.registers().general.svr_request_stream_cfg_capacity == 2);

    // Over kMaxEntries: rejected, table left unchanged.
    std::vector<regmap::RequestStreamConfig> too_many(regmap::request_stream_cfg::kMaxEntries + 1);
    REQUIRE_FALSE(server.set_request_stream_cfg(too_many));
    REQUIRE(server.request_stream_cfg().size() == 2);
}

TEST_CASE("set_ep_id_map replaces the table wholesale and syncs "
          "svr_ep_bytebus_id_map_capacity, bounded by kMaxEntries",
          "[mock][REQ-RMAP-037][REQ-RMAP-052]") {
    mock::Server server;
    REQUIRE(server.ep_id_map().size() == 10); // batch A's own power-on default

    std::vector<regmap::EpIdMappingEntry> entries(1);
    entries[0].ep_id               = 7;
    entries[0].byte_bus_id         = 7;
    entries[0].request_stream_index = 1;
    REQUIRE(server.set_ep_id_map(entries));
    REQUIRE(server.ep_id_map().size() == 1);
    REQUIRE(server.registers().ep_id_mapping[0].ep_id == 7);
    REQUIRE(server.registers().general.svr_ep_bytebus_id_map_capacity == 1);

    std::vector<regmap::EpIdMappingEntry> too_many(regmap::ep_id_map::kMaxEntries + 1);
    REQUIRE_FALSE(server.set_ep_id_map(too_many));
    REQUIRE(server.ep_id_map().size() == 1); // unchanged
}

// ── Table 24 response/ack routing suppression (Phase 4/Phase 17 batch B,
//    REQ-RMAP-048/049) ────────────────────────────────────────────────────────
// Ported from c-RCP's tests/test_mock.c
// test_response_suppressed_when_rx_resp_stream_index_is_zero() /
// test_response_not_suppressed_when_rx_resp_stream_index_is_nonzero() /
// test_response_not_suppressed_for_unresolvable_stream() /
// test_acknowledge_suppressed_by_default_ack_stream_index() /
// test_acknowledge_not_suppressed_when_rx_ack_stream_index_is_nonzero()
// (tests/test_mock.c:1061-1204), reduced to real dispatch()-driven
// Read/Write/Acknowledge responses this mock's own typed endpoints
// already produce, rather than a synthetic handler double.

namespace {
constexpr uint64_t kSuppressionStreamRaw = 0x0102030405060708ULL;
avtp::StreamId      kSuppressionStream() { return avtp::StreamId::from_u64(kSuppressionStreamRaw); }
} // namespace

TEST_CASE("A Read/WriteResponse is suppressed (no wire response at all) when the resolved "
          "request stream's rx_resp_stream_index == 0",
          "[mock][REQ-RMAP-049]") {
    mock::Server server;
    REQUIRE_FALSE(server.advance_to_rcp_configured());

    regmap::RequestStreamConfig cfg;
    cfg.stream_id            = kSuppressionStream();
    cfg.rx_resp_stream_index = 0; // "no response is to be sent"
    REQUIRE(server.set_request_stream_cfg({cfg}));

    auto req = standard_request(mock::kGpioByteBusId, /*write=*/false);
    acf::AcfMessageInfo resp;
    std::vector<uint8_t> resp_payload;
    auto ec = server.dispatch(0, req, {}, resp, resp_payload, kSuppressionStream());

    // The handler still ran (GPIO's own read state is unaffected by
    // suppression) -- only the wire response is withheld.
    REQUIRE_FALSE(ec);
    REQUIRE_FALSE(resp.rsp);
    REQUIRE(resp_payload.empty());
}

TEST_CASE("A ReadResponse is NOT suppressed when the resolved request stream's "
          "rx_resp_stream_index is nonzero (its own power-on default, 1)",
          "[mock][REQ-RMAP-049]") {
    mock::Server server;
    REQUIRE_FALSE(server.advance_to_rcp_configured());

    regmap::RequestStreamConfig cfg; // rx_resp_stream_index defaults to 1
    cfg.stream_id = kSuppressionStream();
    REQUIRE(server.set_request_stream_cfg({cfg}));

    auto req = standard_request(mock::kGpioByteBusId, /*write=*/false);
    acf::AcfMessageInfo resp;
    std::vector<uint8_t> resp_payload;
    auto ec = server.dispatch(0, req, {}, resp, resp_payload, kSuppressionStream());

    REQUIRE_FALSE(ec);
    REQUIRE(acf::response_kind_of(resp) == acf::ResponseKind::ReadResponse);
}

TEST_CASE("A response is NOT suppressed for a stream_id with no configured "
          "request-stream-cfg entry at all (unresolvable stream suppresses nothing)",
          "[mock][REQ-RMAP-049]") {
    mock::Server server;
    REQUIRE_FALSE(server.advance_to_rcp_configured());
    // Deliberately no set_request_stream_cfg() call.

    auto req = standard_request(mock::kGpioByteBusId, /*write=*/false);
    acf::AcfMessageInfo resp;
    std::vector<uint8_t> resp_payload;
    auto ec = server.dispatch(0, req, {}, resp, resp_payload, kSuppressionStream());

    REQUIRE_FALSE(ec);
    REQUIRE(acf::response_kind_of(resp) == acf::ResponseKind::ReadResponse);
}

TEST_CASE("An Acknowledge is suppressed by rx_ack_stream_index's own default (0), "
          "independent of rx_resp_stream_index",
          "[mock][REQ-RMAP-048]") {
    mock::Server server;
    REQUIRE_FALSE(server.advance_to_rcp_configured());
    server.admission(mock::kGpioByteBusId)->set_enable(false); // disabled -> queues + acks

    regmap::RequestStreamConfig cfg; // rx_ack_stream_index defaults to 0
    cfg.stream_id = kSuppressionStream();
    REQUIRE(server.set_request_stream_cfg({cfg}));

    auto req = standard_request(mock::kGpioByteBusId, /*write=*/true,
                                 static_cast<uint8_t>(WriteSemantics::Or), /*evt_ack=*/true);
    auto payload = gpio::encode_gpio_payload(0x0000'000F);
    acf::AcfMessageInfo resp;
    std::vector<uint8_t> resp_payload;
    auto ec = server.dispatch(0, req, payload, resp, resp_payload, kSuppressionStream());

    // Without suppression this would be a genuine Acknowledge (REQ-SRV-016,
    // covered above) -- rx_ack_stream_index == 0 withholds it instead, but
    // the request is still genuinely queued.
    REQUIRE_FALSE(ec);
    REQUIRE_FALSE(resp.rsp);
    REQUIRE(resp_payload.empty());
    REQUIRE(server.admission(mock::kGpioByteBusId)->queue_len() == 1);
}

TEST_CASE("An Acknowledge is NOT suppressed when rx_ack_stream_index is nonzero, EVEN with "
          "rx_resp_stream_index explicitly 0 on the same stream (field separation)",
          "[mock][REQ-RMAP-048]") {
    mock::Server server;
    REQUIRE_FALSE(server.advance_to_rcp_configured());
    server.admission(mock::kGpioByteBusId)->set_enable(false);

    regmap::RequestStreamConfig cfg;
    cfg.stream_id            = kSuppressionStream();
    cfg.rx_ack_stream_index  = 1; // "send it"
    cfg.rx_resp_stream_index = 0; // must NOT apply to an Acknowledge
    REQUIRE(server.set_request_stream_cfg({cfg}));

    auto req = standard_request(mock::kGpioByteBusId, /*write=*/true,
                                 static_cast<uint8_t>(WriteSemantics::Or), /*evt_ack=*/true);
    auto payload = gpio::encode_gpio_payload(0x0000'000F);
    acf::AcfMessageInfo resp;
    std::vector<uint8_t> resp_payload;
    auto ec = server.dispatch(0, req, payload, resp, resp_payload, kSuppressionStream());

    REQUIRE_FALSE(ec);
    REQUIRE(acf::response_kind_of(resp) == acf::ResponseKind::Acknowledge);
}

TEST_CASE("Every operational endpoint type's response is suppressible via Table 24 "
          "(sweep of all ten dispatch_*() wrappers)",
          "[mock][REQ-RMAP-049]") {
    mock::Server server;
    REQUIRE_FALSE(server.advance_to_rcp_configured());

    regmap::RequestStreamConfig cfg;
    cfg.stream_id            = kSuppressionStream();
    cfg.rx_resp_stream_index = 0;
    REQUIRE(server.set_request_stream_cfg({cfg}));

    for (avtp::ByteBusId bus_id : {mock::kGpioByteBusId, mock::kSpiByteBusId, mock::kI2cByteBusId,
                                    mock::kAdcByteBusId, mock::kPwmInByteBusId, mock::kLinByteBusId,
                                    mock::kCanByteBusId, mock::kUartByteBusId, mock::kIseledByteBusId,
                                    mock::kMdioByteBusId}) {
        auto req = standard_request(bus_id, /*write=*/true, /*evt_op=*/0);
        acf::AcfMessageInfo resp;
        std::vector<uint8_t> resp_payload;
        // Every one of these ten byte_bus_ids' own Plain (evt[2:0]==000b)
        // request succeeds unconditionally against this fixture's own
        // defaults (empty payload is valid for all ten write paths tested
        // above), so every response reaching suppression is a genuine one.
        (void)server.dispatch(0, req, {}, resp, resp_payload, kSuppressionStream());
        REQUIRE_FALSE(resp.rsp);
        REQUIRE(resp_payload.empty());
    }
}

// ── Discovery-stream claim (REQ-RMAP-066, Phase 4/Phase 17 batch B) ──────────
// Ported from c-RCP's tests/test_mock.c
// test_new_server_discovery_claim_starts_with_the_tc18_default_timeout() /
// test_set_discovery_timeout_us_syncs_svr_ep_cfg_and_claim() /
// test_discovery_claim_lifecycle_driven_by_configured_timeout()
// (tests/test_mock.c:1917-1980), adapted to discovery::DiscoveryClaim's own
// std::chrono::steady_clock::time_point-based API (discovery.hpp) rather than
// c-RCP's raw timeout_ms/now_ms integers -- behaviorally equivalent, since
// this class exposes no direct "current timeout" getter (matching c-RCP's own
// "thin storage, nothing more" scope for this batch -- see mock.hpp's own
// header comment).

TEST_CASE("A new Server's discovery_claim starts unheld, with TC18's own default "
          "svr_discovery_timeout (20000 us)",
          "[mock][REQ-RMAP-066]") {
    mock::Server server;
    REQUIRE(server.registers().svr_ep_cfg.svr_discovery_timeout == 20000);

    const auto now = discovery::DiscoveryClaim::Clock::now();
    REQUIRE_FALSE(server.discovery_claim().has_active_claim(now));
}

TEST_CASE("set_discovery_timeout_us keeps svr_ep_cfg and discovery_claim's own window in sync",
          "[mock][REQ-RMAP-066]") {
    mock::Server server;
    server.set_discovery_timeout_us(10000); // 10 ms, a short, test-friendly window
    REQUIRE(server.registers().svr_ep_cfg.svr_discovery_timeout == 10000);

    const auto base = discovery::DiscoveryClaim::Clock::now();
    REQUIRE(server.discovery_claim().on_discovery_request(
                /*client=*/1, lifecycle::ServerState::HwUnconfigured, base) ==
            discovery::DiscoveryClaim::ClaimOutcome::Claimed);

    // Well within the 10 ms window: still held.
    REQUIRE(server.discovery_claim().has_active_claim(base + std::chrono::milliseconds(5)));
    // The window has now lapsed: open again.
    REQUIRE_FALSE(server.discovery_claim().has_active_claim(base + std::chrono::milliseconds(10)));
}

TEST_CASE("discovery_claim()'s own real claim lifecycle (open -> claimed -> a second "
          "requester refused -> lapse -> re-grantable) is genuinely driven by "
          "set_discovery_timeout_us()'s configured window",
          "[mock][REQ-RMAP-066][REQ-DISC-029]") {
    mock::Server server;
    server.set_discovery_timeout_us(10000); // 10 ms

    const auto base = discovery::DiscoveryClaim::Clock::now();
    auto&      claim = server.discovery_claim();

    REQUIRE(claim.on_discovery_request(/*client=*/1, lifecycle::ServerState::HwUnconfigured, base) ==
            discovery::DiscoveryClaim::ClaimOutcome::Claimed);
    // A second, different requester within the window is refused
    // (REQ-DISC-029), not granted.
    REQUIRE(claim.on_discovery_request(/*client=*/2, lifecycle::ServerState::HwUnconfigured,
                                        base + std::chrono::milliseconds(5)) ==
            discovery::DiscoveryClaim::ClaimOutcome::HeldByOther);
    // Once the window has lapsed, the second requester is re-grantable.
    REQUIRE(claim.on_discovery_request(/*client=*/2, lifecycle::ServerState::HwUnconfigured,
                                        base + std::chrono::milliseconds(10)) ==
            discovery::DiscoveryClaim::ClaimOutcome::Claimed);
}

// ── E2E dispatch (Phase 4/Phase 17 batch C, cpp-RCP issue #129) ─────────────────
// Ported from c-RCP's tests/test_mock.c coverage for rcp_mock_server_
// dispatch_e2e() (REQ-E2E-021/028/029/045/046, REQ-WDG-010), reduced to
// this mock's own single public dispatch_e2e() entry point shape — see
// that method's own doc comment for why this file has ONE, not ten
// dispatch_<type>_e2e() siblings. See tests/test_e2e.cpp for
// RxSequenceGuard/StreamFaultTracker/RxWatchdog/StreamStatus's own
// already-covered unit-level behavior — this section is INTEGRATION
// coverage (via dispatch_e2e()), not a re-test of those classes' own
// internals.

namespace {

constexpr uint8_t kE2eHeaderOctet1 = 0x00; // mirrors Server's own private kE2eHeaderOctet1Placeholder

avtp::StreamId e2e_stream(uint64_t raw) { return avtp::StreamId::from_u64(raw); }

// configure_gpio_all_outputs sets every GPIO pin's own direction to output
// (GpioState::directions defaults to all-input, TC18 §13.7.4.3 — a write to
// an input pin is masked out and never reaches state.values, per
// apply_gpio_write()'s own doc comment, gpio.hpp) so this section's own
// OR-write assertions below actually observe a state change. A direct
// GpioEndpoint::handle_write(Reconfigure, ...) fixture call, bypassing
// dispatch entirely — the same "script the endpoint's own live state
// directly" convention set_spi_poci()/set_i2c_response()/etc. already
// establish for their own subsystems.
void configure_gpio_all_outputs(mock::Server& server) {
    gpio::PinMask ignored = 0;
    (void)server.gpio().handle_write(WriteSemantics::Reconfigure, 0xFFFF'FFFFu, ignored);
}

// wrap_gpio_write builds a real E2E-CRC-protected NTSCF frame for a GPIO
// OR-write request — the same e2e::wrap_framed() shape tests/test_e2e.cpp
// already exercises directly, reused here as dispatch_e2e()'s own wire
// input.
std::vector<uint8_t> wrap_gpio_write(avtp::StreamId stream_id, uint8_t transaction_num,
                                      gpio::PinMask operand) {
    acf::AcfMessageInfo info;
    info.byte_bus_id     = mock::kGpioByteBusId;
    info.transaction_num = transaction_num;
    info.op              = true;
    info.evt_op           = static_cast<uint8_t>(WriteSemantics::Or);
    auto payload = gpio::encode_gpio_payload(operand);
    return e2e::wrap_framed(/*is_ntscf_framed=*/true, kE2eHeaderOctet1, /*tu=*/false, stream_id,
                             /*avtp_timestamp=*/std::nullopt, info, /*message_timestamp=*/std::nullopt, payload);
}

} // namespace

TEST_CASE("dispatch_e2e in plain command mode (ep_req_crc_enable unset) decodes and delegates "
          "exactly like dispatch()",
          "[mock][e2e][REQ-E2E-021]") {
    mock::Server server;
    configure_gpio_all_outputs(server);
    REQUIRE_FALSE(server.advance_to_rcp_configured());
    // ep_req_crc_enable left at its own struct default (false).

    auto req = standard_request(mock::kGpioByteBusId, /*write=*/true,
                                 static_cast<uint8_t>(WriteSemantics::Or));
    auto payload = gpio::encode_gpio_payload(0x0000'000F);
    auto frame   = acf::encode_acf_abb(req, payload); // NOT CRC-wrapped — plain command mode input

    acf::AcfMessageInfo   resp;
    std::vector<uint8_t>  resp_payload;
    auto ec = server.dispatch_e2e(0, e2e_stream(0x1111), /*sequence_num=*/0, frame, resp, resp_payload);

    REQUIRE_FALSE(ec);
    REQUIRE(acf::response_kind_of(resp) == acf::ResponseKind::WriteResponse);
    REQUIRE(server.gpio().read() == 0x0000'000F);
}

TEST_CASE("dispatch_e2e validates a genuine E2E CRC and delivers the unwrapped request to the "
          "same admission/handler path dispatch() itself uses",
          "[mock][e2e][REQ-E2E-021]") {
    mock::Server server;
    configure_gpio_all_outputs(server);
    REQUIRE_FALSE(server.advance_to_rcp_configured());
    server.registers().generic_configs[mock::kGpioEndpointId - 1].ep_req_crc_enable = true;

    const auto stream_id = e2e_stream(0x2222);
    auto frame = wrap_gpio_write(stream_id, /*transaction_num=*/7, 0x0000'00F0);

    acf::AcfMessageInfo   resp;
    std::vector<uint8_t>  resp_payload;
    auto ec = server.dispatch_e2e(0, stream_id, /*sequence_num=*/0, frame, resp, resp_payload);

    REQUIRE_FALSE(ec);
    REQUIRE(acf::response_kind_of(resp) == acf::ResponseKind::WriteResponse);
    REQUIRE(server.gpio().read() == 0x0000'00F0);
}

TEST_CASE("dispatch_e2e reports crc_error with a POCI_FAILURE error response on CRC corruption, "
          "and latches the stream faulted (REQ-E2E-021) when rx_enforce_e2e is set",
          "[mock][e2e][REQ-E2E-021][REQ-E2E-046]") {
    mock::Server server;
    configure_gpio_all_outputs(server);
    REQUIRE_FALSE(server.advance_to_rcp_configured());
    server.registers().generic_configs[mock::kGpioEndpointId - 1].ep_req_crc_enable = true;

    const auto stream_id = e2e_stream(0x3333);
    regmap::RequestStreamConfig cfg;
    cfg.stream_id       = stream_id;
    cfg.rx_enforce_e2e  = true;
    REQUIRE(server.set_request_stream_cfg({cfg}));

    auto frame = wrap_gpio_write(stream_id, /*transaction_num=*/9, 0x0000'00FF);
    frame[frame.size() - 1] ^= 0xFF; // corrupt one CRC byte

    acf::AcfMessageInfo   resp;
    std::vector<uint8_t>  resp_payload;
    auto ec = server.dispatch_e2e(0, stream_id, /*sequence_num=*/0, frame, resp, resp_payload);

    REQUIRE(ec == e2e::make_error_code(e2e::E2eErrc::crc_error));
    REQUIRE(acf::response_kind_of(resp) == acf::ResponseKind::ErrorResponse);
    REQUIRE(resp_payload.size() == 1);
    REQUIRE(resp_payload[0] == static_cast<uint8_t>(acf::WireErrorCode::PociFailure));
    REQUIRE(server.gpio().read() == 0); // the write never reached the endpoint
    REQUIRE(server.stream_rx_blocked(stream_id));

    // The stream is now latched faulted — a SUBSEQUENT, genuinely valid CRC
    // request on the SAME stream is rejected too, without even being
    // unwrapped (REQ-E2E-021's own "stream is blocked until released").
    auto good_frame = wrap_gpio_write(stream_id, /*transaction_num=*/10, 0x0000'00FF);
    acf::AcfMessageInfo   resp2;
    std::vector<uint8_t>  resp_payload2;
    auto ec2 = server.dispatch_e2e(0, stream_id, /*sequence_num=*/1, good_frame, resp2, resp_payload2);
    REQUIRE(ec2 == mock::make_error_code(mock::DispatchErrc::stream_faulted));
    REQUIRE(acf::response_kind_of(resp2) == acf::ResponseKind::ErrorResponse);
    REQUIRE(resp_payload2[0] == static_cast<uint8_t>(acf::WireErrorCode::PociFailure));
    REQUIRE(server.gpio().read() == 0); // still never reached the endpoint

    // Releasing the latch lets a genuinely valid request through again.
    server.stream_fault_tracker().reset(stream_id.to_u64());
    acf::AcfMessageInfo   resp3;
    std::vector<uint8_t>  resp_payload3;
    auto ec3 = server.dispatch_e2e(0, stream_id, /*sequence_num=*/2, good_frame, resp3, resp_payload3);
    REQUIRE_FALSE(ec3);
    REQUIRE(server.gpio().read() == 0x0000'00FF);
}

TEST_CASE("dispatch_e2e's sequence gate (REQ-E2E-028/029) rejects a non-increasing sequence_num "
          "with no wire response at all, before the request is even CRC-checked",
          "[mock][e2e][REQ-E2E-028][REQ-E2E-029]") {
    mock::Server server;
    configure_gpio_all_outputs(server);
    REQUIRE_FALSE(server.advance_to_rcp_configured());
    server.registers().generic_configs[mock::kGpioEndpointId - 1].ep_req_crc_enable = true;

    const auto stream_id = e2e_stream(0x4444);
    regmap::RequestStreamConfig cfg;
    cfg.stream_id      = stream_id;
    cfg.rx_enforce_seq = true;
    REQUIRE(server.set_request_stream_cfg({cfg}));

    auto frame = wrap_gpio_write(stream_id, /*transaction_num=*/1, 0x0000'000F);

    acf::AcfMessageInfo   resp;
    std::vector<uint8_t>  resp_payload;
    // First observed sequence number always bootstraps as accepted.
    auto ec = server.dispatch_e2e(0, stream_id, /*sequence_num=*/5, frame, resp, resp_payload);
    REQUIRE_FALSE(ec);
    REQUIRE(server.gpio().read() == 0x0000'000F);

    // A second request with the SAME sequence_num (fwd_distance 0, not a
    // forward advance) is rejected before CRC unwrap, so the endpoint's
    // own state is untouched and NO wire response is built at all.
    auto frame2 = wrap_gpio_write(stream_id, /*transaction_num=*/2, 0xFFFF'FFFF);
    acf::AcfMessageInfo   resp2;
    std::vector<uint8_t>  resp_payload2;
    auto ec2 = server.dispatch_e2e(0, stream_id, /*sequence_num=*/5, frame2, resp2, resp_payload2);
    REQUIRE(ec2 == mock::make_error_code(mock::DispatchErrc::seq_error));
    REQUIRE_FALSE(resp2.rsp);
    REQUIRE(resp_payload2.empty());
    REQUIRE(server.gpio().read() == 0x0000'000F); // unchanged — request never dispatched
}

TEST_CASE("dispatch_e2e kicks the per-stream RxWatchdog (REQ-WDG-010) on every call, and "
          "check_watchdog_overflow() purges pending non-safety requests once overflow latches "
          "with rx_wd_safestate_enable set",
          "[mock][e2e][REQ-WDG-010][REQ-E2E-030][REQ-E2E-046]") {
    mock::Server server;
    REQUIRE_FALSE(server.advance_to_rcp_configured());

    const auto stream_id = e2e_stream(0x5555);
    regmap::RequestStreamConfig cfg;
    cfg.stream_id               = stream_id;
    cfg.rx_wd_enable            = true;
    cfg.rx_wd_timeout_interval  = 100; // ms
    cfg.rx_wd_safestate_enable  = true;
    REQUIRE(server.set_request_stream_cfg({cfg}));
    // Power-on ep_id_mapping already binds every operational endpoint's own
    // request_stream_index to 1 (this class's own constructor bug-fix
    // comment) — the resolved index of the single row configured above —
    // so GPIO is already "bound to this stream" with no further setup.

    // check_watchdog_overflow()'s own purge targets server::Endpoint's
    // CONDITIONAL/TSCF-gated pending-request store (purge_non_safety(),
    // server.hpp) — a materially different store from the plain
    // ep_enable-disabled FIFO queue (server.hpp's own queue_/queue_len_),
    // which watchdog_purge() never touches (see this class's own
    // watchdog_purge()'s own doc comment). A directly-admitted Triggered
    // step (not one of TC18's three safety-tagged 0x8x opcodes) gives this
    // test a genuine, purgeable pending record — same fixture pattern this
    // file's own "pending_count()/watchdog_purge() report a
    // directly-admitted Triggered request's own conditional-request store"
    // test above already establishes.
    request::TriggeredStep step;
    step.trigger_source_ep = 1;
    step.trigger_signal_nr = 0;
    step.trigger_threshold = 0;
    auto trig_frame = request::encode_triggered_request(request::RequestTypeOpcode::Triggered,
                                                          mock::kGpioByteBusId, step, /*transaction_num=*/3);
    std::optional<request::RequestTypeOpcode> request_type;
    std::optional<acf::WireErrorCode>           admit_error;
    auto outcome = server.admission(mock::kGpioByteBusId)
                       ->admit(trig_frame.data(), trig_frame.size(), /*now=*/0, /*tv=*/false,
                               /*avtp_timestamp=*/0, /*gptp_reference_now=*/0, request_type,
                               /*out_index=*/nullptr, &admit_error);
    REQUIRE(outcome == server::AdmitOutcome::Pending);
    REQUIRE(server.pending_count(mock::kGpioByteBusId) == 1);

    // dispatch_e2e()'s own REQ-WDG-010 kick (a plain, non-CRC request is
    // enough — the kick fires unconditionally at the very top, before the
    // plain-command-mode/CRC branch is even decided).
    auto req     = standard_request(mock::kGpioByteBusId, /*write=*/false);
    auto frame   = acf::encode_acf_abb(req, {});
    acf::AcfMessageInfo   resp;
    std::vector<uint8_t>  resp_payload;
    auto ec = server.dispatch_e2e(0, stream_id, /*sequence_num=*/0, frame, resp, resp_payload);
    REQUIRE_FALSE(ec);

    // Before the timeout interval has elapsed, nothing overflows yet.
    size_t purged = 123;
    REQUIRE(server.check_watchdog_overflow(stream_id, /*now_ms=*/50, purged));
    REQUIRE(purged == 0);
    REQUIRE(server.pending_count(mock::kGpioByteBusId) == 1);
    REQUIRE_FALSE(server.stream_rx_blocked(stream_id));

    // Past the timeout interval since dispatch_e2e()'s own kick (at
    // now_ms=0): the watchdog overflows, latches safe state, purges the
    // one pending non-safety request, and the REQ-E2E-046 aggregate latches.
    REQUIRE(server.check_watchdog_overflow(stream_id, /*now_ms=*/150, purged));
    REQUIRE(purged == 1);
    REQUIRE(server.pending_count(mock::kGpioByteBusId) == 0);
    REQUIRE(server.stream_rx_blocked(stream_id));
}

TEST_CASE("check_watchdog_overflow()/stream_rx_blocked()/dispatch_e2e() all fail toward no "
          "action for an unresolvable stream_id",
          "[mock][e2e][REQ-E2E-046]") {
    mock::Server server;
    REQUIRE_FALSE(server.advance_to_rcp_configured());
    // Deliberately no set_request_stream_cfg() call.

    const auto stream_id = e2e_stream(0x6666);
    size_t purged = 123;
    REQUIRE_FALSE(server.check_watchdog_overflow(stream_id, /*now_ms=*/999999, purged));
    REQUIRE(purged == 0);
    REQUIRE_FALSE(server.stream_rx_blocked(stream_id));

    // dispatch_e2e()'s own kick is a silent no-op too, and plain command
    // mode (ep_req_crc_enable still unset) still dispatches normally.
    auto req     = standard_request(mock::kGpioByteBusId, /*write=*/false);
    auto frame   = acf::encode_acf_abb(req, {});
    acf::AcfMessageInfo   resp;
    std::vector<uint8_t>  resp_payload;
    auto ec = server.dispatch_e2e(0, stream_id, /*sequence_num=*/0, frame, resp, resp_payload);
    REQUIRE_FALSE(ec);
    REQUIRE(acf::response_kind_of(resp) == acf::ResponseKind::ReadResponse);
}

// ── Fragmented E2E dispatch (Phase 4/Phase 17 batch D1, cpp-RCP issue #129) ──
// Ported from c-RCP's tests/test_mock.c coverage for rcp_mock_server_
// dispatch_e2e_fragment() (REQ-E2E-038/039/046, REQ-ISELED-025, REQ-FRAG-*),
// reduced to this mock's own single public dispatch_e2e_fragment() entry
// point shape — see that method's own doc comment for why. See
// tests/test_fragment.cpp/tests/test_respqueue.cpp for fragment::Reassembler/
// respqueue::RespQueue's own already-covered unit-level behavior — this
// section is INTEGRATION coverage (via dispatch_e2e_fragment()), not a
// re-test of those classes' own internals.

namespace {

// build_fragments splits `payload` into the ordered sequence of ACF_ABB wire
// fragments dispatch_e2e_fragment() itself expects: every fragment but the
// last is plain (no CRC trailer); the last carries a genuine E2E fragmented
// CRC (REQ-E2E-038, e2e::compute_fragmented_crc — computed over the FIRST
// fragment's own raw encoded header bytes, followed by the full,
// unfragmented `payload`) via the same [header][real payload][CRC32] shape
// e2e::wrap() itself builds, mirrored here by hand since wrap() only knows
// the single-frame CRC formula. A one-segment plan (payload already fits in
// one fragment) degrades to a genuine e2e::wrap_framed() call instead — the
// exact "never fragmented" shape dispatch_e2e_fragment() itself falls back
// to dispatch_e2e() for.
std::vector<std::vector<uint8_t>> build_fragments(avtp::StreamId stream_id, avtp::ByteBusId bus_id,
                                                    uint8_t transaction_num, bool write, uint8_t evt_op,
                                                    uint16_t final_read_size, const std::vector<uint8_t>& payload,
                                                    size_t max_fragment_payload) {
    const size_t seg_count = fragment::plan_count(payload.size(), max_fragment_payload);
    REQUIRE(seg_count > 0);
    std::vector<fragment::Segment> segs(seg_count);
    REQUIRE_FALSE(fragment::plan(payload.size(), max_fragment_payload, segs.data(), seg_count));

    std::vector<uint8_t>              first_header_bytes;
    std::vector<std::vector<uint8_t>> frames;
    for (size_t i = 0; i < seg_count; ++i) {
        acf::AcfMessageInfo hdr;
        hdr.byte_bus_id               = bus_id;
        hdr.transaction_num           = transaction_num;
        hdr.op                        = write;
        hdr.evt_op                    = evt_op;
        hdr.ms                        = segs[i].ms;
        hdr.read_size_or_segment_num  = segs[i].ms ? segs[i].segment_num : final_read_size;
        const std::vector<uint8_t> slice(payload.begin() + static_cast<long>(segs[i].offset),
                                          payload.begin() + static_cast<long>(segs[i].offset + segs[i].len));

        if (segs[i].ms) {
            auto frame = acf::encode_acf_abb(hdr, slice);
            if (i == 0) {
                first_header_bytes.assign(frame.begin(), frame.begin() + static_cast<long>(acf::kAcfCommonHeaderLen));
            }
            frames.push_back(std::move(frame));
            continue;
        }

        if (seg_count == 1) {
            // Never actually fragmented — the same single-frame CRC shape
            // wrap_gpio_write() above already uses.
            frames.push_back(e2e::wrap_framed(/*is_ntscf_framed=*/true, kE2eHeaderOctet1, /*tu=*/false, stream_id,
                                               /*avtp_timestamp=*/std::nullopt, hdr, /*message_timestamp=*/std::nullopt,
                                               slice));
            continue;
        }

        hdr.acf_msg_length = acf::compute_acf_msg_length(hdr.acf_msg_type, slice.size());
        e2e::apply_acf_length_adjustment(hdr); // +1 quadlet, reflected in the header this final fragment encodes
        auto frame = acf::encode_acf_abb(hdr, slice);
        const uint32_t crc = e2e::compute_fragmented_crc(avtp::kSubtypeNtscf, kE2eHeaderOctet1, /*tu=*/false,
                                                           stream_id, /*avtp_timestamp=*/std::nullopt,
                                                           first_header_bytes, payload);
        e2e::append_crc(frame, crc);
        frames.push_back(std::move(frame));
    }
    return frames;
}

} // namespace

TEST_CASE("dispatch_e2e_fragment in plain command mode (ep_req_crc_enable unset) decodes and "
          "delegates exactly like dispatch_e2e/dispatch()",
          "[mock][e2e][fragment][REQ-E2E-038]") {
    mock::Server server;
    configure_gpio_all_outputs(server);
    REQUIRE_FALSE(server.advance_to_rcp_configured());
    // ep_req_crc_enable left at its own struct default (false).

    auto req = standard_request(mock::kGpioByteBusId, /*write=*/true,
                                 static_cast<uint8_t>(WriteSemantics::Or));
    auto payload = gpio::encode_gpio_payload(0x0000'000F);
    auto frame   = acf::encode_acf_abb(req, payload); // NOT CRC-wrapped, ms=false — plain command mode input

    acf::AcfMessageInfo   resp;
    std::vector<uint8_t>  resp_payload;
    auto ec = server.dispatch_e2e_fragment(0, e2e_stream(0x7001), frame, resp, resp_payload);

    REQUIRE_FALSE(ec);
    REQUIRE(acf::response_kind_of(resp) == acf::ResponseKind::WriteResponse);
    REQUIRE(server.gpio().read() == 0x0000'000F);
}

TEST_CASE("dispatch_e2e_fragment falls back to dispatch_e2e unchanged for an unresolvable "
          "stream_id",
          "[mock][e2e][fragment][REQ-E2E-038]") {
    mock::Server server;
    configure_gpio_all_outputs(server);
    REQUIRE_FALSE(server.advance_to_rcp_configured());
    server.registers().generic_configs[mock::kGpioEndpointId - 1].ep_req_crc_enable = true;
    // Deliberately no set_request_stream_cfg() call.

    const auto stream_id = e2e_stream(0x7002);
    auto frame = wrap_gpio_write(stream_id, /*transaction_num=*/1, 0x0000'00F0);

    acf::AcfMessageInfo   resp;
    std::vector<uint8_t>  resp_payload;
    auto ec = server.dispatch_e2e_fragment(0, stream_id, frame, resp, resp_payload);

    REQUIRE_FALSE(ec);
    REQUIRE(acf::response_kind_of(resp) == acf::ResponseKind::WriteResponse);
    REQUIRE(server.gpio().read() == 0x0000'00F0);
    REQUIRE(server.fragment_reassembler(stream_id) == nullptr); // still unresolvable — no slot exists
    REQUIRE(server.resp_queue_for_stream(stream_id) == nullptr);
}

TEST_CASE("dispatch_e2e_fragment falls back to dispatch_e2e unchanged for a genuinely "
          "single-fragment (never-fragmented) CRC-protected request",
          "[mock][e2e][fragment][REQ-E2E-038]") {
    mock::Server server;
    configure_gpio_all_outputs(server);
    REQUIRE_FALSE(server.advance_to_rcp_configured());
    server.registers().generic_configs[mock::kGpioEndpointId - 1].ep_req_crc_enable = true;

    const auto stream_id = e2e_stream(0x7003);
    regmap::RequestStreamConfig cfg;
    cfg.stream_id = stream_id;
    REQUIRE(server.set_request_stream_cfg({cfg}));

    auto frames = build_fragments(stream_id, mock::kGpioByteBusId, /*transaction_num=*/2, /*write=*/true,
                                   static_cast<uint8_t>(WriteSemantics::Or), /*final_read_size=*/0,
                                   gpio::encode_gpio_payload(0x0000'00FF), /*max_fragment_payload=*/64);
    REQUIRE(frames.size() == 1); // 4-byte payload comfortably fits in one 64-byte fragment

    acf::AcfMessageInfo   resp;
    std::vector<uint8_t>  resp_payload;
    auto ec = server.dispatch_e2e_fragment(0, stream_id, frames[0], resp, resp_payload);

    REQUIRE_FALSE(ec);
    REQUIRE(acf::response_kind_of(resp) == acf::ResponseKind::WriteResponse);
    REQUIRE(server.gpio().read() == 0x0000'00FF);
    REQUIRE_FALSE(server.fragment_reassembler(stream_id)->is_collecting());
}

TEST_CASE("dispatch_e2e_fragment reassembles a genuinely multi-fragment E2E request across an "
          "intermediate and a final fragment, and dispatches it through the same admission/"
          "handler path as dispatch()/dispatch_e2e()",
          "[mock][e2e][fragment][REQ-E2E-038][REQ-E2E-039]") {
    mock::Server server;
    configure_gpio_all_outputs(server);
    REQUIRE_FALSE(server.advance_to_rcp_configured());
    server.registers().generic_configs[mock::kGpioEndpointId - 1].ep_req_crc_enable = true;

    const auto stream_id = e2e_stream(0x7004);
    regmap::RequestStreamConfig cfg;
    cfg.stream_id = stream_id;
    REQUIRE(server.set_request_stream_cfg({cfg}));

    auto frames = build_fragments(stream_id, mock::kGpioByteBusId, /*transaction_num=*/3, /*write=*/true,
                                   static_cast<uint8_t>(WriteSemantics::Or), /*final_read_size=*/0,
                                   gpio::encode_gpio_payload(0x0000'00F0), /*max_fragment_payload=*/2);
    REQUIRE(frames.size() == 2); // 4-byte payload split into two 2-byte fragments

    acf::AcfMessageInfo   resp1;
    std::vector<uint8_t>  resp_payload1;
    auto ec1 = server.dispatch_e2e_fragment(0, stream_id, frames[0], resp1, resp_payload1);
    REQUIRE(ec1 == mock::make_error_code(mock::DispatchErrc::fragment_pending));
    REQUIRE_FALSE(resp1.rsp);
    REQUIRE(resp_payload1.empty());
    REQUIRE(server.gpio().read() == 0); // nothing dispatched yet
    REQUIRE(server.fragment_reassembler(stream_id)->is_collecting());

    acf::AcfMessageInfo   resp2;
    std::vector<uint8_t>  resp_payload2;
    auto ec2 = server.dispatch_e2e_fragment(0, stream_id, frames[1], resp2, resp_payload2);
    REQUIRE_FALSE(ec2);
    REQUIRE(acf::response_kind_of(resp2) == acf::ResponseKind::WriteResponse);
    REQUIRE(server.gpio().read() == 0x0000'00F0);
    REQUIRE_FALSE(server.fragment_reassembler(stream_id)->is_collecting());
}

TEST_CASE("dispatch_e2e_fragment rejects an out-of-order intermediate segment_num with no wire "
          "response, and resets the reassembler so a later, correctly-ordered sequence still "
          "works",
          "[mock][e2e][fragment][REQ-FRAG-001]") {
    mock::Server server;
    REQUIRE_FALSE(server.advance_to_rcp_configured());
    server.registers().generic_configs[mock::kI2cEndpointId - 1].ep_req_crc_enable = true;

    const auto stream_id = e2e_stream(0x7005);
    regmap::RequestStreamConfig cfg;
    cfg.stream_id = stream_id;
    REQUIRE(server.set_request_stream_cfg({cfg}));

    server.set_i2c_response({0xCA, 0xFE});

    auto frames = build_fragments(stream_id, mock::kI2cByteBusId, /*transaction_num=*/4, /*write=*/true,
                                   /*evt_op=*/0, /*final_read_size=*/0,
                                   std::vector<uint8_t>{0x01, 0x02, 0x03, 0x04, 0x05, 0x06},
                                   /*max_fragment_payload=*/2);
    REQUIRE(frames.size() == 3); // two intermediate segments (segment_num 0, 1) + one final

    // Feed the SECOND intermediate fragment (segment_num == 1) first — the
    // reassembler is not yet collecting and expects segment_num == 0.
    acf::AcfMessageInfo   resp;
    std::vector<uint8_t>  resp_payload;
    auto ec = server.dispatch_e2e_fragment(0, stream_id, frames[1], resp, resp_payload);
    REQUIRE(ec == mock::make_error_code(mock::DispatchErrc::fragment_rejected));
    REQUIRE_FALSE(resp.rsp);
    REQUIRE(resp_payload.empty());
    REQUIRE_FALSE(server.fragment_reassembler(stream_id)->is_collecting()); // reset, not left half-collected

    // The reassembler was reset, not left wedged — a fresh, correctly-ordered
    // sequence on the SAME stream still reassembles and dispatches cleanly.
    auto ec0 = server.dispatch_e2e_fragment(0, stream_id, frames[0], resp, resp_payload);
    REQUIRE(ec0 == mock::make_error_code(mock::DispatchErrc::fragment_pending));
    auto ec1 = server.dispatch_e2e_fragment(0, stream_id, frames[1], resp, resp_payload);
    REQUIRE(ec1 == mock::make_error_code(mock::DispatchErrc::fragment_pending));
    auto ec2 = server.dispatch_e2e_fragment(0, stream_id, frames[2], resp, resp_payload);
    REQUIRE_FALSE(ec2);
    REQUIRE(server.i2c().last_sent() == std::vector<uint8_t>{0x01, 0x02, 0x03, 0x04, 0x05, 0x06});
    REQUIRE(resp_payload == std::vector<uint8_t>{0xCA, 0xFE});
}

TEST_CASE("dispatch_e2e_fragment's fragmented CRC check (REQ-E2E-038) reports crc_error with a "
          "POCI_FAILURE error response on the final fragment, and latches the stream faulted "
          "when rx_enforce_e2e is set",
          "[mock][e2e][fragment][REQ-E2E-021][REQ-E2E-038][REQ-E2E-046]") {
    mock::Server server;
    configure_gpio_all_outputs(server);
    REQUIRE_FALSE(server.advance_to_rcp_configured());
    server.registers().generic_configs[mock::kGpioEndpointId - 1].ep_req_crc_enable = true;

    const auto stream_id = e2e_stream(0x7006);
    regmap::RequestStreamConfig cfg;
    cfg.stream_id      = stream_id;
    cfg.rx_enforce_e2e = true;
    REQUIRE(server.set_request_stream_cfg({cfg}));

    auto frames = build_fragments(stream_id, mock::kGpioByteBusId, /*transaction_num=*/5, /*write=*/true,
                                   static_cast<uint8_t>(WriteSemantics::Or), /*final_read_size=*/0,
                                   gpio::encode_gpio_payload(0x0000'00FF), /*max_fragment_payload=*/2);
    REQUIRE(frames.size() == 2);
    frames[1][frames[1].size() - 1] ^= 0xFF; // corrupt one CRC byte on the final fragment

    acf::AcfMessageInfo   resp1;
    std::vector<uint8_t>  resp_payload1;
    auto ec1 = server.dispatch_e2e_fragment(0, stream_id, frames[0], resp1, resp_payload1);
    REQUIRE(ec1 == mock::make_error_code(mock::DispatchErrc::fragment_pending));

    acf::AcfMessageInfo   resp2;
    std::vector<uint8_t>  resp_payload2;
    auto ec2 = server.dispatch_e2e_fragment(0, stream_id, frames[1], resp2, resp_payload2);
    REQUIRE(ec2 == e2e::make_error_code(e2e::E2eErrc::crc_error));
    REQUIRE(acf::response_kind_of(resp2) == acf::ResponseKind::ErrorResponse);
    REQUIRE(resp_payload2.size() == 1);
    REQUIRE(resp_payload2[0] == static_cast<uint8_t>(acf::WireErrorCode::PociFailure));
    REQUIRE(server.gpio().read() == 0); // the write never reached the endpoint
    REQUIRE(server.stream_rx_blocked(stream_id));
    REQUIRE_FALSE(server.fragment_reassembler(stream_id)->is_collecting());
}

TEST_CASE("dispatch_e2e_fragment's per-stream fragment::Reassembler capacity is a SEPARATE bound "
          "from the ACF-frame re-encode ceiling: exceeding it mid-sequence reports "
          "fragment_rejected with NO wire response at all",
          "[mock][e2e][fragment][REQ-FRAG-005]") {
    mock::Server server;
    REQUIRE_FALSE(server.advance_to_rcp_configured());
    server.registers().generic_configs[mock::kI2cEndpointId - 1].ep_req_crc_enable = true;

    const auto stream_id = e2e_stream(0x7007);
    regmap::RequestStreamConfig cfg;
    cfg.stream_id = stream_id;
    REQUIRE(server.set_request_stream_cfg({cfg}));
    // Deliberately tightened, mirroring c-RCP issue #611's own test (a
    // deliberately tightened rcp_mock_server_fragment_reassembler() ceiling
    // that admits the first fragment alone but is exceeded by the
    // reassembled total).
    REQUIRE(server.fragment_reassembler(stream_id) != nullptr);
    *server.fragment_reassembler(stream_id) = fragment::Reassembler(/*max_total_len=*/3);

    auto frames = build_fragments(stream_id, mock::kI2cByteBusId, /*transaction_num=*/6, /*write=*/true,
                                   /*evt_op=*/0, /*final_read_size=*/0, std::vector<uint8_t>{0x01, 0x02, 0x03, 0x04},
                                   /*max_fragment_payload=*/2);
    REQUIRE(frames.size() == 2); // 2 + 2 bytes; the first 2 fit under max_total_len==3, the total (4) does not

    acf::AcfMessageInfo   resp1;
    std::vector<uint8_t>  resp_payload1;
    auto ec1 = server.dispatch_e2e_fragment(0, stream_id, frames[0], resp1, resp_payload1);
    REQUIRE(ec1 == mock::make_error_code(mock::DispatchErrc::fragment_pending));

    acf::AcfMessageInfo   resp2;
    std::vector<uint8_t>  resp_payload2;
    auto ec2 = server.dispatch_e2e_fragment(0, stream_id, frames[1], resp2, resp_payload2);
    REQUIRE(ec2 == mock::make_error_code(mock::DispatchErrc::fragment_rejected));
    REQUIRE_FALSE(resp2.rsp); // NO wire response — c-RCP's own RCP_MOCK_DISPATCH_REJECTED builds none either
    REQUIRE(resp_payload2.empty());
    REQUIRE_FALSE(server.fragment_reassembler(stream_id)->is_collecting());
}

TEST_CASE("dispatch_e2e_fragment's oversized-reassembly check (c-RCP issue #614/#616) rejects a "
          "genuinely valid, CRC-correct reassembled request with a REAL WireErrorCode::"
          "RequestRejected wire ErrorResponse when it cannot be re-expressed as one ACF frame — "
          "never a silent drop",
          "[mock][e2e][fragment][REQ-E2E-038]") {
    mock::Server server;
    REQUIRE_FALSE(server.advance_to_rcp_configured());
    server.registers().generic_configs[mock::kI2cEndpointId - 1].ep_req_crc_enable = true;

    const auto stream_id = e2e_stream(0x7008);
    regmap::RequestStreamConfig cfg;
    cfg.stream_id = stream_id;
    REQUIRE(server.set_request_stream_cfg({cfg}));

    // 2050 octets: comfortably under fragment::Reassembler's own default
    // 4096-octet capacity (so reassembly itself completes and the fragmented
    // CRC genuinely validates), but over acf::kAcfAbbMaxPayload (2036) — the
    // exact "every fragment valid, combined CRC valid, doesn't fit back into
    // one frame" scenario issue #614/#616 found.
    std::vector<uint8_t> big_payload(2050);
    for (size_t i = 0; i < big_payload.size(); ++i) big_payload[i] = static_cast<uint8_t>(i);
    REQUIRE(big_payload.size() > acf::kAcfAbbMaxPayload);
    REQUIRE(big_payload.size() <= fragment::kDefaultReassemblyCapacity);

    auto frames = build_fragments(stream_id, mock::kI2cByteBusId, /*transaction_num=*/7, /*write=*/true,
                                   /*evt_op=*/0, /*final_read_size=*/0, big_payload, /*max_fragment_payload=*/700);
    REQUIRE(frames.size() == 3);

    acf::AcfMessageInfo   resp;
    std::vector<uint8_t>  resp_payload;
    for (size_t i = 0; i + 1 < frames.size(); ++i) {
        auto ec = server.dispatch_e2e_fragment(0, stream_id, frames[i], resp, resp_payload);
        REQUIRE(ec == mock::make_error_code(mock::DispatchErrc::fragment_pending));
    }

    auto ec = server.dispatch_e2e_fragment(0, stream_id, frames.back(), resp, resp_payload);
    REQUIRE(ec == regmap::make_error_code(regmap::RegMapErrc::request_rejected));
    REQUIRE(acf::response_kind_of(resp) == acf::ResponseKind::ErrorResponse); // a REAL wire response — not dropped
    REQUIRE(resp_payload.size() == 1);
    REQUIRE(resp_payload[0] == static_cast<uint8_t>(acf::WireErrorCode::RequestRejected));
    REQUIRE_FALSE(server.fragment_reassembler(stream_id)->is_collecting()); // reset, ready for a fresh sequence
}

TEST_CASE("maybe_fragment_response slices an over-large ISELED read response across multiple "
          "respqueue::RespQueue entries (REQ-ISELED-025, REQ-RMAP-062) when it does not fit in "
          "one ACF frame, byte-for-byte reconstructible from the queue",
          "[mock][e2e][fragment][respqueue][REQ-ISELED-025][REQ-RMAP-062]") {
    mock::Server server;
    REQUIRE_FALSE(server.advance_to_rcp_configured());
    server.registers().generic_configs[mock::kIseledEndpointId - 1].ep_req_crc_enable = true;
    // rx_resp_stream_index defaults to 1 (RequestStreamConfig's own struct
    // default) — give it a real response_streams[] row to resolve against;
    // a default-valued row (max_avtpdu_size == 0) keeps the ceiling at this
    // ACF variant's own kAcfAbbMaxPayload.
    server.registers().response_streams = {regmap::ResponseQueueConfig{}};

    const auto stream_id = e2e_stream(0x7009);
    regmap::RequestStreamConfig cfg;
    cfg.stream_id = stream_id;
    REQUIRE(server.set_request_stream_cfg({cfg}));

    std::vector<uint8_t> scripted(3000);
    for (size_t i = 0; i < scripted.size(); ++i) scripted[i] = static_cast<uint8_t>(i * 7 + 1);
    server.set_iseled_response(scripted);
    REQUIRE(scripted.size() > acf::kAcfAbbMaxPayload);

    // A tiny (5-byte) ISELED request payload, artificially split into two
    // 3-byte fragments — just enough to force dispatch_e2e_fragment()'s own
    // genuinely-reassembled path (maybe_fragment_response() is only reached
    // from there, not from the "never fragmented" dispatch_e2e() fallback).
    const std::vector<uint8_t> request_payload{0x03, 0x01, 0x02, 0x11, 0x22};
    auto frames = build_fragments(stream_id, mock::kIseledByteBusId, /*transaction_num=*/8, /*write=*/true,
                                   /*evt_op=*/0, /*final_read_size=*/0, request_payload, /*max_fragment_payload=*/3);
    REQUIRE(frames.size() == 2);

    acf::AcfMessageInfo   resp;
    std::vector<uint8_t>  resp_payload;
    auto ec1 = server.dispatch_e2e_fragment(0, stream_id, frames[0], resp, resp_payload);
    REQUIRE(ec1 == mock::make_error_code(mock::DispatchErrc::fragment_pending));

    auto ec2 = server.dispatch_e2e_fragment(0, stream_id, frames[1], resp, resp_payload);
    REQUIRE(ec2 == mock::make_error_code(mock::DispatchErrc::response_fragmented));
    REQUIRE_FALSE(resp.rsp);          // nothing to send synchronously...
    REQUIRE(resp_payload.empty());    // ...the real response is on the queue instead.

    respqueue::RespQueue* queue = server.resp_queue_for_stream(stream_id);
    REQUIRE(queue != nullptr);
    REQUIRE(queue->len() > 1); // genuinely fragmented, not one lone entry

    // Drain the queue and reassemble: every fragment but the last must carry
    // ms=true with a strictly increasing segment_num starting at 0; the last
    // must carry ms=false. Concatenating every fragment's own payload in
    // order must reproduce the original scripted bytes exactly.
    std::vector<uint8_t> reassembled;
    uint16_t             expected_segment_num = 0;
    size_t               popped               = 0;
    std::vector<uint8_t> frame;
    while (queue->pop(frame)) {
        acf::AcfMessageInfo  hdr;
        std::vector<uint8_t> payload;
        REQUIRE_FALSE(acf::decode_acf_abb(frame.data(), frame.size(), hdr, payload));
        ++popped;
        if (hdr.ms) {
            REQUIRE(hdr.read_size_or_segment_num == expected_segment_num);
            ++expected_segment_num;
        }
        reassembled.insert(reassembled.end(), payload.begin(), payload.end());
    }
    REQUIRE(popped > 1);
    REQUIRE(reassembled == scripted);
}

TEST_CASE("maybe_fragment_response rejects an over-large response with a REAL "
          "WireErrorCode::RequestRejected wire ErrorResponse (not a silent drop) when the "
          "resolved request stream's own rx_resp_stream_index names no configured response-"
          "queue row",
          "[mock][e2e][fragment][respqueue][REQ-ISELED-025]") {
    mock::Server server;
    REQUIRE_FALSE(server.advance_to_rcp_configured());
    server.registers().generic_configs[mock::kIseledEndpointId - 1].ep_req_crc_enable = true;
    // Deliberately no regs_.response_streams row — rx_resp_stream_index (== 1
    // by default) resolves to nothing.

    const auto stream_id = e2e_stream(0x700A);
    regmap::RequestStreamConfig cfg;
    cfg.stream_id = stream_id;
    REQUIRE(server.set_request_stream_cfg({cfg}));

    std::vector<uint8_t> scripted(3000, 0xAB);
    server.set_iseled_response(scripted);

    const std::vector<uint8_t> request_payload{0x03, 0x01, 0x02, 0x11, 0x22};
    auto frames = build_fragments(stream_id, mock::kIseledByteBusId, /*transaction_num=*/9, /*write=*/true,
                                   /*evt_op=*/0, /*final_read_size=*/0, request_payload, /*max_fragment_payload=*/3);
    REQUIRE(frames.size() == 2);

    acf::AcfMessageInfo   resp;
    std::vector<uint8_t>  resp_payload;
    REQUIRE(server.dispatch_e2e_fragment(0, stream_id, frames[0], resp, resp_payload) ==
            mock::make_error_code(mock::DispatchErrc::fragment_pending));

    auto ec = server.dispatch_e2e_fragment(0, stream_id, frames[1], resp, resp_payload);
    REQUIRE(ec == regmap::make_error_code(regmap::RegMapErrc::request_rejected));
    REQUIRE(acf::response_kind_of(resp) == acf::ResponseKind::ErrorResponse);
    REQUIRE(resp_payload.size() == 1);
    REQUIRE(resp_payload[0] == static_cast<uint8_t>(acf::WireErrorCode::RequestRejected));
    REQUIRE(server.resp_queue_for_stream(stream_id) == nullptr);
}

TEST_CASE("maybe_fragment_response honors a configured response-stream max_avtpdu_size "
          "(REQ-RMAP-062), producing MORE, smaller fragments than the default ACF-ceiling split",
          "[mock][e2e][fragment][respqueue][REQ-RMAP-062]") {
    mock::Server server;
    REQUIRE_FALSE(server.advance_to_rcp_configured());
    server.registers().generic_configs[mock::kIseledEndpointId - 1].ep_req_crc_enable = true;

    regmap::ResponseQueueConfig rq_cfg;
    rq_cfg.max_avtpdu_size = 32; // quadlets -> 128 octets, well under kAcfAbbMaxPayload
    server.registers().response_streams = {rq_cfg};

    const auto stream_id = e2e_stream(0x700B);
    regmap::RequestStreamConfig cfg;
    cfg.stream_id = stream_id;
    REQUIRE(server.set_request_stream_cfg({cfg}));

    std::vector<uint8_t> scripted(300, 0x5A);
    server.set_iseled_response(scripted);

    const std::vector<uint8_t> request_payload{0x03, 0x01, 0x02, 0x11, 0x22};
    auto frames = build_fragments(stream_id, mock::kIseledByteBusId, /*transaction_num=*/10, /*write=*/true,
                                   /*evt_op=*/0, /*final_read_size=*/0, request_payload, /*max_fragment_payload=*/3);
    REQUIRE(frames.size() == 2);

    acf::AcfMessageInfo   resp;
    std::vector<uint8_t>  resp_payload;
    REQUIRE(server.dispatch_e2e_fragment(0, stream_id, frames[0], resp, resp_payload) ==
            mock::make_error_code(mock::DispatchErrc::fragment_pending));
    REQUIRE(server.dispatch_e2e_fragment(0, stream_id, frames[1], resp, resp_payload) ==
            mock::make_error_code(mock::DispatchErrc::response_fragmented));

    respqueue::RespQueue* queue = server.resp_queue_for_stream(stream_id);
    REQUIRE(queue != nullptr);
    const size_t configured_ceiling =
        respqueue::RespQueue::max_fragment_payload(static_cast<size_t>(rq_cfg.max_avtpdu_size) * 4,
                                                     acf::kAcfCommonHeaderLen);
    const size_t expected_fragments = fragment::plan_count(scripted.size(), configured_ceiling);
    REQUIRE(expected_fragments > 1);
    REQUIRE(queue->len() == expected_fragments);

    std::vector<uint8_t> reassembled;
    std::vector<uint8_t> frame;
    while (queue->pop(frame)) {
        acf::AcfMessageInfo  hdr;
        std::vector<uint8_t> payload;
        REQUIRE_FALSE(acf::decode_acf_abb(frame.data(), frame.size(), hdr, payload));
        REQUIRE(frame.size() <= configured_ceiling + acf::kAcfCommonHeaderLen);
        reassembled.insert(reassembled.end(), payload.begin(), payload.end());
    }
    REQUIRE(reassembled == scripted);
}

TEST_CASE("fragment_reassembler()/resp_queue_for_stream() fail toward nullptr for an "
          "unresolvable stream_id",
          "[mock][e2e][fragment][respqueue]") {
    mock::Server server;
    const auto stream_id = e2e_stream(0x700C);
    // Deliberately no set_request_stream_cfg() call.
    REQUIRE(server.fragment_reassembler(stream_id) == nullptr);
    REQUIRE(server.resp_queue_for_stream(stream_id) == nullptr);
}
