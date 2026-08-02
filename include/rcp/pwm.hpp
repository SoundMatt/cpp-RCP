// fusa:req REQ-PWM-001
// fusa:req REQ-PWM-002
// fusa:req REQ-PWM-003
// fusa:req REQ-PWM-004
// fusa:req REQ-PWM-005
// fusa:req REQ-PWM-006
// fusa:req REQ-PWM-007

// PWM_OUT (ep_type 0x07) and PWM_IN (ep_type 0x08) endpoints — the shared
// period/active-duration two-field payload shape, PWM_OUT's fixed 4-byte
// wire payload, PWM_IN's response-only read model and PWM_IN_NO_SIGNAL
// error path, and the mid-pulse trigger signal used to key ADC sampling
// cadence (extraction §5.5, §5.6).
//
// ROADMAP.md milestone 48, "Basic Endpoint Types II — I2C, UART, ADC,
// PWM_OUT, PWM_IN (v2.4.0)": kept in one header, mirroring how
// rcp/endpoint.hpp is shared scaffolding for rcp/gpio.hpp and rcp/spi.hpp
// rather than each having its own copy — PWM_OUT and PWM_IN share one
// payload struct (PwmValue) and differ only in read/write direction and
// trigger-signal behavior.
//
// Wire-format fix (issue #70, cpp-RCP-01): PWM_OUT/PWM_IN's payload is a
// fixed 4 bytes on the wire — two big-endian 16-bit words, PWM_Period
// followed by PWM_active, verified against the OPEN Alliance TC18 Remote
// Control Protocol Specification's "pwmo request format" figure (§13.7.5.3)
// — not the open-ended 8-byte uint32_t pair this header modeled before.
// `period`/`active_duration` are narrowed to uint16_t and encode_pwm_payload/
// decode_pwm_payload below produce/consume exactly that 4-byte, period-then-
// active big-endian layout, reusing rcp/avtp.hpp's own put_u16/get_u16
// helpers the same way rcp/gpio.hpp's encode_gpio_payload/
// decode_gpio_payload already reuse its put_u32/get_u32 for GPIO's 4-byte
// payload, rather than re-deriving byte order here. The field *order* in
// PwmValue (period first, then active_duration) was already correct and is
// unchanged — only the two fields' width and the addition of an explicit
// wire codec are new.
//
// PWM_OUT write-semantics correction (issue #104, cpp-RCP-14): issue #70
// had narrowed PwmOutEndpoint::handle_write to WriteSemantics::Replace only,
// on the reasoning that the PWM_OUT-specific request-handling section
// (§13.7.5.3) describes only one write behavior. That reasoning missed that
// §13.7.5.3 doesn't need to redescribe write semantics at all: §13.5 Table
// 30 ("EP specific usage of evt-field") is the governing table for
// evt[2:0]'s meaning across every endpoint type, and its GPIO/PWM_OUT row
// explicitly assigns PWM_OUT the *same* eight write semantics as GPIO —
// including Add/Subtract, whose own worked examples in that row name
// PWM_out's duty cycle directly ("this can be used to increase/decrease the
// duty cycle of PWM_out"). rcp::endpoint::saturating_add/saturating_subtract
// (below, in endpoint.hpp) were built templated on the caller's unsigned
// width specifically so GPIO's 32-bit pin mask and PWM_OUT's narrower
// period/duration fields could share one implementation — v2.4.0's own
// roadmap note this file's header used to cite — which issue #70 then
// contradicted without updating. PwmOutEndpoint::handle_write below now
// applies every non-Reconfigure write semantics via
// rcp::endpoint::apply_bitmask_write, per field (period and
// active_duration independently, each saturating within its own uint16_t
// range per Table 30's saturation note). Reconfigure remains rejected: this
// endpoint type has no EP_func addressed-write path implemented yet (a
// separate, larger gap common to every endpoint but one in this codebase,
// not specific to PWM_OUT's write-semantics bug this fixes).
//
// Field names and behavior below implement TC18's *behavior* as described in
// an internal structured extraction of the specification named above; no
// text from that document is reproduced here. The concrete period/duration
// unit (left as an opaque implementation-defined tick count, same as
// rcp/regmap.hpp's own field-width disclaimers) and trigger-signal id
// scheme chosen in this file are this implementation's own, same as the
// equivalent disclaimers in rcp/avtp.hpp, rcp/regmap.hpp, rcp/endpoint.hpp,
// and rcp/gpio.hpp.
#pragma once

#include <rcp/avtp.hpp>
#include <rcp/endpoint.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <system_error>

