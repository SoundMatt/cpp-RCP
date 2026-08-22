// fusa:test REQ-PWR-001
// fusa:test REQ-PWR-002
// fusa:test REQ-PWR-003
// fusa:test REQ-PWR-004
// fusa:test REQ-PWR-005
// fusa:test REQ-PWR-006
// fusa:test REQ-PWR-007
// fusa:test REQ-PWR-008
// fusa:test REQ-PWR-009
// fusa:test REQ-PWR-010
// fusa:test REQ-PWR-011
// fusa:test REQ-PWR-012
// fusa:test REQ-PWR-013
// fusa:test REQ-PWR-014
// fusa:test REQ-PWRMODE-001
// fusa:test REQ-PWRMODE-002
// fusa:test REQ-PWRMODE-003
// fusa:test REQ-PWRMODE-005
// fusa:test REQ-PWRMODE-006
// fusa:test REQ-PWRMODE-007
// fusa:test REQ-PWRMODE-008
// fusa:test REQ-PWRMODE-009
// fusa:test REQ-PWRMODE-010
// fusa:test REQ-PWRMODE-011
// fusa:test REQ-PWRMODE-013
// fusa:test REQ-PWRMODE-014
// fusa:test REQ-PWRMODE-015
// fusa:test REQ-PWRMODE-016
// fusa:test REQ-PWRMODE-018
// fusa:test REQ-PWRMODE-020
// fusa:test REQ-PWRMODE-024
// fusa:test REQ-PWRMODE-025

// Tests for rcp/powerstate.hpp — the TC18 power-mode model, entry-refusal
// conditions, and hot-start-from-Sleep handshake (ROADMAP.md milestone 53,
// "Power Management Rebuild", v2.9.0, Phase 14).

#include <catch2/catch_test_macros.hpp>

#include "rcp/powerstate.hpp"
#include "rcp/wakeup.hpp"

using namespace rcp::powerstate;

// ── PowerMode / initial state ────────────────────────────────────────────────

TEST_CASE("A freshly constructed PowerManager starts in Normal", "[powerstate][REQ-PWR-001]") {
    rcp::wakeup::WakeupEndpoint wep;
    PowerManager mgr(wep);
    REQUIRE(mgr.mode() == PowerMode::Normal);
    REQUIRE(to_string(PowerMode::Normal) == "normal");
    REQUIRE(to_string(PowerMode::StandBy) == "standby");
    REQUIRE(to_string(PowerMode::Sleep) == "sleep");
    REQUIRE(to_string(PowerMode::Unpowered) == "unpowered");
}

// ── enter_standby / enter_sleep: only defined from Normal ───────────────────

TEST_CASE("enter_standby and enter_sleep succeed from Normal", "[powerstate][REQ-PWR-002]") {
    rcp::wakeup::WakeupEndpoint wep;
    PowerManager mgr(wep);
    REQUIRE_FALSE(mgr.enter_standby());
    REQUIRE(mgr.mode() == PowerMode::StandBy);

    rcp::wakeup::WakeupEndpoint wep2;
    PowerManager mgr2(wep2);
    REQUIRE_FALSE(mgr2.enter_sleep());
    REQUIRE(mgr2.mode() == PowerMode::Sleep);
}

TEST_CASE("enter_standby/enter_sleep are refused outside of Normal", "[powerstate][REQ-PWR-002]") {
    rcp::wakeup::WakeupEndpoint wep;
    PowerManager mgr(wep);
    REQUIRE_FALSE(mgr.enter_standby());
    REQUIRE(mgr.mode() == PowerMode::StandBy);

    auto ec = mgr.enter_sleep();
    REQUIRE(ec == make_error_code(PowerErrc::invalid_transition));
    REQUIRE(mgr.mode() == PowerMode::StandBy); // unchanged

    ec = mgr.enter_standby();
    REQUIRE(ec == make_error_code(PowerErrc::invalid_transition));
}

// ── Entry refusal: unacknowledged wake-up event ──────────────────────────────

TEST_CASE("Entry into StandBy/Sleep is refused while a wake-up event is unacknowledged",
          "[powerstate][REQ-PWR-003]") {
    rcp::wakeup::WakeupEndpoint wep;
    wep.record_wake_source_event(2); // arms wakeup_message_pending()
    REQUIRE(wep.wakeup_message_pending());

    PowerManager mgr(wep);
    auto ec = mgr.enter_sleep();
    REQUIRE(ec == make_error_code(PowerErrc::unacknowledged_wakeup_event));
    REQUIRE(mgr.mode() == PowerMode::Normal);

    // Once acknowledged, entry succeeds.
    wep.acknowledge_wakeup();
    REQUIRE_FALSE(mgr.enter_sleep());
    REQUIRE(mgr.mode() == PowerMode::Sleep);
}

