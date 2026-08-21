// fusa:req REQ-MOCK-001
// fusa:req REQ-MOCK-002
// fusa:req REQ-MOCK-003
// fusa:req REQ-MOCK-004
// fusa:req REQ-MOCK-005
// fusa:req REQ-MOCK-006
// fusa:req REQ-MOCK-007
// fusa:req REQ-MOCK-008
// fusa:req REQ-MOCK-009
// fusa:req REQ-MOCK-010
// fusa:req REQ-MOCK-011
// fusa:req REQ-MOCK-012
// fusa:req REQ-MOCK-013
// fusa:req REQ-MOCK-014
// fusa:req REQ-MOCK-015
// fusa:req REQ-MOCK-016
// fusa:req REQ-MOCK-017
// fusa:req REQ-MOCK-018
// fusa:req REQ-MOCK-019
// fusa:req REQ-MOCK-020
// fusa:req REQ-MOCK-021
// fusa:req REQ-MOCK-022
// fusa:req REQ-MOCK-023
// fusa:req REQ-MOCK-024
// fusa:req REQ-MOCK-025
// fusa:req REQ-MOCK-026

// In-process RC Server simulator — a small, representative OPEN Alliance
// TC18 Remote Control Protocol Specification v0.5.1_RC server built
// entirely on the new stream/endpoint/register-map model, for unit tests
// and other in-process callers that want something more realistic than a
// hand-rolled stub to dispatch requests against.
//
// ROADMAP.md milestone 56, "Test & Simulation Harness Rebuild (v2.12.0)",
// opening Phase 14's final pair of milestones: this header REPLACES this
// file's pre-replacement content in full, per the Satellite Package
// Disposition table's entry for `mock.hpp` — the prior in-process
// `Controller`/`Registry` pair built on rcp.hpp's Zone/Command model is
// discarded, not adapted, since it has no analog once addressing moves
// from zone name to server+endpoint identifier. That old content is
// preserved unchanged, under rcp/legacy_mock.hpp, purely so the
// still-untouched old-model dependents that build against it
// (rcp/capi_impl.hpp, rcp/cli.hpp, rcp/config.hpp, all v2.16.0; and every
// test file using it as a generic in-process test double for its own
// not-yet-rebuilt package) keep working until each is rebound at its own
// later milestone — see rcp/legacy_mock.hpp's own header comment and
// rcp/legacy_wire.hpp's equivalent precedent at v2.0.0.
//
// mock::Server holds a real rcp::lifecycle::ServerLifecycle (v2.1.0), a
// real rcp::regmap::RegisterMap plus rcp::regmap::Ep0 (v2.1.0, including
// EP0 whole-map-read and root-client write semantics), and one instance
// each of ten fully-built endpoint types — rcp::gpio::GpioEndpoint and
// rcp::spi::SpiEndpoint (both v2.3.0), plus rcp::i2c::I2cEndpoint,
// rcp::adc::AdcEndpoint, rcp::pwm::PwmInEndpoint, rcp::lin::LinEndpoint,
// rcp::can::CanEndpoint, rcp::uart::UartEndpoint, rcp::iseled::IseledEndpoint,
// and rcp::mdio::MdioEndpoint (v2.4.0/post-v2.7.0, wired in by the Table
// 30/33 Row 2 evt[2:0] validation pilot and its ADC, PWM_IN, LIN, CAN,
// UART, ISELED, and MDIO follow-ups, in that order — UART's was this
// header's FIRST wiring of rcp::uart::UartEndpoint at all, not merely an
// extension of a pre-existing dispatch_uart(); see dispatch_uart's own
// comment for why UART needed its own req.op-branching shape; ISELED's is
// this header's FIRST wiring of rcp::iseled::IseledEndpoint at all — see
// dispatch_iseled's own comment for why it decodes/encodes the wire
// payload via ISELED's own existing Figure 40/41 codec rather than passing
// raw bytes through untouched; MDIO's is this header's FIRST wiring of
// rcp::mdio::MdioEndpoint at all — see dispatch_mdio's own comment for why,
// unlike ISELED, no MDIO byte-level wire codec exists anywhere in this
// codebase to decode/encode against, and what deliberately simplified
// choice this dispatch path makes instead) — as its representative
// endpoint set. dispatch() below is the single
// request/response entry point a test drives, decoding the standard
// request kind's evt[2:0]/op fields (rcp/acf.hpp, v2.0.0) the same way a
// real request-dispatch loop would. Conditional request kinds (v2.5.0),
// E2E CRC safe points (v2.6.0), and watchdog wiring (v2.10.0) are
// deliberately layered on top by rcp/sim.hpp rather than folded in here —
// this header's own scope is the server model and a representative
// endpoint set, matching the roadmap's own split between the two files.
//
// Whole-register-map wire serialization remains out of scope, per
// rcp/regmap.hpp's own header comment — EP0's dispatch()-level read
// answers with just the register map's magic number (the one field a
// rcp/discovery.hpp-shaped read actually needs by default), and EP0
// writes are only reachable through ep0()'s direct write_whole_map() call,
// not through dispatch(), same "data model, not wire codec" scope split
// regmap.hpp itself documents.
//
// Field names and behavior below implement TC18's *behavior* as described
// in an internal structured extraction of the specification named above;
// no text from that document is reproduced here. The concrete endpoint
// numbering (GPIO at endpoint id / byte_bus_id 1, SPI at 2, I2C at 3, ADC at
// 4, PWM_IN at 5, LIN at 6, CAN at 7, UART at 8, ISELED at 9, MDIO at 10),
// access-policy choice for operational requests (gated on lifecycle state
// only, not per-endpoint ownership — see dispatch()'s own comment), and EP0
// partial-read encoding chosen in this file are this implementation's own,
// purely for the purposes of being a usable in-process simulator — full
// bit-for-bit conformance against other TC18 implementations is not claimed,
// same as the equivalent disclaimers in rcp/regmap.hpp, rcp/lifecycle.hpp,
// rcp/gpio.hpp, rcp/spi.hpp, rcp/i2c.hpp, rcp/adc.hpp, rcp/pwm.hpp,
// rcp/lin.hpp, rcp/can.hpp, rcp/uart.hpp, rcp/iseled.hpp, and rcp/mdio.hpp.
#pragma once

#include <rcp/acf.hpp>
#include <rcp/adc.hpp>
#include <rcp/avtp.hpp>
#include <rcp/can.hpp>
#include <rcp/endpoint.hpp>
#include <rcp/gpio.hpp>
#include <rcp/i2c.hpp>
#include <rcp/iseled.hpp>
#include <rcp/lifecycle.hpp>
#include <rcp/lin.hpp>
#include <rcp/mdio.hpp>
#include <rcp/pwm.hpp>
#include <rcp/regmap.hpp>
#include <rcp/spi.hpp>
#include <rcp/uart.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <system_error>
#include <vector>

