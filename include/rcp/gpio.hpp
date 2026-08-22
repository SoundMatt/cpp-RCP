// fusa:req REQ-GPIO-001
// fusa:req REQ-GPIO-002
// fusa:req REQ-GPIO-003
// fusa:req REQ-GPIO-004
// fusa:req REQ-GPIO-005
// fusa:req REQ-GPIO-006
// fusa:req REQ-GPIO-007
// fusa:req REQ-GPIO-008
// fusa:req REQ-GPIO-009
// fusa:req REQ-GPIO-010
// fusa:req REQ-GPIO-011
// fusa:req REQ-GPIO-012
// fusa:req REQ-GPIO-013
// fusa:req REQ-GPIO-014
// fusa:req REQ-GPIO-015
// fusa:req REQ-GPIO-016
// fusa:req REQ-GPIO-017
// fusa:req REQ-GPIO-018
// fusa:req REQ-GPIO-019
// fusa:req REQ-GPIO-020
// fusa:req REQ-GPIO-021
// fusa:req REQ-GPIO-022
// fusa:req REQ-GPIO-023
// fusa:req REQ-GPIO-024
// fusa:req REQ-GPIO-025
// fusa:req REQ-GPIO-026
// fusa:req REQ-GPIO-027
// fusa:req REQ-GPIO-028
// fusa:req REQ-GPIO-029
// fusa:req REQ-GPIO-030
// fusa:req REQ-GPIO-031
// fusa:req REQ-GPIO-032
// fusa:req REQ-GPIO-033
// fusa:req REQ-GPIO-034
// fusa:req REQ-GPIO-035
// fusa:req REQ-GPIO-036
// fusa:req REQ-GPIO-037
// fusa:req REQ-GPIO-038
// fusa:req REQ-GPIO-039
// fusa:req REQ-GPIO-040
// fusa:req REQ-GPIO-041
// fusa:req REQ-GPIO-042
// fusa:req REQ-GPIO-043
// fusa:req REQ-GPIO-044
// fusa:req REQ-GPIO-045
// fusa:req REQ-GPIO-046

// GPIO endpoint (ep_type 0x02) — a 32-pin bitmask read/written with one of
// eight evt[2:0]-selected write semantics, per-pin change/rising/falling
// trigger signals, per-pin debounce filtering, response-timing
// classification, a real ACF-level wire codec, and the EP_func
// functional-configuration register block (evt[2:0] == 111b) (extraction
// §5.3, §4.5 Group C).
//
// ROADMAP.md Phase 17 / cpp-RCP issue #129, Phase 3 ("Per-endpoint
// modules"): this header is re-derived from c-RCP's ep_gpio.h/ep_gpio.c —
// c-RCP's RC5-conformant reference implementation for this endpoint type —
// rather than incrementally patched, per the roadmap's own module-by-module
// rewrite plan. No text from the OPEN Alliance TC18 Remote Control Protocol
// Specification is reproduced here; field names and behavior below
// implement TC18's *behavior* as ported from c-RCP's own implementation of
// an internal structured extraction of the specification.
//
// Content re-verified against c-RCP's *current* ep_gpio.h/.c (Phase 3 task
// instruction, given this repo's own earlier "Table 30 Row-2" pilot-module
// history for the sibling rcp/adc.hpp): this header's own pre-Phase-3
// content (GPIO pin mask, GpioState, apply_gpio_write's input-pin write
// masking (issue #105, cpp-RCP-15), the 4-byte payload codec, and the
// per-pin trigger-signal TriggerRegistry wiring) was already correct and is
// preserved unchanged below — c-RCP's own rcp_ep_gpio_apply_masked_write()
// implements the identical input-pin masking rule this header's
// apply_gpio_write() already had. What was genuinely missing, ported below
// for the first time: the real ACF-level wire codec (encode/decode of read
// requests, write requests including the reserved evt[2:0]=100b rejection,
// and responses), the EP_func functional-configuration register block
// (render_registers()/apply_reconfig(), evt[2:0]==111b), per-pin debounce
// filtering (REQ-GPIO-035), response-timing classification
// (REQ-GPIO-036), and Table 43's own wire trigger-signal numbering
// (REQ-GPIO-034) — distinct from this header's own internal
// TriggerRegistry::SignalId encoding (gpio_signal_id below), which remains
// this codebase's own bookkeeping scheme, not a wire-visible value.
//
// Field names and behavior below implement TC18's *behavior* as described in
// an internal structured extraction of the specification named above; no
// text from that document is reproduced here. The concrete pin-direction bit
// convention, functional-config layout, and trigger-signal id encoding
// chosen in this file are this implementation's own, same as the equivalent
// disclaimers in rcp/avtp.hpp, rcp/regmap.hpp, and rcp/endpoint.hpp.
#pragma once

#include <rcp/acf.hpp>
#include <rcp/avtp.hpp>
#include <rcp/endpoint.hpp>
#include <rcp/lifecycle.hpp>
#include <rcp/regmap.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <system_error>
#include <vector>

