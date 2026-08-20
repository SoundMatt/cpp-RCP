// fusa:test REQ-PWM-001
// fusa:test REQ-PWM-002
// fusa:test REQ-PWM-003
// fusa:test REQ-PWM-004
// fusa:test REQ-PWM-005
// fusa:test REQ-PWM-006
// fusa:test REQ-PWM-007
// fusa:test REQ-PWM-008
// fusa:test REQ-PWM-009

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

TEST_CASE("decode_pwm_payload rejects an over-long buffer (spec requires exactly 4 bytes)",
          "[pwm][REQ-PWM-001]") {
    // §13.7.5.3: "A request not having exactly four bytes is rejected" —
    // cpp-RCP-03. Trailing bytes must not be silently ignored.
    uint8_t long_buf[5] = {0, 0, 0, 0, 0};
    PwmValue out;
    auto ec = decode_pwm_payload(long_buf, sizeof(long_buf), out);
    REQUIRE(ec == rcp::avtp::make_error_code(rcp::avtp::AvtpErrc::short_buffer));
}

// ── PWM_OUT: Replace applies directly, every other semantics is rejected ────
// TC18 §13.5 Table 30's GPIO/PWM_OUT row assigns PWM_OUT the same eight
// write semantics as GPIO — Replace/Or/And/Xor/Add/Subtract combine against
// the endpoint's current period/active_duration, each independently and
// each saturating within its own uint16_t range (Table 30's own saturation
// note); Reserved is rejected; Reconfigure has no target in this codebase
// yet (no EP_func addressed-write path exists for any endpoint but one).

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

TEST_CASE("PwmOutEndpoint::handle_write applies Or/And/Xor against the previous state, per field",
          "[pwm][REQ-PWM-003]") {
    PwmOutEndpoint ep;
    PwmValue out;
    REQUIRE_FALSE(ep.handle_write(WriteSemantics::Replace, {0x0F00, 0x00F0}, out));

    REQUIRE_FALSE(ep.handle_write(WriteSemantics::Or, {0x00F0, 0x000F}, out));
    REQUIRE(out.period == 0x0FF0);
    REQUIRE(out.active_duration == 0x00FF);

    REQUIRE_FALSE(ep.handle_write(WriteSemantics::And, {0x0FF0, 0x00FF}, out));
    REQUIRE(out.period == 0x0FF0);
    REQUIRE(out.active_duration == 0x00FF);

    REQUIRE_FALSE(ep.handle_write(WriteSemantics::Xor, {0x00F0, 0x00FF}, out));
    REQUIRE(out.period == 0x0F00);
    REQUIRE(out.active_duration == 0x0000);
}