namespace rcp {
namespace pwm {

// ── Shared period/active-duration payload shape ───────────────────────────────
// `period` and `active_duration` share one implementation-defined tick unit
// (extraction §5.5, §5.6); this header does not itself fix that unit to a
// physical time value. Both fields are 16 bits wide on the wire — the
// PWM_OUT/PWM_IN payload is a fixed 4 bytes total (verified against the
// spec's "pwmo request format" figure, §13.7.5.3), not an open-ended
// 32-bit-per-field pair.

struct PwmValue {
    uint16_t period          = 0;
    uint16_t active_duration = 0;
};

// ── Wire codec ────────────────────────────────────────────────────────────────
// Fixed 4-byte payload: PWM_Period (big-endian 16 bit) followed by PWM_active
// (big-endian 16 bit), per the spec's "pwmo request format" figure
// (§13.7.5.3) — a request not having exactly four bytes is rejected there
// with INVALID_PARAMETER, which decode_pwm_payload below surfaces as
// avtp::AvtpErrc::short_buffer for a too-short buffer.

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

// ── Errors ────────────────────────────────────────────────────────────────────

enum class PwmErrc : int {
    no_signal = 1, // PWM_IN_NO_SIGNAL: no pulse has been measured yet
};

inline const std::error_category& pwm_category() noexcept {
    struct Cat : std::error_category {
        const char* name() const noexcept override { return "rcp.pwm"; }
        std::string message(int ev) const override {
            switch (static_cast<PwmErrc>(ev)) {
            case PwmErrc::no_signal: return "rcp/pwm: PWM_IN_NO_SIGNAL — no pulse measured";
            default:                 return "rcp/pwm: unknown error";
            }
        }
    };
    static Cat instance;
    return instance;
}

inline std::error_code make_error_code(PwmErrc e) noexcept {
    return {static_cast<int>(e), pwm_category()};
}

// apply_write_field is PWM_OUT's own field-width combinator: it mirrors
// rcp::endpoint::apply_bitmask_write's case set exactly, but instantiates
// saturating_add/saturating_subtract at uint16_t rather than uint32_t —
// apply_bitmask_write itself is hardcoded to uint32_t (see its own
// declaration in endpoint.hpp), so calling it directly against a uint16_t
// field and narrowing the uint32_t result back down would compute the
// Add/Subtract saturation bound at the wrong width (saturating at
// 0xFFFFFFFF, then truncating — silently wrapping instead of the 0xFFFF
// saturation Table 30's own note requires for a 16-bit field).
inline std::error_code apply_write_field(endpoint::WriteSemantics op, uint16_t current,
                                          uint16_t operand, uint16_t& out) noexcept {
    switch (op) {
    case endpoint::WriteSemantics::Replace:  out = operand;                                                 return {};
    case endpoint::WriteSemantics::Or:       out = static_cast<uint16_t>(current | operand);                return {};
    case endpoint::WriteSemantics::And:      out = static_cast<uint16_t>(current & operand);                return {};
    case endpoint::WriteSemantics::Xor:      out = static_cast<uint16_t>(current ^ operand);                return {};
    case endpoint::WriteSemantics::Add:      out = endpoint::saturating_add<uint16_t>(current, operand);      return {};
    case endpoint::WriteSemantics::Subtract: out = endpoint::saturating_subtract<uint16_t>(current, operand); return {};
    case endpoint::WriteSemantics::Reserved:
        return endpoint::make_error_code(endpoint::EndpointErrc::reserved_write_semantics);
    case endpoint::WriteSemantics::Reconfigure:
    default:
        return endpoint::make_error_code(endpoint::EndpointErrc::non_combinable_write_semantics);
    }
}

// ── PwmOutEndpoint (ep_type 0x07) ────────────────────────────────────────────
// handle_write applies every write semantics Table 30's GPIO/PWM_OUT row
// defines — Replace/Or/And/Xor/Add/Subtract — via apply_write_field above,
// independently against `period` and `active_duration` (each its own
// uint16_t, so each combines and saturates within its own 16-bit range, per
// Table 30's saturation note). Reconfigure is rejected: PWM_OUT has no
// EP_func addressed-write path in this codebase yet (see the header comment
// above), so there is nothing for it to target.
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
        out_value               = state_;
        return {};
    }

    const PwmValue& read() const noexcept { return state_; }

private:
    PwmValue state_;
};

// ── PwmInEndpoint (ep_type 0x08) ─────────────────────────────────────────────
// Response-only read model (extraction §5.6): PWM_IN has no write request
// shape at all in this milestone's scope. Trigger signals fixed to match
// Table 44 ("pwmi trigger outputs") exactly (issue cpp-RCP-A4-pwmin): the
// spec defines two independent trigger outputs — rising edge (0) and
// falling edge (1) of the measured PWM_IN signal — not the single invented
// "MidPulse" signal this header modeled before. record_edge fires whichever
// one edge actually occurred; record_measurement (kept for
// rcp/adc.hpp's AdcCadence::ExternalTrigger pattern and existing callers)
// models one full input-capture cycle completing — which inherently spans
// both a rising and a falling edge of the measured signal — by recording
// the new value once and then firing both signals.
enum class PwmInSignal : uint8_t { RisingEdge = 0, FallingEdge = 1 };

constexpr endpoint::TriggerRegistry::SignalId pwm_in_signal_id(PwmInSignal sig) noexcept {
    return static_cast<endpoint::TriggerRegistry::SignalId>(sig);
}

class PwmInEndpoint {
public:
    // record_edge fires exactly one of Table 44's two trigger signals for
    // an armed listener, without updating the last-measured value — for a
    // caller that observes rising/falling edges independently rather than
    // only at whole-cycle granularity.
    void record_edge(PwmInSignal edge) noexcept {
        triggers_.notify(pwm_in_signal_id(edge));
    }

    // record_measurement records one completed input-capture cycle (a full
    // period, spanning one rising and one falling edge of the measured
    // signal) and fires both Table 44 trigger signals for any armed
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

    endpoint::TriggerRegistry& triggers() noexcept { return triggers_; }

private:
    PwmValue                   last_value_{};
    bool                        has_signal_ = false;
    endpoint::TriggerRegistry  triggers_;
};

} // namespace pwm
} // namespace rcp

// Enable std::error_code construction from rcp::pwm::PwmErrc.
namespace std {
template <>
struct is_error_code_enum<rcp::pwm::PwmErrc> : true_type {};
} // namespace std