namespace rcp {
namespace mock {

// ── Representative endpoint set ──────────────────────────────────────────────
// Endpoint ids double as byte_bus_id values in this simulator — a
// deliberately simple 1:1 choice this mock is free to make since it owns
// both the register map and the ep_id_mapping table it populates itself
// (extraction §3.9's client-guaranteed table-order caveat, which
// rcp/regmap.hpp flags explicitly, does not apply here: there is no
// client populating this table, only this constructor).

using regmap::EndpointId;

constexpr EndpointId   kGpioEndpointId  = 1;
constexpr EndpointId   kSpiEndpointId   = 2;
constexpr EndpointId   kI2cEndpointId   = 3;
constexpr EndpointId   kAdcEndpointId   = 4;
constexpr EndpointId   kPwmInEndpointId = 5;
constexpr EndpointId   kLinEndpointId   = 6;
constexpr EndpointId   kCanEndpointId   = 7;
constexpr EndpointId   kUartEndpointId  = 8;
constexpr EndpointId   kIseledEndpointId = 9;
constexpr EndpointId   kMdioEndpointId   = 10;
constexpr avtp::ByteBusId kGpioByteBusId  = static_cast<avtp::ByteBusId>(kGpioEndpointId);
constexpr avtp::ByteBusId kSpiByteBusId   = static_cast<avtp::ByteBusId>(kSpiEndpointId);
constexpr avtp::ByteBusId kI2cByteBusId   = static_cast<avtp::ByteBusId>(kI2cEndpointId);
constexpr avtp::ByteBusId kAdcByteBusId   = static_cast<avtp::ByteBusId>(kAdcEndpointId);
constexpr avtp::ByteBusId kPwmInByteBusId = static_cast<avtp::ByteBusId>(kPwmInEndpointId);
constexpr avtp::ByteBusId kLinByteBusId   = static_cast<avtp::ByteBusId>(kLinEndpointId);
constexpr avtp::ByteBusId kCanByteBusId   = static_cast<avtp::ByteBusId>(kCanEndpointId);
constexpr avtp::ByteBusId kUartByteBusId  = static_cast<avtp::ByteBusId>(kUartEndpointId);
constexpr avtp::ByteBusId kIseledByteBusId = static_cast<avtp::ByteBusId>(kIseledEndpointId);
constexpr avtp::ByteBusId kMdioByteBusId   = static_cast<avtp::ByteBusId>(kMdioEndpointId);

// A discovery-shaped EP0 read only ever answers with the register map's
// magic number below — see this header's own scope note above.
constexpr size_t kEp0PartialReadLen = sizeof(uint32_t);

// ── Error-response byte_msg_payload (extraction §12.9.6, Table 27; issue cpp-RCP-02) ──
// "The error response shall contain a byte_msg_payload with an error code."
// Every dispatch_*() error path below carries an internal std::error_code
// from whichever subsystem raised it (rcp/regmap.hpp's RegMapErrc, an
// rcp/avtp.hpp short_buffer from a payload-length check, ...); this
// translates that internal condition to the acf::WireErrorCode
// (Table 27) the wire error response payload must actually carry — the two
// are deliberately independent numbering spaces (see acf.hpp's own
// WireErrorCode comment), so this mapping is name-based, not a numeric
// cast. Falls back to UnsupportedCmd (Table 27's own value 1) for anything
// this mock does not otherwise recognize, a reasonably conservative default
// rather than silently picking an unrelated specific code.
inline acf::WireErrorCode wire_error_code_for(const std::error_code& ec) noexcept {
    if (ec == make_error_code(regmap::RegMapErrc::unauthorized_access)) return acf::WireErrorCode::UnauthorizedAccess;
    if (ec == make_error_code(regmap::RegMapErrc::locked_mem_access))   return acf::WireErrorCode::LockedMemAccess;
    if (ec == make_error_code(regmap::RegMapErrc::request_rejected))    return acf::WireErrorCode::RequestRejected;
    if (ec == make_error_code(regmap::RegMapErrc::invalid_parameter))   return acf::WireErrorCode::InvalidParameter;
    if (ec == avtp::make_error_code(avtp::AvtpErrc::short_buffer))      return acf::WireErrorCode::InvalidParameter; // e.g. GPIO/SPI payload not exactly the required length
    if (ec == make_error_code(gpio::GpioErrc::pin_index_out_of_range))  return acf::WireErrorCode::InvalidParameter;
    if (ec == make_error_code(spi::SpiErrc::channel_out_of_range))      return acf::WireErrorCode::UnsupportedCmd; // reserved evt[2:0] selection, extraction §5.4/Table entries at 110b
    if (ec == endpoint::make_error_code(endpoint::EndpointErrc::reserved_evt_row2))
        return acf::WireErrorCode::UnsupportedCmd; // Table 33 Row 2 evt[2:0] in 001b-110b, extraction §13.5
    if (ec == make_error_code(i2c::I2cErrc::config_write_not_supported))
        return acf::WireErrorCode::UnsupportedCmd; // evt[2:0]==111b config-write, not yet implemented by this mock
    if (ec == make_error_code(i2c::I2cErrc::nack))                      return acf::WireErrorCode::EpError;
    if (ec == make_error_code(adc::AdcErrc::reconfig_short) ||
        ec == make_error_code(adc::AdcErrc::reconfig_out_of_range))
        return acf::WireErrorCode::InvalidParameter; // evt[2:0]==111b config-write payload malformed/out of range (Phase 3, adc.hpp's own apply_reconfig)
    if (ec == make_error_code(adc::AdcErrc::no_signal))
        return acf::WireErrorCode::EpError; // internal no-valid-sample condition, not a TC18-defined ADC error code (see adc.hpp's own comment) — mirrors i2c::I2cErrc::nack's mapping above
    if (ec == make_error_code(pwm::PwmErrc::config_write_not_supported))
        return acf::WireErrorCode::UnsupportedCmd; // evt[2:0]==111b config-write, not yet implemented by this mock
    if (ec == make_error_code(pwm::PwmErrc::no_signal))
        return acf::WireErrorCode::PwmInNoSignal; // Table 27's own dedicated PWM_IN_NO_SIGNAL(9) code — unlike adc::AdcErrc::no_signal above, TC18 defines a real numbered code for this condition, so this maps to it directly rather than falling back to EpError
    if (ec == make_error_code(lin::LinErrc::config_write_not_supported))
        return acf::WireErrorCode::UnsupportedCmd; // evt[2:0]==111b config-write, not yet implemented by this mock
    if (ec == make_error_code(lin::LinErrc::no_response))
        return acf::WireErrorCode::EpError; // internal no-response condition, not a TC18-defined LIN error code — mirrors i2c::I2cErrc::nack's mapping above
    if (ec == make_error_code(can::CanErrc::config_write_not_supported))
        return acf::WireErrorCode::UnsupportedCmd; // evt[2:0]==111b config-write, not yet implemented by this mock
    if (ec == make_error_code(can::CanErrc::identifier_out_of_range))
        return acf::WireErrorCode::InvalidParameter; // CanIdentifier::value exceeds its 11-/29-bit range
    if (ec == make_error_code(can::CanErrc::payload_exceeds_format_limit))
        return acf::WireErrorCode::InvalidParameter; // frame payload exceeds the selected FrameFormat's own ceiling
    if (ec == make_error_code(can::CanErrc::xl_payload_exceeds_single_avtpdu_bound))
        return acf::WireErrorCode::EpError; // internal single-AVTPDU capability bound, not a TC18-defined CAN error code — mirrors i2c::I2cErrc::nack's mapping above
    if (ec == make_error_code(uart::UartErrc::config_write_not_supported))
        return acf::WireErrorCode::UnsupportedCmd; // evt[2:0]==111b config-write, not yet implemented by this mock
    if (ec == make_error_code(uart::UartErrc::read_size_exceeds_bound))
        return acf::WireErrorCode::InvalidParameter; // requested read_size exceeds kMaxReadSize — client-caused, mirrors avtp::short_buffer's mapping above
    if (ec == make_error_code(uart::UartErrc::tx_queue_overflow))
        return acf::WireErrorCode::InvalidParameter; // write payload would overflow the TX queue — client-caused, same rationale as read_size_exceeds_bound above
    if (ec == make_error_code(iseled::IseledErrc::config_write_not_supported))
        return acf::WireErrorCode::UnsupportedCmd; // evt[2:0]==111b config-write, not yet implemented by this mock
    if (ec == make_error_code(iseled::IseledErrc::field_out_of_range))
        return acf::WireErrorCode::InvalidParameter; // instruction/address/data exceeds its documented wire field width — client-caused, same rationale as avtp::short_buffer's mapping above
    if (ec == make_error_code(mdio::MdioErrc::config_write_not_supported))
        return acf::WireErrorCode::UnsupportedCmd; // evt[2:0]==111b config-write, not yet implemented by this mock
    if (ec == make_error_code(mdio::MdioErrc::payload_exceeds_mode_width))
        return acf::WireErrorCode::InvalidParameter; // mdio_payload exceeds the width mdio_mode assigns it — client-caused, same rationale as avtp::short_buffer's mapping above
    return acf::WireErrorCode::UnsupportedCmd;
}

// ── Server ────────────────────────────────────────────────────────────────────
// A single simulated RC Server instance. Not copyable — regmap::Ep0 holds
// references into this object's own RegisterMap/ServerLifecycle members,
// so a Server is meant to be owned by reference or a smart pointer, the
// same restriction Ep0 itself already carries.
class Server final {
public:
    Server()
        : lifecycle_(),
          regs_(make_initial_register_map()),
          ep0_(regs_, lifecycle_) {}

