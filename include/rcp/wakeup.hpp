// fusa:req REQ-WAKEUP-001
// fusa:req REQ-WAKEUP-002
// fusa:req REQ-WAKEUP-003
// fusa:req REQ-WAKEUP-004
// fusa:req REQ-WAKEUP-005
// fusa:req REQ-WAKEUP-006
// fusa:req REQ-WAKEUP-007
// fusa:req REQ-WAKEUP-008
// fusa:req REQ-WAKEUP-009
// fusa:req REQ-WAKEUP-010
// fusa:req REQ-WAKEUP-011
// fusa:req REQ-WAKEUP-012
// fusa:req REQ-WAKEUP-013
// fusa:req REQ-WAKEUP-014
// fusa:req REQ-WAKEUP-015
// fusa:req REQ-WAKEUP-016
// fusa:req REQ-WAKEUP-017
// fusa:req REQ-WAKEUP-018
// fusa:req REQ-WAKEUP-019
// fusa:req REQ-WAKEUP-021
// fusa:req REQ-WAKEUP-022
// fusa:req REQ-WAKEUP-023
// fusa:req REQ-WAKEUP-024
// fusa:req REQ-WAKEUP-025
// fusa:req REQ-WAKEUP-027
// fusa:req REQ-WAKEUP-028
// fusa:req REQ-WAKEUP-029
// fusa:req REQ-WAKEUP-030
// fusa:req REQ-WAKEUP-031
// fusa:req REQ-WAKEUP-032
// fusa:req REQ-WAKEUP-033
// fusa:req REQ-WAKEUP-034
// fusa:req REQ-WAKEUP-035
// fusa:req REQ-WAKEUP-036
// fusa:req REQ-PWRMODE-023

// WakeUp endpoint (ep_type 0x01) — TC18's dedicated power-management
// endpoint: the fixed-opcode SleepCMD request/response, the fixed-opcode
// WakeUp message (plain and carrying its own wake source), configurable
// wake-source pin monitoring (level- and edge-triggered), the per-source
// wup_status latch, and the EP_func functional-configuration register block
// (extraction §13.7.2, §12.4.1, §12.5).
//
// ROADMAP.md Phase 17 / cpp-RCP issue #129, Phase 3 ("Per-endpoint
// modules"): this header is re-derived from c-RCP's ep_wakeup.h/ep_wakeup.c
// — c-RCP's RC5-conformant reference implementation for this endpoint type —
// rather than incrementally patched, per the roadmap's own module-by-module
// rewrite plan. This header's own pre-Phase-3 content modeled none of the
// above: a single fixed byte compared for equality (kSleepCmd == 0xA5, at
// least numerically consistent with c-RCP's own SleepCMD opcode), an
// asleep_ bool, and one flat 32-bit wake_source_pins_ mask with no
// per-source polarity/edge configuration, no wup_status latch, no WakeUp
// message of any kind, and no EP_func register block at all — none of
// c-RCP's real WakeUp API redesign history (REQ-WAKEUP-018/021/022) or its
// EP_func register-block addition (issue #256 Group I) was reflected. This
// pass replaces that content with a faithful port of c-RCP's *current*
// ep_wakeup.h/.c (post all of that history), not the earlier invented model.
//
// No text from the OPEN Alliance TC18 Remote Control Protocol Specification
// is reproduced here; field names and behavior below implement TC18's
// *behavior* as ported from c-RCP's own implementation of an internal
// structured extraction of the specification.
//
// ── Layering divergence from c-RCP (deliberate) ─────────────────────────────
// c-RCP's ep_wakeup.h depends on power.h for rcp_pwrmode_entry_result_t (the
// SleepCMD response's own payload) — the two modules are one directional
// dependency (wakeup -> power) there. cpp-RCP's existing rcp/powerstate.hpp
// (ROADMAP.md milestone 53, Phase 14, out of this pass's own scope) already
// established the OPPOSITE direction: it depends on rcp/wakeup.hpp's
// WakeupEndpoint (via wakeup_message_pending()/acknowledge_wakeup()), not
// the reverse — including from tests/test_powerstate.cpp, a real call site
// this pass must not break. Rather than invert cpp-RCP's own established
// layering (which would ripple into rcp/powerstate.hpp, out of this pass's
// scope) or duplicate rcp::powerstate::PowerMode's concept of an entry
// result here under a different name, this header defines its own minimal,
// local SleepCmdResult (Ok/Refused) for the SleepCMD response payload —
// this module's own original choice, not a port of c-RCP's
// rcp_pwrmode_entry_result_t, though numerically and behaviorally
// equivalent for this wire pair's own purposes. A caller gluing this header
// to rcp/powerstate.hpp's own PowerMode is expected to translate between
// the two at the call site, the same "primitive, not a scheduler" split
// every header in this codebase already follows for cross-module coupling.
//
// ── WakeupEndpoint's pre-Phase-3 API is preserved for rcp/powerstate.hpp ────
// rcp/powerstate.hpp's PowerManager (and its own tests,
// tests/test_powerstate.cpp) construct a WakeupEndpoint by default and call
// exactly two of its methods: wakeup_message_pending() and
// acknowledge_wakeup() (plus record_wake_source_event() from their own
// tests, to arm the handshake). Those three methods, and their exact
// pre-Phase-3 semantics (a wake-source event arms a pending flag;
// acknowledge_wakeup() clears it; entering Sleep via handle_sleep_cmd()
// clears any handshake left over from a prior cycle), are kept byte-for-byte
// unchanged below — WakeupEndpoint now additionally owns a real
// WakeupFunctionalConfig (sources/wup_status/ep_status), but
// wakeup_message_pending()'s own pending-flag bookkeeping remains the
// simple bool this codebase's PowerManager already depends on, rather than
// being re-derived from wup_status (whose write-1-to-clear register
// semantics do not map cleanly onto "still owed a WakeUp repetition" without
// changing PowerManager's own contract, out of this pass's scope).
//
// Field names and behavior below implement TC18's *behavior* as described in
// an internal structured extraction of the specification named above; no
// text from that document is reproduced here. The concrete wake-source pin
// count (kMaxWakeSources == 8, matching c-RCP's own
// RCP_EP_WAKEUP_MAX_SOURCES), the SleepCmdResult enum, and the exact split
// of responsibility across the methods below are this implementation's own
// encoding of that behavior, same as the equivalent disclaimers in
// rcp/avtp.hpp, rcp/regmap.hpp, rcp/endpoint.hpp, and rcp/request.hpp.
#pragma once

