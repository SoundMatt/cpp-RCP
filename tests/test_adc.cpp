// fusa:test REQ-ADC-001
// fusa:test REQ-ADC-002
// fusa:test REQ-ADC-003
// fusa:test REQ-ADC-004
// fusa:test REQ-ADC-005
// fusa:test REQ-ADC-006
// fusa:test REQ-ADC-007
// fusa:test REQ-ADC-008
// fusa:test REQ-ADC-009
// fusa:test REQ-ADC-010
// fusa:test REQ-ADC-011
// fusa:test REQ-ADC-012
// fusa:test REQ-ADC-013
// fusa:test REQ-ADC-014
// fusa:test REQ-ADC-015
// fusa:test REQ-ADC-016
// fusa:test REQ-ADC-017
// fusa:test REQ-ADC-018
// fusa:test REQ-ADC-019
// fusa:test REQ-ADC-020
// fusa:test REQ-ADC-021
// fusa:test REQ-ADC-022
// fusa:test REQ-ADC-023
// fusa:test REQ-ADC-024
// fusa:test REQ-ADC-025
// fusa:test REQ-ADC-026
// fusa:test REQ-ADC-027
// fusa:test REQ-ADC-028
// fusa:test REQ-ADC-029
// fusa:test REQ-ADC-030
// fusa:test REQ-ADC-031
// fusa:test REQ-ADC-032
// fusa:test REQ-ADC-033
// fusa:test REQ-ADC-035
// fusa:test REQ-ADC-036
// fusa:test REQ-ADC-037
// fusa:test REQ-ADC-038
// fusa:test REQ-ADC-039
// fusa:test REQ-ADC-040
// fusa:test REQ-ADC-041
// fusa:test REQ-ADC-042
// fusa:test REQ-ADC-043
// fusa:test REQ-ADC-044
// fusa:test REQ-ADC-045
// fusa:test REQ-ADC-046
// fusa:test REQ-ADC-047
// fusa:test REQ-ADC-048
// fusa:test REQ-ADC-049
// fusa:test REQ-ADC-050
// fusa:test REQ-ADC-051
// fusa:test REQ-ADC-052
// fusa:test REQ-ADC-053
// fusa:test REQ-ADC-054
// fusa:test REQ-ADC-055

// Tests for rcp/adc.hpp — the ADC endpoint type, re-derived from c-RCP's
// test_ep_adc.c (Phase 3, cpp-RCP issue #129).

#include <catch2/catch_test_macros.hpp>
#include <rcp/adc.hpp>

using namespace rcp::adc;
using rcp::endpoint::EndpointErrc;
using rcp::lifecycle::FieldKind;
using rcp::lifecycle::ServerState;
using rcp::lifecycle::WriterCtx;

// ── Layer 1: average_interval ─────────────────────────────────────────────────

TEST_CASE("average_interval computes the arithmetic mean of raw samples", "[adc][REQ-ADC-002]") {
    std::vector<AdcSample> samples{{10, 100}, {20, 200}, {30, 300}};
    auto avg = average_interval(samples);
    REQUIRE(avg.value == 20);
}

TEST_CASE("average_interval returns kAdcNoSignal with a zero timestamp when sample_count is 0",
          "[adc][REQ-ADC-003]") {
    auto avg = average_interval({});
    REQUIRE(avg.value == kAdcNoSignal);
    REQUIRE(avg.timestamp == 0);
}

TEST_CASE("average_interval excludes kAdcNoSignal samples from its arithmetic mean", "[adc][REQ-ADC-004]") {
    std::vector<AdcSample> samples{{10, 1}, {kAdcNoSignal, 2}, {30, 3}};
    auto avg = average_interval(samples);
    REQUIRE(avg.value == 20); // mean of 10 and 30 only
}

TEST_CASE("average_interval reports the timestamp of the last sample that fed the mean",
          "[adc][REQ-ADC-005]") {
    std::vector<AdcSample> samples{{10, 100}, {20, 200}, {kAdcNoSignal, 300}};
    auto avg = average_interval(samples);
    REQUIRE(avg.timestamp == 200); // last USED sample, not the interval's literal last sample
}

TEST_CASE("average_interval reports the last sample's timestamp when every sample timed out",
          "[adc][REQ-ADC-041]") {
    std::vector<AdcSample> samples{{kAdcNoSignal, 100}, {kAdcNoSignal, 200}};
    auto avg = average_interval(samples);
    REQUIRE(avg.value == kAdcNoSignal);
    REQUIRE(avg.timestamp == 200); // the interval's own last sample still marks when it closed
}

TEST_CASE("average_interval averages samples at the 16-bit ceiling without overflow", "[adc][REQ-ADC-002]") {
    std::vector<AdcSample> samples{{0xFFFE, 1}, {0xFFFE, 2}, {0xFFFE, 3}};
    auto avg = average_interval(samples);
    REQUIRE(avg.value == 0xFFFE);
}

// ── REQ-ADC-033: inter-sample spacing ─────────────────────────────────────────

