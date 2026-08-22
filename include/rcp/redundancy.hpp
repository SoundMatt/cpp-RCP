// fusa:req REQ-RED-001
// fusa:req REQ-RED-002
// fusa:req REQ-RED-003
// fusa:req REQ-RED-004
// fusa:req REQ-RED-005
// fusa:req REQ-RED-006
// fusa:req REQ-RED-007
// fusa:req REQ-RED-008

// Hot-standby primary/standby failover for ASIL-B fault tolerance (v0.23.0).
//
// RedundantRequestFn holds a primary and a standby rcp::RequestFn (rcp/
// adapt.hpp's client-side send-equivalent call) addressing the same RC
// Server endpoint over two independent paths. All requests go to the
// primary; on ErrClosed or ErrTimeout the active path is promoted to the
// standby automatically and the request is retried.
//
// Rebound (cpp-RCP-FS-03, #86): this was `RedundantController`, an
// `rcp::Controller` decorator keyed by the retired `Zone` (`Zone zone()`
// echoed whichever inner controller happened to be active). That base
// interface and Zone itself are retired (cpp-RCP-FS-01, #84); a redundant
// pair is now identified by whatever the embedding application already
// uses to key its two `RequestFn`s (e.g. a stream_key), not by this header.
#pragma once

#include "rcp.hpp"
#include "adapt.hpp"

#include <memory>
#include <mutex>

namespace rcp {
namespace redundancy {

// ── Config ────────────────────────────────────────────────────────────────────

struct Config {
    bool auto_promote{true};  // promote standby on primary failure without operator confirmation
    int  max_retries {1};     // number of retries on the standby before giving up
};

// ── RedundantRequestFn ────────────────────────────────────────────────────────

class RedundantRequestFn {
public:
    RedundantRequestFn(RequestFn primary, RequestFn standby, Config cfg = {})
        : primary_(std::move(primary))
        , standby_(std::move(standby))
        , cfg_(cfg) {
        active_ = &primary_;
    }

    // active_ points into this object's own primary_/standby_ members, so
    // copying or moving a RedundantRequestFn would leave active_ dangling
    // (or pointing at the wrong instance) — not supported. Always hold one
    // through a std::shared_ptr (see new_redundant() below).
    RedundantRequestFn(const RedundantRequestFn&) = delete;
    RedundantRequestFn& operator=(const RedundantRequestFn&) = delete;
    RedundantRequestFn(RedundantRequestFn&&) = delete;
    RedundantRequestFn& operator=(RedundantRequestFn&&) = delete;

    std::error_code send(const rcp::Context& ctx, const acf::AcfMessageInfo& req,
                          const std::vector<uint8_t>& payload,
                          acf::AcfMessageInfo& out, std::vector<uint8_t>& out_payload) {
        RequestFn* active;
        {
            std::lock_guard<std::mutex> lk(mu_);
            active = active_;
        }
        auto ec = (*active)(ctx, req, payload, out, out_payload);
        if (!ec) return {};

        if (!cfg_.auto_promote) return ec;

        // Promote standby on retriable failure. Pass along the exact
        // RequestFn* this call observed active (captured under the lock
        // above, before the call): if two send() calls race on the same
        // failing active pointer, only the one whose promote_from() runs
        // first actually flips active_; the other's observed pointer no
        // longer matches active_ by the time it acquires the lock, so it is
        // a no-op instead of an unconditional toggle that would flip
        // active_ right back (see promote_from()).
        if (ec == ErrClosed || ec == ErrTimeout) {
            promote_from(active);
            for (int i = 0; i < cfg_.max_retries; ++i) {
                RequestFn* retry_active;
                {
                    std::lock_guard<std::mutex> lk(mu_);
                    retry_active = active_;
                }
                ec = (*retry_active)(ctx, req, payload, out, out_payload);
                if (!ec) return {};
            }
        }
        return ec;
    }

    // operator() lets a RedundantRequestFn itself be handed anywhere an
    // rcp::RequestFn is expected — e.g. straight into rcp::Adapt().
    std::error_code operator()(const rcp::Context& ctx, const acf::AcfMessageInfo& req,
                                const std::vector<uint8_t>& payload,
                                acf::AcfMessageInfo& out, std::vector<uint8_t>& out_payload) {
        return send(ctx, req, payload, out, out_payload);
    }

    // promote manually promotes the standby to active (or back to primary
    // if the standby is already active).
    void promote() {
        std::lock_guard<std::mutex> lk(mu_);
        active_ = (active_ == &primary_) ? &standby_ : &primary_;
    }

    bool is_primary_active() const {
        std::lock_guard<std::mutex> lk(mu_);
        return active_ == &primary_;
    }

private:
    // promote_from is send()'s internal, CAS-style counterpart to the public
    // promote() above: it only flips active_ away from the specific pointer
    // the caller observed failing. If active_ has already moved on (e.g. a
    // concurrent send() on the same observed-failing pointer promoted first),
    // this is a no-op rather than re-toggling — without this guard, two
    // send() calls that both observe the primary failing concurrently would
    // together apply the toggle twice (primary->standby, then straight back
    // standby->primary), silently leaving active_ on the confirmed-bad
    // primary for every later caller (REQ-RED-006).
    void promote_from(RequestFn* observed_active) {
        std::lock_guard<std::mutex> lk(mu_);
        if (active_ == observed_active) {
            active_ = (active_ == &primary_) ? &standby_ : &primary_;
        }
    }

    RequestFn primary_;
    RequestFn standby_;
    RequestFn* active_;
    Config cfg_;
    mutable std::mutex mu_;
};

inline std::shared_ptr<RedundantRequestFn> new_redundant(
        RequestFn primary, RequestFn standby, Config cfg = {}) {
    return std::make_shared<RedundantRequestFn>(std::move(primary), std::move(standby), cfg);
}

} // namespace redundancy
} // namespace rcp