#include <rcp/acf.hpp>
#include <rcp/avtp.hpp>
#include <rcp/lifecycle.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace rcp {
namespace wakeup {

// ── SleepCMD / WakeUp fixed opcodes ───────────────────────────────────────────
// Both are fixed, single-byte request kinds — unlike every ordinary
// endpoint's own evt[2:0]-carrying request, neither is a member of
// rcp/request.hpp's conditional-request taxonomy. This header has no
// dependency on rcp/request.hpp.

constexpr uint8_t kSleepCmdOpcode = 0xA5; // REQ-WAKEUP-010, TC18 §13.7.2.3 Figure 23
constexpr uint8_t kWakeupOpcode   = 0x5A; // REQ-WAKEUP-014, this module's own marker
                                            // (TC18 defines no wire encoding for the
                                            // repetitive WakeUp message itself)

// ── Errors ────────────────────────────────────────────────────────────────────

enum class WakeupErrc : int {
    short_frame          = 1,
    bad_msg_type          = 2,
    wrong_bus              = 3,
    bad_opcode              = 4,
    // Configuration write (evt[2:0]==111b) payload carries no address
    // prefix, or an address prefix with no data octet after it.
    reconfig_short           = 5,
    // Configuration write's start_address + data length exceeds the EP_func
    // block's own length — the whole write is ignored, per the
    // specification's own rule.
    reconfig_out_of_range      = 6,
};

inline const std::error_category& wakeup_category() noexcept {
    struct Cat : std::error_category {
        const char* name() const noexcept override { return "rcp.wakeup"; }
        std::string message(int ev) const override {
            switch (static_cast<WakeupErrc>(ev)) {
            case WakeupErrc::short_frame:  return "rcp/wakeup: frame too short";
            case WakeupErrc::bad_msg_type: return "rcp/wakeup: unexpected ACF message type";
            case WakeupErrc::wrong_bus:    return "rcp/wakeup: wrong byte_bus_id";
            case WakeupErrc::bad_opcode:   return "rcp/wakeup: wrong fixed opcode byte";
            case WakeupErrc::reconfig_short:
                return "rcp/wakeup: configuration write has no address and data";
            case WakeupErrc::reconfig_out_of_range:
                return "rcp/wakeup: configuration write extends past the EP_func block";
            default: return "rcp/wakeup: unknown error";
            }
        }
    };
    static Cat instance;
    return instance;
}

inline std::error_code make_error_code(WakeupErrc e) noexcept {
    return {static_cast<int>(e), wakeup_category()};
}

// ── Wake-source pin configuration/monitoring (Table 39/40) ───────────────────

constexpr size_t kMaxWakeSources = 8; // matches c-RCP's own RCP_EP_WAKEUP_MAX_SOURCES

// One wake-source slot's own configuration (REQ-WAKEUP-022: LEVEL mode via
// enabled/active_high, or EDGE mode via either trigger_on_*_edge flag —
// either trigger bit set puts the slot in EDGE mode and active_high is then
// not consulted; both false, the zero-init default, means LEVEL mode).
struct WakeSourceCfg {
    bool     enabled              = false;
    bool     active_high          = false; // LEVEL mode only
    uint16_t pin_number            = 0;     // wup_io_scrN's own [10:0] wire field
    bool     trigger_on_rising_edge  = false; // EDGE mode
    bool     trigger_on_falling_edge = false; // EDGE mode
};

// REQ-WAKEUP-003: true iff cfg is enabled and pin_level matches cfg's own
// active_high polarity. LEVEL-mode only — deliberately unaffected by
// trigger_on_rising_edge/trigger_on_falling_edge; see
// source_edge_asserted() below for the EDGE-mode counterpart.
inline bool source_asserted(const WakeSourceCfg& cfg, bool pin_level) noexcept {
    return cfg.enabled && (pin_level == cfg.active_high);
}

// REQ-WAKEUP-004: true iff any of the first
// min(pin_levels.size(), kMaxWakeSources) entries of sources is currently
// asserted per source_asserted(). LEVEL-mode only.
inline bool any_source_asserted(const std::array<WakeSourceCfg, kMaxWakeSources>& sources,
                                 const std::vector<bool>& pin_levels) noexcept {
    const size_t n = std::min(pin_levels.size(), kMaxWakeSources);
    for (size_t i = 0; i < n; ++i) {
        if (source_asserted(sources[i], pin_levels[i])) return true;
    }
    return false;
}

// ── Edge-triggered wake-source detection (REQ-WAKEUP-022) ────────────────────
// One previous-pin-level slot per wake-source: edge detection compares the
// CURRENT pin level against the PREVIOUS one, state a pure per-call
// predicate like source_asserted() cannot carry itself — the same
// caller-owned "has_previous" idiom this codebase already establishes
// elsewhere (e.g. rcp/gpio.hpp's GpioDebounceState). The very first
// observation only seeds previous_level, never fires.
struct SourceEdgeState {
    bool has_previous   = false;
    bool previous_level = false;
};

// REQ-WAKEUP-032/033: the EDGE-aware counterpart to source_asserted() for a
// single source. If cfg is in LEVEL mode (both trigger flags false), state
// is left entirely untouched and this delegates to source_asserted(cfg,
// pin_level) — a single call site usable uniformly regardless of mode.
// Otherwise (EDGE mode): the very first call only seeds previous_level and
// returns false; every call after that returns true iff the level actually
// transitioned in a direction cfg's own trigger flags select (both true —
// "both edges" — fires on either transition), and state's previous_level is
// updated unconditionally (every call, whether or not it fires). A disabled
// EDGE-mode source still updates state but never fires.
inline bool source_edge_asserted(const WakeSourceCfg& cfg, SourceEdgeState& state, bool pin_level) noexcept {
    if (!cfg.trigger_on_rising_edge && !cfg.trigger_on_falling_edge) {
        return source_asserted(cfg, pin_level);
    }

    if (!state.has_previous) {
        state.has_previous   = true;
        state.previous_level = pin_level;
        return false;
    }

    const bool rose = !state.previous_level && pin_level;
    const bool fell = state.previous_level && !pin_level;
    const bool fired = cfg.enabled &&
                        ((cfg.trigger_on_rising_edge && rose) || (cfg.trigger_on_falling_edge && fell));

    state.previous_level = pin_level;
    return fired;
}

// REQ-WAKEUP-034: the EDGE-aware counterpart to any_source_asserted().
// Deliberately does NOT short-circuit — every in-range source's own state
// must be updated on every call (see source_edge_asserted()'s own "every
// call updates state" contract), not just sources scanned before the first
// hit, or a later transition on an unscanned source would be silently
// missed.
inline bool any_source_edge_asserted(const std::array<WakeSourceCfg, kMaxWakeSources>& sources,
                                      std::array<SourceEdgeState, kMaxWakeSources>& states,
                                      const std::vector<bool>& pin_levels) noexcept {
    const size_t n = std::min(pin_levels.size(), kMaxWakeSources);
    bool any = false;
    for (size_t i = 0; i < n; ++i) {
        if (source_edge_asserted(sources[i], states[i], pin_levels[i])) any = true;
    }
    return any;
}

// ── wup_status latch (Table 39) ───────────────────────────────────────────────
// REQ-WAKEUP-021: a genuine per-source bitmask ("each bit represents a
// wake-up source"), bit i corresponding to sources[i] — not a single
// aggregate "did anything wake the device" bool (this header's own
// pre-Phase-3 model, discarded by this pass — see the file header).
class WupStatus {
public:
    void latch_source(size_t source_index) noexcept {
        if (source_index >= kMaxWakeSources) return;
        mask_ = static_cast<uint16_t>(mask_ | (uint16_t{1} << source_index));
    }