TEST_CASE("validate_sample_spacing distinguishes even from ragged spacing", "[adc][REQ-ADC-033]") {
    // base_clk_divider=5, sample_interval=200, base_clk_hz=1e9 -> expected
    // spacing exactly 1000ns.
    std::vector<AdcSample> even{{0, 0}, {0, 1000}, {0, 2000}};
    std::vector<AdcSample> ragged{{0, 0}, {0, 5}, {0, 2000}};

    REQUIRE(validate_sample_spacing(even, 5, 200, 1000000000u, 0) == AdcSpacingResult::Ok);
    REQUIRE(validate_sample_spacing(ragged, 5, 200, 1000000000u, 0) == AdcSpacingResult::Violation);
}

TEST_CASE("validate_sample_spacing respects a nonzero tolerance", "[adc][REQ-ADC-033]") {
    std::vector<AdcSample> s{{0, 0}, {0, 1050}}; // 50ns off the expected 1000ns

    REQUIRE(validate_sample_spacing(s, 5, 200, 1000000000u, 0) == AdcSpacingResult::Violation);
    REQUIRE(validate_sample_spacing(s, 5, 200, 1000000000u, 50) == AdcSpacingResult::Ok);
    REQUIRE(validate_sample_spacing(s, 5, 200, 1000000000u, 49) == AdcSpacingResult::Violation);
}

TEST_CASE("validate_sample_spacing fails open without a real clock rate", "[adc][REQ-ADC-033]") {
    std::vector<AdcSample> s{{0, 0}, {0, 5000}};
    REQUIRE(validate_sample_spacing(s, 1, 1, 0, 0) == AdcSpacingResult::Ok);         // base_clk_hz == 0
    REQUIRE(validate_sample_spacing(s, 0, 1, 1000000000u, 0) == AdcSpacingResult::Ok); // divider == 0
    REQUIRE(validate_sample_spacing({s[0]}, 1, 1, 1000000000u, 0) == AdcSpacingResult::Ok); // < 2 samples
    REQUIRE(validate_sample_spacing({}, 1, 1, 1000000000u, 0) == AdcSpacingResult::Ok);
}

TEST_CASE("validate_sample_spacing rejects non-monotonic timestamps", "[adc][REQ-ADC-033]") {
    std::vector<AdcSample> s{{0, 1000}, {0, 500}}; // went backwards
    REQUIRE(validate_sample_spacing(s, 5, 200, 1000000000u, 0) == AdcSpacingResult::Violation);
    // Must be caught even with a huge tolerance -- not an accident of the
    // (i+1)-minus-i subtraction underflowing.
    REQUIRE(validate_sample_spacing(s, 5, 200, 1000000000u, UINT64_MAX - 1000) ==
            AdcSpacingResult::Violation);
}

// ── Layers 2/3: collect_response_values / cadence ─────────────────────────────

TEST_CASE("collect_response_values packs averaged values in capture order, verbatim",
          "[adc][REQ-ADC-006][REQ-ADC-009]") {
    std::vector<AdcAvgValue> avg{{10, 1}, {kAdcNoSignal, 2}, {30, 3}};
    std::vector<uint16_t> out;
    auto n = collect_response_values(avg, 3, out);
    REQUIRE(n == 3);
    REQUIRE(out == std::vector<uint16_t>{10, kAdcNoSignal, 30});
}

TEST_CASE("collect_response_values packs exactly value_count values -- the leading ones -- when "
          "more averages are available than requested",
          "[adc][REQ-ADC-007]") {
    std::vector<AdcAvgValue> avg{{10, 0}, {20, 0}, {30, 0}, {40, 0}};
    std::vector<uint16_t> out;
    auto n = collect_response_values(avg, 2, out);
    REQUIRE(n == 2);
    REQUIRE(out == std::vector<uint16_t>{10, 20});
}

TEST_CASE("collect_response_values reports a short count without touching unwritten entries",
          "[adc][REQ-ADC-008]") {
    std::vector<AdcAvgValue> avg{{10, 1}, {20, 2}};
    std::vector<uint16_t> out;
    auto n = collect_response_values(avg, 5, out);
    REQUIRE(n == 2);
    REQUIRE(out.size() == 2);
}

TEST_CASE("collect_response_values returns 0 when either count is 0", "[adc][REQ-ADC-010]") {
    std::vector<uint16_t> out;
    REQUIRE(collect_response_values({}, 3, out) == 0);
    REQUIRE(collect_response_values({{10, 1}}, 0, out) == 0);
}

TEST_CASE("response_value_count returns half a request's read_size", "[adc][REQ-ADC-001]") {
    REQUIRE(response_value_count(0) == 0);
    REQUIRE(response_value_count(2) == 1);
    REQUIRE(response_value_count(16) == 8);
    REQUIRE(response_value_count(3) == 0); // odd -> no whole number of values
}

