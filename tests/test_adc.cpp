// fusa:test REQ-ADC-001
// fusa:test REQ-ADC-002
// fusa:test REQ-ADC-003
// fusa:test REQ-ADC-004
// fusa:test REQ-ADC-005
// fusa:test REQ-ADC-006

// Tests for rcp/adc.hpp — the ADC endpoint type (ROADMAP.md milestone 48,
// "Basic Endpoint Types II — I2C, UART, ADC, PWM_OUT, PWM_IN", v2.4.0).

#include <catch2/catch_test_macros.hpp>
#include <rcp/adc.hpp>

#include <deque>

using namespace rcp::adc;

// ── Three-level averaging model ──────────────────────────────────────────────

TEST_CASE("compute_average computes an arithmetic mean", "[adc][REQ-ADC-001]") {
    uint32_t out = 0;
    REQUIRE_FALSE(compute_average({10, 20, 30}, out));
    REQUIRE(out == 20);
}

TEST_CASE("compute_average reports no_signal on an empty sample set", "[adc][REQ-ADC-001]") {
    uint32_t out = 0;
    REQUIRE(compute_average({}, out) == make_error_code(AdcErrc::no_signal));
}

TEST_CASE("request_reading combines level-1 and level-2 averaging correctly", "[adc][REQ-ADC-002]") {
    AdcEndpoint ep;
    AdcAveragingConfig cfg;
    cfg.adc_avg_intervals_per_request = 2; // level 1: average 2 raw samples per interval
    cfg.adc_combine_avg_values        = 2; // level 2: average 2 intervals

    // Raw samples, consumed in order: interval 1 = {10,20} -> avg 15;
    // interval 2 = {30,40} -> avg 35; combined = avg(15,35) = 25.
    std::deque<uint32_t> raw{10, 20, 30, 40};
    auto take_sample = [&raw]() -> std::optional<uint32_t> {
        if (raw.empty()) return std::nullopt;
        uint32_t v = raw.front();
        raw.pop_front();
        return v;
    };

    uint32_t out_value = 0;
    auto ec = ep.request_reading(cfg, take_sample, out_value);
    REQUIRE_FALSE(ec);
    REQUIRE(out_value == 25);
}

TEST_CASE("request_reading rejects a zero-valued averaging config field", "[adc][REQ-ADC-002]") {
    AdcEndpoint ep;
    AdcAveragingConfig cfg;
    cfg.adc_avg_intervals_per_request = 0;
    auto take_sample = []() -> std::optional<uint32_t> { return 1; };

    uint32_t out_value = 0;
    auto ec = ep.request_reading(cfg, take_sample, out_value);
    REQUIRE(ec == make_error_code(AdcErrc::invalid_averaging_config));
}

// ── Request-driven sampling only / no-signal timeout path ───────────────────

TEST_CASE("request_reading reports no_signal when take_sample underruns", "[adc][REQ-ADC-003]") {
    AdcEndpoint ep;
    AdcAveragingConfig cfg;
    cfg.adc_avg_intervals_per_request = 3;
    cfg.adc_combine_avg_values        = 1;

    auto take_sample = []() -> std::optional<uint32_t> { return std::nullopt; }; // no signal captured

    uint32_t out_value = 0;
    auto ec = ep.request_reading(cfg, take_sample, out_value);
    REQUIRE(ec == make_error_code(AdcErrc::no_signal));
}

TEST_CASE("request_reading only invokes take_sample exactly the required number of times",
          "[adc][REQ-ADC-003]") {
    AdcEndpoint ep;
    AdcAveragingConfig cfg;
    cfg.adc_avg_intervals_per_request = 2;
    cfg.adc_combine_avg_values        = 3;

    int calls = 0;
    auto take_sample = [&calls]() -> std::optional<uint32_t> {
        ++calls;
        return 42u;
    };

    uint32_t out_value = 0;
    REQUIRE_FALSE(ep.request_reading(cfg, take_sample, out_value));
    REQUIRE(calls == 6); // 2 * 3, no free-running/extra sampling
    REQUIRE(out_value == 42);
}

// ── Two self-triggering cadence patterns ─────────────────────────────────────

TEST_CASE("request_reading_from_trigger_queue implements the ExternalTrigger cadence pattern",
          "[adc][REQ-ADC-004]") {
    AdcEndpoint ep;
    AdcAveragingConfig cfg;
    cfg.adc_avg_intervals_per_request = 2;
    cfg.adc_combine_avg_values        = 2;

    std::vector<std::optional<uint32_t>> queue{10u, 20u, 30u, 40u, 999u}; // one extra entry left over

    uint32_t out_value = 0;
    auto ec = ep.request_reading_from_trigger_queue(cfg, queue, out_value);
    REQUIRE_FALSE(ec);
    REQUIRE(out_value == 25); // same combination as the SelfTimed test above
    REQUIRE(queue.size() == 1); // exactly the consumed entries were removed
    REQUIRE(*queue[0] == 999u);
}

TEST_CASE("request_reading_from_trigger_queue reports no_signal on queue underrun",
          "[adc][REQ-ADC-004]") {
    AdcEndpoint ep;
    AdcAveragingConfig cfg;
    cfg.adc_avg_intervals_per_request = 2;
    cfg.adc_combine_avg_values        = 2;

    std::vector<std::optional<uint32_t>> queue{1u, 2u}; // fewer than the 4 needed
    uint32_t out_value = 0;
    auto ec = ep.request_reading_from_trigger_queue(cfg, queue, out_value);
    REQUIRE(ec == make_error_code(AdcErrc::no_signal));
}

TEST_CASE("request_reading_from_trigger_queue reports no_signal on a missing-capture entry",
          "[adc][REQ-ADC-005]") {
    AdcEndpoint ep;
    AdcAveragingConfig cfg;
    cfg.adc_avg_intervals_per_request = 2;
    cfg.adc_combine_avg_values        = 1;

    std::vector<std::optional<uint32_t>> queue{10u, std::nullopt}; // trigger occurred, no valid capture
    uint32_t out_value = 0;
    auto ec = ep.request_reading_from_trigger_queue(cfg, queue, out_value);
    REQUIRE(ec == make_error_code(AdcErrc::no_signal));
}

// ── AdcErrc category sanity ────────────────────────────────────────────────────

TEST_CASE("AdcErrc reports a non-empty message in its own category", "[adc][REQ-ADC-006]") {
    auto ec = make_error_code(AdcErrc::no_signal);
    REQUIRE(ec.category() == adc_category());
    REQUIRE_FALSE(ec.message().empty());
}