    // REQ-WAKEUP-007: clears every latched bit at once — a caller drives
    // this from a client register write that writes 1 to every bit
    // position, or from a full functional-config reset.
    void clear() noexcept { mask_ = 0; }

    // REQ-WAKEUP-027: clears exactly source_index's own bit, leaving every
    // other source's own latch state untouched — TC18's own per-bit
    // write-1-to-clear rule, applied one bit at a time by
    // WakeupEndpoint::apply_reconfig() below.
    void clear_source(size_t source_index) noexcept {
        if (source_index >= kMaxWakeSources) return;
        mask_ = static_cast<uint16_t>(mask_ & ~(uint16_t{1} << source_index));
    }

    bool is_clear() const noexcept { return mask_ == 0; }

    // REQ-WAKEUP-028: true iff source_index's own bit is latched
    // specifically. source_index >= kMaxWakeSources always returns false.
    bool source_is_latched(size_t source_index) const noexcept {
        if (source_index >= kMaxWakeSources) return false;
        return (mask_ & (uint16_t{1} << source_index)) != 0;
    }

    uint16_t mask() const noexcept { return mask_; }

private:
    uint16_t mask_ = 0;
};

// ── Functional config ─────────────────────────────────────────────────────────
// Flattens regmap.h's shared functional-config "common" prefix directly into
// this struct's own bools, matching rcp/adc.hpp's/rcp/pwm.hpp's own
// convention. repetition_time_us (REQ-WAKEUP-018, TC18 §12.4.1) is
// discoverable/settable over this in-memory API but is NOT itself
// wire-reachable — Table 39/40 (this endpoint's own EP_func block, fully
// mapped below) define no field for it, matching c-RCP's own
// rcp_ep_wakeup_functional_cfg_t::repetition_time_us doc comment.
struct WakeupFunctionalConfig {
    bool ep_enable             = false;
    bool ep_clear_req_storage  = false;
    bool ep_req_crc_enable     = false;
    bool ep_response_ts_enable = false;
    bool ep_suppress_response  = false;