TEST_CASE("cadence_case classifies ACCUMULATE/ONE_TO_ONE/FAN_OUT", "[adc][REQ-ADC-037]") {
    REQUIRE(cadence_case(2, 4) == AdcCadenceCase::Accumulate); // combine > intervals
    REQUIRE(cadence_case(4, 4) == AdcCadenceCase::OneToOne);   // combine == intervals
    REQUIRE(cadence_case(4, 2) == AdcCadenceCase::FanOut);      // combine < intervals
}

TEST_CASE("cadence_response_ready compares pending count against combine_avg_values",
          "[adc][REQ-ADC-053]") {
    REQUIRE(cadence_response_ready(3, 3));
    REQUIRE_FALSE(cadence_response_ready(2, 3));
    REQUIRE(cadence_response_ready(0, 0)); // zero combine is trivially ready
}

TEST_CASE("capture_moment_timestamp returns the first response value's timestamp",
          "[adc][REQ-ADC-012]") {
    std::vector<AdcAvgValue> avg{{10, 111}, {20, 222}};
    REQUIRE(capture_moment_timestamp(avg) == 111);
}

TEST_CASE("capture_moment_timestamp returns 0 when avg_count is 0", "[adc][REQ-ADC-013]") {
    REQUIRE(capture_moment_timestamp({}) == 0);
}

// ── Functional config ──────────────────────────────────────────────────────────

TEST_CASE("AdcFunctionalConfig default-constructs zeroed", "[adc][REQ-ADC-014]") {
    AdcFunctionalConfig cfg;
    REQUIRE_FALSE(cfg.ep_enable);
    REQUIRE(cfg.adc_samples_per_avg_interval == 0);
    REQUIRE(cfg.adc_avg_intervals_per_request == 0);
    REQUIRE(cfg.adc_combine_avg_values == 0);
    REQUIRE(cfg.trigger_min == 0);
    REQUIRE(cfg.trigger_max == 0);
}

TEST_CASE("functional_cfg_writable is unwritable while HwUnconfigured", "[adc][REQ-ADC-015]") {
    WriterCtx writer;
    writer.via_root_client_ep0 = true;
    REQUIRE_FALSE(functional_cfg_writable(ServerState::HwUnconfigured, writer));
}

TEST_CASE("functional_cfg_writable requires authorization or a discovery stream while HwConfigured",
          "[adc][REQ-ADC-016]") {
    WriterCtx authorized;
    authorized.via_root_client_ep0 = true;
    REQUIRE(functional_cfg_writable(ServerState::HwConfigured, authorized));

    WriterCtx discovery;
    discovery.via_discovery_stream = true;
    REQUIRE(functional_cfg_writable(ServerState::HwConfigured, discovery));

    REQUIRE_FALSE(functional_cfg_writable(ServerState::HwConfigured, WriterCtx{}));
}

TEST_CASE("functional_cfg_writable requires authorization once RcpConfigured", "[adc][REQ-ADC-017]") {
    WriterCtx discovery;
    discovery.via_discovery_stream = true;
    REQUIRE_FALSE(functional_cfg_writable(ServerState::RcpConfigured, discovery)); // no longer suffices alone

    WriterCtx owning;
    owning.via_owning_stream = true;
    REQUIRE(functional_cfg_writable(ServerState::RcpConfigured, owning));
}

TEST_CASE("set_samples_per_avg_interval rejects an unauthorized write without mutating cfg",
          "[adc][REQ-ADC-018]") {
    AdcFunctionalConfig cfg;
    REQUIRE_FALSE(set_samples_per_avg_interval(cfg, 42, ServerState::HwUnconfigured, WriterCtx{}));
    REQUIRE(cfg.adc_samples_per_avg_interval == 0);
}

TEST_CASE("set_samples_per_avg_interval applies the write when authorized", "[adc][REQ-ADC-019]") {
    AdcFunctionalConfig cfg;
    WriterCtx writer;
    writer.via_root_client_ep0 = true;
    REQUIRE(set_samples_per_avg_interval(cfg, 42, ServerState::HwConfigured, writer));
    REQUIRE(cfg.adc_samples_per_avg_interval == 42);
}

TEST_CASE("set_avg_intervals_per_request rejects unauthorized / applies when authorized",
          "[adc][REQ-ADC-020][REQ-ADC-021]") {
    AdcFunctionalConfig cfg;
    REQUIRE_FALSE(set_avg_intervals_per_request(cfg, 7, ServerState::HwUnconfigured, WriterCtx{}));
    REQUIRE(cfg.adc_avg_intervals_per_request == 0);

    WriterCtx writer;
    writer.via_owning_stream = true;
    REQUIRE(set_avg_intervals_per_request(cfg, 7, ServerState::RcpConfigured, writer));
    REQUIRE(cfg.adc_avg_intervals_per_request == 7);
}

TEST_CASE("set_combine_avg_values rejects unauthorized / applies when authorized",
          "[adc][REQ-ADC-022][REQ-ADC-023]") {
    AdcFunctionalConfig cfg;
    REQUIRE_FALSE(set_combine_avg_values(cfg, 9, ServerState::HwUnconfigured, WriterCtx{}));
    REQUIRE(cfg.adc_combine_avg_values == 0);

    WriterCtx writer;
    writer.via_root_client_ep0 = true;
    REQUIRE(set_combine_avg_values(cfg, 9, ServerState::RcpConfigured, writer));
    REQUIRE(cfg.adc_combine_avg_values == 9);
}

