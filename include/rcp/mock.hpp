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
// dispatch_iseled's own comment for why it passes the raw byte_msg_payload
// straight through to IseledEndpoint::handle_request, same as dispatch_i2c/
// dispatch_lin do (Phase 3's rcp/iseled.hpp rewrite replaced its earlier
// structured Address/Data ACF-payload model with the same raw-byte-stream
// codec I2C/LIN already use — see rcp/iseled.hpp's own header comment);
// MDIO's is this header's FIRST wiring of
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
#include <rcp/discovery.hpp>
#include <rcp/endpoint.hpp>
#include <rcp/gpio.hpp>
#include <rcp/i2c.hpp>
#include <rcp/iseled.hpp>
#include <rcp/lifecycle.hpp>
#include <rcp/lin.hpp>
#include <rcp/mdio.hpp>
#include <rcp/pwm.hpp>
#include <rcp/regmap.hpp>
#include <rcp/request.hpp>
#include <rcp/server.hpp>
#include <rcp/spi.hpp>
#include <rcp/uart.hpp>

#include <array>
#include <chrono>
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
    if (ec == make_error_code(spi::SpiErrc::bad_channel))                return acf::WireErrorCode::UnsupportedCmd; // reserved evt[2:0] selection, extraction §5.4/Table entries at 110b
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
    if (ec == make_error_code(mdio::MdioErrc::config_write_not_supported))
        return acf::WireErrorCode::UnsupportedCmd; // evt[2:0]==111b config-write, not yet implemented by this mock
    if (ec == make_error_code(mdio::MdioErrc::payload_exceeds_mode_width))
        return acf::WireErrorCode::InvalidParameter; // mdio_payload exceeds the width mdio_mode assigns it — client-caused, same rationale as avtp::short_buffer's mapping above
    return acf::WireErrorCode::UnsupportedCmd;
}

// ── Admission-outcome-only error codes (Phase 4/Phase 17 batch A, cpp-RCP
// issue #129) ────────────────────────────────────────────────────────────────
// dispatch()'s pre-existing contract returns {} for success (out_resp holds
// a real Read/Write/Acknowledge response) and a non-empty std::error_code
// for every other outcome (out_resp holds an ErrorResponse). Routing every
// dispatch_*() below through rcp::server::Endpoint::admit_with_ack() before
// its own handler body now adds four outcomes that build NO wire response
// at all, when the request's own evt[3] did not ask for one (REQ-SRV-016's
// "if requested" gate — c-RCP's own finish_admission(), src/mock.c:
// 1361-1423, documents the identical "*out_response left zeroed" contract
// for these same four outcomes). A caller receiving one of these below MUST
// NOT send anything on the wire for this request: out_resp is left
// default-constructed (rsp == false — distinguishable from every genuine
// response this codec ever builds, all of which set rsp == true via
// acf::make_response()) and out_resp_payload is left empty.
enum class DispatchErrc : int {
    queued    = 1, // REQ-SRV-015 (TC18 §12.3.1.3): endpoint disabled, request queued, no ack requested
    pending   = 2, // conditional/TSCF-gated request stored, no ack requested — unreachable through this
                    // file's own dispatch_*() below (every request they admit is encoded ACF_ABB, so
                    // server::Endpoint::admit_with_ack()'s own GBB-opcode peek never fires — see
                    // admit_and_classify()'s own comment); kept for classifier completeness, exercised
                    // directly against admission() by this file's own test suite instead.
    cancelled = 3, // a cancellation request was applied — likewise unreachable through dispatch_*()
                    // below, same reason as `pending` above.
    suspended = 4, // REQ-PWRMODE-028: admission_suspended() was set; the frame was never even inspected
};

inline const std::error_category& dispatch_category() noexcept {
    struct Cat : std::error_category {
        const char* name() const noexcept override { return "rcp.mock.dispatch"; }
        std::string message(int ev) const override {
            switch (static_cast<DispatchErrc>(ev)) {
            case DispatchErrc::queued:    return "rcp/mock: request queued, endpoint disabled";
            case DispatchErrc::pending:   return "rcp/mock: request stored, awaiting its execution condition";
            case DispatchErrc::cancelled: return "rcp/mock: cancellation request applied";
            case DispatchErrc::suspended: return "rcp/mock: admission suspended";
            default:                      return "rcp/mock: unknown dispatch outcome";
            }
        }
    };
    static Cat instance;
    return instance;
}

inline std::error_code make_error_code(DispatchErrc e) noexcept {
    return {static_cast<int>(e), dispatch_category()};
}

// ── Admission-rejection response-shape classifier (ported from c-RCP's
// admission_reject_response_shape(), src/mock.c:1277-1300) ───────────────────
// issue #454 (c-RCP): RCP_SERVER_ADMIT_REJECTED/AdmitOutcome::Rejected is
// NOT one single TC18 response shape. The general rule, per §11.3.1's own
// wording ("err = 1 indicates that the request has been rejected" for a
// request never filed into EP request storage at all), is the Acknowledge-
// rejected shape (evt[3:0] == 0xF, err = 1). But §13.5.1 explicitly
// overrides that default for exactly one admission-rejection reason
// server::Endpoint::admit_with_ack() can report: a compound-wait request's
// reserved evt[2:0] = 011b, reported as WireErrorCode::UnsupportedCmd —
// "request shall be ignored and an err-response with error code =
// UNSUPPORTED_CMD shall be sent." "err-response" is TC18's own specific
// term for §11.3.4's Error Response shape (evt[3:0] < 0x9, err = 1),
// structurally distinct from the Acknowledge shape. Every other
// WireErrorCode admit_with_ack() can report for Rejected (InvalidParameter:
// a response frame received where a request was expected; ReqStorageOverflow:
// the request store is full) has no such override in the spec text, and
// keeps the §11.3.1 Acknowledge-rejected shape.
enum class AdmitRejectShape : uint8_t { AcknowledgeRejected, ErrorResponse };

inline AdmitRejectShape admission_reject_response_shape(acf::WireErrorCode error) noexcept {
    switch (error) {
    case acf::WireErrorCode::UnsupportedCmd: // TC18 §13.5.1, REQ-ACF-024/REQ-SRV-019
        return AdmitRejectShape::ErrorResponse;
    default:
        return AdmitRejectShape::AcknowledgeRejected;
    }
}

// make_acknowledge_rejected_response builds TC18 §11.3.1's OTHER Acknowledge
// shape, distinct from acf::make_response(req, ResponseKind::Acknowledge):
// same evt[3:0] == kEvtAcknowledge, but with err also set — "err = 1
// indicates that the request has been rejected." acf::make_response() alone
// cannot express this combination (its own ResponseKind::Acknowledge
// hardcodes err = false, see its own doc comment), so this builds on top of
// it rather than re-deriving the evt_ack/evt_op bit pattern from scratch —
// the same field values acf::build_acknowledge_rejected_response() (a raw-
// bytes builder) encodes, just expressed as the decoded AcfMessageInfo this
// dispatch layer's own out_resp/out_resp_payload contract needs.
inline acf::AcfMessageInfo make_acknowledge_rejected_response(const acf::AcfMessageInfo& req) noexcept {
    acf::AcfMessageInfo resp = acf::make_response(req, acf::ResponseKind::Acknowledge);
    resp.err = true;
    return resp;
}

