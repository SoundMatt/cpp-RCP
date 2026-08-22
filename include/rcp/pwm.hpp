// fusa:req REQ-PWM-001
// fusa:req REQ-PWM-002
// fusa:req REQ-PWM-003
// fusa:req REQ-PWM-004
// fusa:req REQ-PWM-005
// fusa:req REQ-PWM-006
// fusa:req REQ-PWM-007
// fusa:req REQ-PWM-008
// fusa:req REQ-PWM-009
// fusa:req REQ-PWM-010
// fusa:req REQ-PWM-011
// fusa:req REQ-PWM-012
// fusa:req REQ-PWM-013
// fusa:req REQ-PWM-014
// fusa:req REQ-PWM-015
// fusa:req REQ-PWM-016
// fusa:req REQ-PWM-017
// fusa:req REQ-PWM-018
// fusa:req REQ-PWM-019
// fusa:req REQ-PWM-020
// fusa:req REQ-PWM-021
// fusa:req REQ-PWM-022
// fusa:req REQ-PWM-023
// fusa:req REQ-PWM-024
// fusa:req REQ-PWM-025
// fusa:req REQ-PWM-026
// fusa:req REQ-PWM-027
// fusa:req REQ-PWM-028
// fusa:req REQ-PWM-029
// fusa:req REQ-PWM-030
// fusa:req REQ-PWM-031
// fusa:req REQ-PWM-032
// fusa:req REQ-PWM-033
// fusa:req REQ-PWM-034
// fusa:req REQ-PWM-035
// fusa:req REQ-PWM-036
// fusa:req REQ-PWM-037
// fusa:req REQ-PWM-038
// fusa:req REQ-PWM-039
// fusa:req REQ-PWM-040
// fusa:req REQ-PWM-041
// fusa:req REQ-PWM-042
// fusa:req REQ-PWM-043
// fusa:req REQ-PWM-044
// fusa:req REQ-PWM-045
// fusa:req REQ-PWM-046
// fusa:req REQ-PWM-047
// fusa:req REQ-PWM-048
// fusa:req REQ-PWM-049
// fusa:req REQ-PWM-050
// fusa:req REQ-PWM-051
// fusa:req REQ-PWM-052
// fusa:req REQ-PWM-053
// fusa:req REQ-PWM-054
// fusa:req REQ-PWM-055
// fusa:req REQ-PWM-056
// fusa:req REQ-PWM-057
// fusa:req REQ-PWM-058
// fusa:req REQ-PWM-059
// fusa:req REQ-PWM-060
// fusa:req REQ-PWM-061
// fusa:req REQ-PWM-062
// fusa:req REQ-PWM-063
// fusa:req REQ-PWM-064
// fusa:req REQ-PWM-065
// fusa:req REQ-PWM-066
// fusa:req REQ-PWM-067
// fusa:req REQ-PWM-068
// fusa:req REQ-PWM-069
// fusa:req REQ-PWM-070
// fusa:req REQ-PWM-071
// fusa:req REQ-PWM-072
// fusa:req REQ-PWM-073
// fusa:req REQ-PWM-074
// fusa:req REQ-PWM-075

// PWM_OUT (ep_type 0x07) and PWM_IN (ep_type 0x08) endpoints — the shared
// period/active-duration 4-byte payload shape, PWM_OUT's eight GPIO-style
// evt[2:0] write semantics plus duty-cycle capping and skew-delayed trigger
// timing, PWM_IN's response-only read model with a real EP_func register
// block and MAX_PERIOD timeout classification, and the compound-wait
// numeric comparison modes against a captured PWM_IN measurement (extraction
// §13.7.5, §13.7.6, §13.5.1).
//
// ROADMAP.md Phase 17 / cpp-RCP issue #129, Phase 3 ("Per-endpoint
// modules"): this header is re-derived from c-RCP's ep_pwm.h/ep_pwm.c —
// c-RCP's RC5-conformant reference implementation for this endpoint type —
// rather than incrementally patched, per the roadmap's own module-by-module
// rewrite plan. No text from the OPEN Alliance TC18 Remote Control Protocol
// Specification is reproduced here; field names and behavior below implement
// TC18's *behavior* as ported from c-RCP's own implementation of an internal
// structured extraction of the specification. c-RCP models PWM_OUT/PWM_IN as
// free functions (no stateful "endpoint" object of its own); this port
// composes that same behavior into PwmOutEndpoint/PwmInEndpoint, matching
// rcp/gpio.hpp's and rcp/adc.hpp's own established Phase 3 endpoint-class
// idiom.
//
// Content re-verified against c-RCP's *current* ep_pwm.h/.c, not assumed
// accurate from this header's own earlier "Table 30/33 Row 2 evt[2:0]
// validation" pilot-module history (pre-Phase-3, v2.4.0) — that pass's own
// Plain/Reserved/ConfigWrite classification of PwmInEndpoint::handle_request
// via endpoint::evt_row2_kind_of was re-checked against c-RCP's
// rcp_ep_pwm_in_decode_read_request()/rcp_acf_evt_row2_is_plain() and found
// still correct; its trigger-table citation was NOT ("Table 44" — actually
// Table 47, "pwmi trigger outputs", REQ-PWM-032/033/034; fixed below,
// alongside a second pre-existing citation error c-RCP's own file header
// documents having made and corrected in ITS history, "Table 45" for
// PWM_IN's own functional-config table when the real Table 45 is "pwmo
// trigger outputs" — PWM_OUT's own table, unrelated to PWM_IN — corrected in
// c-RCP 2026-08-14, issue #428).
//
// Real content deltas this Phase 3 pass ported/fixed, beyond the pre-Phase-3
// wire codec (encode_pwm_payload/decode_pwm_payload, unchanged) and PWM_OUT
// write-semantics correction (issue #104, cpp-RCP-14, unchanged):
//
//  1. PWM_OUT Subtract operand order (found during this pass, mirroring
//     rcp/gpio.hpp's own already-fixed REQ-GPIO-011): apply_write_field's
//     Subtract case computed saturating_subtract(current, operand) —
//     current MINUS request — the reverse of Table 33's own GPIO/PWM_OUT
//     row, which (like GPIO's own row) defines Subtract as "byte_msg_payload
//     minus current interface status", request MINUS current
//     (REQ-PWM-007). Verified against c-RCP's own saturating_sub_u16(current,
//     request) => "(current > request) ? 0 : (request - current)". Fixed
//     below to saturating_subtract<uint16_t>(operand, current). This bug was
//     independent of, but the same operand-order class as, the PWM_IN
//     compound-wait polarity bug below — GPIO's own header comment
//     (rcp/gpio.hpp, REQ-GPIO-011) explicitly flagged this exact module as
//     "not yet re-verified against its own current c-RCP counterpart" when
//     it fixed its own copy of the identical mistake; this pass is that
//     re-verification.
//  2. PWM_IN compound-wait comparison polarity (c-RCP issue #256 Group B,
//     REQ-PWM-049..052): TC18 §13.5.1's own GE/LE naming is stated from the
//     wire payload's own point of view — evt[2:0]=100b/110b ("GE") is met
//     when byte_msg_payload (threshold) is >= the current interface status
//     (captured), i.e. captured <= threshold; evt=101b/111b ("LE") is the
//     mirror, captured >= threshold. Ported as compound_wait_compare()
//     below, matching c-RCP's own corrected rcp_ep_pwm_in_compound_wait_
//     compare() exactly (c-RCP's own file header records this direction was
//     itself once inverted and fixed — see that function's own doc comment
//     there); this header had no compound-wait comparison of any kind before
//     this pass, so there was no inverted copy of it to carry forward, only
//     the correct polarity to port fresh. A regression test below pins this
//     exact polarity.
//  3. PWM_OUT trigger-event tick derivation (REQ-PWM-055/067,
//     "PWM-055" fix): trigger_events_at_tick() below derives which of Table
//     45's CYCLE_START/MID_PULSE events fire at a given elapsed clock tick,
//     honoring both TC18 rules this header previously had no equivalent of
//     at all — trigger timing tracks the pwmo_skew-DELAYED cycle edge, not
//     the undelayed source edge, and MID_PULSE fires unconditionally at
//     active_duration/2 past that delayed start, including
//     active_duration == 0 (coincident with CYCLE_START, not suppressed).
//  4. PWM_IN's EP_func register block (TC18 §13.7.6.2 Table 48,
//     REQ-PWM-058/070/071): entirely missing before this pass — PwmInEndpoint
//     ::handle_request's ConfigWrite branch could only ever report
//     PwmErrc::config_write_not_supported, the same "N of 11 endpoint types"
//     accounting gap c-RCP's own issue #256 Group I found and closed for
//     this identical module. render_registers()/apply_reconfig() below are
//     the real register-block content (this port's own dispatch layer does
//     not yet route an incoming evt=111b request into apply_reconfig() —
//     see the "// TODO(phase3-followup)" marker on PwmInEndpoint below).
//  5. PWM_IN MAX_PERIOD timeout classification (REQ-PWM-072..075) — this
//     endpoint type's own analogue of rcp/gpio.hpp's debounce_sample()
//     (REQ-GPIO-035) and response_timing() (REQ-GPIO-036): a pure classifier
//     a caller drives with a real measured period and the configured
//     max_period/err_on_max_period/resp_on_err_enabled bits, since this
//     module owns no timer of its own. Entirely new; ported as
//     max_period_outcome()/pwm_in_wire_error() below.
//  6. PWM_OUT duty-cycle capping (REQ-PWM-056) and generation-state
//     classification (REQ-PWM-057/068/069) — both entirely new; ported as
//     apply_write()/generation_state() below.
//  7. PWM_IN's trigger model (REQ-PWM-032..034) was redesigned from this
//     header's own pre-Phase-3 invention (record_edge()/record_measurement()
//     unconditionally firing an always-armed RisingEdge/FallingEdge
//     TriggerRegistry pair on every capture) to match c-RCP's real Table 47
//     shape: a single, mutually-exclusive, client-selected PwmInTrigger
//     (None/Rising/Falling) evaluated against real level transitions via
//     trigger_fires(trigger, prev_level, new_level) — Table 47 names these
//     as PWM_IN's two fixed hardware trigger signals with no register field
//     selecting among them, so (matching c-RCP's own file header note) a
//     real implementation most naturally exposes both; the exclusive-select
//     `trigger` field is this codebase's own original simplification, same
//     disclaimer c-RCP's own rcp_ep_pwm_in_trigger_t carries. Both
//     `record_edge`/`record_measurement`'s prior unconditional-both-fire
//     behavior via the generic TriggerRegistry is retained for source
//     compatibility (PWM_IN's read model and rcp/mock.hpp's own
//     record_measurement() call site are otherwise unaffected by this
//     rework), alongside the new trigger_fires() classifier.
//
// Full ACF-level wire codec for both endpoint types (encode/decode read
// request, PWM_OUT write request including the reserved evt[2:0]=100b
// rejection, response) is ported below too, matching rcp/gpio.hpp's/
// rcp/adc.hpp's own Phase 3 wire-codec pattern — PWM_OUT/PWM_IN were never
// wired into rcp/mock.hpp's simulated dispatch loop (only PWM_IN's plain-read
// path is, via PwmInEndpoint::handle_request, unchanged in shape by this
// pass), so this is new, additive surface with no existing call site to
// preserve compatibility with.
//
// Field names and behavior below implement TC18's *behavior* as described in
// an internal structured extraction of the specification named above; no
// text from that document is reproduced here. The concrete period/duration
// unit (left as an opaque implementation-defined tick count, same as
// rcp/regmap.hpp's own field-width disclaimers) and trigger-signal id scheme
// chosen in this file are this implementation's own, same as the equivalent
// disclaimers in rcp/avtp.hpp, rcp/regmap.hpp, rcp/endpoint.hpp, rcp/gpio.hpp,
// rcp/i2c.hpp, and rcp/adc.hpp.
#pragma once