// ── Trigger outputs (Table 53) ─────────────────────────────────────────────────

TEST_CASE("AdcTriggerState starts with no previous value tracked", "[adc][REQ-ADC-031]") {
    AdcTriggerState s;
    REQUIRE_FALSE(s.has_previous);
}

TEST_CASE("trigger_evaluate never fires an edge trigger on the first call", "[adc][REQ-ADC-031]") {
    AdcTriggerState s;
    auto fired = trigger_evaluate(s, 5, 10, 100, false);
    REQUIRE(fired == 0);
    REQUIRE(s.has_previous);
}

TEST_CASE("an ADC measurement value is fixed at 16 bits, matching TC18's byte_msg_payload layout",
          "[adc][REQ-ADC-032]") {
    // kAdcValueLen is the single source of truth every wire-level ADC
    // encode/decode path (encode_response/decode_response, response_
    // value_count) derives its byte-count arithmetic from -- pin it
    // directly at 16 bits (RCP_EP_ADC_VALUE_LEN's own value in c-RCP) so
    // any future change to that constant is caught here explicitly, not
    // just as an incidental side effect of an unrelated codec test.
    REQUIRE(kAdcValueLen == sizeof(uint16_t));
    REQUIRE(kAdcValueLen == 2);
}

TEST_CASE("trigger_evaluate BELOW_MIN fires on a downward crossing of trigger_min",
          "[adc][REQ-ADC-048]") {
    AdcTriggerState s;
    trigger_evaluate(s, 20, 10, 100, false); // establish previous >= min
    auto fired = trigger_evaluate(s, 5, 10, 100, false); // crosses below min
    REQUIRE((fired & kAdcTriggerBelowMin) != 0);
}

TEST_CASE("trigger_evaluate ABOVE_MIN fires on an upward crossing of trigger_min",
          "[adc][REQ-ADC-049]") {
    AdcTriggerState s;
    trigger_evaluate(s, 5, 10, 100, false);
    auto fired = trigger_evaluate(s, 20, 10, 100, false);
    REQUIRE((fired & kAdcTriggerAboveMin) != 0);
}

TEST_CASE("trigger_evaluate BELOW_MAX/ABOVE_MAX cross trigger_max", "[adc][REQ-ADC-050][REQ-ADC-051]") {
    AdcTriggerState s;
    trigger_evaluate(s, 200, 10, 100, false); // above max
    auto down = trigger_evaluate(s, 50, 10, 100, false); // crosses below max
    REQUIRE((down & kAdcTriggerBelowMax) != 0);

    AdcTriggerState s2;
    trigger_evaluate(s2, 50, 10, 100, false);
    auto up = trigger_evaluate(s2, 200, 10, 100, false);
    REQUIRE((up & kAdcTriggerAboveMax) != 0);
}

TEST_CASE("trigger_evaluate's MEASUREMENT_FINISHED composes independently with the other four",
          "[adc][REQ-ADC-052]") {
    AdcTriggerState s;
    trigger_evaluate(s, 20, 10, 100, false);
    auto fired = trigger_evaluate(s, 5, 10, 100, /*measurement_finished=*/true);
    REQUIRE((fired & kAdcTriggerBelowMin) != 0);
    REQUIRE((fired & kAdcTriggerMeasurementFinished) != 0);

    AdcTriggerState s2;
    auto first_call_finished_only = trigger_evaluate(s2, 5, 10, 100, true);
    REQUIRE(first_call_finished_only == kAdcTriggerMeasurementFinished); // no previous -> no edge triggers
}

// ── The EP_func register block ────────────────────────────────────────────────

TEST_CASE("render_registers matches Table 54's own offsets", "[adc][REQ-ADC-035][REQ-ADC-036][REQ-ADC-038][REQ-ADC-040]") {
    AdcFunctionalConfig cfg;
    cfg.ep_enable            = true;
    cfg.ep_response_ts_enable = true;
    cfg.ep_status              = 0xBEEF;
    cfg.base_clk_divider        = 5;
    cfg.sample_interval          = 200;
    cfg.adc_avg_intervals_per_request = 4;
    cfg.adc_samples_per_avg_interval  = 8;
    cfg.adc_combine_avg_values         = 2;
    cfg.resolution                       = 12;
    cfg.trigger_min                       = 100;
    cfg.trigger_max                       = 4000;

    AdcRegisterBlock block{};
    render_registers(cfg, block);

    REQUIRE(block[kAdcRegEpLen] == kAdcEpFuncLen);
    REQUIRE((block[kAdcRegEpEnableClr] & 0x01) != 0);   // ep_enable
    REQUIRE((block[kAdcRegEpOptions] & 0x08) != 0);      // ep_response_ts_enable
    REQUIRE(rcp::avtp::detail::get_u16(&block[kAdcRegBaseClk]) == 0); // no real clock modelled
    REQUIRE(rcp::avtp::detail::get_u16(&block[kAdcRegEpStatus]) == 0xBEEF);
    REQUIRE(block[kAdcRegBaseClkDivider] == 5);
    REQUIRE(block[kAdcRegSampleInterval] == 200);
    REQUIRE(block[kAdcRegAvgIntervals] == 4);
    REQUIRE(block[kAdcRegSamplesPerAvg] == 8);
    REQUIRE(block[kAdcRegCombineAvg] == 2);
    REQUIRE(block[kAdcRegResolution] == 12);
    REQUIRE(rcp::avtp::detail::get_u16(&block[kAdcRegTriggerMin]) == 100);
    REQUIRE(rcp::avtp::detail::get_u16(&block[kAdcRegTriggerMax]) == 4000);
}

