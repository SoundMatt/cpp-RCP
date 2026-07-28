// fusa:req REQ-DL-001
// fusa:req REQ-DL-002
// fusa:req REQ-DL-003
// fusa:req REQ-DL-004
// fusa:req REQ-DL-005
// fusa:req REQ-DL-006
// fusa:req REQ-DL-007
// fusa:req REQ-DL-008

// RC Server liveness monitor — rebuilt around the response/ack queue's
// periodic flush heartbeat and/or the EP0 lifecycle-state-changed trigger
// signal, since the OPEN Alliance TC18 Remote Control Protocol
// Specification v0.5.1_RC's target model has no `Status`-subscription
// concept to poll (extraction §3.10, §5.1).
//
// ROADMAP.md milestone 54, "Watchdog & Liveness Rebuild (v2.10.0)": per the
// Satellite Package Disposition table's entry for `deadline.hpp` — "ADAPT":
// the *concept* of "declare a monitored target dead once its liveness
// signal has been silent past a configured deadline, alive again the
// moment it resumes" survives from the pre-replacement `Monitor`, but every
// concrete signal source is rebound. The old `Status`-subscription-driven
// `Monitor` (background thread per zone, `ctrl->subscribe`) is discarded in
// full; nothing else in this tree depended on that old API (only this
// file's own test did), so no legacy shim is needed here, same as
// rcp/e2e.hpp's/rcp/powerstate.hpp's/rcp/watchdog.hpp's equivalent notes.
//
// Two independent liveness signals feed this file's Monitor, per the
// roadmap's own "and/or" — either is sufficient evidence a target is
// alive, and this header does not distinguish which one produced a given
// note_*() call:
//   - rcp/regmap.hpp's ResponseQueueConfig::flush_time (added at this
//     milestone — see that header's own comment) is the durable register
//     value behind the response/ack queue's periodic flush heartbeat;
//     note_heartbeat() is the driver hook for observing one.
//   - rcp/lifecycle.hpp's ServerLifecycle::subscribe_state_changed (also
//     added at this milestone) is the EP0 lifecycle-state-changed trigger
//     signal; note_lifecycle_change() is the driver hook for observing one
//     (a caller typically wires ServerLifecycle::subscribe_state_changed
//     directly to note_lifecycle_change).
//
// This header has no clock, thread, or transport of its own — deciding
// *when* a heartbeat/lifecycle-change event occurred (drives note_*()) and
// *when* to evaluate the configured deadline (drives check()) is left to
// the embedding application, same disclaimer as every other header in this
// codebase since v2.6.0. This is a deliberate simplification versus the
// pre-replacement Monitor's own background-thread-per-zone design, not an
// oversight: every other header this rewrite has touched since v2.6.0
// already made the same "primitives, not a scheduler" choice, and there is
// no reason for this file to be the exception.
//
// Field names and behavior below implement TC18's *behavior* as described
// in an internal structured extraction of the specification named above;
// no text from that document is reproduced here. The concrete target-
// keying scheme (an opaque uint64_t the caller derives — see
// rcp/watchdog.hpp's identical convention, itself following
// rcp/regmap.hpp's Ep0 "opaque index" comment) and deadline-tracking shape
// chosen in this file are this implementation's own encoding of that
// behavior — full bit-for-bit conformance against other TC18
// implementations is not claimed, same as the equivalent disclaimers in
// rcp/e2e.hpp, rcp/regmap.hpp, rcp/lifecycle.hpp, and rcp/watchdog.hpp.
#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <vector>

namespace rcp {
namespace deadline {

// ── LivenessEvent ─────────────────────────────────────────────────────────────

struct LivenessEvent {
    uint64_t target = 0;
    bool     alive  = false;
};

// ── Config ────────────────────────────────────────────────────────────────────

struct Config {
    // The specification leaves the concrete liveness deadline to the
    // implementation, same as rcp/powerstate.hpp's wakeup_repeat_limit;
    // this default is this implementation's own choice.
    std::chrono::milliseconds deadline{50};
};

inline Config default_config() { return Config{}; }

// ── LivenessTracker ───────────────────────────────────────────────────────────
// The single-target primitive: records the most recent liveness-signal
// timestamp and reports whether more than Config::deadline has elapsed
// since it, or whether the target has never reported at all (also treated
// as dead, same "never kicked" convention as e2e::RxWatchdog). Usable
// directly for a single monitored target; Monitor below is the
// multi-target multiplexer most embeddings actually want.
class LivenessTracker {
public:
    // note_activity records one liveness signal at now_ms. Called from
    // either note_heartbeat() or note_lifecycle_change() below — this class
    // does not distinguish which source produced a given call, per this
    // file's header comment.
    void note_activity(uint64_t now_ms) noexcept {
        last_seen_ms_    = now_ms;
        has_ever_reported_ = true;
    }