#include <rcp/acf.hpp>
#include <rcp/avtp.hpp>
#include <rcp/endpoint.hpp>
#include <rcp/lifecycle.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace rcp {
namespace pwm {

// ── Shared period/active-duration payload shape ───────────────────────────────
// `period` and `active_duration` share one implementation-defined tick unit
// (extraction §13.7.5, §13.7.6); this header does not itself fix that unit to
// a physical time value. Both fields are 16 bits wide on the wire — the
// PWM_OUT/PWM_IN payload is a fixed 4 bytes total (verified against the
// spec's "pwmo request format" figure, §13.7.5.3), not an open-ended
// 32-bit-per-field pair.

struct PwmValue {
    uint16_t period          = 0;
    uint16_t active_duration = 0;
};

// ── Wire codec: shared payload ────────────────────────────────────────────────
// Fixed 4-byte payload: PWM_Period (big-endian 16 bit) followed by PWM_active
// (big-endian 16 bit), per the spec's "pwmo request format" figure
// (§13.7.5.3) — a request not having exactly four bytes is rejected there
// with INVALID_PARAMETER, which decode_pwm_payload below surfaces as
// avtp::AvtpErrc::short_buffer for a buffer of the wrong length.

constexpr size_t kPwmPayloadLen = 4;
using PwmWireBytes              = std::array<uint8_t, kPwmPayloadLen>;

inline PwmWireBytes encode_pwm_payload(const PwmValue& value) noexcept {
    PwmWireBytes buf{};
    avtp::detail::put_u16(&buf[0], value.period);
    avtp::detail::put_u16(&buf[2], value.active_duration);
    return buf;
}

inline std::error_code decode_pwm_payload(const uint8_t* buf, size_t len, PwmValue& out) noexcept {
    // Spec §13.7.5.3: "A request not having exactly four bytes is rejected" —
    // reject both too-short and too-long buffers, not just too-short
    // (cpp-RCP-03).
    if (len != kPwmPayloadLen) return avtp::make_error_code(avtp::AvtpErrc::short_buffer);
    out.period          = avtp::detail::get_u16(&buf[0]);
    out.active_duration = avtp::detail::get_u16(&buf[2]);
    return {};
}

// The sentinel value either field of a PWM_IN response — or, by ep_adc.h's
// own reuse in c-RCP, an ADC raw sample (see rcp/adc.hpp's own kAdcNoSignal,
// defined locally there at this identical numeric value rather than
// depending on this header — see that header's own file comment) — carries
// on the wire when no valid measurement completed within the applicable
// timeout window (extraction §13.7.6). This is a WIRE-level constant: this
// header's own PwmInEndpoint below models "no signal" as an explicit
// has_signal_ bool + PwmErrc::no_signal error code for its own handle_read/
// handle_request surface (unchanged from before this pass), not by stuffing
// this sentinel into PwmValue's fields — kPwmInNoSignal exists for wire
// round-tripping (REQ-PWM-047) and for compound_wait_compare() below, which
// (matching c-RCP's own rcp_ep_pwm_in_compound_wait_compare()) operates on a
// raw captured PwmValue that may legitimately carry this sentinel in either
// field.
constexpr uint16_t kPwmInNoSignal = 0xFFFF;

// ── Errors ────────────────────────────────────────────────────────────────────

enum class PwmErrc : int {
    no_signal = 1, // PWM_IN_NO_SIGNAL: no pulse has been measured yet
    // evt_row2_kind_of classified the request as ConfigWrite (evt[2:0] ==
    // 111b, §12.7.1). PwmInEndpoint::handle_request (unchanged by this pass)
    // deliberately does not implement the configuration-write shape through
    // that entry point — see that method's own comment. Reported explicitly
    // rather than silently accepted as a plain read or silently ignored,
    // same as I2C's I2cErrc::config_write_not_supported and ADC's
    // AdcErrc::config_write_not_supported.
    config_write_not_supported = 2,
    short_frame                = 3,
    bad_msg_type                = 4,
    wrong_bus                    = 5,
    wrong_op                      = 6,
    bad_payload_len                = 7,
    // TC18 §13.5 Table 33's GPIO/PWM_OUT row, evt[2:0]=100b: "reserved --
    // request shall be ignored and an err-response with error code =
    // UNSUPPORTED_CMD shall be sent" -- the wire-decode half of that rule.
    reserved_evt                    = 8,
    // PWM_IN's own read-request decode: evt[2:0] is not one of the
    // plain-request values Table 33 Row 2 accepts for this endpoint type
    // (REQ-PWM-059).
    bad_evt                          = 9,
    // Configuration write (evt[2:0]==111b) payload carries no address
    // prefix, or an address prefix with no data octet after it.
    reconfig_short                    = 10,
    // Configuration write's start_address + data length exceeds the EP_func
    // block's own length -- the whole write is ignored, per the
    // specification's own rule.
    reconfig_out_of_range              = 11,
};

inline const std::error_category& pwm_category() noexcept {
    struct Cat : std::error_category {
        const char* name() const noexcept override { return "rcp.pwm"; }
        std::string message(int ev) const override {
            switch (static_cast<PwmErrc>(ev)) {
            case PwmErrc::no_signal: return "rcp/pwm: PWM_IN_NO_SIGNAL — no pulse measured";
            case PwmErrc::config_write_not_supported:
                return "rcp/pwm: evt[2:0]=111b configuration-write requests are not yet implemented";
            case PwmErrc::short_frame:     return "rcp/pwm: frame too short";
            case PwmErrc::bad_msg_type:    return "rcp/pwm: unexpected ACF message type";
            case PwmErrc::wrong_bus:       return "rcp/pwm: wrong byte_bus_id";
            case PwmErrc::wrong_op:        return "rcp/pwm: wrong ACF op";
            case PwmErrc::bad_payload_len: return "rcp/pwm: unexpected payload length";
            case PwmErrc::reserved_evt:    return "rcp/pwm: evt[2:0] is the reserved value 100b";
            case PwmErrc::bad_evt:         return "rcp/pwm: PWM_IN evt[2:0] is not a valid plain-request value";
            case PwmErrc::reconfig_short:
                return "rcp/pwm: configuration write has no address and data";
            case PwmErrc::reconfig_out_of_range:
                return "rcp/pwm: configuration write extends past the EP_func block";
            default: return "rcp/pwm: unknown error";
            }
        }
    };
    static Cat instance;
    return instance;
}

inline std::error_code make_error_code(PwmErrc e) noexcept {
    return {static_cast<int>(e), pwm_category()};
}

// wire_error maps e to its numbered wire error code (acf::WireErrorCode),
// for a caller building an Error Response frame once a request has failed to
// decode. std::nullopt for every PwmErrc value with no numbered counterpart
// (local framing/routing outcomes, or a specification rule that says to
// ignore the write rather than respond with an error).
inline std::optional<acf::WireErrorCode> wire_error(PwmErrc e) noexcept {
    switch (e) {
    // TC18 §13.7.5.3: "A request not having exactly four bytes is rejected
    // and an error response with error code = INVALID_PARAMETER will be
    // sent." (mirrors rcp/gpio.hpp's own wire_error(), REQ-GPIO-033/
    // REQ-PWM-028.)
    case PwmErrc::bad_payload_len: return acf::WireErrorCode::InvalidParameter;
    // TC18 §13.5 Table 33's GPIO/PWM_OUT row, evt[2:0]=100b (REQ-PWM-008),
    // and PWM_IN's own bad-evt/config-write-not-supported cases (Table 33
    // Row 2): all three are UNSUPPORTED_CMD.
    case PwmErrc::reserved_evt:                 return acf::WireErrorCode::UnsupportedCmd;
    case PwmErrc::bad_evt:                      return acf::WireErrorCode::UnsupportedCmd;
    case PwmErrc::config_write_not_supported:   return acf::WireErrorCode::UnsupportedCmd;
    // Table 27's own dedicated PWM_IN_NO_SIGNAL(9) code — unlike ADC's own
    // internal no_signal condition (adc::AdcErrc::no_signal, which has no
    // TC18-defined code and maps to EpError in rcp/mock.hpp's own
    // translation), PWM_IN has a real numbered code for this condition.
    case PwmErrc::no_signal:                    return acf::WireErrorCode::PwmInNoSignal;
    default: return std::nullopt;
    }
}

// ── PWM_OUT: write-semantics application ─────────────────────────────────────
// apply_write_field is PWM_OUT's own field-width combinator: it mirrors
// rcp::endpoint::apply_bitmask_write's case set, but instantiates
// saturating_add/saturating_subtract at uint16_t rather than uint32_t —
// apply_bitmask_write itself is hardcoded to uint32_t, so calling it
// directly against a uint16_t field and narrowing the result back down would
// compute the Add/Subtract saturation bound at the wrong width.
//
// Subtract's operand order (REQ-PWM-007; found and fixed during this pass —
// see the file header's own delta-list item 1): request MINUS current, not
// the reverse — saturating_subtract(operand, current), not
// saturating_subtract(current, operand).
inline std::error_code apply_write_field(endpoint::WriteSemantics op, uint16_t current,
                                          uint16_t operand, uint16_t& out) noexcept {
    switch (op) {
    case endpoint::WriteSemantics::Replace:  out = operand;                                                 return {};
    case endpoint::WriteSemantics::Or:       out = static_cast<uint16_t>(current | operand);                return {};
    case endpoint::WriteSemantics::And:      out = static_cast<uint16_t>(current & operand);                return {};
    case endpoint::WriteSemantics::Xor:      out = static_cast<uint16_t>(current ^ operand);                return {};
    case endpoint::WriteSemantics::Add:      out = endpoint::saturating_add<uint16_t>(current, operand);      return {};
    case endpoint::WriteSemantics::Subtract: out = endpoint::saturating_subtract<uint16_t>(operand, current); return {};
    case endpoint::WriteSemantics::Reserved:
        return endpoint::make_error_code(endpoint::EndpointErrc::reserved_write_semantics);
    case endpoint::WriteSemantics::Reconfigure:
    default:
        return endpoint::make_error_code(endpoint::EndpointErrc::non_combinable_write_semantics);
    }
}

// apply_write is PWM_OUT's own faithful port of c-RCP's
// rcp_ep_pwm_out_apply_write(): unlike apply_write_field above (which rejects
// Reserved/Reconfigure with an error, matching this codebase's own
// decode-time-rejection idiom, see rcp/gpio.hpp's apply_gpio_write), this
// function is INFALLIBLE, mirroring c-RCP's own pure-classifier shape
// exactly — Reserved leaves each field unchanged (the "ignored" half of
// Table 33's own two-part reserved-value rule; the "err-response" half is
// decode_write_request()'s own PwmErrc::reserved_evt below) and Reconfigure
// does too (fail-safe for a caller that violates the "never RECONFIG here"
// contract). After evt's own semantics are applied, active_duration (only —
// Table 46 names only "PWM active", not the whole period) is CAPPED into
// [duty_cycle_min, duty_cycle_max] (REQ-PWM-056), applied unconditionally
// including for the Reserved/Reconfigure "unchanged" cases — idempotent if
// current already satisfied the limits, self-correcting if the limits
// themselves changed since active_duration was last written. A caller
// passing duty_cycle_min > duty_cycle_max (not itself validated here) gets
// duty_cycle_min applied last and so wins, the same "later cap always wins"
// fail-safe a caller relying on either limit alone would see.
inline PwmValue apply_write(const PwmValue& current, const PwmValue& request, endpoint::WriteSemantics evt,
                             uint16_t duty_cycle_min, uint16_t duty_cycle_max) noexcept {
    PwmValue result;
    uint16_t period_out = current.period;
    uint16_t active_out = current.active_duration;
    if (evt != endpoint::WriteSemantics::Reserved && evt != endpoint::WriteSemantics::Reconfigure) {
        // apply_write_field only ever fails for Reserved/Reconfigure, both
        // excluded here, so its error is unreachable and safely ignored.
        (void)apply_write_field(evt, current.period, request.period, period_out);
        (void)apply_write_field(evt, current.active_duration, request.active_duration, active_out);
    }
    result.period          = period_out;
    result.active_duration = active_out;

    if (result.active_duration < duty_cycle_min) result.active_duration = duty_cycle_min;
    if (result.active_duration > duty_cycle_max) result.active_duration = duty_cycle_max;
    return result;
}

// ── PWM_OUT: signal-generation state (REQ-PWM-057/068/069) ───────────────────
// A pure classifier of the endpoint's own {period, active_duration} pair —
// §13.7.5.3: a request with PWM_Period == 0 stops generation; PWM_active ==
// 0 with PWM_Period > 0 leaves the PWM active but the output disabled
// (triggers still fire); otherwise ordinary generation.
enum class PwmOutGenerationState : uint8_t {
    Stopped         = 0, // period == 0
    OutputDisabled  = 1, // active_duration == 0, period != 0
    Running         = 2, // period != 0 and active_duration != 0
};

inline PwmOutGenerationState generation_state(PwmValue value) noexcept {
    if (value.period == 0) return PwmOutGenerationState::Stopped;
    if (value.active_duration == 0) return PwmOutGenerationState::OutputDisabled;
    return PwmOutGenerationState::Running;
}

// ── PWM_OUT: triggers ─────────────────────────────────────────────────────────
// Table 45 ("pwmo trigger outputs") names three fixed hardware trigger
// signals (exec-done, cycle-start, mid-pulse) with no register field
// selecting among them (c-RCP-AUDIT-06, issue #256 Group C) — the
// mutually-exclusive `PwmOutTrigger` field (plus a None/off state Table 45
// doesn't define) is this implementation's own simplification, matching
// c-RCP's own rcp_ep_pwm_out_trigger_t.

enum class PwmOutTrigger : uint8_t { None = 0, CycleStart = 1, MidPulse = 2, Done = 3 };
enum class PwmOutEvent : uint8_t { CycleStart = 0, MidPulse = 1, Done = 2 };

inline bool trigger_fires(PwmOutTrigger trigger, PwmOutEvent event) noexcept {
    switch (trigger) {
    case PwmOutTrigger::CycleStart: return event == PwmOutEvent::CycleStart;
    case PwmOutTrigger::MidPulse:   return event == PwmOutEvent::MidPulse;
    case PwmOutTrigger::Done:       return event == PwmOutEvent::Done;
    case PwmOutTrigger::None:
    default:                        return false;
    }
}

constexpr uint8_t kPwmOutTriggerEventCycleStart = 0x01;
constexpr uint8_t kPwmOutTriggerEventMidPulse   = 0x02;

// trigger_events_at_tick derives WHICH of CYCLE_START/MID_PULSE actually
// occur at a given elapsed clock-source tick (REQ-PWM-055/067; "PWM-055"):
//
//  (1) "For trigger signal generation the delayed signal is used" — `skew`
//      delays the primary edge by `skew` clock ticks (break-before-make
//      half/full-bridge support); trigger timing tracks that DELAYED edge,
//      not the undelayed source edge raw_tick is measured from.
//  (2) "in the middle of the active pulse (even in case duty cycle is 0%)"
//      — MID_PULSE is evaluated unconditionally at active_duration/2 ticks
//      past the delayed cycle start, including active_duration == 0, where
//      this naturally coincides with CYCLE_START (both fire together)
//      rather than being suppressed.
//
// raw_tick is the elapsed tick count since the UNDELAYED cycle's own rising
// edge, 0-based; a caller already tracking phase passes raw_tick % period
// itself. period == 0 (Stopped) yields 0 unconditionally — a stopped
// generator has no cycle to derive a phase within. Table 45's own event 0
// ("PWM request exec done") is deliberately NOT derived here — nothing ties
// it to cycle timing; it stays the one-shot, caller-driven signal
// trigger_fires() already models via PwmOutEvent::Done. A caller composes
// this function's output with trigger_fires(cfg.trigger, event) per set bit.
inline uint8_t trigger_events_at_tick(uint16_t period, uint16_t active_duration, uint8_t skew,
                                       uint32_t raw_tick) noexcept {
    if (period == 0) return 0;

    const uint32_t skew_mod     = static_cast<uint32_t>(skew) % period;
    const uint32_t delayed_tick = (raw_tick % period + period - skew_mod) % period;

    uint8_t events = 0;
    if (delayed_tick == 0) events |= kPwmOutTriggerEventCycleStart;
    if (delayed_tick == static_cast<uint32_t>(active_duration / 2)) events |= kPwmOutTriggerEventMidPulse;
    return events;
}

// ── PWM_OUT: functional config (Table 46) ─────────────────────────────────────
// Flattens regmap.h's shared functional-config "common" prefix directly into
// this struct's own bools, matching rcp/adc.hpp's own AdcFunctionalConfig
// rationale (cpp-RCP's rcp/regmap.hpp leaves EndpointFunctionalConfig::data
// opaque; render_registers()/apply_reconfig() below are this endpoint type's
// own full interpretation of that blob).
//
// NOTE (matches c-RCP's own zero-init exactly): a freshly default-constructed
// config's duty_cycle_min/duty_cycle_max are BOTH 0 — apply_write() above
// therefore caps every active_duration to 0 until a caller explicitly widens
// duty_cycle_max via Table 46's own registers (or set_duty_cycle_limits()
// below). This looks surprising for a "just try a plain Replace write" test,
// but is c-RCP's own literal zero-initialized register default, ported
// faithfully rather than substituted with a more convenient invented
// default.
struct PwmOutFunctionalConfig {
    bool ep_enable             = false;
    bool ep_clear_req_storage  = false;
    bool ep_req_crc_enable     = false;
    bool ep_response_ts_enable = false;
    bool ep_suppress_response  = false;

