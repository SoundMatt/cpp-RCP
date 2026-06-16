// fusa:req REQ-SHMEM-001
// fusa:req REQ-SHMEM-002
// fusa:req REQ-SHMEM-003
// fusa:req REQ-SHMEM-004
// fusa:req REQ-SHMEM-005
// fusa:req REQ-SHMEM-006
// fusa:req REQ-SHMEM-007
// fusa:req REQ-SHMEM-008

// Zero-copy intra-host command delivery via shared in-process memory.
//
// shmem::Controller and shmem::ZoneServer communicate through buffered
// in-process channels (queue + condition variable), avoiding serialisation
// overhead when both sides run in the same process. This is the default
// "shared memory" implementation for unit testing and single-process deployments.
//
// For true OS shared memory across processes (shm_open/mmap), define
// RCP_SHMEM_POSIX. That path is Linux-only and is planned for v0.8.1+.
#pragma once

#include "rcp.hpp"

#include <algorithm>
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <vector>

namespace rcp {
namespace shmem {

// ── ZoneServer ────────────────────────────────────────────────────────────────

// ZoneServer is the zone-controller side of the in-process transport.
// Create one per zone, then pair it with a Controller.
class ZoneServer {
public:
    using Handler = std::function<Response(const Command&)>;

    explicit ZoneServer(Zone zone) : zone_(zone) {}

    Zone zone() const noexcept { return zone_; }

    void set_handler(Handler h) {
        std::lock_guard<std::mutex> lk(mu_);
        handler_ = std::move(h);
    }

    void set_healthy(bool v) { healthy_.store(v, std::memory_order_release); }

    // publish sends a Status to all active subscribers.
    void publish(const std::vector<uint8_t>& payload) {
        uint32_t seq = ++seq_;
        Status st{zone_, seq, healthy_.load(), payload};
        std::lock_guard<std::mutex> lk(mu_);
        for (auto& s : subs_) s->push(st);
    }

    // dispatch_one processes one pending command (called by Controller).
    // Returns false if the server is closed.
    bool dispatch_one(const Command& cmd, Response& out) {
        if (closed_.load()) return false;
        std::lock_guard<std::mutex> lk(mu_);
        if (handler_) out = handler_(cmd);
        else          out = Response{cmd.id, zone_, ResponseStatus::OK, {}};
        return true;
    }

    // add_sub registers a subscriber channel.
    void add_sub(std::shared_ptr<StatusChannel> ch) {
        std::lock_guard<std::mutex> lk(mu_);
        subs_.push_back(std::move(ch));
    }

    // remove_sub deregisters a subscriber channel.
    void remove_sub(const std::shared_ptr<StatusChannel>& ch) {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = std::find(subs_.begin(), subs_.end(), ch);
        if (it != subs_.end()) subs_.erase(it);
    }

    void close() {
        closed_.store(true, std::memory_order_release);
        std::lock_guard<std::mutex> lk(mu_);
        for (auto& s : subs_) s->close();
        subs_.clear();
    }

    bool ok() const noexcept { return !closed_.load(); }

private:
    Zone  zone_;
    Handler handler_;
    std::atomic<bool>     closed_{false};
    std::atomic<bool>     healthy_{true};
    std::atomic<uint32_t> seq_{0};
    std::mutex mu_;
    std::vector<std::shared_ptr<StatusChannel>> subs_;
};

// ── Controller ────────────────────────────────────────────────────────────────

class Controller final : public rcp::Controller {
public:
    explicit Controller(std::shared_ptr<ZoneServer> server)
        : server_(std::move(server)) {}

    Zone zone() const noexcept override { return server_->zone(); }

    std::error_code send(const rcp::Context& ctx,
                          const Command&      cmd,
                          Response&           out) override {
        if (closed_.load()) return ErrClosed;
        if (ctx.done())     return ErrTimeout;
        if (cmd.zone != server_->zone()) return ErrZoneMismatch;

        // Copy payload (REQ-SHMEM-006: no aliasing).
        Command safe = cmd;
        if (!cmd.payload.empty()) safe.payload = cmd.payload;

        if (!server_->dispatch_one(safe, out)) return ErrClosed;
        return {};
    }

    std::error_code subscribe(const rcp::Context&             ctx,
                               std::shared_ptr<StatusChannel>& out) override {
        if (closed_.load()) return ErrClosed;
        auto ch = std::make_shared<StatusChannel>(16);
        server_->add_sub(ch);
        std::thread([this, weak_ch = std::weak_ptr<StatusChannel>(ch), ctx]() mutable {
            while (!ctx.done() && !closed_.load()) {
                auto c = weak_ch.lock();
                if (!c || c->is_closed()) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            auto c = weak_ch.lock();
            if (!c) return;
            server_->remove_sub(c);
            c->close();
        }).detach();
        out = std::move(ch);
        return {};
    }

    std::error_code close() override {
        closed_.store(true, std::memory_order_release);
        return {};
    }

private:
    std::shared_ptr<ZoneServer> server_;
    std::atomic<bool> closed_{false};
};

// ── Registry ──────────────────────────────────────────────────────────────────

class Registry final : public rcp::Registry {
public:
    std::error_code add_server(std::shared_ptr<ZoneServer> server) {
        return register_ctrl(std::make_shared<Controller>(std::move(server)));
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
        auto ctrl = it->second;
        ctrls_.erase(it);
        lk.unlock();
        return ctrl->close();
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

inline std::unique_ptr<Registry> new_registry() {
    return std::make_unique<Registry>();
}

} // namespace shmem
} // namespace rcp
