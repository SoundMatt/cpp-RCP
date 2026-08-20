// fusa:test REQ-ADC-001
// fusa:test REQ-ADC-002
// fusa:test REQ-ADC-003
// fusa:test REQ-ADC-004
// fusa:test REQ-ADC-005
// fusa:test REQ-ADC-006
// fusa:test REQ-ADC-007
// fusa:test REQ-ADC-008
// fusa:test REQ-ADC-009

// Tests for rcp/adc.hpp — the ADC endpoint type (ROADMAP.md milestone 48,
// "Basic Endpoint Types II — I2C, UART, ADC, PWM_OUT, PWM_IN", v2.4.0).

#include <catch2/catch_test_macros.hpp>
#include <rcp/adc.hpp>

#include <deque>

using namespace rcp::adc;

// ── Three-level averaging model ──────────────────────────────────────────────

TEST_CASE("compute_average computes an arithmetic mean", "[adc][REQ-ADC-001]") {
    uint16_t out = 0;
    REQUIRE_FALSE(compute_average({10, 20, 30}, out));
    REQUIRE(out == 20);
}

TEST_CASE("compute_average reports no_signal on an empty sample set", "[adc][REQ-ADC-001]") {
    uint16_t out = 0;
    REQUIRE(compute_average({}, out) == make_error_code(AdcErrc::no_signal));
}

// ADC's resolution ceiling is 16 bits (§13.7.9.1) — compute_average must
// average values right up to that ceiling without overflowing internally
// (it widens to a 64-bit accumulator before narrowing the result back to
// uint16_t), unlike the previous uint32_t-everywhere model this replaces.
TEST_CASE("compute_average handles samples at the 16-bit ceiling without overflow",
          "[adc][REQ-ADC-001]") {
    uint16_t out = 0;
    REQUIRE_FALSE(compute_average({0xFFFF, 0xFFFF, 0xFFFF}, out));
    REQUIRE(out == 0xFFFF);
}

TEST_CASE("request_reading combines level-1 and level-2 averaging correctly", "[adc][REQ-ADC-002]") {
    AdcEndpoint ep;
    AdcAveragingConfig cfg;
    cfg.adc_avg_intervals_per_request = 2; // level 1: average 2 raw samples per interval
    cfg.adc_combine_avg_values        = 2; // level 2: average 2 intervals

    // Raw samples, consumed in order: interval 1 = {10,20} -> avg 15;
    // interval 2 = {30,40} -> avg 35; combined = avg(15,35) = 25.
    std::deque<uint16_t> raw{10, 20, 30, 40};
    auto take_sample = [&raw]() -> std::optional<uint16_t> {
        if (raw.empty()) return std::nullopt;
        uint16_t v = raw.front();
        raw.pop_front();
        return v;
    };

    uint16_t out_value = 0;
    auto ec = ep.request_reading(cfg, take_sample, out_value);
    REQUIRE_FALSE(ec);
    REQUIRE(out_value == 25);
}

TEST_CASE("request_reading rejects a zero-valued averaging config field", "[adc][REQ-ADC-002]") {
    AdcEndpoint ep;
    AdcAveragingConfig cfg;
    cfg.adc_avg_intervals_per_request = 0;
    auto take_sample = []() -> std::optional<uint16_t> { return uint16_t{1}; };

    uint16_t out_value = 0;
    auto ec = ep.request_reading(cfg, take_sample, out_value);
    REQUIRE(ec == make_error_code(AdcErrc::invalid_averaging_config));
}

// ── Request-driven sampling only / no-signal timeout path ───────────────────

TEST_CASE("request_reading reports no_signal when take_sample underruns", "[adc][REQ-ADC-003]") {
    AdcEndpoint ep;
    AdcAveragingConfig cfg;
    cfg.adc_avg_intervals_per_request = 3;
    cfg.adc_combine_avg_values        = 1;

    auto take_sample = []() -> std::optional<uint16_t> { return std::nullopt; }; // no signal captured

    uint16_t out_value = 0;
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
    auto take_sample = [&calls]() -> std::optional<uint16_t> {
        ++calls;
        return uint16_t{42};
    };

    uint16_t out_value = 0;
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

    std::vector<std::optional<uint16_t>> queue{uint16_t{10}, uint16_t{20}, uint16_t{30}, uint16_t{40}, uint16_t{999}}; // one extra entry left over

    uint16_t out_value = 0;
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

    std::vector<std::optional<uint16_t>> queue{uint16_t{1}, uint16_t{2}}; // fewer than the 4 needed
    uint16_t out_value = 0;
    auto ec = ep.request_reading_from_trigger_queue(cfg, queue, out_value);
    REQUIRE(ec == make_error_code(AdcErrc::no_signal));
}

TEST_CASE("request_reading_from_trigger_queue reports no_signal on a missing-capture entry",
          "[adc][REQ-ADC-005]") {
    AdcEndpoint ep;
    AdcAveragingConfig cfg;
    cfg.adc_avg_intervals_per_request = 2;
    cfg.adc_combine_avg_values        = 1;

    std::vector<std::optional<uint16_t>> queue{uint16_t{10}, std::nullopt}; // trigger occurred, no valid capture
    uint16_t out_value = 0;
    auto ec = ep.request_reading_from_trigger_queue(cfg, queue, out_value);
    REQUIRE(ec == make_error_code(AdcErrc::no_signal));
}

// ── AdcErrc category sanity ────────────────────────────────────────────────────

