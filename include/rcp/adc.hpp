// fusa:req REQ-ADC-001
// fusa:req REQ-ADC-002
// fusa:req REQ-ADC-003
// fusa:req REQ-ADC-004
// fusa:req REQ-ADC-005
// fusa:req REQ-ADC-006
// fusa:req REQ-ADC-007
// fusa:req REQ-ADC-008
// fusa:req REQ-ADC-009
// fusa:req REQ-ADC-010
// fusa:req REQ-ADC-011
// fusa:req REQ-ADC-012
// fusa:req REQ-ADC-013
// fusa:req REQ-ADC-014
// fusa:req REQ-ADC-015
// fusa:req REQ-ADC-016
// fusa:req REQ-ADC-017
// fusa:req REQ-ADC-018
// fusa:req REQ-ADC-019
// fusa:req REQ-ADC-020
// fusa:req REQ-ADC-021
// fusa:req REQ-ADC-022
// fusa:req REQ-ADC-023
// fusa:req REQ-ADC-024
// fusa:req REQ-ADC-025
// fusa:req REQ-ADC-026
// fusa:req REQ-ADC-027
// fusa:req REQ-ADC-028
// fusa:req REQ-ADC-029
// fusa:req REQ-ADC-030
// fusa:req REQ-ADC-031
// fusa:req REQ-ADC-032
// fusa:req REQ-ADC-033
// fusa:req REQ-ADC-034
// fusa:req REQ-ADC-035
// fusa:req REQ-ADC-036
// fusa:req REQ-ADC-037
// fusa:req REQ-ADC-038
// fusa:req REQ-ADC-039
// fusa:req REQ-ADC-040
// fusa:req REQ-ADC-041
// fusa:req REQ-ADC-042
// fusa:req REQ-ADC-043
// fusa:req REQ-ADC-044
// fusa:req REQ-ADC-045
// fusa:req REQ-ADC-046
// fusa:req REQ-ADC-047
// fusa:req REQ-ADC-048
// fusa:req REQ-ADC-049
// fusa:req REQ-ADC-050
// fusa:req REQ-ADC-051
// fusa:req REQ-ADC-052
// fusa:req REQ-ADC-053
// fusa:req REQ-ADC-054
// fusa:req REQ-ADC-055

// ADC endpoint (ep_type 0x09) — the three-layer sampling model
// (adc_samples_per_avg_interval -> adc_avg_intervals_per_request ->
// adc_combine_avg_values), a real ACF-level wire codec, the EP_func
// functional-configuration register block (evt[2:0] == 111b), per-endpoint
// threshold/measurement-finished triggers (Table 53), and wall-clock
// inter-sample spacing validation (§13.7.9.1), request-driven sampling only
// (no free-running push) (extraction §5.9).
//
// ROADMAP.md Phase 17 / cpp-RCP issue #129, Phase 3 ("Per-endpoint modules"):
// this header is re-derived from c-RCP's ep_adc.h/ep_adc.c — c-RCP's
// RC5-conformant reference implementation for this endpoint type — rather
// than incrementally patched, per the roadmap's own module-by-module
// rewrite plan. No text from the OPEN Alliance TC18 Remote Control Protocol
// Specification is reproduced here; field names and behavior below
// implement TC18's *behavior* as ported from c-RCP's own implementation of
// an internal structured extraction of the specification.
//
// CORRECTED (Phase 3 content-parity pass): this header's pre-Phase-3
// content (ROADMAP.md milestone 48, v2.4.0, later relabeled the "Table 30/33
// Row 2 evt[2:0] validation" pilot module) modeled adc_combine_avg_values as
// a SECOND arithmetic-mean reduction — AdcEndpoint::request_reading averaged
// `adc_avg_intervals_per_request` per-interval means down to one single
// out_value. c-RCP's own file header records that this exact mistake was
// already made and fixed once in c-RCP's own history: "An earlier revision
// of this module modelled adc_combine_avg_values as a four-way
// AVERAGE/MIN/MAX/LATEST mode enum collapsing every averaged value into one
// 2-octet response payload; that has no basis in the register table, which
// defines the field as the number of output values to be combined into one
// response, and it made every response exactly one value wide where a
// conforming peer expects N." This header's own pre-Phase-3 model was a
// different-shaped instance of the identical bug (silently averaging N
// output values into 1 instead of packing all N verbatim into a multi-value
// response) — re-verified against c-RCP's *current* ep_adc.c/.h rather than
// assumed accurate, exactly per this phase's own task instructions. Fixed
// below: `average_interval` (layer 1) and `collect_response_values`
// (layers 2/3, a packing operation, never a reduction) are now direct,
// signature-faithful ports of rcp_ep_adc_average_interval()/
// rcp_ep_adc_collect_response_values(); `AdcEndpoint`'s old
// request_reading()/request_reading_from_trigger_queue()/handle_request()
// (which could only ever produce one out_value per call, and which
// conflated the field-naming: the old `AdcAveragingConfig::
// adc_avg_intervals_per_request` was actually used as a *layer-1
// samples-per-interval* count, c-RCP's `adc_samples_per_avg_interval`'s own
// role, not layer 2's real "intervals captured per measurement cycle" role)
// are replaced by AdcEndpoint::execute_measurement_cycle()/response_ready()/
// collect_response(), which drive the corrected three-layer pipeline and
// produce a real N-value response array. The former AdcErrc::
// invalid_averaging_config and AdcErrc::config_write_not_supported are
// removed: c-RCP validates neither adc_samples_per_avg_interval nor
// adc_avg_intervals_per_request (a 0 there simply yields kAdcNoSignal, the
// same fail-safe-over-rejection disposition average_interval() already uses
// for an empty sample set), and the evt[2:0]==111b configuration-write path
// this header previously stubbed out is now genuinely implemented via
// apply_reconfig() below (REQ-ADC-038/039), closing that gap rather than
// reporting it as out of scope.
//
// No-signal sentinel: c-RCP reuses ep_pwm.h's RCP_EP_PWM_IN_NO_SIGNAL
// (0xFFFF) as ADC's own raw-sample/averaged-value timeout sentinel, so a
// "no valid measurement" value means the same numeric thing across both
// endpoint types sharing that milestone. cpp-RCP's own rcp/pwm.hpp models
// PWM_IN's no-signal condition as a std::error_code (PwmErrc::no_signal)
// rather than a reusable numeric constant, so this header defines its own
// `kAdcNoSignal` at the identical wire value (0xFFFF) instead of depending
// on a pwm.hpp constant that does not exist — same numeric identity as
// c-RCP's choice, own local definition per this codebase's own idiom.
// AdcErrc::no_signal (an actual error, distinct from the kAdcNoSignal
// sentinel value) no longer claims a TC18-defined error-code identity —
// Table 27 defines no ADC-specific no-signal code — matching this file's
// pre-Phase-3 finding (issue #77, cpp-RCP-09), which remains correct and is
// preserved below.
//
// Sample/result width (issue #77, cpp-RCP-09, preserved): ADC samples and
// results are 16-bit quantities (§13.7.9.1, "up to 16bits"; Figure 32/34's
// "16 bit ADC value" fields) — RCP_EP_ADC_VALUE_LEN in c-RCP, kAdcValueLen
// here.
#pragma once