    PwmOutTrigger trigger         = PwmOutTrigger::None; // this module's own field, not part of the EP_func block
    uint16_t      base_clk        = 0; // 0x0004, R
    uint16_t      ep_status       = 0; // 0x0006, R/W
    uint8_t       clk_divider     = 0; // 0x0008, R/W
    uint8_t       signal_flags    = 0; // 0x0009, R/W — see kPwmOutFlag*
    uint16_t      duty_cycle_min  = 0; // 0x000A, R/W
    uint16_t      duty_cycle_max  = 0; // 0x000C, R/W
    uint8_t       skew            = 0; // 0x000E, R/W
};

constexpr uint8_t kPwmOutFlagInvPolarity   = 1u << 0;
constexpr uint8_t kPwmOutFlagIdleState     = 1u << 1;
constexpr uint8_t kPwmOutFlagIdleStateInv  = 1u << 2;

inline bool functional_cfg_writable(lifecycle::ServerState state, lifecycle::WriterCtx writer) noexcept {
    return lifecycle::field_writable(state, lifecycle::FieldKind::FunctionalW, writer);
}

inline bool set_trigger(PwmOutFunctionalConfig& cfg, PwmOutTrigger trigger, lifecycle::ServerState state,
                         lifecycle::WriterCtx writer) noexcept {
    if (!functional_cfg_writable(state, writer)) return false;
    cfg.trigger = trigger;
    return true;
}

inline bool set_enabled(PwmOutFunctionalConfig& cfg, bool enabled, lifecycle::ServerState state,
                         lifecycle::WriterCtx writer) noexcept {
    if (!functional_cfg_writable(state, writer)) return false;
    cfg.ep_enable = enabled;
    return true;
}

// ── PWM_OUT: the EP_func register block (evt[2:0] == 111b) ───────────────────

constexpr uint16_t kPwmOutRegEpLen        = 0x0000; //  8 bit, R
constexpr uint16_t kPwmOutRegReserved01   = 0x0001; //  8 bit, R
constexpr uint16_t kPwmOutRegEpEnableClr  = 0x0002; //  8 bit, R/W
constexpr uint16_t kPwmOutRegEpOptions    = 0x0003; //  8 bit, R/W
constexpr uint16_t kPwmOutRegBaseClk      = 0x0004; // 16 bit, R
constexpr uint16_t kPwmOutRegEpStatus     = 0x0006; // 16 bit, R/W
constexpr uint16_t kPwmOutRegClkDivider   = 0x0008; //  8 bit, R/W
constexpr uint16_t kPwmOutRegSignalFlags  = 0x0009; //  8 bit, R/W
constexpr uint16_t kPwmOutRegDutyCycleMin = 0x000A; // 16 bit, R/W
constexpr uint16_t kPwmOutRegDutyCycleMax = 0x000C; // 16 bit, R/W
constexpr uint16_t kPwmOutRegSkew         = 0x000E; //  8 bit, R/W

constexpr uint16_t kPwmOutEpFuncLen       = 0x000F;
constexpr size_t   kPwmOutReconfigAddrLen = 2;

using PwmOutRegisterBlock = std::array<uint8_t, kPwmOutEpFuncLen>;

namespace detail {
constexpr uint8_t kPwmEnableClrBitEnable = 1u << 0;
constexpr uint8_t kPwmEnableClrBitClear  = 1u << 4;
constexpr uint8_t kPwmOptionsBitReqCrc   = 1u << 0;
constexpr uint8_t kPwmOptionsBitRespTs   = 1u << 3;
constexpr uint8_t kPwmOptionsBitSuppress = 1u << 7;

inline bool pwm_out_reg_offset_read_only(uint16_t addr) noexcept {
    return addr == kPwmOutRegEpLen || addr == kPwmOutRegReserved01 || addr == kPwmOutRegBaseClk ||
           addr == static_cast<uint16_t>(kPwmOutRegBaseClk + 1);
}
} // namespace detail

inline void render_registers(const PwmOutFunctionalConfig& cfg, PwmOutRegisterBlock& out) noexcept {
    uint8_t enable_clr = 0;
    uint8_t options    = 0;
    if (cfg.ep_enable) enable_clr |= detail::kPwmEnableClrBitEnable;
    if (cfg.ep_clear_req_storage) enable_clr |= detail::kPwmEnableClrBitClear;
    if (cfg.ep_req_crc_enable) options |= detail::kPwmOptionsBitReqCrc;
    if (cfg.ep_response_ts_enable) options |= detail::kPwmOptionsBitRespTs;
    if (cfg.ep_suppress_response) options |= detail::kPwmOptionsBitSuppress;

    out[kPwmOutRegEpLen]       = static_cast<uint8_t>(kPwmOutEpFuncLen);
    out[kPwmOutRegReserved01]  = 0;
    out[kPwmOutRegEpEnableClr] = enable_clr;
    out[kPwmOutRegEpOptions]   = options;
    avtp::detail::put_u16(&out[kPwmOutRegBaseClk], cfg.base_clk);
    avtp::detail::put_u16(&out[kPwmOutRegEpStatus], cfg.ep_status);
    out[kPwmOutRegClkDivider]  = cfg.clk_divider;
    out[kPwmOutRegSignalFlags] = cfg.signal_flags;
    avtp::detail::put_u16(&out[kPwmOutRegDutyCycleMin], cfg.duty_cycle_min);
    avtp::detail::put_u16(&out[kPwmOutRegDutyCycleMax], cfg.duty_cycle_max);
    out[kPwmOutRegSkew] = cfg.skew;
}

namespace detail {
inline void pwm_out_parse_registers(PwmOutFunctionalConfig& cfg, const PwmOutRegisterBlock& in) noexcept {
    const uint8_t enable_clr = in[kPwmOutRegEpEnableClr];
    const uint8_t options    = in[kPwmOutRegEpOptions];

    cfg.ep_enable             = (enable_clr & kPwmEnableClrBitEnable) != 0;
    cfg.ep_clear_req_storage  = (enable_clr & kPwmEnableClrBitClear) != 0;
    cfg.ep_req_crc_enable     = (options & kPwmOptionsBitReqCrc) != 0;
    cfg.ep_response_ts_enable = (options & kPwmOptionsBitRespTs) != 0;
    cfg.ep_suppress_response  = (options & kPwmOptionsBitSuppress) != 0;

    cfg.ep_status      = avtp::detail::get_u16(&in[kPwmOutRegEpStatus]);
    cfg.clk_divider    = in[kPwmOutRegClkDivider];
    cfg.signal_flags   = in[kPwmOutRegSignalFlags];
    cfg.duty_cycle_min = avtp::detail::get_u16(&in[kPwmOutRegDutyCycleMin]);
    cfg.duty_cycle_max = avtp::detail::get_u16(&in[kPwmOutRegDutyCycleMax]);
    cfg.skew           = in[kPwmOutRegSkew];
}
} // namespace detail

inline std::error_code apply_reconfig(PwmOutFunctionalConfig& cfg, const uint8_t* payload,
                                       size_t payload_len) {
    if (payload_len <= kPwmOutReconfigAddrLen) return make_error_code(PwmErrc::reconfig_short);

    const uint16_t start_address = avtp::detail::get_u16(payload);
    const size_t   data_len      = payload_len - kPwmOutReconfigAddrLen;

    if (static_cast<size_t>(start_address) + data_len > static_cast<size_t>(kPwmOutEpFuncLen))
        return make_error_code(PwmErrc::reconfig_out_of_range);

    PwmOutRegisterBlock block{};
    render_registers(cfg, block);
    for (size_t i = 0; i < data_len; ++i) {
        const auto addr = static_cast<uint16_t>(start_address + i);
        if (detail::pwm_out_reg_offset_read_only(addr)) continue;
        block[addr] = payload[kPwmOutReconfigAddrLen + i];
    }
    detail::pwm_out_parse_registers(cfg, block);
    return {};
}

inline std::vector<uint8_t> encode_reconfig_request(avtp::ByteBusId byte_bus_id, uint16_t start_address,
                                                      const std::vector<uint8_t>& data,
                                                      uint8_t transaction_num) {
    if (data.empty()) return {};
    if (kPwmOutReconfigAddrLen + data.size() > acf::kAcfAbbMaxPayload) return {};

    std::vector<uint8_t> payload(kPwmOutReconfigAddrLen + data.size());
    avtp::detail::put_u16(payload.data(), start_address);
    std::copy(data.begin(), data.end(), payload.begin() + static_cast<long>(kPwmOutReconfigAddrLen));

    acf::AcfMessageInfo info;
    info.byte_bus_id     = byte_bus_id;
    info.op               = true; // write
    info.evt_op            = static_cast<uint8_t>(endpoint::WriteSemantics::Reconfigure);
    info.transaction_num   = transaction_num;
    return acf::encode_acf_abb(info, payload);
}

// ── PWM_OUT: wire codec ───────────────────────────────────────────────────────
// Ported directly from c-RCP's ep_pwm.c PWM_OUT wire functions, using
// rcp/acf.hpp's ACF_ABB/ACF_GBB codec, matching rcp/gpio.hpp's own pattern.

inline std::vector<uint8_t> encode_read_request(avtp::ByteBusId byte_bus_id, uint8_t transaction_num) {
    acf::AcfMessageInfo info;
    info.byte_bus_id     = byte_bus_id;
    info.op                = false; // read
    info.transaction_num    = transaction_num;
    return acf::encode_acf_abb(info, {});
}

inline std::error_code decode_read_request(const uint8_t* b, size_t len, avtp::ByteBusId expected_bus_id,
                                            uint8_t& out_transaction_num) {
    acf::AcfMessageInfo   info;
    std::vector<uint8_t>   payload;
    const auto              ec = acf::decode_acf_abb(b, len, info, payload);
    if (ec == avtp::make_error_code(avtp::AvtpErrc::short_buffer)) return make_error_code(PwmErrc::short_frame);
    if (ec) return make_error_code(PwmErrc::bad_msg_type);

    if (info.byte_bus_id != expected_bus_id) return make_error_code(PwmErrc::wrong_bus);
    if (info.op) return make_error_code(PwmErrc::wrong_op);

    out_transaction_num = info.transaction_num;
    return {};
}

// encode_write_request encodes an ACF_ABB write request: evt's low three
// bits carry evt, and the payload is value as kPwmPayloadLen big-endian
// octets (period then active_duration).
inline std::vector<uint8_t> encode_write_request(avtp::ByteBusId byte_bus_id, PwmValue value,
                                                   endpoint::WriteSemantics evt, uint8_t transaction_num) {
    acf::AcfMessageInfo info;
    info.byte_bus_id     = byte_bus_id;
    info.op                = true; // write
    info.evt_op             = static_cast<uint8_t>(static_cast<uint8_t>(evt) & 0x7);
    info.transaction_num    = transaction_num;
    const auto wire = encode_pwm_payload(value);
    return acf::encode_acf_abb(info, std::vector<uint8_t>(wire.begin(), wire.end()));
}

// decode_write_request: REQ-PWM-008 — evt[2:0] == 100b (Reserved) is
// rejected with PwmErrc::reserved_evt (Table 33's GPIO/PWM_OUT row) — none
// of out_value/out_evt/out_transaction_num are populated in that case.
inline std::error_code decode_write_request(const uint8_t* b, size_t len, avtp::ByteBusId expected_bus_id,
                                             PwmValue& out_value, endpoint::WriteSemantics& out_evt,
                                             uint8_t& out_transaction_num) {
    acf::AcfMessageInfo   info;
    std::vector<uint8_t>   payload;
    const auto              ec = acf::decode_acf_abb(b, len, info, payload);
    if (ec == avtp::make_error_code(avtp::AvtpErrc::short_buffer)) return make_error_code(PwmErrc::short_frame);
    if (ec) return make_error_code(PwmErrc::bad_msg_type);

    if (info.byte_bus_id != expected_bus_id) return make_error_code(PwmErrc::wrong_bus);
    if (!info.op) return make_error_code(PwmErrc::wrong_op);
    if (payload.size() != kPwmPayloadLen) return make_error_code(PwmErrc::bad_payload_len);

    const auto evt = endpoint::write_semantics_of(info.evt_op);
    if (evt == endpoint::WriteSemantics::Reserved) return make_error_code(PwmErrc::reserved_evt);

    PwmValue value{};
    (void)decode_pwm_payload(payload.data(), payload.size(), value);
    out_value            = value;
    out_evt               = evt;
    out_transaction_num    = info.transaction_num;
    return {};
}

// encode_response/decode_response answer either a read or a write request,
// same ACF_ABB/ACF_GBB timed/untimed choice as rcp/gpio.hpp's own pair.
inline std::vector<uint8_t> encode_response(avtp::ByteBusId byte_bus_id, PwmValue value,
                                             uint8_t transaction_num, bool timed, uint64_t timestamp) {
    const auto wire = encode_pwm_payload(value);
    const std::vector<uint8_t> payload(wire.begin(), wire.end());

    acf::AcfMessageInfo info;
    info.byte_bus_id     = byte_bus_id;
    info.op                = false; // read
    info.rsp                 = true;
    info.transaction_num      = transaction_num;

    if (timed) {
        info.mtv = true;
        return acf::encode_acf_gbb(info, timestamp, payload);
    }
    return acf::encode_acf_abb(info, payload);
}

inline std::error_code decode_response(const uint8_t* b, size_t len, avtp::ByteBusId expected_bus_id,
                                        PwmValue& out_value, bool& out_timed, uint64_t& out_timestamp,
                                        uint8_t& out_transaction_num) {
    uint8_t msg_type = 0;
    if (acf::peek_msg_type(b, len, msg_type)) return make_error_code(PwmErrc::short_frame);

    acf::AcfMessageInfo  info;
    std::vector<uint8_t>  payload;
    avtp::ByteBusId        bus_id = 0;
    uint8_t                 txn    = 0;

    if (msg_type == acf::kAcfMsgTypeGbb) {
        uint64_t ts = 0;
        const auto ec = acf::decode_acf_gbb(b, len, info, ts, payload);
        if (ec == avtp::make_error_code(avtp::AvtpErrc::short_buffer)) return make_error_code(PwmErrc::short_frame);
        if (ec) return make_error_code(PwmErrc::bad_msg_type);
        bus_id        = info.byte_bus_id;
        txn           = info.transaction_num;
        out_timed     = info.mtv;
        out_timestamp = out_timed ? ts : 0;
    } else {
        const auto ec = acf::decode_acf_abb(b, len, info, payload);
        if (ec == avtp::make_error_code(avtp::AvtpErrc::short_buffer)) return make_error_code(PwmErrc::short_frame);
        if (ec) return make_error_code(PwmErrc::bad_msg_type);
        bus_id        = info.byte_bus_id;
        txn           = info.transaction_num;
        out_timed     = false;
        out_timestamp = 0;
    }

    if (bus_id != expected_bus_id) return make_error_code(PwmErrc::wrong_bus);
    if (payload.size() != kPwmPayloadLen) return make_error_code(PwmErrc::bad_payload_len);

    PwmValue value{};
    (void)decode_pwm_payload(payload.data(), payload.size(), value);
    out_value           = value;
    out_transaction_num = txn;
    return {};
}

// ── PwmOutEndpoint (ep_type 0x07) ────────────────────────────────────────────
// handle_write applies every write semantics Table 30/33's GPIO/PWM_OUT row
// defines — Replace/Or/And/Xor/Add/Subtract — via apply_write_field above,
// independently against `period` and `active_duration`, then caps
// active_duration into [cfg().duty_cycle_min, cfg().duty_cycle_max]
// (REQ-PWM-056, see PwmOutFunctionalConfig's own doc comment about its
// zero-initialized default). Reserved is rejected without mutating state;
// Reconfigure is rejected too — this endpoint's own EP_func register write
// is apply_reconfig() above, a separate entry point from handle_write, same
// split as rcp/gpio.hpp's apply_gpio_write/apply_reconfig.
class PwmOutEndpoint {
public:
    std::error_code handle_write(endpoint::WriteSemantics op, PwmValue operand,
                                  PwmValue& out_value) noexcept {
        uint16_t new_period = 0;
        auto ec = apply_write_field(op, state_.period, operand.period, new_period);
        if (ec) return ec;

        uint16_t new_active = 0;
        ec = apply_write_field(op, state_.active_duration, operand.active_duration, new_active);
        if (ec) return ec;

        state_.period          = new_period;
        state_.active_duration = new_active;

        if (state_.active_duration < cfg_.duty_cycle_min) state_.active_duration = cfg_.duty_cycle_min;
        if (state_.active_duration > cfg_.duty_cycle_max) state_.active_duration = cfg_.duty_cycle_max;

        out_value = state_;
        return {};
    }

