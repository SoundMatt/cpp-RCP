// fusa:req REQ-PROXY-001
// fusa:req REQ-PROXY-002
// fusa:req REQ-PROXY-003
// fusa:req REQ-PROXY-004
// fusa:req REQ-PROXY-005
// fusa:req REQ-PROXY-006

// Transparent zone proxy for cascaded zonal topologies (v0.22.0).
//
// ProxyController forwards commands through an upstream proxy hop.
// A latency budget is enforced at the proxy boundary: if the budget
// is exceeded the command returns ErrTimeout rather than propagating
// a slow response to the caller.
//
// Route table: zone → upstream Controller
// The ProxyRegistry builds routes from a zone-to-Controller map and
// exposes a standard rcp::Registry interface.
#pragma once

#include "rcp.hpp"

#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <vector>

namespace rcp {
namespace proxy {

// ── Config ────────────────────────────────────────────────────────────────────

struct Config {
    std::chrono::milliseconds latency_budget{50}; // max added hop latency
};

// ── ProxyController ───────────────────────────────────────────────────────────

// ProxyController wraps an upstream Controller and enforces a latency budget.
// If the adjusted context deadline (now + budget) is tighter than the caller's
// original deadline, the tighter deadline is used to prevent budget overrun.
class ProxyController final : public rcp::Controller {
public:
    ProxyController(std::shared_ptr<rcp::Controller> upstream, Config cfg = {})
        : upstream_(std::move(upstream)), cfg_(cfg) {}

    Zone zone() const noexcept override { return upstream_->zone(); }

    std::error_code send(const rcp::Context& ctx,
                          const Command&      cmd,
                          Response&           out) override {
        // Derive a tighter deadline from the budget.
        auto budget_deadline = std::chrono::steady_clock::now() + cfg_.latency_budget;
        Context proxy_ctx;
        if (ctx.deadline()) {
            if (*ctx.deadline() <= budget_deadline)
                proxy_ctx = ctx;
            else
                proxy_ctx = Context::with_deadline(budget_deadline);
        } else {
            proxy_ctx = Context::with_deadline(budget_deadline);
        }
        return upstream_->send(proxy_ctx, cmd, out);
    }

    std::error_code subscribe(const rcp::Context&             ctx,
                               std::shared_ptr<StatusChannel>& out) override {
        return upstream_->subscribe(ctx, out);
    }

    std::error_code close() override { return upstream_->close(); }

private:
    std::shared_ptr<rcp::Controller> upstream_;
    Config cfg_;
};

// ── ProxyRegistry ─────────────────────────────────────────────────────────────

class ProxyRegistry final : public rcp::Registry {
public:
    std::error_code add_route(std::shared_ptr<rcp::Controller> upstream,
                               Config                            cfg = {}) {
        return register_ctrl(
            std::make_shared<ProxyController>(std::move(upstream), cfg));
    }

    std::error_code register_ctrl(std::shared_ptr<rcp::Controller> ctrl) override {
        std::unique_lock<std::shared_mutex> lk(mu_);
        if (closed_) return ErrClosed;
        if (ctrls_.count(ctrl->zone())) return ErrAlreadyExists;
        ctrls_[ctrl->zone()] = std::move(ctrl);
        return {};
    }

    std::error_code deregister(Zone z) override {
        std::unique_lock<std::shared_mutex> lk(mu_);
        auto it = ctrls_.find(z);
        if (it == ctrls_.end()) return ErrNotFound;
        auto c = it->second;
        ctrls_.erase(it);
        lk.unlock();
        return c->close();
    }

    std::error_code lookup(Zone z, std::shared_ptr<rcp::Controller>& out) override {
        std::shared_lock<std::shared_mutex> lk(mu_);
        if (closed_) return ErrClosed;
        auto it = ctrls_.find(z);
        if (it == ctrls_.end()) return ErrNotFound;
        out = it->second;
        return {};
    }

    std::vector<std::shared_ptr<rcp::Controller>> controllers() override {
        std::shared_lock<std::shared_mutex> lk(mu_);
        std::vector<std::shared_ptr<rcp::Controller>> out;
        out.reserve(ctrls_.size());
        for (auto& kv : ctrls_) out.push_back(kv.second);
        return out;
    }

    std::error_code close() override {
        std::unique_lock<std::shared_mutex> lk(mu_);
        if (closed_) return {};
        closed_ = true;
        auto local = std::move(ctrls_);
        lk.unlock();
        for (auto& kv : local) { auto ec = kv.second->close(); (void)ec; }
        return {};
    }

private:
    mutable std::shared_mutex mu_;
    std::map<Zone, std::shared_ptr<rcp::Controller>> ctrls_;
    bool closed_ = false;
};

inline std::unique_ptr<ProxyRegistry> new_registry() {
    return std::make_unique<ProxyRegistry>();
}

} // namespace proxy
} // namespace rcp