#include <rcp/acf.hpp>
#include <rcp/avtp.hpp>
#include <rcp/endpoint.hpp>
#include <rcp/lifecycle.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace rcp {
namespace adc {

// ── No-signal sentinel & response geometry ────────────────────────────────────
// See the file header for kAdcNoSignal's derivation. A response's payload is
// exactly value_count * kAdcValueLen octets long -- N values, never one.

constexpr uint16_t kAdcNoSignal = 0xFFFF;
constexpr size_t   kAdcValueLen = sizeof(uint16_t);

// The largest number of measurement values one response can carry, derived
// from acf.hpp's own ACF_ABB payload ceiling -- the same bound c-RCP derives
// from RCP_ACF_MAX_PAYLOAD for RCP_EP_ADC_MAX_VALUES.
constexpr size_t kAdcMaxResponseValues = acf::kAcfAbbMaxPayload / kAdcValueLen;

// adc_combine_avg_values (Table 54) is an 8-bit register field, so no
// AdcEndpoint measurement cycle can ever need to hold more than 255 pending
// per-interval averages before a response can be assembled -- the bound
// AdcEndpoint's own fixed-capacity accumulator (below) is sized against,
// matching Phase 1/2's std::array-backed fixed-capacity convention (see
// rcp/request.hpp's detail::BoundedVector).
constexpr size_t kAdcMaxCombineValues = 255;

// response_value_count returns the number of measurement values a response
// to a request carrying read_size is expected to contain: half the
// read_size, since each value occupies kAdcValueLen octets (extraction
// §5.9.3). Returns 0 for an odd read_size, which cannot describe a whole
// number of values.
inline size_t response_value_count(uint16_t read_size) noexcept {
    if ((read_size % static_cast<uint16_t>(kAdcValueLen)) != 0) return 0;
    return static_cast<size_t>(read_size) / kAdcValueLen;
}

// ── Layer 1: adc_samples_per_avg_interval ─────────────────────────────────────

// One raw ADC sample: value is kAdcNoSignal iff this particular sample did
// not complete within its timeout window; timestamp is the moment this
// sample was captured (or attempted) -- an attempted-but-timed-out sample is
// still a real capture attempt at a real moment (see
// validate_sample_spacing's own doc comment below).
struct AdcSample {
    uint16_t value     = 0;
    uint64_t timestamp = 0;
};

// One averaging interval's result: value is the arithmetic mean (rounded
// down) of every non-kAdcNoSignal sample, or kAdcNoSignal itself iff every
// sample in this interval timed out (or the interval was empty); timestamp
// is the capture moment of the LAST sample that contributed to that mean --
// or, when none did, of the last sample in the interval (0 iff the interval
// was empty).
struct AdcAvgValue {
    uint16_t value     = 0;
    uint64_t timestamp = 0;
};

// average_interval computes one averaging interval's AdcAvgValue from raw
// samples -- layer 1 of the three-layer averaging model (see the file
// header). The capture moment reported is the end of the averaging window
// (the last sample that fed the mean), not its start (extraction §5.9.2) --
// this function has no awareness of whether that spacing is actually
// uniform; see validate_sample_spacing below for the dedicated spacing
// check.
inline AdcAvgValue average_interval(const std::vector<AdcSample>& samples) noexcept {
    AdcAvgValue result;
    if (samples.empty()) {
        result.value     = kAdcNoSignal;
        result.timestamp = 0;
        return result;
    }

    uint64_t sum     = 0;
    size_t   counted = 0;
    for (const auto& s : samples) {
        if (s.value == kAdcNoSignal) continue;
        sum += s.value;
        ++counted;
    }

    // The capture moment of the LAST sample that actually fed the mean; when
    // no sample was usable at all, the interval's last sample still marks
    // the moment the window closed.
    result.timestamp = samples.back().timestamp;
    for (size_t i = samples.size(); i > 0; --i) {
        if (samples[i - 1].value == kAdcNoSignal) continue;
        result.timestamp = samples[i - 1].timestamp;
        break;
    }

    result.value = (counted == 0) ? kAdcNoSignal : static_cast<uint16_t>(sum / counted);
    return result;
}

// ── REQ-ADC-033: inter-sample spacing, validated against a real caller- ──────
// supplied clock rate ─────────────────────────────────────────────────────────

enum class AdcSpacingResult : uint8_t {
    Ok        = 0, // every consecutive pair within tolerance, or too little
                    // information to check (fewer than 2 samples,
                    // base_clk_hz == 0, or base_clk_divider == 0)
    Violation = 1, // at least one consecutive pair was not spaced within
                    // tolerance of the configured interval, OR two
                    // consecutive timestamps were not monotonically increasing
};

// TC18 §13.7.9.1's own inter-sample-spacing rule: successive samples one
// adc_sample_interval apart, that interval expressed in multiples of
// ADC_CLK cycles, where ADC_CLK = adc_base_clk / adc_base_clk_divider.
// adc_base_clk itself is never modelled by this module (no real clock
// source) -- base_clk_hz is the caller's own real oscillator frequency in
// Hz, supplied directly, the same "this library never invents wall time or
// a clock rate itself" discipline rcp/request.hpp's own presentation-time
// admission uses for its caller-supplied gptp reference.
//
// expected_spacing_ns = sample_interval * base_clk_divider * 1e9 /
// base_clk_hz. tolerance_ns widens that expectation by +/- tolerance_ns on
// each side to absorb real capture jitter; 0 demands exact spacing.
//
// Fails open (AdcSpacingResult::Ok) when there is too little information to
// check at all (fewer than 2 samples, base_clk_hz == 0, or
// base_clk_divider == 0) -- the same disposition average_interval() uses
// for an empty sample set. Otherwise reports Violation on the first
// consecutive pair whose timestamps are not monotonically increasing, or
// whose spacing falls outside [expected - tolerance, expected + tolerance]
// -- does not continue checking past the first violation.
inline AdcSpacingResult validate_sample_spacing(const std::vector<AdcSample>& samples,
                                                 uint8_t base_clk_divider, uint8_t sample_interval,
                                                 uint32_t base_clk_hz, uint64_t tolerance_ns) noexcept {
    if (samples.size() < 2 || base_clk_hz == 0 || base_clk_divider == 0) {
        return AdcSpacingResult::Ok;
    }

    const uint64_t expected_ns = static_cast<uint64_t>(sample_interval) *
                                  static_cast<uint64_t>(base_clk_divider) * 1000000000ULL / base_clk_hz;

    const uint64_t lo = (expected_ns > tolerance_ns) ? (expected_ns - tolerance_ns) : 0;
    const uint64_t hi = (expected_ns <= UINT64_MAX - tolerance_ns) ? (expected_ns + tolerance_ns)
                                                                    : UINT64_MAX;

    for (size_t i = 0; i + 1 < samples.size(); ++i) {
        if (samples[i + 1].timestamp < samples[i].timestamp) return AdcSpacingResult::Violation;
        const uint64_t actual_ns = samples[i + 1].timestamp - samples[i].timestamp;
        if (actual_ns < lo || actual_ns > hi) return AdcSpacingResult::Violation;
    }

    return AdcSpacingResult::Ok;
}

// ── Layers 2/3: adc_avg_intervals_per_request + adc_combine_avg_values ───────
// adc_combine_avg_values is a COUNT of averaged output values a response
// carries, not a mode selector -- this stage packs rather than reduces (see
// the file header's own correction note).

// collect_response_values packs the first value_count layer-1 results into
// out_values, in capture order and verbatim (a kAdcNoSignal interval is
// carried through as kAdcNoSignal, never averaged away). Returns the number
// of values actually written, min(avg_values.size(), value_count).
inline size_t collect_response_values(const std::vector<AdcAvgValue>& avg_values, size_t value_count,
                                       std::vector<uint16_t>& out_values) {
    const size_t n = std::min(avg_values.size(), value_count);
    out_values.clear();
    out_values.reserve(n);
    for (size_t i = 0; i < n; ++i) out_values.push_back(avg_values[i].value);
    return n;
}

// REQ-ADC-037: which of the three documented cadence cases applies for a
// given (adc_avg_intervals_per_request, adc_combine_avg_values) pair. Pure
// function of the two register values; this module still owns no scheduling
// state (see the file header) -- it only names the decision a caller must
// make.
enum class AdcCadenceCase : uint8_t {
    Accumulate = 0, // combine > intervals: several request executions feed one response
    OneToOne   = 1, // combine == intervals: exactly one response per execution
    FanOut     = 2, // combine < intervals: one execution yields several responses
};

inline AdcCadenceCase cadence_case(uint16_t avg_intervals_per_request, uint8_t combine_avg_values) noexcept {
    if (static_cast<uint32_t>(combine_avg_values) > static_cast<uint32_t>(avg_intervals_per_request))
        return AdcCadenceCase::Accumulate;
    if (static_cast<uint32_t>(combine_avg_values) < static_cast<uint32_t>(avg_intervals_per_request))
        return AdcCadenceCase::FanOut;
    return AdcCadenceCase::OneToOne;
}

// REQ-ADC-053: true iff pending_value_count averaged values already captured
// are enough to assemble one response, i.e. pending_value_count >=
// combine_avg_values -- the one comparison underlying all three cadence
// cases. combine_avg_values == 0 always returns true (an empty response is
// trivially "ready", matching collect_response_values' own value_count == 0
// handling).
inline bool cadence_response_ready(size_t pending_value_count, uint8_t combine_avg_values) noexcept {
    return pending_value_count >= static_cast<size_t>(combine_avg_values);
}

// The last-sample-of-the-first-response-value capture-moment rule
// (extraction §5.9.2): returns avg_values[0].timestamp (0 iff avg_values is
// empty).
inline uint64_t capture_moment_timestamp(const std::vector<AdcAvgValue>& avg_values) noexcept {
    if (avg_values.empty()) return 0;
    return avg_values.front().timestamp;
}

// ── Functional config ─────────────────────────────────────────────────────────
// Flattens regmap.h's shared functional-config "common" prefix
// (ep_enable/ep_clear_req_storage/ep_req_crc_enable/ep_response_ts_enable/
// ep_suppress_response) directly into this struct's own bools, rather than
// composing rcp::regmap::EndpointFunctionalConfig as a typed member: unlike
// c-RCP's regmap.h, cpp-RCP's rcp/regmap.hpp deliberately leaves
// EndpointFunctionalConfig::data as an opaque byte blob (endpoint-type
// interpretation is out of regmap.hpp's own scope, per that header's file
// comment) -- render_registers()/apply_reconfig() below are this endpoint
// type's own full interpretation of that blob, matching gpio.hpp's existing
// encode/decode_gpio_functional_config precedent.
struct AdcFunctionalConfig {
    bool ep_enable             = false;
    bool ep_clear_req_storage  = false;
    bool ep_req_crc_enable     = false;
    bool ep_response_ts_enable = false;
    bool ep_suppress_response  = false;

