// fusa:req REQ-WDG-001
// fusa:req REQ-WDG-002
// fusa:req REQ-WDG-003
// fusa:req REQ-WDG-004
// fusa:req REQ-WDG-005
// fusa:req REQ-WDG-006
// fusa:req REQ-WDG-007
// fusa:req REQ-WDG-008

// Per-request-stream watchdog driver — the embedding-application wiring
// layer that turns "any inbound request accepted on a stream" into the
// OPEN Alliance TC18 Remote Control Protocol Specification v0.5.1_RC's
// actual watchdog reset rule, and surfaces overflow/safe-state transitions
// through this module's own health-event API (extraction §3.8).
//
// ROADMAP.md milestone 54, "Watchdog & Liveness Rebuild (v2.10.0)", opening
// the second half of Phase 14: this header REPLACES this file's
// pre-replacement content in full, per the Satellite Package Disposition
// table's entry for `watchdog.hpp` — the prior client-driven periodic
// `CommandType::Watchdog` kick model built on rcp.hpp's
// `Zone`/`Command`/`Controller` is discarded, not adapted (that command
// type has no analog in the target protocol). Nothing else in this tree
// depended on the old `watchdog::Keeper` API (only this file's own test
// did), so no legacy shim is needed here, same as rcp/e2e.hpp's and
// rcp/powerstate.hpp's equivalent notes at v2.6.0/v2.9.0.
//
// The actual per-stream watchdog timeout/latch primitive already exists as
// rcp/e2e.hpp's `RxWatchdog` (landed at v2.6.0, ahead of the safe-state
// mechanism this file depends on) — this header does not reimplement
// watchdog timeout detection or the purge-normal/retain-safety queue rule;
// it wraps `RxWatchdog`/`apply_watchdog_overflow` as the driver that (a)
// resets ("kicks") a stream's watchdog on every accepted inbound request,
// regardless of that request's kind or safety tag, and (b) reads
// rcp/regmap.hpp's `RequestStreamConfig` for the live enable/timeout/
// safe-state fields on every poll rather than caching a stale copy. This
// header has no clock, thread, or transport of its own — deciding *when*
// a request was "received" (drives kick_from_request) and *when* to poll
// for overflow (drives check()/poll()) is left to the embedding
// application, same disclaimer as every other endpoint/lifecycle header in
// this codebase since v2.6.0.
//
// Field names and behavior below implement TC18's *behavior* as described
// in an internal structured extraction of the specification named above;
// no text from that document is reproduced here. The concrete health-event
// shape and stream-keying scheme (an opaque uint64_t the caller derives,
// typically via avtp::StreamId::to_u64() — see rcp/regmap.hpp's Ep0 class
// comment for the same "opaque index" convention) chosen in this file are
// this implementation's own encoding of that behavior — full bit-for-bit
// conformance against other TC18 implementations is not claimed, same as
// the equivalent disclaimers in rcp/e2e.hpp, rcp/regmap.hpp, and
// rcp/powerstate.hpp.
#pragma once

#include <rcp/e2e.hpp>
#include <rcp/regmap.hpp>
#include <rcp/request.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace rcp {
namespace watchdog {

// ── Errors ────────────────────────────────────────────────────────────────────

enum class WatchdogErrc : int {
    stream_not_registered = 1, // target stream key was never passed to Manager::register_stream
};

inline const std::error_category& watchdog_category() noexcept {
    struct Cat : std::error_category {
        const char* name() const noexcept override { return "rcp.watchdog"; }
        std::string message(int ev) const override {
            switch (static_cast<WatchdogErrc>(ev)) {
            case WatchdogErrc::stream_not_registered:
                return "rcp/watchdog: stream key was not registered with this Manager";
            default:
                return "rcp/watchdog: unknown error";
            }
        }
    };
    static Cat instance;
    return instance;
}

inline std::error_code make_error_code(WatchdogErrc e) noexcept {
    return {static_cast<int>(e), watchdog_category()};
}

// ── HealthEvent ───────────────────────────────────────────────────────────────
// What a caller-supplied HealthCallback receives. `overflowed` and
// `entered_safe_state` are independent: a watchdog can overflow on a stream
// whose rx_wd_safestate_enable is clear, in which case the timeout is
// reported but nothing is latched or purged (extraction §3.8's per-field
// gating — a register existing does not mean the behavior it gates is on).

struct HealthEvent {
    uint64_t stream_key             = 0;
    bool     overflowed             = false; // this poll observed the watchdog timeout expire
    bool     entered_safe_state     = false; // the expiry additionally latched safe state this poll
    bool     info_notification_due  = false; // rx_wd_info_enable's repeating "still safe" notice is due now
    size_t   canceled_request_count = 0;     // normal (non-safety) requests purged by this poll, if any
};

// ── StreamWatchdog ────────────────────────────────────────────────────────────
// The single-stream driver: wraps one e2e::RxWatchdog and drives it from two
// caller-invoked events — an accepted inbound request (kick_from_request)
// and a periodic poll (check) that reads whatever RequestStreamConfig/
// RequestLedger the caller currently has live for this stream. Usable
// directly for a single-stream embedding; Manager below is the
// multi-stream multiplexer most embeddings actually want.
class StreamWatchdog {
public:
    // kick_from_request resets this stream's watchdog. Call this once per
    // request this stream *accepts*, regardless of request kind or
    // is_safety — extraction §3.8's reset rule does not distinguish by
    // request content, only "any inbound request on the stream" (the
    // roadmap's own phrasing for what supersedes the old client-side
    // CommandType::Watchdog poll).
    void kick_from_request(uint64_t now_ms) noexcept { wd_.kick(now_ms); }

