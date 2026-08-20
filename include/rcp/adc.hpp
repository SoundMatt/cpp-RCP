// fusa:req REQ-ADC-001
// fusa:req REQ-ADC-002
// fusa:req REQ-ADC-003
// fusa:req REQ-ADC-004
// fusa:req REQ-ADC-005
// fusa:req REQ-ADC-006
// fusa:req REQ-ADC-007
// fusa:req REQ-ADC-008
// fusa:req REQ-ADC-009

// ADC endpoint (ep_type 0x09) — the three-level averaging model
// (adc_sample_interval -> adc_avg_intervals_per_request ->
// adc_combine_avg_values), request-driven sampling only (no free-running
// push), two self-triggering cadence patterns, and an internal no-signal
// condition analogous in *purpose*, but not in TC18 error-code identity, to
// PWM_IN's PWM_IN_NO_SIGNAL (extraction §5.9).
//
// ROADMAP.md milestone 48, "Basic Endpoint Types II — I2C, UART, ADC,
// PWM_OUT, PWM_IN (v2.4.0)": ADC has no combinable write-request payload
// (it is read/request-driven only), so unlike rcp/gpio.hpp and
// rcp/pwm.hpp's PwmOutEndpoint it does not build on
// rcp/endpoint.hpp's WriteSemantics/apply_bitmask_write at all — its shared
// surface with the rest of this milestone is only the general
// request-dispatch shape (one entry point per request) and the no-signal
// error-path pattern rcp/pwm.hpp's PwmInEndpoint also implements.
//
// Sample/result width fix (issue #77, cpp-RCP-09): this header previously
// modeled ADC sample and result values as uint32_t throughout. Verified
// against the OPEN Alliance TC18 Remote Control Protocol Specification's
// ADC section (§13.7.9.1, "This endpoint type is for ADC's with a
// resolution of up to 16bits") and its response-frame figures (Figure 32,
// Figure 34), which are explicit about "16 bit ADC value" fields: ADC
// samples and results are 16-bit quantities, not 32-bit. compute_average
// and AdcEndpoint::request_reading/request_reading_from_trigger_queue below
// operate on uint16_t samples/results (widening only internally, to a
// 64-bit accumulator, to sum without overflow — the averaged *result*
// itself is always narrowed back to uint16_t, matching the wire width).
//
// No-signal error-identity fix (issue #77, cpp-RCP-09): this header
// previously rendered AdcErrc::no_signal's message as literally containing
// "ADC_NO_SIGNAL", implying a TC18-defined numeric error code by that name.
// Checked against the spec's Table 27 ("Error codes in responses",
// §12.9.6): there is no ADC-specific no-signal entry there at all — the
// only "*_NO_SIGNAL" entry in the whole table is PWM_IN_NO_SIGNAL (value 9),
// which belongs to the PWM_IN endpoint type, not ADC. AdcErrc::no_signal
// below is now documented and rendered as a plain internal library
// condition with no claimed spec error-code identity; it remains
// conceptually parallel to PWM_IN's no-signal handling (a request that
// cannot produce a valid sample) without pretending to be, or to map to,
// any numbered TC18 error code.
//
// Table 30/33 Row 2 evt[2:0] validation (post-v2.4.0, second endpoint type
// after I2C): AdcEndpoint::handle_request is this header's own wiring of
// rcp::endpoint::evt_row2_kind_of — the shared 3-way evt[2:0] classifier for
// Table 33's {ADC, PWM_IN, I2C, LIN, CAN, UART, ISELED, MDIO} row — into
// ADC's request decode, following the exact shape rcp/i2c.hpp's
// I2cEndpoint::handle_request established as the pilot. ADC is in fact the
// row's own worked example (§13.7.9.3 Figure 34, "RC Client sends a standard
// read request", evt field all-zero) that resolved the row's evt=000b
// textual ambiguity in the first place — see rcp/endpoint.hpp's own "RESOLVED
// AMBIGUITY" comment on evt_row2_kind_of, which cites this exact section.
// Plain (evt[2:0]==000b) delegates to request_reading — the SelfTimed
// cadence pattern below, ADC's on-demand "standard read request" behavior,
// matching Figure 34 — not to request_reading_from_trigger_queue, whose
// ExternalTrigger cadence remains a separate entry point this milestone does
// not fold into handle_request. Reserved (001b-110b) is rejected with
// endpoint::EndpointErrc::reserved_evt_row2. ConfigWrite (evt[2:0]==111b,
// §12.7.1) is reported as AdcErrc::config_write_not_supported rather than
// crashing or silently accepted as a plain read — ADC has no EP_functional-
// config wiring in this codebase yet (same gap I2C's own handle_request
// comment calls out for I2C), so full §12.7.1 handling is out of scope here
// too.
//
// Field names and behavior below implement TC18's *behavior* as described in
// an internal structured extraction of the specification named above; no
// text from that document is reproduced here. The concrete averaging
// combinator (arithmetic mean at both levels), cadence-pattern API shape,
// and single-value wire codec (encode_adc_value) chosen in this file are
// this implementation's own, same as the equivalent disclaimers in
// rcp/avtp.hpp, rcp/regmap.hpp, rcp/endpoint.hpp, rcp/gpio.hpp, rcp/spi.hpp,
// and rcp/i2c.hpp.
#pragma once

