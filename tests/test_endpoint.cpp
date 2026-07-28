// fusa:test REQ-ENDPOINT-001
// fusa:test REQ-ENDPOINT-002
// fusa:test REQ-ENDPOINT-003
// fusa:test REQ-ENDPOINT-004
// fusa:test REQ-ENDPOINT-005
// fusa:test REQ-ENDPOINT-006

// Tests for rcp/endpoint.hpp — the shared endpoint-registration and
// request-dispatch scaffolding (ROADMAP.md milestone 47, "Basic Endpoint
// Types I — GPIO & SPI", v2.3.0).

#include <catch2/catch_test_macros.hpp>
#include <rcp/endpoint.hpp>

#include <limits>

using namespace rcp::endpoint;

// ── ep_type ids ───────────────────────────────────────────────────────────────

TEST_CASE("GPIO and SPI ep_type ids match extraction §5.3/§5.4", "[endpoint][REQ-ENDPOINT-001]") {
    REQUIRE(kEndpointTypeGpio == 0x02);
    REQUIRE(kEndpointTypeSpi == 0x03);
}

// ── evt[2:0] write-semantics decode ──────────────────────────────────────────

TEST_CASE("write_semantics_of decodes all 8 evt[2:0] values", "[endpoint][REQ-ENDPOINT-002]") {
    REQUIRE(write_semantics_of(0) == WriteSemantics::Replace);
    REQUIRE(write_semantics_of(1) == WriteSemantics::Or);
    REQUIRE(write_semantics_of(2) == WriteSemantics::And);
    REQUIRE(write_semantics_of(3) == WriteSemantics::Xor);
    REQUIRE(write_semantics_of(4) == WriteSemantics::Reserved);
    REQUIRE(write_semantics_of(5) == WriteSemantics::Add);
    REQUIRE(write_semantics_of(6) == WriteSemantics::Subtract);
    REQUIRE(write_semantics_of(7) == WriteSemantics::Reconfigure);
}

TEST_CASE("write_semantics_of masks its input down to 3 bits", "[endpoint][REQ-ENDPOINT-002]") {
    REQUIRE(write_semantics_of(0xF8) == WriteSemantics::Replace); // low 3 bits are 000
    REQUIRE(write_semantics_of(0xF9) == WriteSemantics::Or);      // low 3 bits are 001
}

// ── Generic bitmask/value write combinator ───────────────────────────────────

TEST_CASE("apply_bitmask_write implements Replace/Or/And/Xor", "[endpoint][REQ-ENDPOINT-003]") {
    uint32_t out = 0;

    REQUIRE_FALSE(apply_bitmask_write(WriteSemantics::Replace, 0xAAAAAAAA, 0x11111111, out));
    REQUIRE(out == 0x11111111);

    REQUIRE_FALSE(apply_bitmask_write(WriteSemantics::Or, 0x0F0F0F0F, 0xF0F0F0F0, out));
    REQUIRE(out == 0xFFFFFFFF);

    REQUIRE_FALSE(apply_bitmask_write(WriteSemantics::And, 0xFFFF0000, 0x00FFFF00, out));
    REQUIRE(out == 0x00FF0000);

    REQUIRE_FALSE(apply_bitmask_write(WriteSemantics::Xor, 0xFFFFFFFF, 0x0000FFFF, out));
    REQUIRE(out == 0xFFFF0000);
}

TEST_CASE("apply_bitmask_write implements saturating Add/Subtract", "[endpoint][REQ-ENDPOINT-004]") {
    uint32_t out = 0;

    REQUIRE_FALSE(apply_bitmask_write(WriteSemantics::Add, 10, 5, out));
    REQUIRE(out == 15);

    REQUIRE_FALSE(apply_bitmask_write(WriteSemantics::Add, std::numeric_limits<uint32_t>::max(), 100, out));
    REQUIRE(out == std::numeric_limits<uint32_t>::max()); // saturates, does not wrap

    REQUIRE_FALSE(apply_bitmask_write(WriteSemantics::Subtract, 10, 4, out));
    REQUIRE(out == 6);

    REQUIRE_FALSE(apply_bitmask_write(WriteSemantics::Subtract, 4, 10, out));
    REQUIRE(out == 0); // saturates at 0, does not wrap
}

TEST_CASE("apply_bitmask_write rejects Reserved", "[endpoint][REQ-ENDPOINT-005]") {
    uint32_t out = 0;
    auto ec = apply_bitmask_write(WriteSemantics::Reserved, 0, 0, out);
    REQUIRE(ec == make_error_code(EndpointErrc::reserved_write_semantics));
}

TEST_CASE("apply_bitmask_write refuses to combine Reconfigure generically", "[endpoint][REQ-ENDPOINT-005]") {
    uint32_t out = 0;
    auto ec = apply_bitmask_write(WriteSemantics::Reconfigure, 0, 0, out);
    REQUIRE(ec == make_error_code(EndpointErrc::non_combinable_write_semantics));
}

// ── Saturating arithmetic helpers, directly ──────────────────────────────────

TEST_CASE("saturating_add/saturating_subtract clamp instead of wrapping", "[endpoint][REQ-ENDPOINT-004]") {
    REQUIRE(saturating_add<uint8_t>(250, 10) == 255);
    REQUIRE(saturating_add<uint8_t>(10, 20) == 30);
    REQUIRE(saturating_subtract<uint8_t>(5, 10) == 0);
    REQUIRE(saturating_subtract<uint8_t>(10, 5) == 5);
}

// ── Generic trigger-signal table ──────────────────────────────────────────────

TEST_CASE("TriggerRegistry only queues notify() for enabled signals", "[endpoint][REQ-ENDPOINT-006]") {
    TriggerRegistry reg;
    REQUIRE_FALSE(reg.notify(1)); // not enabled yet
    REQUIRE_FALSE(reg.has_pending());

    reg.enable(1);
    REQUIRE(reg.is_enabled(1));
    REQUIRE(reg.notify(1));
    REQUIRE(reg.has_pending());

    auto drained = reg.drain();
    REQUIRE(drained == std::vector<TriggerRegistry::SignalId>{1});
    REQUIRE_FALSE(reg.has_pending());
}

TEST_CASE("TriggerRegistry::disable stops further notifications from queuing", "[endpoint][REQ-ENDPOINT-006]") {
    TriggerRegistry reg;
    reg.enable(2);
    REQUIRE(reg.notify(2));
    reg.drain();

    reg.disable(2);
    REQUIRE_FALSE(reg.is_enabled(2));
    REQUIRE_FALSE(reg.notify(2));
    REQUIRE_FALSE(reg.has_pending());
}

TEST_CASE("TriggerRegistry preserves notification order across distinct signal ids",
          "[endpoint][REQ-ENDPOINT-006]") {
    TriggerRegistry reg;
    reg.enable(5);
    reg.enable(9);
    reg.notify(9);
    reg.notify(5);
    reg.notify(9);

    auto drained = reg.drain();
    REQUIRE(drained == std::vector<TriggerRegistry::SignalId>{9, 5, 9});
}

// ── EndpointErrc category sanity ─────────────────────────────────────────────

TEST_CASE("EndpointErrc reports a non-empty message in its own category", "[endpoint][REQ-ENDPOINT-005]") {
    auto ec = make_error_code(EndpointErrc::reserved_write_semantics);
    REQUIRE(ec.category() == endpoint_category());
    REQUIRE_FALSE(ec.message().empty());
}