TEST_CASE("AdcErrc reports a non-empty message in its own category", "[adc][REQ-ADC-006]") {
    auto ec = make_error_code(AdcErrc::no_signal);
    REQUIRE(ec.category() == adc_category());
    REQUIRE_FALSE(ec.message().empty());
}

// AdcErrc::no_signal must not claim a TC18-defined error-code identity it
// does not have (issue #77): Table 27 defines no ADC-specific no-signal
// code, so the message must not contain an invented "ADC_NO_SIGNAL"
// identifier.
TEST_CASE("AdcErrc::no_signal's message does not claim an invented ADC_NO_SIGNAL spec identifier",
          "[adc][REQ-ADC-006]") {
    auto ec = make_error_code(AdcErrc::no_signal);
    REQUIRE(ec.message().find("ADC_NO_SIGNAL") == std::string::npos);
}

// ── Wire codec ────────────────────────────────────────────────────────────────

TEST_CASE("encode_adc_value encodes a single measurement as 2-byte big-endian",
          "[adc][REQ-ADC-007]") {
    REQUIRE(encode_adc_value(0x0000) == std::vector<uint8_t>{0x00, 0x00});
    REQUIRE(encode_adc_value(0xABCD) == std::vector<uint8_t>{0xAB, 0xCD});
    REQUIRE(encode_adc_value(0xFFFF) == std::vector<uint8_t>{0xFF, 0xFF});
}

// ── Table 33 Row 2 evt[2:0] validation (handle_request) ──────────────────────
// Table 33 Row 2's shared evt[2:0] classification (Plain/Reserved/
// ConfigWrite, exercised for all 8 evt_op values by
// tests/test_endpoint.cpp's own "evt_row2_kind_of classifies all 8 evt[2:0]
// values" case) applied to ADC's own request-decode entry point, mirroring
// tests/test_i2c.cpp's "Table 33 Row 2 evt[2:0] validation (handle_request)"
// section exactly — see rcp/adc.hpp's own handle_request comment.

TEST_CASE("AdcEndpoint::handle_request delegates a Plain (evt[2:0]==000b) request to "
          "request_reading()",
          "[adc][REQ-ADC-008]") {
    AdcEndpoint ep;
    AdcAveragingConfig cfg;
    cfg.adc_avg_intervals_per_request = 2;
    cfg.adc_combine_avg_values        = 2;

    std::deque<uint16_t> raw{10, 20, 30, 40};
    auto take_sample = [&raw]() -> std::optional<uint16_t> {
        if (raw.empty()) return std::nullopt;
        uint16_t v = raw.front();
        raw.pop_front();
        return v;
    };

    uint16_t out_value = 0;
    auto ec = ep.handle_request(/*evt_op=*/0, cfg, take_sample, out_value);
    REQUIRE_FALSE(ec);
    REQUIRE(out_value == 25); // same combination as request_reading's own direct-call test
}

TEST_CASE("AdcEndpoint::handle_request rejects every reserved evt[2:0] value (001b-110b) "
          "without invoking take_sample",
          "[adc][REQ-ADC-008]") {
    AdcAveragingConfig cfg; // default: 1x1, no averaging
    for (uint8_t evt_op = 1; evt_op <= 6; ++evt_op) {
        AdcEndpoint ep;
        bool take_sample_called = false;
        auto take_sample = [&take_sample_called]() -> std::optional<uint16_t> {
            take_sample_called = true;
            return uint16_t{1};
        };
        uint16_t out_value = 0;
        auto ec = ep.handle_request(evt_op, cfg, take_sample, out_value);
        REQUIRE(ec == rcp::endpoint::make_error_code(rcp::endpoint::EndpointErrc::reserved_evt_row2));
        REQUIRE_FALSE(take_sample_called);
    }
}

TEST_CASE("AdcEndpoint::handle_request reports config_write_not_supported for evt[2:0]==111b "
          "without invoking take_sample",
          "[adc][REQ-ADC-009]") {
    AdcEndpoint ep;
    AdcAveragingConfig cfg;
    bool take_sample_called = false;
    auto take_sample = [&take_sample_called]() -> std::optional<uint16_t> {
        take_sample_called = true;
        return uint16_t{1};
    };
    uint16_t out_value = 0;
    auto ec = ep.handle_request(/*evt_op=*/7, cfg, take_sample, out_value);
    REQUIRE(ec == make_error_code(AdcErrc::config_write_not_supported));
    REQUIRE_FALSE(take_sample_called);
}

TEST_CASE("AdcEndpoint::handle_request masks evt_op down to 3 bits before classifying",
          "[adc][REQ-ADC-008]") {
    AdcEndpoint ep;
    AdcAveragingConfig cfg;
    auto take_sample = []() -> std::optional<uint16_t> { return uint16_t{7}; };

    uint16_t out_value = 0;
    REQUIRE_FALSE(ep.handle_request(/*evt_op=*/0xF8, cfg, take_sample, out_value)); // low 3 bits 000 -> Plain
    REQUIRE(out_value == 7);

    auto ec = ep.handle_request(/*evt_op=*/0xF9, cfg, take_sample, out_value); // low 3 bits 001 -> Reserved
    REQUIRE(ec == rcp::endpoint::make_error_code(rcp::endpoint::EndpointErrc::reserved_evt_row2));
}

TEST_CASE("AdcErrc::config_write_not_supported reports a non-empty message in its own category",
          "[adc][REQ-ADC-009]") {
    auto ec = make_error_code(AdcErrc::config_write_not_supported);
    REQUIRE(ec.category() == adc_category());
    REQUIRE_FALSE(ec.message().empty());
}