#include <rcp/avtp.hpp>
#include <rcp/endpoint.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
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
    no_signal                = 1, // internal condition: no valid sample before timeout/underrun — not a TC18 error code (see header comment; Table 27 has no ADC-specific no-signal entry)
    invalid_averaging_config = 2, // adc_avg_intervals_per_request or adc_combine_avg_values is 0
    // evt_row2_kind_of classified the request as ConfigWrite (evt[2:0] ==
    // 111b, §12.7.1). This milestone deliberately does not implement the
    // configuration-write shape (relative EP_functional-config start
    // address + configuration data) — see AdcEndpoint::handle_request's own
    // comment. Reported explicitly rather than silently accepted as a plain
    // read or silently ignored, same as I2C's I2cErrc::config_write_not_supported.
    config_write_not_supported = 3,
};

inline const std::error_category& adc_category() noexcept {
    struct Cat : std::error_category {
        const char* name() const noexcept override { return "rcp.adc"; }
        std::string message(int ev) const override {
            switch (static_cast<AdcErrc>(ev)) {
            case AdcErrc::no_signal:
                return "rcp/adc: no signal — no valid sample captured (an internal condition; "
                       "TC18's Table 27 defines no ADC-specific no-signal error code)";
            case AdcErrc::invalid_averaging_config:
                return "rcp/adc: adc_avg_intervals_per_request/adc_combine_avg_values must be >= 1";
            case AdcErrc::config_write_not_supported:
                return "rcp/adc: evt[2:0]=111b configuration-write requests are not yet implemented";
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

// ── Wire codec ────────────────────────────────────────────────────────────────
// A single measurement value is a 16-bit big-endian "ADC value" field on the
// wire (§13.7.9.3's own field labels, Figure 34/Figure 35), reusing
// rcp/avtp.hpp's put_u16 rather than re-deriving byte order here, the same
// discipline rcp/pwm.hpp's encode_pwm_payload and rcp/gpio.hpp's
// encode_gpio_payload already follow. Only a single scripted-value encoding
// is provided — the multi-measurement (readsize > 2, Figure 35) response
// shape a real averaging/combine-count response can carry is out of scope
// for this milestone, same "data model, not full wire codec" scope split
// documented in rcp/regmap.hpp.
constexpr size_t kAdcValueLen = sizeof(uint16_t);

inline std::vector<uint8_t> encode_adc_value(uint16_t value) {
    std::vector<uint8_t> buf(kAdcValueLen);
    avtp::detail::put_u16(buf.data(), value);
    return buf;
}

// ── Level 1 / level 2 combinators ─────────────────────────────────────────────
// Both levels of the averaging model use the same arithmetic-mean
// combinator in this implementation — the extraction does not mandate a
// different combination function per level, so reusing one keeps the two
// levels' behavior obviously consistent with each other. Samples and the
// averaged result are 16-bit (the spec's ADC resolution ceiling, §13.7.9.1);
// summation widens internally to avoid overflow but the result is narrowed
// back to uint16_t.

inline std::error_code compute_average(const std::vector<uint16_t>& samples, uint16_t& out_avg) noexcept {
    if (samples.empty()) return make_error_code(AdcErrc::no_signal);
    uint64_t sum = 0;
    for (const uint16_t s : samples) sum += s;
    out_avg = static_cast<uint16_t>(sum / samples.size());
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
    // this endpoint's analog of PWM_IN's no-signal handling on the request
    // path (see header comment: not itself a TC18 error code).
    template <typename SampleFn>
    std::error_code request_reading(const AdcAveragingConfig& cfg, SampleFn take_sample,
                                     uint16_t& out_value) {
        if (cfg.adc_avg_intervals_per_request == 0 || cfg.adc_combine_avg_values == 0)
            return make_error_code(AdcErrc::invalid_averaging_config);

        std::vector<uint16_t> averaged_intervals;
        averaged_intervals.reserve(cfg.adc_combine_avg_values);
        for (uint16_t i = 0; i < cfg.adc_combine_avg_values; ++i) {
            std::vector<uint16_t> raw;
            raw.reserve(cfg.adc_avg_intervals_per_request);
            for (uint16_t j = 0; j < cfg.adc_avg_intervals_per_request; ++j) {
                std::optional<uint16_t> sample = take_sample();
                if (!sample.has_value()) return make_error_code(AdcErrc::no_signal);
                raw.push_back(*sample);
            }
            uint16_t interval_avg = 0;
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
        const AdcAveragingConfig& cfg, std::vector<std::optional<uint16_t>>& triggered_samples,
        uint16_t& out_value) {
        if (cfg.adc_avg_intervals_per_request == 0 || cfg.adc_combine_avg_values == 0)
            return make_error_code(AdcErrc::invalid_averaging_config);

        const size_t needed =
            static_cast<size_t>(cfg.adc_avg_intervals_per_request) * cfg.adc_combine_avg_values;
        if (triggered_samples.size() < needed) return make_error_code(AdcErrc::no_signal);

        std::vector<uint16_t> averaged_intervals;
        averaged_intervals.reserve(cfg.adc_combine_avg_values);
        size_t cursor = 0;
        for (uint16_t i = 0; i < cfg.adc_combine_avg_values; ++i) {
            std::vector<uint16_t> raw;
            raw.reserve(cfg.adc_avg_intervals_per_request);
            for (uint16_t j = 0; j < cfg.adc_avg_intervals_per_request; ++j, ++cursor) {
                if (!triggered_samples[cursor].has_value()) return make_error_code(AdcErrc::no_signal);
                raw.push_back(*triggered_samples[cursor]);
            }
            uint16_t interval_avg = 0;
            auto ec = compute_average(raw, interval_avg);
            if (ec) return ec;
            averaged_intervals.push_back(interval_avg);
        }
        triggered_samples.erase(triggered_samples.begin(),
                                 triggered_samples.begin() + static_cast<long>(needed));
        return compute_average(averaged_intervals, out_value);
    }

    // handle_request is ADC's request-decode entry point — the piece this
    // header previously had none of, mirroring rcp::i2c::I2cEndpoint::
    // handle_request's shape exactly (this repo's second Table 33 Row 2
    // endpoint type after I2C). It classifies the incoming request's
    // evt[2:0] field via rcp::endpoint::evt_row2_kind_of before doing
    // anything else, so a Reserved value can never reach request_reading and
    // be misread as an ordinary read, and a ConfigWrite value can never be
    // silently accepted or silently dropped:
    //   - Plain (evt[2:0] == 000b): delegates to request_reading (the
    //     SelfTimed cadence pattern above) with `cfg`/`take_sample`/
    //     `out_value` unchanged — ADC's "standard read request" behavior,
    //     matching §13.7.9.3 Figure 34's own worked example (the same
    //     figure rcp/endpoint.hpp's evt_row2_kind_of cites to resolve Table
    //     33's evt=000b ambiguity in the first place). This does not
    //     delegate to request_reading_from_trigger_queue — the
    //     ExternalTrigger cadence pattern remains its own separate entry
    //     point, not folded into handle_request by this milestone.
    //   - Reserved (evt[2:0] in 001b-110b): returns
    //     endpoint::EndpointErrc::reserved_evt_row2 without invoking
    //     `take_sample` or touching `out_value` — TC18 requires this be
    //     rejected with error code UNSUPPORTED_CMD.
    //   - ConfigWrite (evt[2:0] == 111b): §12.7.1's configuration-write
    //     shape targets the ADC EP's own functional-config block (relative
    //     start address + configuration data), not a sampling read at all.
    //     Full handling is deliberately out of scope for this milestone
    //     (nontrivial — it needs EP_functional-config wiring this header
    //     does not yet have, the same gap I2C's own handle_request defers
    //     for the identical reason); this returns
    //     AdcErrc::config_write_not_supported rather than crashing, silently
    //     accepting the request as a read, or silently doing nothing.
    template <typename SampleFn>
    std::error_code handle_request(uint8_t evt_op, const AdcAveragingConfig& cfg, SampleFn take_sample,
                                    uint16_t& out_value) {
        switch (endpoint::evt_row2_kind_of(evt_op)) {
        case endpoint::EvtRow2Kind::Plain:
            return request_reading(cfg, std::move(take_sample), out_value);
        case endpoint::EvtRow2Kind::Reserved:
            return endpoint::make_error_code(endpoint::EndpointErrc::reserved_evt_row2);
        case endpoint::EvtRow2Kind::ConfigWrite:
            return make_error_code(AdcErrc::config_write_not_supported);
        }
        return endpoint::make_error_code(endpoint::EndpointErrc::reserved_evt_row2); // unreachable
    }
};

} // namespace adc
} // namespace rcp

// Enable std::error_code construction from rcp::adc::AdcErrc.
namespace std {
template <>
struct is_error_code_enum<rcp::adc::AdcErrc> : true_type {};
} // namespace std