// ── Server ────────────────────────────────────────────────────────────────────
// A single simulated RC Server instance. Not copyable — regmap::Ep0 holds
// references into this object's own RegisterMap/ServerLifecycle members,
// so a Server is meant to be owned by reference or a smart pointer, the
// same restriction Ep0 itself already carries.
//
// Phase 4/Phase 17 batch A (cpp-RCP issue #129): each of the ten
// operational endpoint types above now also owns its own
// rcp::server::Endpoint admission queue/conditional-request store —
// mirroring c-RCP's own rcp_mock_endpoint_slot_t, which embeds one
// rcp_server_endpoint_t queue per registered endpoint (src/mock.c:35-233,
// field `queue` at line 60). Every dispatch_*() below now routes through
// its own admission Endpoint's admit_with_ack() BEFORE ever invoking its
// handler body — see dispatch()'s own updated doc comment and
// admit_and_classify()'s doc comment for the full admission-outcome-to-
// response-shape contract this adds (ported from c-RCP's
// dispatch_plain_inner()/finish_admission(), src/mock.c:1438-1683/
// 1361-1423). EP0 is deliberately excluded: it is not one of this file's
// ten *operational* endpoints (dispatch_ep0() is gated by
// ep0_.check_read_access(), not by lifecycle state or admission at all),
// matching c-RCP's own mock.c, which never registers EP0 as a
// rcp_mock_endpoint_slot_t either.
//
// Phase 4/Phase 17 batch B (cpp-RCP issue #129) adds three more pieces on
// top of batch A's admission wiring:
//
//  1. TC18 Table 24 response/ack routing suppression (REQ-RMAP-048/049)
//     -- ported from c-RCP's suppress_response_per_stream_cfg()
//     (src/mock.c:1716-1742) wrapping dispatch_plain() around
//     dispatch_plain_inner() (src/mock.c:1743-1760). This mock's own ten
//     typed dispatch_*() functions each already play
//     dispatch_plain_inner()'s own role (the single-endpoint handler
//     body, with every one of admit_and_classify()'s own early exits and
//     the handler's own success/error exits) -- batch B applies the
//     identical wrap, once per endpoint type: each public dispatch_*()
//     entry point below is now a thin wrapper calling its own renamed
//     dispatch_*_inner() (batch A's unchanged handler body) and then
//     suppress_response_per_stream_cfg() (below) on whatever response it
//     produced, before returning it to dispatch()'s own caller. Requires
//     a stream_id at the call site to resolve into a
//     regmap::RequestStreamConfig row -- dispatch()'s own signature grows
//     a trailing, defaulted `avtp::StreamId stream_id` parameter for this
//     (default avtp::StreamId{} -- to_u64() == 0 -- matches an
//     unconfigured stream, which
//     regmap::request_stream_cfg::resolve_index() already treats as "no
//     match", so no existing dispatch() caller's behavior changes unless
//     it opts in to a real stream_id AND has configured a matching
//     request_streams[] row via set_request_stream_cfg() below).
//     dispatch_ep0() is NOT wrapped, matching this same class's own
//     established "EP0 is not one of this file's ten operational
//     endpoints" exclusion directly above.
//
//  2. request_streams/ep_id_mapping storage -- regmap::RegisterMap already
//     carries both tables as plain public members (regs_.request_streams,
//     regs_.ep_id_mapping); set_request_stream_cfg()/request_stream_cfg()
//     and set_ep_id_map()/ep_id_map() below add the same bounds-checked
//     wholesale-replace + capacity-register-sync convention c-RCP's own
//     rcp_mock_server_set_request_stream_cfg()/_set_ep_id_map() (mock.h)
//     already establish (REQ-RMAP-034/037), so item 1's suppression logic
//     above has a real, test-settable table to resolve a stream_id
//     against instead of only ever seeing an empty one.
//
//  3. Discovery-stream claim storage (REQ-RMAP-066) -- a
//     discovery::DiscoveryClaim member plus discovery_claim()/
//     set_discovery_timeout_us(), mirroring c-RCP's own
//     rcp_mock_server_discovery_claim()/_set_discovery_timeout_us()
//     (mock.h:501-529, mock.c:663-687) exactly as thin as c-RCP's own
//     mock.c keeps it: direct storage plus a timeout setter that keeps
//     regs_.svr_ep_cfg.svr_discovery_timeout and the claim's own internal
//     window in sync, and nothing more -- c-RCP's mock.c itself never
//     calls into discovery.h's own claim/timeout logic from inside its
//     dispatch path either (confirmed by direct source read), so this
//     port does not invent a dispatch()-side discovery-request handler
//     that c-RCP has no counterpart for.
class Server final {
public:
    Server()
        : lifecycle_(),
          regs_(make_initial_register_map()),
          ep0_(regs_, lifecycle_) {
        // REQ-RMAP-066: route the power-on svr_discovery_timeout through
        // the same setter every later change to it uses, rather than
        // duplicating the sync here -- mirrors c-RCP's own
        // rcp_mock_server_new() (src/mock.c:291-298), which does the
        // identical single call for the identical reason ("srv->
        // discovery_claim is never left holding a timeout_ms out of sync
        // with svr_ep_cfg's own current value"). A no-op in terms of the
        // actual numeric window right now -- discovery::DiscoveryClaim's
        // own kDefaultTimeout (20 ms) already equals
        // regmap::SvrEpCfg::svr_discovery_timeout's own default (20000
        // us) -- but establishes the single source of truth for whenever
        // either one changes.
        set_discovery_timeout_us(regs_.svr_ep_cfg.svr_discovery_timeout);
    }

    Server(const Server&)            = delete;
    Server& operator=(const Server&) = delete;

    lifecycle::ServerLifecycle&       lifecycle() noexcept { return lifecycle_; }
    const lifecycle::ServerLifecycle& lifecycle() const noexcept { return lifecycle_; }

    regmap::RegisterMap&       registers() noexcept { return regs_; }
    const regmap::RegisterMap& registers() const noexcept { return regs_; }

    regmap::Ep0& ep0() noexcept { return ep0_; }

    // ── Table 24 request-stream config / EP-ID mapping storage (Phase 4/
    // Phase 17 batch B) ──────────────────────────────────────────────────
    // set_request_stream_cfg/set_ep_id_map wholesale-replace their own
    // table (regs_.request_streams/regs_.ep_id_mapping, both already
    // plain public RegisterMap members -- see this class's own header
    // comment, item 2), bounds-checked against each table's own
    // regmap.hpp-defined kMaxEntries and syncing the matching Table 20
    // capacity register, mirroring c-RCP's own
    // rcp_mock_server_set_request_stream_cfg() (mock.c:444-460,
    // REQ-RMAP-034) and rcp_mock_server_set_ep_id_map() (mock.c:565-580,
    // REQ-RMAP-037) exactly. Returns false (the table left unchanged) iff
    // entries.size() exceeds that bound; true otherwise. The getters are
    // a convenience mirroring registers().request_streams/.ep_id_mapping
    // directly -- either spelling reaches the same storage.
    bool set_request_stream_cfg(std::vector<regmap::RequestStreamConfig> entries) noexcept {
        if (entries.size() > regmap::request_stream_cfg::kMaxEntries) return false;
        regs_.general.svr_request_stream_cfg_capacity = static_cast<uint8_t>(entries.size());
        regs_.request_streams = std::move(entries);
        return true;
    }
    const std::vector<regmap::RequestStreamConfig>& request_stream_cfg() const noexcept {
        return regs_.request_streams;
    }

    bool set_ep_id_map(std::vector<regmap::EpIdMappingEntry> entries) noexcept {
        if (entries.size() > regmap::ep_id_map::kMaxEntries) return false;
        regs_.general.svr_ep_bytebus_id_map_capacity = static_cast<uint8_t>(entries.size());
        regs_.ep_id_mapping = std::move(entries);
        return true;
    }
    const std::vector<regmap::EpIdMappingEntry>& ep_id_map() const noexcept {
        return regs_.ep_id_mapping;
    }