    Server(const Server&)            = delete;
    Server& operator=(const Server&) = delete;

    lifecycle::ServerLifecycle&       lifecycle() noexcept { return lifecycle_; }
    const lifecycle::ServerLifecycle& lifecycle() const noexcept { return lifecycle_; }

    regmap::RegisterMap&       registers() noexcept { return regs_; }
    const regmap::RegisterMap& registers() const noexcept { return regs_; }

    regmap::Ep0& ep0() noexcept { return ep0_; }

    gpio::GpioEndpoint& gpio() noexcept { return gpio_; }
    spi::SpiEndpoint&   spi() noexcept { return spi_; }
    i2c::I2cEndpoint&   i2c() noexcept { return i2c_; }
    adc::AdcEndpoint&   adc() noexcept { return adc_; }
    pwm::PwmInEndpoint& pwm_in() noexcept { return pwm_in_; }
    lin::LinEndpoint&   lin() noexcept { return lin_; }
    can::CanEndpoint&   can() noexcept { return can_; }
    uart::UartEndpoint& uart() noexcept { return uart_; }
    iseled::IseledEndpoint& iseled() noexcept { return iseled_; }
    mdio::MdioEndpoint&     mdio() noexcept { return mdio_; }

    // set_spi_poci scripts the bytes a subsequent dispatch()/transfer()
    // call on `channel` reads back as POCI-in data. A real SPI peripheral's
    // response is whatever hardware is attached to that channel; this
    // simulator lets a test script it directly instead of modeling actual
    // hardware.
    void set_spi_poci(uint8_t channel, std::vector<uint8_t> data) {
        if (channel < spi::kMaxChannels) spi_poci_[channel] = std::move(data);
    }

    // set_i2c_response scripts the bytes and ack/nack outcome a subsequent
    // dispatch()-driven I2C transfer reads back — the same "test scripts
    // the bus, this mock does not model actual hardware" pattern
    // set_spi_poci above already establishes. Applies to the next plain
    // (evt[2:0]==000b) I2C request only in spirit — like SPI's POCI script,
    // it stays in effect until overwritten, there is no auto-clear.
    void set_i2c_response(std::vector<uint8_t> data, bool acked = true) {
        i2c_response_ = std::move(data);
        i2c_acked_    = acked;
    }

    // set_adc_response scripts the queue of 16-bit sample values a
    // subsequent dispatch()-driven ADC plain (evt[2:0]==000b) request pulls
    // from — the same "test scripts the sensor, this mock does not model
    // actual hardware" pattern set_spi_poci/set_i2c_response above already
    // establish. dispatch_adc() below always requests against a default-
    // constructed AdcAveragingConfig (adc_avg_intervals_per_request==1,
    // adc_combine_avg_values==1 — no averaging), so each dispatched plain
    // ADC request consumes exactly one scripted sample, oldest first;
    // scripted samples are consumed and NOT auto-refilled, so dispatching
    // more plain requests than scripted samples reports AdcErrc::no_signal,
    // the same underrun behavior AdcEndpoint::request_reading itself
    // already implements when `take_sample` returns std::nullopt.
    void set_adc_response(std::vector<uint16_t> samples) {
        adc_samples_.assign(samples.begin(), samples.end());
    }

    // set_pwm_in_response scripts the PwmValue (period, active_duration) a
    // subsequent dispatch()-driven plain (evt[2:0]==000b) PWM_IN request
    // answers with — the same "test scripts the sensor, this mock does not
    // model actual hardware" pattern set_spi_poci/set_i2c_response/
    // set_adc_response above already establish. Unlike set_adc_response's
    // FIFO queue, this calls pwm::PwmInEndpoint::record_measurement
    // directly: PWM_IN's own read model (rcp/pwm.hpp) reports the most
    // recently recorded measurement persistently, not a one-shot consumed
    // sample the way ADC's take_sample callback models, so there is
    // nothing to queue — the scripted value stays in effect (and fires
    // both Table 44 trigger signals, per record_measurement's own
    // contract) until this is called again, or pwm_in().clear_signal() is
    // used to model signal loss (a subsequent plain request then answers
    // PwmErrc::no_signal / wire error code PWM_IN_NO_SIGNAL again).
    void set_pwm_in_response(pwm::PwmValue value) {
        pwm_in_.record_measurement(value);
    }