TEST_CASE("render_registers truncates the wide avg_intervals/samples_per_avg fields to their low octet",
          "[adc][REQ-ADC-035]") {
    AdcFunctionalConfig cfg;
    cfg.adc_avg_intervals_per_request = 0x0142; // > 255
    cfg.adc_samples_per_avg_interval  = 0x0201;

    AdcRegisterBlock block{};
    render_registers(cfg, block);
    REQUIRE(block[kAdcRegAvgIntervals] == 0x42);
    REQUIRE(block[kAdcRegSamplesPerAvg] == 0x01);
}

TEST_CASE("apply_reconfig writes a multi-register span", "[adc][REQ-ADC-039]") {
    AdcFunctionalConfig cfg;
    // Patch from kAdcRegBaseClkDivider through kAdcRegAvgIntervals inclusive.
    std::vector<uint8_t> payload;
    payload.push_back(static_cast<uint8_t>(kAdcRegBaseClkDivider >> 8));
    payload.push_back(static_cast<uint8_t>(kAdcRegBaseClkDivider & 0xFF));
    payload.push_back(9);  // base_clk_divider
    payload.push_back(77); // sample_interval
    payload.push_back(3);  // avg_intervals (low octet)

    auto ec = apply_reconfig(cfg, payload.data(), payload.size());
    REQUIRE_FALSE(ec);
    REQUIRE(cfg.base_clk_divider == 9);
    REQUIRE(cfg.sample_interval == 77);
    REQUIRE(cfg.adc_avg_intervals_per_request == 3);
}

TEST_CASE("apply_reconfig writes resolution and trigger thresholds", "[adc][REQ-ADC-040]") {
    AdcFunctionalConfig cfg;
    std::vector<uint8_t> payload;
    payload.push_back(static_cast<uint8_t>(kAdcRegResolution >> 8));
    payload.push_back(static_cast<uint8_t>(kAdcRegResolution & 0xFF));
    payload.push_back(16);   // resolution
    payload.push_back(0x01); // trigger_min hi
    payload.push_back(0x00); // trigger_min lo
    payload.push_back(0x02); // trigger_max hi
    payload.push_back(0x00); // trigger_max lo

    REQUIRE_FALSE(apply_reconfig(cfg, payload.data(), payload.size()));
    REQUIRE(cfg.resolution == 16);
    REQUIRE(cfg.trigger_min == 0x0100);
    REQUIRE(cfg.trigger_max == 0x0200);
}

TEST_CASE("apply_reconfig ignores read-only registers within an otherwise-applied span",
          "[adc][REQ-ADC-039]") {
    AdcFunctionalConfig cfg;
    cfg.ep_status = 0x1234;

    // Span from EP_LEN (0x0000, read-only) through EP_OPTIONS (0x0003).
    std::vector<uint8_t> payload{0x00, 0x00, /*EP_LEN write*/ 0xAA, /*reserved write*/ 0xBB,
                                  /*EP_ENABLE_CLR*/ 0x01, /*EP_OPTIONS*/ 0x08};
    REQUIRE_FALSE(apply_reconfig(cfg, payload.data(), payload.size()));

    AdcRegisterBlock block{};
    render_registers(cfg, block);
    REQUIRE(block[kAdcRegEpLen] == kAdcEpFuncLen); // untouched by the write
    REQUIRE(block[kAdcRegReserved01] == 0);         // untouched
    REQUIRE(cfg.ep_enable);                          // R/W octet DID apply
    REQUIRE(cfg.ep_response_ts_enable);               // options bit 3 (0x08) DID apply
}

TEST_CASE("apply_reconfig rejects a write extending past EP_LEN", "[adc][REQ-ADC-039]") {
    AdcFunctionalConfig cfg;
    std::vector<uint8_t> payload{0x00, static_cast<uint8_t>(kAdcEpFuncLen - 1), 0xAA, 0xBB};
    auto ec = apply_reconfig(cfg, payload.data(), payload.size());
    REQUIRE(ec == make_error_code(AdcErrc::reconfig_out_of_range));
}

TEST_CASE("apply_reconfig rejects a payload with no data octet", "[adc][REQ-ADC-039]") {
    AdcFunctionalConfig cfg;
    std::vector<uint8_t> payload{0x00, 0x00}; // address only, no data
    auto ec = apply_reconfig(cfg, payload.data(), payload.size());
    REQUIRE(ec == make_error_code(AdcErrc::reconfig_short));
}

