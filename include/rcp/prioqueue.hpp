// fusa:req REQ-PQ-001
// fusa:req REQ-PQ-002
// fusa:req REQ-PQ-003
// fusa:req REQ-PQ-004
// fusa:req REQ-PQ-005
// fusa:req REQ-PQ-006
// fusa:req REQ-PQ-007
// fusa:req REQ-PQ-008

// Per-zone priority queue that serialises concurrent command senders while
// honouring Priority::Critical > Priority::High > Priority::Normal.
//
// Commands at equal priority are dispatched FIFO. Critical commands always
// pre-empt queued Normal and High commands, ensuring watchdog kicks and
// safety-critical actuation are never head-of-line blocked by lower-priority
// traffic.
//
// A single dispatch thread issues inner send() calls one at a time.
#pragma once

#include "rcp.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace rcp {
namespace prioqueue {

// ── Internal queue entry ──────────────────────────────────────────────────────

struct Entry {
    Context  ctx;
    Command  cmd;
    uint64_t seq = 0;

    struct Result {
        std::error_code ec;
        Response        resp;
    };
    std::shared_ptr<std::promise<Result>> promise; // set by dispatch thread
};

struct EntryOrder {
    bool operator()(const std::shared_ptr<Entry>& a,
                    const std::shared_ptr<Entry>& b) const noexcept {
        // Higher priority wins; equal priority: lower seq first (FIFO).
        if (a->cmd.priority != b->cmd.priority)
            return a->cmd.priority < b->cmd.priority;
        return a->seq > b->seq;
    }
};

// ── Controller ────────────────────────────────────────────────────────────────

// Controller wraps any rcp::Controller and serialises Send calls through a
// priority queue. A single background thread dispatches commands so the
// inner controller never sees concurrent sends.
class Controller final : public rcp::Controller {
public:
    explicit Controller(std::shared_ptr<rcp::Controller> inner)
        : inner_(std::move(inner)) {
        dispatch_thread_ = std::thread([this]{ dispatch(); });
    }

    ~Controller() override {
        closed_.store(true, std::memory_order_release);
        cv_.notify_all();
        if (dispatch_thread_.joinable()) dispatch_thread_.join();
        (void)close();
    }

    Zone zone() const noexcept override { return inner_->zone(); }

    std::error_code send(const rcp::Context& ctx,
                          const Command&      cmd,
                          Response&           out) override {
        if (closed_.load(std::memory_order_acquire))
            return ErrClosed;

        auto e  = std::make_shared<Entry>();
        e->ctx  = ctx;
        e->cmd  = cmd;
        e->seq  = ++seq_;
        e->promise = std::make_shared<std::promise<Entry::Result>>();
        auto future = e->promise->get_future();

        {
            std::lock_guard<std::mutex> lk(mu_);
            queue_.push(std::move(e));
        }
        cv_.notify_one();

        // Wait for dispatch or context expiry.
        if (ctx.deadline()) {
            auto status = future.wait_until(*ctx.deadline());
            if (status == std::future_status::timeout) {
                // Leave the entry in the queue; dispatch will find ctx done.
                return ErrTimeout;
            }
        } else {
            future.wait();
        }

        auto result = future.get();
        if (result.ec) return result.ec;
        out = std::move(result.resp);
        return {};
    }

    std::error_code subscribe(const rcp::Context&             ctx,
                               std::shared_ptr<StatusChannel>& out) override {
        return inner_->subscribe(ctx, out);
    }

    std::error_code close() override {
        closed_.store(true, std::memory_order_release);
        cv_.notify_all();
        return inner_->close();
    }

private:
    std::shared_ptr<rcp::Controller> inner_;
    std::atomic<bool>    closed_{false};
    std::atomic<uint64_t> seq_{0};

    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::priority_queue<std::shared_ptr<Entry>,
                        std::vector<std::shared_ptr<Entry>>,
                        EntryOrder> queue_;
    std::thread dispatch_thread_;

    void dispatch() {
        while (true) {
            std::shared_ptr<Entry> e;
            {
                std::unique_lock<std::mutex> lk(mu_);
                cv_.wait(lk, [&]{ return closed_.load() || !queue_.empty(); });
                if (closed_.load() && queue_.empty()) break;
                if (queue_.empty()) continue;
                e = queue_.top();
                queue_.pop();
            }
            if (e->ctx.done()) {
                e->promise->set_value({ErrTimeout, {}});
                continue;
            }
            Entry::Result r;
            r.ec = inner_->send(e->ctx, e->cmd, r.resp);
            e->promise->set_value(std::move(r));
        }
        // Drain remaining entries with ErrClosed.
        std::unique_lock<std::mutex> lk(mu_);
        while (!queue_.empty()) {
            auto top = queue_.top();
            queue_.pop();
            try { top->promise->set_value({ErrClosed, {}}); } catch (...) {}
        }
    }
};

inline std::shared_ptr<Controller> new_controller(std::shared_ptr<rcp::Controller> inner) {
    return std::make_shared<Controller>(std::move(inner));
}

} // namespace prioqueue
} // namespace rcp
