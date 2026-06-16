// fusa:req REQ-FI-001
// fusa:req REQ-FI-002
// fusa:req REQ-FI-003
// fusa:req REQ-FI-004
// fusa:req REQ-FI-005
// fusa:req REQ-FI-006
// fusa:req REQ-FI-007
// fusa:req REQ-FI-008

// Structured fault injection for validating safety mechanisms (v0.11.0–v0.16.0).
//
// Controller wraps any rcp::Controller and intercepts send() calls according
// to an ordered list of Rules. Rules may drop responses, add latency, return
// errors, or return timeouts. Count-based rules auto-expire after N applications.
#pragma once

#include "rcp.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace rcp {
namespace faultinject {

// ── FaultType ─────────────────────────────────────────────────────────────────

enum class FaultType : uint8_t {
    Drop    = 1, // return an error without calling inner send()
    Slow    = 2, // sleep Rule::latency, then call inner send()
    Error   = 3, // return ResponseStatus::Error without calling inner
    Timeout = 4, // return ErrTimeout without calling inner
};

// ── Rule ─────────────────────────────────────────────────────────────────────

struct Rule {
    FaultType                 type;
    std::chrono::milliseconds latency{0}; // used by FaultType::Slow
    int                       count{-1};  // -1 = forever; >0 = fires N times
    int                       fired{0};   // internal counter
};

// ── Controller ────────────────────────────────────────────────────────────────

class Controller final : public rcp::Controller {
public:
    explicit Controller(std::shared_ptr<rcp::Controller> inner)
        : inner_(std::move(inner)) {}

    void add_rule(Rule r) {
        std::lock_guard<std::mutex> lk(mu_);
        rules_.push_back(std::move(r));
    }

    void clear_rules() {
        std::lock_guard<std::mutex> lk(mu_);
        rules_.clear();
    }

    Zone zone() const noexcept override { return inner_->zone(); }

    std::error_code send(const rcp::Context& ctx,
                          const Command&      cmd,
                          Response&           out) override {
        if (closed_.load(std::memory_order_acquire)) return ErrClosed;

        Rule* rule = pick_rule();
        if (!rule) return inner_->send(ctx, cmd, out);

        switch (rule->type) {
        case FaultType::Drop:
            return make_error_code(Errc::closed); // injected drop
        case FaultType::Slow:
            std::this_thread::sleep_for(rule->latency);
            return inner_->send(ctx, cmd, out);
        case FaultType::Error:
            out = Response{cmd.id, inner_->zone(), ResponseStatus::Error, {}};
            return {};
        case FaultType::Timeout:
            return ErrTimeout;
        default:
            return inner_->send(ctx, cmd, out);
        }
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
    std::atomic<bool> closed_{false};
    std::mutex mu_;
    std::vector<Rule> rules_;

    // Returns the first active rule (or nullptr) and decrements its count.
    Rule* pick_rule() {
        std::lock_guard<std::mutex> lk(mu_);
        for (auto it = rules_.begin(); it != rules_.end(); ) {
            if (it->count == 0) {
                it = rules_.erase(it);
                continue;
            }
            Rule* r = &(*it);
            ++r->fired;
            if (r->count > 0 && r->fired >= r->count) {
                it = rules_.erase(it);
            }
            return r;
        }
        return nullptr;
    }
};

inline std::shared_ptr<Controller> new_controller(std::shared_ptr<rcp::Controller> inner) {
    return std::make_shared<Controller>(std::move(inner));
}

} // namespace faultinject
} // namespace rcp
