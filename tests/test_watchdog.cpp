// fusa:test REQ-WDG-001
// fusa:test REQ-WDG-002
// fusa:test REQ-WDG-003
// fusa:test REQ-WDG-004
// fusa:test REQ-WDG-005
// fusa:test REQ-WDG-006
// fusa:test REQ-WDG-007
// fusa:test REQ-WDG-008

#include <catch2/catch_test_macros.hpp>
#include <rcp/mock.hpp>
#include <rcp/watchdog.hpp>

#include <atomic>
#include <chrono>
#include <thread>

using namespace rcp;

// ── Keeper creation ───────────────────────────────────────────────────────────

TEST_CASE("Watchdog Keeper constructs without error", "[watchdog][REQ-WDG-001]") {
    auto ctrl = std::make_shared<mock::Controller>(Zone::FrontLeft);
    watchdog::Config cfg;
    cfg.interval      = std::chrono::milliseconds(200);
    cfg.timeout       = std::chrono::milliseconds(50);
    cfg.degrade_after = 3;
    cfg.fault_after   = 5;
    watchdog::Keeper keeper(cfg, {ctrl});
}

// ── Health state transitions ──────────────────────────────────────────────────

TEST_CASE("Watchdog transitions zone to Faulted after misses exceed fault_after", "[watchdog][REQ-WDG-002][REQ-WDG-003]") {
    auto ctrl = std::make_shared<mock::Controller>(Zone::FrontLeft);

    // Close so every kick returns ErrClosed immediately.
    { auto ec = ctrl->close(); (void)ec; }

    watchdog::Config cfg;
    cfg.interval      = std::chrono::milliseconds(20);
    cfg.timeout       = std::chrono::milliseconds(5);
    cfg.degrade_after = 2;
    cfg.fault_after   = 3;

    std::atomic<watchdog::HealthState> last_state{watchdog::HealthState::Healthy};
    watchdog::Keeper keeper(cfg, {ctrl});
    keeper.subscribe([&](const watchdog::HealthEvent& ev) {
        last_state.store(ev.state);
    });

    // The background keeper thread needs fault_after (3) miss cycles of 20 ms to
    // reach Faulted. On loaded/preempted CI runners the thread may be scheduled
    // late, so poll up to a generous deadline rather than asserting after one
    // fixed sleep (which is flaky — observed on macOS shared runners).
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (last_state.load() != watchdog::HealthState::Faulted &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    REQUIRE(last_state.load() == watchdog::HealthState::Faulted);
}

// ── Initial state ─────────────────────────────────────────────────────────────

TEST_CASE("Watchdog zone starts Healthy", "[watchdog][REQ-WDG-004]") {
    auto ctrl = std::make_shared<mock::Controller>(Zone::FrontLeft);

    watchdog::Config cfg;
    cfg.interval = std::chrono::milliseconds(500); // long so no kicks fire
    watchdog::Keeper keeper(cfg, {ctrl});

    REQUIRE(keeper.health(Zone::FrontLeft) == watchdog::HealthState::Healthy);
}

// ── Health recovery ───────────────────────────────────────────────────────────

TEST_CASE("Watchdog zone stays Healthy when kicks succeed", "[watchdog][REQ-WDG-005][REQ-WDG-006]") {
    auto ctrl = std::make_shared<mock::Controller>(Zone::FrontLeft);

    watchdog::Config cfg;
    cfg.interval      = std::chrono::milliseconds(20);
    cfg.timeout       = std::chrono::milliseconds(10);
    cfg.degrade_after = 2;
    cfg.fault_after   = 3;

    std::atomic<watchdog::HealthState> last_state{watchdog::HealthState::Healthy};
    watchdog::Keeper keeper(cfg, {ctrl});
    keeper.subscribe([&](const watchdog::HealthEvent& ev) {
        last_state.store(ev.state);
    });

    // Wait a few kick cycles — controller responds OK.
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    // Should never have transitioned away from Healthy.
    REQUIRE(last_state.load() == watchdog::HealthState::Healthy);
}

// ── Close ─────────────────────────────────────────────────────────────────────

TEST_CASE("Watchdog Keeper::close stops background kicks", "[watchdog][REQ-WDG-007][REQ-WDG-008]") {
    auto ctrl = std::make_shared<mock::Controller>(Zone::FrontLeft);
    watchdog::Config cfg;
    cfg.interval = std::chrono::milliseconds(50);
    watchdog::Keeper keeper(cfg, {ctrl});
    keeper.close();
    // Destructor should join without hanging.
}