// ── Entry refusal: non-idle endpoint ─────────────────────────────────────────

TEST_CASE("Entry into StandBy/Sleep is refused while the endpoints_idle hook reports false",
          "[powerstate][REQ-PWR-004]") {
    rcp::wakeup::WakeupEndpoint wep;
    bool idle = false;
    PowerManager::Hooks hooks;
    hooks.endpoints_idle = [&] { return idle; };
    PowerManager mgr(wep, hooks);

    auto ec = mgr.enter_standby();
    REQUIRE(ec == make_error_code(PowerErrc::endpoint_not_idle));
    REQUIRE(mgr.mode() == PowerMode::Normal);

    idle = true;
    REQUIRE_FALSE(mgr.enter_standby());
    REQUIRE(mgr.mode() == PowerMode::StandBy);
}

// ── Entry refusal: non-empty response/ack queue ──────────────────────────────

TEST_CASE("Entry into StandBy/Sleep is refused while the response_ack_queues_empty hook reports false",
          "[powerstate][REQ-PWR-005]") {
    rcp::wakeup::WakeupEndpoint wep;
    bool empty = false;
    PowerManager::Hooks hooks;
    hooks.response_ack_queues_empty = [&] { return empty; };
    PowerManager mgr(wep, hooks);

    auto ec = mgr.enter_sleep();
    REQUIRE(ec == make_error_code(PowerErrc::response_ack_queue_not_empty));
    REQUIRE(mgr.mode() == PowerMode::Normal);

    empty = true;
    REQUIRE_FALSE(mgr.enter_sleep());
    REQUIRE(mgr.mode() == PowerMode::Sleep);
}

// ── StandBy: always a hot start, no handshake ────────────────────────────────

TEST_CASE("resume_from_standby returns directly to Normal with no handshake",
          "[powerstate][REQ-PWR-006]") {
    rcp::wakeup::WakeupEndpoint wep;
    bool network_reenabled = false;
    PowerManager::Hooks hooks;
    hooks.reenable_network_interface = [&] { network_reenabled = true; };
    PowerManager mgr(wep, hooks);

    REQUIRE_FALSE(mgr.enter_standby());
    REQUIRE(mgr.pending_start_kind() == StartKind::Hot);

    REQUIRE_FALSE(mgr.resume_from_standby());
    REQUIRE(mgr.mode() == PowerMode::Normal);
    REQUIRE(mgr.wake_stage() == WakeStage::Idle);
    REQUIRE_FALSE(network_reenabled); // hot start from StandBy never touches the handshake hooks
}

TEST_CASE("resume_from_standby is refused outside of StandBy", "[powerstate][REQ-PWR-006]") {
    rcp::wakeup::WakeupEndpoint wep;
    PowerManager mgr(wep);
    auto ec = mgr.resume_from_standby();
    REQUIRE(ec == make_error_code(PowerErrc::invalid_transition));
}

// ── Sleep: hot-start-from-Sleep handshake (cold start overall) ──────────────

TEST_CASE("begin_wake_from_sleep requires Sleep and runs network re-enablement",
          "[powerstate][REQ-PWR-007]") {
    rcp::wakeup::WakeupEndpoint wep;
    int reenable_calls = 0;
    PowerManager::Hooks hooks;
    hooks.reenable_network_interface = [&] { ++reenable_calls; };
    PowerManager mgr(wep, hooks);

    REQUIRE_FALSE(mgr.enter_sleep());
    REQUIRE(mgr.pending_start_kind() == StartKind::Cold);

    REQUIRE_FALSE(mgr.begin_wake_from_sleep());
    REQUIRE(reenable_calls == 1);
    REQUIRE(mgr.wake_stage() == WakeStage::HandshakeActive);
    REQUIRE(mgr.mode() == PowerMode::Sleep); // still Sleep until acknowledged
}

