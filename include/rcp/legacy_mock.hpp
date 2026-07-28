// fusa:req REQ-CTRL-001
// fusa:req REQ-CTRL-002
// fusa:req REQ-CTRL-003
// fusa:req REQ-CTRL-004
// fusa:req REQ-CTRL-005
// fusa:req REQ-CTRL-006
// fusa:req REQ-CTRL-007
// fusa:req REQ-CTRL-008
// fusa:req REQ-CTRL-009
// fusa:req REQ-CTRL-010
// fusa:req REQ-CTRL-011
// fusa:req REQ-CTRL-012
// fusa:req REQ-CTRL-013
// fusa:req REQ-CTRL-014
// fusa:req REQ-CTRL-015
// fusa:req REQ-CTRL-016
// fusa:req REQ-CTRL-017
// fusa:req REQ-CTRL-018
// fusa:req REQ-CTRL-019
// fusa:req REQ-CTRL-020
// fusa:req REQ-CTRL-021
// fusa:req REQ-CTRL-022
// fusa:req REQ-CTRL-023
// fusa:req REQ-CTRL-024
// fusa:req REQ-CTRL-025
// fusa:req REQ-CTRL-026
// fusa:req REQ-CTRL-027
// fusa:req REQ-REG-001
// fusa:req REQ-REG-002
// fusa:req REQ-REG-003
// fusa:req REQ-REG-004
// fusa:req REQ-REG-005
// fusa:req REQ-REG-006
// fusa:req REQ-REG-007
// fusa:req REQ-REG-008
// fusa:req REQ-REG-009
// fusa:req REQ-REG-010
// fusa:req REQ-REG-011
// fusa:req REQ-REG-012
// fusa:req REQ-REG-013
// fusa:req REQ-RESP-001
// fusa:req REQ-RESP-002
// fusa:req REQ-STAT-001
// fusa:req REQ-STAT-002
// fusa:req REQ-STAT-003
// fusa:req REQ-STAT-004
// fusa:req REQ-STAT-005
// fusa:req REQ-ERR-011

// In-process controller/registry for the pre-replacement Zone/Command/
// Response/Status model (rcp.hpp), preserved unchanged under this name so
// the still-untouched old-model dependents that build against it keep
// working until each of them is rebound at its own later milestone.
//
// ROADMAP.md milestone 56, "Test & Simulation Harness Rebuild (v2.12.0)":
// this is the pre-replacement content that used to live in rcp/mock.hpp —
// moved here, verbatim in behavior and REQ-CTRL-*/REQ-REG-*/REQ-RESP-*/
// REQ-STAT-*/REQ-ERR-011 identity, only renamed (mock -> legacy_mock, same
// as rcp/wire.hpp's old 16-byte codec became rcp/legacy_wire.hpp at
// v2.0.0) so rcp/mock.hpp itself could become the new RC Server simulator
// this milestone calls for without breaking the roughly three dozen other
// headers/tests still built on rcp.hpp's Zone/Command/Controller/Registry
// model and not yet rebound to the new one: rcp/capi_impl.hpp,
// rcp/cli.hpp, and rcp/config.hpp (all scheduled for v2.16.0 per the
// Satellite Package Disposition table's `capi_impl.hpp`/`cli.hpp`/
// `config.hpp` entries), plus every test file that used the old
// mock::Controller/Registry purely as a generic in-process test double for
// its own not-yet-rebuilt package (firmware/prioqueue/proxy/record/
// redundancy/tsn/zonegroup/federation/observe/faultinject/loan/admin —
// most themselves DEPRECATE/ADAPT candidates for later milestones per that
// same table).
//
// Do not build new functionality on top of this file — it is a
// deliberately temporary bridge, not a second mock implementation going
// forward. See rcp/mock.hpp for the new RC Server simulator.
//
// All operations execute synchronously in memory — no I/O, minimal threading
// (one watcher thread per subscription). The mock is safe for concurrent use.
#pragma once

#include "rcp.hpp"

#include <algorithm>
#include <atomic>
#include <map>
#include <shared_mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace rcp {
namespace legacy_mock {

// Handler is a user-supplied function that produces a Response for a Command.
// If nullptr, the controller returns StatusOK with empty payload.
using Handler = std::function<Response(const Command&)>;

// Controller is a mock zone controller that handles commands in-process.
class Controller final : public rcp::Controller {
public:
    explicit Controller(Zone zone, Handler handler = nullptr)
        : zone_(zone), handler_(std::move(handler)) {}

    ~Controller() override { (void)close(); }

    Zone zone() const noexcept override { return zone_; }

