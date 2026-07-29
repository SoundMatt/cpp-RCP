// fusa:req REQ-PWM-001
// fusa:req REQ-PWM-002
// fusa:req REQ-PWM-003
// fusa:req REQ-PWM-004
// fusa:req REQ-PWM-005
// fusa:req REQ-PWM-006
// fusa:req REQ-PWM-007

// PWM_OUT (ep_type 0x07) and PWM_IN (ep_type 0x08) endpoints — the shared
// period/active-duration two-field payload shape, PWM_OUT's 8-way
// write-semantics reuse from GPIO, PWM_IN's response-only read model and
// PWM_IN_NO_SIGNAL error path, and the mid-pulse trigger signal used to key
// ADC sampling cadence (extraction §5.5, §5.6).
//
// ROADMAP.md milestone 48, "Basic Endpoint Types II — I2C, UART, ADC,
// PWM_OUT, PWM_IN (v2.4.0)": kept in one header, mirroring how
// rcp/endpoint.hpp is shared scaffolding for rcp/gpio.hpp and rcp/spi.hpp
// rather than each having its own copy — PWM_OUT and PWM_IN share one
// payload struct (PwmValue) and differ only in read/write direction and
// trigger-signal behavior. PWM_OUT reuses rcp/endpoint.hpp's
// WriteSemantics/apply_bitmask_write directly (including the
// saturating-add/subtract rule it already implements generically), the
// same primitive rcp/gpio.hpp's GpioEndpoint builds on, rather than
// re-deriving its own combinator. PWM_OUT defines no Reconfigure target of
// its own (unlike GPIO's pin-direction retarget) — the extraction does not
// name one for PWM_OUT, so a Reconfigure write is left to
// apply_bitmask_write's own built-in rejection
// (EndpointErrc::non_combinable_write_semantics) rather than this header
// inventing a meaning for it.
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

#include <rcp/endpoint.hpp>

#include <cstdint>
#include <string>
#include <system_error>

namespace rcp {
namespace pwm {

// ── Shared period/active-duration payload shape ───────────────────────────────
// `period` and `active_duration` share one implementation-defined tick unit
// (extraction §5.5, §5.6); this header does not itself fix that unit to a
// physical time value.

struct PwmValue {
    uint32_t period          = 0;
    uint32_t active_duration = 0;
};

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
// Applies one of GPIO's 8 write semantics independently to `period` and to
// `active_duration` — each field is combined against its own current value
// (this implementation's own choice: the extraction does not itself say
// whether the two fields share one combinator application or each combines
// independently, and independent combination is what this header
// implements). Reuses rcp::endpoint::apply_bitmask_write directly, so the
// saturating add/subtract rule endpoint.hpp already implements generically
// applies to both fields without PWM_OUT re-deriving it.
class PwmOutEndpoint {
public:
    std::error_code handle_write(endpoint::WriteSemantics op, PwmValue operand,
                                  PwmValue& out_value) noexcept {
        uint32_t period_out = 0;
        auto ec = endpoint::apply_bitmask_write(op, state_.period, operand.period, period_out);
        if (ec) return ec;
        uint32_t duration_out = 0;
        ec = endpoint::apply_bitmask_write(op, state_.active_duration, operand.active_duration,
                                            duration_out);
        if (ec) return ec;
        state_.period          = period_out;
        state_.active_duration = duration_out;
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
