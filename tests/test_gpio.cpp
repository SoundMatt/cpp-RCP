// fusa:test REQ-GPIO-001
// fusa:test REQ-GPIO-002
// fusa:test REQ-GPIO-003
// fusa:test REQ-GPIO-004
// fusa:test REQ-GPIO-005
// fusa:test REQ-GPIO-006
// fusa:test REQ-GPIO-007
// fusa:test REQ-GPIO-008

// Tests for rcp/gpio.hpp — the GPIO endpoint type (ROADMAP.md milestone 47,
// "Basic Endpoint Types I — GPIO & SPI", v2.3.0).

#include <catch2/catch_test_macros.hpp>
#include <rcp/gpio.hpp>

using namespace rcp::gpio;
using rcp::endpoint::WriteSemantics;

// ── Payload shape ─────────────────────────────────────────────────────────────

TEST_CASE("GPIO payload is a 4-byte bitmask", "[gpio][REQ-GPIO-001]") {
    REQUIRE(kGpioPayloadLen == 4);
    REQUIRE(kMaxPins == 32);
}

TEST_CASE("encode_gpio_payload / decode_gpio_payload round-trip big-endian", "[gpio][REQ-GPIO-001]") {
    auto buf = encode_gpio_payload(0x01020304);
    REQUIRE(buf == std::vector<uint8_t>{0x01, 0x02, 0x03, 0x04});

    PinMask out = 0;
    auto ec = decode_gpio_payload(buf.data(), buf.size(), out);
    REQUIRE_FALSE(ec);
    REQUIRE(out == 0x01020304);
}

TEST_CASE("decode_gpio_payload rejects a short buffer", "[gpio][REQ-GPIO-001]") {
    std::vector<uint8_t> short_buf{0x01, 0x02};
    PinMask out = 0;
    auto ec = decode_gpio_payload(short_buf.data(), short_buf.size(), out);
    REQUIRE(ec == rcp::avtp::make_error_code(rcp::avtp::AvtpErrc::short_buffer));
}

TEST_CASE("decode_gpio_payload rejects an over-long buffer (spec requires exactly 4 bytes)",
          "[gpio][REQ-GPIO-001]") {
    // §13.7.4: "A request not having exactly four bytes is rejected" —
    // cpp-RCP-05-fresh. Trailing bytes must not be silently ignored.
    std::vector<uint8_t> long_buf{0x01, 0x02, 0x03, 0x04, 0x05};
    PinMask out = 0;
    auto ec = decode_gpio_payload(long_buf.data(), long_buf.size(), out);
    REQUIRE(ec == rcp::avtp::make_error_code(rcp::avtp::AvtpErrc::short_buffer));
}

// ── Write semantics: the 6 generic combinators ───────────────────────────────

TEST_CASE("apply_gpio_write applies Replace/Or/And/Xor to state.values", "[gpio][REQ-GPIO-002]") {
    GpioState state;
    state.values = 0x0000FFFF;

    REQUIRE_FALSE(apply_gpio_write(WriteSemantics::Or, state, 0xFFFF0000));
    REQUIRE(state.values == 0xFFFFFFFF);

    REQUIRE_FALSE(apply_gpio_write(WriteSemantics::And, state, 0x0F0F0F0F));
    REQUIRE(state.values == 0x0F0F0F0F);

    REQUIRE_FALSE(apply_gpio_write(WriteSemantics::Xor, state, 0xFFFFFFFF));
    REQUIRE(state.values == 0xF0F0F0F0);

    REQUIRE_FALSE(apply_gpio_write(WriteSemantics::Replace, state, 0x00000001));
    REQUIRE(state.values == 0x00000001);
}

// ── Write semantics: saturating Add/Subtract shared with PWM_OUT ────────────

TEST_CASE("apply_gpio_write applies saturating Add/Subtract to state.values", "[gpio][REQ-GPIO-003]") {
    GpioState state;
    state.values = 0xFFFFFFF0;

    REQUIRE_FALSE(apply_gpio_write(WriteSemantics::Add, state, 0x20));
    REQUIRE(state.values == 0xFFFFFFFF); // saturates, does not wrap past max

    state.values = 5;
    REQUIRE_FALSE(apply_gpio_write(WriteSemantics::Subtract, state, 10));
    REQUIRE(state.values == 0); // saturates at 0, does not wrap
}

// ── Write semantics: Reserved rejected, Reconfigure targets directions ──────

TEST_CASE("apply_gpio_write rejects Reserved and leaves state untouched", "[gpio][REQ-GPIO-004]") {
    GpioState state;
    state.values = 0x12345678;
    auto ec = apply_gpio_write(WriteSemantics::Reserved, state, 0xFFFFFFFF);
    REQUIRE(ec);
    REQUIRE(state.values == 0x12345678);
}

TEST_CASE("apply_gpio_write's Reconfigure replaces state.directions, not state.values",
          "[gpio][REQ-GPIO-004]") {
    GpioState state;
    state.values     = 0xAAAAAAAA;
    state.directions = 0;

    REQUIRE_FALSE(apply_gpio_write(WriteSemantics::Reconfigure, state, 0x0000FFFF));
    REQUIRE(state.directions == 0x0000FFFF);
    REQUIRE(state.values == 0xAAAAAAAA); // unaffected by a Reconfigure write
}

// ── Per-pin change/rising/falling trigger signals ────────────────────────────