    // set_lin_response scripts the bytes and responded/no-response outcome
    // a subsequent dispatch()-driven LIN transfer reads back — the same
    // "test scripts the bus, this mock does not model actual hardware"
    // pattern set_spi_poci/set_i2c_response above already establish
    // (LinEndpoint::transfer mirrors I2cEndpoint::transfer's shape, so this
    // mirrors set_i2c_response's shape too). Applies to the next plain
    // (evt[2:0]==000b) LIN request only in spirit — like set_i2c_response,
    // it stays in effect until overwritten, there is no auto-clear.
    void set_lin_response(std::vector<uint8_t> data, bool responded = true) {
        lin_response_  = std::move(data);
        lin_responded_ = responded;
    }

    // No set_can_response()-shaped hook here: deliberately, not an
    // oversight. set_spi_poci/set_i2c_response/set_adc_response/
    // set_pwm_in_response/set_lin_response above all script data this mock
    // reads back into a *response* payload for the next dispatched request.
    // CAN's request-side operation (extraction §13.7.11.3) is transmit() —
    // a fire-a-frame TX call whose outcome is entirely determined by the
    // request's own frame contents (identifier range + FrameFormat payload
    // ceiling, both already validated deterministically by
    // can::validate_frame), not by any external "current bus state" a test
    // would need to inject. A successful Plain CAN request has no response
    // *data* to script in the first place (see dispatch_can's own
    // comment): out_resp_payload stays empty, unlike every other
    // dispatch_*() above. can() above already exposes CanEndpoint directly
    // for any test that wants to inspect last_transmitted()/
    // last_received() or drive receive()/acceptance filters itself.

    // No set_uart_response()-shaped hook here either, same rationale as
    // set_can_response's own note directly above: UART's RX side (extraction
    // §13.7.8.1) is fed by rx_fill() pushing bytes as if they had arrived
    // off the wire, not by scripting a one-shot response value the way
    // set_i2c_response/set_lin_response do for their own bus-transfer
    // models. uart() above already exposes UartEndpoint directly for any
    // test that wants to call rx_fill()/drain_tx() or inspect
    // rx_available() itself — see dispatch_uart's own comment for how a
    // dispatched read request drains whatever rx_fill() has already put
    // there.

    // set_iseled_response scripts the IseledResponse (address/data) value a
    // subsequent dispatch()-driven ISELED transaction records — the same
    // "test scripts the bus, this mock does not model actual hardware"
    // pattern set_i2c_response/set_lin_response above already establish.
    // Unlike those two, which script a raw byte vector, ISELED's response
    // is already a fixed Address/Data struct (Figure 41), so this scripts
    // that struct directly rather than a byte vector. Applies to the next
    // plain (evt[2:0]==000b) ISELED request only in spirit — like
    // set_i2c_response, it stays in effect until overwritten, there is no
    // auto-clear. Defaults to IseledResponse{0, 0} (a validly in-range,
    // all-zero response) until first called.
    void set_iseled_response(iseled::IseledResponse response) {
        iseled_response_ = response;
    }

    // No set_mdio_response()-shaped hook here either, but for yet a
    // different reason than set_can_response's/the UART note's own: MDIO's
    // model (rcp/mdio.hpp) is a self-contained (mode, mdio_address)-keyed
    // register map, not an external bus — a value dispatch_mdio() writes
    // round-trips straight back out of mdio::MdioEndpoint itself on a
    // subsequent read (see MdioEndpoint::transact's own comment and
    // REQ-MDIO-003), so there is nothing external left for a test to
    // script. mdio() above already exposes MdioEndpoint directly for any
    // test that wants to call transact()/handle_request() itself.

    // advance_to_rcp_configured is a convenience for tests/simulators that
    // don't care about exercising ServerLifecycle's intermediate
    // plausibility-check gating themselves and just want a fully live
    // server to dispatch operational requests against — this Server
    // supplies no PlausibilityCheck of its own (both transitions always
    // succeed), so this always succeeds too.
    std::error_code advance_to_rcp_configured() noexcept {
        auto ec = lifecycle_.advance(lifecycle::ServerState::HwConfigured);
        if (ec) return ec;
        return lifecycle_.advance(lifecycle::ServerState::RcpConfigured);
    }

    // dispatch is this simulator's single request/response entry point,
    // modeling the standard request kind (rcp::acf::RequestKind::Standard,
    // v2.0.0) only — conditional kinds are rcp/request.hpp's concern, not
    // this simulator's. `client` is the opaque per-connection index
    // rcp/regmap.hpp's Ep0 already uses for root-client/ownership checks
    // (see its own header comment). Every non-EP0 endpoint's operational
    // (non-EP0) requests are gated purely on lifecycle state here — this
    // mock's own choice of "operational traffic answers once fully live,
    // rejected before that" rather than reusing Ep0::check_write_access's
    // config-block locking (which models something different: whether an
    // endpoint's *configuration* may still change, not whether the
    // endpoint may be *operated*). Returns the same std::error_code the
    // failing step below produced; out_resp is always populated (Error/Ack/
    // Read/WriteResponse, per rcp::acf::make_response) even on failure, so
    // a caller can always encode *something* back to a client.
    std::error_code dispatch(size_t client, const acf::AcfMessageInfo& req,
                              const std::vector<uint8_t>& req_payload,
                              acf::AcfMessageInfo& out_resp,
                              std::vector<uint8_t>& out_resp_payload) noexcept {
        out_resp_payload.clear();
        if (req.byte_bus_id == regmap::kEp0) return dispatch_ep0(client, req, out_resp, out_resp_payload);
        if (req.byte_bus_id == kGpioByteBusId) return dispatch_gpio(req, req_payload, out_resp, out_resp_payload);
        if (req.byte_bus_id == kSpiByteBusId) return dispatch_spi(req, req_payload, out_resp, out_resp_payload);
        if (req.byte_bus_id == kI2cByteBusId) return dispatch_i2c(req, req_payload, out_resp, out_resp_payload);
        if (req.byte_bus_id == kAdcByteBusId) return dispatch_adc(req, req_payload, out_resp, out_resp_payload);
        if (req.byte_bus_id == kPwmInByteBusId) return dispatch_pwm_in(req, req_payload, out_resp, out_resp_payload);
        if (req.byte_bus_id == kLinByteBusId) return dispatch_lin(req, req_payload, out_resp, out_resp_payload);
        if (req.byte_bus_id == kCanByteBusId) return dispatch_can(req, req_payload, out_resp, out_resp_payload);
        if (req.byte_bus_id == kUartByteBusId) return dispatch_uart(req, req_payload, out_resp, out_resp_payload);
        if (req.byte_bus_id == kIseledByteBusId) return dispatch_iseled(req, req_payload, out_resp, out_resp_payload);
        if (req.byte_bus_id == kMdioByteBusId) return dispatch_mdio(req, req_payload, out_resp, out_resp_payload);

        return set_error_response(req, make_error_code(regmap::RegMapErrc::invalid_parameter),
                                   out_resp, out_resp_payload);
    }

private:
    // set_error_response is the single place every dispatch_*() error path
    // below builds an ErrorResponse header AND its Table 27 error-code
    // byte_msg_payload (cpp-RCP-02) — replacing the pre-fix pattern of
    // building the header alone and leaving out_resp_payload empty.
    static std::error_code set_error_response(const acf::AcfMessageInfo& req, std::error_code ec,
                                               acf::AcfMessageInfo& out_resp,
                                               std::vector<uint8_t>& out_resp_payload) noexcept {
        out_resp         = acf::make_response(req, acf::ResponseKind::ErrorResponse);
        out_resp_payload = acf::encode_error_payload(wire_error_code_for(ec));
        return ec;
    }

