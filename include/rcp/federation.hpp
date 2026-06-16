// fusa:req REQ-FED-001
// fusa:req REQ-FED-002
// fusa:req REQ-FED-003
// fusa:req REQ-FED-004
// fusa:req REQ-FED-005
// fusa:req REQ-FED-006
// fusa:req REQ-FED-007
// fusa:req REQ-FED-008

// Multi-HPC federation: cross-HPC zone forwarding with lease-based ownership
// (v0.24.0).
//
// FederatedRegistry is an rcp::Registry whose lookup() transparently forwards
// to a remote HPC when the zone is not locally owned.  Lease ownership is
// time-bounded; an expired lease causes lookup() to return ErrNotFound until
// refreshed by the owning HPC.
#pragma once

#include "rcp.hpp"

#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <vector>

namespace rcp {
namespace federation {

// ── HpcId ─────────────────────────────────────────────────────────────────────

using HpcId = std::string;

// ── Lease ─────────────────────────────────────────────────────────────────────

struct Lease {
    HpcId                                     owner;
    std::chrono::steady_clock::time_point     expires_at;
    std::shared_ptr<rcp::Controller>          remote_ctrl; // forwarding handle

    bool expired() const noexcept {
        return std::chrono::steady_clock::now() >= expires_at;
    }
};

// ── FederatedRegistry ─────────────────────────────────────────────────────────

class FederatedRegistry final : public rcp::Registry {
public:
    // local_id identifies this HPC node.
    explicit FederatedRegistry(HpcId local_id) : local_id_(std::move(local_id)) {}

    // register_ctrl adds a locally-owned zone controller.
    std::error_code register_ctrl(std::shared_ptr<rcp::Controller> ctrl) override {
        std::unique_lock<std::shared_mutex> lk(mu_);
        if (closed_) return ErrClosed;
        if (local_.count(ctrl->zone())) return ErrAlreadyExists;
        local_[ctrl->zone()] = std::move(ctrl);
        return {};
    }

    std::error_code deregister(Zone z) override {
        std::unique_lock<std::shared_mutex> lk(mu_);
        auto it = local_.find(z);
        if (it == local_.end()) return ErrNotFound;
        auto c = it->second;
        local_.erase(it);
        lk.unlock();
        return c->close();
    }

    // add_lease publishes a remote-HPC lease for a zone.
    // The FederatedRegistry will forward commands through the lease's
    // remote_ctrl until the lease expires.
    std::error_code add_lease(Zone zone, Lease lease) {
        std::unique_lock<std::shared_mutex> lk(mu_);
        if (closed_) return ErrClosed;
        leases_[zone] = std::move(lease);
        return {};
    }

    // revoke_lease removes an active lease (called by the owning HPC on shutdown).
    std::error_code revoke_lease(Zone zone) {
        std::unique_lock<std::shared_mutex> lk(mu_);
        leases_.erase(zone);
        return {};
    }

    std::error_code lookup(Zone z, std::shared_ptr<rcp::Controller>& out) override {
        std::shared_lock<std::shared_mutex> lk(mu_);
        if (closed_) return ErrClosed;

        // Prefer local ownership.
        auto lit = local_.find(z);
        if (lit != local_.end()) { out = lit->second; return {}; }

        // Fall through to remote lease.
        auto it = leases_.find(z);
        if (it == leases_.end()) return ErrNotFound;
        if (it->second.expired()) return ErrNotFound;
        out = it->second.remote_ctrl;
        return {};
    }

    std::vector<std::shared_ptr<rcp::Controller>> controllers() override {
        std::shared_lock<std::shared_mutex> lk(mu_);
        std::vector<std::shared_ptr<rcp::Controller>> out;
        out.reserve(local_.size());
        for (auto& kv : local_) out.push_back(kv.second);
        return out;
    }

    std::error_code close() override {
        std::unique_lock<std::shared_mutex> lk(mu_);
        if (closed_) return {};
        closed_ = true;
        auto local  = std::move(local_);
        auto leases = std::move(leases_);
        lk.unlock();
        for (auto& kv : local)  { auto ec = kv.second->close(); (void)ec; }
        for (auto& kv : leases) {
            if (kv.second.remote_ctrl) {
                auto ec = kv.second.remote_ctrl->close(); (void)ec;
            }
        }
        return {};
    }

    const HpcId& local_id() const noexcept { return local_id_; }

private:
    HpcId                                              local_id_;
    mutable std::shared_mutex                          mu_;
    std::map<Zone, std::shared_ptr<rcp::Controller>>  local_;
    std::map<Zone, Lease>                              leases_;
    bool                                               closed_ = false;
};

inline std::unique_ptr<FederatedRegistry> new_registry(HpcId local_id) {
    return std::make_unique<FederatedRegistry>(std::move(local_id));
}

} // namespace federation
} // namespace rcp
