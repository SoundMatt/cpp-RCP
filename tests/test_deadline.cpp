// fusa:test REQ-DL-001
// fusa:test REQ-DL-002
// fusa:test REQ-DL-003
// fusa:test REQ-DL-004
// fusa:test REQ-DL-005
// fusa:test REQ-DL-006
// fusa:test REQ-DL-007
// fusa:test REQ-DL-008

// Tests for rcp/deadline.hpp — the RC Server liveness monitor rebuilt
// around the response/ack queue flush heartbeat and the EP0
// lifecycle-state-changed trigger (ROADMAP.md milestone 54, "Watchdog &
// Liveness Rebuild", v2.10.0).

#include <catch2/catch_test_macros.hpp>

#include "rcp/deadline.hpp"

#include <vector>

using namespace rcp::deadline;

// ── LivenessTracker ────────────────────────────────────────────────────────────

TEST_CASE("LivenessTracker is dead until note_activity is ever called", "[deadline][REQ-DL-001]") {
    LivenessTracker tr;
    Config cfg = default_config();
    REQUIRE(tr.dead(cfg, 0));
    REQUIRE(tr.dead(cfg, 1'000'000)); // regardless of how much simulated time has passed
    REQUIRE_FALSE(tr.has_ever_reported());
}

// ── Heartbeat and lifecycle-change are independently sufficient ─────────────

TEST_CASE("Both note_heartbeat and note_lifecycle_change independently keep a target alive",
          "[deadline][REQ-DL-002]") {
    Config cfg;
    cfg.deadline = std::chrono::milliseconds(50);

    Monitor mon_hb;
    mon_hb.register_target(1);
    mon_hb.note_heartbeat(1, 1'000);
    mon_hb.check(cfg, 1'020);
    REQUIRE(mon_hb.alive(1));

    Monitor mon_lc;
    mon_lc.register_target(1);
    mon_lc.note_lifecycle_change(1, 1'000);
    mon_lc.check(cfg, 1'020);
    REQUIRE(mon_lc.alive(1));
}

// ── Dead event on first check of a never-reported target ─────────────────────

TEST_CASE("check() emits a dead event on the first evaluation of a never-reported target",
          "[deadline][REQ-DL-003]") {
    Config cfg;
    Monitor mon;
    mon.register_target(1);

    std::vector<LivenessEvent> events;
    mon.subscribe([&](const LivenessEvent& ev) { events.push_back(ev); });

    mon.check(cfg, 0);
    REQUIRE(events.size() == 1);
    REQUIRE(events[0].target == 1);
    REQUIRE_FALSE(events[0].alive);
}

// ── Repeated dead events suppressed ───────────────────────────────────────────

TEST_CASE("check() does not re-emit a dead event for a target already known dead",
          "[deadline][REQ-DL-004]") {
    Config cfg;
    Monitor mon;
    mon.register_target(1);

    std::vector<LivenessEvent> events;
    mon.subscribe([&](const LivenessEvent& ev) { events.push_back(ev); });

    mon.check(cfg, 0);
    mon.check(cfg, 100);
    mon.check(cfg, 200);
    REQUIRE(events.size() == 1); // only the first dead report
}

// ── Alive event on recovery ────────────────────────────────────────────────────

TEST_CASE("check() emits an alive event once a heartbeat arrives for a previously-dead target",
          "[deadline][REQ-DL-005]") {
    Config cfg;
    cfg.deadline = std::chrono::milliseconds(50);

    Monitor mon;
    mon.register_target(1);

    std::vector<LivenessEvent> events;
    mon.subscribe([&](const LivenessEvent& ev) { events.push_back(ev); });

    mon.check(cfg, 0); // dead -- never reported
    REQUIRE_FALSE(mon.alive(1));

    mon.note_heartbeat(1, 10);
    mon.check(cfg, 30); // within the 50ms deadline of the heartbeat at t=10
    REQUIRE(mon.alive(1));

    REQUIRE(events.size() == 2);
    REQUIRE_FALSE(events[0].alive);
    REQUIRE(events[1].alive);
}

// ── alive() before any check ──────────────────────────────────────────────────

TEST_CASE("alive() returns false before check() has ever evaluated the target",
          "[deadline][REQ-DL-006]") {
    Monitor mon;
    mon.register_target(1);
    mon.note_heartbeat(1, 0); // reported activity, but never yet checked

    REQUIRE_FALSE(mon.alive(1));
    REQUIRE_FALSE(mon.alive(99)); // never even registered
}

// ── Subscribed callbacks ──────────────────────────────────────────────────────

TEST_CASE("check() invokes every subscribed callback in registration order",
          "[deadline][REQ-DL-007]") {
    Config cfg;
    Monitor mon;
    mon.register_target(1);

    std::vector<int> order;
    mon.subscribe([&](const LivenessEvent&) { order.push_back(1); });
    mon.subscribe([&](const LivenessEvent&) { order.push_back(2); });

    mon.check(cfg, 0);
    REQUIRE(order == std::vector<int>{1, 2});
}

// ── Independence across targets ───────────────────────────────────────────────

TEST_CASE("check() evaluates each registered target independently", "[deadline][REQ-DL-008]") {
    Config cfg;
    cfg.deadline = std::chrono::milliseconds(50);

    Monitor mon;
    mon.register_target(1);
    mon.register_target(2);

    mon.note_heartbeat(1, 0); // target 1 stays alive; target 2 never reports
    mon.check(cfg, 20);

    REQUIRE(mon.alive(1));
    REQUIRE_FALSE(mon.alive(2));

    mon.unregister_target(1);
    REQUIRE_FALSE(mon.is_registered(1));
    REQUIRE(mon.is_registered(2));
}