namespace rcp {
namespace gpio {

// ── Pin mask ──────────────────────────────────────────────────────────────────
// GPIO's request/response payload is a single 4-byte bitmask, one bit per
// pin, up to 32 pins (extraction §5.3).

using PinMask = uint32_t;
constexpr uint8_t kMaxPins        = 32;
constexpr size_t  kGpioPayloadLen = sizeof(PinMask);

// ── GpioState ─────────────────────────────────────────────────────────────────
// `values` is the live per-pin logic-level bitmask every write semantics
// below (other than Reconfigure) reads and updates. `directions` is this
// implementation's own choice of bit convention for which pins are
// currently configured as outputs (1) vs. inputs (0); Reconfigure is the
// only write semantics that targets it rather than `values`.

struct GpioState {
    PinMask values     = 0;
    PinMask directions = 0;
};

// ── Errors ────────────────────────────────────────────────────────────────────

enum class GpioErrc : int {
    pin_index_out_of_range = 1, // a pin index >= kMaxPins was referenced
    short_frame             = 2,
    bad_msg_type              = 3,
    wrong_bus                  = 4,
    wrong_op                    = 5,
    bad_payload_len              = 6,
    // TC18 §13.5 Table 33's GPIO/PWM_OUT row, evt[2:0]=100b: "reserved --
    // request shall be ignored and an err-response with error code =
    // UNSUPPORTED_CMD shall be sent" -- the wire-decode half of that rule
    // (apply_gpio_write already implements the "ignored" half by rejecting
    // WriteSemantics::Reserved without mutating state).
    reserved_evt                  = 7,
    // Configuration write (evt[2:0]==111b) payload carries no address
    // prefix, or an address prefix with no data octet after it.
    reconfig_short                 = 8,
    // Configuration write's start_address + data length exceeds
    // kGpioEpFuncLen -- the whole write is ignored, per the specification's
    // own rule.
    reconfig_out_of_range            = 9,
};

inline const std::error_category& gpio_category() noexcept {
    struct Cat : std::error_category {
        const char* name() const noexcept override { return "rcp.gpio"; }
        std::string message(int ev) const override {
            switch (static_cast<GpioErrc>(ev)) {
            case GpioErrc::pin_index_out_of_range: return "rcp/gpio: pin index out of range";
            case GpioErrc::short_frame:            return "rcp/gpio: frame too short";
            case GpioErrc::bad_msg_type:           return "rcp/gpio: unexpected ACF message type";
            case GpioErrc::wrong_bus:              return "rcp/gpio: wrong byte_bus_id";
            case GpioErrc::wrong_op:               return "rcp/gpio: wrong ACF op";
            case GpioErrc::bad_payload_len:        return "rcp/gpio: unexpected payload length";
            case GpioErrc::reserved_evt:
                return "rcp/gpio: evt[2:0] is the reserved value 100b";
            case GpioErrc::reconfig_short:
                return "rcp/gpio: GPIO configuration write has no address and data";
            case GpioErrc::reconfig_out_of_range:
                return "rcp/gpio: GPIO configuration write extends past the EP_func block";
            default:                               return "rcp/gpio: unknown error";
            }
        }
    };
    static Cat instance;
    return instance;
}

inline std::error_code make_error_code(GpioErrc e) noexcept {
    return {static_cast<int>(e), gpio_category()};
}

// wire_error maps e to its numbered wire error code (acf::WireErrorCode),
// for a caller building an Error Response frame once a request has failed
// to decode. std::nullopt for every GpioErrc value with no numbered
// counterpart.
inline std::optional<acf::WireErrorCode> wire_error(GpioErrc e) noexcept {
    switch (e) {
    case GpioErrc::bad_payload_len: return acf::WireErrorCode::InvalidParameter;
    case GpioErrc::reserved_evt:    return acf::WireErrorCode::UnsupportedCmd;
    default:                         return std::nullopt;
    }
}

// ── Write-semantics application ──────────────────────────────────────────────
// apply_gpio_write is GPIO's endpoint-specific completion of
// endpoint::apply_bitmask_write: it handles the six generic combinators by
// delegating to it against `state.values`, and separately handles
// Reconfigure by treating `operand` as the *new pin-direction mask*
// (replacing, not combining, `state.directions`) rather than a value
// combined with the pin-level state (extraction §5.3, §4.5 Group C).
//
// Subtract operand order (REQ-GPIO-011, found during the Phase 3
// content-parity pass): TC18's evt[2:0]=110b row (shared normatively by
// GPIO and PWM_OUT) defines this operation as "byte_msg_payload minus
// current interface status" — request MINUS current, not the reverse.
// rcp::endpoint::apply_bitmask_write's own Subtract branch instead computes
// current MINUS operand (saturating_subtract(current, operand)) — the
// opposite order, verified wrong against c-RCP's own
// rcp_ep_gpio_apply_write()'s RCP_EP_GPIO_WRITE_SUB case ("(current >
// request) ? 0u : (request - current)"), c-RCP being this project's
// RC5-conformant reference. Rather than change that shared helper's
// Subtract behavior for every caller (which would also silently change
// rcp/pwm.hpp's PWM_OUT semantics — a different endpoint type, out of this
// phase's own scope, not yet re-verified against its own current c-RCP
// counterpart), this function special-cases Subtract locally, calling
// endpoint::saturating_subtract directly with the operand order this row
// actually specifies.
//
// Input-pin write masking (issue #105, cpp-RCP-15; c-RCP's
// rcp_ep_gpio_apply_masked_write() implements the identical rule): TC18
// §13.7.4.3 states that a write request to a pin currently configured as an
// input is ignored for that pin — inputs are driven externally, and a write
// combinator's result for those bit positions must not be committed to
// state.values. The masking below commits the combinator's result only for
// bits where directions == output (1), preserving state.values unchanged
// wherever directions == input (0).
inline std::error_code apply_gpio_write(endpoint::WriteSemantics op, GpioState& state,
                                         PinMask operand) noexcept {
    if (op == endpoint::WriteSemantics::Reconfigure) {
        state.directions = operand;
        return {};
    }

    uint32_t out = 0;
    if (op == endpoint::WriteSemantics::Subtract) {
        out = endpoint::saturating_subtract<uint32_t>(operand, state.values); // request minus current
    } else {
        auto ec = endpoint::apply_bitmask_write(op, state.values, operand, out);
        if (ec) return ec;
    }
    state.values = (out & state.directions) | (state.values & ~state.directions);
    return {};
}

// ── Payload codec ─────────────────────────────────────────────────────────────
// Big-endian 4-byte encoding, matching rcp/avtp.hpp's own field convention
// (reusing its internal put_u32/get_u32 rather than re-deriving byte order
// here).

inline std::vector<uint8_t> encode_gpio_payload(PinMask mask) {
    std::vector<uint8_t> buf(kGpioPayloadLen);
    avtp::detail::put_u32(buf.data(), mask);
    return buf;
}

inline std::error_code decode_gpio_payload(const uint8_t* buf, size_t len, PinMask& out) noexcept {
    // Spec §13.7.4: "A request not having exactly four bytes is rejected" —
    // reject both too-short and too-long buffers, not just too-short
    // (cpp-RCP-05-fresh; same bug class as PWM's decode_pwm_payload,
    // cpp-RCP-03).
    if (len != kGpioPayloadLen) return avtp::make_error_code(avtp::AvtpErrc::short_buffer);
    out = avtp::detail::get_u32(buf);
    return {};
}

// ── Trigger signals ───────────────────────────────────────────────────────────
// Per-pin change/rising/falling signals (extraction §5.3, §4.5 Group C),
// built on rcp/endpoint.hpp's generic TriggerRegistry. gpio_signal_id packs
// (pin, edge) into one TriggerRegistry::SignalId so all three of a pin's
// signals — and every other pin's — share one registry instance per GPIO
// endpoint. This is this codebase's own internal bookkeeping scheme, kept
// deliberately distinct from trigger_signal_number() below (Table 43's own
// wire-visible signal numbering, REQ-GPIO-034).

enum class GpioEdge : uint8_t { Change = 0, Rising = 1, Falling = 2 };

constexpr endpoint::TriggerRegistry::SignalId gpio_signal_id(uint8_t pin, GpioEdge edge) noexcept {
    return static_cast<endpoint::TriggerRegistry::SignalId>((uint16_t(pin) << 2) | uint16_t(edge));
}

// evaluate_gpio_triggers compares `old_values` against `new_values` bit by
// bit and, for every pin whose bit changed, notifies TriggerRegistry of that
// pin's Change signal, plus Rising or Falling depending on the direction of
// the transition. Returns every signal id notify() reported as armed (i.e.
// worth delivering to a client), in pin-ascending, Change-before-edge order.
inline std::vector<endpoint::TriggerRegistry::SignalId>
evaluate_gpio_triggers(endpoint::TriggerRegistry& triggers, PinMask old_values, PinMask new_values) {
    std::vector<endpoint::TriggerRegistry::SignalId> fired;
    for (uint8_t pin = 0; pin < kMaxPins; ++pin) {
        const bool was_set = ((old_values >> pin) & 1u) != 0;
        const bool is_set  = ((new_values >> pin) & 1u) != 0;
        if (was_set == is_set) continue;

        if (triggers.notify(gpio_signal_id(pin, GpioEdge::Change)))
            fired.push_back(gpio_signal_id(pin, GpioEdge::Change));

        const GpioEdge edge = is_set ? GpioEdge::Rising : GpioEdge::Falling;
        if (triggers.notify(gpio_signal_id(pin, edge)))
            fired.push_back(gpio_signal_id(pin, edge));
    }
    return fired;
}

// ── Per-pin trigger mode (functional config) & Table 43 wire numbering ──────
// GpioTrigger names the three asynchronous-event trigger modes a pin's
// functional config may select, plus None — ordinals deliberately equal to
// Table 43's own per-pin offset (None=0, AnyChange=1, Rising=2, Falling=3,
// matching c-RCP's rcp_ep_gpio_trigger_t exactly), so trigger_signal_number
// below is exactly 3*pin_index + trigger with no per-case arithmetic.

enum class GpioTrigger : uint8_t { None = 0, AnyChange = 1, Rising = 2, Falling = 3 };

// trigger_fires: true iff a level transition from prev_level to new_level
// satisfies trigger — never for None; for AnyChange iff prev_level !=
// new_level; for Rising iff prev_level is false and new_level is true; for
// Falling iff prev_level is true and new_level is false.
inline bool trigger_fires(GpioTrigger trigger, bool prev_level, bool new_level) noexcept {
    switch (trigger) {
    case GpioTrigger::AnyChange: return prev_level != new_level;
    case GpioTrigger::Rising:    return !prev_level && new_level;
    case GpioTrigger::Falling:   return prev_level && !new_level;
    case GpioTrigger::None:
    default:                     return false;
    }
}

// REQ-GPIO-034: TC18 §13.7.4.1 Table 43 (RC5)'s own trigger signal
// numbering — signal 0 is "GPIO EP request execution done" (a whole-
// endpoint trigger, not modeled by this per-pin function), and for each pin
// IOn: signal 3n+1 is AnyChange, 3n+2 is Rising, 3n+3 is Falling, running up
// to signal 96 for IO31's own Falling entry. Returns the signal number iff
// pin_index < kMaxPins and trigger is one of AnyChange/Rising/Falling
// (never GpioTrigger::None, which names no trigger event and therefore no
// Table 43 signal number); std::nullopt otherwise.
inline std::optional<uint8_t> trigger_signal_number(uint8_t pin_index, GpioTrigger trigger) noexcept {
    if (pin_index >= kMaxPins) return std::nullopt;
    switch (trigger) {
    case GpioTrigger::AnyChange:
    case GpioTrigger::Rising:
    case GpioTrigger::Falling:
        return static_cast<uint8_t>(3u * pin_index + static_cast<uint8_t>(trigger));
    case GpioTrigger::None:
    default:
        return std::nullopt;
    }
}

// ── Debounce filtering (REQ-GPIO-035/044) ─────────────────────────────────────
// GpioDebounceState is one pin's own debounce-filter state — the
// caller-owned tracker every other stateful primitive in this codebase uses
// (matches AdcTriggerState's own "one small struct per thing being tracked"
// convention).

struct GpioDebounceState {
    bool    has_settled       = false; // false until the first sample settles
    bool    settled_value     = false; // the pin's current, debounced output value
    bool    has_candidate     = false; // whether a same-value run is in progress
    bool    candidate_value   = false; // the value being counted, if has_candidate
    uint8_t consecutive_count = 0;     // how many consecutive samples of
                                        // candidate_value have been seen so far
};

inline void debounce_state_init(GpioDebounceState& s) noexcept { s = GpioDebounceState{}; }

// TC18 §13.7.4.2 Table 44's own gpio_debounce_IOn rule: "0: no debounce;
// n>0: n consecutive samples of the same value need to be sampled before
// the output value is changed." This function is the pure decision the
// rule describes for one newly sampled raw pin value; the actual periodic
// sampling cadence (gpio_base_clk/gpio_clk_divider) remains a caller-owned
// timer this module never itself runs, matching every other endpoint
// type's own "never owns a timer, thread, or hardware" scope boundary.
//
// n == 0 (no debounce): raw_value becomes the settled value immediately,
// every call. n > 0: raw_value must be observed n consecutive times before
// it becomes the new settled value; a raw_value that differs from the value
// currently being counted resets the count to 1 for the new value — a
// single differing sample discards any partial run, it does not merely
// pause it.
//
// Returns s's settled value AFTER this call — false before the very first
// debounce window ever completes (there is no settled value yet; this
// deliberately does not leak the raw, unfiltered sample). *out_changed,
// when non-null, is set to whether this call's settled-value return
// differs from the settled value before this call — always false on the
// very first call.
inline bool debounce_sample(GpioDebounceState& s, bool raw_value, uint8_t n, bool* out_changed) noexcept {
    const bool prev_settled = s.has_settled && s.settled_value;

    if (n == 0) {
        s.has_candidate = false;
        const bool changed = s.has_settled && (prev_settled != raw_value);
        s.has_settled   = true;
        s.settled_value = raw_value;
        if (out_changed) *out_changed = changed;
        return s.settled_value;
    }

    if (!s.has_candidate || s.candidate_value != raw_value) {
        s.has_candidate     = true;
        s.candidate_value   = raw_value;
        s.consecutive_count = 1;
    } else if (s.consecutive_count < 0xFF) {
        ++s.consecutive_count;
    }

    bool changed = false;
    if (s.consecutive_count >= n && (!s.has_settled || s.settled_value != s.candidate_value)) {
        changed         = s.has_settled; // first-ever settle isn't a "change"
        s.has_settled   = true;
        s.settled_value = s.candidate_value;
    }

    if (out_changed) *out_changed = changed;
    return s.has_settled ? s.settled_value : false;
}

// ── Response timing (REQ-GPIO-036) ────────────────────────────────────────────

enum class GpioResponseTiming : uint8_t {
    Immediate      = 0, // pure read (no payload): respond immediately on execution
    AfterDebounce  = 1, // payload-bearing read, or any write: change the pin drive
                          // first, then wait the configured debounce time before
                          // responding
};

// TC18 §13.7.4.3: "A read request without a byte_msg_payload (pure read)
// generates a response immediately upon execution. A read request with a
// byte_msg_payload as well as a write request first change the drive of
// the pins, then wait for the debounce time before creating a response."
// `is_write` is acf::AcfMessageInfo::op's own read/write convention (false
// = read, true = write) — a caller that has already decoded the ACF header
// already has both values in hand. For is_write, payload_len is irrelevant
// to the outcome (a write always debounces) but is still accepted to keep
// the call site uniform.
inline GpioResponseTiming response_timing(bool is_write, size_t payload_len) noexcept {
    if (is_write) return GpioResponseTiming::AfterDebounce;
    return (payload_len == 0) ? GpioResponseTiming::Immediate : GpioResponseTiming::AfterDebounce;
}

// ── Functional config ─────────────────────────────────────────────────────────
// Flattens regmap.h's shared functional-config "common" prefix directly
// into this struct's own bools, same rationale as rcp/adc.hpp's
// AdcFunctionalConfig (cpp-RCP's rcp/regmap.hpp leaves
// EndpointFunctionalConfig::data as an opaque byte blob; render_registers()/
// apply_reconfig() below are this endpoint type's own full interpretation
// of that blob).

struct GpioPinCfg {
    // This implementation's own pin-direction bit convention (bit0 =
    // output when set), matching GpioState::directions' own convention
    // above — cpp-RCP's rcp/regmap.hpp defines no RCP_REGMAP_PIN_PROP_*
    // bitmask of its own (unlike c-RCP's regmap.h), so this endpoint type
    // owns the encoding, per this file's own "concrete ... bit convention
    // ... this implementation's own" disclaimer.
    uint8_t     pin_property = 0;
    GpioTrigger trigger      = GpioTrigger::None;
};

constexpr uint8_t kPinPropOutput = 0x01;

struct GpioFunctionalConfig {
    bool ep_enable             = false;
    bool ep_clear_req_storage  = false;
    bool ep_req_crc_enable     = false;
    bool ep_response_ts_enable = false;
    bool ep_suppress_response  = false;