    static regmap::RegisterMap make_initial_register_map() {
        regmap::RegisterMap regs;
        regs.endpoint_count = 10;
        regs.generic_configs.resize(10);
        regs.functional_configs.resize(10);
        regs.ep_id_mapping = {
            {kGpioEndpointId,  kGpioByteBusId},
            {kSpiEndpointId,   kSpiByteBusId},
            {kI2cEndpointId,   kI2cByteBusId},
            {kAdcEndpointId,   kAdcByteBusId},
            {kPwmInEndpointId, kPwmInByteBusId},
            {kLinEndpointId,   kLinByteBusId},
            {kCanEndpointId,   kCanByteBusId},
            {kUartEndpointId,  kUartByteBusId},
            {kIseledEndpointId, kIseledByteBusId},
            {kMdioEndpointId,   kMdioByteBusId},
        };
        return regs;
    }

    std::error_code dispatch_ep0(size_t /*client*/, const acf::AcfMessageInfo& req,
                                  acf::AcfMessageInfo& out_resp,
                                  std::vector<uint8_t>& out_resp_payload) noexcept {
        if (req.op) {
            // Whole-map writes go through ep0().write_whole_map() at the
            // object level, not through this byte-oriented path — see this
            // header's own scope note.
            return set_error_response(req, make_error_code(regmap::RegMapErrc::request_rejected),
                                       out_resp, out_resp_payload);
        }
        auto ec = ep0_.check_read_access(regmap::kEp0);
        if (ec) return set_error_response(req, ec, out_resp, out_resp_payload);
        out_resp_payload.resize(kEp0PartialReadLen);
        avtp::detail::put_u32(out_resp_payload.data(), regs_.magic);
        out_resp = acf::make_response(req, acf::ResponseKind::ReadResponse);
        return {};
    }

    bool operational_requests_allowed() const noexcept {
        return lifecycle_.state() == lifecycle::ServerState::RcpConfigured;
    }

    std::error_code dispatch_gpio(const acf::AcfMessageInfo& req, const std::vector<uint8_t>& payload,
                                   acf::AcfMessageInfo& out_resp,
                                   std::vector<uint8_t>& out_resp_payload) noexcept {
        if (!operational_requests_allowed()) {
            return set_error_response(req, make_error_code(regmap::RegMapErrc::request_rejected),
                                       out_resp, out_resp_payload);
        }
        if (!req.op) {
            out_resp_payload = gpio::encode_gpio_payload(gpio_.read());
            out_resp = acf::make_response(req, acf::ResponseKind::ReadResponse);
            return {};
        }
        gpio::PinMask operand = 0;
        auto ec = gpio::decode_gpio_payload(payload.data(), payload.size(), operand);
        if (ec) return set_error_response(req, ec, out_resp, out_resp_payload);

        gpio::PinMask new_value = 0;
        ec = gpio_.handle_write(endpoint::write_semantics_of(req.evt_op), operand, new_value);
        if (ec) return set_error_response(req, ec, out_resp, out_resp_payload);
        out_resp_payload = gpio::encode_gpio_payload(new_value);
        out_resp = acf::make_response(req, req.evt_ack ? acf::ResponseKind::Acknowledge
                                                         : acf::ResponseKind::WriteResponse);
        return {};
    }

    // SPI is inherently full-duplex (extraction §5.4) — this dispatch path
    // deliberately does not branch on req.op the way GPIO's does: every SPI
    // request, read or write, drives one spi::SpiEndpoint::transfer() using
    // `payload` as the PICO-out bytes (empty for a pure read) and answers
    // with whatever POCI-in bytes set_spi_poci() last scripted for that
    // channel.
    std::error_code dispatch_spi(const acf::AcfMessageInfo& req, const std::vector<uint8_t>& payload,
                                  acf::AcfMessageInfo& out_resp,
                                  std::vector<uint8_t>& out_resp_payload) noexcept {
        if (!operational_requests_allowed()) {
            return set_error_response(req, make_error_code(regmap::RegMapErrc::request_rejected),
                                       out_resp, out_resp_payload);
        }
        uint8_t channel = 0;
        auto ec = spi::channel_of(req.evt_op, channel);
        if (ec) return set_error_response(req, ec, out_resp, out_resp_payload);

        ec = spi_.transfer(channel, payload, spi_poci_[channel]);
        if (ec) return set_error_response(req, ec, out_resp, out_resp_payload);
        out_resp_payload = spi_.last_received(channel);
        out_resp = acf::make_response(req, acf::ResponseKind::ReadResponse);
        return {};
    }

    // I2C, same as SPI (extraction §13.7.7.3), is a raw byte-stream
    // transfer rather than a read/write-branched register — this dispatch
    // path does not branch on req.op either. Unlike SPI, I2C's evt[2:0]
    // does not select a channel; it is Table 33 Row 2's 3-way
    // Plain/Reserved/ConfigWrite classification (rcp::endpoint::
    // evt_row2_kind_of), which I2cEndpoint::handle_request checks before
    // ever touching `payload` as transfer data — see its own doc comment
    // for why a Reserved or ConfigWrite evt must never reach transfer().
    std::error_code dispatch_i2c(const acf::AcfMessageInfo& req, const std::vector<uint8_t>& payload,
                                  acf::AcfMessageInfo& out_resp,
                                  std::vector<uint8_t>& out_resp_payload) noexcept {
        if (!operational_requests_allowed()) {
            return set_error_response(req, make_error_code(regmap::RegMapErrc::request_rejected),
                                       out_resp, out_resp_payload);
        }
        auto ec = i2c_.handle_request(req.evt_op, payload, i2c_response_, i2c_acked_);
        if (ec) return set_error_response(req, ec, out_resp, out_resp_payload);
        out_resp_payload = i2c_.last_received();
        out_resp = acf::make_response(req, req.evt_ack ? acf::ResponseKind::Acknowledge
                                                          : acf::ResponseKind::ReadResponse);
        return {};
    }

