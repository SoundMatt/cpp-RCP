// fusa:req REQ-ADC-001
// fusa:req REQ-ADC-002
// fusa:req REQ-ADC-003
// fusa:req REQ-ADC-004
// fusa:req REQ-ADC-005
// fusa:req REQ-ADC-006

// ADC endpoint (ep_type 0x09) — the three-level averaging model
// (adc_sample_interval -> adc_avg_intervals_per_request ->
// adc_combine_avg_values), request-driven sampling only (no free-running
// push), two self-triggering cadence patterns, and a no-signal timeout
// handling path analogous to PWM_IN's PWM_IN_NO_SIGNAL error (extraction
// §5.9).
//
// ROADMAP.md milestone 48, "Basic Endpoint Types II — I2C, UART, ADC,
// PWM_OUT, PWM_IN (v2.4.0)": ADC has no combinable write-request payload
// (it is read/request-driven only), so unlike rcp/gpio.hpp and
// rcp/pwm.hpp's PwmOutEndpoint it does not build on
// rcp/endpoint.hpp's WriteSemantics/apply_bitmask_write at all — its shared
// surface with the rest of this milestone is only the general
// request-dispatch shape (one entry point per request) and the no-signal
// error-path pattern rcp/pwm.hpp's PwmInEndpoint also implements
// (PwmErrc::no_signal / AdcErrc::no_signal are deliberately parallel).
//
// Three-level averaging model (extraction §5.9), as implemented below:
//   1. adc_sample_interval:            spacing between individual raw
//                                        samples (a caller/clock concern —
//                                        this header does not itself model
//                                        wall-clock time, see AdcCadence)
//   2. adc_avg_intervals_per_request:  how many adc_sample_interval-spaced
//                                        raw samples are averaged into one
//                                        "averaged-interval" value
//   3. adc_combine_avg_values:         how many averaged-interval values
//                                        are further combined into the
//                                        single value returned to the
//                                        request
//
// Two self-triggering cadence patterns (extraction §5.9), as implemented
// below: SelfTimed, where the caller supplies raw samples already paced by
// adc_sample_interval (e.g. a wall-clock-driven sampler), and
// ExternalTrigger, where raw samples are instead paced by an external
// trigger-signal occurrence — the roadmap's own example being rcp/pwm.hpp's
// PwmInEndpoint mid-pulse signal keying ADC sampling cadence.
//
// Field names and behavior below implement TC18's *behavior* as described in
// an internal structured extraction of the specification named above; no
// text from that document is reproduced here. The concrete averaging
// combinator (arithmetic mean at both levels) and cadence-pattern API shape
// chosen in this file are this implementation's own, same as the equivalent
// disclaimers in rcp/wire.hpp, rcp/regmap.hpp, rcp/endpoint.hpp,
// rcp/gpio.hpp, and rcp/spi.hpp.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace rcp {
namespace adc {

// ── Averaging configuration ───────────────────────────────────────────────────

struct AdcAveragingConfig {
    uint32_t adc_sample_interval_us      = 0; // level 1 pacing; not enforced by this header, see AdcCadence
    uint16_t adc_avg_intervals_per_request = 1; // level 1 sample count per averaged-interval value
    uint16_t adc_combine_avg_values        = 1; // level 2 averaged-interval count per request result
};

// ── Cadence pattern selector ──────────────────────────────────────────────────

enum class AdcCadence : uint8_t {
    SelfTimed       = 0, // raw samples paced internally by adc_sample_interval (caller-supplied pacing)
    ExternalTrigger = 1, // raw samples paced by an external trigger signal occurrence
};

// ── Errors ────────────────────────────────────────────────────────────────────

enum class AdcErrc : int {
    no_signal                = 1, // analogous to PWM_IN_NO_SIGNAL: no valid sample before timeout/underrun
    invalid_averaging_config = 2, // adc_avg_intervals_per_request or adc_combine_avg_values is 0
};

inline const std::error_category& adc_category() noexcept {
    struct Cat : std::error_category {
        const char* name() const noexcept override { return "rcp.adc"; }
        std::string message(int ev) const override {
            switch (static_cast<AdcErrc>(ev)) {
            case AdcErrc::no_signal:
                return "rcp/adc: no signal — no valid sample captured (ADC_NO_SIGNAL)";
            case AdcErrc::invalid_averaging_config:
                return "rcp/adc: adc_avg_intervals_per_request/adc_combine_avg_values must be >= 1";
            default:
                return "rcp/adc: unknown error";
            }
        }
    };
    static Cat instance;
    return instance;
}

inline std::error_code make_error_code(AdcErrc e) noexcept {
    return {static_cast<int>(e), adc_category()};
}

// ── Level 1 / level 2 combinators ─────────────────────────────────────────────
// Both levels of the averaging model use the same arithmetic-mean
// combinator in this implementation — the extraction does not mandate a
// different combination function per level, so reusing one keeps the two
// levels' behavior obviously consistent with each other.

inline std::error_code compute_average(const std::vector<uint32_t>& samples, uint32_t& out_avg) noexcept {
    if (samples.empty()) return make_error_code(AdcErrc::no_signal);
    uint64_t sum = 0;
    for (const uint32_t s : samples) sum += s;
    out_avg = static_cast<uint32_t>(sum / samples.size());
    return {};
}

// ── AdcEndpoint ───────────────────────────────────────────────────────────────
// Request-driven sampling only (extraction §5.9): a reading only happens as
// the direct result of one of the request_reading_* calls below — there is
// no free-running/self-pushed sample stream modeled anywhere in this
// header.
class AdcEndpoint {
public:
    // request_reading implements the SelfTimed cadence pattern: `take_sample`
    // is invoked synchronously once per required raw sample (already paced
    // by adc_sample_interval on the caller's side, e.g. by a wall-clock
    // driven sampler) and must return std::nullopt to report a failed/
    // missing capture, which this function surfaces as AdcErrc::no_signal —
    // this endpoint's analog of PWM_IN's PWM_IN_NO_SIGNAL on the request
    // path.
    template <typename SampleFn>
    std::error_code request_reading(const AdcAveragingConfig& cfg, SampleFn take_sample,
                                     uint32_t& out_value) {
        if (cfg.adc_avg_intervals_per_request == 0 || cfg.adc_combine_avg_values == 0)
            return make_error_code(AdcErrc::invalid_averaging_config);

        std::vector<uint32_t> averaged_intervals;
        averaged_intervals.reserve(cfg.adc_combine_avg_values);
        for (uint16_t i = 0; i < cfg.adc_combine_avg_values; ++i) {
            std::vector<uint32_t> raw;
            raw.reserve(cfg.adc_avg_intervals_per_request);
            for (uint16_t j = 0; j < cfg.adc_avg_intervals_per_request; ++j) {
                std::optional<uint32_t> sample = take_sample();
                if (!sample.has_value()) return make_error_code(AdcErrc::no_signal);
                raw.push_back(*sample);
            }
            uint32_t interval_avg = 0;
            auto ec = compute_average(raw, interval_avg);
            if (ec) return ec;
            averaged_intervals.push_back(interval_avg);
        }
        return compute_average(averaged_intervals, out_value);
    }

