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
// Interceptor wraps an rcp::RequestFn (rcp/adapt.hpp's client-side send-
// equivalent call) and intercepts each call according to an ordered list of
// Rules. Rules may drop responses, add latency, return errors, or return
// timeouts. Count-based rules auto-expire after N applications.
//
// Rebound (cpp-RCP-FS-03, #86): this was `faultinject::Controller`, an
// `rcp::Controller` decorator over the retired Zone/Command/Response model.
// That base interface is retired (cpp-RCP-FS-01, #84); this header now wraps
// the same `RequestFn` shape rcp/adapt.hpp, rcp/record.hpp, and
// rcp/observe.hpp already standardize on, so it can sit in front of any
// TC18 request path instead of only an in-process rcp::Controller. The
// fault-injected "error" case now flags AcfMessageInfo::err instead of
// returning the retired ResponseStatus::Error; there is no more
// Controller::zone()/subscribe() to preserve, since a plain RequestFn has no
// subscribe() of its own (see rcp/adapt.hpp's own note on subscribe() having
// no analog in the target request/response shape).
#pragma once

#include "acf.hpp"
#include "adapt.hpp"
#include "rcp.hpp"

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

namespace rcp {
namespace faultinject {

// ── FaultType ─────────────────────────────────────────────────────────────────

enum class FaultType : uint8_t {
    Drop    = 1, // return an error without calling the inner RequestFn
    Slow    = 2, // sleep Rule::latency, then call the inner RequestFn
    Error   = 3, // return an AcfMessageInfo with err set, without calling the inner RequestFn
    Timeout = 4, // return ErrTimeout without calling the inner RequestFn
};

// ── Rule ─────────────────────────────────────────────────────────────────────

struct Rule {
    FaultType                 type;
    std::chrono::milliseconds latency{0}; // used by FaultType::Slow
    int                       count{-1};  // -1 = forever; >0 = fires N times
    int                       fired{0};   // internal counter
};

// ── Interceptor ────────────────────────────────────────────────────────────────

class Interceptor {
public:
    explicit Interceptor(RequestFn inner) : inner_(std::move(inner)) {}

    void add_rule(Rule r) {
        std::lock_guard<std::mutex> lk(mu_);
        rules_.push_back(std::move(r));
    }

    void clear_rules() {
        std::lock_guard<std::mutex> lk(mu_);
        rules_.clear();
    }

    // send intercepts one request/response round trip through `inner`
    // according to the active rule (if any), the same call shape
    // rcp::RequestFn/rcp::Adapt() already standardize on.
    std::error_code send(const rcp::Context& ctx, const acf::AcfMessageInfo& req,
                          const std::vector<uint8_t>& payload,
                          acf::AcfMessageInfo& out, std::vector<uint8_t>& out_payload) {
        if (closed_.load(std::memory_order_acquire)) return ErrClosed;

        Rule* rule = pick_rule();
        if (!rule) return inner_(ctx, req, payload, out, out_payload);

        switch (rule->type) {
        case FaultType::Drop:
            return make_error_code(Errc::closed); // injected drop
        case FaultType::Slow:
            std::this_thread::sleep_for(rule->latency);
            return inner_(ctx, req, payload, out, out_payload);
        case FaultType::Error:
            out         = acf::make_response(req, acf::ResponseKind::ErrorResponse);
            out_payload = {};
            return {};
        case FaultType::Timeout:
            return ErrTimeout;
        default:
            return inner_(ctx, req, payload, out, out_payload);
        }
    }

    // operator() lets an Interceptor itself be handed anywhere an
    // rcp::RequestFn is expected — e.g. straight into rcp::Adapt().
    std::error_code operator()(const rcp::Context& ctx, const acf::AcfMessageInfo& req,
                                const std::vector<uint8_t>& payload,
                                acf::AcfMessageInfo& out, std::vector<uint8_t>& out_payload) {
        return send(ctx, req, payload, out, out_payload);
    }

    void close() { closed_.store(true, std::memory_order_release); }

private:
    RequestFn inner_;
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

inline std::shared_ptr<Interceptor> new_interceptor(RequestFn inner) {
    return std::make_shared<Interceptor>(std::move(inner));
}

} // namespace faultinject
} // namespace rcp
