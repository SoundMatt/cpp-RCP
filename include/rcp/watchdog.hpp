// fusa:req REQ-WDG-001
// fusa:req REQ-WDG-002
// fusa:req REQ-WDG-003
// fusa:req REQ-WDG-004
// fusa:req REQ-WDG-005
// fusa:req REQ-WDG-006
// fusa:req REQ-WDG-007
// fusa:req REQ-WDG-008

// ── Phase 17 c-RCP-reference pass (cpp-RCP issue #129) ───────────────────────
// c-RCP's include/rcp/watchdog.h + src/watchdog.c (this project's RC5-spec-
// conformant reference implementation) is a substantially different design
// from the one below: `rcp_watchdog_keeper_t` owns a background
// re-evaluation thread (default poll_interval_ms = 10, `rcp_watchdog_
// default_config()`), is constructed once from a fixed array of streams
// (`rcp_watchdog_keeper_new(cfg, streams, n_streams)`), and reports state
// purely via a cached `rcp_e2e_wd_result_t` (`rcp_watchdog_keeper_status()`)
// updated by that thread, fired to subscribers on change. That shape is
// itself a *later* c-RCP redesign (issue #338/[c-RCP-17]) of an even
// earlier caller-driven c-RCP watchdog; this codebase's own StreamWatchdog/
// Manager below is a third, independently-engineered design, derived
// directly from the TC18 extraction (§3.8) at v2.10.0 (ROADMAP.md milestone
// 54) rather than transliterated from either c-RCP shape — and already the
// one every other Phase 17 module in this tree depends on (rcp/sim.hpp's
// Simulator::register_stream/poll_watchdog, rcp/mdns.hpp's ServerInfo::
// stream_key convention). Per this rewrite's own stated approach ("re-derive
// ..., translated into idiomatic C++, not transliterated C" — ROADMAP.md
// Phase 17), the audit below evaluates c-RCP's 12 REQ-WDG-* requirements
// (.fusa-reqs.json) against this file's own already-shipped, independently-
// numbered REQ-WDG-001..008 catalog (this project's own .fusa-reqs.json) for
// *behavioral* content gaps, not API-shape gaps:
//
//   - c-RCP REQ-WDG-001/002 (re-run evaluate() at poll interval; elapsed =
//     time since last kick) and REQ-WDG-003/004/005/008 (kick/status/notify/
//     disabled-never-overflows) are behaviorally already covered by this
//     file's own REQ-WDG-001/002/004/005/008 — StreamWatchdog::check/
//     kick_from_request compute the identical "elapsed since last kick vs.
//     rx_wd_timeout_interval, gated on rx_wd_enable" rule, just evaluated
//     on caller-driven poll() rather than a thread's own timer.
//   - c-RCP REQ-WDG-006 (event callback fires on result *change*) does NOT
//     transfer as written: this file's Manager::poll fires subscribers on
//     every HealthEvent poll() *produces* (REQ-WDG-008, below), which is a
//     deliberately different, already-tested edge-triggered contract (an
//     overflow event and a still-latched info-notification are both
//     "produced", not deduplicated against a cached previous result) — c-RCP
//     itself changed on essentially this exact axis across its own
//     watchdog redesigns; no cached rcp_e2e_wd_result_t exists here to
//     compare against for a "changed" test in the first place.
//   - c-RCP REQ-WDG-007 (close() terminates the poll thread, idempotent) and
//     REQ-WDG-011 (default_config poll_interval_ms) do not transfer: this
//     header owns no clock or background thread of its own, same
//     disclaimer as every other endpoint/lifecycle header in this codebase
//     since v2.6.0 (see this file's own header comment below) — there is no
//     thread to close and no poll interval to default.
//   - c-RCP REQ-WDG-009 (initial status computed synchronously before
//     rcp_watchdog_keeper_new() returns, so status() never observes a stale
//     placeholder) does not transfer: this design has no cached "last
//     computed result" at all — check()/poll() compute and return live,
//     nothing to go stale.
//   - c-RCP REQ-WDG-010 (the RC Server's request-reception path shall call
//     kick() on every request received) is a cross-cutting dispatch-layer
//     integration requirement, not watchdog-module behavior; this file's
//     kick_from_request()/Manager::on_request_received() are the hook, and
//     rcp/sim.hpp's Simulator::dispatch already wires it in exactly this
//     shape (see sim.hpp's own header comment).
//   - c-RCP REQ-WDG-012 (destroy(k) is a null-safe no-op, otherwise closes
//     then frees) does not transfer: no manual destroy exists here —
//     StreamWatchdog/Manager are plain value types with ordinary C++
//     destructors, RAII covers this by construction.
//   - c-RCP's [c-RCP-17] fixed-capacity conversion of its stream table and
//     callback list (`RCP_WATCHDOG_MAX_STREAMS`/`RCP_WATCHDOG_MAX_CALLBACKS`,
//     both 16 — watchdog.h) DOES genuinely transfer as a content gap: prior
//     to this pass, Manager below stored streams_/callbacks_ in an unbounded
//     std::map/std::vector, unlike every other Phase 17 table in this
//     codebase (rcp/loan.hpp's kPoolMaxEntries, rcp/respqueue.hpp's
//     kMaxEntries, rcp/request.hpp's kMaxTrackedRequests) — exactly the "no
//     fixed-capacity/no-dynamic-allocation architecture" gap ROADMAP.md's
//     Phase 17 introduction cites as one of the reasons for this whole
//     rewrite. Manager::kMaxStreams/kMaxCallbacks below port c-RCP's own
//     RCP_WATCHDOG_MAX_STREAMS/MAX_CALLBACKS value (16) unchanged, matching
//     loan.hpp's own "match c-RCP's chosen bound exactly, not inventing a
//     stricter or looser one" precedent — see Manager's own doc comment.
//     This is an engineering hardening tracked under issue #129, not a new
//     numbered REQ-WDG-* behavior (c-RCP's own bound isn't tied to a
//     REQ-WDG-* id in its catalog either — see [c-RCP-17]'s own citation
//     style), so no new fusa:req/fusa:test tag is added for it, matching
//     loan.hpp's and respqueue.hpp's own fixed-capacity tests (untagged
//     Catch2 [watchdog] tests only).

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

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <system_error>