    // request_reading_from_trigger_queue implements the ExternalTrigger
    // cadence pattern: raw samples are not pulled on demand but have
    // already been captured once per external trigger occurrence (e.g.
    // rcp/pwm.hpp's PwmInEndpoint mid-pulse signal) and queued by the
    // caller in `triggered_samples`, oldest first. This function consumes
    // exactly enough entries to satisfy the averaging config, reporting
    // AdcErrc::no_signal on either a std::nullopt entry (a trigger occurred
    // with no valid capture) or a queue underrun (fewer trigger occurrences
    // than required).
    std::error_code request_reading_from_trigger_queue(
        const AdcAveragingConfig& cfg, std::vector<std::optional<uint32_t>>& triggered_samples,
        uint32_t& out_value) {
        if (cfg.adc_avg_intervals_per_request == 0 || cfg.adc_combine_avg_values == 0)
            return make_error_code(AdcErrc::invalid_averaging_config);

        const size_t needed =
            static_cast<size_t>(cfg.adc_avg_intervals_per_request) * cfg.adc_combine_avg_values;
        if (triggered_samples.size() < needed) return make_error_code(AdcErrc::no_signal);

        std::vector<uint32_t> averaged_intervals;
        averaged_intervals.reserve(cfg.adc_combine_avg_values);
        size_t cursor = 0;
        for (uint16_t i = 0; i < cfg.adc_combine_avg_values; ++i) {
            std::vector<uint32_t> raw;
            raw.reserve(cfg.adc_avg_intervals_per_request);
            for (uint16_t j = 0; j < cfg.adc_avg_intervals_per_request; ++j, ++cursor) {
                if (!triggered_samples[cursor].has_value()) return make_error_code(AdcErrc::no_signal);
                raw.push_back(*triggered_samples[cursor]);
            }
            uint32_t interval_avg = 0;
            auto ec = compute_average(raw, interval_avg);
            if (ec) return ec;
            averaged_intervals.push_back(interval_avg);
        }
        triggered_samples.erase(triggered_samples.begin(),
                                 triggered_samples.begin() + static_cast<long>(needed));
        return compute_average(averaged_intervals, out_value);
    }
};

} // namespace adc
} // namespace rcp

// Enable std::error_code construction from rcp::adc::AdcErrc.
namespace std {
template <>
struct is_error_code_enum<rcp::adc::AdcErrc> : true_type {};
} // namespace std