    // ── Discovery-stream claim (REQ-RMAP-066, Phase 4/Phase 17 batch B) ───
    // discovery_claim gives direct, mutable access to this Server's own
    // discovery::DiscoveryClaim -- the same "caller may freely set/consult
    // it directly" convention c-RCP's own rcp_mock_server_discovery_claim()
    // (mock.h:501-529) establishes for its own plain-struct
    // rcp_discovery_claim_t*. Never a null reference.
    discovery::DiscoveryClaim& discovery_claim() noexcept { return discovery_claim_; }

    // set_discovery_timeout_us sets regs_.svr_ep_cfg.svr_discovery_timeout
    // (TC18's own wire register, microseconds) AND re-derives
    // discovery_claim_'s own internal window from it (truncating
    // microsecond division, matching every other us/ms boundary
    // conversion in this codebase's own convention of never silently
    // rounding up past a caller's own requested bound) -- mirrors c-RCP's
    // rcp_mock_server_set_discovery_timeout_us() (mock.c:669-687) exactly,
    // including its own explicit "does NOT reset held/claimant/deadline"
    // guarantee: discovery::DiscoveryClaim::set_timeout() (discovery.hpp)
    // only ever touches its own timeout_ field, never holder_/claimed_at_
    // -- an in-flight claim's own current deadline is unaffected by a
    // timeout-VALUE change mid-claim; only the window a FUTURE grant
    // computes uses the new value.
    void set_discovery_timeout_us(uint16_t timeout_us) noexcept {
        regs_.svr_ep_cfg.svr_discovery_timeout = timeout_us;
        discovery_claim_.set_timeout(std::chrono::milliseconds(timeout_us / 1000u));
    }

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

    // admission gives a caller (a test, or a future rcp/sim.hpp scheduling
    // loop) direct access to one operational endpoint's own
    // rcp::server::Endpoint admission queue/conditional-request store —
    // ep_enable()/set_enable(), set_admission_suspended(), and every other
    // server::Endpoint method (server.hpp) are reachable through it
    // directly, mirroring gpio()/spi()/etc.'s own "expose the real
    // subsystem object, don't wrap every one of its methods" convention
    // rather than duplicating server::Endpoint's own surface here. Returns
    // nullptr for a byte_bus_id this mock does not host an operational
    // endpoint at (including regmap::kEp0, which has no admission queue of
    // its own — see this class's own header comment above).
    server::Endpoint* admission(avtp::ByteBusId byte_bus_id) noexcept {
        if (byte_bus_id == kGpioByteBusId) return &gpio_admission_;
        if (byte_bus_id == kSpiByteBusId) return &spi_admission_;
        if (byte_bus_id == kI2cByteBusId) return &i2c_admission_;
        if (byte_bus_id == kAdcByteBusId) return &adc_admission_;
        if (byte_bus_id == kPwmInByteBusId) return &pwm_in_admission_;
        if (byte_bus_id == kLinByteBusId) return &lin_admission_;
        if (byte_bus_id == kCanByteBusId) return &can_admission_;
        if (byte_bus_id == kUartByteBusId) return &uart_admission_;
        if (byte_bus_id == kIseledByteBusId) return &iseled_admission_;
        if (byte_bus_id == kMdioByteBusId) return &mdio_admission_;
        return nullptr;
    }
    const server::Endpoint* admission(avtp::ByteBusId byte_bus_id) const noexcept {
        return const_cast<Server*>(this)->admission(byte_bus_id);
    }

    // ── server.hpp passthroughs (Phase 4/Phase 17 batch A) ────────────────────
    // Thin exposures of primitives server.hpp already implements in full —
    // see rcp/server.hpp's own "Integration surface" header-comment section
    // for the contract each of these is built directly on top of.

    // pending_count: how many conditional/TSCF-gated requests are currently
    // stored on the endpoint at byte_bus_id, or 0 if byte_bus_id names no
    // operational endpoint. Mirrors c-RCP's rcp_mock_server_pending_count()
    // (src/mock.c:3685-3691).
    size_t pending_count(avtp::ByteBusId byte_bus_id) const noexcept {
        const server::Endpoint* ep = admission(byte_bus_id);
        return ep ? ep->pending_count() : 0;
    }

    // watchdog_purge: removes every non-safety-tagged stored request on the
    // endpoint at byte_bus_id, returning how many were removed (0 if
    // byte_bus_id names no operational endpoint). Mirrors c-RCP's
    // rcp_mock_server_watchdog_purge() (src/mock.c:3702-3708); a future
    // rcp/watchdog.hpp overflow callback wiring is out of this batch's own
    // scope (see this file's own header comment on why watchdog wiring is
    // rcp/sim.hpp's concern).
    size_t watchdog_purge(avtp::ByteBusId byte_bus_id) noexcept {
        server::Endpoint* ep = admission(byte_bus_id);
        return ep ? ep->watchdog_purge() : 0;
    }

    // drain_one: dequeues the oldest ep_enable-queued request on the
    // endpoint at byte_bus_id into out_frame (an owned copy of the whole
    // ACF message, same shape dispatch()'s own req_payload/encode_acf_abb()
    // pairing would decode via acf::decode_acf_abb()) and returns true, iff
    // that endpoint is enabled and its queue is non-empty. Mirrors c-RCP's
    // rcp_mock_server_drain_endpoint() (src/mock.c:2856-2870) MINUS running
    // a handler on the dequeued frame itself — this mock has no generic
    // byte-level handler entry point (every dispatch_*() below operates on
    // an already-decoded AcfMessageInfo+payload, not raw bytes); a caller
    // that wants to execute the drained frame decodes it via
    // acf::decode_acf_abb() and calls the matching dispatch_*() (or the
    // public dispatch()) itself.
    bool drain_one(avtp::ByteBusId byte_bus_id, std::vector<uint8_t>& out_frame) {
        server::Endpoint* ep = admission(byte_bus_id);
        return ep != nullptr && ep->drain_one(out_frame);
    }

    // notify_trigger broadcasts one observed trigger occurrence to every
    // operational endpoint's own request store (a Triggered request may be
    // stored on any endpoint regardless of which one hosts
    // trigger_source_ep/trigger_signal_nr), returning how many stored
    // requests counted it. Mirrors c-RCP's rcp_mock_server_notify_trigger()
    // (src/mock.c:3670-3683).
    size_t notify_trigger(uint8_t source_ep, uint8_t signal_nr) noexcept {
        server::Endpoint* eps[] = {&gpio_admission_, &spi_admission_,   &i2c_admission_,
                                    &adc_admission_,  &pwm_in_admission_, &lin_admission_,
                                    &can_admission_,  &uart_admission_,   &iseled_admission_,
                                    &mdio_admission_};
        size_t matched = 0;
        for (server::Endpoint* ep : eps) matched += ep->notify_trigger(source_ep, signal_nr);
        return matched;
    }

    // notify_gptp_lock_state evaluates one newly observed gPTP lock state
    // (TC18 §13.7.1.3 Table 37) against this Server's own previously
    // observed state and, iff that observation is a genuine edge, broadcasts
    // the fired signal via notify_trigger() above using source_ep as
    // whichever byte_bus_id/endpoint-id this deployment's own convention
    // assigns to the RC Server itself (server::GptpTriggerState's own doc
    // comment, server.hpp). Returns notify_trigger()'s own return value when
    // an edge fired, or 0 for an unchanged observation (including the very
    // first call). Mirrors c-RCP's rcp_mock_server_notify_gptp_lock_state()
    // (src/mock.c:3785-3802).
    size_t notify_gptp_lock_state(bool locked, uint8_t source_ep) noexcept {
        const std::optional<uint8_t> fired = server::gptp_trigger_evaluate(gptp_trigger_state_, locked);
        if (!fired) return 0;
        return notify_trigger(source_ep, *fired);
    }

