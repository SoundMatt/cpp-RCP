// fusa:test REQ-WAKEUP-001
// fusa:test REQ-WAKEUP-002
// fusa:test REQ-WAKEUP-003
// fusa:test REQ-WAKEUP-004
// fusa:test REQ-WAKEUP-005

// Tests for rcp/wakeup.hpp — the Wakeup control endpoint type (ROADMAP.md
// milestone 51, "Remaining Endpoint Types — LIN, CAN (incl. CAN XL),
// ISELED, MDIO, Wakeup Control", v2.7.0).

#include <catch2/catch_test_macros.hpp>
#include <rcp/endpoint.hpp>
#include <rcp/wakeup.hpp>

using namespace rcp::wakeup;

// ── ep_type id ────────────────────────────────────────────────────────────────

TEST_CASE("Wakeup control's ep_type id is 0x01", "[wakeup][REQ-WAKEUP-001]") {
    REQUIRE(rcp::endpoint::kEndpointTypeWakeup == 0x01);
}

// ── SleepCMD: a fixed opcode, not a RequestTypeOpcode ────────────────────────

TEST_CASE("decode_sleep_cmd accepts only the fixed 0xA5 byte", "[wakeup][REQ-WAKEUP-002]") {
    REQUIRE_FALSE(decode_sleep_cmd(kSleepCmd));
    REQUIRE(kSleepCmd == 0xA5);
}

TEST_CASE("decode_sleep_cmd rejects every other byte", "[wakeup][REQ-WAKEUP-002]") {
    REQUIRE(decode_sleep_cmd(0x00) == make_error_code(WakeupErrc::not_sleep_cmd));
    REQUIRE(decode_sleep_cmd(0x8F) == make_error_code(WakeupErrc::not_sleep_cmd)); // a sequencer opcode byte,
                                                                                     // deliberately not accepted here
}

TEST_CASE("WakeupEndpoint::handle_sleep_cmd transitions to asleep on the fixed opcode",
          "[wakeup][REQ-WAKEUP-002]") {
    WakeupEndpoint ep;
    REQUIRE_FALSE(ep.is_asleep());
    REQUIRE_FALSE(ep.handle_sleep_cmd(kSleepCmd));
    REQUIRE(ep.is_asleep());
}

TEST_CASE("WakeupEndpoint::handle_sleep_cmd rejects a non-SleepCMD byte without changing state",
          "[wakeup][REQ-WAKEUP-002]") {
    WakeupEndpoint ep;
    auto ec = ep.handle_sleep_cmd(0x5A);
    REQUIRE(ec == make_error_code(WakeupErrc::not_sleep_cmd));
    REQUIRE_FALSE(ep.is_asleep());
}

// ── Wake-source pin monitoring ────────────────────────────────────────────────

TEST_CASE("WakeupEndpoint::record_wake_source_event sets the pin's bit and wakes the endpoint",
          "[wakeup][REQ-WAKEUP-003]") {
    WakeupEndpoint ep;
    REQUIRE_FALSE(ep.handle_sleep_cmd(kSleepCmd));
    REQUIRE(ep.is_asleep());

    ep.record_wake_source_event(3);
    REQUIRE_FALSE(ep.is_asleep());
    REQUIRE(ep.wake_source_pins() == (WakeSourceMask{1} << 3));
}

TEST_CASE("WakeupEndpoint::clear_wake_source_pins resets the accumulated mask", "[wakeup][REQ-WAKEUP-003]") {
    WakeupEndpoint ep;
    ep.record_wake_source_event(0);
    ep.record_wake_source_event(1);
    REQUIRE(ep.wake_source_pins() == 0b11);

    ep.clear_wake_source_pins();
    REQUIRE(ep.wake_source_pins() == 0);
}

// ── Repeating WakeUp message handshake (hot-start-from-Sleep) ────────────────

TEST_CASE("A wake-source event arms the repeating WakeUp handshake until acknowledged",
          "[wakeup][REQ-WAKEUP-004]") {
    WakeupEndpoint ep;
    REQUIRE_FALSE(ep.wakeup_message_pending());

    ep.record_wake_source_event(5);
    REQUIRE(ep.wakeup_message_pending());

    // Repeated calls keep reporting "still owed a repetition" until acked.
    REQUIRE(ep.wakeup_message_pending());
    REQUIRE(ep.wakeup_message_pending());

    ep.acknowledge_wakeup();
    REQUIRE_FALSE(ep.wakeup_message_pending());
}

TEST_CASE("Entering Sleep clears any handshake left pending from a prior wake cycle",
          "[wakeup][REQ-WAKEUP-004]") {
    WakeupEndpoint ep;
    ep.record_wake_source_event(2);
    REQUIRE(ep.wakeup_message_pending());

    REQUIRE_FALSE(ep.handle_sleep_cmd(kSleepCmd));
    REQUIRE_FALSE(ep.wakeup_message_pending());
}

// ── WakeupErrc category sanity ────────────────────────────────────────────────

TEST_CASE("WakeupErrc reports a non-empty message in its own category", "[wakeup][REQ-WAKEUP-005]") {
    auto ec = make_error_code(WakeupErrc::not_sleep_cmd);
    REQUIRE(ec.category() == wakeup_category());
    REQUIRE_FALSE(ec.message().empty());
}
