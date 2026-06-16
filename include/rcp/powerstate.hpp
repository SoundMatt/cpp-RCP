// fusa:req REQ-PWR-001
// fusa:req REQ-PWR-002
// fusa:req REQ-PWR-003
// fusa:req REQ-PWR-004
// fusa:req REQ-PWR-005
// fusa:req REQ-PWR-006
// fusa:req REQ-PWR-007
// fusa:req REQ-PWR-008

// Zone controller power state manager (SG-003, ISO 26262 ASIL-B).
//
// A Manager sends CommandType::Sleep / CommandType::Wake to zone controllers
// and tracks the resulting power state. When a command fails the zone
// transitions to BusOff and the Manager retries CommandType::Wake at the
// configured recovery interval.
#pragma once

#include "rcp.hpp"

#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace rcp {
namespace powerstate {

// ── PowerState ────────────────────────────────────────────────────────────────

enum class PowerState : uint8_t {
    Active   = 0,
    Sleeping = 1,
    BusOff   = 2,
};

inline std::string to_string(PowerState p) {
    switch (p) {
    case PowerState::Active:   return "active";
    case PowerState::Sleeping: return "sleeping";
    case PowerState::BusOff:   return "bus-off";
    default:                   return "unknown";
    }
}

struct PowerEvent {
    Zone       zone;
    PowerState state;
    std::error_code err;
};

// ── Config ────────────────────────────────────────────────────────────────────

struct Config {
    std::chrono::milliseconds recovery_interval{100};
    std::chrono::milliseconds recovery_timeout  {50};
};

inline Config default_config() { return Config{}; }

// ── Manager ───────────────────────────────────────────────────────────────────

class Manager {
public:
    using PowerCallback = std::function<void(const PowerEvent&)>;

    explicit Manager(Config cfg, std::vector<std::shared_ptr<rcp::Controller>> ctrls)
        : cfg_(cfg) {
        for (auto& c : ctrls) {
            ctrls_[c->zone()] = c;
            states_[c->zone()] = PowerState::Active;
        }
        recover_thread_ = std::thread([this]{ recover_loop(); });
    }

    ~Manager() {
        closed_.store(true, std::memory_order_release);
        if (recover_thread_.joinable()) recover_thread_.join();
    }

    PowerState state(Zone z) const {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = states_.find(z);
        return it != states_.end() ? it->second : PowerState::BusOff;
    }

    std::error_code sleep(const Context& ctx, Zone z) {
        auto ctrl = get_ctrl(z);
        if (!ctrl) return ErrNotFound;
        {
            std::lock_guard<std::mutex> lk(mu_);
            if (states_[z] != PowerState::Active)
                return make_error_code(Errc::busy);
        }
        Command cmd;
        cmd.zone     = z;
        cmd.type     = CommandType::Sleep;
        cmd.priority = Priority::High;
        Response resp;
        auto ec = ctrl->send(ctx, cmd, resp);
        transition(z, ec ? PowerState::BusOff : PowerState::Sleeping, ec);
        return ec;
    }

    std::error_code wake(const Context& ctx, Zone z) {
        auto ctrl = get_ctrl(z);
        if (!ctrl) return ErrNotFound;
        {
            std::lock_guard<std::mutex> lk(mu_);
            if (states_[z] == PowerState::Active)
                return make_error_code(Errc::busy);
        }
        Command cmd;
        cmd.zone     = z;
        cmd.type     = CommandType::Wake;
        cmd.priority = Priority::High;
        Response resp;
        auto ec = ctrl->send(ctx, cmd, resp);
        transition(z, ec ? PowerState::BusOff : PowerState::Active, ec);
        return ec;
    }

    void subscribe(PowerCallback cb) {
        std::lock_guard<std::mutex> lk(mu_);
        callbacks_.push_back(std::move(cb));
    }

    void close() {
        closed_.store(true, std::memory_order_release);
    }

private:
    Config cfg_;
    mutable std::mutex mu_;
    std::map<Zone, std::shared_ptr<rcp::Controller>> ctrls_;
    std::map<Zone, PowerState> states_;
    std::vector<PowerCallback> callbacks_;
    std::atomic<bool> closed_{false};
    std::thread recover_thread_;

    std::shared_ptr<rcp::Controller> get_ctrl(Zone z) const {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = ctrls_.find(z);
        return it != ctrls_.end() ? it->second : nullptr;
    }

    void transition(Zone z, PowerState next, std::error_code ec) {
        std::lock_guard<std::mutex> lk(mu_);
        if (states_[z] == next) return;
        states_[z] = next;
        PowerEvent ev{z, next, ec};
        for (auto& cb : callbacks_) cb(ev);
    }

    void recover_loop() {
        while (!closed_.load(std::memory_order_acquire)) {
            attempt_recovery();
            std::this_thread::sleep_for(cfg_.recovery_interval);
        }
    }

    void attempt_recovery() {
        std::vector<Zone> bus_off;
        {
            std::lock_guard<std::mutex> lk(mu_);
            for (auto& kv : states_) {
                if (kv.second == PowerState::BusOff) bus_off.push_back(kv.first);
            }
        }
        for (Zone z : bus_off) {
            auto ctrl = get_ctrl(z);
            if (!ctrl) continue;
            Command cmd;
            cmd.zone     = z;
            cmd.type     = CommandType::Wake;
            cmd.priority = Priority::High;
            auto ctx = Context::with_timeout(cfg_.recovery_timeout);
            Response resp;
            auto ec = ctrl->send(ctx, cmd, resp);
            if (!ec) transition(z, PowerState::Active, {});
        }
    }
};

inline std::unique_ptr<Manager> new_manager(
    Config cfg,
    std::vector<std::shared_ptr<rcp::Controller>> ctrls) {
    return std::make_unique<Manager>(cfg, std::move(ctrls));
}

} // namespace powerstate
} // namespace rcp
