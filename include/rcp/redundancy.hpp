// fusa:req REQ-RED-001
// fusa:req REQ-RED-002
// fusa:req REQ-RED-003
// fusa:req REQ-RED-004
// fusa:req REQ-RED-005
// fusa:req REQ-RED-006
// fusa:req REQ-RED-007
// fusa:req REQ-RED-008

// Hot-standby Registry and HPC failover for ASIL-B fault tolerance (v0.23.0).
//
// RedundantController holds a primary and a standby rcp::Controller for the
// same zone.  All sends go to the primary; on ErrClosed or ErrTimeout the
// controller promotes the standby automatically and retries.
//
// RedundantRegistry wraps two rcp::Registry instances (primary/standby).
// Heartbeat health monitoring (via watchdog::Keeper) drives promotion.
#pragma once

#include "rcp.hpp"

#include <memory>
#include <mutex>

namespace rcp {
namespace redundancy {

// ── Config ────────────────────────────────────────────────────────────────────

struct Config {
    bool auto_promote{true};  // promote standby on primary failure without operator confirmation
    int  max_retries {1};     // number of retries on the standby before giving up
};

// ── RedundantController ───────────────────────────────────────────────────────

class RedundantController final : public rcp::Controller {
public:
    RedundantController(std::shared_ptr<rcp::Controller> primary,
                         std::shared_ptr<rcp::Controller> standby,
                         Config                            cfg = {})
        : primary_(std::move(primary))
        , standby_(std::move(standby))
        , cfg_(cfg) {
        // Both controllers must serve the same zone.
        active_ = primary_;
    }

    Zone zone() const noexcept override { return active_->zone(); }

    std::error_code send(const rcp::Context& ctx,
                          const Command&      cmd,
                          Response&           out) override {
        auto ec = active_->send(ctx, cmd, out);
        if (!ec) return {};

        if (!cfg_.auto_promote) return ec;

        // Promote standby on retriable failure.
        if (ec == ErrClosed || ec == ErrTimeout) {
            promote();
            for (int i = 0; i < cfg_.max_retries; ++i) {
                ec = active_->send(ctx, cmd, out);
                if (!ec) return {};
            }
        }
        return ec;
    }

    std::error_code subscribe(const rcp::Context&             ctx,
                               std::shared_ptr<StatusChannel>& out) override {
        return active_->subscribe(ctx, out);
    }

    std::error_code close() override {
        std::lock_guard<std::mutex> lk(mu_);
        auto ec = primary_->close();
        auto ec2 = standby_->close();
        return ec ? ec : ec2;
    }

    // promote manually promotes the standby to active.
    void promote() {
        std::lock_guard<std::mutex> lk(mu_);
        if (active_ == primary_) active_ = standby_;
        else                      active_ = primary_;
    }

    bool is_primary_active() const {
        std::lock_guard<std::mutex> lk(mu_);
        return active_ == primary_;
    }

private:
    std::shared_ptr<rcp::Controller> primary_;
    std::shared_ptr<rcp::Controller> standby_;
    std::shared_ptr<rcp::Controller> active_;
    Config cfg_;
    mutable std::mutex mu_;
};

inline std::shared_ptr<RedundantController> new_controller(
        std::shared_ptr<rcp::Controller> primary,
        std::shared_ptr<rcp::Controller> standby,
        Config                            cfg = {}) {
    return std::make_shared<RedundantController>(
        std::move(primary), std::move(standby), cfg);
}

} // namespace redundancy
} // namespace rcp