namespace rcp {
namespace watchdog {

// ── Errors ────────────────────────────────────────────────────────────────────

enum class WatchdogErrc : int {
    stream_not_registered    = 1, // target stream key was never passed to Manager::register_stream
    // [Phase 17 c-RCP-reference pass, cpp-RCP issue #129] Ported from
    // c-RCP's RCP_WATCHDOG_MAX_STREAMS/MAX_CALLBACKS fixed-capacity bound
    // (watchdog.h) — see Manager::kMaxStreams/kMaxCallbacks's own doc
    // comment below.
    stream_capacity_exceeded = 2, // Manager already tracks kMaxStreams distinct streams
    callback_capacity_exceeded = 3, // Manager already holds kMaxCallbacks subscribers
};

inline const std::error_category& watchdog_category() noexcept {
    struct Cat : std::error_category {
        const char* name() const noexcept override { return "rcp.watchdog"; }
        std::string message(int ev) const override {
            switch (static_cast<WatchdogErrc>(ev)) {
            case WatchdogErrc::stream_not_registered:
                return "rcp/watchdog: stream key was not registered with this Manager";
            case WatchdogErrc::stream_capacity_exceeded:
                return "rcp/watchdog: Manager already tracks its fixed maximum number of streams";
            case WatchdogErrc::callback_capacity_exceeded:
                return "rcp/watchdog: Manager already holds its fixed maximum number of subscribers";
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
    // [Phase 17 c-RCP-reference pass, cpp-RCP issue #129] Ported from
    // c-RCP's include/rcp/watchdog.h RCP_WATCHDOG_MAX_STREAMS/
    // RCP_WATCHDOG_MAX_CALLBACKS (both 16) — c-RCP's own doc comment there
    // rationalizes 16 as "matches e2e.h's own
    // RCP_E2E_STREAM_FAULT_TRACKER_MAX_STREAMS precedent... rather than
    // inventing an unrelated number"; ported unchanged here for the same
    // reason rcp/loan.hpp's kPoolMaxEntries matches c-RCP's own
    // RCP_LOAN_POOL_MAX_ENTRIES exactly rather than this port choosing a
    // stricter or looser bound of its own. Backs the fixed std::array
    // storage below (streams_keys_/streams_, callbacks_), not
    // std::map/std::vector growable without bound — this class previously
    // had no capacity ceiling at all, unlike every other Phase 17 table in
    // this codebase (see this file's own header comment).
    static constexpr size_t kMaxStreams   = 16;
    static constexpr size_t kMaxCallbacks = 16;

    using HealthCallback = std::function<void(const HealthEvent&)>;

    // register_stream begins tracking a stream; a harmless no-op (returns
    // no error) if stream_key is already registered (existing watchdog
    // state, including any latched safe state, is left untouched). Returns
    // stream_capacity_exceeded, unchanged, once kMaxStreams distinct
    // streams are already tracked — see kMaxStreams's own doc comment
    // above; the fixed-capacity table is not silently grown past it,
    // matching c-RCP's own rcp_watchdog_keeper_new()/RCP_WATCHDOG_MAX_STREAMS
    // "rejected, not silently truncated" contract.
    std::error_code register_stream(uint64_t stream_key) {
        if (find_index(stream_key) != kNotFound) return {};
        if (streams_len_ >= kMaxStreams) return make_error_code(WatchdogErrc::stream_capacity_exceeded);
        streams_keys_[streams_len_] = stream_key;
        streams_[streams_len_]      = StreamWatchdog{};
        ++streams_len_;
        return {};
    }

    // unregister_stream stops tracking a stream and discards its state —
    // swap-with-last removal (same O(1) technique rcp/loan.hpp's
    // BufferPool::loan() free-list release already uses): the vacated slot
    // is filled from the table's last live entry, so streams_len_ shrinks
    // by exactly one with no gap. A harmless no-op for an unregistered key.
    void unregister_stream(uint64_t stream_key) noexcept {
        size_t idx = find_index(stream_key);
        if (idx == kNotFound) return;
        size_t last          = streams_len_ - 1;
        streams_keys_[idx]   = streams_keys_[last];
        streams_[idx]        = std::move(streams_[last]);
        --streams_len_;
    }

    bool is_registered(uint64_t stream_key) const noexcept { return find_index(stream_key) != kNotFound; }

    // on_request_received is the driver hook the embedding transport calls
    // once per accepted inbound request — see StreamWatchdog::
    // kick_from_request. Returns stream_not_registered, unchanged, for a
    // key never passed to register_stream.
    std::error_code on_request_received(uint64_t stream_key, uint64_t now_ms) noexcept {
        size_t idx = find_index(stream_key);
        if (idx == kNotFound) return make_error_code(WatchdogErrc::stream_not_registered);
        streams_[idx].kick_from_request(now_ms);
        return {};
    }

    // poll drives StreamWatchdog::check for one registered stream and fans
    // out any resulting HealthEvent to every subscribed callback, in
    // subscription order. Returns stream_not_registered, unchanged, for a
    // key never passed to register_stream; other streams' state is
    // unaffected by that failure.
    std::error_code poll(uint64_t stream_key, const regmap::RequestStreamConfig& cfg,
                          request::RequestLedger& ledger, uint64_t now_ms) {
        size_t idx = find_index(stream_key);
        if (idx == kNotFound) return make_error_code(WatchdogErrc::stream_not_registered);
        auto ev = streams_[idx].check(stream_key, cfg, ledger, now_ms);
        if (ev.has_value()) {
            for (size_t i = 0; i < callbacks_len_; ++i) callbacks_[i](*ev);
        }
        return {};
    }

    bool in_safe_state(uint64_t stream_key) const noexcept {
        size_t idx = find_index(stream_key);
        return idx != kNotFound && streams_[idx].in_safe_state();
    }

    std::error_code clear_safe_state(uint64_t stream_key) noexcept {
        size_t idx = find_index(stream_key);
        if (idx == kNotFound) return make_error_code(WatchdogErrc::stream_not_registered);
        streams_[idx].clear_safe_state();
        return {};
    }

    // subscribe registers a callback fired, in registration order, on
    // every HealthEvent poll() produces for any registered stream. Returns
    // callback_capacity_exceeded, unchanged, once kMaxCallbacks subscribers
    // are already registered — c-RCP's own rcp_watchdog_keeper_subscribe()
    // has the identical bound and the identical "rejected, not silently
    // grown" contract (RCP_WATCHDOG_MAX_CALLBACKS, watchdog.h/.c).
    std::error_code subscribe(HealthCallback cb) {
        if (callbacks_len_ >= kMaxCallbacks) return make_error_code(WatchdogErrc::callback_capacity_exceeded);
        callbacks_[callbacks_len_] = std::move(cb);
        ++callbacks_len_;
        return {};
    }

    // Introspection for tests, not part of the register/subscribe contract
    // itself — always <= kMaxStreams/kMaxCallbacks, by construction.
    size_t stream_count() const noexcept { return streams_len_; }
    size_t callback_count() const noexcept { return callbacks_len_; }

private:
    static constexpr size_t kNotFound = static_cast<size_t>(-1);

    size_t find_index(uint64_t stream_key) const noexcept {
        for (size_t i = 0; i < streams_len_; ++i)
            if (streams_keys_[i] == stream_key) return i;
        return kNotFound;
    }

    std::array<uint64_t, kMaxStreams>       streams_keys_{};
    std::array<StreamWatchdog, kMaxStreams> streams_{};
    size_t                                   streams_len_ = 0; // always <= kMaxStreams

    std::array<HealthCallback, kMaxCallbacks> callbacks_{};
    size_t                                     callbacks_len_ = 0; // always <= kMaxCallbacks
};

} // namespace watchdog
} // namespace rcp

// Enable std::error_code construction from rcp::watchdog::WatchdogErrc.
namespace std {
template <>
struct is_error_code_enum<rcp::watchdog::WatchdogErrc> : true_type {};
} // namespace std
