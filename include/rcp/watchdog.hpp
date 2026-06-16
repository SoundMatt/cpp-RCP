// fusa:req REQ-WDG-001
// fusa:req REQ-WDG-002
// fusa:req REQ-WDG-003
// fusa:req REQ-WDG-004
// fusa:req REQ-WDG-005
// fusa:req REQ-WDG-006
// fusa:req REQ-WDG-007
// fusa:req REQ-WDG-008

// Watchdog keeper for ASIL-B zone controller liveness (SG-001, SG-003, SG-007).
//
// A Keeper periodically sends CommandType::Watchdog to each registered zone
// controller. Consecutive failures transition a zone through the health state
// machine: Healthy → Degraded → Faulted.
#pragma once

#include "rcp.hpp"

#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace rcp {
namespace watchdog {

// ── HealthState ───────────────────────────────────────────────────────────────

enum class HealthState : uint8_t {
    Healthy  = 0,
    Degraded = 1,
    Faulted  = 2,
};

inline std::string to_string(HealthState h) {
    switch (h) {
    case HealthState::Healthy:  return "healthy";
    case HealthState::Degraded: return "degraded";
    case HealthState::Faulted:  return "faulted";
    default:                    return "unknown";
    }
}

struct HealthEvent {
    Zone        zone;
    HealthState state;
    std::error_code err;
};

// ── Config ────────────────────────────────────────────────────────────────────

struct Config {
    std::chrono::milliseconds interval     {10};  // 100 Hz
    std::chrono::milliseconds timeout      {5};   // per-kick deadline
    int                       degrade_after{3};
    int                       fault_after  {5};
};

inline Config default_config() { return Config{}; }

// ── Keeper ────────────────────────────────────────────────────────────────────

class Keeper {
public:
    using HealthCallback = std::function<void(const HealthEvent&)>;

    explicit Keeper(Config cfg, std::vector<std::shared_ptr<rcp::Controller>> ctrls)
        : cfg_(cfg) {
        for (auto& c : ctrls) {
            ctrls_[c->zone()] = c;
            states_[c->zone()] = State{};
        }
        run_thread_ = std::thread([this]{ run(); });
    }

    ~Keeper() {
        closed_.store(true, std::memory_order_release);
        if (run_thread_.joinable()) run_thread_.join();
    }

    // health returns the current health state for zone.
    HealthState health(Zone z) const {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = states_.find(z);
        return it != states_.end() ? it->second.health : HealthState::Faulted;
    }

    // subscribe registers a callback fired on every health state change.
    // Not thread-safe with close(); register before starting operation.
    void subscribe(HealthCallback cb) {
        std::lock_guard<std::mutex> lk(mu_);
        callbacks_.push_back(std::move(cb));
    }

    void close() {
        closed_.store(true, std::memory_order_release);
    }

private:
    struct State {
        HealthState health = HealthState::Healthy;
        int         misses = 0;
        uint32_t    cmd_id = 0;
    };

    Config cfg_;
    std::map<Zone, std::shared_ptr<rcp::Controller>> ctrls_;
    mutable std::mutex mu_;
    std::map<Zone, State> states_;
    std::vector<HealthCallback> callbacks_;
    std::atomic<bool> closed_{false};
    std::thread run_thread_;

    void run() {
        while (!closed_.load(std::memory_order_acquire)) {
            kick_all();
            std::this_thread::sleep_for(cfg_.interval);
        }
    }

    void kick_all() {
        for (auto& kv : ctrls_) {
            Zone z = kv.first;
            auto ctrl = kv.second;
            std::thread([this, z, ctrl]{ kick(z, ctrl); }).detach();
        }
    }

    void kick(Zone z, std::shared_ptr<rcp::Controller> ctrl) {
        uint32_t id;
        {
            std::lock_guard<std::mutex> lk(mu_);
            id = ++states_[z].cmd_id;
        }
        Command cmd;
        cmd.id       = id;
        cmd.zone     = z;
        cmd.type     = CommandType::Watchdog;
        cmd.priority = Priority::High;

        auto ctx = Context::with_timeout(cfg_.timeout);
        Response resp;
        auto ec  = ctrl->send(ctx, cmd, resp);

        std::lock_guard<std::mutex> lk(mu_);
        auto& st = states_[z];
        HealthState next = st.health;
        if (!ec) {
            st.misses = 0;
            next      = HealthState::Healthy;
        } else {
            ++st.misses;
            if (st.misses >= cfg_.fault_after)    next = HealthState::Faulted;
            else if (st.misses >= cfg_.degrade_after) next = HealthState::Degraded;
        }
        if (next != st.health) {
            st.health = next;
            HealthEvent ev{z, next, ec};
            for (auto& cb : callbacks_) cb(ev);
        }
    }
};

inline std::unique_ptr<Keeper> new_keeper(
    Config cfg,
    std::vector<std::shared_ptr<rcp::Controller>> ctrls) {
    return std::make_unique<Keeper>(cfg, std::move(ctrls));
}

} // namespace watchdog
} // namespace rcp
