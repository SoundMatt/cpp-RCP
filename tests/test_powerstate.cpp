// fusa:test REQ-PWR-001
// fusa:test REQ-PWR-002
// fusa:test REQ-PWR-003
// fusa:test REQ-PWR-004
// fusa:test REQ-PWR-005
// fusa:test REQ-PWR-006
// fusa:test REQ-PWR-007
// fusa:test REQ-PWR-008

// Power-state Manager tests (SG-003, ISO 26262 ASIL-B).
//
// Verifies Sleep/Wake command emission and the Active/Sleeping/BusOff state
// machine, including automatic recovery of BusOff zones by the background loop.
#include <catch2/catch_test_macros.hpp>

#include "rcp/mock.hpp"
#include "rcp/powerstate.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

using namespace rcp;
using namespace std::chrono_literals;

namespace {

// AlwaysFail fails every send with a fixed error and counts attempts.
class AlwaysFail final : public rcp::Controller {
public:
    AlwaysFail(Zone z, std::error_code ec) : zone_(z), ec_(ec) {}
    Zone zone() const noexcept override { return zone_; }
    std::error_code send(const Context&, const Command&, Response&) override {
        sends.fetch_add(1, std::memory_order_relaxed);
        return ec_;
    }
    std::error_code subscribe(const Context&, std::shared_ptr<StatusChannel>&) override {
        return ec_;
    }
    std::error_code close() override { return {}; }
    std::atomic<int> sends{0};
private:
    Zone zone_;
    std::error_code ec_;
};

// FailThenOk fails the first `fail_count` sends, then succeeds.
class FailThenOk final : public rcp::Controller {
public:
    FailThenOk(Zone z, int fail_count) : zone_(z), remaining_(fail_count) {}
    Zone zone() const noexcept override { return zone_; }
    std::error_code send(const Context&, const Command&, Response& out) override {
        if (remaining_.fetch_sub(1, std::memory_order_acq_rel) > 0)
            return ErrTimeout;
        out = Response{0, zone_, ResponseStatus::OK, {}};
        return {};
    }
    std::error_code subscribe(const Context&, std::shared_ptr<StatusChannel>&) override {
        return {};
    }
    std::error_code close() override { return {}; }
private:
    Zone zone_;
    std::atomic<int> remaining_;
};

template <typename Pred>
bool wait_until(Pred p, std::chrono::milliseconds deadline = 5s) {
    auto end = std::chrono::steady_clock::now() + deadline;
    while (std::chrono::steady_clock::now() < end) {
        if (p()) return true;
        std::this_thread::sleep_for(2ms);
    }
    return p();
}

} // namespace

TEST_CASE("powerstate: sleep sends CmdSleep and transitions Active->Sleeping",
          "[powerstate][REQ-PWR-001][REQ-PWR-002]") {
    std::atomic<int> last_type{-1};
    auto ctrl = std::make_shared<mock::Controller>(
        Zone::FrontLeft, [&](const Command& c) {
            last_type.store(static_cast<int>(c.type));
            return Response{c.id, Zone::FrontLeft, ResponseStatus::OK, {}};
        });
    auto mgr = powerstate::new_manager(powerstate::default_config(), {ctrl});

    REQUIRE_FALSE(mgr->sleep(Context{}, Zone::FrontLeft));
    REQUIRE(last_type.load() == static_cast<int>(CommandType::Sleep)); // REQ-PWR-001
    REQUIRE(mgr->state(Zone::FrontLeft) == powerstate::PowerState::Sleeping); // REQ-PWR-002
}

TEST_CASE("powerstate: wake sends CmdWake and transitions Sleeping->Active",
          "[powerstate][REQ-PWR-003][REQ-PWR-004]") {
    std::atomic<int> last_type{-1};
    auto ctrl = std::make_shared<mock::Controller>(
        Zone::FrontLeft, [&](const Command& c) {
            last_type.store(static_cast<int>(c.type));
            return Response{c.id, Zone::FrontLeft, ResponseStatus::OK, {}};
        });
    auto mgr = powerstate::new_manager(powerstate::default_config(), {ctrl});

    REQUIRE_FALSE(mgr->sleep(Context{}, Zone::FrontLeft));
    REQUIRE(mgr->state(Zone::FrontLeft) == powerstate::PowerState::Sleeping);

    REQUIRE_FALSE(mgr->wake(Context{}, Zone::FrontLeft));
    REQUIRE(last_type.load() == static_cast<int>(CommandType::Wake)); // REQ-PWR-003
    REQUIRE(mgr->state(Zone::FrontLeft) == powerstate::PowerState::Active); // REQ-PWR-004
}