    std::array<WakeSourceCfg, kMaxWakeSources> sources{};
    uint16_t   ep_status            = 0; // wup_ep_status, 0x0002, R/W
    WupStatus  wup_status;               // wup_status, 0x0004, R/W
    uint32_t   repetition_time_us    = 0; // REQ-WAKEUP-018; not wire-reachable, see above
};

inline bool functional_cfg_writable(lifecycle::ServerState state, lifecycle::WriterCtx writer) noexcept {
    return lifecycle::field_writable(state, lifecycle::FieldKind::FunctionalW, writer);
}

// REQ-PWRMODE-023 (TC18 §12.5): "The RC Client that is allowed to access the
// RC Server endpoint can request the entire RC Server implementation to
// enter standby or sleep mode." This codebase's only concept of "an RC
// Client allowed to access [an] endpoint" is lifecycle::WriterCtx's own
// root-client/discovery-stream classification — true iff
// writer.via_root_client_ep0, mirroring lifecycle::ServerLifecycle's own
// RcpConfigured-state authorization rule for the same reason: once
// RcpConfigured (the only state a SleepCMD is meaningful in), only the root
// client, not an unqualified discovery-stream sender, may act on server-wide
// power state.
inline bool sleepcmd_writable(lifecycle::WriterCtx writer) noexcept {
    return writer.via_root_client_ep0;
}

// ── SleepCMD request/response (0xA5) ──────────────────────────────────────────

inline std::vector<uint8_t> encode_sleepcmd_request(avtp::ByteBusId byte_bus_id, uint8_t transaction_num) {
    acf::AcfMessageInfo info;
    info.byte_bus_id     = byte_bus_id;
    info.transaction_num = transaction_num;
    return acf::encode_acf_abb(info, {kSleepCmdOpcode});
}

inline std::error_code decode_sleepcmd_request(const uint8_t* b, size_t len, avtp::ByteBusId expected_bus_id,
                                                uint8_t& out_transaction_num) {
    acf::AcfMessageInfo   info;
    std::vector<uint8_t>   payload;
    const auto              ec = acf::decode_acf_abb(b, len, info, payload);
    if (ec == avtp::make_error_code(avtp::AvtpErrc::short_buffer)) return make_error_code(WakeupErrc::short_frame);
    if (ec) return make_error_code(WakeupErrc::bad_msg_type);

    if (info.byte_bus_id != expected_bus_id) return make_error_code(WakeupErrc::wrong_bus);
    if (payload.empty()) return make_error_code(WakeupErrc::short_frame);
    if (payload[0] != kSleepCmdOpcode) return make_error_code(WakeupErrc::bad_opcode);

    out_transaction_num = info.transaction_num;
    return {};
}

// This module's own minimal SleepCMD-response outcome — see the file
// header's own "Layering divergence from c-RCP" note for why this is not
// c-RCP's rcp_pwrmode_entry_result_t.
enum class SleepCmdResult : uint8_t { Ok = 0, Refused = 1 };

// REQ-WAKEUP-012/019: RCP_PWRMODE_ENTRY_OK-equivalent (Ok) encodes this
// module's own positive-form payload (opcode + result byte), echoing
// transaction_num. Refused instead returns a genuine ACF Error Response
// carrying acf::WireErrorCode::RequestCanceled (TC18 §12.5: "The RC Server
// will reject requests to enter sleep or standby mode and send an error
// message with error code = REQUEST_CANCELED"), not this module's own
// positive-form payload with a "refused" byte — a conformant peer watching
// for an error response sees the refusal.
inline std::vector<uint8_t> encode_sleepcmd_response(avtp::ByteBusId byte_bus_id, SleepCmdResult result,
                                                       uint8_t transaction_num) {
    if (result == SleepCmdResult::Refused) {
        return acf::build_error_response(byte_bus_id, transaction_num, acf::WireErrorCode::RequestCanceled);
    }

    acf::AcfMessageInfo info;
    info.byte_bus_id     = byte_bus_id;
    info.rsp               = true;
    info.transaction_num    = transaction_num;
    return acf::encode_acf_abb(info, {kSleepCmdOpcode, static_cast<uint8_t>(result)});
}

// REQ-WAKEUP-013/023/025: an Error Response (info.err set) is recognized as
// the refused-entry half of this pair — *out_result is Refused iff its
// payload carries WireErrorCode::RequestCanceled (the only code this
// function's own encode counterpart ever builds); any other err payload is
// WakeupErrc::bad_opcode, not silently reinterpreted. A non-error response
// decodes the fixed-opcode/short-frame/wrong-bus/bad-opcode failure modes
// same as decode_sleepcmd_request(); any second payload byte other than
// SleepCmdResult::Ok's own raw value (0) decodes as Refused (fail-safe: an
// unrecognized result byte is never treated as an admitted entry).
inline std::error_code decode_sleepcmd_response(const uint8_t* b, size_t len, avtp::ByteBusId expected_bus_id,
                                                 SleepCmdResult& out_result, uint8_t& out_transaction_num) {
    acf::AcfMessageInfo   info;
    std::vector<uint8_t>   payload;
    const auto              ec = acf::decode_acf_abb(b, len, info, payload);
    if (ec == avtp::make_error_code(avtp::AvtpErrc::short_buffer)) return make_error_code(WakeupErrc::short_frame);
    if (ec) return make_error_code(WakeupErrc::bad_msg_type);

    if (info.byte_bus_id != expected_bus_id) return make_error_code(WakeupErrc::wrong_bus);

    if (info.err) {
        if (payload.empty() || payload[0] != static_cast<uint8_t>(acf::WireErrorCode::RequestCanceled)) {
            return make_error_code(WakeupErrc::bad_opcode);
        }
        out_result           = SleepCmdResult::Refused;
        out_transaction_num  = info.transaction_num;
        return {};
    }

    if (payload.size() < 2) return make_error_code(WakeupErrc::short_frame);
    if (payload[0] != kSleepCmdOpcode) return make_error_code(WakeupErrc::bad_opcode);

    out_result = (payload[1] == static_cast<uint8_t>(SleepCmdResult::Ok)) ? SleepCmdResult::Ok
                                                                           : SleepCmdResult::Refused;
    out_transaction_num = info.transaction_num;
    return {};
}

// ── WakeUp-message emission (§12.4.1, §13.7.2.1) ──────────────────────────────

inline std::vector<uint8_t> encode_wakeup_message(avtp::ByteBusId byte_bus_id, uint8_t transaction_num) {
    acf::AcfMessageInfo info;
    info.byte_bus_id     = byte_bus_id;
    info.transaction_num = transaction_num;
    return acf::encode_acf_abb(info, {kWakeupOpcode});
}

inline std::error_code decode_wakeup_message(const uint8_t* b, size_t len, avtp::ByteBusId expected_bus_id,
                                              uint8_t& out_transaction_num) {
    acf::AcfMessageInfo   info;
    std::vector<uint8_t>   payload;
    const auto              ec = acf::decode_acf_abb(b, len, info, payload);
    if (ec == avtp::make_error_code(avtp::AvtpErrc::short_buffer)) return make_error_code(WakeupErrc::short_frame);
    if (ec) return make_error_code(WakeupErrc::bad_msg_type);

    if (info.byte_bus_id != expected_bus_id) return make_error_code(WakeupErrc::wrong_bus);
    if (payload.empty()) return make_error_code(WakeupErrc::short_frame);
    if (payload[0] != kWakeupOpcode) return make_error_code(WakeupErrc::bad_opcode);

    out_transaction_num = info.transaction_num;
    return {};
}

// REQ-WAKEUP-016: true iff b decodes as a valid WakeUp message addressed to
// expected_bus_id whose transaction number equals sent_transaction_num —
// this module's own "is this the echo of the WakeUp message I sent"
// predicate, meant to feed a hot-start-from-Sleep handshake's own `echoed`
// input. False for any decode failure or transaction-number mismatch.
inline bool is_wakeup_echo(const uint8_t* b, size_t len, avtp::ByteBusId expected_bus_id,
                            uint8_t sent_transaction_num) noexcept {
    uint8_t txn = 0;
    if (decode_wakeup_message(b, len, expected_bus_id, txn)) return false;
    return txn == sent_transaction_num;
}

// REQ-WAKEUP-017: three classes of wake source TC18 §12.4.1's own text
// names — "an internal EP signal" (a configured wake-source pin), "the
// dedicated wakepin" (named separately from the configured pin table), and
// "a TC14/TC10 wake-up request on the network". TC18 defines no wire
// encoding for this classification (same disclaimer as SleepCMD's own
// response payload above) — this enum and the 3-byte message shape below
// are this module's own original design, matching c-RCP's own
// rcp_ep_wakeup_source_t exactly.
enum class WakeupSource : uint8_t {
    Unknown = 0, // no wake-source information available/applicable
    Io      = 1, // a configured wake-source pin — see source_index
    Wakepin = 2, // TC18 §12.4.1's own "the dedicated wakepin"
    Network = 3, // TC18 §12.4.1's own "TC14/TC10 wake-up request on the network"
};

// source_index's own sentinel for "not applicable to this source
// classification" — every classification other than WakeupSource::Io
// carries this value.
constexpr uint8_t kWakeupSourceIndexNa = 0xFF;

// Encodes the same message as encode_wakeup_message(), but with 2 additional
// payload bytes: source and source_index. The plain decode_wakeup_message()/
// is_wakeup_echo() pair still decodes a message built by this function
// correctly (they only ever check payload.size() >= 1 and payload[0]) — this
// is a strictly additive wire extension.
inline std::vector<uint8_t> encode_wakeup_message_with_source(avtp::ByteBusId byte_bus_id,
                                                                uint8_t transaction_num, WakeupSource source,
                                                                uint8_t source_index) {
    acf::AcfMessageInfo info;
    info.byte_bus_id     = byte_bus_id;
    info.transaction_num = transaction_num;
    return acf::encode_acf_abb(info, {kWakeupOpcode, static_cast<uint8_t>(source), source_index});
}

// Decodes and validates the 3-byte shape encode_wakeup_message_with_source()
// builds. A message built by the plain encode_wakeup_message() (only 1
// payload byte) is REJECTED here with WakeupErrc::short_frame — this
// decoder's own contract is the 3-byte shape specifically. Fails with
// WakeupErrc::bad_opcode if the source byte is not one of this enum's own
// 4 defined values (fail-safe: never silently reinterpreted as Unknown).
inline std::error_code decode_wakeup_message_with_source(const uint8_t* b, size_t len,
                                                           avtp::ByteBusId expected_bus_id,
                                                           uint8_t& out_transaction_num,
                                                           WakeupSource& out_source, uint8_t& out_source_index) {
    acf::AcfMessageInfo   info;
    std::vector<uint8_t>   payload;
    const auto              ec = acf::decode_acf_abb(b, len, info, payload);
    if (ec == avtp::make_error_code(avtp::AvtpErrc::short_buffer)) return make_error_code(WakeupErrc::short_frame);
    if (ec) return make_error_code(WakeupErrc::bad_msg_type);

    if (info.byte_bus_id != expected_bus_id) return make_error_code(WakeupErrc::wrong_bus);
    if (payload.size() < 3) return make_error_code(WakeupErrc::short_frame);
    if (payload[0] != kWakeupOpcode) return make_error_code(WakeupErrc::bad_opcode);

    switch (payload[1]) {
    case static_cast<uint8_t>(WakeupSource::Unknown):
    case static_cast<uint8_t>(WakeupSource::Io):
    case static_cast<uint8_t>(WakeupSource::Wakepin):
    case static_cast<uint8_t>(WakeupSource::Network):
        break;
    default:
        return make_error_code(WakeupErrc::bad_opcode);
    }

    out_transaction_num = info.transaction_num;
    out_source           = static_cast<WakeupSource>(payload[1]);
    out_source_index      = payload[2];
    return {};
}

// ── The EP_func register block (evt[2:0] == 111b), Table 39/40 ──────────────
// FIXED/ADDED (this pass, mirroring c-RCP's own issue #256 Group I fix):
// entirely missing before this pass. TC18's own source table has a genuine
// address collision between wup_status and the wake-source array's own
// first entry, both printed at the same relative address — resolved here
// exactly as c-RCP resolves it: wup_status at its own dedicated slot,
// the wake-source array shifted to start immediately after it.

constexpr uint16_t kWakeupRegEpLen        = 0x0000; //  8 bit, R
constexpr uint16_t kWakeupRegNrIoPinsMax  = 0x0001; //  8 bit, R
constexpr uint16_t kWakeupRegEpStatus     = 0x0002; // 16 bit, R/W
constexpr uint16_t kWakeupRegWupStatus    = 0x0004; // 16 bit, R/W
constexpr uint16_t kWakeupRegSourceBase   = 0x0006; // wup_io_scrN, 2 octets each
constexpr uint16_t kWakeupRegSourceSpan   = 0x0002;

constexpr uint16_t kWakeupEpFuncLen =
    static_cast<uint16_t>(kWakeupRegSourceBase + static_cast<uint16_t>(kMaxWakeSources) * kWakeupRegSourceSpan);
constexpr size_t kWakeupReconfigAddrLen = 2;

using WakeupRegisterBlock = std::array<uint8_t, kWakeupEpFuncLen>;

// The 6 IO_SRC[15:11] values this module can represent (REQ-WAKEUP-022) —
// only the reserved range (0x06-0x1F) remains unrepresentable, correctly,
// since TC18 itself defines no meaning for it.
constexpr uint8_t kWakeupIoSrcInactive    = 0x00;
constexpr uint8_t kWakeupIoSrcRisingEdge  = 0x01;
constexpr uint8_t kWakeupIoSrcFallingEdge = 0x02;
constexpr uint8_t kWakeupIoSrcBothEdges   = 0x03;
constexpr uint8_t kWakeupIoSrcHighLevel   = 0x04;
constexpr uint8_t kWakeupIoSrcLowLevel    = 0x05;

namespace detail {
inline bool wakeup_reg_offset_read_only(uint16_t addr) noexcept {
    return addr == kWakeupRegEpLen || addr == kWakeupRegNrIoPinsMax;
}
} // namespace detail

// REQ-WAKEUP-021/022: renders wup_status as the full per-source bitmask
// (bits [15:kMaxWakeSources] always 0) and each source slot as one of
// kWakeupIoSrc*, derived from enabled/active_high/trigger_on_rising_edge/
// trigger_on_falling_edge — EDGE mode (either trigger bit set) takes
// precedence over the LEVEL-mode enabled/active_high pair.
inline void render_registers(const WakeupFunctionalConfig& cfg, WakeupRegisterBlock& out) noexcept {
    out[kWakeupRegEpLen]       = static_cast<uint8_t>(kWakeupEpFuncLen);
    out[kWakeupRegNrIoPinsMax] = static_cast<uint8_t>(kMaxWakeSources);
    avtp::detail::put_u16(&out[kWakeupRegEpStatus], cfg.ep_status);
    avtp::detail::put_u16(&out[kWakeupRegWupStatus],
                           static_cast<uint16_t>(cfg.wup_status.mask() &
                                                  ((uint16_t{1} << kMaxWakeSources) - 1)));

    for (size_t i = 0; i < kMaxWakeSources; ++i) {
        const WakeSourceCfg& src = cfg.sources[i];
        const uint16_t base = static_cast<uint16_t>(kWakeupRegSourceBase + i * kWakeupRegSourceSpan);

        uint8_t io_src;
        if (src.trigger_on_rising_edge && src.trigger_on_falling_edge) {
            io_src = kWakeupIoSrcBothEdges;
        } else if (src.trigger_on_rising_edge) {
            io_src = kWakeupIoSrcRisingEdge;
        } else if (src.trigger_on_falling_edge) {
            io_src = kWakeupIoSrcFallingEdge;
        } else if (!src.enabled) {
            io_src = kWakeupIoSrcInactive;
        } else {
            io_src = src.active_high ? kWakeupIoSrcHighLevel : kWakeupIoSrcLowLevel;
        }

        const uint16_t reg = static_cast<uint16_t>((static_cast<uint16_t>(io_src & 0x1F) << 11) |
                                                     (src.pin_number & 0x07FF));
        avtp::detail::put_u16(&out[base], reg);
    }
}

namespace detail {
inline void wakeup_parse_registers(WakeupFunctionalConfig& cfg, const WakeupRegisterBlock& in) noexcept {
    cfg.ep_status = avtp::detail::get_u16(&in[kWakeupRegEpStatus]);

    // REQ-WAKEUP-029: write-1-to-clear, per bit — each wire bit set to 1
    // clears that SAME bit's own source in wup_status, independently of
    // every other bit.
    const uint16_t wup = avtp::detail::get_u16(&in[kWakeupRegWupStatus]);
    for (size_t i = 0; i < kMaxWakeSources; ++i) {
        if ((wup & (uint16_t{1} << i)) != 0) cfg.wup_status.clear_source(i);
    }

    // REQ-WAKEUP-035
    for (size_t i = 0; i < kMaxWakeSources; ++i) {
        WakeSourceCfg& src = cfg.sources[i];
        const uint16_t base = static_cast<uint16_t>(kWakeupRegSourceBase + i * kWakeupRegSourceSpan);
        const uint16_t reg  = avtp::detail::get_u16(&in[base]);
        const uint8_t  io_src = static_cast<uint8_t>((reg >> 11) & 0x1F);

        src.pin_number = static_cast<uint16_t>(reg & 0x07FF);

        switch (io_src) {
        case kWakeupIoSrcInactive:
            src.enabled = false; src.trigger_on_rising_edge = false; src.trigger_on_falling_edge = false;
            break;
        case kWakeupIoSrcRisingEdge:
            src.enabled = true; src.trigger_on_rising_edge = true; src.trigger_on_falling_edge = false;
            break;
        case kWakeupIoSrcFallingEdge:
            src.enabled = true; src.trigger_on_rising_edge = false; src.trigger_on_falling_edge = true;
            break;
        case kWakeupIoSrcBothEdges:
            src.enabled = true; src.trigger_on_rising_edge = true; src.trigger_on_falling_edge = true;
            break;
        case kWakeupIoSrcHighLevel:
            src.enabled = true; src.active_high = true;
            src.trigger_on_rising_edge = false; src.trigger_on_falling_edge = false;
            break;
        case kWakeupIoSrcLowLevel:
            src.enabled = true; src.active_high = false;
            src.trigger_on_rising_edge = false; src.trigger_on_falling_edge = false;
            break;
        default:
            // Reserved (0x06-0x1F): cannot represent it — enabled/
            // active_high/trigger_on_*_edge are left exactly as they were,
            // an honest "cannot apply" rather than a silently wrong
            // reinterpretation.
            break;
        }
    }
}
} // namespace detail

inline std::error_code apply_reconfig(WakeupFunctionalConfig& cfg, const uint8_t* payload, size_t payload_len) {
    if (payload_len <= kWakeupReconfigAddrLen) return make_error_code(WakeupErrc::reconfig_short);

    const uint16_t start_address = avtp::detail::get_u16(payload);
    const size_t   data_len      = payload_len - kWakeupReconfigAddrLen;

    if (static_cast<size_t>(start_address) + data_len > static_cast<size_t>(kWakeupEpFuncLen))
        return make_error_code(WakeupErrc::reconfig_out_of_range);

    WakeupRegisterBlock block{};
    render_registers(cfg, block);
    for (size_t i = 0; i < data_len; ++i) {
        const auto addr = static_cast<uint16_t>(start_address + i);
        if (detail::wakeup_reg_offset_read_only(addr)) continue;
        block[addr] = payload[kWakeupReconfigAddrLen + i];
    }
    detail::wakeup_parse_registers(cfg, block);
    return {};
}

inline std::vector<uint8_t> encode_reconfig_request(avtp::ByteBusId byte_bus_id, uint16_t start_address,
                                                      const std::vector<uint8_t>& data,
                                                      uint8_t transaction_num) {
    if (data.empty()) return {};
    if (kWakeupReconfigAddrLen + data.size() > acf::kAcfAbbMaxPayload) return {};

    std::vector<uint8_t> payload(kWakeupReconfigAddrLen + data.size());
    avtp::detail::put_u16(payload.data(), start_address);
    std::copy(data.begin(), data.end(), payload.begin() + static_cast<long>(kWakeupReconfigAddrLen));

    acf::AcfMessageInfo info;
    info.byte_bus_id     = byte_bus_id;
    info.op                = true; // write, §12.7.1
    info.evt_op             = 0x7; // evt[2:0] == 111b
    info.transaction_num    = transaction_num;
    return acf::encode_acf_abb(info, payload);
}

// ── WakeupEndpoint ────────────────────────────────────────────────────────────
// Ties the wake/asleep state SleepCMD and wake-source events drive, the real
// WakeupFunctionalConfig (sources/wup_status/ep_status), and the pending-
// WakeUp-repetition handshake flag rcp/powerstate.hpp's PowerManager depends
// on (see the file header's own "WakeupEndpoint's pre-Phase-3 API is
// preserved" note) into one object. This header has no clock or transport of
// its own — record_wake_source_event()/acknowledge_wakeup() are called by
// the embedding application's driver/transport layer, same disclaimer as
// every other endpoint header in this codebase.
class WakeupEndpoint {
public:
    // handle_sleep_cmd applies the fixed SleepCMD opcode (in-memory
    // convenience, independent of the ACF wire codec above — a caller with
    // a raw decoded request byte in hand does not need to build a full ACF
    // frame just to drive this state machine). Returns
    // WakeupErrc::bad_opcode, unchanged, without altering endpoint state,
    // for any byte other than kSleepCmdOpcode.
    std::error_code handle_sleep_cmd(uint8_t request_byte) noexcept {
        if (request_byte != kSleepCmdOpcode) return make_error_code(WakeupErrc::bad_opcode);
        asleep_ = true;
        wake_handshake_pending_ = false; // entering Sleep clears any handshake left over from a prior cycle
        return {};
    }