TEST_CASE("evaluate_gpio_triggers fires Change+Rising for a 0->1 transition when both are armed",
          "[gpio][REQ-GPIO-005]") {
    rcp::endpoint::TriggerRegistry triggers;
    triggers.enable(gpio_signal_id(3, GpioEdge::Change));
    triggers.enable(gpio_signal_id(3, GpioEdge::Rising));
    triggers.enable(gpio_signal_id(3, GpioEdge::Falling));

    evaluate_gpio_triggers(triggers, /*old=*/0, /*new=*/(1u << 3));

    auto drained = triggers.drain();
    REQUIRE(drained.size() == 2);
    REQUIRE(drained[0] == gpio_signal_id(3, GpioEdge::Change));
    REQUIRE(drained[1] == gpio_signal_id(3, GpioEdge::Rising));
}

TEST_CASE("evaluate_gpio_triggers fires Change+Falling for a 1->0 transition", "[gpio][REQ-GPIO-005]") {
    rcp::endpoint::TriggerRegistry triggers;
    triggers.enable(gpio_signal_id(7, GpioEdge::Change));
    triggers.enable(gpio_signal_id(7, GpioEdge::Falling));

    evaluate_gpio_triggers(triggers, /*old=*/(1u << 7), /*new=*/0);

    auto drained = triggers.drain();
    REQUIRE(drained.size() == 2);
    REQUIRE(drained[0] == gpio_signal_id(7, GpioEdge::Change));
    REQUIRE(drained[1] == gpio_signal_id(7, GpioEdge::Falling));
}

TEST_CASE("evaluate_gpio_triggers fires nothing for a pin whose bit is unchanged", "[gpio][REQ-GPIO-005]") {
    rcp::endpoint::TriggerRegistry triggers;
    triggers.enable(gpio_signal_id(0, GpioEdge::Change));

    evaluate_gpio_triggers(triggers, /*old=*/0x1, /*new=*/0x1);
    REQUIRE_FALSE(triggers.has_pending());
}

TEST_CASE("evaluate_gpio_triggers only reports signals that were actually armed", "[gpio][REQ-GPIO-005]") {
    rcp::endpoint::TriggerRegistry triggers; // nothing enabled
    auto fired = evaluate_gpio_triggers(triggers, /*old=*/0, /*new=*/1);
    REQUIRE(fired.empty());
}

// ── Functional config block wiring ────────────────────────────────────────────

TEST_CASE("encode/decode_gpio_functional_config round-trips through the opaque regmap blob",
          "[gpio][REQ-GPIO-006]") {
    std::array<uint8_t, kMaxPins> edges{};
    edges[0] = 0b001; // Change
    edges[5] = 0b110; // Rising + Falling

    auto cfg = encode_gpio_functional_config(0xF0F0F0F0, edges);
    REQUIRE(cfg.data.size() == kGpioFunctionalConfigLen);

    PinMask out_directions = 0;
    std::array<uint8_t, kMaxPins> out_edges{};
    auto ec = decode_gpio_functional_config(cfg, out_directions, out_edges);
    REQUIRE_FALSE(ec);
    REQUIRE(out_directions == 0xF0F0F0F0);
    REQUIRE(out_edges == edges);
}

TEST_CASE("decode_gpio_functional_config rejects an undersized blob", "[gpio][REQ-GPIO-006]") {
    rcp::regmap::EndpointFunctionalConfig cfg;
    cfg.data = {0x01, 0x02}; // far short of kGpioFunctionalConfigLen

    PinMask out_directions = 0;
    std::array<uint8_t, kMaxPins> out_edges{};
    auto ec = decode_gpio_functional_config(cfg, out_directions, out_edges);
    REQUIRE(ec == rcp::avtp::make_error_code(rcp::avtp::AvtpErrc::short_buffer));
}

// ── GpioEndpoint request-dispatch pattern ────────────────────────────────────

TEST_CASE("GpioEndpoint::handle_write updates state and fires triggers in one call",
          "[gpio][REQ-GPIO-007]") {
    GpioEndpoint ep;
    ep.triggers().enable(gpio_signal_id(0, GpioEdge::Change));
    ep.triggers().enable(gpio_signal_id(0, GpioEdge::Rising));

    PinMask out_value = 0;
    auto ec = ep.handle_write(WriteSemantics::Or, /*operand=*/0x1, out_value);
    REQUIRE_FALSE(ec);
    REQUIRE(out_value == 0x1);
    REQUIRE(ep.read() == 0x1);

    auto fired = ep.triggers().drain();
    REQUIRE(fired.size() == 2);
}

TEST_CASE("GpioEndpoint::handle_write propagates a Reserved-semantics error without changing state",
          "[gpio][REQ-GPIO-008]") {
    GpioEndpoint ep;
    PinMask out_value = 0;
    auto ec = ep.handle_write(WriteSemantics::Reserved, 0xFF, out_value);
    REQUIRE(ec);
    REQUIRE(ep.read() == 0);
}

// ── GpioErrc category sanity ──────────────────────────────────────────────────

TEST_CASE("GpioErrc reports a non-empty message in its own category", "[gpio][REQ-GPIO-008]") {
    auto ec = make_error_code(GpioErrc::pin_index_out_of_range);
    REQUIRE(ec.category() == gpio_category());
    REQUIRE_FALSE(ec.message().empty());
}
