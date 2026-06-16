// fusa:req REQ-DL-001
// fusa:req REQ-DL-002
// fusa:req REQ-DL-003
// fusa:req REQ-DL-004
// fusa:req REQ-DL-005
// fusa:req REQ-DL-006
// fusa:req REQ-DL-007
// fusa:req REQ-DL-008

// Liveness deadline monitor for zone controller Status streams (SG-001, SG-004).
//
// A Monitor subscribes to each registered zone controller and resets a per-zone
// deadline timer on every incoming Status frame. If no Status arrives within the
// deadline, the zone transitions to dead and a LivenessEvent is emitted.
// Recovery to alive is reported as soon as the next Status arrives.
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
namespace deadline {

struct LivenessEvent {
    Zone  zone;
    bool  alive;
    std::error_code err; // non-null when subscription itself failed
};

struct Config {
    std::chrono::milliseconds deadline{50}; // 20 Hz status cadence
};

inline Config default_config() { return Config{}; }

// ── Monitor ───────────────────────────────────────────────────────────────────

class Monitor {
public:
    using LivenessCallback = std::function<void(const LivenessEvent&)>;

    explicit Monitor(Config cfg, std::vector<std::shared_ptr<rcp::Controller>> ctrls)
        : cfg_(cfg) {
        for (auto& c : ctrls) {
            states_[c->zone()] = false;
        }
        for (auto& c : ctrls) {
            auto t = std::thread([this, c]{ watch(c); });
            watch_threads_.push_back(std::move(t));
        }
    }

    ~Monitor() {
        closed_.store(true, std::memory_order_release);
        for (auto& t : watch_threads_) {
            if (t.joinable()) t.join();
        }
    }

    bool alive(Zone z) const {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = states_.find(z);
        return it != states_.end() && it->second;
    }

    void subscribe(LivenessCallback cb) {
        std::lock_guard<std::mutex> lk(mu_);
        callbacks_.push_back(std::move(cb));
    }

    void close() {
        closed_.store(true, std::memory_order_release);
    }

private:
    Config cfg_;
    mutable std::mutex mu_;
    std::map<Zone, bool> states_;
    std::vector<LivenessCallback> callbacks_;
    std::atomic<bool> closed_{false};
    std::vector<std::thread> watch_threads_;

    void emit(const LivenessEvent& ev) {
        std::lock_guard<std::mutex> lk(mu_);
        states_[ev.zone] = ev.alive;
        for (auto& cb : callbacks_) cb(ev);
    }

    void watch(std::shared_ptr<rcp::Controller> ctrl) {
        Zone z   = ctrl->zone();
        // Use a background context — the monitor drives lifetime.
        auto ctx = Context::background();
        std::shared_ptr<StatusChannel> ch;
        auto ec = ctrl->subscribe(ctx, ch);
        if (ec) {
            emit({z, false, ec});
            return;
        }

        bool is_alive      = false;
        bool ever_reported = false; // first dead event always fires
        auto deadline = std::chrono::steady_clock::now() + cfg_.deadline;

        while (!closed_.load(std::memory_order_acquire)) {
            auto now = std::chrono::steady_clock::now();
            if (now >= deadline) {
                // Deadline missed.
                if (is_alive || !ever_reported) {
                    is_alive      = false;
                    ever_reported = true;
                    emit({z, false, {}});
                }
                deadline = now + cfg_.deadline;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            if (ch->is_closed()) break;

            auto maybe = ch->try_recv();
            if (maybe) {
                deadline = std::chrono::steady_clock::now() + cfg_.deadline;
                if (!is_alive) {
                    is_alive = true;
                    emit({z, true, {}});
                }
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        if (is_alive) emit({z, false, {}});
    }
};

inline std::unique_ptr<Monitor> new_monitor(
    Config cfg,
    std::vector<std::shared_ptr<rcp::Controller>> ctrls) {
    return std::make_unique<Monitor>(cfg, std::move(ctrls));
}

} // namespace deadline
} // namespace rcp
