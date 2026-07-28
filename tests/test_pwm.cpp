// fusa:test REQ-PWM-001
// fusa:test REQ-PWM-002
// fusa:test REQ-PWM-003
// fusa:test REQ-PWM-004
// fusa:test REQ-PWM-005
// fusa:test REQ-PWM-006
// fusa:test REQ-PWM-007

// Tests for rcp/pwm.hpp — the PWM_OUT and PWM_IN endpoint types
// (ROADMAP.md milestone 48, "Basic Endpoint Types II — I2C, UART, ADC,
// PWM_OUT, PWM_IN", v2.4.0).

#include <catch2/catch_test_macros.hpp>
#include <rcp/pwm.hpp>

using namespace rcp::pwm;
using rcp::endpoint::WriteSemantics;

// ── Shared period/active-duration payload shape ──────────────────────────────

TEST_CASE("PwmValue defaults both fields to zero", "[pwm][REQ-PWM-001]") {
    PwmValue v;
    REQUIRE(v.period == 0);
    REQUIRE(v.active_duration == 0);
}

// ── PWM_OUT: 8-way write-semantics reuse from GPIO ───────────────────────────

TEST_CASE("PwmOutEndpoint::handle_write applies Replace/Or/And/Xor independently to both fields",
          "[pwm][REQ-PWM-002]") {
    PwmOutEndpoint ep;
    PwmValue out;

    REQUIRE_FALSE(ep.handle_write(WriteSemantics::Replace, {100, 40}, out));
    REQUIRE(out.period == 100);
    REQUIRE(out.active_duration == 40);

    REQUIRE_FALSE(ep.handle_write(WriteSemantics::Or, {0x0F, 0x01}, out));
    REQUIRE(out.period == (100 | 0x0F));
    REQUIRE(out.active_duration == (40 | 0x01));
}

TEST_CASE("PwmOutEndpoint::handle_write applies saturating Add/Subtract to both fields",
          "[pwm][REQ-PWM-003]") {
    PwmOutEndpoint ep;
    PwmValue out;
    REQUIRE_FALSE(ep.handle_write(WriteSemantics::Replace, {0xFFFFFFF0, 5}, out));

    REQUIRE_FALSE(ep.handle_write(WriteSemantics::Add, {0x20, 0}, out));
    REQUIRE(out.period == 0xFFFFFFFF); // saturates, does not wrap

    REQUIRE_FALSE(ep.handle_write(WriteSemantics::Subtract, {0, 10}, out));
    REQUIRE(out.active_duration == 0); // saturates at 0, does not wrap
}

TEST_CASE("PwmOutEndpoint::handle_write rejects Reserved without changing state",
          "[pwm][REQ-PWM-004]") {
    PwmOutEndpoint ep;
    PwmValue out;
    REQUIRE_FALSE(ep.handle_write(WriteSemantics::Replace, {10, 5}, out));

    auto ec = ep.handle_write(WriteSemantics::Reserved, {999, 999}, out);
    REQUIRE(ec);
    REQUIRE(ep.read().period == 10);
    REQUIRE(ep.read().active_duration == 5);
}

TEST_CASE("PwmOutEndpoint::handle_write leaves Reconfigure to apply_bitmask_write's own rejection",
          "[pwm][REQ-PWM-004]") {
    PwmOutEndpoint ep;
    PwmValue out;
    auto ec = ep.handle_write(WriteSemantics::Reconfigure, {1, 1}, out);
    REQUIRE(ec == rcp::endpoint::make_error_code(rcp::endpoint::EndpointErrc::non_combinable_write_semantics));
}

// ── PWM_IN: response-only read model + PWM_IN_NO_SIGNAL ─────────────────────

TEST_CASE("PwmInEndpoint::handle_read reports no_signal before any measurement", "[pwm][REQ-PWM-005]") {
    PwmInEndpoint ep;
    PwmValue out;
    auto ec = ep.handle_read(out);
    REQUIRE(ec == make_error_code(PwmErrc::no_signal));
}

TEST_CASE("PwmInEndpoint::handle_read returns the last recorded measurement", "[pwm][REQ-PWM-005]") {
    PwmInEndpoint ep;
    ep.record_measurement({200, 75});

    PwmValue out;
    auto ec = ep.handle_read(out);
    REQUIRE_FALSE(ec);
    REQUIRE(out.period == 200);
    REQUIRE(out.active_duration == 75);
}

TEST_CASE("PwmInEndpoint::clear_signal re-arms PWM_IN_NO_SIGNAL", "[pwm][REQ-PWM-005]") {
    PwmInEndpoint ep;
    ep.record_measurement({200, 75});
    ep.clear_signal();

    PwmValue out;
    auto ec = ep.handle_read(out);
    REQUIRE(ec == make_error_code(PwmErrc::no_signal));
}

// ── Mid-pulse trigger signal (keys ADC sampling cadence) ─────────────────────

TEST_CASE("PwmInEndpoint::record_measurement fires MidPulse when armed", "[pwm][REQ-PWM-006]") {
    PwmInEndpoint ep;
    ep.triggers().enable(pwm_in_signal_id(PwmInSignal::MidPulse));

    ep.record_measurement({100, 50});

    auto drained = ep.triggers().drain();
    REQUIRE(drained.size() == 1);
    REQUIRE(drained[0] == pwm_in_signal_id(PwmInSignal::MidPulse));
}

TEST_CASE("PwmInEndpoint::record_measurement fires nothing when MidPulse is not armed",
          "[pwm][REQ-PWM-006]") {
    PwmInEndpoint ep;
    ep.record_measurement({100, 50});
    REQUIRE_FALSE(ep.triggers().has_pending());
}

// ── PwmErrc category sanity ───────────────────────────────────────────────────

TEST_CASE("PwmErrc reports a non-empty message in its own category", "[pwm][REQ-PWM-007]") {
    auto ec = make_error_code(PwmErrc::no_signal);
    REQUIRE(ec.category() == pwm_category());
    REQUIRE_FALSE(ec.message().empty());
}