    // ADC is request-driven sampling only (extraction §5.9) with no
    // combinable write-request payload at all (adc.hpp's own header
    // comment) — this dispatch path does not branch on req.op either, same
    // rationale as dispatch_i2c/dispatch_spi above, just for a different
    // reason (ADC has no write semantics implemented anywhere in this
    // codebase, not that it is inherently full-duplex).
    //
    // Table 33 Row 2's 3-way Plain/Reserved/ConfigWrite classification
    // (rcp::endpoint::evt_row2_kind_of) is checked directly here (Phase 3:
    // adc::AdcEndpoint no longer has its own combined handle_request, since
    // c-RCP's own ep_adc.h/.c have no such function either — see adc.hpp's
    // own file header): Reserved is rejected without consuming a scripted
    // sample or touching adc_cfg_; ConfigWrite applies `payload` as a real
    // §12.7.1 addressed register write against this server's own
    // adc_cfg_ (adc::apply_reconfig, genuinely implemented as of Phase 3,
    // unlike this mock's still-open Table 30/33 dispatch wiring for other
    // endpoint types' own reconfig paths — full response-shape/register-
    // read-back wiring for every endpoint type remains Phase 4's scope, see
    // ROADMAP.md); Plain drives one AdcEndpoint::execute_measurement_cycle
    // (samples_per_avg_interval=1, combine_avg_values=1: no averaging, one
    // raw sample per request) against a take_sample callback that pulls the
    // next scripted value set_adc_response() queued. An empty scripted
    // queue reports a kAdcNoSignal-valued sample, which average_interval()
    // carries through as the response's own value on the wire (adc.hpp's
    // own corrected behavior) — this mock chooses to additionally surface
    // that as an AdcErrc::no_signal *error* response instead, preserving
    // this dispatch path's own pre-existing "queue underrun is an error"
    // contract for set_adc_response()'s own callers/tests.
    std::error_code dispatch_adc(const acf::AcfMessageInfo& req, const std::vector<uint8_t>& payload,
                                  acf::AcfMessageInfo& out_resp,
                                  std::vector<uint8_t>& out_resp_payload) noexcept {
        if (!operational_requests_allowed()) {
            return set_error_response(req, make_error_code(regmap::RegMapErrc::request_rejected),
                                       out_resp, out_resp_payload);
        }

        switch (endpoint::evt_row2_kind_of(req.evt_op)) {
        case endpoint::EvtRow2Kind::Reserved:
            return set_error_response(req, endpoint::make_error_code(endpoint::EndpointErrc::reserved_evt_row2),
                                       out_resp, out_resp_payload);
        case endpoint::EvtRow2Kind::ConfigWrite: {
            auto ec = adc::apply_reconfig(adc_cfg_, payload.data(), payload.size());
            if (ec) return set_error_response(req, ec, out_resp, out_resp_payload);
            out_resp = acf::make_response(req, acf::ResponseKind::WriteResponse);
            return {};
        }
        case endpoint::EvtRow2Kind::Plain:
            break;
        }

        adc::AdcFunctionalConfig cfg;
        cfg.adc_samples_per_avg_interval = 1;
        cfg.adc_combine_avg_values        = 1;

        auto take_sample = [this]() -> adc::AdcSample {
            if (adc_samples_.empty()) return adc::AdcSample{adc::kAdcNoSignal, 0};
            uint16_t v = adc_samples_.front();
            adc_samples_.pop_front();
            return adc::AdcSample{v, 0};
        };

        auto ec = adc_.execute_measurement_cycle(cfg, take_sample);
        if (ec) return set_error_response(req, ec, out_resp, out_resp_payload);

        std::vector<uint16_t> values;
        uint64_t              timestamp = 0;
        ec = adc_.collect_response(cfg, values, timestamp);
        if (ec) return set_error_response(req, ec, out_resp, out_resp_payload);

        if (values.front() == adc::kAdcNoSignal) {
            return set_error_response(req, adc::make_error_code(adc::AdcErrc::no_signal), out_resp,
                                       out_resp_payload);
        }

        out_resp_payload.resize(adc::kAdcValueLen);
        avtp::detail::put_u16(out_resp_payload.data(), values.front());
        out_resp = acf::make_response(req, req.evt_ack ? acf::ResponseKind::Acknowledge
                                                          : acf::ResponseKind::ReadResponse);
        return {};
    }

    // PWM_IN is a response-only read model (§13.7.6.3, rcp/pwm.hpp's own
    // header comment) with no functional request payload at all — this
    // dispatch path does not branch on req.op either, same rationale as
    // dispatch_adc's own comment (no write semantics implemented for this
    // endpoint type, not an inherently full-duplex protocol). Every
    // request addressed here drives one PwmInEndpoint::handle_request,
    // which checks Table 33 Row 2's 3-way Plain/Reserved/ConfigWrite
    // classification (rcp::endpoint::evt_row2_kind_of) before ever
    // touching the endpoint's last-measured state — so a Reserved or
    // ConfigWrite evt can never read or disturb it. Unlike dispatch_adc,
    // there is no scripted-sample queue to consume: PWM_IN's read model
    // reports the most recently recorded measurement persistently (see
    // set_pwm_in_response's own comment), so a Plain request simply
    // answers whatever is currently recorded, or PwmErrc::no_signal
    // (wire error code PwmInNoSignal) if nothing has been recorded yet or
    // the signal was cleared. `payload` is unused: §13.7.6.3 states the
    // PWM_IN request itself carries no functional byte_msg_payload.
    std::error_code dispatch_pwm_in(const acf::AcfMessageInfo& req, const std::vector<uint8_t>& /*payload*/,
                                     acf::AcfMessageInfo& out_resp,
                                     std::vector<uint8_t>& out_resp_payload) noexcept {
        if (!operational_requests_allowed()) {
            return set_error_response(req, make_error_code(regmap::RegMapErrc::request_rejected),
                                       out_resp, out_resp_payload);
        }
        pwm::PwmValue value{};
        auto ec = pwm_in_.handle_request(req.evt_op, value);
        if (ec) return set_error_response(req, ec, out_resp, out_resp_payload);
        auto wire = pwm::encode_pwm_payload(value);
        out_resp_payload.assign(wire.begin(), wire.end());
        out_resp = acf::make_response(req, req.evt_ack ? acf::ResponseKind::Acknowledge
                                                          : acf::ResponseKind::ReadResponse);
        return {};
    }

    // LIN, same as I2C (extraction §13.7.10.3), is a raw byte-stream
    // transfer rather than a read/write-branched register — this dispatch
    // path does not branch on req.op either, same rationale as
    // dispatch_i2c's own comment. Unlike SPI, LIN's evt[2:0] does not
    // select a channel; it is Table 33 Row 2's 3-way
    // Plain/Reserved/ConfigWrite classification (rcp::endpoint::
    // evt_row2_kind_of), which LinEndpoint::handle_request checks before
    // ever touching `payload` as transfer data — see its own doc comment
    // for why a Reserved or ConfigWrite evt must never reach transfer().
    std::error_code dispatch_lin(const acf::AcfMessageInfo& req, const std::vector<uint8_t>& payload,
                                  acf::AcfMessageInfo& out_resp,
                                  std::vector<uint8_t>& out_resp_payload) noexcept {
        if (!operational_requests_allowed()) {
            return set_error_response(req, make_error_code(regmap::RegMapErrc::request_rejected),
                                       out_resp, out_resp_payload);
        }
        auto ec = lin_.handle_request(req.evt_op, payload, lin_response_, lin_responded_);
        if (ec) return set_error_response(req, ec, out_resp, out_resp_payload);
        out_resp_payload = lin_.last_received();
        out_resp = acf::make_response(req, req.evt_ack ? acf::ResponseKind::Acknowledge
                                                          : acf::ResponseKind::ReadResponse);
        return {};
    }