TEST_CASE("powerstate: command failure yields BusOff", "[powerstate][REQ-PWR-005]") {
    auto ctrl = std::make_shared<AlwaysFail>(Zone::Central, ErrTimeout);
    powerstate::Config cfg;
    cfg.recovery_interval = 1h; // effectively disable recovery for this test
    auto mgr = powerstate::new_manager(cfg, {ctrl});

    auto ec = mgr->sleep(Context{}, Zone::Central);
    REQUIRE(ec); // the underlying send failed
    REQUIRE(mgr->state(Zone::Central) == powerstate::PowerState::BusOff);
}

TEST_CASE("powerstate: recover_loop retries BusOff zones", "[powerstate][REQ-PWR-006]") {
    // First send (the sleep) fails -> BusOff; the recovery loop's Wake succeeds.
    auto ctrl = std::make_shared<FailThenOk>(Zone::RearLeft, /*fail_count=*/1);
    powerstate::Config cfg;
    cfg.recovery_interval = 10ms;
    cfg.recovery_timeout  = 10ms;
    auto mgr = powerstate::new_manager(cfg, {ctrl});

    REQUIRE(mgr->sleep(Context{}, Zone::RearLeft)); // fails -> BusOff
    REQUIRE(mgr->state(Zone::RearLeft) == powerstate::PowerState::BusOff);

    // Background recovery should bring the zone back to Active.
    REQUIRE(wait_until([&] {
        return mgr->state(Zone::RearLeft) == powerstate::PowerState::Active;
    }));
}

TEST_CASE("powerstate: state() is thread-safe", "[powerstate][REQ-PWR-007]") {
    auto ctrl = std::make_shared<mock::Controller>(Zone::FrontRight);
    auto mgr  = powerstate::new_manager(powerstate::default_config(), {ctrl});

    std::atomic<bool> stop{false};
    std::vector<std::thread> readers;
    for (int i = 0; i < 4; ++i) {
        readers.emplace_back([&] {
            while (!stop.load()) {
                auto s = mgr->state(Zone::FrontRight);
                REQUIRE((s == powerstate::PowerState::Active ||
                         s == powerstate::PowerState::Sleeping ||
                         s == powerstate::PowerState::BusOff));
            }
        });
    }
    for (int i = 0; i < 50; ++i) {
        auto e1 = mgr->sleep(Context{}, Zone::FrontRight); (void)e1;
        auto e2 = mgr->wake(Context{}, Zone::FrontRight);  (void)e2;
    }
    stop.store(true);
    for (auto& t : readers) t.join();
}

TEST_CASE("powerstate: close stops the recovery loop", "[powerstate][REQ-PWR-008]") {
    auto ctrl = std::make_shared<AlwaysFail>(Zone::RearRight, ErrTimeout);
    powerstate::Config cfg;
    cfg.recovery_interval = 10ms;
    cfg.recovery_timeout  = 10ms;
    auto mgr = powerstate::new_manager(cfg, {ctrl});

    REQUIRE(mgr->sleep(Context{}, Zone::RearRight)); // -> BusOff, recovery starts retrying
    REQUIRE(wait_until([&] { return ctrl->sends.load() >= 3; })); // loop is active

    mgr->close();
    std::this_thread::sleep_for(50ms);   // let any in-flight attempt settle
    int after_close = ctrl->sends.load();
    std::this_thread::sleep_for(100ms);  // several recovery_intervals
    // No further send attempts once the loop has stopped (allow one in-flight).
    REQUIRE(ctrl->sends.load() <= after_close + 1);
}