TEST_CASE("encode_reconfig_request round-trips through apply_reconfig", "[adc][REQ-ADC-054]") {
    std::vector<uint8_t> data{9, 77};
    auto frame = encode_reconfig_request(0x10, kAdcRegBaseClkDivider, data, 5);
    REQUIRE_FALSE(frame.empty());

    // Manually decode the frame's payload (the reconfig-request encoder
    // builds an ordinary ACF_ABB write with evt[2:0]=111b).
    rcp::acf::AcfMessageInfo info;
    std::vector<uint8_t> payload;
    REQUIRE_FALSE(rcp::acf::decode_acf_abb(frame.data(), frame.size(), info, payload));
    REQUIRE(info.op);
    REQUIRE(info.evt_op == 0x7);

    AdcFunctionalConfig cfg;
    REQUIRE_FALSE(apply_reconfig(cfg, payload.data(), payload.size()));
    REQUIRE(cfg.base_clk_divider == 9);
    REQUIRE(cfg.sample_interval == 77);
}

TEST_CASE("encode_reconfig_request rejects empty data", "[adc][REQ-ADC-054]") {
    REQUIRE(encode_reconfig_request(0x10, 0, {}, 1).empty());
}

// ── AdcErrc category sanity ────────────────────────────────────────────────────

TEST_CASE("AdcErrc reports a non-empty, category-correct message for every value",
          "[adc][REQ-ADC-024][REQ-ADC-055]") {
    for (int v = 1; v <= 12; ++v) {
        auto ec = make_error_code(static_cast<AdcErrc>(v));
        REQUIRE(ec.category() == adc_category());
        REQUIRE_FALSE(ec.message().empty());
    }
    // An out-of-range/unrecognized code (covers both AdcErrc's own general
    // default branch, REQ-ADC-024, and the reconfig-specific codes 9/10
    // folded into the same category, REQ-ADC-055 -- c-RCP's
    // rcp_ep_adc_strerror()/rcp_ep_adc_reconfig_strerror() are two separate
    // functions each tested with an out-of-range value; this category's
    // message() merges both, so one out-of-range assertion here covers both
    // ids' "including an out-of-range value" clause).
    auto ec = make_error_code(static_cast<AdcErrc>(99));
    REQUIRE_FALSE(ec.message().empty());
}

TEST_CASE("AdcErrc::no_signal's message does not claim an invented ADC_NO_SIGNAL spec identifier",
          "[adc][REQ-ADC-006]") {
    auto ec = make_error_code(AdcErrc::no_signal);
    REQUIRE(ec.message().find("ADC_NO_SIGNAL") == std::string::npos);
}

TEST_CASE("wire_error maps bad_payload_len/bad_evt to their numbered wire codes", "[adc][REQ-ADC-024]") {
    REQUIRE(wire_error(AdcErrc::bad_payload_len) == rcp::acf::WireErrorCode::InvalidParameter);
    REQUIRE(wire_error(AdcErrc::bad_evt) == rcp::acf::WireErrorCode::UnsupportedCmd);
    REQUIRE_FALSE(wire_error(AdcErrc::short_frame).has_value());
}

// ── Wire codec: read request ──────────────────────────────────────────────────

TEST_CASE("ADC read request encode/decode round-trips, carrying read_size", "[adc][REQ-ADC-025]") {
    auto frame = encode_read_request(0x20, 16, 7);
    uint16_t read_size = 0;
    uint8_t txn = 0;
    REQUIRE_FALSE(decode_read_request(frame.data(), frame.size(), 0x20, read_size, txn));
    REQUIRE(read_size == 16);
    REQUIRE(txn == 7);
}

TEST_CASE("encode_read_request's frame carries no payload", "[adc][REQ-ADC-043]") {
    auto frame = encode_read_request(0x20, 16, 7);
    REQUIRE(frame.size() == rcp::acf::kAcfCommonHeaderLen);
}

TEST_CASE("decode_read_request rejects a misaddressed byte_bus_id", "[adc][REQ-ADC-044]") {
    auto frame = encode_read_request(0x20, 2, 1);
    uint16_t read_size = 0;
    uint8_t txn = 0;
    REQUIRE(decode_read_request(frame.data(), frame.size(), 0x21, read_size, txn) ==
            make_error_code(AdcErrc::wrong_bus));
}

// A dedicated write request encoder doesn't exist for ADC (it has no write
// request shape besides the reconfiguration escape hatch) -- a read request
// with op forced to write is built directly to exercise this rejection.
TEST_CASE("decode_read_request rejects a write-op frame", "[adc][REQ-ADC-045]") {
    rcp::acf::AcfMessageInfo info;
    info.byte_bus_id = 0x20;
    info.op = true; // write
    auto frame = rcp::acf::encode_acf_abb(info, {});
    uint16_t read_size = 0;
    uint8_t txn = 0;
    REQUIRE(decode_read_request(frame.data(), frame.size(), 0x20, read_size, txn) ==
            make_error_code(AdcErrc::wrong_op));
}

