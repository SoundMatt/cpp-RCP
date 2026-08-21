// fusa:test REQ-WDG-001
// fusa:test REQ-WDG-002
// fusa:test REQ-WDG-003
// fusa:test REQ-WDG-004
// fusa:test REQ-WDG-005
// fusa:test REQ-WDG-006
// fusa:test REQ-WDG-007
// fusa:test REQ-WDG-008

// Tests for rcp/watchdog.hpp — the per-request-stream watchdog driver
// (ROADMAP.md milestone 54, "Watchdog & Liveness Rebuild", v2.10.0).

#include <catch2/catch_test_macros.hpp>

#include "rcp/watchdog.hpp"

#include <vector>

using namespace rcp::watchdog;
using rcp::regmap::RequestStreamConfig;
using rcp::request::request_record_for;
using rcp::request::RequestLedger;
using rcp::request::RequestState;
using rcp::request::RequestTypeOpcode;

// ── StreamWatchdog::check — overflow detection ───────────────────────────────

TEST_CASE("StreamWatchdog never overflows while disabled or before any kick", "[watchdog][REQ-WDG-001]") {
    RequestStreamConfig cfg;
    cfg.rx_wd_timeout_interval = 100;
    RequestLedger ledger;
    StreamWatchdog wd;

    REQUIRE_FALSE(wd.check(/*stream_key=*/1, cfg, ledger, /*now_ms=*/10'000).has_value());

    cfg.rx_wd_enable = true;
    REQUIRE_FALSE(wd.check(1, cfg, ledger, 10'000).has_value()); // never kicked
}

TEST_CASE("StreamWatchdog::check reports overflow once the timeout interval elapses since the last kick",
          "[watchdog][REQ-WDG-001]") {
    RequestStreamConfig cfg;
    cfg.rx_wd_enable           = true;
    cfg.rx_wd_timeout_interval = 100;
    RequestLedger ledger;
    StreamWatchdog wd;

    wd.kick_from_request(1'000);
    REQUIRE_FALSE(wd.check(1, cfg, ledger, 1'050).has_value());

    auto ev = wd.check(1, cfg, ledger, 1'101);
    REQUIRE(ev.has_value());
    REQUIRE(ev->overflowed);
    REQUIRE(ev->stream_key == 1);
}

// ── kick_from_request resets on any inbound request ──────────────────────────

TEST_CASE("kick_from_request resets the watchdog regardless of what triggers it",
          "[watchdog][REQ-WDG-002]") {
    RequestStreamConfig cfg;
    cfg.rx_wd_enable           = true;
    cfg.rx_wd_timeout_interval = 100;
    RequestLedger ledger;
    StreamWatchdog wd;

    wd.kick_from_request(1'000);
    wd.kick_from_request(1'050); // a second, unrelated inbound request also resets it
    REQUIRE_FALSE(wd.check(1, cfg, ledger, 1'100).has_value());
    REQUIRE(wd.check(1, cfg, ledger, 1'151).has_value());
}

// ── Overflow -> safe-state latch + purge-normal/retain-safety ───────────────

TEST_CASE("Overflow with rx_wd_safestate_enable latches safe state and purges normal requests",
          "[watchdog][REQ-WDG-003]") {
    RequestStreamConfig cfg;
    cfg.rx_wd_enable           = true;
    cfg.rx_wd_timeout_interval = 100;
    cfg.rx_wd_safestate_enable = true;

    RequestLedger ledger;
    REQUIRE_FALSE(ledger.submit(request_record_for(1, RequestTypeOpcode::Compound, /*cs=*/false)));
    REQUIRE_FALSE(ledger.submit(request_record_for(2, RequestTypeOpcode::CompoundSafety, /*cs=*/false)));

    StreamWatchdog wd;
    wd.kick_from_request(0);
    auto ev = wd.check(7, cfg, ledger, 200);

    REQUIRE(ev.has_value());
    REQUIRE(ev->overflowed);
    REQUIRE(ev->entered_safe_state);
    REQUIRE(ev->canceled_request_count == 1);
    REQUIRE(wd.in_safe_state());
    REQUIRE(ledger.find(1)->state == RequestState::Canceled);
    REQUIRE(ledger.find(2)->state == RequestState::Pending);
}

TEST_CASE("Overflow without rx_wd_safestate_enable reports overflowed but does not latch or purge",
          "[watchdog][REQ-WDG-003]") {
    RequestStreamConfig cfg;
    cfg.rx_wd_enable           = true;
    cfg.rx_wd_timeout_interval = 100; // rx_wd_safestate_enable defaults to false

    RequestLedger ledger;
    REQUIRE_FALSE(ledger.submit(request_record_for(1, RequestTypeOpcode::Compound, /*cs=*/false)));

    StreamWatchdog wd;
    wd.kick_from_request(0);
    auto ev = wd.check(1, cfg, ledger, 200);

    REQUIRE(ev.has_value());
    REQUIRE(ev->overflowed);
    REQUIRE_FALSE(ev->entered_safe_state);
    REQUIRE(ev->canceled_request_count == 0);
    REQUIRE_FALSE(wd.in_safe_state());
    REQUIRE(ledger.find(1)->state == RequestState::Pending);
}

// ── Latch persistence / explicit clear ───────────────────────────────────────

TEST_CASE("Once latched, check() does not re-detect overflow until clear_safe_state",
          "[watchdog][REQ-WDG-004]") {
    RequestStreamConfig cfg;
    cfg.rx_wd_enable           = true;
    cfg.rx_wd_timeout_interval = 100;
    cfg.rx_wd_safestate_enable = true;

    RequestLedger ledger;
    StreamWatchdog wd;
    wd.kick_from_request(0);
    REQUIRE(wd.check(1, cfg, ledger, 200)->overflowed);
    REQUIRE(wd.in_safe_state());

    // Still overdue at a later time, but the latch already fired -- no
    // second "overflowed" report (should_emit_info_notification is off
    // since rx_wd_info_enable defaults false, so this is nullopt).
    REQUIRE_FALSE(wd.check(1, cfg, ledger, 500).has_value());

    wd.clear_safe_state();
    REQUIRE_FALSE(wd.in_safe_state());

    // A fresh kick + a fresh overdue interval can latch again.
    wd.kick_from_request(500);
    auto ev = wd.check(1, cfg, ledger, 700);
    REQUIRE(ev.has_value());
    REQUIRE(ev->overflowed);
    REQUIRE(wd.in_safe_state());
}

// ── Repeating info notification ───────────────────────────────────────────────

TEST_CASE("check() reports the repeating info notification while latched with rx_wd_info_enable set",
          "[watchdog][REQ-WDG-005]") {
    RequestStreamConfig cfg;
    cfg.rx_wd_enable           = true;
    cfg.rx_wd_timeout_interval = 100;
    cfg.rx_wd_safestate_enable = true;
    cfg.rx_wd_info_enable      = true;

    RequestLedger ledger;
    StreamWatchdog wd;
    wd.kick_from_request(0);

    auto overflow_ev = wd.check(1, cfg, ledger, 200);
    REQUIRE(overflow_ev.has_value());
    REQUIRE(overflow_ev->info_notification_due); // due immediately on the latching poll too

    // Every subsequent poll while latched repeats the notification.
    auto ev2 = wd.check(1, cfg, ledger, 300);
    REQUIRE(ev2.has_value());
    REQUIRE_FALSE(ev2->overflowed); // already latched -- this is purely the repeat
    REQUIRE(ev2->info_notification_due);

    auto ev3 = wd.check(1, cfg, ledger, 400);
    REQUIRE(ev3.has_value());
    REQUIRE(ev3->info_notification_due);
}

TEST_CASE("check() reports nothing while latched with rx_wd_info_enable clear",
          "[watchdog][REQ-WDG-005]") {
    RequestStreamConfig cfg;
    cfg.rx_wd_enable           = true;
    cfg.rx_wd_timeout_interval = 100;
    cfg.rx_wd_safestate_enable = true; // rx_wd_info_enable defaults false

    RequestLedger ledger;
    StreamWatchdog wd;
    wd.kick_from_request(0);
    REQUIRE(wd.check(1, cfg, ledger, 200)->overflowed);
    REQUIRE_FALSE(wd.check(1, cfg, ledger, 300).has_value());
}

// ── Manager: multi-stream independence ────────────────────────────────────────

TEST_CASE("Manager tracks each registered stream's watchdog state independently",
          "[watchdog][REQ-WDG-006]") {
    Manager mgr;
    mgr.register_stream(1);
    mgr.register_stream(2);

    RequestStreamConfig cfg;
    cfg.rx_wd_enable           = true;
    cfg.rx_wd_timeout_interval = 100;
    cfg.rx_wd_safestate_enable = true;
    RequestLedger ledger1;
    RequestLedger ledger2;

    REQUIRE_FALSE(mgr.on_request_received(1, 0));
    // Stream 2 is never kicked at all.

    REQUIRE_FALSE(mgr.poll(1, cfg, ledger1, 50)); // not yet overdue (kicked at t=0, timeout 100ms)
    REQUIRE_FALSE(mgr.in_safe_state(1));

    REQUIRE_FALSE(mgr.poll(2, cfg, ledger2, 50)); // never kicked, but timeout has not elapsed yet either
    REQUIRE_FALSE(mgr.in_safe_state(2));

    // Stream 1 crosses its deadline (elapsed 150ms > 100ms) and latches;
    // stream 2 -- still never kicked, so still not yet overdue at t=50 in
    // its own clock frame -- is unaffected.
    REQUIRE_FALSE(mgr.poll(1, cfg, ledger1, 150));
    REQUIRE(mgr.in_safe_state(1));
    REQUIRE_FALSE(mgr.in_safe_state(2));
}

// ── Unregistered stream error handling ───────────────────────────────────────

TEST_CASE("Operations on an unregistered stream return stream_not_registered without side effects",
          "[watchdog][REQ-WDG-007]") {
    Manager mgr;
    mgr.register_stream(1);

    RequestStreamConfig cfg;
    RequestLedger ledger;

    REQUIRE(mgr.on_request_received(99, 0) == make_error_code(WatchdogErrc::stream_not_registered));
    REQUIRE(mgr.poll(99, cfg, ledger, 0) == make_error_code(WatchdogErrc::stream_not_registered));
    REQUIRE(mgr.clear_safe_state(99) == make_error_code(WatchdogErrc::stream_not_registered));
    REQUIRE_FALSE(mgr.in_safe_state(99));

    // Stream 1 remains registered and unaffected.
    REQUIRE(mgr.is_registered(1));
    REQUIRE_FALSE(mgr.on_request_received(1, 0));
}

// ── Subscribed callbacks ──────────────────────────────────────────────────────

TEST_CASE("Manager::poll invokes every subscribed callback in registration order",
          "[watchdog][REQ-WDG-008]") {
    Manager mgr;
    mgr.register_stream(1);

    RequestStreamConfig cfg;
    cfg.rx_wd_enable           = true;
    cfg.rx_wd_timeout_interval = 100;
    RequestLedger ledger;

    std::vector<int> order;
    mgr.subscribe([&](const HealthEvent&) { order.push_back(1); });
    mgr.subscribe([&](const HealthEvent&) { order.push_back(2); });

    REQUIRE_FALSE(mgr.on_request_received(1, 0));
    REQUIRE_FALSE(mgr.poll(1, cfg, ledger, 200)); // overflow -> one HealthEvent

    REQUIRE(order == std::vector<int>{1, 2});
}

// ── [Phase 17 c-RCP-reference pass, cpp-RCP issue #129] Fixed-capacity ──────
// Manager::streams_/callbacks_ (ported from c-RCP's watchdog.h
// RCP_WATCHDOG_MAX_STREAMS/RCP_WATCHDOG_MAX_CALLBACKS, both 16 — see
// Manager's own doc comment). Untagged, matching rcp/loan.hpp's/
// rcp/respqueue.hpp's own fixed-capacity conversion tests: this bound is an
// engineering hardening tracked under issue #129, not itself a numbered
// REQ-WDG-* requirement in either c-RCP's or this project's own catalog.

TEST_CASE("Manager::register_stream succeeds up to kMaxStreams, then rejects further streams", "[watchdog]") {
    Manager mgr;
    for (uint64_t i = 0; i < Manager::kMaxStreams; ++i) {
        REQUIRE_FALSE(mgr.register_stream(i + 1));
    }
    REQUIRE(mgr.stream_count() == Manager::kMaxStreams);

    // One more, at capacity: rejected, not silently grown.
    REQUIRE(mgr.register_stream(Manager::kMaxStreams + 1) ==
            make_error_code(WatchdogErrc::stream_capacity_exceeded));
    REQUIRE(mgr.stream_count() == Manager::kMaxStreams);
    REQUIRE_FALSE(mgr.is_registered(Manager::kMaxStreams + 1));

    // Every stream registered before capacity was reached remains reachable
    // — confirms the fixed array was fully populated, not silently
    // truncated below capacity.
    REQUIRE(mgr.is_registered(Manager::kMaxStreams));
}

TEST_CASE("Manager::register_stream is a harmless no-op for an already-registered stream, even at capacity",
          "[watchdog]") {
    Manager mgr;
    for (uint64_t i = 0; i < Manager::kMaxStreams; ++i) {
        REQUIRE_FALSE(mgr.register_stream(i + 1));
    }
    // Re-registering an existing key never consults the capacity check.
    REQUIRE_FALSE(mgr.register_stream(1));
    REQUIRE(mgr.stream_count() == Manager::kMaxStreams);
}

TEST_CASE("Manager::unregister_stream frees a slot for a subsequent register_stream at capacity",
          "[watchdog]") {
    Manager mgr;
    for (uint64_t i = 0; i < Manager::kMaxStreams; ++i) {
        REQUIRE_FALSE(mgr.register_stream(i + 1));
    }
    REQUIRE(mgr.register_stream(999) == make_error_code(WatchdogErrc::stream_capacity_exceeded));

    mgr.unregister_stream(1);
    REQUIRE(mgr.stream_count() == Manager::kMaxStreams - 1);
    REQUIRE_FALSE(mgr.is_registered(1));

    REQUIRE_FALSE(mgr.register_stream(999)); // room again
    REQUIRE(mgr.is_registered(999));
    REQUIRE(mgr.stream_count() == Manager::kMaxStreams);
}

TEST_CASE("Manager::subscribe succeeds up to kMaxCallbacks, then rejects further subscribers", "[watchdog]") {
    Manager mgr;
    for (size_t i = 0; i < Manager::kMaxCallbacks; ++i) {
        REQUIRE_FALSE(mgr.subscribe([](const HealthEvent&) {}));
    }
    REQUIRE(mgr.callback_count() == Manager::kMaxCallbacks);

    // One more, at capacity: rejected, not silently grown.
    REQUIRE(mgr.subscribe([](const HealthEvent&) {}) ==
            make_error_code(WatchdogErrc::callback_capacity_exceeded));
    REQUIRE(mgr.callback_count() == Manager::kMaxCallbacks);
}
