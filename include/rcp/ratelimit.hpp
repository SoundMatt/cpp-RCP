// fusa:req REQ-RL-001
// fusa:req REQ-RL-002
// fusa:req REQ-RL-003
// fusa:req REQ-RL-004
// fusa:req REQ-RL-005
// fusa:req REQ-RL-006
// fusa:req REQ-RL-007
// fusa:req REQ-RL-008

// Per-zone token-bucket admission control against command flooding (SG-009, H-009).
//
// Controller wraps any rcp::Controller and enforces a sustained rate limit and
// burst capacity. Priority::Critical commands bypass the bucket by default so
// watchdog kicks and emergency actuations are never throttled. All other
// commands consume one token; send() returns ErrBusy immediately when exhausted.
#pragma once

#include "rcp.hpp"

#include <atomic>
#include <chrono>
#include <mutex>

namespace rcp {
namespace ratelimit {

struct Config {
    double rate            = 100.0; // sustained token refill rate (tokens/second)
    int    burst           = 20;    // maximum token accumulation
    bool   exempt_critical = true;  // if true, Priority::Critical bypasses bucket
};

// DefaultConfig returns ASIL-B recommended values.
inline Config default_config() {
    return Config{};
}

class Controller final : public rcp::Controller {
public:
    explicit Controller(std::shared_ptr<rcp::Controller> inner, Config cfg = {})
        : inner_(std::move(inner)), cfg_(cfg),
          tokens_(static_cast<double>(cfg.burst)),
          last_(std::chrono::steady_clock::now()) {}

    Zone zone() const noexcept override { return inner_->zone(); }

    std::error_code send(const rcp::Context& ctx,
                          const Command&      cmd,
                          Response&           out) override {
        if (closed_.load(std::memory_order_acquire))
            return ErrClosed;
        bool exempt = cfg_.exempt_critical && (cmd.priority == Priority::Critical);
        if (!exempt && !take())
            return ErrBusy;
        return inner_->send(ctx, cmd, out);
    }

    std::error_code subscribe(const rcp::Context&             ctx,
                               std::shared_ptr<StatusChannel>& out) override {
        return inner_->subscribe(ctx, out);
    }

    std::error_code close() override {
        closed_.store(true, std::memory_order_release);
        return inner_->close();
    }

private:
    std::shared_ptr<rcp::Controller> inner_;
    Config cfg_;
    std::atomic<bool> closed_{false};

    std::mutex mu_;
    double     tokens_;
    std::chrono::steady_clock::time_point last_;

    bool take() {
        std::lock_guard<std::mutex> lk(mu_);
        auto now     = std::chrono::steady_clock::now();
        double secs  = std::chrono::duration<double>(now - last_).count();
        last_        = now;
        tokens_     += secs * cfg_.rate;
        if (tokens_ > static_cast<double>(cfg_.burst))
            tokens_ = static_cast<double>(cfg_.burst);
        if (tokens_ < 1.0) return false;
        tokens_ -= 1.0;
        return true;
    }
};

inline std::shared_ptr<Controller> new_controller(std::shared_ptr<rcp::Controller> inner,
                                                    Config cfg = {}) {
    return std::make_shared<Controller>(std::move(inner), cfg);
}

} // namespace ratelimit
} // namespace rcp