    bool has_ever_reported() const noexcept { return has_ever_reported_; }

    // dead reports true when this target has never reported, or when more
    // than cfg.deadline has elapsed since its last recorded activity.
    bool dead(const Config& cfg, uint64_t now_ms) const noexcept {
        if (!has_ever_reported_) return true;
        return (now_ms - last_seen_ms_) > static_cast<uint64_t>(cfg.deadline.count());
    }

private:
    bool     has_ever_reported_ = false;
    uint64_t last_seen_ms_      = 0;
};

// ── Monitor ───────────────────────────────────────────────────────────────────
// Multiplexes LivenessTracker across every monitored target the embedding
// application registers, keyed by an opaque uint64_t the caller derives
// (see this file's header comment). Fires a LivenessEvent, to every
// subscribed callback, on every alive<->dead transition — including the
// initial dead report for a target that has never sent a liveness signal
// by the time check() first evaluates it — and suppresses repeat events
// for a state that has not changed.
class Monitor {
public:
    using LivenessCallback = std::function<void(const LivenessEvent&)>;

    // register_target begins tracking a target, initially considered dead
    // (has_ever_reported() is false) until the first note_*() call. A
    // harmless no-op if target is already registered.
    void register_target(uint64_t target) { trackers_.try_emplace(target); }

    void unregister_target(uint64_t target) noexcept {
        trackers_.erase(target);
        alive_.erase(target);
    }

    bool is_registered(uint64_t target) const noexcept {
        return trackers_.find(target) != trackers_.end();
    }

    // note_heartbeat records the response/ack queue flush-cadence signal
    // (regmap::ResponseQueueConfig::flush_time) for `target` at `now_ms`.
    // No-op for an unregistered target.
    void note_heartbeat(uint64_t target, uint64_t now_ms) noexcept { note(target, now_ms); }

    // note_lifecycle_change records the EP0 lifecycle-state-changed trigger
    // signal (lifecycle::ServerLifecycle::subscribe_state_changed) for
    // `target` at `now_ms`. No-op for an unregistered target.
    void note_lifecycle_change(uint64_t target, uint64_t now_ms) noexcept { note(target, now_ms); }

    // check evaluates every registered target's deadline at now_ms
    // independently — one target's liveness state never affects another's
    // — and fires a LivenessEvent to every subscribed callback, in
    // subscription order, for each target whose alive/dead state changed
    // since the last check().
    void check(const Config& cfg, uint64_t now_ms) {
        for (auto& kv : trackers_) {
            uint64_t target       = kv.first;
            bool     is_alive     = !kv.second.dead(cfg, now_ms);
            auto     it           = alive_.find(target);
            bool     first_check  = (it == alive_.end());
            bool     was_alive    = !first_check && it->second;
            if (!first_check && is_alive == was_alive) continue; // no change since last check()
            alive_[target] = is_alive;
            LivenessEvent ev{target, is_alive};
            for (auto& cb : callbacks_) cb(ev);
        }
    }

    // alive reports the last state check() observed for target; false for
    // a target never registered or never yet check()ed.
    bool alive(uint64_t target) const noexcept {
        auto it = alive_.find(target);
        return it != alive_.end() && it->second;
    }

    // subscribe registers a callback fired, in registration order, on
    // every LivenessEvent check() produces for any registered target.
    void subscribe(LivenessCallback cb) { callbacks_.push_back(std::move(cb)); }

private:
    std::map<uint64_t, LivenessTracker> trackers_;
    std::map<uint64_t, bool>            alive_;
    std::vector<LivenessCallback>       callbacks_;

    void note(uint64_t target, uint64_t now_ms) noexcept {
        auto it = trackers_.find(target);
        if (it == trackers_.end()) return;
        it->second.note_activity(now_ms);
    }
};

} // namespace deadline
} // namespace rcp