    const PwmValue& read() const noexcept { return state_; }
    PwmOutGenerationState generation() const noexcept { return generation_state(state_); }

    PwmOutFunctionalConfig&       functional_cfg() noexcept { return cfg_; }
    const PwmOutFunctionalConfig& functional_cfg() const noexcept { return cfg_; }

private:
    PwmValue                state_;
    PwmOutFunctionalConfig   cfg_;
};

// ── PwmInEndpoint (ep_type 0x08) ─────────────────────────────────────────────
// Response-only read model (extraction §13.7.6): PWM_IN has no data write
// request shape. Trigger signals fixed to match Table 47 ("pwmi trigger
// outputs") — rising edge (0) and falling edge (1) of the measured PWM_IN
// signal — the two independent, always-on hardware trigger signals (issue
// cpp-RCP-A4-pwmin). record_edge fires whichever one edge actually occurred;
// record_measurement models one full input-capture cycle completing (both a
// rising and falling edge) by recording the new value once and firing both.
enum class PwmInSignal : uint8_t { RisingEdge = 0, FallingEdge = 1 };

constexpr endpoint::TriggerRegistry::SignalId pwm_in_signal_id(PwmInSignal sig) noexcept {
    return static_cast<endpoint::TriggerRegistry::SignalId>(sig);
}

// PwmInTrigger/trigger_fires (REQ-PWM-032..034): Table 47's own two trigger
// signals, modeled additionally as a single, mutually-exclusive,
// client-selected mode (plus a None/off state Table 47 doesn't define) —
// this implementation's own simplification, matching c-RCP's own
// rcp_ep_pwm_in_trigger_t (see the file header's own delta-list item 7).
// Independent of, and does not replace, the always-on TriggerRegistry pair
// above (record_edge/record_measurement) — a caller may use either or both.
enum class PwmInTrigger : uint8_t { None = 0, Rising = 1, Falling = 2 };

inline bool trigger_fires(PwmInTrigger trigger, bool prev_level, bool new_level) noexcept {
    switch (trigger) {
    case PwmInTrigger::Rising:  return !prev_level && new_level;
    case PwmInTrigger::Falling: return prev_level && !new_level;
    case PwmInTrigger::None:
    default:                    return false;
    }
}

// ── PWM_IN: functional config (Table 48) ──────────────────────────────────────

struct PwmInFunctionalConfig {
    bool ep_enable             = false;
    bool ep_clear_req_storage  = false;
    bool ep_req_crc_enable     = false;
    bool ep_response_ts_enable = false;
    bool ep_suppress_response  = false;