    // tick re-evaluates the stored conditional/TSCF-gated requests on the
    // endpoint at byte_bus_id against ctx (server::TickContext, server.hpp)
    // and, iff one is due, hands back its own owned raw stored frame in
    // out_frame and finalizes it via server::Endpoint::complete() — server::
    // Endpoint::select_due()/complete() (server.hpp) exposed directly, per
    // this batch's own scope. Unlike c-RCP's rcp_mock_server_tick()
    // (src/mock.c:3587-3628), this does NOT itself decode or execute
    // out_frame: this mock has no generic byte-level dispatch entry point
    // (see drain_one()'s own comment above for the identical reason) — a
    // caller decodes out_frame via acf::decode_acf_abb()/decode_acf_gbb()
    // and dispatches it itself. Returns false (out_frame left untouched) if
    // byte_bus_id names no operational endpoint or nothing is due.
    bool tick(avtp::ByteBusId byte_bus_id, const server::TickContext& ctx, std::vector<uint8_t>& out_frame) {
        server::Endpoint* ep = admission(byte_bus_id);
        if (!ep) return false;
        size_t index = 0;
        if (!ep->select_due(ctx, &index)) return false;
        if (const server::PendingRequest* slot = ep->pending(index)) out_frame = slot->frame;
        (void)ep->complete(index, ctx);
        return true;
    }

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