TEST_CASE("PwmOutEndpoint::handle_write applies saturating Add/Subtract per field, "
          "independently of GPIO's 32-bit saturation bound",
          "[pwm][REQ-PWM-003]") {
    PwmOutEndpoint ep;
    PwmValue out;
    REQUIRE_FALSE(ep.handle_write(WriteSemantics::Replace, {0xFFF0, 10}, out));

    // Saturates at 0xFFFF (16-bit), not 0xFFFFFFFF (32-bit, GPIO's own
    // bound) — the exact bug class this test guards against: applying
    // GPIO's 32-bit combinator directly to a 16-bit field and truncating
    // the result would wrap instead of saturate.
    REQUIRE_FALSE(ep.handle_write(WriteSemantics::Add, {0x0020, 5}, out));
    REQUIRE(out.period == 0xFFFF);
    REQUIRE(out.active_duration == 15);

    REQUIRE_FALSE(ep.handle_write(WriteSemantics::Subtract, {0xFFFF, 20}, out));
    REQUIRE(out.period == 0);
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

// ── Rising/falling-edge trigger signals (Table 44; issue cpp-RCP-A4-pwmin) ───

TEST_CASE("PwmInEndpoint::record_measurement fires both RisingEdge and FallingEdge when armed",
          "[pwm][REQ-PWM-006]") {
    PwmInEndpoint ep;
    ep.triggers().enable(pwm_in_signal_id(PwmInSignal::RisingEdge));
    ep.triggers().enable(pwm_in_signal_id(PwmInSignal::FallingEdge));

    ep.record_measurement({100, 50});

    auto drained = ep.triggers().drain();
    REQUIRE(drained.size() == 2);
    REQUIRE(drained[0] == pwm_in_signal_id(PwmInSignal::RisingEdge));
    REQUIRE(drained[1] == pwm_in_signal_id(PwmInSignal::FallingEdge));
}

TEST_CASE("PwmInEndpoint::record_measurement fires nothing when neither edge is armed",
          "[pwm][REQ-PWM-006]") {
    PwmInEndpoint ep;
    ep.record_measurement({100, 50});
    REQUIRE_FALSE(ep.triggers().has_pending());
}

TEST_CASE("PwmInEndpoint::record_edge fires exactly the requested edge", "[pwm][REQ-PWM-006]") {
    PwmInEndpoint ep;
    ep.triggers().enable(pwm_in_signal_id(PwmInSignal::RisingEdge));
    ep.triggers().enable(pwm_in_signal_id(PwmInSignal::FallingEdge));

    ep.record_edge(PwmInSignal::RisingEdge);
    auto drained = ep.triggers().drain();
    REQUIRE(drained.size() == 1);
    REQUIRE(drained[0] == pwm_in_signal_id(PwmInSignal::RisingEdge));

    ep.record_edge(PwmInSignal::FallingEdge);
    drained = ep.triggers().drain();
    REQUIRE(drained.size() == 1);
    REQUIRE(drained[0] == pwm_in_signal_id(PwmInSignal::FallingEdge));
}

// ── Table 33 Row 2 evt[2:0] validation (handle_request) ─────────────────────
// Table 33 Row 2's shared evt[2:0] classification (Plain/Reserved/
// ConfigWrite, exercised for all 8 evt_op values by
// tests/test_endpoint.cpp's own "evt_row2_kind_of classifies all 8 evt[2:0]
// values" case) applied to PWM_IN's own request-decode entry point,
// mirroring tests/test_i2c.cpp's and tests/test_adc.cpp's "Table 33 Row 2
// evt[2:0] validation (handle_request)" sections exactly — see
// rcp/pwm.hpp's own PwmInEndpoint::handle_request comment.

TEST_CASE("PwmInEndpoint::handle_request delegates a Plain (evt[2:0]==000b) request to "
          "handle_read()",
          "[pwm][REQ-PWM-008]") {
    PwmInEndpoint ep;
    ep.record_measurement({200, 75});

    PwmValue out;
    auto ec = ep.handle_request(/*evt_op=*/0, out);
    REQUIRE_FALSE(ec);
    REQUIRE(out.period == 200);
    REQUIRE(out.active_duration == 75);
}

TEST_CASE("PwmInEndpoint::handle_request Plain surfaces no_signal before any measurement, "
          "same as handle_read()",
          "[pwm][REQ-PWM-008]") {
    PwmInEndpoint ep;
    PwmValue out;
    auto ec = ep.handle_request(/*evt_op=*/0, out);
    REQUIRE(ec == make_error_code(PwmErrc::no_signal));
}

TEST_CASE("PwmInEndpoint::handle_request rejects every reserved evt[2:0] value (001b-110b) "
          "without touching out_value",
          "[pwm][REQ-PWM-008]") {
    for (uint8_t evt_op = 1; evt_op <= 6; ++evt_op) {
        PwmInEndpoint ep;
        ep.record_measurement({111, 22});

        PwmValue out;
        auto ec = ep.handle_request(evt_op, out);
        REQUIRE(ec == rcp::endpoint::make_error_code(rcp::endpoint::EndpointErrc::reserved_evt_row2));
        REQUIRE(out.period == 0); // untouched — still default-constructed
        REQUIRE(out.active_duration == 0);
    }
}

TEST_CASE("PwmInEndpoint::handle_request reports config_write_not_supported for evt[2:0]==111b "
          "without touching out_value",
          "[pwm][REQ-PWM-009]") {
    PwmInEndpoint ep;
    ep.record_measurement({111, 22});

    PwmValue out;
    auto ec = ep.handle_request(/*evt_op=*/7, out);
    REQUIRE(ec == make_error_code(PwmErrc::config_write_not_supported));
    REQUIRE(out.period == 0);
    REQUIRE(out.active_duration == 0);
}

TEST_CASE("PwmInEndpoint::handle_request masks evt_op down to 3 bits before classifying",
          "[pwm][REQ-PWM-008]") {
    PwmInEndpoint ep;
    ep.record_measurement({9, 4});

    PwmValue out;
    REQUIRE_FALSE(ep.handle_request(/*evt_op=*/0xF8, out)); // low 3 bits 000 -> Plain
    REQUIRE(out.period == 9);
    REQUIRE(out.active_duration == 4);

    auto ec = ep.handle_request(/*evt_op=*/0xF9, out); // low 3 bits 001 -> Reserved
    REQUIRE(ec == rcp::endpoint::make_error_code(rcp::endpoint::EndpointErrc::reserved_evt_row2));
}

// ── PwmErrc category sanity ───────────────────────────────────────────────────

TEST_CASE("PwmErrc reports a non-empty message in its own category", "[pwm][REQ-PWM-007]") {
    auto ec = make_error_code(PwmErrc::no_signal);
    REQUIRE(ec.category() == pwm_category());
    REQUIRE_FALSE(ec.message().empty());
}

TEST_CASE("PwmErrc::config_write_not_supported reports a non-empty message in its own category",
          "[pwm][REQ-PWM-009]") {
    auto ec = make_error_code(PwmErrc::config_write_not_supported);
    REQUIRE(ec.category() == pwm_category());
    REQUIRE_FALSE(ec.message().empty());
}
