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
// each of five fully-built endpoint types — rcp::gpio::GpioEndpoint and
// rcp::spi::SpiEndpoint (both v2.3.0), plus rcp::i2c::I2cEndpoint,
// rcp::adc::AdcEndpoint, and rcp::pwm::PwmInEndpoint (v2.4.0, wired in by
// the Table 30/33 Row 2 evt[2:0] validation pilot and its ADC and PWM_IN
// follow-ups, in that order) — as its representative endpoint set.
// dispatch() below is the single
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
// 4, PWM_IN at 5), access-policy choice for operational requests (gated on
// lifecycle state only, not per-endpoint ownership — see dispatch()'s own
// comment), and EP0 partial-read encoding chosen in this file are this
// implementation's own, purely for the purposes of being a usable
// in-process simulator — full bit-for-bit conformance against other TC18
// implementations is not claimed, same as the equivalent disclaimers in
// rcp/regmap.hpp, rcp/lifecycle.hpp, rcp/gpio.hpp, rcp/spi.hpp, rcp/i2c.hpp,
// rcp/adc.hpp, and rcp/pwm.hpp.
#pragma once

#include <rcp/acf.hpp>
#include <rcp/adc.hpp>
#include <rcp/avtp.hpp>
#include <rcp/endpoint.hpp>
#include <rcp/gpio.hpp>
#include <rcp/i2c.hpp>
#include <rcp/lifecycle.hpp>
#include <rcp/pwm.hpp>
#include <rcp/regmap.hpp>
#include <rcp/spi.hpp>

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
constexpr avtp::ByteBusId kGpioByteBusId  = static_cast<avtp::ByteBusId>(kGpioEndpointId);
constexpr avtp::ByteBusId kSpiByteBusId   = static_cast<avtp::ByteBusId>(kSpiEndpointId);
constexpr avtp::ByteBusId kI2cByteBusId   = static_cast<avtp::ByteBusId>(kI2cEndpointId);
constexpr avtp::ByteBusId kAdcByteBusId   = static_cast<avtp::ByteBusId>(kAdcEndpointId);
constexpr avtp::ByteBusId kPwmInByteBusId = static_cast<avtp::ByteBusId>(kPwmInEndpointId);

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
    if (ec == make_error_code(adc::AdcErrc::config_write_not_supported))
        return acf::WireErrorCode::UnsupportedCmd; // evt[2:0]==111b config-write, not yet implemented by this mock
    if (ec == make_error_code(adc::AdcErrc::no_signal))
        return acf::WireErrorCode::EpError; // internal no-valid-sample condition, not a TC18-defined ADC error code (see adc.hpp's own comment) — mirrors i2c::I2cErrc::nack's mapping above
    if (ec == make_error_code(pwm::PwmErrc::config_write_not_supported))
        return acf::WireErrorCode::UnsupportedCmd; // evt[2:0]==111b config-write, not yet implemented by this mock
    if (ec == make_error_code(pwm::PwmErrc::no_signal))
        return acf::WireErrorCode::PwmInNoSignal; // Table 27's own dedicated PWM_IN_NO_SIGNAL(9) code — unlike adc::AdcErrc::no_signal above, TC18 defines a real numbered code for this condition, so this maps to it directly rather than falling back to EpError
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
        regs.endpoint_count = 5;
        regs.generic_configs.resize(5);
        regs.functional_configs.resize(5);
        regs.ep_id_mapping = {
            {kGpioEndpointId,  kGpioByteBusId},
            {kSpiEndpointId,   kSpiByteBusId},
            {kI2cEndpointId,   kI2cByteBusId},
            {kAdcEndpointId,   kAdcByteBusId},
            {kPwmInEndpointId, kPwmInByteBusId},
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
    // codebase, not that it is inherently full-duplex). Every request
    // addressed here drives one AdcEndpoint::handle_request against a
    // default-constructed AdcAveragingConfig (no averaging: one raw sample
    // per request) and a take_sample callback that pulls the next scripted
    // value set_adc_response() queued — Table 33 Row 2's 3-way
    // Plain/Reserved/ConfigWrite classification (rcp::endpoint::
    // evt_row2_kind_of) is checked by handle_request itself before
    // take_sample is ever invoked, so a Reserved or ConfigWrite evt can
    // never consume a scripted sample. `payload` is unused: §13.7.9.3
    // states the ADC request itself carries no byte_msg_payload.
    std::error_code dispatch_adc(const acf::AcfMessageInfo& req, const std::vector<uint8_t>& /*payload*/,
                                  acf::AcfMessageInfo& out_resp,
                                  std::vector<uint8_t>& out_resp_payload) noexcept {
        if (!operational_requests_allowed()) {
            return set_error_response(req, make_error_code(regmap::RegMapErrc::request_rejected),
                                       out_resp, out_resp_payload);
        }
        auto take_sample = [this]() -> std::optional<uint16_t> {
            if (adc_samples_.empty()) return std::nullopt;
            uint16_t v = adc_samples_.front();
            adc_samples_.pop_front();
            return v;
        };
        uint16_t value = 0;
        auto ec = adc_.handle_request(req.evt_op, adc::AdcAveragingConfig{}, take_sample, value);
        if (ec) return set_error_response(req, ec, out_resp, out_resp_payload);
        out_resp_payload = adc::encode_adc_value(value);
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

    lifecycle::ServerLifecycle lifecycle_;
    regmap::RegisterMap        regs_;
    regmap::Ep0                ep0_;
    gpio::GpioEndpoint         gpio_;
    spi::SpiEndpoint           spi_;
    i2c::I2cEndpoint           i2c_;
    adc::AdcEndpoint           adc_;
    pwm::PwmInEndpoint         pwm_in_;
    std::array<std::vector<uint8_t>, spi::kMaxChannels> spi_poci_{};
    std::vector<uint8_t>       i2c_response_{};
    bool                       i2c_acked_ = true;
    std::deque<uint16_t>       adc_samples_{};
};

} // namespace mock
} // namespace rcp