    std::array<GpioPinCfg, kMaxPins> pins{};
    uint16_t                          ep_status    = 0; // gpio_ep_status
    uint8_t                            clk_divider   = 0; // gpio_clk_divider
    std::array<uint8_t, kMaxPins>      debounce{};        // gpio_debounce_IO0..IO31
};

// functional_cfg_writable is a thin, named wrapper over
// rcp::lifecycle::field_writable() with FieldKind::FunctionalW, reusing --
// never duplicating -- that function's authorization logic.
inline bool functional_cfg_writable(lifecycle::ServerState state, lifecycle::WriterCtx writer) noexcept {
    return lifecycle::field_writable(state, lifecycle::FieldKind::FunctionalW, writer);
}

// set_pin_property sets cfg.pins[pin_index].pin_property iff pin_index is
// valid and functional_cfg_writable() authorizes the write; returns whether
// the write was applied. cfg is left entirely unchanged when it returns
// false.
inline bool set_pin_property(GpioFunctionalConfig& cfg, uint8_t pin_index, uint8_t pin_property,
                              lifecycle::ServerState state, lifecycle::WriterCtx writer) noexcept {
    if (pin_index >= kMaxPins) return false;
    if (!functional_cfg_writable(state, writer)) return false;
    cfg.pins[pin_index].pin_property = pin_property;
    return true;
}

// Same authorization/validity rule as set_pin_property, for
// cfg.pins[pin_index].trigger.
inline bool set_pin_trigger(GpioFunctionalConfig& cfg, uint8_t pin_index, GpioTrigger trigger,
                             lifecycle::ServerState state, lifecycle::WriterCtx writer) noexcept {
    if (pin_index >= kMaxPins) return false;
    if (!functional_cfg_writable(state, writer)) return false;
    cfg.pins[pin_index].trigger = trigger;
    return true;
}

// ── The EP_func register block (evt[2:0] == 111b) ────────────────────────────
// Relative addresses within this endpoint's own EP_func block. Known
// editorial defect in the source table, resolved the same way c-RCP's own
// ep_gpio.h resolves it: the table's own explicit, non-elided rows
// establish gpio_debounce_IO0 at 0x0009 and gpio_debounce_IO1 at 0x000A —
// one octet per register, in pin order — but the table's own summary label
// for the elided range's last entry ("0x0024 gpio_debounce_IO31") is
// arithmetically inconsistent with that pattern (0x0009 + 31 = 0x0028, not
// 0x0024). This module places gpio_debounce_IO31 at the
// arithmetically-consistent 0x0028 (kGpioEpFuncLen = 0x0029, 41 octets
// total).

constexpr uint16_t kGpioRegEpLen        = 0x0000; //  8 bit, R
constexpr uint16_t kGpioRegIoMax        = 0x0001; //  8 bit, R
constexpr uint16_t kGpioRegEpEnableClr  = 0x0002; //  8 bit, R/W
constexpr uint16_t kGpioRegEpOptions    = 0x0003; //  8 bit, R/W
constexpr uint16_t kGpioRegBaseClk      = 0x0004; // 16 bit, R
constexpr uint16_t kGpioRegEpStatus     = 0x0006; // 16 bit, R/W
constexpr uint16_t kGpioRegClkDivider   = 0x0008; //  8 bit, R/W
constexpr uint16_t kGpioRegDebounceIo0  = 0x0009; //  8 bit, R/W; IO(n) at 0x0009+n

constexpr uint16_t kGpioEpFuncLen        = 0x0029;
constexpr size_t   kGpioReconfigAddrLen  = 2;

using GpioRegisterBlock = std::array<uint8_t, kGpioEpFuncLen>;

namespace detail {
constexpr uint8_t kGpioEnableClrBitEnable = 1u << 0;
constexpr uint8_t kGpioEnableClrBitClear  = 1u << 4;
constexpr uint8_t kGpioOptionsBitReqCrc   = 1u << 0;
constexpr uint8_t kGpioOptionsBitRespTs   = 1u << 3;
constexpr uint8_t kGpioOptionsBitSuppress = 1u << 7;

inline bool gpio_reg_offset_read_only(uint16_t addr) noexcept {
    return addr == kGpioRegEpLen || addr == kGpioRegIoMax || addr == kGpioRegBaseClk ||
           addr == static_cast<uint16_t>(kGpioRegBaseClk + 1);
}
} // namespace detail

// render_registers serializes cfg's EP_func registers into out exactly as a
// configuration *read* of the whole block would report them — the inverse
// of apply_reconfig()'s own parse step.
inline void render_registers(const GpioFunctionalConfig& cfg, GpioRegisterBlock& out) noexcept {
    uint8_t enable_clr = 0;
    uint8_t options    = 0;
    if (cfg.ep_enable) enable_clr |= detail::kGpioEnableClrBitEnable;
    if (cfg.ep_clear_req_storage) enable_clr |= detail::kGpioEnableClrBitClear;
    if (cfg.ep_req_crc_enable) options |= detail::kGpioOptionsBitReqCrc;
    if (cfg.ep_response_ts_enable) options |= detail::kGpioOptionsBitRespTs;
    if (cfg.ep_suppress_response) options |= detail::kGpioOptionsBitSuppress;

    out[kGpioRegEpLen]       = static_cast<uint8_t>(kGpioEpFuncLen);
    out[kGpioRegIoMax]       = kMaxPins;
    out[kGpioRegEpEnableClr] = enable_clr;
    out[kGpioRegEpOptions]   = options;
    avtp::detail::put_u16(&out[kGpioRegBaseClk], 0); // read-only, no GPIO clock source modelled
    avtp::detail::put_u16(&out[kGpioRegEpStatus], cfg.ep_status);
    out[kGpioRegClkDivider] = cfg.clk_divider;
    for (uint8_t i = 0; i < kMaxPins; ++i) out[kGpioRegDebounceIo0 + i] = cfg.debounce[i];
}

namespace detail {
inline void gpio_parse_registers(GpioFunctionalConfig& cfg, const GpioRegisterBlock& in) noexcept {
    const uint8_t enable_clr = in[kGpioRegEpEnableClr];
    const uint8_t options    = in[kGpioRegEpOptions];

    cfg.ep_enable             = (enable_clr & kGpioEnableClrBitEnable) != 0;
    cfg.ep_clear_req_storage  = (enable_clr & kGpioEnableClrBitClear) != 0;
    cfg.ep_req_crc_enable     = (options & kGpioOptionsBitReqCrc) != 0;
    cfg.ep_response_ts_enable = (options & kGpioOptionsBitRespTs) != 0;
    cfg.ep_suppress_response  = (options & kGpioOptionsBitSuppress) != 0;

    cfg.ep_status    = avtp::detail::get_u16(&in[kGpioRegEpStatus]);
    cfg.clk_divider  = in[kGpioRegClkDivider];
    for (uint8_t i = 0; i < kMaxPins; ++i) cfg.debounce[i] = in[kGpioRegDebounceIo0 + i];
}
} // namespace detail

// apply_reconfig applies the real configuration escape hatch (evt[2:0] ==
// 111b): payload is a 16-bit big-endian relative start address followed by
// the configuration data octets to write from that address onward
// (extraction §3.7.1, TC18 §12.7.1 Figure 18). Returns
// GpioErrc::reconfig_short when payload_len is not at least
// kGpioReconfigAddrLen + 1, and GpioErrc::reconfig_out_of_range when the
// addressed span would extend past kGpioEpFuncLen; in both cases cfg is
// left entirely unchanged. Octets of the addressed span that land on a
// read-only register (EP_LEN, IO_MAX, base_clk) are left at their current
// values while the rest of the span is still applied.
inline std::error_code apply_reconfig(GpioFunctionalConfig& cfg, const uint8_t* payload,
                                       size_t payload_len) {
    if (payload_len <= kGpioReconfigAddrLen) return make_error_code(GpioErrc::reconfig_short);

    const uint16_t start_address = avtp::detail::get_u16(payload);
    const size_t   data_len      = payload_len - kGpioReconfigAddrLen;

    if (static_cast<size_t>(start_address) + data_len > static_cast<size_t>(kGpioEpFuncLen))
        return make_error_code(GpioErrc::reconfig_out_of_range);

    GpioRegisterBlock block{};
    render_registers(cfg, block);
    for (size_t i = 0; i < data_len; ++i) {
        const auto addr = static_cast<uint16_t>(start_address + i);
        if (detail::gpio_reg_offset_read_only(addr)) continue;
        block[addr] = payload[kGpioReconfigAddrLen + i];
    }
    detail::gpio_parse_registers(cfg, block);
    return {};
}

// ── Wire codec ─────────────────────────────────────────────────────────────────
// Ported directly from c-RCP's ep_gpio.c wire functions, using rcp/acf.hpp's
// ACF_ABB/ACF_GBB codec rather than re-deriving frame layout here.

// encode_read_request encodes an ACF_ABB read request addressed to
// byte_bus_id, with no payload.
inline std::vector<uint8_t> encode_read_request(avtp::ByteBusId byte_bus_id, uint8_t transaction_num) {
    acf::AcfMessageInfo info;
    info.byte_bus_id     = byte_bus_id;
    info.op                = false; // read
    info.transaction_num   = transaction_num;
    return acf::encode_acf_abb(info, {});
}

// decode_read_request decodes and validates an ACF-level GPIO read request.
// Fails with GpioErrc::short_frame / bad_msg_type / wrong_bus / wrong_op.
// On success, out_transaction_num is populated.
inline std::error_code decode_read_request(const uint8_t* b, size_t len, avtp::ByteBusId expected_bus_id,
                                            uint8_t& out_transaction_num) {
    acf::AcfMessageInfo   info;
    std::vector<uint8_t>   payload;
    const auto              ec = acf::decode_acf_abb(b, len, info, payload);
    if (ec == avtp::make_error_code(avtp::AvtpErrc::short_buffer)) return make_error_code(GpioErrc::short_frame);
    if (ec) return make_error_code(GpioErrc::bad_msg_type);

    if (info.byte_bus_id != expected_bus_id) return make_error_code(GpioErrc::wrong_bus);
    if (info.op) return make_error_code(GpioErrc::wrong_op); // op=true means write

    out_transaction_num = info.transaction_num;
    return {};
}

// encode_write_request encodes an ACF_ABB write request addressed to
// byte_bus_id: evt's low three bits carry evt (masked to 3 bits), and the
// payload is bitmask as kGpioPayloadLen big-endian octets.
inline std::vector<uint8_t> encode_write_request(avtp::ByteBusId byte_bus_id, PinMask bitmask,
                                                   endpoint::WriteSemantics evt, uint8_t transaction_num) {
    acf::AcfMessageInfo info;
    info.byte_bus_id     = byte_bus_id;
    info.op                = true; // write
    info.evt_op             = static_cast<uint8_t>(static_cast<uint8_t>(evt) & 0x7);
    info.transaction_num    = transaction_num;
    return acf::encode_acf_abb(info, encode_gpio_payload(bitmask));
}

// decode_write_request decodes and validates a GPIO write request. Same
// ACF-level failure modes as decode_read_request (short frame / bad msg
// type / wrong bus), except GpioErrc::wrong_op is returned when op is not
// write, and GpioErrc::bad_payload_len when the payload is not exactly
// kGpioPayloadLen octets.
//
// REQ-GPIO-012/045: GpioErrc::reserved_evt is returned when evt[2:0] ==
// WriteSemantics::Reserved (100b), Table 33's own GPIO/PWM_OUT row's
// reserved value — neither out_bitmask, out_evt, nor out_transaction_num is
// populated in that case; a caller builds the required err-response via
// wire_error(), which maps this errc to acf::WireErrorCode::UnsupportedCmd.
//
// On success, out_bitmask, out_evt, and out_transaction_num are populated.
inline std::error_code decode_write_request(const uint8_t* b, size_t len, avtp::ByteBusId expected_bus_id,
                                             PinMask& out_bitmask, endpoint::WriteSemantics& out_evt,
                                             uint8_t& out_transaction_num) {
    acf::AcfMessageInfo   info;
    std::vector<uint8_t>   payload;
    const auto              ec = acf::decode_acf_abb(b, len, info, payload);
    if (ec == avtp::make_error_code(avtp::AvtpErrc::short_buffer)) return make_error_code(GpioErrc::short_frame);
    if (ec) return make_error_code(GpioErrc::bad_msg_type);

    if (info.byte_bus_id != expected_bus_id) return make_error_code(GpioErrc::wrong_bus);
    if (!info.op) return make_error_code(GpioErrc::wrong_op); // op=false means read
    if (payload.size() != kGpioPayloadLen) return make_error_code(GpioErrc::bad_payload_len);

    const auto evt = endpoint::write_semantics_of(info.evt_op);
    if (evt == endpoint::WriteSemantics::Reserved) return make_error_code(GpioErrc::reserved_evt);

    out_bitmask         = avtp::detail::get_u32(payload.data());
    out_evt              = evt;
    out_transaction_num  = info.transaction_num;
    return {};
}

// encode_reconfig_request encodes an ACF_ABB configuration request
// (evt[2:0] == 111b) addressed to byte_bus_id: payload is start_address
// (16-bit big-endian) followed by data. Returns an empty vector if data is
// empty or the encoded payload would exceed acf::kAcfAbbMaxPayload.
inline std::vector<uint8_t> encode_reconfig_request(avtp::ByteBusId byte_bus_id, uint16_t start_address,
                                                      const std::vector<uint8_t>& data,
                                                      uint8_t transaction_num) {
    if (data.empty()) return {};
    if (kGpioReconfigAddrLen + data.size() > acf::kAcfAbbMaxPayload) return {};

    std::vector<uint8_t> payload(kGpioReconfigAddrLen + data.size());
    avtp::detail::put_u16(payload.data(), start_address);
    std::copy(data.begin(), data.end(), payload.begin() + static_cast<long>(kGpioReconfigAddrLen));

    acf::AcfMessageInfo info;
    info.byte_bus_id     = byte_bus_id;
    info.op                = true; // write
    info.evt_op             = static_cast<uint8_t>(endpoint::WriteSemantics::Reconfigure);
    info.transaction_num    = transaction_num;
    return acf::encode_acf_abb(info, payload);
}

// encode_response encodes a GPIO response carrying bitmask as its
// kGpioPayloadLen big-endian payload, echoing transaction_num. Encoded as
// ACF_ABB when timed is false; as ACF_GBB (mtv valid, message_timestamp =
// timestamp) when timed is true.
inline std::vector<uint8_t> encode_response(avtp::ByteBusId byte_bus_id, PinMask bitmask,
                                             uint8_t transaction_num, bool timed, uint64_t timestamp) {
    const auto payload = encode_gpio_payload(bitmask);

    acf::AcfMessageInfo info;
    info.byte_bus_id     = byte_bus_id;
    info.op                = false; // read
    info.rsp                = true;
    info.transaction_num     = transaction_num;

    if (timed) {
        info.mtv = true;
        return acf::encode_acf_gbb(info, timestamp, payload);
    }
    return acf::encode_acf_abb(info, payload);
}

// decode_response decodes a GPIO response from either an ACF_ABB or
// ACF_GBB message (peeks the ACF message type itself). Fails with
// GpioErrc::short_frame / wrong_bus / bad_payload_len (payload present but
// not exactly kGpioPayloadLen octets). On success, out_bitmask/
// out_transaction_num are populated; out_timed/out_timestamp report
// whether the message was a valid-timestamp ACF_GBB, and that timestamp's
// value (0 when !out_timed).
inline std::error_code decode_response(const uint8_t* b, size_t len, avtp::ByteBusId expected_bus_id,
                                        PinMask& out_bitmask, bool& out_timed, uint64_t& out_timestamp,
                                        uint8_t& out_transaction_num) {
    uint8_t msg_type = 0;
    if (acf::peek_msg_type(b, len, msg_type)) return make_error_code(GpioErrc::short_frame);

    acf::AcfMessageInfo  info;
    std::vector<uint8_t>  payload;
    avtp::ByteBusId        bus_id = 0;
    uint8_t                 txn    = 0;

    if (msg_type == acf::kAcfMsgTypeGbb) {
        uint64_t ts = 0;
        const auto ec = acf::decode_acf_gbb(b, len, info, ts, payload);
        if (ec == avtp::make_error_code(avtp::AvtpErrc::short_buffer)) return make_error_code(GpioErrc::short_frame);
        if (ec) return make_error_code(GpioErrc::bad_msg_type);
        bus_id        = info.byte_bus_id;
        txn           = info.transaction_num;
        out_timed     = info.mtv;
        out_timestamp = out_timed ? ts : 0;
    } else {
        const auto ec = acf::decode_acf_abb(b, len, info, payload);
        if (ec == avtp::make_error_code(avtp::AvtpErrc::short_buffer)) return make_error_code(GpioErrc::short_frame);
        if (ec) return make_error_code(GpioErrc::bad_msg_type);
        bus_id        = info.byte_bus_id;
        txn           = info.transaction_num;
        out_timed     = false;
        out_timestamp = 0;
    }

    if (bus_id != expected_bus_id) return make_error_code(GpioErrc::wrong_bus);
    if (payload.size() != kGpioPayloadLen) return make_error_code(GpioErrc::bad_payload_len);

    out_bitmask          = avtp::detail::get_u32(payload.data());
    out_transaction_num  = txn;
    return {};
}

// ── Functional config block wiring (pre-Phase-3 opaque-blob helpers) ────────
// Kept unchanged for existing callers: interprets
// regmap::EndpointFunctionalConfig::data as a plain (directions, per-pin
// enabled-edge-mask) pair — a simpler, independent encoding from
// GpioFunctionalConfig/render_registers/apply_reconfig above, which now
// model the *real* TC18 EP_func register block (Table 44) instead. This
// pair predates that real register block and is retained as a lighter
// caller-side convenience where the full register block is unneeded.

constexpr size_t kGpioFunctionalConfigLen = kGpioPayloadLen + kMaxPins;

inline regmap::EndpointFunctionalConfig
encode_gpio_functional_config(PinMask directions, const std::array<uint8_t, kMaxPins>& enabled_edge_masks) {
    regmap::EndpointFunctionalConfig cfg;
    cfg.data.resize(kGpioFunctionalConfigLen);
    avtp::detail::put_u32(cfg.data.data(), directions);
    std::copy(enabled_edge_masks.begin(), enabled_edge_masks.end(),
              cfg.data.begin() + static_cast<long>(kGpioPayloadLen));
    return cfg;
}

inline std::error_code decode_gpio_functional_config(const regmap::EndpointFunctionalConfig& cfg,
                                                       PinMask& out_directions,
                                                       std::array<uint8_t, kMaxPins>& out_enabled_edge_masks) noexcept {
    if (cfg.data.size() < kGpioFunctionalConfigLen)
        return avtp::make_error_code(avtp::AvtpErrc::short_buffer);
    out_directions = avtp::detail::get_u32(cfg.data.data());
    std::copy(cfg.data.begin() + static_cast<long>(kGpioPayloadLen),
              cfg.data.begin() + static_cast<long>(kGpioFunctionalConfigLen),
              out_enabled_edge_masks.begin());
    return {};
}

// ── GpioEndpoint ──────────────────────────────────────────────────────────────
// Ties GpioState, apply_gpio_write, and the trigger-signal evaluation above
// into the single request-dispatch entry point a caller (e.g. an RC Server's
// request loop) would invoke per incoming GPIO write.
class GpioEndpoint {
public:
    // handle_write applies one decoded write request (`op` from
    // endpoint::write_semantics_of(evt_op), `operand` from the request's
    // decoded 4-byte payload) to this endpoint's live state, evaluates the
    // resulting transition against the trigger-signal table, and reports
    // the endpoint's new pin-value bitmask for the caller to encode into a
    // write response.
    std::error_code handle_write(endpoint::WriteSemantics op, PinMask operand, PinMask& out_value) noexcept {
        const PinMask before = state_.values;
        auto ec = apply_gpio_write(op, state_, operand);
        if (ec) return ec;
        evaluate_gpio_triggers(triggers_, before, state_.values);
        out_value = state_.values;
        return {};
    }

    PinMask read() const noexcept { return state_.values; }
    const GpioState& state() const noexcept { return state_; }
    endpoint::TriggerRegistry& triggers() noexcept { return triggers_; }

private:
    GpioState                 state_;
    endpoint::TriggerRegistry triggers_;
};

} // namespace gpio
} // namespace rcp

// Enable std::error_code construction from rcp::gpio::GpioErrc.
namespace std {
template <>
struct is_error_code_enum<rcp::gpio::GpioErrc> : true_type {};
} // namespace std
