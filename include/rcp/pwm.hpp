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
// PWM_OUT write-semantics fix (issue #70, cpp-RCP-01, second half): this
// header previously layered rcp/gpio.hpp's/rcp/endpoint.hpp's generic
// 8-way evt[2:0] bitmask/saturating-write combinator
// (rcp::endpoint::apply_bitmask_write) onto PWM_OUT's period/active pair.
// The specification's own PWM_OUT request-handling section describes only
// one write behavior: a request supplies both values and they take effect
// directly (a request with PWM_Period of 0 stops generation; the whole
// point of Add/Or/Xor-style combination — bitwise OR/AND/XOR or saturating
// add/subtract against the *previous* period/active values — is never
// described for this endpoint type, and does not obviously make sense for
// a period/duty-cycle pair the way it does for GPIO's independent pin
// bits). PwmOutEndpoint::handle_write below therefore no longer calls
// apply_bitmask_write at all: WriteSemantics::Replace applies the request
// directly, and every other semantics (Or/And/Xor/Add/Subtract/Reserved/
// Reconfigure) is rejected via the same
// EndpointErrc::non_combinable_write_semantics sentinel Reconfigure already
// used, without reusing apply_bitmask_write's combinator logic itself.
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
    if (len < kPwmPayloadLen) return avtp::make_error_code(avtp::AvtpErrc::short_buffer);
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

// ── PwmOutEndpoint (ep_type 0x07) ────────────────────────────────────────────
// handle_write applies WriteSemantics::Replace directly (the only write
// behavior the spec's PWM_OUT request-handling section describes) and
// rejects every other write semantics as non-combinable — see the header
// comment above for why this no longer reuses
// rcp::endpoint::apply_bitmask_write's generic bitmask/saturating-write
// combinator the way rcp/gpio.hpp does.
class PwmOutEndpoint {
public:
    std::error_code handle_write(endpoint::WriteSemantics op, PwmValue operand,
                                  PwmValue& out_value) noexcept {
        if (op != endpoint::WriteSemantics::Replace) {
            return endpoint::make_error_code(endpoint::EndpointErrc::non_combinable_write_semantics);
        }
        state_    = operand;
        out_value = state_;
        return {};
    }

    const PwmValue& read() const noexcept { return state_; }

private:
    PwmValue state_;
};

// ── PwmInEndpoint (ep_type 0x08) ─────────────────────────────────────────────
// Response-only read model (extraction §5.6): PWM_IN has no write request
// shape at all in this milestone's scope. record_measurement models one
// input-capture cycle completing and firing the MidPulse trigger signal
// rcp/adc.hpp's AdcCadence::ExternalTrigger pattern is expected to key
// sampling cadence off of.
enum class PwmInSignal : uint8_t { MidPulse = 0 };

constexpr endpoint::TriggerRegistry::SignalId pwm_in_signal_id(PwmInSignal sig) noexcept {
    return static_cast<endpoint::TriggerRegistry::SignalId>(sig);
}

class PwmInEndpoint {
public:
    // record_measurement records one completed input-capture cycle and
    // fires MidPulse for any armed listener.
    void record_measurement(PwmValue value) noexcept {
        last_value_ = value;
        has_signal_ = true;
        triggers_.notify(pwm_in_signal_id(PwmInSignal::MidPulse));
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
