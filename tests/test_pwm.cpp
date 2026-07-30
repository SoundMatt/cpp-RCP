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
#include <rcp/avtp.hpp>
#include <rcp/pwm.hpp>

using namespace rcp::pwm;
using rcp::endpoint::WriteSemantics;

// ── Shared period/active-duration payload shape ──────────────────────────────

TEST_CASE("PwmValue defaults both fields to zero", "[pwm][REQ-PWM-001]") {
    PwmValue v;
    REQUIRE(v.period == 0);
    REQUIRE(v.active_duration == 0);
}

// ── Wire codec: fixed 4-byte payload, period then active, big-endian ────────
// Verified against the spec's "pwmo request format" figure (§13.7.5.3),
// which shows PWM_Period occupying the first two bytes of the payload and
// PWM_active the last two.

TEST_CASE("kPwmPayloadLen is exactly 4 bytes", "[pwm][REQ-PWM-001]") {
    REQUIRE(kPwmPayloadLen == 4);
    REQUIRE(sizeof(PwmWireBytes) == 4);
}

TEST_CASE("encode_pwm_payload places period first, then active_duration, big-endian",
          "[pwm][REQ-PWM-001]") {
    PwmValue v;
    v.period          = 0x1234;
    v.active_duration = 0x5678;

    auto wire = encode_pwm_payload(v);
    REQUIRE(wire == PwmWireBytes{0x12, 0x34, 0x56, 0x78});
}

TEST_CASE("decode_pwm_payload round-trips encode_pwm_payload", "[pwm][REQ-PWM-001]") {
    PwmValue in;
    in.period          = 60000;
    in.active_duration = 12345;

    auto wire = encode_pwm_payload(in);

    PwmValue out;
    auto ec = decode_pwm_payload(wire.data(), wire.size(), out);
    REQUIRE_FALSE(ec);
    REQUIRE(out.period == in.period);
    REQUIRE(out.active_duration == in.active_duration);
}

TEST_CASE("decode_pwm_payload reports short_buffer for fewer than 4 bytes", "[pwm][REQ-PWM-001]") {
    uint8_t short_buf[3] = {0, 0, 0};
    PwmValue out;
    auto ec = decode_pwm_payload(short_buf, sizeof(short_buf), out);
    REQUIRE(ec == rcp::avtp::make_error_code(rcp::avtp::AvtpErrc::short_buffer));
}

// ── PWM_OUT: Replace applies directly, every other semantics is rejected ────
// The spec's PWM_OUT request-handling section describes only direct
// application of the request's period/active values — no GPIO-style
// bitmask/saturating-write combination against the previous state — so
// handle_write no longer reuses rcp::endpoint::apply_bitmask_write.

TEST_CASE("PwmOutEndpoint::handle_write applies Replace directly", "[pwm][REQ-PWM-002]") {
    PwmOutEndpoint ep;
    PwmValue out;

    REQUIRE_FALSE(ep.handle_write(WriteSemantics::Replace, {100, 40}, out));
    REQUIRE(out.period == 100);
    REQUIRE(out.active_duration == 40);

    // A second Replace fully overwrites the previous state (no combination
    // against it — e.g. this is not an OR of 0x0F/0x01 against {100, 40}).
    REQUIRE_FALSE(ep.handle_write(WriteSemantics::Replace, {0x0F, 0x01}, out));
    REQUIRE(out.period == 0x0F);
    REQUIRE(out.active_duration == 0x01);
}

TEST_CASE("PwmOutEndpoint::handle_write applying a period of 0 is a normal Replace (stop request)",
          "[pwm][REQ-PWM-002]") {
    PwmOutEndpoint ep;
    PwmValue out;
    REQUIRE_FALSE(ep.handle_write(WriteSemantics::Replace, {500, 250}, out));
    REQUIRE_FALSE(ep.handle_write(WriteSemantics::Replace, {0, 0}, out));
    REQUIRE(out.period == 0);
    REQUIRE(ep.read().period == 0);
}

TEST_CASE("PwmOutEndpoint::handle_write rejects Or/And/Xor/Add/Subtract as non-combinable",
          "[pwm][REQ-PWM-003]") {
    PwmOutEndpoint ep;
    PwmValue out;
    REQUIRE_FALSE(ep.handle_write(WriteSemantics::Replace, {10, 5}, out));

    for (auto op : {WriteSemantics::Or, WriteSemantics::And, WriteSemantics::Xor,
                     WriteSemantics::Add, WriteSemantics::Subtract}) {
        auto ec = ep.handle_write(op, {999, 999}, out);
        REQUIRE(ec == rcp::endpoint::make_error_code(
                           rcp::endpoint::EndpointErrc::non_combinable_write_semantics));
        // State is unchanged by a rejected write.
        REQUIRE(ep.read().period == 10);
        REQUIRE(ep.read().active_duration == 5);
    }
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

TEST_CASE("PwmOutEndpoint::handle_write rejects Reconfigure (PWM_OUT defines no Reconfigure target)",
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