TEST_CASE("Handshake steps are refused when not applicable", "[powerstate][REQ-PWR-008]") {
    rcp::wakeup::WakeupEndpoint wep;
    PowerManager mgr(wep);

    // Not asleep at all.
    REQUIRE(mgr.begin_wake_from_sleep() == make_error_code(PowerErrc::not_asleep));
    REQUIRE(mgr.note_wakeup_attempt_sent() == make_error_code(PowerErrc::not_asleep));
    REQUIRE(mgr.acknowledge_wakeup() == make_error_code(PowerErrc::not_asleep));

    // Asleep, but handshake not yet begun.
    REQUIRE_FALSE(mgr.enter_sleep());
    REQUIRE(mgr.note_wakeup_attempt_sent() == make_error_code(PowerErrc::not_asleep));
    REQUIRE(mgr.acknowledge_wakeup() == make_error_code(PowerErrc::not_asleep));
}

TEST_CASE("note_wakeup_attempt_sent enforces the repeat limit", "[powerstate][REQ-PWR-009]") {
    rcp::wakeup::WakeupEndpoint wep;

    Config cfg;
    cfg.wakeup_repeat_limit = 3;
    PowerManager mgr(wep, {}, cfg);

    REQUIRE_FALSE(mgr.enter_sleep());
    wep.record_wake_source_event(0); // a wake-source pin fires while asleep, arming the handshake
    REQUIRE_FALSE(mgr.begin_wake_from_sleep());

    // Never acknowledged: wep keeps reporting wakeup_message_pending(), so
    // every attempt counts against the limit.
    REQUIRE_FALSE(mgr.note_wakeup_attempt_sent()); // 1
    REQUIRE_FALSE(mgr.note_wakeup_attempt_sent()); // 2
    REQUIRE_FALSE(mgr.note_wakeup_attempt_sent()); // 3
    auto ec = mgr.note_wakeup_attempt_sent();       // 4 -> exceeds limit
    REQUIRE(ec == make_error_code(PowerErrc::handshake_repeat_limit_exceeded));
    REQUIRE(mgr.wake_stage() == WakeStage::Failed);
    REQUIRE(mgr.mode() == PowerMode::Sleep); // still Sleep — no silent recovery
}

TEST_CASE("note_wakeup_attempt_sent is a no-op once the handshake is already echoed",
          "[powerstate][REQ-PWR-009]") {
    rcp::wakeup::WakeupEndpoint wep;
    PowerManager mgr(wep);
    REQUIRE_FALSE(mgr.enter_sleep());
    wep.record_wake_source_event(0);
    REQUIRE_FALSE(mgr.begin_wake_from_sleep());

    wep.acknowledge_wakeup(); // echoed out-of-band (e.g. by a concurrent driver call)
    REQUIRE_FALSE(mgr.note_wakeup_attempt_sent());
    REQUIRE(mgr.wake_stage() == WakeStage::HandshakeActive); // acknowledge_wakeup() itself completes the stage
}

TEST_CASE("acknowledge_wakeup completes the handshake, re-enables queues, and returns to Normal",
          "[powerstate][REQ-PWR-010]") {
    rcp::wakeup::WakeupEndpoint wep;

    int reenable_network = 0;
    int reenable_queues  = 0;
    PowerManager::Hooks hooks;
    hooks.reenable_network_interface   = [&] { ++reenable_network; };
    hooks.reenable_response_ack_queues = [&] { ++reenable_queues; };
    PowerManager mgr(wep, hooks);

    REQUIRE_FALSE(mgr.enter_sleep());
    wep.record_wake_source_event(0);
    REQUIRE_FALSE(mgr.begin_wake_from_sleep());
    REQUIRE(reenable_network == 1);
    REQUIRE(reenable_queues == 0); // not yet — only after acknowledge

    REQUIRE_FALSE(mgr.note_wakeup_attempt_sent());
    REQUIRE_FALSE(mgr.acknowledge_wakeup());

    REQUIRE(reenable_queues == 1);
    REQUIRE_FALSE(wep.wakeup_message_pending());
    REQUIRE(mgr.wake_stage() == WakeStage::Complete);
    REQUIRE(mgr.mode() == PowerMode::Normal);
}

// ── Unpowered ─────────────────────────────────────────────────────────────────

TEST_CASE("notify_power_removed forces Unpowered unconditionally", "[powerstate][REQ-PWR-011]") {
    rcp::wakeup::WakeupEndpoint wep;
    wep.record_wake_source_event(0); // would normally block entry into a low-power mode

    PowerManager mgr(wep);
    mgr.notify_power_removed();
    REQUIRE(mgr.mode() == PowerMode::Unpowered); // no refusal path for power loss
}