    // CAN, unlike I2C/LIN, is a fire-a-frame TX operation with no
    // synchronous read-back — this dispatch path drives one
    // CanEndpoint::handle_request per request, same Table 33 Row 2
    // Plain/Reserved/ConfigWrite classification (rcp::endpoint::
    // evt_row2_kind_of) every other dispatch_*() above already applies,
    // which handle_request checks before ever touching `payload` as frame
    // data — see its own doc comment for why a Reserved or ConfigWrite evt
    // must never reach transmit(). Figure 40's own wire layout for a CAN
    // request's byte_msg_payload (FrameFormat + CAN ID + CAN data,
    // §13.7.11.3) is not decoded here — no byte-level CAN wire codec exists
    // anywhere in this codebase yet (see rcp/can.hpp's own header comment
    // on what it does and does not lay out); this dispatch path's own
    // simplification, scoped identically to every other "full bit-for-bit
    // conformance ... not claimed" disclaimer in this file, is to treat
    // `payload` directly as the frame's data bytes, addressed with a fixed
    // standard identifier (CanIdentifier{0, false}) and
    // FrameFormat::Classical — enough to exercise the evt[2:0]
    // classification end-to-end without inventing a real wire decode. A
    // successful Plain request has no response data to send back (see
    // set_can_response's own comment above for why there is no scripting
    // hook), so out_resp_payload stays empty and the response is a plain
    // Acknowledge/WriteResponse, mirroring dispatch_gpio's write-response
    // shape rather than dispatch_i2c/dispatch_lin's read-response shape.
    std::error_code dispatch_can(const acf::AcfMessageInfo& req, const std::vector<uint8_t>& payload,
                                  acf::AcfMessageInfo& out_resp,
                                  std::vector<uint8_t>& out_resp_payload) noexcept {
        if (!operational_requests_allowed()) {
            return set_error_response(req, make_error_code(regmap::RegMapErrc::request_rejected),
                                       out_resp, out_resp_payload);
        }
        can::CanDataFrame frame;
        frame.data = payload;
        auto ec = can_.handle_request(req.evt_op, frame);
        if (ec) return set_error_response(req, ec, out_resp, out_resp_payload);
        out_resp = acf::make_response(req, req.evt_ack ? acf::ResponseKind::Acknowledge
                                                          : acf::ResponseKind::WriteResponse);
        return {};
    }

    // UART, unlike every other dispatch_*() above, drives a
    // request-shape-dependent operation on UartEndpoint: this is the first
    // time this file wires UartEndpoint in at all (see this header's own
    // top comment). dispatch_gpio above is the precedent this path
    // follows for branching on req.op — read (op==false) vs write
    // (op==true) select genuinely different UART operations (extraction
    // §13.7.8.1's independent TX/RX request storages), not a single
    // full-duplex/raw-transfer call the way SPI/I2C/LIN's dispatch paths
    // stay op-agnostic. Both branches funnel through the same
    // UartEndpoint::handle_request choke point, so Table 33 Row 2's 3-way
    // Plain/Reserved/ConfigWrite classification (rcp::endpoint::
    // evt_row2_kind_of) is checked once, before `payload` is ever read as
    // TX bytes or `req.read_size_or_segment_num` is ever read as a read
    // request's read_size — see handle_request's own doc comment for why
    // a Reserved or ConfigWrite evt must never reach either queue.
    //
    // This mock has no clock of its own (same disclaimer every other
    // endpoint type in this file carries), so a dispatched read request is
    // always answered synchronously against whatever is currently buffered
    // in the RX FIFO: elapsed_ms and uart_timeout_ms are both passed as 0,
    // which handle_read's own "elapsed_ms >= uart_timeout_ms" completion
    // rule treats as an immediately-elapsed timeout — the read never
    // blocks, it simply drains up to read_size bytes right now, the same
    // "test scripts the buffer via rx_fill(), this mock does not model
    // actual timing" approach set_i2c_response/set_lin_response take for
    // their own bus-transfer models (see uart()'s own comment above for
    // why there is no set_uart_response() hook instead). A write request's
    // successful response carries no payload (out_resp_payload stays
    // empty), mirroring dispatch_gpio's write-response shape; a read
    // request's successful response carries whatever handle_read drained,
    // mirroring dispatch_i2c/dispatch_lin's read-response shape. The
    // §13.7.8.3 payload-less-read-only rule ("A read request having a
    // byte_msg_payload will be rejected with error code = UNKNOWN_CMD") is
    // not enforced here — deliberately out of scope for this evt[2:0]
    // classification pass, same as handle_request's own comment already
    // flags.
    std::error_code dispatch_uart(const acf::AcfMessageInfo& req, const std::vector<uint8_t>& payload,
                                   acf::AcfMessageInfo& out_resp,
                                   std::vector<uint8_t>& out_resp_payload) noexcept {
        if (!operational_requests_allowed()) {
            return set_error_response(req, make_error_code(regmap::RegMapErrc::request_rejected),
                                       out_resp, out_resp_payload);
        }
        std::vector<uint8_t> data;
        bool timed_out = false;
        auto ec = uart_.handle_request(req.evt_op, req.op, payload, req.read_size_or_segment_num,
                                        /*elapsed_ms=*/0, /*uart_timeout_ms=*/0, data, timed_out);
        if (ec) return set_error_response(req, ec, out_resp, out_resp_payload);
        if (req.op) {
            out_resp = acf::make_response(req, req.evt_ack ? acf::ResponseKind::Acknowledge
                                                              : acf::ResponseKind::WriteResponse);
        } else {
            out_resp_payload = std::move(data);
            out_resp = acf::make_response(req, req.evt_ack ? acf::ResponseKind::Acknowledge
                                                              : acf::ResponseKind::ReadResponse);
        }
        return {};
    }