TEST_CASE("decode_read_request rejects a nonzero evt[2:0]", "[adc][REQ-ADC-046]") {
    rcp::acf::AcfMessageInfo info;
    info.byte_bus_id = 0x20;
    info.op = false;
    info.evt_op = 0x3; // reserved
    auto frame = rcp::acf::encode_acf_abb(info, {});
    uint16_t read_size = 0;
    uint8_t txn = 0;
    REQUIRE(decode_read_request(frame.data(), frame.size(), 0x20, read_size, txn) ==
            make_error_code(AdcErrc::bad_evt));
}

TEST_CASE("decode_read_request rejects a frame shorter than the ACF_ABB fixed header",
          "[adc][REQ-ADC-026]") {
    // byte0's top 7 bits must carry acf_msg_type == kAcfMsgTypeAbb (0x0E) --
    // otherwise decode_acf_abb reports bad_acf_msg_type before it even gets
    // to check the buffer's length against the fixed header size.
    std::vector<uint8_t> short_frame{0x1C, 0x00, 0x00};
    uint16_t read_size = 0;
    uint8_t txn = 0;
    REQUIRE(decode_read_request(short_frame.data(), short_frame.size(), 0x20, read_size, txn) ==
            make_error_code(AdcErrc::short_frame));
}

// ── Wire codec: response ──────────────────────────────────────────────────────

TEST_CASE("ADC response encode/decode round-trips every measurement value when untimed",
          "[adc][REQ-ADC-027]") {
    std::vector<uint16_t> values{1, 2, 3, 4, 5, 6, 7, 8};
    auto frame = encode_response(0x20, values, 9, /*timed=*/false, 0);

    std::vector<uint16_t> out;
    bool timed = false;
    uint64_t ts = 0;
    uint8_t txn = 0;
    REQUIRE_FALSE(decode_response(frame.data(), frame.size(), 0x20, 16, out, timed, ts, txn));
    REQUIRE(out == values);
    REQUIRE_FALSE(timed);
    REQUIRE(ts == 0);
    REQUIRE(txn == 9);
}

TEST_CASE("ADC response encode/decode round-trips every measurement value when timed",
          "[adc][REQ-ADC-028]") {
    std::vector<uint16_t> values{100, 200};
    auto frame = encode_response(0x20, values, 3, /*timed=*/true, 999999);

    std::vector<uint16_t> out;
    bool timed = false;
    uint64_t ts = 0;
    uint8_t txn = 0;
    REQUIRE_FALSE(decode_response(frame.data(), frame.size(), 0x20, 16, out, timed, ts, txn));
    REQUIRE(out == values);
    REQUIRE(timed);
    REQUIRE(ts == 999999);
}

TEST_CASE("ADC response encode/decode round-trips kAdcNoSignal verbatim in its own value slot",
          "[adc][REQ-ADC-030]") {
    std::vector<uint16_t> values{10, kAdcNoSignal, 30};
    auto frame = encode_response(0x20, values, 1, false, 0);

    std::vector<uint16_t> out;
    bool timed = false;
    uint64_t ts = 0;
    uint8_t txn = 0;
    REQUIRE_FALSE(decode_response(frame.data(), frame.size(), 0x20, 16, out, timed, ts, txn));
    REQUIRE(out == values);
}

TEST_CASE("decode_response rejects a bad-payload-length frame", "[adc][REQ-ADC-029]") {
    rcp::acf::AcfMessageInfo info;
    info.byte_bus_id = 0x20;
    info.op = false;
    info.rsp = true;
    auto frame = rcp::acf::encode_acf_abb(info, std::vector<uint8_t>{0x01}); // odd length
    std::vector<uint16_t> out;
    bool timed = false;
    uint64_t ts = 0;
    uint8_t txn = 0;
    REQUIRE(decode_response(frame.data(), frame.size(), 0x20, 16, out, timed, ts, txn) ==
            make_error_code(AdcErrc::bad_payload_len));
}

TEST_CASE("decode_response rejects a frame holding more values than the caller can hold",
          "[adc][REQ-ADC-047]") {
    std::vector<uint16_t> values{1, 2, 3, 4};
    auto frame = encode_response(0x20, values, 1, false, 0);
    std::vector<uint16_t> out;
    bool timed = false;
    uint64_t ts = 0;
    uint8_t txn = 0;
    REQUIRE(decode_response(frame.data(), frame.size(), 0x20, /*max_values=*/2, out, timed, ts, txn) ==
            make_error_code(AdcErrc::too_many_values));
}

TEST_CASE("encode_response returns an empty vector for an invalid value_count", "[adc][REQ-ADC-042]") {
    REQUIRE(encode_response(0x20, {}, 1, false, 0).empty());
    std::vector<uint16_t> too_many(kAdcMaxResponseValues + 1, 1);
    REQUIRE(encode_response(0x20, too_many, 1, false, 0).empty());
}