TEST_CASE("notify_power_restored returns to Normal from Unpowered", "[powerstate][REQ-PWR-012]") {
    rcp::wakeup::WakeupEndpoint wep;
    PowerManager mgr(wep);
    mgr.notify_power_removed();
    REQUIRE(mgr.mode() == PowerMode::Unpowered);

    mgr.notify_power_restored();
    REQUIRE(mgr.mode() == PowerMode::Normal);
    REQUIRE(mgr.wake_stage() == WakeStage::Idle);
}

// ── StartKind ─────────────────────────────────────────────────────────────────

TEST_CASE("start_kind_on_exit reports Hot for StandBy and Cold for Sleep", "[powerstate][REQ-PWR-013]") {
    REQUIRE(start_kind_on_exit(PowerMode::StandBy) == StartKind::Hot);
    REQUIRE(start_kind_on_exit(PowerMode::Sleep) == StartKind::Cold);
    REQUIRE(to_string(StartKind::Hot) == "hot");
    REQUIRE(to_string(StartKind::Cold) == "cold");
}

// ── PowerErrc category sanity ────────────────────────────────────────────────

TEST_CASE("PowerErrc reports a non-empty message in its own category", "[powerstate][REQ-PWR-014]") {
    auto ec = make_error_code(PowerErrc::invalid_transition);
    REQUIRE(ec.category() == power_category());
    REQUIRE_FALSE(ec.message().empty());
}

// ── cold_start_lifecycle_target (REQ-PWRMODE-003/014) ───────────────────────

TEST_CASE("cold_start_lifecycle_target returns a valid recovered_state unchanged",
          "[powerstate][REQ-PWRMODE-003][REQ-PWRMODE-014]") {
    REQUIRE(cold_start_lifecycle_target(rcp::lifecycle::ServerState::HwUnconfigured) ==
            rcp::lifecycle::ServerState::HwUnconfigured);
    REQUIRE(cold_start_lifecycle_target(rcp::lifecycle::ServerState::HwConfigured) ==
            rcp::lifecycle::ServerState::HwConfigured);
    REQUIRE(cold_start_lifecycle_target(rcp::lifecycle::ServerState::RcpConfigured) ==
            rcp::lifecycle::ServerState::RcpConfigured);
}

TEST_CASE("cold_start_lifecycle_target falls back to HwUnconfigured for an unrecognized value",
          "[powerstate][REQ-PWRMODE-003][REQ-PWRMODE-014]") {
    auto bogus = static_cast<rcp::lifecycle::ServerState>(0xFF);
    REQUIRE(cold_start_lifecycle_target(bogus) == rcp::lifecycle::ServerState::HwUnconfigured);
}

// ── begin_wake_from_sleep's network_available gate (REQ-PWRMODE-016) ────────

TEST_CASE("begin_wake_from_sleep defaults to network_available=true, preserving every "
          "pre-existing caller's exact behavior",
          "[powerstate][REQ-PWRMODE-016]") {
    rcp::wakeup::WakeupEndpoint wep;
    int reenable_calls = 0;
    PowerManager::Hooks hooks;
    hooks.reenable_network_interface = [&] { ++reenable_calls; };
    PowerManager mgr(wep, hooks);

    REQUIRE_FALSE(mgr.enter_sleep());
    REQUIRE_FALSE(mgr.begin_wake_from_sleep()); // no explicit argument — defaults to true
    REQUIRE(reenable_calls == 1);
    REQUIRE(mgr.wake_stage() == WakeStage::HandshakeActive);
}

TEST_CASE("begin_wake_from_sleep(false) is a free, uncounted retry that leaves wake_stage() at "
          "Idle and never touches the network-reenable hook",
          "[powerstate][REQ-PWRMODE-016]") {
    rcp::wakeup::WakeupEndpoint wep;
    int reenable_calls = 0;
    PowerManager::Hooks hooks;
    hooks.reenable_network_interface = [&] { ++reenable_calls; };
    PowerManager mgr(wep, hooks);

    REQUIRE_FALSE(mgr.enter_sleep());

    auto ec = mgr.begin_wake_from_sleep(/*network_available=*/false);
    REQUIRE(ec == make_error_code(PowerErrc::network_not_available));
    REQUIRE(mgr.wake_stage() == WakeStage::Idle); // still Idle, not HandshakeActive
    REQUIRE(reenable_calls == 0);                 // network hook never fired
    REQUIRE(mgr.wake_attempts() == 0);             // not counted against the repeat limit

    // A later retry once the network comes up succeeds normally.
    REQUIRE_FALSE(mgr.begin_wake_from_sleep(/*network_available=*/true));
    REQUIRE(reenable_calls == 1);
    REQUIRE(mgr.wake_stage() == WakeStage::HandshakeActive);
}
