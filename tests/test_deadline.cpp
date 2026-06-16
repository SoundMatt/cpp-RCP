// fusa:test REQ-DL-001
// fusa:test REQ-DL-002
// fusa:test REQ-DL-003
// fusa:test REQ-DL-004
// fusa:test REQ-DL-005
// fusa:test REQ-DL-006
// fusa:test REQ-DL-007
// fusa:test REQ-DL-008

#include <catch2/catch_test_macros.hpp>
#include <rcp/mock.hpp>
#include <rcp/deadline.hpp>

#include <atomic>

using namespace rcp;

// ── Monitor creation ──────────────────────────────────────────────────────────

TEST_CASE("Deadline Monitor constructs without error", "[deadline][REQ-DL-001]") {
    auto ctrl = std::make_shared<mock::Controller>(Zone::FrontLeft);
    deadline::Config cfg;
    cfg.deadline = std::chrono::milliseconds(100);
    deadline::Monitor mon(cfg, {ctrl});
}

// ── Dead zone detected ────────────────────────────────────────────────────────

TEST_CASE("Deadline Monitor fires alive=false when no status arrives", "[deadline][REQ-DL-002][REQ-DL-003]") {
    auto ctrl = std::make_shared<mock::Controller>(Zone::FrontLeft);
    deadline::Config cfg;
    cfg.deadline = std::chrono::milliseconds(30);

    std::atomic<int> dead_count{0};
    deadline::Monitor mon(cfg, {ctrl});
    mon.subscribe([&](const deadline::LivenessEvent& ev) {
        if (!ev.alive) ++dead_count;
    });

    // No status published — wait for deadline to expire.
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    REQUIRE(dead_count.load() > 0);
}

// ── Live zone detected ────────────────────────────────────────────────────────

TEST_CASE("Deadline Monitor reports alive=true when status arrives", "[deadline][REQ-DL-004][REQ-DL-005]") {
    auto ctrl = std::make_shared<mock::Controller>(Zone::FrontLeft);
    deadline::Config cfg;
    cfg.deadline = std::chrono::milliseconds(200);

    std::atomic<int> alive_count{0};
    deadline::Monitor mon(cfg, {ctrl});
    mon.subscribe([&](const deadline::LivenessEvent& ev) {
        if (ev.alive) ++alive_count;
    });

    // Let the monitor start its watch thread then publish a status.
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    ctrl->publish({});

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    REQUIRE(alive_count.load() > 0);
}

// ── alive() query ────────────────────────────────────────────────────────────

TEST_CASE("Monitor::alive returns false before any status arrives", "[deadline][REQ-DL-006]") {
    auto ctrl = std::make_shared<mock::Controller>(Zone::FrontLeft);
    deadline::Config cfg;
    cfg.deadline = std::chrono::milliseconds(5000); // long timeout
    deadline::Monitor mon(cfg, {ctrl});
    // Immediately after construction, no status has arrived.
    REQUIRE_FALSE(mon.alive(Zone::FrontLeft));
}

// ── Close ─────────────────────────────────────────────────────────────────────

TEST_CASE("Deadline Monitor::close stops watch threads", "[deadline][REQ-DL-007][REQ-DL-008]") {
    auto ctrl = std::make_shared<mock::Controller>(Zone::FrontLeft);
    deadline::Config cfg;
    cfg.deadline = std::chrono::milliseconds(100);
    deadline::Monitor mon(cfg, {ctrl});
    mon.close();
    // Destructor should join without hanging.
}