    PwmInTrigger trigger    = PwmInTrigger::None; // this module's own field, not part of the EP_func block
    uint16_t     base_clk   = 0; // 0x0004, R
    uint16_t     ep_status  = 0; // 0x0006, R/W
    uint8_t      clk_divider = 0; // 0x0008, R/W
    uint8_t      flags      = 0; // 0x0009, R/W — see kPwmInFlag*
    uint16_t     max_period = 0; // 0x000A, R/W
};

constexpr uint8_t kPwmInFlagPolarity        = 1u << 0;
constexpr uint8_t kPwmInFlagErrOnMaxPeriod  = 1u << 1;
constexpr uint8_t kPwmInFlagContinuousMode  = 1u << 2;

inline bool pwm_in_functional_cfg_writable(lifecycle::ServerState state, lifecycle::WriterCtx writer) noexcept {
    return lifecycle::field_writable(state, lifecycle::FieldKind::FunctionalW, writer);
}

inline bool set_trigger(PwmInFunctionalConfig& cfg, PwmInTrigger trigger, lifecycle::ServerState state,
                         lifecycle::WriterCtx writer) noexcept {
    if (!pwm_in_functional_cfg_writable(state, writer)) return false;
    cfg.trigger = trigger;
    return true;
}

// ── PWM_IN: the EP_func register block (evt[2:0] == 111b), Table 48 ──────────
// FIXED (this pass, mirroring c-RCP's own issue #256 Group I fix): entirely
// missing before this pass — see the file header's own delta-list item 4.

constexpr uint16_t kPwmInRegEpLen       = 0x0000; //  8 bit, R
constexpr uint16_t kPwmInRegReserved01  = 0x0001; //  8 bit, R
constexpr uint16_t kPwmInRegEpEnableClr = 0x0002; //  8 bit, R/W
constexpr uint16_t kPwmInRegEpOptions   = 0x0003; //  8 bit, R/W
constexpr uint16_t kPwmInRegBaseClk     = 0x0004; // 16 bit, R
constexpr uint16_t kPwmInRegEpStatus    = 0x0006; // 16 bit, R/W
constexpr uint16_t kPwmInRegClkDivider  = 0x0008; //  8 bit, R/W
constexpr uint16_t kPwmInRegFlags       = 0x0009; //  8 bit, R/W
constexpr uint16_t kPwmInRegMaxPeriod   = 0x000A; // 16 bit, R/W

constexpr uint16_t kPwmInEpFuncLen       = 0x000C;
constexpr size_t   kPwmInReconfigAddrLen = 2;

using PwmInRegisterBlock = std::array<uint8_t, kPwmInEpFuncLen>;

namespace detail {
inline bool pwm_in_reg_offset_read_only(uint16_t addr) noexcept {
    return addr == kPwmInRegEpLen || addr == kPwmInRegReserved01 || addr == kPwmInRegBaseClk ||
           addr == static_cast<uint16_t>(kPwmInRegBaseClk + 1);
}
} // namespace detail

inline void render_registers(const PwmInFunctionalConfig& cfg, PwmInRegisterBlock& out) noexcept {
    uint8_t enable_clr = 0;
    uint8_t options    = 0;
    if (cfg.ep_enable) enable_clr |= detail::kPwmEnableClrBitEnable;
    if (cfg.ep_clear_req_storage) enable_clr |= detail::kPwmEnableClrBitClear;
    if (cfg.ep_req_crc_enable) options |= detail::kPwmOptionsBitReqCrc;
    if (cfg.ep_response_ts_enable) options |= detail::kPwmOptionsBitRespTs;
    if (cfg.ep_suppress_response) options |= detail::kPwmOptionsBitSuppress;

    out[kPwmInRegEpLen]       = static_cast<uint8_t>(kPwmInEpFuncLen);
    out[kPwmInRegReserved01]  = 0;
    out[kPwmInRegEpEnableClr] = enable_clr;
    out[kPwmInRegEpOptions]   = options;
    avtp::detail::put_u16(&out[kPwmInRegBaseClk], cfg.base_clk);
    avtp::detail::put_u16(&out[kPwmInRegEpStatus], cfg.ep_status);
    out[kPwmInRegClkDivider] = cfg.clk_divider;
    out[kPwmInRegFlags]      = cfg.flags;
    avtp::detail::put_u16(&out[kPwmInRegMaxPeriod], cfg.max_period);
}

namespace detail {
inline void pwm_in_parse_registers(PwmInFunctionalConfig& cfg, const PwmInRegisterBlock& in) noexcept {
    const uint8_t enable_clr = in[kPwmInRegEpEnableClr];
    const uint8_t options    = in[kPwmInRegEpOptions];

    cfg.ep_enable             = (enable_clr & kPwmEnableClrBitEnable) != 0;
    cfg.ep_clear_req_storage  = (enable_clr & kPwmEnableClrBitClear) != 0;
    cfg.ep_req_crc_enable     = (options & kPwmOptionsBitReqCrc) != 0;
    cfg.ep_response_ts_enable = (options & kPwmOptionsBitRespTs) != 0;
    cfg.ep_suppress_response  = (options & kPwmOptionsBitSuppress) != 0;

    cfg.ep_status    = avtp::detail::get_u16(&in[kPwmInRegEpStatus]);
    cfg.clk_divider  = in[kPwmInRegClkDivider];
    cfg.flags        = in[kPwmInRegFlags];
    cfg.max_period   = avtp::detail::get_u16(&in[kPwmInRegMaxPeriod]);
}
} // namespace detail

inline std::error_code apply_reconfig(PwmInFunctionalConfig& cfg, const uint8_t* payload, size_t payload_len) {
    if (payload_len <= kPwmInReconfigAddrLen) return make_error_code(PwmErrc::reconfig_short);

    const uint16_t start_address = avtp::detail::get_u16(payload);
    const size_t   data_len      = payload_len - kPwmInReconfigAddrLen;

    if (static_cast<size_t>(start_address) + data_len > static_cast<size_t>(kPwmInEpFuncLen))
        return make_error_code(PwmErrc::reconfig_out_of_range);

    PwmInRegisterBlock block{};
    render_registers(cfg, block);
    for (size_t i = 0; i < data_len; ++i) {
        const auto addr = static_cast<uint16_t>(start_address + i);
        if (detail::pwm_in_reg_offset_read_only(addr)) continue;
        block[addr] = payload[kPwmInReconfigAddrLen + i];
    }
    detail::pwm_in_parse_registers(cfg, block);
    return {};
}

inline std::vector<uint8_t> encode_pwm_in_reconfig_request(avtp::ByteBusId byte_bus_id,
                                                             uint16_t start_address,
                                                             const std::vector<uint8_t>& data,
                                                             uint8_t transaction_num) {
    if (data.empty()) return {};
    if (kPwmInReconfigAddrLen + data.size() > acf::kAcfAbbMaxPayload) return {};

    std::vector<uint8_t> payload(kPwmInReconfigAddrLen + data.size());
    avtp::detail::put_u16(payload.data(), start_address);
    std::copy(data.begin(), data.end(), payload.begin() + static_cast<long>(kPwmInReconfigAddrLen));

    acf::AcfMessageInfo info;
    info.byte_bus_id     = byte_bus_id;
    info.op                = true; // write: §12.7.1 Figure 18
    info.evt_op             = 0x7; // PWM_IN belongs to Table 33 Row 2's reserved-range group
                                    // (ADC/I2C/LIN/CAN/UART/ISELED/MDIO), not GPIO/PWM_OUT's own
                                    // eight-value write-semantics group, so the raw evt value is
                                    // used directly, matching rcp/adc.hpp's own encode_reconfig_request.
    info.transaction_num    = transaction_num;
    return acf::encode_acf_abb(info, payload);
}

// ── PWM_IN: wire codec (read request / response) ──────────────────────────────

inline std::vector<uint8_t> encode_pwm_in_read_request(avtp::ByteBusId byte_bus_id, uint8_t transaction_num) {
    acf::AcfMessageInfo info;
    info.byte_bus_id     = byte_bus_id;
    info.op                = false; // read
    info.transaction_num    = transaction_num;
    return acf::encode_acf_abb(info, {});
}

// decode_pwm_in_read_request: REQ-PWM-059 — evt[2:0] must be one of Table 33
// Row 2's plain-request values (endpoint::evt_row2_kind_of == Plain);
// PwmErrc::bad_evt otherwise.
inline std::error_code decode_pwm_in_read_request(const uint8_t* b, size_t len,
                                                    avtp::ByteBusId expected_bus_id,
                                                    uint8_t& out_transaction_num) {
    acf::AcfMessageInfo   info;
    std::vector<uint8_t>   payload;
    const auto              ec = acf::decode_acf_abb(b, len, info, payload);
    if (ec == avtp::make_error_code(avtp::AvtpErrc::short_buffer)) return make_error_code(PwmErrc::short_frame);
    if (ec) return make_error_code(PwmErrc::bad_msg_type);

    if (info.byte_bus_id != expected_bus_id) return make_error_code(PwmErrc::wrong_bus);
    if (info.op) return make_error_code(PwmErrc::wrong_op);
    if (endpoint::evt_row2_kind_of(info.evt_op) != endpoint::EvtRow2Kind::Plain)
        return make_error_code(PwmErrc::bad_evt);

    out_transaction_num = info.transaction_num;
    return {};
}

inline std::vector<uint8_t> encode_pwm_in_response(avtp::ByteBusId byte_bus_id, PwmValue value,
                                                     uint8_t transaction_num, bool timed, uint64_t timestamp) {
    return encode_response(byte_bus_id, value, transaction_num, timed, timestamp);
}

inline std::error_code decode_pwm_in_response(const uint8_t* b, size_t len, avtp::ByteBusId expected_bus_id,
                                               PwmValue& out_value, bool& out_timed, uint64_t& out_timestamp,
                                               uint8_t& out_transaction_num) {
    return decode_response(b, len, expected_bus_id, out_value, out_timed, out_timestamp, out_transaction_num);
}

// ── PWM_IN: MAX_PERIOD timeout classification (REQ-PWM-072..075) ─────────────
// This endpoint type's own analogue of rcp/gpio.hpp's debounce_sample()
// (REQ-GPIO-035) — a pure classifier a caller (owning the real timer this
// module does not) drives with one measured period. Table 48's own
// pwmi_err_on_max_period row:
//   0b: if MAX_PERIOD is exceeded, invalidate measurement and wait for a
//       new active phase of signal.
//   1b: if MAX_PERIOD is exceeded, stop measurement and signal error if
//       error response is enabled in EP_config (EP_RESP_ON_ERR).
// resp_on_err_enabled is EP_config's own EP_RESP_ON_ERR flag (a distinct
// register block from PwmInFunctionalConfig above), passed in already
// classified by the caller — this module never reaches into a register map
// to resolve it, matching this codebase's standing convention for every
// other caller-supplied classification flag.
enum class PwmInMaxPeriodOutcome : uint8_t {
    Ok            = 0, // measured_period <= max_period — no timeout
    Invalidate    = 1, // err_on_max_period == 0b
    Stop          = 2, // err_on_max_period == 1b, resp_on_err_enabled == false
    StopAndError  = 3, // err_on_max_period == 1b, resp_on_err_enabled == true
};

inline PwmInMaxPeriodOutcome max_period_outcome(uint16_t measured_period, uint16_t max_period,
                                                 bool err_on_max_period, bool resp_on_err_enabled) noexcept {
    if (measured_period <= max_period) return PwmInMaxPeriodOutcome::Ok;
    if (!err_on_max_period) return PwmInMaxPeriodOutcome::Invalidate;
    return resp_on_err_enabled ? PwmInMaxPeriodOutcome::StopAndError : PwmInMaxPeriodOutcome::Stop;
}

// pwm_in_wire_error maps outcome to its numbered wire error code —
// StopAndError is Table 48's own "stop measurement and signal error"
// outcome, mapping to the same acf::WireErrorCode::PwmInNoSignal (Table
// 27's own dedicated code) that PwmErrc::no_signal maps to above; every
// other outcome explicitly signals no error of its own.
inline std::optional<acf::WireErrorCode> pwm_in_wire_error(PwmInMaxPeriodOutcome outcome) noexcept {
    if (outcome == PwmInMaxPeriodOutcome::StopAndError) return acf::WireErrorCode::PwmInNoSignal;
    return std::nullopt;
}

// ── Compound-wait's numeric ≥/≤ comparison modes against PWM_IN ──────────────
// A future compound-wait request (generic compound-wait plumbing is a later
// milestone) that targets a PWM_IN endpoint compares one of its two captured
// sub-fields (period or active-duration, read as the "duty-cycle sub-field")
// against a caller-supplied threshold using one of four numeric comparison
// modes selected by evt[2:0] = 4..7 (100b..111b, §13.5.1) — a property of
// this endpoint type itself, not of the compound-wait mechanism, so it is
// implemented and unit-tested here, following the same "isolated precedent"
// rcp/spi.hpp's compound_wait_status_equal() establishes.
enum class PwmInCompoundWaitMode : uint8_t {
    PeriodGe = 4, // 100b
    PeriodLe = 5, // 101b
    DutyGe   = 6, // 110b
    DutyLe   = 7, // 111b
};

inline bool compound_wait_mode_valid(uint8_t v) noexcept {
    return v >= static_cast<uint8_t>(PwmInCompoundWaitMode::PeriodGe) &&
           v <= static_cast<uint8_t>(PwmInCompoundWaitMode::DutyLe);
}

// compound_wait_compare — see the file header's own delta-list item 2 for
// the polarity fix this function's own comment pins with a regression test:
// TC18 §13.5.1: evt[2:0]=100b/110b ("GE") is met when byte_msg_payload
// (threshold) is >= the current interface status (captured) — i.e.
// threshold >= captured, equivalently captured <= threshold. evt=101b/111b
// ("LE") is the mirror: threshold <= captured, i.e. captured >= threshold.
// Returns false (never an error) for an invalid mode, and equally false
// whenever the relevant captured sub-field itself equals kPwmInNoSignal — a
// "no signal" measurement never satisfies (or fails to satisfy) a numeric
// comparison, it is simply never a match.
inline bool compound_wait_compare(PwmValue captured, PwmInCompoundWaitMode mode, uint16_t threshold) noexcept {
    switch (mode) {
    case PwmInCompoundWaitMode::PeriodGe:
        if (captured.period == kPwmInNoSignal) return false;
        return captured.period <= threshold;
    case PwmInCompoundWaitMode::PeriodLe:
        if (captured.period == kPwmInNoSignal) return false;
        return captured.period >= threshold;
    case PwmInCompoundWaitMode::DutyGe:
        if (captured.active_duration == kPwmInNoSignal) return false;
        return captured.active_duration <= threshold;
    case PwmInCompoundWaitMode::DutyLe:
        if (captured.active_duration == kPwmInNoSignal) return false;
        return captured.active_duration >= threshold;
    default:
        return false;
    }
}

class PwmInEndpoint {
public:
    // record_edge fires exactly one of Table 47's two trigger signals for
    // an armed listener, without updating the last-measured value.
    void record_edge(PwmInSignal edge) noexcept {
        triggers_.notify(pwm_in_signal_id(edge));
    }