    // set_iseled_response scripts the raw response bytes a subsequent
    // dispatch()-driven ISELED read transaction records — the same "test
    // scripts the bus, this mock does not model actual hardware" pattern
    // set_i2c_response/set_lin_response above already establish (ISELED's
    // ACF-level codec is a raw byte stream, same as I2C/LIN — see
    // rcp/iseled.hpp's own header comment on why it is not the earlier,
    // now-replaced structured Address/Data model). Applies to the next
    // plain (evt[2:0]==000b) ISELED request only in spirit — like
    // set_i2c_response, it stays in effect until overwritten, there is no
    // auto-clear. Defaults to an empty response until first called.
    void set_iseled_response(std::vector<uint8_t> response) {
        iseled_response_ = std::move(response);
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
    // endpoint may be *operated*).
    //
    // Phase 4/Phase 17 batch A: once past the lifecycle-state gate, every
    // non-EP0 endpoint's request is now ALSO routed through that
    // endpoint's own rcp::server::Endpoint admission queue
    // (admit_and_classify(), below) before its handler body ever runs —
    // see that function's own doc comment for the full ExecuteNow/Queued/
    // Pending/Cancellation/Suspended/Rejected classification this adds.
    // Returns the same std::error_code the failing/non-executing step
    // below produced; out_resp/out_resp_payload are populated with a real
    // response (Error/Ack/Read/WriteResponse, per rcp::acf::make_response)
    // for every outcome that TC18 actually sends one for — but for the
    // four admission outcomes that build no wire response at all (an
    // evt[3]-less Queued/Pending/Cancellation/Suspended — DispatchErrc's
    // own doc comment above), out_resp is instead left default-constructed
    // (rsp == false, never true of a genuine response this codec builds)
    // and out_resp_payload left empty: a caller MUST check the returned
    // std::error_code before assuming there is anything to send.
    //
    // Phase 4/Phase 17 batch B: `stream_id` (default avtp::StreamId{},
    // to_u64() == 0) names which request stream this request arrived on —
    // mirrors c-RCP's own rcp_mock_server_dispatch()'s own stream_id
    // parameter (mock.h), reduced to just what Table 24 response
    // suppression (REQ-RMAP-048/049) needs: each of the ten dispatch_*()
    // calls below now threads it through to that endpoint type's own
    // suppress_response_per_stream_cfg() tail call — see this class's own
    // header comment, item 1, and suppress_response_per_stream_cfg()'s own
    // doc comment for the full contract. The default leaves every existing
    // caller's behavior unchanged: an unconfigured/default stream_id never
    // resolves to a real regmap::request_streams[] row, so suppression is
    // always a no-op unless a caller both passes a real stream_id AND has
    // configured a matching row via set_request_stream_cfg() above.
    std::error_code dispatch(size_t client, const acf::AcfMessageInfo& req,
                              const std::vector<uint8_t>& req_payload,
                              acf::AcfMessageInfo& out_resp,
                              std::vector<uint8_t>& out_resp_payload,
                              avtp::StreamId stream_id = avtp::StreamId{}) noexcept {
        out_resp_payload.clear();
        if (req.byte_bus_id == regmap::kEp0) return dispatch_ep0(client, req, out_resp, out_resp_payload);
        if (req.byte_bus_id == kGpioByteBusId)
            return dispatch_gpio(req, req_payload, out_resp, out_resp_payload, stream_id);
        if (req.byte_bus_id == kSpiByteBusId)
            return dispatch_spi(req, req_payload, out_resp, out_resp_payload, stream_id);
        if (req.byte_bus_id == kI2cByteBusId)
            return dispatch_i2c(req, req_payload, out_resp, out_resp_payload, stream_id);
        if (req.byte_bus_id == kAdcByteBusId)
            return dispatch_adc(req, req_payload, out_resp, out_resp_payload, stream_id);
        if (req.byte_bus_id == kPwmInByteBusId)
            return dispatch_pwm_in(req, req_payload, out_resp, out_resp_payload, stream_id);
        if (req.byte_bus_id == kLinByteBusId)
            return dispatch_lin(req, req_payload, out_resp, out_resp_payload, stream_id);
        if (req.byte_bus_id == kCanByteBusId)
            return dispatch_can(req, req_payload, out_resp, out_resp_payload, stream_id);
        if (req.byte_bus_id == kUartByteBusId)
            return dispatch_uart(req, req_payload, out_resp, out_resp_payload, stream_id);
        if (req.byte_bus_id == kIseledByteBusId)
            return dispatch_iseled(req, req_payload, out_resp, out_resp_payload, stream_id);
        if (req.byte_bus_id == kMdioByteBusId)
            return dispatch_mdio(req, req_payload, out_resp, out_resp_payload, stream_id);

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

    // apply_cancellation applies an already-classified AdmitOutcome::
    // Cancellation request against admission's own request store — ported
    // from c-RCP's apply_cancellation() (src/mock.c:1180-1245), reduced to
    // the two whole-store cancellation kinds (clear-all/clear-non-
    // safestate) this batch actually needs a response shape for.
    // clear-single's own REQ-CANCEL-012 chain-cascade needs its target
    // transaction_num decoded out of the raw frame
    // (request::decode_clear_single(), request.hpp) plus a genuine
    // REQUEST_NOT_FOUND error response when no match exists — deliberately
    // left as a stub here: AdmitOutcome::Cancellation is unreachable
    // through this file's own dispatch_*() below in the first place (see
    // admit_and_classify()'s own comment), so there is no dispatch()
    // caller yet that can exercise it end-to-end; wiring a GBB-carrying
    // frame in that actually reaches this path is fragment.hpp/AVTPDU
    // frame-level dispatch_frame() territory (batch D's own scope).
    // TODO(phase4-batch-d): decode clear-single's own target transaction_num
    // and apply REQ-CANCEL-012's chain cascade once dispatch_frame() can
    // reach this path with a real multi-member frame to derive
    // chain_group/chain_position from.
    static void apply_cancellation(server::Endpoint& admission, request::RequestTypeOpcode request_type,
                                    acf::AcfMessageInfo& out_resp,
                                    std::vector<uint8_t>& out_resp_payload) noexcept {
        out_resp         = acf::AcfMessageInfo{};
        out_resp_payload.clear();
        switch (request_type) {
        case request::RequestTypeOpcode::ClearAll:
            (void)admission.cancel_all();
            break;
        case request::RequestTypeOpcode::ClearNonSafestate:
            (void)admission.cancel_non_safestate();
            break;
        default:
            break; // ClearSingle: see this function's own TODO above.
        }
    }

    // admit_and_classify is the shared admission gate every dispatch_*()
    // below now runs, immediately after its own operational_requests_
    // allowed() check — REPLACING the old "call the handler unconditionally"
    // pattern (Phase 4/Phase 17 batch A, cpp-RCP issue #129). Ported from
    // c-RCP's dispatch_plain_inner()'s own admit_with_ack() call
    // (src/mock.c:1549-1551) composed with finish_admission()/
    // admission_reject_response_shape() (src/mock.c:1290-1423, both ported
    // above this class), reduced to what this mock's own Standard-request-
    // only dispatch() can actually reach (see this file's own header
    // comment on why conditional request kinds are rcp/sim.hpp's concern):
    // every request built here is always encoded ACF_ABB
    // (acf::encode_acf_abb(), never ACF_GBB), so server::Endpoint::
    // admit_with_ack()'s own repurposed-opcode peek always reports "not a
    // conditional/cancellation request" — meaning only AdmitOutcome::
    // ExecuteNow/Queued/Suspended/Rejected are ever actually reachable
    // through this function AS CALLED BELOW. The Pending/Cancellation
    // branches are still fully implemented for classifier completeness (a
    // later batch's GBB-carrying frame-level dispatch, or a direct
    // rcp::server::Endpoint caller via admission() above, can reach them),
    // just not exercised by this file's own dispatch_*() call sites — see
    // tests/test_mock.cpp for coverage of those two paths driven directly
    // through admission() instead.
    //
    // now/tv/avtp_timestamp/gptp_reference_now are passed as 0/false/0/0:
    // this mock's own dispatch() models the standard request kind under an
    // (implicit) NTSCF header only — no TSCF presentation-time gate, no
    // tick count of its own — matching every other "this mock has no clock
    // of its own" disclaimer in this file (e.g. dispatch_uart's own
    // comment). A caller wanting REQ-TIMED-012's own TSCF gate exercised
    // calls admission(byte_bus_id)->admit_with_ack() directly with real
    // values, the same escape hatch tick()'s own doc comment above already
    // establishes.
    //
    // Returns true iff the caller must now run its own handler body
    // (AdmitOutcome::ExecuteNow) — every other outcome already has
    // out_resp/out_resp_payload fully built and an appropriate
    // std::error_code returned via out_ec (DispatchErrc's own doc comment
    // above documents the four non-wire outcomes this signals through it;
    // a genuine wire ErrorResponse/Acknowledge-rejected uses
    // regmap::RegMapErrc::request_rejected instead, mirroring
    // operational_requests_allowed()'s own existing "rejected" signal).
    static bool admit_and_classify(server::Endpoint& admission, const acf::AcfMessageInfo& req,
                                    const std::vector<uint8_t>& payload, acf::AcfMessageInfo& out_resp,
                                    std::vector<uint8_t>& out_resp_payload, std::error_code& out_ec) {
        const std::vector<uint8_t> frame = acf::encode_acf_abb(req, payload);

        std::optional<request::RequestTypeOpcode> request_type;
        std::optional<acf::WireErrorCode>          admit_error;
        std::vector<uint8_t>                       ack;
        const server::AdmitOutcome outcome =
            admission.admit_with_ack(frame.data(), frame.size(), /*now=*/0, /*tv=*/false,
                                      /*avtp_timestamp=*/0, /*gptp_reference_now=*/0, request_type,
                                      /*out_index=*/nullptr, &admit_error, &ack);

        switch (outcome) {
        case server::AdmitOutcome::ExecuteNow:
            return true;

        case server::AdmitOutcome::Queued:
        case server::AdmitOutcome::Pending:
            // REQ-SRV-016/TC18 §12.9.5: a genuine Acknowledge iff evt[3]
            // asked for one and admission actually filed the request into
            // storage — exactly what a nonempty `ack` here already means
            // (server::Endpoint::build_store_ack()'s own identical gate,
            // reading the same evt[3] bit acf::evt_requests_acknowledge()
            // tests, off this codec's own already-decoded req.evt_ack).
            if (!ack.empty()) {
                out_resp = acf::make_response(req, acf::ResponseKind::Acknowledge);
                out_resp_payload.clear();
                out_ec = {};
            } else {
                out_resp = acf::AcfMessageInfo{};
                out_resp_payload.clear();
                out_ec = make_error_code(outcome == server::AdmitOutcome::Queued ? DispatchErrc::queued
                                                                                   : DispatchErrc::pending);
            }
            return false;

        case server::AdmitOutcome::Cancellation:
            apply_cancellation(admission, *request_type, out_resp, out_resp_payload);
            out_ec = make_error_code(DispatchErrc::cancelled);
            return false;

        case server::AdmitOutcome::Suspended:
            // REQ-PWRMODE-028: nothing was inspected — no ack to check, no
            // response of any shape (c-RCP's finish_admission() default
            // branch with error == RCP_ERROR_NONE, src/mock.c:1394-1421).
            out_resp = acf::AcfMessageInfo{};
            out_resp_payload.clear();
            out_ec = make_error_code(DispatchErrc::suspended);
            return false;

        case server::AdmitOutcome::Rejected:
        default:
            if (admit_error) {
                if (admission_reject_response_shape(*admit_error) == AdmitRejectShape::ErrorResponse) {
                    // TC18 §13.5.1 "err-response": unconditional, no evt[3]
                    // qualifier.
                    out_resp         = acf::make_response(req, acf::ResponseKind::ErrorResponse);
                    out_resp_payload = acf::encode_error_payload(*admit_error);
                } else if (req.evt_ack) {
                    // TC18 §11.3.1's Acknowledge-rejected shape — "if
                    // requested" (REQ-SRV-016's own gate, mirrored here for
                    // rejection per c-RCP issue #454).
                    out_resp         = make_acknowledge_rejected_response(req);
                    out_resp_payload = acf::encode_error_payload(*admit_error);
                } else {
                    out_resp = acf::AcfMessageInfo{};
                    out_resp_payload.clear();
                }
            } else {
                out_resp = acf::AcfMessageInfo{};
                out_resp_payload.clear();
            }
            out_ec = make_error_code(regmap::RegMapErrc::request_rejected);
            return false;
        }
    }

    // suppress_response_per_stream_cfg — TC18 §12.7.7 Table 24
    // (REQ-RMAP-048/049), ported from c-RCP's own identically-named
    // function (src/mock.c:1716-1742). Table 24's own two per-request-
    // stream routing pointers each carry a "0 means no X is to be sent"
    // default — rx_ack_stream_index for an Acknowledge-classified response
    // (acf::response_kind_of(out_resp) == ResponseKind::Acknowledge, which
    // also covers the Acknowledge-REJECTED shape
    // make_acknowledge_rejected_response() builds above, since evt[3:0]
    // alone — not err — decides that classification, same as c-RCP's own
    // rcp_acf_classify_response()), rx_resp_stream_index for every other
    // response kind (Write/Read/Error). This mock owns no real
    // multi-stream transport to actually DELIVER a response on a
    // caller-chosen stream (the same "simulator, not a scheduler/
    // transport" boundary this class's own tick()/drain_one() doc
    // comments already establish) — but it CAN, and now does, honor the
    // "0 means send nothing at all" half of that rule, which needs no
    // transport concept whatsoever: out_resp/out_resp_payload are simply
    // reset to the identical "nothing to send" shape DispatchErrc's own
    // evt[3]-less outcomes already use (rsp == false, payload empty) —
    // see admit_and_classify()'s own doc comment for that shape's own
    // contract.
    //
    // An unresolvable stream_id (regmap::request_stream_cfg::
    // resolve_index() returns 0 — no set_request_stream_cfg() entry names
    // it) suppresses nothing, the same fail-toward-no-action disposition
    // every resolve_index() call site in c-RCP's own mock.c already uses;
    // a caller that never configured a request stream (including every
    // dispatch() call site that leaves the new trailing stream_id
    // parameter at its default) sees this mock's pre-existing, unaffected
    // response behavior. `if (!out_resp.rsp) return;` mirrors c-RCP's own
    // `if (out->data == NULL) return;` leading guard — nothing built,
    // nothing to suppress.
    static void suppress_response_per_stream_cfg(const regmap::RegisterMap& regs,
                                                  avtp::StreamId stream_id,
                                                  acf::AcfMessageInfo& out_resp,
                                                  std::vector<uint8_t>& out_resp_payload) noexcept {
        if (!out_resp.rsp) return;

        const uint8_t stream_index = regmap::request_stream_cfg::resolve_index(
            regs.request_streams.data(), regs.request_streams.size(), stream_id.to_u64());
        if (stream_index == 0) return;

        const regmap::RequestStreamConfig& cfg = regs.request_streams[stream_index - 1];
        const bool suppress = (acf::response_kind_of(out_resp) == acf::ResponseKind::Acknowledge)
                                   ? (cfg.rx_ack_stream_index == 0)
                                   : (cfg.rx_resp_stream_index == 0);
        if (!suppress) return;

        out_resp = acf::AcfMessageInfo{};
        out_resp_payload.clear();
    }

    static regmap::RegisterMap make_initial_register_map() {
        regmap::RegisterMap regs;
        regs.general.svr_ep_count = 10;
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
        // Bug fix (Phase 4/Phase 17 batch B): every row above left its own
        // trailing request_stream_index/crc_required fields at
        // EpIdMappingEntry's own struct defaults (0/false) -- but 0 is
        // REQ-RMAP-054's own defined END-OF-TABLE SENTINEL
        // (regmap::ep_id_map::effective_count(), regmap.hpp), not "row 0's
        // own request stream". Left uncorrected, effective_count() over
        // this table would read the very first row's request_stream_index
        // == 0 and report the WHOLE ten-row table as zero effective rows,
        // even though every row here is a real, populated mapping. Set to
        // 1 (the same power-on default regmap::ep_id_map::row_init_default()
        // itself assigns, and the same value RequestStreamConfig::
        // rx_resp_stream_index's own struct default already uses for "the
        // one pre-existing stream a freshly reset server can answer
        // through") so this table renders as ten real, associated rows
        // rather than an apparently-empty one -- inert today (nothing in
        // this file yet consults request_stream_index, see this class's
        // own header comment on this batch's own scope), but a genuine
        // correctness fix for whichever later batch's REQ-SEQ-013/
        // REQ-E2E-029/030/045 logic (broadcast_safe_state and friends,
        // c-RCP's own mock.c) is first to actually read it.
        for (auto& entry : regs.ep_id_mapping) entry.request_stream_index = 1;
        // REQ-RMAP-037: syncs the matching Table 20 capacity register to
        // this table's own real length, the same convention
        // set_ep_id_map() below establishes for a later wholesale
        // replacement — previously left at GeneralMap's own zero default
        // despite this constructor already populating ten real rows above.
        regs.general.svr_ep_bytebus_id_map_capacity = 10;
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
        avtp::detail::put_u32(out_resp_payload.data(), regs_.general.magic);
        out_resp = acf::make_response(req, acf::ResponseKind::ReadResponse);
        return {};
    }

    bool operational_requests_allowed() const noexcept {
        return lifecycle_.state() == lifecycle::ServerState::RcpConfigured;
    }

    // dispatch_gpio — Phase 4/Phase 17 batch B: thin wrapper applying TC18
    // Table 24 response suppression (REQ-RMAP-048/049) around
    // dispatch_gpio_inner()'s own unchanged (batch A) handler body — see
    // suppress_response_per_stream_cfg()'s own doc comment and this
    // class's own header comment, item 1.
    std::error_code dispatch_gpio(const acf::AcfMessageInfo& req, const std::vector<uint8_t>& payload,
                                   acf::AcfMessageInfo& out_resp, std::vector<uint8_t>& out_resp_payload,
                                   avtp::StreamId stream_id) noexcept {
        std::error_code ec = dispatch_gpio_inner(req, payload, out_resp, out_resp_payload);
        suppress_response_per_stream_cfg(regs_, stream_id, out_resp, out_resp_payload);
        return ec;
    }

    std::error_code dispatch_gpio_inner(const acf::AcfMessageInfo& req, const std::vector<uint8_t>& payload,
                                   acf::AcfMessageInfo& out_resp,
                                   std::vector<uint8_t>& out_resp_payload) noexcept {
        if (!operational_requests_allowed()) {
            return set_error_response(req, make_error_code(regmap::RegMapErrc::request_rejected),
                                       out_resp, out_resp_payload);
        }
        std::error_code admit_ec;
        if (!admit_and_classify(gpio_admission_, req, payload, out_resp, out_resp_payload, admit_ec)) {
            return admit_ec;
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
    // dispatch_spi — Phase 4/Phase 17 batch B: thin wrapper applying TC18
    // Table 24 response suppression (REQ-RMAP-048/049) around
    // dispatch_spi_inner()'s own unchanged (batch A) handler body — see
    // suppress_response_per_stream_cfg()'s own doc comment and this
    // class's own header comment, item 1.
    std::error_code dispatch_spi(const acf::AcfMessageInfo& req, const std::vector<uint8_t>& payload,
                                  acf::AcfMessageInfo& out_resp, std::vector<uint8_t>& out_resp_payload,
                                  avtp::StreamId stream_id) noexcept {
        std::error_code ec = dispatch_spi_inner(req, payload, out_resp, out_resp_payload);
        suppress_response_per_stream_cfg(regs_, stream_id, out_resp, out_resp_payload);
        return ec;
    }

    std::error_code dispatch_spi_inner(const acf::AcfMessageInfo& req, const std::vector<uint8_t>& payload,
                                  acf::AcfMessageInfo& out_resp,
                                  std::vector<uint8_t>& out_resp_payload) noexcept {
        if (!operational_requests_allowed()) {
            return set_error_response(req, make_error_code(regmap::RegMapErrc::request_rejected),
                                       out_resp, out_resp_payload);
        }
        std::error_code admit_ec;
        if (!admit_and_classify(spi_admission_, req, payload, out_resp, out_resp_payload, admit_ec)) {
            return admit_ec;
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
    // dispatch_i2c — Phase 4/Phase 17 batch B: thin wrapper applying TC18
    // Table 24 response suppression (REQ-RMAP-048/049) around
    // dispatch_i2c_inner()'s own unchanged (batch A) handler body — see
    // suppress_response_per_stream_cfg()'s own doc comment and this
    // class's own header comment, item 1.
    std::error_code dispatch_i2c(const acf::AcfMessageInfo& req, const std::vector<uint8_t>& payload,
                                  acf::AcfMessageInfo& out_resp, std::vector<uint8_t>& out_resp_payload,
                                  avtp::StreamId stream_id) noexcept {
        std::error_code ec = dispatch_i2c_inner(req, payload, out_resp, out_resp_payload);
        suppress_response_per_stream_cfg(regs_, stream_id, out_resp, out_resp_payload);
        return ec;
    }

    std::error_code dispatch_i2c_inner(const acf::AcfMessageInfo& req, const std::vector<uint8_t>& payload,
                                  acf::AcfMessageInfo& out_resp,
                                  std::vector<uint8_t>& out_resp_payload) noexcept {
        if (!operational_requests_allowed()) {
            return set_error_response(req, make_error_code(regmap::RegMapErrc::request_rejected),
                                       out_resp, out_resp_payload);
        }
        std::error_code admit_ec;
        if (!admit_and_classify(i2c_admission_, req, payload, out_resp, out_resp_payload, admit_ec)) {
            return admit_ec;
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
    // dispatch_adc — Phase 4/Phase 17 batch B: thin wrapper applying TC18
    // Table 24 response suppression (REQ-RMAP-048/049) around
    // dispatch_adc_inner()'s own unchanged (batch A) handler body — see
    // suppress_response_per_stream_cfg()'s own doc comment and this
    // class's own header comment, item 1.
    std::error_code dispatch_adc(const acf::AcfMessageInfo& req, const std::vector<uint8_t>& payload,
                                  acf::AcfMessageInfo& out_resp, std::vector<uint8_t>& out_resp_payload,
                                  avtp::StreamId stream_id) noexcept {
        std::error_code ec = dispatch_adc_inner(req, payload, out_resp, out_resp_payload);
        suppress_response_per_stream_cfg(regs_, stream_id, out_resp, out_resp_payload);
        return ec;
    }

    std::error_code dispatch_adc_inner(const acf::AcfMessageInfo& req, const std::vector<uint8_t>& payload,
                                  acf::AcfMessageInfo& out_resp,
                                  std::vector<uint8_t>& out_resp_payload) noexcept {
        if (!operational_requests_allowed()) {
            return set_error_response(req, make_error_code(regmap::RegMapErrc::request_rejected),
                                       out_resp, out_resp_payload);
        }
        std::error_code admit_ec;
        if (!admit_and_classify(adc_admission_, req, payload, out_resp, out_resp_payload, admit_ec)) {
            return admit_ec;
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
    // dispatch_pwm_in — Phase 4/Phase 17 batch B: thin wrapper applying
    // TC18 Table 24 response suppression (REQ-RMAP-048/049) around
    // dispatch_pwm_in_inner()'s own unchanged (batch A) handler body —
    // see suppress_response_per_stream_cfg()'s own doc comment and this
    // class's own header comment, item 1.
    std::error_code dispatch_pwm_in(const acf::AcfMessageInfo& req, const std::vector<uint8_t>& payload,
                                     acf::AcfMessageInfo& out_resp, std::vector<uint8_t>& out_resp_payload,
                                     avtp::StreamId stream_id) noexcept {
        std::error_code ec = dispatch_pwm_in_inner(req, payload, out_resp, out_resp_payload);
        suppress_response_per_stream_cfg(regs_, stream_id, out_resp, out_resp_payload);
        return ec;
    }

    std::error_code dispatch_pwm_in_inner(const acf::AcfMessageInfo& req, const std::vector<uint8_t>& payload,
                                     acf::AcfMessageInfo& out_resp,
                                     std::vector<uint8_t>& out_resp_payload) noexcept {
        if (!operational_requests_allowed()) {
            return set_error_response(req, make_error_code(regmap::RegMapErrc::request_rejected),
                                       out_resp, out_resp_payload);
        }
        std::error_code admit_ec;
        if (!admit_and_classify(pwm_in_admission_, req, payload, out_resp, out_resp_payload, admit_ec)) {
            return admit_ec;
        }
        // `payload` remains otherwise unused below (§13.7.6.3 carries no
        // functional byte_msg_payload for this endpoint type — see this
        // function's own header comment); admission above still needs the
        // real bytes to reconstruct the frame it peeks/stores.
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
    // dispatch_lin — Phase 4/Phase 17 batch B: thin wrapper applying TC18
    // Table 24 response suppression (REQ-RMAP-048/049) around
    // dispatch_lin_inner()'s own unchanged (batch A) handler body — see
    // suppress_response_per_stream_cfg()'s own doc comment and this
    // class's own header comment, item 1.
    std::error_code dispatch_lin(const acf::AcfMessageInfo& req, const std::vector<uint8_t>& payload,
                                  acf::AcfMessageInfo& out_resp, std::vector<uint8_t>& out_resp_payload,
                                  avtp::StreamId stream_id) noexcept {
        std::error_code ec = dispatch_lin_inner(req, payload, out_resp, out_resp_payload);
        suppress_response_per_stream_cfg(regs_, stream_id, out_resp, out_resp_payload);
        return ec;
    }

    std::error_code dispatch_lin_inner(const acf::AcfMessageInfo& req, const std::vector<uint8_t>& payload,
                                  acf::AcfMessageInfo& out_resp,
                                  std::vector<uint8_t>& out_resp_payload) noexcept {
        if (!operational_requests_allowed()) {
            return set_error_response(req, make_error_code(regmap::RegMapErrc::request_rejected),
                                       out_resp, out_resp_payload);
        }
        std::error_code admit_ec;
        if (!admit_and_classify(lin_admission_, req, payload, out_resp, out_resp_payload, admit_ec)) {
            return admit_ec;
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
    // dispatch_can — Phase 4/Phase 17 batch B: thin wrapper applying TC18
    // Table 24 response suppression (REQ-RMAP-048/049) around
    // dispatch_can_inner()'s own unchanged (batch A) handler body — see
    // suppress_response_per_stream_cfg()'s own doc comment and this
    // class's own header comment, item 1.
    std::error_code dispatch_can(const acf::AcfMessageInfo& req, const std::vector<uint8_t>& payload,
                                  acf::AcfMessageInfo& out_resp, std::vector<uint8_t>& out_resp_payload,
                                  avtp::StreamId stream_id) noexcept {
        std::error_code ec = dispatch_can_inner(req, payload, out_resp, out_resp_payload);
        suppress_response_per_stream_cfg(regs_, stream_id, out_resp, out_resp_payload);
        return ec;
    }

    std::error_code dispatch_can_inner(const acf::AcfMessageInfo& req, const std::vector<uint8_t>& payload,
                                  acf::AcfMessageInfo& out_resp,
                                  std::vector<uint8_t>& out_resp_payload) noexcept {
        if (!operational_requests_allowed()) {
            return set_error_response(req, make_error_code(regmap::RegMapErrc::request_rejected),
                                       out_resp, out_resp_payload);
        }
        std::error_code admit_ec;
        if (!admit_and_classify(can_admission_, req, payload, out_resp, out_resp_payload, admit_ec)) {
            return admit_ec;
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
    // dispatch_uart — Phase 4/Phase 17 batch B: thin wrapper applying TC18
    // Table 24 response suppression (REQ-RMAP-048/049) around
    // dispatch_uart_inner()'s own unchanged (batch A) handler body — see
    // suppress_response_per_stream_cfg()'s own doc comment and this
    // class's own header comment, item 1.
    std::error_code dispatch_uart(const acf::AcfMessageInfo& req, const std::vector<uint8_t>& payload,
                                   acf::AcfMessageInfo& out_resp, std::vector<uint8_t>& out_resp_payload,
                                   avtp::StreamId stream_id) noexcept {
        std::error_code ec = dispatch_uart_inner(req, payload, out_resp, out_resp_payload);
        suppress_response_per_stream_cfg(regs_, stream_id, out_resp, out_resp_payload);
        return ec;
    }

    std::error_code dispatch_uart_inner(const acf::AcfMessageInfo& req, const std::vector<uint8_t>& payload,
                                   acf::AcfMessageInfo& out_resp,
                                   std::vector<uint8_t>& out_resp_payload) noexcept {
        if (!operational_requests_allowed()) {
            return set_error_response(req, make_error_code(regmap::RegMapErrc::request_rejected),
                                       out_resp, out_resp_payload);
        }
        std::error_code admit_ec;
        if (!admit_and_classify(uart_admission_, req, payload, out_resp, out_resp_payload, admit_ec)) {
            return admit_ec;
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

    // ISELED, same as I2C/LIN (§13.7.12.3), operates on the raw
    // byte_msg_payload directly — this dispatch path does not branch on
    // req.op either, same rationale as dispatch_i2c/dispatch_lin's own
    // comment, and passes `payload` straight to
    // IseledEndpoint::handle_request unchanged, the same "mock has already
    // ACF-decoded the frame, so it calls the endpoint's own dispatch entry
    // point with the raw payload rather than re-invoking rcp/iseled.hpp's
    // own encode/decode_command_request() codec a second time" pattern
    // dispatch_i2c uses. Table 33 Row 2's 3-way Plain/Reserved/ConfigWrite
    // classification (rcp::endpoint::evt_row2_kind_of) is checked by
    // handle_request itself, before anything is recorded — see
    // handle_request's own doc comment for why a Reserved or ConfigWrite
    // evt must never reach it.
    //
    // This mock has no real ISELED daisy-chain hardware behind it (same
    // disclaimer every other endpoint type in this file carries), so the
    // response bytes recorded on a successful Plain request are whatever
    // set_iseled_response() last scripted (empty if never called) — the
    // same "test scripts the bus, this mock does not model actual
    // hardware" pattern set_i2c_response/set_lin_response already establish
    // for their own bus-transfer models. Answered as a ReadResponse,
    // mirroring dispatch_i2c/dispatch_lin's read-response shape.
    // dispatch_iseled — Phase 4/Phase 17 batch B: thin wrapper applying
    // TC18 Table 24 response suppression (REQ-RMAP-048/049) around
    // dispatch_iseled_inner()'s own unchanged (batch A) handler body —
    // see suppress_response_per_stream_cfg()'s own doc comment and this
    // class's own header comment, item 1.
    std::error_code dispatch_iseled(const acf::AcfMessageInfo& req, const std::vector<uint8_t>& payload,
                                     acf::AcfMessageInfo& out_resp, std::vector<uint8_t>& out_resp_payload,
                                     avtp::StreamId stream_id) noexcept {
        std::error_code ec = dispatch_iseled_inner(req, payload, out_resp, out_resp_payload);
        suppress_response_per_stream_cfg(regs_, stream_id, out_resp, out_resp_payload);
        return ec;
    }

    std::error_code dispatch_iseled_inner(const acf::AcfMessageInfo& req, const std::vector<uint8_t>& payload,
                                     acf::AcfMessageInfo& out_resp,
                                     std::vector<uint8_t>& out_resp_payload) noexcept {
        if (!operational_requests_allowed()) {
            return set_error_response(req, make_error_code(regmap::RegMapErrc::request_rejected),
                                       out_resp, out_resp_payload);
        }
        std::error_code admit_ec;
        if (!admit_and_classify(iseled_admission_, req, payload, out_resp, out_resp_payload, admit_ec)) {
            return admit_ec;
        }
        auto ec = iseled_.handle_request(req.evt_op, payload);
        if (ec) return set_error_response(req, ec, out_resp, out_resp_payload);
        iseled_.receive(iseled_response_);
        out_resp_payload = iseled_.last_received();
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
    // comment for the identical rationale). Unlike ISELED/I2C/LIN, whose
    // ACF byte_msg_payload IS the raw wire content itself (no further
    // decode needed at this dispatch layer), NO MDIO byte-level wire codec
    // (Figure 43/Table 60) exists
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
    // dispatch_mdio — Phase 4/Phase 17 batch B: thin wrapper applying TC18
    // Table 24 response suppression (REQ-RMAP-048/049) around
    // dispatch_mdio_inner()'s own unchanged (batch A) handler body — see
    // suppress_response_per_stream_cfg()'s own doc comment and this
    // class's own header comment, item 1.
    std::error_code dispatch_mdio(const acf::AcfMessageInfo& req, const std::vector<uint8_t>& payload,
                                   acf::AcfMessageInfo& out_resp, std::vector<uint8_t>& out_resp_payload,
                                   avtp::StreamId stream_id) noexcept {
        std::error_code ec = dispatch_mdio_inner(req, payload, out_resp, out_resp_payload);
        suppress_response_per_stream_cfg(regs_, stream_id, out_resp, out_resp_payload);
        return ec;
    }

    std::error_code dispatch_mdio_inner(const acf::AcfMessageInfo& req, const std::vector<uint8_t>& payload,
                                   acf::AcfMessageInfo& out_resp,
                                   std::vector<uint8_t>& out_resp_payload) noexcept {
        if (!operational_requests_allowed()) {
            return set_error_response(req, make_error_code(regmap::RegMapErrc::request_rejected),
                                       out_resp, out_resp_payload);
        }
        std::error_code admit_ec;
        if (!admit_and_classify(mdio_admission_, req, payload, out_resp, out_resp_payload, admit_ec)) {
            return admit_ec;
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
    // Phase 4/Phase 17 batch A: one rcp::server::Endpoint admission queue/
    // conditional-request store per operational endpoint above — see this
    // class's own header comment and admit_and_classify()'s doc comment for
    // how these are wired into dispatch_*() below. Mirrors c-RCP's own
    // rcp_mock_endpoint_slot_t.queue field, one per registered endpoint
    // (src/mock.c:35-233, field at line 60).
    server::Endpoint           gpio_admission_;
    server::Endpoint           spi_admission_;
    server::Endpoint           i2c_admission_;
    server::Endpoint           adc_admission_;
    server::Endpoint           pwm_in_admission_;
    server::Endpoint           lin_admission_;
    server::Endpoint           can_admission_;
    server::Endpoint           uart_admission_;
    server::Endpoint           iseled_admission_;
    server::Endpoint           mdio_admission_;
    // REQ-SRV-018: this Server's own edge-detector state for TC18 Table
    // 37's gPTP lock-established/lost trigger signals — see
    // notify_gptp_lock_state()'s own doc comment above.
    server::GptpTriggerState   gptp_trigger_state_{};
    // REQ-RMAP-066 (Phase 4/Phase 17 batch B): this Server's own
    // discovery-stream claim/timeout state -- see discovery_claim()'s and
    // set_discovery_timeout_us()'s own doc comments above. Default-
    // constructed to discovery::DiscoveryClaim::kDefaultTimeout (20 ms),
    // then immediately re-synced from regs_.svr_ep_cfg.svr_discovery_
    // timeout by the constructor body above -- the same two-step
    // "zero-init default, then one real sync call" pattern this class's
    // own gptp_trigger_state_ and every RegisterMap table above already
    // use for their own power-on state.
    discovery::DiscoveryClaim  discovery_claim_{};
    std::array<std::vector<uint8_t>, spi::kMaxChannels> spi_poci_{};
    std::vector<uint8_t>       i2c_response_{};
    bool                       i2c_acked_ = true;
    std::deque<uint16_t>       adc_samples_{};
    std::vector<uint8_t>       lin_response_{};
    bool                       lin_responded_ = true;
    std::vector<uint8_t>       iseled_response_{};
};

} // namespace mock
} // namespace rcp