    bool is_asleep() const noexcept { return asleep_; }

    // record_wake_source_event models wake-source slot source_index
    // transitioning to its own asserted condition: it wakes the endpoint
    // (whether or not it was currently asleep — a source event outside
    // Sleep is still latched) and arms the repeating WakeUp message
    // handshake for a hot start. Also latches source_index's own bit in the
    // real wup_status register (REQ-WAKEUP-006, no-op if source_index >=
    // kMaxWakeSources).
    void record_wake_source_event(size_t source_index) noexcept {
        cfg_.wup_status.latch_source(source_index);
        asleep_ = false;
        wake_handshake_pending_ = true;
    }

    // wakeup_message_pending/acknowledge_wakeup: see the file header's own
    // "WakeupEndpoint's pre-Phase-3 API is preserved" note — rcp/
    // powerstate.hpp's PowerManager depends on these two exactly.
    bool wakeup_message_pending() const noexcept { return wake_handshake_pending_; }
    void acknowledge_wakeup() noexcept { wake_handshake_pending_ = false; }

    WakeupFunctionalConfig&       functional_cfg() noexcept { return cfg_; }
    const WakeupFunctionalConfig& functional_cfg() const noexcept { return cfg_; }

private:
    bool                     asleep_                 = false;
    bool                     wake_handshake_pending_ = false;
    WakeupFunctionalConfig   cfg_;
};

} // namespace wakeup
} // namespace rcp

// Enable std::error_code construction from rcp::wakeup::WakeupErrc.
namespace std {
template <>
struct is_error_code_enum<rcp::wakeup::WakeupErrc> : true_type {};
} // namespace std