    uint16_t adc_samples_per_avg_interval  = 0;
    uint16_t adc_avg_intervals_per_request = 0;
    uint8_t  adc_combine_avg_values        = 0; // COUNT of output values per response, not a mode selector
    uint16_t ep_status                     = 0; // adc_ep_status, Table 54
    uint8_t  base_clk_divider              = 0; // adc_base_clk_divider
    uint8_t  sample_interval               = 0; // adc_sample_interval
    uint8_t  resolution                    = 0; // adc_resolution, <=16
    uint16_t trigger_min                   = 0; // adc_trigger_min
    uint16_t trigger_max                   = 0; // adc_trigger_max
};

// functional_cfg_writable is a thin, named wrapper over
// rcp::lifecycle::field_writable() with FieldKind::FunctionalW, reusing --
// never duplicating -- that function's authorization logic (TC18 Table 24's
// W marker: functionally, not permanently, re-lockable once RcpConfigured).
inline bool functional_cfg_writable(lifecycle::ServerState state, lifecycle::WriterCtx writer) noexcept {
    return lifecycle::field_writable(state, lifecycle::FieldKind::FunctionalW, writer);
}

inline bool set_samples_per_avg_interval(AdcFunctionalConfig& cfg, uint16_t samples_per_interval,
                                          lifecycle::ServerState state, lifecycle::WriterCtx writer) noexcept {
    if (!functional_cfg_writable(state, writer)) return false;
    cfg.adc_samples_per_avg_interval = samples_per_interval;
    return true;
}

inline bool set_avg_intervals_per_request(AdcFunctionalConfig& cfg, uint16_t intervals_per_request,
                                           lifecycle::ServerState state, lifecycle::WriterCtx writer) noexcept {
    if (!functional_cfg_writable(state, writer)) return false;
    cfg.adc_avg_intervals_per_request = intervals_per_request;
    return true;
}

inline bool set_combine_avg_values(AdcFunctionalConfig& cfg, uint8_t combine_avg_values,
                                    lifecycle::ServerState state, lifecycle::WriterCtx writer) noexcept {
    if (!functional_cfg_writable(state, writer)) return false;
    cfg.adc_combine_avg_values = combine_avg_values;
    return true;
}

// ── Trigger outputs (Table 53), REQ-ADC-031/048-052 ───────────────────────────
// Table 53: an ADC endpoint can generate five trigger events -- 0/1 fire
// when the averaged output value falls below/rises above adc_trigger_min,
// 2/3 fire when it falls below/rises above adc_trigger_max, and 4 fires when
// the endpoint finished a measurement interval. Triggers 0-3 are
// EDGE-triggered (a transition relative to the PREVIOUS averaged value, not
// a level comparison against the current one alone) -- distinct from
// trigger 4, a pure "did a measurement just finish" signal with no
// threshold or previous-value concept at all.

constexpr uint8_t kAdcTriggerBelowMin            = 0x01; // trigger 0
constexpr uint8_t kAdcTriggerAboveMin            = 0x02; // trigger 1
constexpr uint8_t kAdcTriggerBelowMax            = 0x04; // trigger 2
constexpr uint8_t kAdcTriggerAboveMax            = 0x08; // trigger 3
constexpr uint8_t kAdcTriggerMeasurementFinished = 0x10; // trigger 4

// AdcTriggerState is this module's own small, caller-owned per-endpoint
// tracker holding the one piece of state edge detection needs -- the same
// caller-owned-data architecture rcp/pwm.hpp's PwmInEndpoint and
// rcp/endpoint.hpp's TriggerRegistry already establish for a per-endpoint
// tracker.
struct AdcTriggerState {
    bool     has_previous   = false; // false until the first evaluate() call
    uint16_t previous_value = 0;     // meaningless while has_previous is false
};

// trigger_evaluate evaluates one newly acquired averaged output value
// against trigger_min/trigger_max and updates s's own tracked previous
// value for the next call. measurement_finished is a caller-supplied bool --
// trigger 4 is not a threshold comparison at all, so this function cannot
// derive it from value alone.
//
// Triggers 0-3 fire only relative to a genuine previous value:
// BELOW_MIN/BELOW_MAX fire iff the previous value was AT OR ABOVE the
// threshold and value is strictly below it (a genuine downward crossing,
// not merely "currently below"); ABOVE_MIN/ABOVE_MAX symmetrically for an
// upward crossing. While s.has_previous is false, no edge exists to detect
// yet, so none of triggers 0-3 can fire on that call regardless of value --
// only trigger 4, which needs no previous value at all. Returns 0 (no bits
// set) iff no trigger fires.
inline uint8_t trigger_evaluate(AdcTriggerState& s, uint16_t value, uint16_t trigger_min,
                                 uint16_t trigger_max, bool measurement_finished) noexcept {
    uint8_t fired = 0;

    if (s.has_previous) {
        if (s.previous_value >= trigger_min && value < trigger_min) fired |= kAdcTriggerBelowMin;
        if (s.previous_value <= trigger_min && value > trigger_min) fired |= kAdcTriggerAboveMin;
        if (s.previous_value >= trigger_max && value < trigger_max) fired |= kAdcTriggerBelowMax;
        if (s.previous_value <= trigger_max && value > trigger_max) fired |= kAdcTriggerAboveMax;
    }

    if (measurement_finished) fired |= kAdcTriggerMeasurementFinished;

    s.previous_value = value;
    s.has_previous    = true;
    return fired;
}

// ── Errors ────────────────────────────────────────────────────────────────────

enum class AdcErrc : int {
    no_signal              = 1, // internal condition, not a TC18 error code (see file header)
    short_frame            = 2,
    bad_msg_type           = 3,
    wrong_bus               = 4,
    wrong_op                 = 5,
    bad_payload_len          = 6,
    // The response carries more measurement values than the caller's
    // out_values buffer can hold -- see decode_response().
    too_many_values           = 7,
    // evt[2:0] is not 0b000, TC18 §13.5 Table 33's only legal value for a
    // plain (non-configuration) request in ADC's endpoint-type row --
    // caller shall respond with error code UNSUPPORTED_CMD.
    bad_evt                   = 8,
    // Configuration write (evt[2:0]==111b) payload carries no address
    // prefix, or an address prefix with no data octet after it.
    reconfig_short             = 9,
    // Configuration write's start_address + data length exceeds
    // kAdcEpFuncLen -- the whole write is ignored, per the specification's
    // own rule.
    reconfig_out_of_range       = 10,
    // AdcEndpoint's own fixed-capacity pending-value accumulator (bounded
    // at kAdcMaxCombineValues, see the file header) is full -- this
    // implementation's own defensive bound, not a TC18 error code.
    pending_values_full          = 11,
    // AdcEndpoint::collect_response() was called before cadence_response_
    // ready() would report true.
    response_not_ready            = 12,
};

inline const std::error_category& adc_category() noexcept {
    struct Cat : std::error_category {
        const char* name() const noexcept override { return "rcp.adc"; }
        std::string message(int ev) const override {
            switch (static_cast<AdcErrc>(ev)) {
            case AdcErrc::no_signal:
                return "rcp/adc: no signal — no valid sample captured (an internal condition; "
                       "TC18's Table 27 defines no ADC-specific no-signal error code)";
            case AdcErrc::short_frame:      return "rcp/adc: frame too short";
            case AdcErrc::bad_msg_type:     return "rcp/adc: unexpected ACF message type";
            case AdcErrc::wrong_bus:        return "rcp/adc: wrong byte_bus_id";
            case AdcErrc::wrong_op:         return "rcp/adc: wrong ACF op";
            case AdcErrc::bad_payload_len:  return "rcp/adc: unexpected payload length";
            case AdcErrc::too_many_values:
                return "rcp/adc: more measurement values than the caller can hold";
            case AdcErrc::bad_evt:          return "rcp/adc: evt[2:0] is not 0b000";
            case AdcErrc::reconfig_short:
                return "rcp/adc: ADC configuration write has no address and data";
            case AdcErrc::reconfig_out_of_range:
                return "rcp/adc: ADC configuration write extends past the EP_func block";
            case AdcErrc::pending_values_full:
                return "rcp/adc: no room for another pending averaged value";
            case AdcErrc::response_not_ready:
                return "rcp/adc: not enough pending averaged values for a response yet";
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

// wire_error maps e to its numbered wire error code (acf::WireErrorCode),
// for a caller building an Error Response frame once a request has failed
// to decode. std::nullopt for every AdcErrc value with no numbered
// counterpart (local framing/routing outcomes a caller resolves before an
// ADC-specific Response frame would even be constructible).
inline std::optional<acf::WireErrorCode> wire_error(AdcErrc e) noexcept {
    switch (e) {
    case AdcErrc::bad_payload_len: return acf::WireErrorCode::InvalidParameter;
    case AdcErrc::bad_evt:         return acf::WireErrorCode::UnsupportedCmd;
    default:                        return std::nullopt;
    }
}

// ── The EP_func register block (evt[2:0] == 111b) ────────────────────────────
// Relative octet offsets, per Table 54. Every multi-octet register is
// big-endian. Offsets marked R are read-only: a configuration write
// covering them leaves them unchanged (see apply_reconfig()).

constexpr uint16_t kAdcRegEpLen           = 0x0000; //  8 bit, R
constexpr uint16_t kAdcRegReserved01      = 0x0001; //  8 bit, R
constexpr uint16_t kAdcRegEpEnableClr     = 0x0002; //  8 bit, R/W
constexpr uint16_t kAdcRegEpOptions       = 0x0003; //  8 bit, R/W
constexpr uint16_t kAdcRegBaseClk         = 0x0004; // 16 bit, R
constexpr uint16_t kAdcRegEpStatus        = 0x0006; // 16 bit, R/W
constexpr uint16_t kAdcRegBaseClkDivider  = 0x0008; //  8 bit, R/W
constexpr uint16_t kAdcRegSampleInterval  = 0x0009; //  8 bit, R/W
constexpr uint16_t kAdcRegAvgIntervals    = 0x000A; //  8 bit, R/W
constexpr uint16_t kAdcRegSamplesPerAvg   = 0x000B; //  8 bit, R/W
constexpr uint16_t kAdcRegCombineAvg      = 0x000C; //  8 bit, R/W
constexpr uint16_t kAdcRegResolution      = 0x000D; //  8 bit, R/W
constexpr uint16_t kAdcRegTriggerMin      = 0x000E; // 16 bit, R/W
constexpr uint16_t kAdcRegTriggerMax      = 0x0010; // 16 bit, R/W

// The block's own length in octets -- one past the last assigned offset.
constexpr uint16_t kAdcEpFuncLen       = 0x0012;
constexpr size_t   kAdcReconfigAddrLen = 2;

using AdcRegisterBlock = std::array<uint8_t, kAdcEpFuncLen>;

namespace detail {
constexpr uint8_t kAdcEnableClrBitEnable = 1u << 0;
constexpr uint8_t kAdcEnableClrBitClear  = 1u << 4;
constexpr uint8_t kAdcOptionsBitReqCrc   = 1u << 0;
constexpr uint8_t kAdcOptionsBitRespTs   = 1u << 3;
constexpr uint8_t kAdcOptionsBitSuppress = 1u << 7;

inline bool adc_reg_offset_read_only(uint16_t addr) noexcept {
    return addr == kAdcRegEpLen || addr == kAdcRegReserved01 || addr == kAdcRegBaseClk ||
           addr == static_cast<uint16_t>(kAdcRegBaseClk + 1);
}
} // namespace detail

// render_registers serializes cfg's EP_func registers into out exactly as a
// configuration *read* of the whole block would report them -- the inverse
// of apply_reconfig()'s own parse step. adc_base_clk (read-only) always
// renders 0 -- no real clock source modelled (see the file header).
inline void render_registers(const AdcFunctionalConfig& cfg, AdcRegisterBlock& out) noexcept {
    uint8_t enable_clr = 0;
    uint8_t options    = 0;
    if (cfg.ep_enable) enable_clr |= detail::kAdcEnableClrBitEnable;
    if (cfg.ep_clear_req_storage) enable_clr |= detail::kAdcEnableClrBitClear;
    if (cfg.ep_req_crc_enable) options |= detail::kAdcOptionsBitReqCrc;
    if (cfg.ep_response_ts_enable) options |= detail::kAdcOptionsBitRespTs;
    if (cfg.ep_suppress_response) options |= detail::kAdcOptionsBitSuppress;

    out[kAdcRegEpLen]        = static_cast<uint8_t>(kAdcEpFuncLen);
    out[kAdcRegReserved01]   = 0;
    out[kAdcRegEpEnableClr]  = enable_clr;
    out[kAdcRegEpOptions]    = options;
    avtp::detail::put_u16(&out[kAdcRegBaseClk], 0);
    avtp::detail::put_u16(&out[kAdcRegEpStatus], cfg.ep_status);
    out[kAdcRegBaseClkDivider] = cfg.base_clk_divider;
    out[kAdcRegSampleInterval] = cfg.sample_interval;
    // Truncated to the low octet -- these two fields are this module's own
    // wider uint16_t than Table 54's 8-bit registers.
    out[kAdcRegAvgIntervals]  = static_cast<uint8_t>(cfg.adc_avg_intervals_per_request & 0xFF);
    out[kAdcRegSamplesPerAvg] = static_cast<uint8_t>(cfg.adc_samples_per_avg_interval & 0xFF);
    out[kAdcRegCombineAvg]    = cfg.adc_combine_avg_values;
    out[kAdcRegResolution]    = cfg.resolution;
    avtp::detail::put_u16(&out[kAdcRegTriggerMin], cfg.trigger_min);
    avtp::detail::put_u16(&out[kAdcRegTriggerMax], cfg.trigger_max);
}

namespace detail {
inline void adc_parse_registers(AdcFunctionalConfig& cfg, const AdcRegisterBlock& in) noexcept {
    const uint8_t enable_clr = in[kAdcRegEpEnableClr];
    const uint8_t options    = in[kAdcRegEpOptions];

    cfg.ep_enable             = (enable_clr & kAdcEnableClrBitEnable) != 0;
    cfg.ep_clear_req_storage  = (enable_clr & kAdcEnableClrBitClear) != 0;
    cfg.ep_req_crc_enable     = (options & kAdcOptionsBitReqCrc) != 0;
    cfg.ep_response_ts_enable = (options & kAdcOptionsBitRespTs) != 0;
    cfg.ep_suppress_response  = (options & kAdcOptionsBitSuppress) != 0;

    cfg.ep_status        = avtp::detail::get_u16(&in[kAdcRegEpStatus]);
    cfg.base_clk_divider = in[kAdcRegBaseClkDivider];
    cfg.sample_interval  = in[kAdcRegSampleInterval];
    cfg.adc_avg_intervals_per_request  = in[kAdcRegAvgIntervals];
    cfg.adc_samples_per_avg_interval   = in[kAdcRegSamplesPerAvg];
    cfg.adc_combine_avg_values         = in[kAdcRegCombineAvg];
    cfg.resolution  = in[kAdcRegResolution];
    cfg.trigger_min = avtp::detail::get_u16(&in[kAdcRegTriggerMin]);
    cfg.trigger_max = avtp::detail::get_u16(&in[kAdcRegTriggerMax]);
}
} // namespace detail

// apply_reconfig applies the configuration escape hatch (evt[2:0] == 111b):
// payload is a 16-bit big-endian relative start address followed by the
// configuration data octets to write from that address onward (§12.7.1).
// Returns AdcErrc::reconfig_short when payload_len is not at least
// kAdcReconfigAddrLen + 1, and AdcErrc::reconfig_out_of_range when the
// addressed span would extend past kAdcEpFuncLen; in both cases cfg is left
// entirely unchanged. Octets of the addressed span that land on a read-only
// register are left at their current values while the rest of the span is
// still applied.
inline std::error_code apply_reconfig(AdcFunctionalConfig& cfg, const uint8_t* payload,
                                       size_t payload_len) {
    if (payload_len <= kAdcReconfigAddrLen) return make_error_code(AdcErrc::reconfig_short);

    const uint16_t start_address = avtp::detail::get_u16(payload);
    const size_t   data_len      = payload_len - kAdcReconfigAddrLen;

    if (static_cast<size_t>(start_address) + data_len > static_cast<size_t>(kAdcEpFuncLen))
        return make_error_code(AdcErrc::reconfig_out_of_range);

    AdcRegisterBlock block{};
    render_registers(cfg, block);
    for (size_t i = 0; i < data_len; ++i) {
        const auto addr = static_cast<uint16_t>(start_address + i);
        if (detail::adc_reg_offset_read_only(addr)) continue;
        block[addr] = payload[kAdcReconfigAddrLen + i];
    }
    detail::adc_parse_registers(cfg, block);
    return {};
}

// ── Wire codec ─────────────────────────────────────────────────────────────────
// Ported directly from c-RCP's ep_adc.c wire functions, using rcp/acf.hpp's
// ACF_ABB/ACF_GBB codec rather than re-deriving frame layout here.

// encode_read_request encodes an ACF_ABB read request addressed to
// byte_bus_id, with no payload -- how many measurement values it asks for
// is carried by read_size alone (extraction §5.9.3).
inline std::vector<uint8_t> encode_read_request(avtp::ByteBusId byte_bus_id, uint16_t read_size,
                                                  uint8_t transaction_num) {
    acf::AcfMessageInfo info;
    info.byte_bus_id              = byte_bus_id;
    info.op                       = false; // read
    info.read_size_or_segment_num = read_size;
    info.transaction_num          = transaction_num;
    return acf::encode_acf_abb(info, {});
}

// decode_read_request decodes and validates an ACF-level ADC read request.
// Fails with AdcErrc::short_frame / bad_msg_type / wrong_bus / wrong_op /
// bad_evt (evt[2:0] != 0b000, caller shall respond UNSUPPORTED_CMD). On
// success, out_read_size/out_transaction_num are populated.
inline std::error_code decode_read_request(const uint8_t* b, size_t len, avtp::ByteBusId expected_bus_id,
                                            uint16_t& out_read_size, uint8_t& out_transaction_num) {
    acf::AcfMessageInfo   info;
    std::vector<uint8_t>   payload;
    const auto              ec = acf::decode_acf_abb(b, len, info, payload);
    if (ec == avtp::make_error_code(avtp::AvtpErrc::short_buffer)) return make_error_code(AdcErrc::short_frame);
    if (ec) return make_error_code(AdcErrc::bad_msg_type);

    if (info.byte_bus_id != expected_bus_id) return make_error_code(AdcErrc::wrong_bus);
    if (info.op) return make_error_code(AdcErrc::wrong_op); // op=true means write
    if (endpoint::evt_row2_kind_of(info.evt_op) != endpoint::EvtRow2Kind::Plain)
        return make_error_code(AdcErrc::bad_evt);

    out_read_size       = info.read_size_or_segment_num;
    out_transaction_num = info.transaction_num;
    return {};
}

// encode_response encodes an ADC response carrying values[0..values.size())
// as a big-endian payload, echoing transaction_num and reporting
// 2*values.size() as the header's read_size. Encoded as ACF_ABB when timed
// is false; as ACF_GBB (mtv valid, message_timestamp = timestamp) when
// timed is true. Returns an empty vector if values is empty or exceeds
// kAdcMaxResponseValues.
inline std::vector<uint8_t> encode_response(avtp::ByteBusId byte_bus_id, const std::vector<uint16_t>& values,
                                             uint8_t transaction_num, bool timed, uint64_t timestamp) {
    if (values.empty() || values.size() > kAdcMaxResponseValues) return {};

    std::vector<uint8_t> payload(values.size() * kAdcValueLen);
    for (size_t i = 0; i < values.size(); ++i)
        avtp::detail::put_u16(&payload[i * kAdcValueLen], values[i]);

    acf::AcfMessageInfo info;
    info.byte_bus_id              = byte_bus_id;
    info.op                       = false; // read
    info.rsp                      = true;
    info.read_size_or_segment_num = static_cast<uint16_t>(payload.size());
    info.transaction_num          = transaction_num;

    if (timed) {
        info.mtv = true;
        return acf::encode_acf_gbb(info, timestamp, payload);
    }
    return acf::encode_acf_abb(info, payload);
}

// decode_response decodes an ADC response from either an ACF_ABB or
// ACF_GBB message (peeks the ACF message type itself, since a response's
// encoding depends on the responding endpoint's own timed/untimed choice).
// Fails with AdcErrc::short_frame / bad_msg_type / wrong_bus /
// bad_payload_len (payload absent, or not a whole number of kAdcValueLen-
// octet values) / too_many_values (more values than max_values). On
// success, out_values/out_transaction_num are populated; out_timed/
// out_timestamp report whether the message was a valid-timestamp ACF_GBB,
// and that timestamp's value (0 when !out_timed).
inline std::error_code decode_response(const uint8_t* b, size_t len, avtp::ByteBusId expected_bus_id,
                                        size_t max_values, std::vector<uint16_t>& out_values,
                                        bool& out_timed, uint64_t& out_timestamp,
                                        uint8_t& out_transaction_num) {
    uint8_t msg_type = 0;
    if (acf::peek_msg_type(b, len, msg_type)) return make_error_code(AdcErrc::short_frame);

    acf::AcfMessageInfo  info;
    std::vector<uint8_t>  payload;
    avtp::ByteBusId        bus_id = 0;
    uint8_t                 txn    = 0;

    if (msg_type == acf::kAcfMsgTypeGbb) {
        uint64_t ts = 0;
        const auto ec = acf::decode_acf_gbb(b, len, info, ts, payload);
        if (ec == avtp::make_error_code(avtp::AvtpErrc::short_buffer)) return make_error_code(AdcErrc::short_frame);
        if (ec) return make_error_code(AdcErrc::bad_msg_type);
        bus_id         = info.byte_bus_id;
        txn            = info.transaction_num;
        out_timed      = info.mtv;
        out_timestamp  = out_timed ? ts : 0;
    } else {
        const auto ec = acf::decode_acf_abb(b, len, info, payload);
        if (ec == avtp::make_error_code(avtp::AvtpErrc::short_buffer)) return make_error_code(AdcErrc::short_frame);
        if (ec) return make_error_code(AdcErrc::bad_msg_type);
        bus_id        = info.byte_bus_id;
        txn           = info.transaction_num;
        out_timed     = false;
        out_timestamp = 0;
    }

    if (bus_id != expected_bus_id) return make_error_code(AdcErrc::wrong_bus);
    if (payload.empty() || (payload.size() % kAdcValueLen) != 0)
        return make_error_code(AdcErrc::bad_payload_len);

    const size_t value_count = payload.size() / kAdcValueLen;
    if (value_count > max_values) return make_error_code(AdcErrc::too_many_values);

    out_values.clear();
    out_values.reserve(value_count);
    for (size_t i = 0; i < value_count; ++i)
        out_values.push_back(avtp::detail::get_u16(&payload[i * kAdcValueLen]));

    out_transaction_num = txn;
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
    if (kAdcReconfigAddrLen + data.size() > acf::kAcfAbbMaxPayload) return {};

    std::vector<uint8_t> payload(kAdcReconfigAddrLen + data.size());
    avtp::detail::put_u16(payload.data(), start_address);
    std::copy(data.begin(), data.end(), payload.begin() + static_cast<long>(kAdcReconfigAddrLen));

    acf::AcfMessageInfo info;
    info.byte_bus_id     = byte_bus_id;
    info.op               = true; // write: §12.7.1 Figure 18, the data of the byte_msg_payload
                                   // from a write request is written into the EP_func block
    info.evt_op            = 0x7; // evt[2:0] = 111b
    info.transaction_num   = transaction_num;
    return acf::encode_acf_abb(info, payload);
}

// ── AdcEndpoint ───────────────────────────────────────────────────────────────
// Drives the corrected three-layer pipeline end to end: execute_measurement_
// cycle() runs one layer-1 averaging interval and accumulates its result;
// response_ready()/collect_response() implement REQ-ADC-053's own single
// readiness rule and then pack+drain the pending accumulator into a real
// N-value response array, exactly mirroring the caller-orchestration loop
// c-RCP's own file header describes for all three cadence cases (this
// module still owns no scheduling/timer state of its own -- see the file
// header's "request-driven sampling only" section). The pending accumulator
// is a fixed-capacity, std::array-backed buffer bounded at
// kAdcMaxCombineValues (Table 54's own 8-bit adc_combine_avg_values width),
// matching Phase 1/2's fixed-capacity convention.
namespace detail {
class AdcPendingBuffer {
public:
    bool push_back(AdcAvgValue v) noexcept {
        if (size_ >= kAdcMaxCombineValues) return false;
        data_[size_] = v;
        ++size_;
        return true;
    }
    size_t size() const noexcept { return size_; }
    bool   empty() const noexcept { return size_ == 0; }
    const AdcAvgValue& operator[](size_t i) const noexcept { return data_[i]; }

    // pop_front_n removes the first n entries (clamped to size()), shifting
    // the remainder down.
    void pop_front_n(size_t n) noexcept {
        n = std::min(n, size_);
        std::move(data_.begin() + static_cast<long>(n), data_.begin() + static_cast<long>(size_),
                   data_.begin());
        size_ -= n;
    }

private:
    std::array<AdcAvgValue, kAdcMaxCombineValues> data_{};
    size_t                                          size_ = 0;
};
} // namespace detail

class AdcEndpoint {
public:
    // execute_measurement_cycle runs cfg.adc_samples_per_avg_interval raw
    // samples through take_sample() (each returning an AdcSample -- a
    // kAdcNoSignal value is itself a legitimate, timestamped capture
    // attempt, not an exceptional error, matching average_interval()'s own
    // handling), reduces them via average_interval(), and appends the
    // result to this endpoint's pending accumulator. Reports
    // AdcErrc::pending_values_full rather than growing without bound if the
    // accumulator is already at its kAdcMaxCombineValues capacity.
    template <typename SampleFn>
    std::error_code execute_measurement_cycle(const AdcFunctionalConfig& cfg, SampleFn take_sample) {
        std::vector<AdcSample> samples;
        samples.reserve(cfg.adc_samples_per_avg_interval);
        for (uint16_t i = 0; i < cfg.adc_samples_per_avg_interval; ++i) samples.push_back(take_sample());

        const AdcAvgValue avg = average_interval(samples);
        if (!pending_.push_back(avg)) return make_error_code(AdcErrc::pending_values_full);
        return {};
    }

    // response_ready reports whether enough pending averaged values have
    // accumulated to assemble one response (REQ-ADC-053).
    bool response_ready(const AdcFunctionalConfig& cfg) const noexcept {
        return cadence_response_ready(pending_.size(), cfg.adc_combine_avg_values);
    }

    // collect_response pops cfg.adc_combine_avg_values pending averaged
    // values (oldest first) into out_values verbatim, reports the response's
    // own capture-moment timestamp, and removes exactly those entries from
    // the pending accumulator. Fails with AdcErrc::response_not_ready
    // (leaving the accumulator untouched) if response_ready() would report
    // false.
    std::error_code collect_response(const AdcFunctionalConfig& cfg, std::vector<uint16_t>& out_values,
                                      uint64_t& out_timestamp) {
        if (!response_ready(cfg)) return make_error_code(AdcErrc::response_not_ready);

        const size_t n = cfg.adc_combine_avg_values;
        std::vector<AdcAvgValue> front;
        front.reserve(n);
        for (size_t i = 0; i < n; ++i) front.push_back(pending_[i]);

        collect_response_values(front, n, out_values);
        out_timestamp = capture_moment_timestamp(front);
        pending_.pop_front_n(n);
        return {};
    }

    size_t pending_count() const noexcept { return pending_.size(); }

private:
    detail::AdcPendingBuffer pending_;
};

} // namespace adc
} // namespace rcp

// Enable std::error_code construction from rcp::adc::AdcErrc.
namespace std {
template <>
struct is_error_code_enum<rcp::adc::AdcErrc> : true_type {};
} // namespace std