    // ISELED, same as I2C/LIN (extraction §13.7.12.3), pairs a request with
    // a full Address/Data response in a single transaction rather than a
    // read/write-branched register — this dispatch path does not branch on
    // req.op either, same rationale as dispatch_i2c/dispatch_lin's own
    // comment. Unlike I2C/LIN, whose transfer()/handle_request calls
    // operate on raw std::vector<uint8_t> payload bytes directly, ISELED
    // already has a real Figure 40/41 byte-level codec
    // (encode_iseled_request/decode_iseled_request,
    // encode_iseled_response/decode_iseled_response — see rcp/iseled.hpp's
    // own header comment on why those exist and what they do and do not
    // cover), so this dispatch path decodes `payload` into an
    // IseledRequest via decode_iseled_request and encodes
    // IseledEndpoint::handle_request's resulting response back via
    // encode_iseled_response, rather than passing raw bytes through
    // untouched the way dispatch_i2c/dispatch_lin do — this is calling
    // rcp/iseled.hpp's own pre-existing codec, not inventing a new one.
    // Table 33 Row 2's 3-way Plain/Reserved/ConfigWrite classification
    // (rcp::endpoint::evt_row2_kind_of) is checked by handle_request
    // itself, before request/response is ever recorded by transact() — see
    // handle_request's own doc comment for why a Reserved or ConfigWrite
    // evt must never reach it.
    //
    // This mock has no real ISELED daisy-chain hardware behind it (same
    // disclaimer every other endpoint type in this file carries), so the
    // Address/Data response value transact() records is whatever
    // set_iseled_response() last scripted (default-constructed,
    // IseledResponse{0, 0}, if never called) — the same "test scripts the
    // bus, this mock does not model actual hardware" pattern
    // set_i2c_response/set_lin_response already establish for their own
    // bus-transfer models. A successful request's response payload is
    // whatever handle_request recorded, encoded back via
    // encode_iseled_response, answered as a ReadResponse, mirroring
    // dispatch_i2c/dispatch_lin's read-response shape.
    std::error_code dispatch_iseled(const acf::AcfMessageInfo& req, const std::vector<uint8_t>& payload,
                                     acf::AcfMessageInfo& out_resp,
                                     std::vector<uint8_t>& out_resp_payload) noexcept {
        if (!operational_requests_allowed()) {
            return set_error_response(req, make_error_code(regmap::RegMapErrc::request_rejected),
                                       out_resp, out_resp_payload);
        }
        iseled::IseledRequest request;
        auto ec = iseled::decode_iseled_request(payload.data(), payload.size(), request);
        if (ec) return set_error_response(req, ec, out_resp, out_resp_payload);
        ec = iseled_.handle_request(req.evt_op, request, iseled_response_);
        if (ec) return set_error_response(req, ec, out_resp, out_resp_payload);
        out_resp_payload = iseled::encode_iseled_response(iseled_.last_response());
        out_resp = acf::make_response(req, req.evt_ack ? acf::ResponseKind::Acknowledge
                                                          : acf::ResponseKind::ReadResponse);
        return {};
    }

    // MDIO, this header's own eighth and LAST Table 33 Row 2 endpoint type
    // (extraction §13.7.13.3), branches on req.op the same way
    // dispatch_uart does — MdioRequest::is_write is intrinsic to the
    // request itself, so read vs write really is a different operation
    // here, not a single full-duplex/raw-transfer call the way SPI/I2C/
    // LIN's dispatch paths stay op-agnostic (see dispatch_uart's own
    // comment for the identical rationale). Unlike ISELED, which already
    // has a real Figure 40/41 byte-level codec to decode/encode `payload`
    // against, NO MDIO byte-level wire codec (Figure 43/Table 60) exists
    // anywhere in this codebase yet — mdio.hpp deliberately stops at the
    // (mode, mdio_address, mdio_payload) struct level (see its own header
    // comment on the addressing-model fix), the same gap dispatch_can's own
    // comment flags for CAN's Figure 40. This dispatch path makes the same
    // kind of deliberately simple, explicitly disclaimed choice
    // dispatch_can makes for its own fixed CanIdentifier/FrameFormat: a
    // fixed representative MdioMode::MmdSingleWord mode and mdio_address 0
    // (the mode/address bit-level decode Table 60/Figure 43 would require
    // is out of scope for this evt[2:0] classification pass), with
    // mdio_payload packed/unpacked via avtp::detail::put_u16/get_u16 — the
    // same generic byte-packing helper dispatch_ep0 above already uses for
    // its own magic-number encoding — purely so this mock's own
    // request/response payload has *some* round-trippable value to exercise
    // end-to-end. This is this mock's own simulation choice for its own
    // wire shape, not a TC18-derived MDIO byte-level encoding of Figure
    // 43's actual field layout; full bit-for-bit conformance is not
    // claimed, same as every other disclaimer in this file. Table 33 Row
    // 2's 3-way Plain/Reserved/ConfigWrite classification (rcp::endpoint::
    // evt_row2_kind_of) is checked by handle_request itself, before
    // transact() ever touches the register map — see handle_request's own
    // doc comment for why a Reserved or ConfigWrite evt must never reach
    // it. No set_mdio_response() hook exists — see mdio()'s own comment
    // above for why MDIO's self-contained register model needs none.
    std::error_code dispatch_mdio(const acf::AcfMessageInfo& req, const std::vector<uint8_t>& payload,
                                   acf::AcfMessageInfo& out_resp,
                                   std::vector<uint8_t>& out_resp_payload) noexcept {
        if (!operational_requests_allowed()) {
            return set_error_response(req, make_error_code(regmap::RegMapErrc::request_rejected),
                                       out_resp, out_resp_payload);
        }
        mdio::MdioRequest request;
        request.mode          = mdio::MdioMode::MmdSingleWord;
        request.mdio_address  = 0;
        request.mms_is_0_or_1 = false;
        request.is_write      = req.op;
        request.mdio_payload  = (req.op && payload.size() >= 2) ? avtp::detail::get_u16(payload.data()) : 0;

        mdio::MdioResponse response;
        auto ec = mdio_.handle_request(req.evt_op, request, response);
        if (ec) return set_error_response(req, ec, out_resp, out_resp_payload);

        if (req.op) {
            out_resp = acf::make_response(req, req.evt_ack ? acf::ResponseKind::Acknowledge
                                                              : acf::ResponseKind::WriteResponse);
        } else {
            out_resp_payload.resize(2);
            avtp::detail::put_u16(out_resp_payload.data(), static_cast<uint16_t>(response.mdio_payload));
            out_resp = acf::make_response(req, req.evt_ack ? acf::ResponseKind::Acknowledge
                                                              : acf::ResponseKind::ReadResponse);
        }
        return {};
    }

    lifecycle::ServerLifecycle lifecycle_;
    regmap::RegisterMap        regs_;
    regmap::Ep0                ep0_;
    gpio::GpioEndpoint         gpio_;
    spi::SpiEndpoint           spi_;
    i2c::I2cEndpoint           i2c_;
    adc::AdcEndpoint           adc_;
    adc::AdcFunctionalConfig   adc_cfg_; // backs dispatch_adc's evt[2:0]==111b configuration-write path
    pwm::PwmInEndpoint         pwm_in_;
    lin::LinEndpoint           lin_;
    can::CanEndpoint           can_;
    uart::UartEndpoint         uart_;
    iseled::IseledEndpoint     iseled_;
    mdio::MdioEndpoint         mdio_;
    std::array<std::vector<uint8_t>, spi::kMaxChannels> spi_poci_{};
    std::vector<uint8_t>       i2c_response_{};
    bool                       i2c_acked_ = true;
    std::deque<uint16_t>       adc_samples_{};
    std::vector<uint8_t>       lin_response_{};
    bool                       lin_responded_ = true;
    iseled::IseledResponse     iseled_response_{};
};

} // namespace mock
} // namespace rcp