    // check evaluates `cfg` against `now_ms`: while not already latched
    // into safe state, an elapsed rx_wd_timeout_interval is reported via
    // HealthEvent::overflowed and, only when cfg.rx_wd_safestate_enable is
    // set, additionally applies apply_watchdog_overflow (latch + purge)
    // against `ledger`. Once latched, further overflow checks are skipped
    // (the latch does not re-fire) and, while cfg.rx_wd_info_enable is set,
    // every poll instead reports the repeating info-notification-due flag
    // — repeat cadence is the caller's own polling interval, same
    // "primitive, not scheduler" split as e2e::RxWatchdog itself. Returns
    // std::nullopt when there is nothing new to report this poll.
    std::optional<HealthEvent> check(uint64_t stream_key, const regmap::RequestStreamConfig& cfg,
                                      request::RequestLedger& ledger, uint64_t now_ms) noexcept {
        if (!wd_.in_safe_state() && wd_.overflowed(cfg, now_ms)) {
            size_t canceled = e2e::apply_watchdog_overflow(cfg, wd_, ledger);
            HealthEvent ev;
            ev.stream_key             = stream_key;
            ev.overflowed             = true;
            ev.entered_safe_state     = wd_.in_safe_state();
            ev.info_notification_due  = wd_.should_emit_info_notification(cfg);
            ev.canceled_request_count = canceled;
            return ev;
        }
        if (wd_.should_emit_info_notification(cfg)) {
            HealthEvent ev;
            ev.stream_key            = stream_key;
            ev.info_notification_due = true;
            return ev;
        }
        return std::nullopt;
    }

    bool in_safe_state() const noexcept { return wd_.in_safe_state(); }

    // clear_safe_state unlatches this stream's watchdog (e.g. on stream
    // reconfiguration) — the same explicit, caller-driven unlatch e2e.hpp's
    // RxWatchdog/RxStreamGuard already require elsewhere in this codebase;
    // kick_from_request alone never clears a latched safe state.
    void clear_safe_state() noexcept { wd_.clear_safe_state(); }

private:
    e2e::RxWatchdog wd_;
};

// ── Manager ───────────────────────────────────────────────────────────────────
// Multiplexes StreamWatchdog across every request stream the embedding
// application registers, keyed by an opaque uint64_t the caller derives
// (see this file's header comment). This is the layer the roadmap calls
// out as the milestone's real job: wiring "any inbound request" receipt
// and periodic polling to e2e.hpp's primitives, per stream, and fanning
// out HealthEvents to subscribers — not a reimplementation of watchdog
// timeout detection itself.
class Manager {
public:
    using HealthCallback = std::function<void(const HealthEvent&)>;

    // register_stream begins tracking a stream; a harmless no-op if
    // stream_key is already registered (existing watchdog state, including
    // any latched safe state, is left untouched).
    void register_stream(uint64_t stream_key) { streams_.try_emplace(stream_key); }

    // unregister_stream stops tracking a stream and discards its state.
    void unregister_stream(uint64_t stream_key) noexcept { streams_.erase(stream_key); }

    bool is_registered(uint64_t stream_key) const noexcept {
        return streams_.find(stream_key) != streams_.end();
    }

    // on_request_received is the driver hook the embedding transport calls
    // once per accepted inbound request — see StreamWatchdog::
    // kick_from_request. Returns stream_not_registered, unchanged, for a
    // key never passed to register_stream.
    std::error_code on_request_received(uint64_t stream_key, uint64_t now_ms) noexcept {
        auto it = streams_.find(stream_key);
        if (it == streams_.end()) return make_error_code(WatchdogErrc::stream_not_registered);
        it->second.kick_from_request(now_ms);
        return {};
    }

    // poll drives StreamWatchdog::check for one registered stream and fans
    // out any resulting HealthEvent to every subscribed callback, in
    // subscription order. Returns stream_not_registered, unchanged, for a
    // key never passed to register_stream; other streams' state is
    // unaffected by that failure.
    std::error_code poll(uint64_t stream_key, const regmap::RequestStreamConfig& cfg,
                          request::RequestLedger& ledger, uint64_t now_ms) {
        auto it = streams_.find(stream_key);
        if (it == streams_.end()) return make_error_code(WatchdogErrc::stream_not_registered);
        auto ev = it->second.check(stream_key, cfg, ledger, now_ms);
        if (ev.has_value()) {
            for (auto& cb : callbacks_) cb(*ev);
        }
        return {};
    }

    bool in_safe_state(uint64_t stream_key) const noexcept {
        auto it = streams_.find(stream_key);
        return it != streams_.end() && it->second.in_safe_state();
    }

    std::error_code clear_safe_state(uint64_t stream_key) noexcept {
        auto it = streams_.find(stream_key);
        if (it == streams_.end()) return make_error_code(WatchdogErrc::stream_not_registered);
        it->second.clear_safe_state();
        return {};
    }

    // subscribe registers a callback fired, in registration order, on
    // every HealthEvent poll() produces for any registered stream.
    void subscribe(HealthCallback cb) { callbacks_.push_back(std::move(cb)); }

private:
    std::map<uint64_t, StreamWatchdog> streams_;
    std::vector<HealthCallback>        callbacks_;
};

} // namespace watchdog
} // namespace rcp

// Enable std::error_code construction from rcp::watchdog::WatchdogErrc.
namespace std {
template <>
struct is_error_code_enum<rcp::watchdog::WatchdogErrc> : true_type {};
} // namespace std