TEST_CASE("encode_response carries value_count measurement values and reports 2*value_count as read_size",
          "[adc][REQ-ADC-011]") {
    std::vector<uint16_t> values{1, 2, 3};
    auto frame = encode_response(0x20, values, 1, false, 0);
    rcp::acf::AcfMessageInfo info;
    std::vector<uint8_t> payload;
    REQUIRE_FALSE(rcp::acf::decode_acf_abb(frame.data(), frame.size(), info, payload));
    REQUIRE(info.read_size_or_segment_num == values.size() * kAdcValueLen);
    REQUIRE(payload.size() == values.size() * kAdcValueLen);
}

// ── AdcEndpoint: corrected three-layer pipeline end to end ────────────────────

TEST_CASE("AdcEndpoint accumulates averaged interval values and reports response readiness",
          "[adc][REQ-ADC-053]") {
    AdcEndpoint ep;
    AdcFunctionalConfig cfg;
    cfg.adc_samples_per_avg_interval = 2;
    cfg.adc_combine_avg_values        = 2;

    std::vector<uint16_t> raw{10, 20, 30, 40};
    size_t cursor = 0;
    auto take_sample = [&]() -> AdcSample {
        AdcSample s{raw[cursor], cursor};
        ++cursor;
        return s;
    };

    REQUIRE_FALSE(ep.execute_measurement_cycle(cfg, take_sample)); // interval 1: avg(10,20)=15
    REQUIRE_FALSE(ep.response_ready(cfg));
    REQUIRE_FALSE(ep.execute_measurement_cycle(cfg, take_sample)); // interval 2: avg(30,40)=35
    REQUIRE(ep.response_ready(cfg));

    std::vector<uint16_t> out_values;
    uint64_t out_ts = 0;
    REQUIRE_FALSE(ep.collect_response(cfg, out_values, out_ts));
    REQUIRE(out_values == std::vector<uint16_t>{15, 35}); // packed verbatim, NOT re-averaged
    REQUIRE(ep.pending_count() == 0);
}

TEST_CASE("AdcEndpoint::collect_response fails when not enough values are pending",
          "[adc][REQ-ADC-053]") {
    AdcEndpoint ep;
    AdcFunctionalConfig cfg;
    cfg.adc_combine_avg_values = 3;
    std::vector<uint16_t> out_values;
    uint64_t out_ts = 0;
    REQUIRE(ep.collect_response(cfg, out_values, out_ts) == make_error_code(AdcErrc::response_not_ready));
}

TEST_CASE("AdcEndpoint::execute_measurement_cycle only invokes take_sample exactly "
          "samples_per_avg_interval times",
          "[adc][REQ-ADC-002]") {
    AdcEndpoint ep;
    AdcFunctionalConfig cfg;
    cfg.adc_samples_per_avg_interval = 5;
    int calls = 0;
    auto take_sample = [&calls]() -> AdcSample {
        ++calls;
        return AdcSample{42, static_cast<uint64_t>(calls)};
    };
    REQUIRE_FALSE(ep.execute_measurement_cycle(cfg, take_sample));
    REQUIRE(calls == 5);
}

TEST_CASE("AdcEndpoint::execute_measurement_cycle reports pending_values_full at capacity",
          "[adc][REQ-ADC-053]") {
    AdcEndpoint ep;
    AdcFunctionalConfig cfg; // samples_per_avg_interval == 0 -> every cycle is instantly kAdcNoSignal
    auto take_sample = []() -> AdcSample { return AdcSample{1, 1}; };
    for (size_t i = 0; i < kAdcMaxCombineValues; ++i) {
        REQUIRE_FALSE(ep.execute_measurement_cycle(cfg, take_sample));
    }
    REQUIRE(ep.execute_measurement_cycle(cfg, take_sample) == make_error_code(AdcErrc::pending_values_full));
}

TEST_CASE("AdcEndpoint's FAN_OUT case: one execution's worth of values serve multiple responses",
          "[adc][REQ-ADC-037][REQ-ADC-053]") {
    AdcEndpoint ep;
    AdcFunctionalConfig cfg;
    cfg.adc_samples_per_avg_interval = 1;
    cfg.adc_combine_avg_values        = 1; // combine < intervals executed below -> FAN_OUT relationship
    REQUIRE(cadence_case(4, 1) == AdcCadenceCase::FanOut);

    int v = 0;
    auto take_sample = [&v]() -> AdcSample { ++v; return AdcSample{static_cast<uint16_t>(v), static_cast<uint64_t>(v)}; };

    for (int i = 0; i < 4; ++i) REQUIRE_FALSE(ep.execute_measurement_cycle(cfg, take_sample));
    REQUIRE(ep.pending_count() == 4);

    for (int i = 0; i < 4; ++i) {
        REQUIRE(ep.response_ready(cfg));
        std::vector<uint16_t> out_values;
        uint64_t out_ts = 0;
        REQUIRE_FALSE(ep.collect_response(cfg, out_values, out_ts));
        REQUIRE(out_values.size() == 1);
    }
    REQUIRE(ep.pending_count() == 0);
}
