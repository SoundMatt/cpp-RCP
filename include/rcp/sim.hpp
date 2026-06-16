// fusa:req REQ-SIM-001
// fusa:req REQ-SIM-002
// fusa:req REQ-SIM-003
// fusa:req REQ-SIM-004
// fusa:req REQ-SIM-005
// fusa:req REQ-SIM-006
// fusa:req REQ-SIM-007
// fusa:req REQ-SIM-008

// Timing-realistic zone controller simulator for SiL/HIL testing.
//
// sim::Controller implements the full rcp::Controller interface and adds
// Fault/Recover controls for deterministic scenario testing. Configurable
// latency (constant or jitter), periodic Status publishing, and watchdog-miss
// detection enable validation of safety mechanisms from v0.11.0–v0.16.0.
#pragma once

#include "rcp.hpp"

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

namespace rcp {
namespace sim {

// ── LatencyModel ─────────────────────────────────────────────────────────────

enum class LatencyModel : uint8_t {
    Constant = 0,
    Jitter   = 1,
};

// ── Config ────────────────────────────────────────────────────────────────────

struct Config {
    Zone                      zone;
    std::chrono::milliseconds base_latency    {2};
    std::chrono::milliseconds jitter          {1};
    std::chrono::milliseconds status_interval {10}; // 0 = disabled
    std::chrono::milliseconds watchdog_timeout{50}; // 0 = disabled
    LatencyModel              latency_model   {LatencyModel::Jitter};
};

inline Config default_config(Zone z) {
    Config cfg;
    cfg.zone = z;
    return cfg;
}

// ── Controller ────────────────────────────────────────────────────────────────

class Controller final : public rcp::Controller {
public:
    using Handler = std::function<Response(const Command&)>;

    explicit Controller(Config cfg, Handler handler = nullptr)
        : cfg_(cfg), handler_(std::move(handler)),
          rng_(std::random_device{}()) {
        if (cfg_.status_interval.count() > 0)
            status_thread_ = std::thread([this]{ status_loop(); });
        if (cfg_.watchdog_timeout.count() > 0)
            wd_thread_ = std::thread([this]{ watchdog_loop(); });
    }

    ~Controller() override {
        closed_.store(true, std::memory_order_release);
        if (status_thread_.joinable()) status_thread_.join();
        if (wd_thread_.joinable())     wd_thread_.join();
        (void)close();
    }

    Zone zone() const noexcept override { return cfg_.zone; }

    std::error_code send(const rcp::Context& ctx,
                          const Command&      cmd,
                          Response&           out) override {
        if (closed_.load(std::memory_order_acquire)) return ErrClosed;
        if (ctx.done())                               return ErrTimeout;
        if (cmd.zone != cfg_.zone)                    return ErrZoneMismatch;

        {
            std::lock_guard<std::mutex> lk(mu_);
            if (fault_err_) return fault_err_;
        }

        // Simulate response latency.
        auto delay = cfg_.base_latency;
        if (cfg_.latency_model == LatencyModel::Jitter && cfg_.jitter.count() > 0) {
            std::uniform_int_distribution<long> dist(0, cfg_.jitter.count());
            std::lock_guard<std::mutex> lk(mu_);
            delay += std::chrono::milliseconds(dist(rng_));
        }
        if (delay.count() > 0)
            std::this_thread::sleep_for(delay);

        if (ctx.done()) return ErrTimeout;

        // Reset watchdog on CmdWatchdog.
        if (cmd.type == CommandType::Watchdog) {
            wd_last_kick_.store(
                std::chrono::steady_clock::now().time_since_epoch().count(),
                std::memory_order_release);
        }

        if (handler_) {
            std::lock_guard<std::mutex> lk(mu_);
            out = handler_(cmd);
        } else {
            out = Response{cmd.id, cfg_.zone, ResponseStatus::OK, {}};
        }
        return {};
    }

    std::error_code subscribe(const rcp::Context&             ctx,
                               std::shared_ptr<StatusChannel>& out) override {
        if (closed_.load(std::memory_order_acquire)) return ErrClosed;
        auto ch = std::make_shared<StatusChannel>(16);
        {
            std::lock_guard<std::mutex> lk(mu_);
            subs_.push_back(ch);
        }
        std::thread([this, weak_ch = std::weak_ptr<StatusChannel>(ch), ctx]() mutable {
            while (!ctx.done() && !closed_.load()) {
                auto c = weak_ch.lock();
                if (!c || c->is_closed()) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            auto c = weak_ch.lock();
            if (!c) return;
            std::lock_guard<std::mutex> lk(mu_);
            auto it = std::find(subs_.begin(), subs_.end(), c);
            if (it != subs_.end()) subs_.erase(it);
            c->close();
        }).detach();
        out = std::move(ch);
        return {};
    }

    std::error_code close() override {
        if (!closed_.exchange(true, std::memory_order_acq_rel)) {
            std::lock_guard<std::mutex> lk(mu_);
            for (auto& s : subs_) s->close();
            subs_.clear();
        }
        return {};
    }

    // fault injects err on all subsequent send() calls until recover() is called.
    void fault(std::error_code err) {
        std::lock_guard<std::mutex> lk(mu_);
        fault_err_ = err;
    }

    // recover clears any injected fault.
    void recover() {
        std::lock_guard<std::mutex> lk(mu_);
        fault_err_ = {};
    }

    bool watchdog_missed() const noexcept {
        return wd_miss_.load(std::memory_order_acquire);
    }

    // publish sends a Status to all subscribers (called by test harness).
    void publish(const std::vector<uint8_t>& payload) {
        uint32_t seq = ++seq_;
        Status st{cfg_.zone, seq, !closed_.load(), payload};
        std::lock_guard<std::mutex> lk(mu_);
        for (auto& s : subs_) s->push(st);
    }

private:
    Config    cfg_;
    Handler   handler_;
    std::mt19937 rng_;
    std::atomic<bool>     closed_{false};
    std::atomic<uint32_t> seq_{0};
    std::atomic<bool>     wd_miss_{false};
    std::atomic<long long> wd_last_kick_{0};

    std::mutex mu_;
    std::error_code fault_err_;
    std::vector<std::shared_ptr<StatusChannel>> subs_;
    std::thread status_thread_;
    std::thread wd_thread_;

    void status_loop() {
        while (!closed_.load(std::memory_order_acquire)) {
            publish({});
            std::this_thread::sleep_for(cfg_.status_interval);
        }
    }

    void watchdog_loop() {
        while (!closed_.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(cfg_.watchdog_timeout);
            auto last = wd_last_kick_.load(std::memory_order_acquire);
            if (last == 0) {
                wd_miss_.store(true, std::memory_order_release);
                continue;
            }
            auto last_tp = std::chrono::steady_clock::time_point(
                std::chrono::steady_clock::duration(last));
            auto elapsed = std::chrono::steady_clock::now() - last_tp;
            if (elapsed >= cfg_.watchdog_timeout) {
                wd_miss_.store(true, std::memory_order_release);
            } else {
                wd_miss_.store(false, std::memory_order_release);
            }
        }
    }
};

inline std::unique_ptr<Controller> new_controller(Config cfg, Controller::Handler h = nullptr) {
    return std::make_unique<Controller>(cfg, std::move(h));
}

} // namespace sim
} // namespace rcp