    // record_measurement records one completed input-capture cycle (a full
    // period, spanning one rising and one falling edge of the measured
    // signal) and fires both Table 47 trigger signals for any armed
    // listener.
    void record_measurement(PwmValue value) noexcept {
        last_value_ = value;
        has_signal_ = true;
        triggers_.notify(pwm_in_signal_id(PwmInSignal::RisingEdge));
        triggers_.notify(pwm_in_signal_id(PwmInSignal::FallingEdge));
    }

    // clear_signal models signal loss (e.g. the measured line goes idle
    // long enough that no pulse is being received) — a subsequent
    // handle_read reports PwmErrc::no_signal again until the next
    // record_measurement.
    void clear_signal() noexcept { has_signal_ = false; }

    // handle_read is PWM_IN's entire read model: return the last measured
    // value, or PwmErrc::no_signal (PWM_IN_NO_SIGNAL) if none has ever been
    // captured (or the signal was subsequently lost).
    std::error_code handle_read(PwmValue& out_value) const noexcept {
        if (!has_signal_) return make_error_code(PwmErrc::no_signal);
        out_value = last_value_;
        return {};
    }

    // handle_request is PWM_IN's request-decode entry point, classifying
    // evt[2:0] via endpoint::evt_row2_kind_of (Table 33 Row 2) before ever
    // touching last-measured state:
    //   - Plain (000b): delegates to handle_read().
    //   - Reserved (001b-110b): endpoint::EndpointErrc::reserved_evt_row2,
    //     `out_value` untouched.
    //   - ConfigWrite (111b): PwmErrc::config_write_not_supported.
    //     TODO(phase3-followup): this endpoint type now has a real Table 48
    //     EP_func register block (render_registers()/apply_reconfig() free
    //     functions above, and functional_cfg() below) — this entry point
    //     does not yet route a ConfigWrite request's raw payload into
    //     apply_reconfig() (handle_request's own signature carries no
    //     payload, only evt_op), matching this port's own scope boundary;
    //     a caller wanting real register writes today calls
    //     pwm::apply_reconfig(ep.functional_cfg(), payload, len) directly.
    std::error_code handle_request(uint8_t evt_op, PwmValue& out_value) {
        switch (endpoint::evt_row2_kind_of(evt_op)) {
        case endpoint::EvtRow2Kind::Plain:
            return handle_read(out_value);
        case endpoint::EvtRow2Kind::Reserved:
            return endpoint::make_error_code(endpoint::EndpointErrc::reserved_evt_row2);
        case endpoint::EvtRow2Kind::ConfigWrite:
            return make_error_code(PwmErrc::config_write_not_supported);
        }
        return endpoint::make_error_code(endpoint::EndpointErrc::reserved_evt_row2); // unreachable
    }

    endpoint::TriggerRegistry& triggers() noexcept { return triggers_; }

    PwmInFunctionalConfig&       functional_cfg() noexcept { return cfg_; }
    const PwmInFunctionalConfig& functional_cfg() const noexcept { return cfg_; }

private:
    PwmValue                   last_value_{};
    bool                        has_signal_ = false;
    endpoint::TriggerRegistry  triggers_;
    PwmInFunctionalConfig       cfg_;
};

} // namespace pwm
} // namespace rcp

// Enable std::error_code construction from rcp::pwm::PwmErrc.
namespace std {
template <>
struct is_error_code_enum<rcp::pwm::PwmErrc> : true_type {};
} // namespace std