    std::error_code send(const rcp::Context& ctx,
                          const Command&       cmd,
                          Response&            out) override {
        if (closed_.load(std::memory_order_acquire))
            return ErrClosed;
        if (ctx.done())
            return ErrTimeout;
        if (cmd.zone != zone_)
            return ErrZoneMismatch;

        // Copy payload before passing to handler (REQ-CTRL-026).
        Command safe = cmd;
        if (!cmd.payload.empty()) {
            safe.payload = cmd.payload;
        }

        if (handler_) {
            out = handler_(safe);
        } else {
            out = Response{cmd.id, zone_, ResponseStatus::OK, {}};
        }
        return {};
    }

    std::error_code subscribe(const rcp::Context&              ctx,
                               std::shared_ptr<StatusChannel>&  out) override {
        auto ch = std::make_shared<StatusChannel>(16);
        {
            // Check closed_ and append under the same critical section close()
            // uses, so a subscribe() racing a close() cannot observe "open" and
            // then add to subs_ after close() has already cleared it (REQ-CTRL-008;
            // RELAY spec §6.2/6.3/6.6 require a deterministic ErrClosed here, not
            // a transiently-valid channel).
            std::lock_guard<std::mutex> lk(mu_);
            if (closed_.load(std::memory_order_acquire))
                return ErrClosed;
            subs_.push_back(ch);
        }

        // Watcher thread: remove subscription when ctx expires or controller closes.
        std::thread([this, weak_ch = std::weak_ptr<StatusChannel>(ch), ctx]() mutable {
            // Poll until ctx done or channel closed.
            while (!ctx.done()) {
                if (closed_.load(std::memory_order_acquire)) break;
                auto ch_ptr = weak_ch.lock();
                if (!ch_ptr || ch_ptr->is_closed()) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            auto ch_ptr = weak_ch.lock();
            if (!ch_ptr) return;
            {
                std::lock_guard<std::mutex> lk(mu_);
                auto it = std::find(subs_.begin(), subs_.end(), ch_ptr);
                if (it != subs_.end()) subs_.erase(it);
            }
            ch_ptr->close();
        }).detach();

        out = std::move(ch);
        return {};
    }

    // Publish pushes a Status update to all active subscribers.
    void publish(const std::vector<uint8_t>& payload) {
        uint32_t seq = ++seq_;
        // Copy payload so caller mutation cannot affect delivered Status (REQ-CTRL-027).
        std::vector<uint8_t> p = payload;

        Status st{zone_, seq, !closed_.load(std::memory_order_acquire), std::move(p)};
        std::lock_guard<std::mutex> lk(mu_);
        for (auto& s : subs_) {
            s->push(st);
        }
    }

    std::error_code close() override {
        // The closed_ exchange lives under mu_ too, so it is properly serialized
        // against subscribe()'s check-and-append critical section above.
        std::lock_guard<std::mutex> lk(mu_);
        bool was_open = !closed_.exchange(true, std::memory_order_acq_rel);
        if (!was_open) return {};
        for (auto& s : subs_) s->close();
        subs_.clear();
        return {};
    }

private:
    Zone                zone_;
    Handler             handler_;
    std::atomic<bool>   closed_{false};
    std::atomic<uint32_t> seq_{0};
    std::mutex          mu_;
    std::vector<std::shared_ptr<StatusChannel>> subs_;
};

// Registry is an in-process RCP registry backed by mock controllers.
class Registry final : public rcp::Registry {
public:
    Registry() {
        for (auto z : {Zone::FrontLeft, Zone::FrontRight,
                        Zone::RearLeft,  Zone::RearRight,
                        Zone::Central}) {
            ctrls_[z] = std::make_shared<Controller>(z);
        }
    }

    ~Registry() override { (void)close(); }

    std::error_code register_ctrl(std::shared_ptr<rcp::Controller> ctrl) override {
        auto* mc = dynamic_cast<Controller*>(ctrl.get());
        if (!mc) return std::make_error_code(std::errc::invalid_argument);

        std::unique_lock<std::shared_mutex> lk(mu_);
        if (closed_) return ErrClosed;
        if (ctrls_.count(mc->zone())) return ErrAlreadyExists;
        ctrls_[mc->zone()] = std::shared_ptr<Controller>(ctrl, mc);
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
        for (auto& [z, c] : ctrls_) out.push_back(c);
        return out;
    }

    std::error_code close() override {
        std::unique_lock<std::shared_mutex> lk(mu_);
        if (closed_) return {};
        closed_ = true;
        auto local = std::move(ctrls_);
        lk.unlock();
        for (auto& [z, c] : local) (void)c->close();
        return {};
    }

private:
    mutable std::shared_mutex            mu_;
    std::map<Zone, std::shared_ptr<Controller>> ctrls_;
    bool                                 closed_ = false;
};

// Convenience factory — analogous to mock.NewRegistry() in Go.
inline std::unique_ptr<Registry> new_registry() {
    return std::make_unique<Registry>();
}

} // namespace legacy_mock
} // namespace rcp
