// fusa:req REQ-LIFECYCLE-001
// fusa:req REQ-LIFECYCLE-002
// fusa:req REQ-LIFECYCLE-003
// fusa:req REQ-LIFECYCLE-004
// fusa:req REQ-LIFECYCLE-005
// fusa:req REQ-LIFECYCLE-006
// fusa:req REQ-LIFECYCLE-007
// fusa:req REQ-LIFECYCLE-013
// fusa:req REQ-LIFECYCLE-014
// fusa:req REQ-LIFECYCLE-015
// fusa:req REQ-LIFECYCLE-016
// fusa:req REQ-LIFECYCLE-017
// fusa:req REQ-LIFECYCLE-018
// fusa:req REQ-LIFECYCLE-019
// fusa:req REQ-LIFECYCLE-020
// fusa:req REQ-LIFECYCLE-021
// fusa:req REQ-LIFECYCLE-022
// fusa:req REQ-LIFECYCLE-024
// fusa:req REQ-LIFECYCLE-025
// fusa:req REQ-LIFECYCLE-026
// fusa:req REQ-LIFECYCLE-027
// fusa:req REQ-LIFECYCLE-028
// fusa:req REQ-LIFECYCLE-029
// fusa:req REQ-LIFECYCLE-030
// fusa:req REQ-LIFECYCLE-031
// fusa:req REQ-LIFECYCLE-032
// fusa:req REQ-LIFECYCLE-033
// fusa:req REQ-LIFECYCLE-034
// fusa:req REQ-LIFECYCLE-035
// fusa:req REQ-LIFECYCLE-036
// fusa:req REQ-LIFECYCLE-037
// fusa:req REQ-LIFECYCLE-038
// fusa:req REQ-LIFECYCLE-039
// fusa:req REQ-RMAP-049
// fusa:req REQ-RMAP-055

// RC Server lifecycle state machine — the 3-state HW_UNCONFIGURED /
// HW_CONFIGURED / RCP_CONFIGURED progression an OPEN Alliance TC18 Remote
// Control Protocol Specification v0.5.1_RC server advances through as it
// gets configured, plus the plausibility checks and register-locking
// behavior tied to that progression (extraction §3.2).
//
// ROADMAP.md milestone 45, "RC Server Lifecycle & Register-Map Model
// (v2.1.0)": this header, together with rcp/regmap.hpp, begins the
// stream/endpoint/register-map model that supersedes rcp/rcp.hpp's
// pre-replacement Zone/Command/Controller/Registry model per the
// Satellite Package Disposition table. rcp.hpp itself is left in place
// for now — roughly three dozen other headers still build against its
// Controller/Registry types and are not rebound until their own later
// milestones (v2.9.0 onward per the Release Plan), so removing it here
// would break the whole tree for no benefit to this milestone's scope.
//
// Field names and behavior below implement TC18's *behavior* as described
// in an internal structured extraction of the specification named above;
// no text from that document is reproduced here. The concrete transition
// rules and locking policy chosen in this file are this implementation's
// own encoding of that behavior — full bit-for-bit register-map
// conformance against other TC18 implementations is a later-milestone
// concern, same as rcp/avtp.hpp's disclaimer for the wire codec.
//
// ROADMAP.md milestone 54, "Watchdog & Liveness Rebuild (v2.10.0)":
// ServerLifecycle::subscribe_state_changed below is a small, explicitly-
// scoped addition for that milestone — rcp/deadline.hpp's liveness monitor
// needs an "EP0 lifecycle-state-changed" trigger signal to treat as
// evidence of RC Server liveness (there being no Status-subscription
// concept left to poll in the new model), and no such signal existed
// anywhere in this header prior to v2.10.0. It is purely additive: every
// pre-existing transition rule and locking policy above is unchanged.
//
// ── Phase 2 content-parity pass (cpp-RCP issue #129) ──────────────────────
// Re-derived against c-RCP's `include/rcp/lifecycle.h`/`src/lifecycle.c` —
// the RC5-spec-conformant source of truth — which has grown a whole access-
// control layer (writer authorization, unicast enforcement, idle-gating;
// c-RCP's own issue #198) this file never had at all: only the bare 3-state
// machine (advance()/deconfigure()) and a coarse generic-vs-functional
// config lock existed here before this pass. Added, all purely additive
// (every pre-existing member above is unchanged in signature and
// behavior): EndpointPlausibility/RequestStreamPlausibility/
// PlausibilitySnapshot + check_hw_cfg()/check_rcp_cfg() (the actual
// plausibility-check content advance()'s own PlausibilityCheck callback
// hook never modeled), WriterCtx, ServerLifecycle::transition() (the full
// writer/idle-gated state machine, coexisting with advance() — see that
// method's own doc comment for why both are kept), Disposition/
// should_accept() (per-state request filtering), and FieldKind/
// field_writable()/field_write_error()/field_writable_w_plus()/
// field_write_error_w_plus() (register-locking-by-state, superseding
// generic_config_locked()/functional_config_locked()'s coarse boolean with
// c-RCP's real four-kind/writer-aware model — those two coarse queries stay
// in place unchanged for their existing callers, e.g. rcp/regmap.hpp's
// Ep0). See this repository's PR description for the full delta list.
#pragma once

#include <rcp/acf.hpp>
#include <rcp/avtp.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>
#include <system_error>

namespace rcp {
namespace lifecycle {

// ── ServerState ───────────────────────────────────────────────────────────────
// The three states an RC Server progresses through, in order, as hardware
// and then protocol-level configuration is applied. Values match the
// extraction's own numeric encoding (extraction §3.2) so they can be read
// or written directly as a register byte.

enum class ServerState : uint8_t {
    HwUnconfigured = 0x00, // reset/default state; no configuration accepted yet
    HwConfigured   = 0x55, // HW pin-map / generic endpoint config has been accepted
    RcpConfigured  = 0xAA, // functional config has been accepted; server is fully live
};

inline std::string to_string(ServerState s) {
    switch (s) {
    case ServerState::HwUnconfigured: return "HW_UNCONFIGURED";
    case ServerState::HwConfigured:   return "HW_CONFIGURED";
    case ServerState::RcpConfigured:  return "RCP_CONFIGURED";
    default:                          return "unknown";
    }
}

// ── Errors ────────────────────────────────────────────────────────────────────

enum class LifecycleErrc : int {
    invalid_transition   = 1, // advance() target is not the single next state in sequence
    hw_cfg_inconsistent   = 2, // HW_UNCONFIGURED -> HW_CONFIGURED plausibility check failed
    rcp_cfg_inconsistent  = 3, // HW_CONFIGURED -> RCP_CONFIGURED plausibility check failed
    // Added during the Phase 2 content-parity pass (cpp-RCP issue #129),
    // ported from c-RCP's rcp_lifecycle_errc_t (RCP_LIFECYCLE_ERR_UNAUTHORIZED/
    // _EPS_NOT_IDLE) — behavior ServerLifecycle::transition() below needed
    // and advance() never modeled at all: writer-authorization gating
    // (REQ-LIFECYCLE-031) and idleness gating (REQ-LIFECYCLE-022).
    unauthorized          = 4, // transition(): writer not authorized for this svr_lifecycle_state change
    eps_not_idle          = 5, // transition(): another endpoint still has an in-flight/queued request
                                 // (TC18 Figure 17's own diagram-only "EPs_NOT_IDLE" outcome, which maps
                                 // to none of TC18's seventeen numbered wire error codes — a genuine
                                 // spec inconsistency this library cannot resolve by inventing a mapping,
                                 // so this stays a local-only error code, mirroring c-RCP's own
                                 // RCP_LIFECYCLE_ERR_EPS_NOT_IDLE doc comment)
};

inline const std::error_category& lifecycle_category() noexcept {
    struct Cat : std::error_category {
        const char* name() const noexcept override { return "rcp.lifecycle"; }
        std::string message(int ev) const override {
            switch (static_cast<LifecycleErrc>(ev)) {
            case LifecycleErrc::invalid_transition:
                return "rcp/lifecycle: requested state is not the next state in sequence";
            case LifecycleErrc::hw_cfg_inconsistent:
                return "rcp/lifecycle: HW_CFG_INCONSISTENT — hardware configuration failed its plausibility check";
            case LifecycleErrc::rcp_cfg_inconsistent:
                return "rcp/lifecycle: RCP_CFG_INCONSISTENT — protocol configuration failed its plausibility check";
            case LifecycleErrc::unauthorized:
                return "rcp/lifecycle: writer not authorized for this svr_lifecycle_state change";
            case LifecycleErrc::eps_not_idle:
                return "rcp/lifecycle: EPS_NOT_IDLE — another endpoint is not idle";
            default:
                return "rcp/lifecycle: unknown error";
            }
        }
    };
    static Cat instance;
    return instance;
}

inline std::error_code make_error_code(LifecycleErrc e) noexcept {
    return {static_cast<int>(e), lifecycle_category()};
}

// ── Plausibility snapshot (transition-guard input) ────────────────────────────
// Added during the Phase 2 content-parity pass (cpp-RCP issue #129), ported
// from c-RCP's rcp_lifecycle_plausibility_snapshot_t/rcp_lifecycle_check_hw_cfg()/
// _check_rcp_cfg() — the RC5-spec-conformant source of truth's own encoding
// of TC18 §12.3.1.2's HW_CFG_INCONSISTENT/RCP_CFG_INCONSISTENT plausibility
// rules. This header's own pre-existing PlausibilityCheck (an opaque
// std::function<bool()>) let an embedding application supply *some* verdict
// but never itself modeled what "plausible" means — the checks below are
// that missing content, exposed as free functions (check_hw_cfg()/
// check_rcp_cfg()) an embedding application MAY use to build its own
// PlausibilityCheck lambda from a real snapshot, or MAY drive directly via
// ServerLifecycle::transition() below (which calls them itself). A minimal,
// self-contained stand-in surface — deliberately not reaching into
// rcp/regmap.hpp's real RegisterMap tables, matching c-RCP's own layering
// choice for the identical struct (see lifecycle.h's own file header:
// "This module only needs a minimal, self-contained stand-in surface...
// sufficient to make its own transition guards and per-state filtering
// testable").

// One endpoint's configuration state, as far as the two plausibility checks
// below need to see it.
struct EndpointPlausibility {
    bool   ep_used            = false; // this endpoint slot is in use
    bool   hw_pin_mapped      = false; // a valid HW pin mapping is present
    bool   has_request_stream = false; // at least one configured request stream exists for this endpoint
    bool   has_stream_assoc   = false; // a stream/byte_bus_id association exists for this endpoint
    size_t request_stream_index = 0;   // which snap.request_streams[] slot this endpoint's own
                                        // has_stream_assoc refers to — meaningless while
                                        // has_stream_assoc is false (REQ-LIFECYCLE-038)
};

// One request stream's configuration state, as far as the RCP_CFG_INCONSISTENT
// guard needs to see it.
struct RequestStreamPlausibility {
    bool   configured          = false; // this request stream slot is configured
    bool   has_response_stream = false; // an associated response stream exists
    size_t response_stream_index = 0;   // which snap.response_stream_count-space slot this
                                         // stream's own has_response_stream refers to —
                                         // meaningless while has_response_stream is false
                                         // (REQ-RMAP-049); 0-based, the same translation
                                         // convention request_stream_index above establishes
};

// A read-only view over every endpoint and request stream slot, passed to
// the plausibility checks and to ServerLifecycle::transition(). Unlike
// c-RCP's own rcp_lifecycle_plausibility_snapshot_t (a raw pointer+count
// pair, with a documented fail-safe "NULL snapshot is treated as
// inconsistent" rule), this is a plain value type — a default-constructed,
// empty PlausibilitySnapshot has no endpoints/request streams at all, which
// both check_hw_cfg()/check_rcp_cfg() below correctly read as vacuously
// consistent (an empty configuration has nothing to be inconsistent about),
// the same as passing a non-NULL-but-empty snap to c-RCP's own checks would.
// A C++ reference type has no analogue of C's "caller passed a null
// pointer" failure mode to guard against, so this port does not carry that
// specific fail-safe check forward — there is nothing for it to catch here.
struct PlausibilitySnapshot {
    std::vector<EndpointPlausibility>      endpoints;
    std::vector<RequestStreamPlausibility> request_streams;
    size_t                                 response_stream_count = 0; // REQ-RMAP-049: how many
                                                                        // real response/ack-queue
                                                                        // slots exist
};

namespace detail {

// Bullet 2/3 of check_rcp_cfg()'s own TC18 §12.3.1.2 check: every configured
// request stream names a real response stream slot, and every configured
// request stream is bound to at least one in-use endpoint (not an orphaned,
// unused stream slot). Split out purely to mirror c-RCP's own
// request_streams_consistent() structure — no behavior difference.
inline bool request_streams_consistent(const PlausibilitySnapshot& snap) noexcept {
    for (size_t i = 0; i < snap.request_streams.size(); ++i) {
        const RequestStreamPlausibility& rs = snap.request_streams[i];
        if (!rs.configured) continue;
        if (!rs.has_response_stream) return false;

        // REQ-RMAP-049 (c-RCP issue #338): has_response_stream alone only
        // proves SOME association was recorded, not that it names a
        // response stream that actually exists.
        if (rs.response_stream_index >= snap.response_stream_count) return false;

        // REQ-LIFECYCLE-038: TC18 §12.3.1.2's third bullet — a configured
        // stream with no endpoint referencing it (an orphaned, unused
        // stream slot) is also inconsistent. ep_used is checked here too,
        // not just has_stream_assoc — an unused endpoint slot is skipped by
        // check_rcp_cfg()'s own bullet-1 loop entirely, so its own
        // has_stream_assoc/request_stream_index values are never validated
        // by anything and must not be trusted to "cover" an otherwise-
        // orphaned stream here; only a genuinely in-use endpoint counts.
        bool has_bound_endpoint = false;
        for (const EndpointPlausibility& ep : snap.endpoints) {
            if (ep.ep_used && ep.has_stream_assoc && ep.request_stream_index == i) {
                has_bound_endpoint = true;
                break;
            }
        }
        if (!has_bound_endpoint) return false;
    }
    return true;
}

} // namespace detail

// The HW_CFG_INCONSISTENT plausibility check: OK iff every endpoint with
// ep_used set has both hw_pin_mapped and has_request_stream set. Endpoints
// with ep_used == false are ignored.
inline std::error_code check_hw_cfg(const PlausibilitySnapshot& snap) noexcept {
    for (const EndpointPlausibility& ep : snap.endpoints) {
        if (!ep.ep_used) continue;
        if (!ep.hw_pin_mapped)      return make_error_code(LifecycleErrc::hw_cfg_inconsistent);
        if (!ep.has_request_stream) return make_error_code(LifecycleErrc::hw_cfg_inconsistent);
    }
    return {};
}

// The RCP_CFG_INCONSISTENT plausibility check: OK iff (1) every endpoint
// with ep_used set has has_stream_assoc set, (2) every request stream with
// configured set has has_response_stream set AND names a real response
// stream slot (REQ-RMAP-049), and (3) every request stream with configured
// set is referenced by at least one endpoint's own request_stream_index
// (REQ-LIFECYCLE-038).
inline std::error_code check_rcp_cfg(const PlausibilitySnapshot& snap) noexcept {
    for (const EndpointPlausibility& ep : snap.endpoints) {
        if (!ep.ep_used) continue;
        if (!ep.has_stream_assoc) return make_error_code(LifecycleErrc::rcp_cfg_inconsistent);
    }
    if (!detail::request_streams_consistent(snap)) return make_error_code(LifecycleErrc::rcp_cfg_inconsistent);
    return {};
}

// ── WriterCtx ─────────────────────────────────────────────────────────────────
// Added during the Phase 2 content-parity pass, ported from c-RCP's
// rcp_lifecycle_writer_ctx_t — identifies who is attempting a write (a
// functional-config write, see field_writable() below, or a
// svr_lifecycle_state change, see ServerLifecycle::transition()).
// Any combination of members may be true; only one authorizing condition
// needs to be true for a given call's own authorization rule to be
// satisfied. Every member defaults to false, so a partial/aggregate
// initializer setting only the members a particular test cares about is
// exactly as safe as it is in c-RCP's own {0}/partial-brace convention.
struct WriterCtx {
    bool via_root_client_ep0        = false; // request arrived via EP0 from the root client
    bool via_owning_stream          = false; // request arrived via the endpoint's own registered request stream
    bool via_non_unicast_frame      = false; // the request frame's destination MAC was multicast/broadcast,
                                              // not unicast (REQ-LIFECYCLE-027)
    bool via_discovery_stream       = false; // request arrived via the discovery stream
                                              // (REQ-LIFECYCLE-030/031/036)
    bool via_valid_stream_association = false; // request arrived via a stream_id/byte_bus_id
                                              // combination that is a real, currently-configured
                                              // EP_ID_config association — ONLY ever true when no
                                              // root client is configured at all (TC18 §12.3.1.2);
                                              // the caller deriving this member bakes that "no root
                                              // client configured" condition in at construction, the
                                              // same pattern via_root_client_ep0 already uses for its
                                              // own root-client-index check
};

// ── ServerLifecycle ───────────────────────────────────────────────────────────
// PlausibilityCheck lets the embedding application supply its own notion of
// "is the configuration applied so far internally consistent" without this
// header needing to know concrete register layouts — the register-map types
// that would typically back such a check live in rcp/regmap.hpp, one layer
// above this one.
using PlausibilityCheck = std::function<bool()>;

// StateChangedCallback is ROADMAP.md milestone 54's (v2.10.0)
// lifecycle-state-changed trigger signal: fired with (previous, current)
// exactly once for every ServerLifecycle-driven transition that actually
// changes state() — see subscribe_state_changed below.
using StateChangedCallback = std::function<void(ServerState previous, ServerState current)>;

// ServerLifecycle owns the current ServerState and enforces:
//   - forward-only, single-step transitions via advance() (extraction §3.2:
//     a state-advance request always targets the next state in sequence;
//     this implementation treats a request for any other state, including a
//     repeat of the current one, as an invalid transition rather than a
//     silent no-op, since a client asking to "advance" into its own current
//     state most likely indicates a client-side bug worth surfacing);
//   - the HW_CFG_INCONSISTENT / RCP_CFG_INCONSISTENT plausibility checks
//     that gate each forward transition;
//   - register-locking: generic (server-owned, pin-mapping/queue-size)
//     config is locked from HW_CONFIGURED onward, and functional config is
//     additionally locked once RCP_CONFIGURED — modeling the general
//     principle that configuration a later stage depends on should not
//     change out from under it (this locking policy is this
//     implementation's own design choice for how to realize "register-
//     locking behavior", not a verbatim rule copied from the specification).
class ServerLifecycle {
public:
    explicit ServerLifecycle(PlausibilityCheck hw_cfg_check  = {},
                              PlausibilityCheck rcp_cfg_check = {}) noexcept
        : hw_cfg_check_(std::move(hw_cfg_check)),
          rcp_cfg_check_(std::move(rcp_cfg_check)) {}

    ServerState state() const noexcept { return state_; }

    // advance requests a transition to `target`. Only the single next state
    // in the HwUnconfigured -> HwConfigured -> RcpConfigured sequence is
    // accepted; anything else (skipping a state, repeating the current one,
    // or moving backward) returns invalid_transition. Going backward is only
    // possible through the explicit deconfigure() call below.
    std::error_code advance(ServerState target) noexcept {
        if (state_ == ServerState::HwUnconfigured && target == ServerState::HwConfigured) {
            if (hw_cfg_check_ && !hw_cfg_check_())
                return make_error_code(LifecycleErrc::hw_cfg_inconsistent);
            ServerState previous = state_;
            state_ = ServerState::HwConfigured;
            notify_state_changed(previous, state_);
            return {};
        }
        if (state_ == ServerState::HwConfigured && target == ServerState::RcpConfigured) {
            if (rcp_cfg_check_ && !rcp_cfg_check_())
                return make_error_code(LifecycleErrc::rcp_cfg_inconsistent);
            ServerState previous = state_;
            state_ = ServerState::RcpConfigured;
            notify_state_changed(previous, state_);
            return {};
        }
        return make_error_code(LifecycleErrc::invalid_transition);
    }

    // deconfigure is the one sanctioned backward path: an explicit reset to
    // HW_UNCONFIGURED (e.g. on a hardware reset or an operator-triggered
    // reconfiguration), unlocking every register block again.
    void deconfigure() noexcept {
        ServerState previous = state_;
        state_ = ServerState::HwUnconfigured;
        notify_state_changed(previous, state_);
    }

    // Register-locking queries (extraction §3.2's locking behavior, this
    // implementation's own realization of it — see class comment above).
    bool generic_config_locked() const noexcept {
        return state_ == ServerState::HwConfigured || state_ == ServerState::RcpConfigured;
    }
    bool functional_config_locked() const noexcept {
        return state_ == ServerState::RcpConfigured;
    }

    // subscribe_state_changed registers a callback fired, in registration
    // order, every time a call above actually changes state() (extraction
    // §5.1's EP0 pseudo-endpoint is the natural place a client would learn
    // of such a transition; this class only models the trigger signal
    // itself, not any wire-level notification of it — that is the
    // embedding application's/rcp/regmap.hpp's concern, same "primitive,
    // not a scheduler or transport" split as every other header in this
    // codebase). A call to advance()/deconfigure() that fails, or that
    // targets the state already current (deconfigure() from
    // HwUnconfigured), fires no callback — see notify_state_changed.
    void subscribe_state_changed(StateChangedCallback cb) {
        state_changed_callbacks_.push_back(std::move(cb));
    }

    // ── transition — the fully writer/idle-gated state machine ─────────────
    // Added during the Phase 2 content-parity pass (cpp-RCP issue #129),
    // ported field-for-field from c-RCP's rcp_lifecycle_transition() — this
    // is genuinely new behavior relative to advance() above, not a
    // replacement for it (see the note below on why both coexist). Where
    // advance() only ever knows about the two forward steps
    // (HW_UNCONFIGURED -> HW_CONFIGURED -> RCP_CONFIGURED), transition()
    // implements TC18 Figure 17's FULL topology: writer authorization
    // (REQ-LIFECYCLE-031), idleness gating (REQ-LIFECYCLE-022), and three
    // transitions advance()/deconfigure() cannot express at all — the
    // RCP_CONFIGURED -> HW_CONFIGURED demotion, and the two authorized
    // resets to HW_UNCONFIGURED (HW_CONFIGURED -> HW_UNCONFIGURED and
    // RCP_CONFIGURED -> HW_UNCONFIGURED) that differ from deconfigure()'s
    // own unconditional reset by actually checking who is asking and
    // whether every other endpoint is idle first.
    //
    //   - HW_UNCONFIGURED -> HW_CONFIGURED: guarded by check_hw_cfg(snap); a
    //     plausibility failure returns hw_cfg_inconsistent. `writer` is not
    //     consulted for this transition — TC18 §12.3.1.1 requires only that
    //     the request travel via the discovery stream, which this library
    //     has no should_accept()-equivalent gate positioned ahead of this
    //     call to enforce on this port's behalf (no root client can exist
    //     yet at this point in bring-up regardless). NOT idle-gated.
    //   - HW_CONFIGURED -> RCP_CONFIGURED: guarded first by writer
    //     authorization (via_discovery_stream || via_root_client_ep0 ||
    //     via_valid_stream_association), then by check_rcp_cfg(snap); a
    //     plausibility failure returns rcp_cfg_inconsistent. NOT idle-gated.
    //   - RCP_CONFIGURED -> HW_CONFIGURED (a demotion): guarded by a
    //     narrower writer authorization — only via_root_client_ep0 or
    //     via_valid_stream_association; via_discovery_stream is deliberately
    //     NOT accepted here (TC18 §12.7.4: discovery-stream authorization no
    //     longer suffices for a configuration change made once already
    //     RCP_CONFIGURED) — and by idleness; no plausibility recheck.
    //   - HW_CONFIGURED -> HW_UNCONFIGURED (a reset): guarded by the same
    //     writer authorization as the advance above, and by idleness; snap
    //     is not consulted once authorized and idle.
    //   - RCP_CONFIGURED -> HW_UNCONFIGURED (a reset): guarded by
    //     via_root_client_ep0 ALONE (REQ-LIFECYCLE-037, TC18 §12.7.4 — the
    //     narrowest authorization of any transition here), and by idleness.
    //   - target == state(): always a no-op success — writer, snap, and
    //     all_other_eps_idle are all ignored. This is the one point where
    //     transition()'s own semantics genuinely differ from advance()'s
    //     documented "repeat of the current state is a client bug, report
    //     invalid_transition" design choice above; both are kept, on
    //     purpose (see the note below), rather than collapsing to one
    //     shared implementation with one shared, necessarily-compromised
    //     same-state rule.
    //   - Anything else (in particular skipping HW_CONFIGURED entirely,
    //     HW_UNCONFIGURED -> RCP_CONFIGURED) is invalid_transition,
    //     regardless of writer or idleness.
    //
    // Why advance() and transition() both exist, rather than folding one
    // into the other: advance()'s own same-state-is-an-error rule is this
    // implementation's own pre-existing, explicitly documented design
    // choice (see advance()'s own doc comment above) that rcp/mock.hpp and
    // this file's own pre-existing tests already depend on; c-RCP's
    // same-state-is-a-no-op rule is the RC5-spec-conformant behavior a
    // writer/idle-aware caller (a future register-map dispatch layer, e.g.
    // rcp/regmap.hpp's Ep0) needs instead. Rather than silently changing
    // advance()'s own long-standing behavior out from under its existing
    // callers (a real, if narrow, behavioral regression this pass declines
    // to introduce), transition() is added alongside it as the richer,
    // spec-complete entry point — a caller that wants Figure 17's full
    // topology uses transition(); a caller that only ever needs the two
    // forward steps keeps using advance() unchanged. Wiring transition()
    // into rcp/regmap.hpp's Ep0/an actual register-map dispatch path is
    // left to a later phase, same "primitive, not a scheduler or
    // transport" split as everywhere else in this codebase — this method
    // only updates state_ and fires the state-changed signal, exactly like
    // advance()/deconfigure() already do.
    std::error_code transition(ServerState target, const PlausibilitySnapshot& snap, WriterCtx writer,
                                bool all_other_eps_idle) noexcept {
        const ServerState from = state_;
        // TC18 §12.3.1.2 (REQ-LIFECYCLE-031): a svr_lifecycle_state write is
        // accepted via the discovery stream, the root client, or any other
        // currently-valid stream_id/byte_bus_id association when no root
        // client is configured at all (writer.via_valid_stream_association
        // already bakes that "no root client configured" narrowing in at
        // its own construction site).
        const bool authorized =
            writer.via_discovery_stream || writer.via_root_client_ep0 || writer.via_valid_stream_association;

        if (target == from) return {}; // no-op; writer/idleness/snap not consulted

        if (from == ServerState::HwUnconfigured && target == ServerState::HwConfigured) {
            auto ec = check_hw_cfg(snap);
            if (ec) return ec;
            return commit(target);
        }

        if (from == ServerState::HwConfigured && target == ServerState::RcpConfigured) {
            if (!authorized) return make_error_code(LifecycleErrc::unauthorized);
            auto ec = check_rcp_cfg(snap);
            if (ec) return ec;
            return commit(target);
        }

        // TC18 Figure 17's own explicit RCP_CONFIGURED -> HW_CONFIGURED
        // arrow ("Root Client or (stream/bb_ID & no root configured) access
        // via EP0 ... & all other EPs are Idle"): narrower authorization
        // than `authorized` above — via_discovery_stream is deliberately
        // NOT accepted (REQ-LIFECYCLE-037). A demotion does not re-verify
        // plausibility.
        if (from == ServerState::RcpConfigured && target == ServerState::HwConfigured) {
            if (!writer.via_root_client_ep0 && !writer.via_valid_stream_association)
                return make_error_code(LifecycleErrc::unauthorized);
            if (!all_other_eps_idle) return make_error_code(LifecycleErrc::eps_not_idle);
            return commit(target);
        }

        // The discovery-stream/root-client reset path — from either
        // configured state — is unconditional once authorized and idle;
        // snap is not consulted for a reset.
        if (target == ServerState::HwUnconfigured && from == ServerState::HwConfigured) {
            if (!authorized) return make_error_code(LifecycleErrc::unauthorized);
            if (!all_other_eps_idle) return make_error_code(LifecycleErrc::eps_not_idle);
            return commit(target);
        }

        // REQ-LIFECYCLE-037 (TC18 §12.7.4): "Changes in configuration via a
        // discovery request are no longer allowed" once RCP_CONFIGURED —
        // only via_root_client_ep0 authorizes this specific reset.
        if (target == ServerState::HwUnconfigured && from == ServerState::RcpConfigured) {
            if (!writer.via_root_client_ep0) return make_error_code(LifecycleErrc::unauthorized);
            if (!all_other_eps_idle) return make_error_code(LifecycleErrc::eps_not_idle);
            return commit(target);
        }

        // Everything else — e.g. skipping a state entirely on the way up
        // (HW_UNCONFIGURED -> RCP_CONFIGURED directly) — is not a modeled
        // transition, regardless of writer or idleness.
        return make_error_code(LifecycleErrc::invalid_transition);
    }

private:
    ServerState        state_ = ServerState::HwUnconfigured;
    PlausibilityCheck  hw_cfg_check_;
    PlausibilityCheck  rcp_cfg_check_;
    std::vector<StateChangedCallback> state_changed_callbacks_;

    // notify_state_changed fires every subscribed callback exactly when
    // `previous` and `current` actually differ — a no-change call (e.g.
    // deconfigure() from an already-HwUnconfigured state) is deliberately
    // silent, since this signal exists to mean "state changed", not
    // "a transition was attempted".
    void notify_state_changed(ServerState previous, ServerState current) noexcept {
        if (previous == current) return;
        for (auto& cb : state_changed_callbacks_) cb(previous, current);
    }

    // commit performs the state_ update + state-changed notification common
    // to every successful branch of transition() above.
    std::error_code commit(ServerState target) noexcept {
        ServerState previous = state_;
        state_ = target;
        notify_state_changed(previous, state_);
        return {};
    }
};

// ── Per-state request filtering ───────────────────────────────────────────────
// Added during the Phase 2 content-parity pass, ported from c-RCP's
// rcp_lifecycle_should_accept() (REQ-LIFECYCLE-014..017/028/029/032/033) —
// this header had no request-admission model of any kind before this pass;
// access-control gating (issue #198 in c-RCP's own history) also covers
// this per-state filtering, not just writer authorization/idleness above.

// The discovery byte_bus_id: the one address reachable while the server is
// still HW_UNCONFIGURED.
constexpr avtp::ByteBusId kDiscoveryByteBusId = 0;

// Disposition is should_accept()'s own three-way outcome (REQ-LIFECYCLE-033):
// a frame is either fully admitted, silently dropped with no response at
// all, or admitted far enough to answer with an error response but no
// further processing. TC18 distinguishes these explicitly — §12.7's own
// "Other valid requests to EP0 will be rejected with an error response with
// error code REQUEST_REJECTED" is a different outcome than §12.3.1.1's/
// §12.3.1.2's "dropped without further response" — so a plain bool cannot
// represent this function's full contract.
enum class Disposition {
    Accept = 0, // admit the frame for normal processing
    Drop   = 1, // silently discard, no response at all
    Reject = 2, // answer with acf::WireErrorCode::RequestRejected, process no further
};

// The per-state request-filtering rule (mirrors rcp/avtp.hpp's own
// should_drop_tscf() convention):
//
//   - Whatever the state, a TSCF-headed frame is first subject to
//     avtp::should_drop_tscf()'s own time-sync rule (Disposition::Drop iff
//     that call returns true) — REQ-AVTP-014/021, TC18 §13.3.
//   - While HwUnconfigured: a TSCF-headed frame is dropped outright
//     regardless of time_sync_supported (presentation-time semantics
//     presuppose a configured request stream, which cannot exist yet). An
//     NTSCF-headed frame not addressed to kDiscoveryByteBusId is dropped
//     too. Addressed there, an ACF_ABB (STANDARD) message is accepted; any
//     other message type (in particular ACF_GBB, this codebase's wire
//     encoding for every conditional request kind) is REQ-LIFECYCLE-033's
//     own Reject outcome, per TC18 §12.7.
//   - While HwConfigured: a TSCF-headed frame is dropped outright too, for
//     the same reason. Acceptance is further restricted to
//     kDiscoveryByteBusId (EP0) — TC18 §12.3.1.2 requires requests to EPs
//     other than EP0 that are not config requests to be dropped, and this
//     library has no wire-level encode/decode pair for a functional-
//     configuration read/write request at all yet, so every currently-
//     decodable non-EP0 request is, by construction, operational — the
//     EP0-only restriction is the honestly-achievable form of that rule
//     given this library's real current scope (Disposition::Drop, not
//     Reject — §12.3.1.2's own "ignored and dropped without response" text
//     for non-EP0 requests). Addressed to EP0 itself, the same
//     ACF_ABB-vs-other split as HwUnconfigured applies.
//   - While RcpConfigured: acceptance beyond the general time-sync rule
//     already applied above is unrestricted at this milestone — the
//     validated mapping HwConfigured's rules are guarding against now
//     exists. Register-level write filtering is field_writable()'s own
//     job, below.
//
// avtp_subtype is one of avtp::kSubtypeNtscf/kSubtypeTscf; acf_msg_type is
// one of acf::kAcfMsgTypeAbb/kAcfMsgTypeGbb, or any other value for a
// message type this filtering rule does not special-case.
inline Disposition should_accept(ServerState state, bool time_sync_supported, uint8_t avtp_subtype,
                                  uint8_t acf_msg_type, avtp::ByteBusId byte_bus_id,
                                  avtp::TscfFallback unsupported_time_sync_policy) noexcept {
    if (avtp::should_drop_tscf(time_sync_supported, avtp_subtype, unsupported_time_sync_policy))
        return Disposition::Drop;

    if (state == ServerState::HwUnconfigured) {
        if (avtp_subtype == avtp::kSubtypeTscf) return Disposition::Drop;
        if (avtp_subtype != avtp::kSubtypeNtscf || byte_bus_id != kDiscoveryByteBusId)
            return Disposition::Drop;
        return (acf_msg_type == acf::kAcfMsgTypeAbb) ? Disposition::Accept : Disposition::Reject;
    }

    if (state == ServerState::HwConfigured) {
        if (avtp_subtype == avtp::kSubtypeTscf) return Disposition::Drop;
        if (byte_bus_id != kDiscoveryByteBusId) return Disposition::Drop;
        return (acf_msg_type == acf::kAcfMsgTypeAbb) ? Disposition::Accept : Disposition::Reject;
    }

    // RcpConfigured
    return Disposition::Accept;
}

// ── Register-locking-by-state ─────────────────────────────────────────────────
// Added during the Phase 2 content-parity pass, ported from c-RCP's
// rcp_lifecycle_field_kind_t/field_writable()/field_write_error() —
// ServerLifecycle's own pre-existing generic_config_locked()/
// functional_config_locked() only ever answered a coarse "is this whole
// category locked right now" question; this header never modeled WHO may
// write, or the two-tier W-vs-W* distinction TC18 Table 24 draws between
// functional-config fields that stay writable via the owning stream once
// RCP_CONFIGURED (FunctionalW) and those that permanently lock at that
// point (FunctionalWStar) — access-control gating (c-RCP issue #198 in its
// own history) is squarely this content.

// Which broad category a register field falls into for locking purposes.
// HwGeneric covers HW-pin-mapping and every other block TC18 Figure 17
// groups under the identical HW_UNCONFIGURED-only locking rule and
// identical LOCKED_CONFIG_ACCESS response. FunctionalW and FunctionalWStar
// both cover functional configuration but differ in what happens once
// RcpConfigured is reached. ReadOnly (TC18 §12.7.5 Table 20's own access
// type "R"): unwritable unconditionally, in every lifecycle state, by
// every writer.
enum class FieldKind {
    HwGeneric        = 0,
    FunctionalW      = 1,
    FunctionalWStar  = 2,
    ReadOnly         = 3,
};

// True iff a field of the given kind is writable while the server is in
// `state`, by the given `writer`:
//
//   - HwGeneric: writable only in HwUnconfigured, and only when writer
//     indicates the discovery stream (TC18 §12.3.1.1/§12.7.2 — "All
//     configurations must be run via the stream which was used for
//     discovery"; REQ-LIFECYCLE-026/035). Read-only from the moment the
//     server reaches HwConfigured, for any writer.
//   - FunctionalW: not writable in HwUnconfigured; while HwConfigured,
//     writable only when writer indicates the root client via EP0, the
//     endpoint's own owning stream, or the discovery stream
//     (REQ-LIFECYCLE-030/036); once RcpConfigured, writable only when
//     writer indicates the endpoint's own stream or the root client via
//     EP0 — the discovery stream no longer suffices on its own
//     (REQ-LIFECYCLE-037).
//   - FunctionalWStar: writable unconditionally while HwUnconfigured; the
//     same authorization as FunctionalW while HwConfigured; permanently
//     locked (unwritable by any writer) once RcpConfigured is reached.
//   - ReadOnly (REQ-RMAP-025): never writable, in any state, by any
//     writer — state and writer are both deliberately ignored.
//
// Independently of all cases above: TC18 §12.3.1.1/§12.3.1.2/§12.3.1.3 each
// state that a write request is accepted only when sent in a unicast frame
// (REQ-LIFECYCLE-027) — ANDed in uniformly here rather than duplicated per
// branch above.
inline bool field_writable(ServerState state, FieldKind kind, WriterCtx writer) noexcept {
    const bool authorized              = writer.via_root_client_ep0 || writer.via_owning_stream;
    const bool hw_configured_authorized = authorized || writer.via_discovery_stream;
    bool writable;

    switch (kind) {
    case FieldKind::HwGeneric:
        writable = (state == ServerState::HwUnconfigured) && writer.via_discovery_stream;
        break;

    case FieldKind::FunctionalW:
        if (state == ServerState::HwUnconfigured) {
            writable = false;
        } else if (state == ServerState::RcpConfigured) {
            writable = authorized;
        } else {
            writable = hw_configured_authorized; // HwConfigured
        }
        break;

    case FieldKind::FunctionalWStar:
        if (state == ServerState::RcpConfigured) {
            writable = false; // permanently locked once reached
        } else if (state == ServerState::HwConfigured) {
            writable = hw_configured_authorized;
        } else {
            writable = true; // HwUnconfigured
        }
        break;

    case FieldKind::ReadOnly:
        writable = false;
        break;

    default:
        writable = false;
        break;
    }

    return writable && !writer.via_non_unicast_frame;
}

// Distinguishes WHY field_writable() denied a write, mapping to the wire
// error code TC18 actually assigns each reason — std::nullopt if it did not
// deny it (REQ-LIFECYCLE-024):
//
//   - acf::WireErrorCode::LockedMemAccess: state alone forbids the write —
//     even a maximally-privileged writer would still be denied (TC18
//     Figure 17's own LOCKED_CONFIG_ACCESS transition — a diagram-only name
//     unambiguously matching this numbered code, the same kind of
//     prose-vs-table naming variance rcp/e2e.hpp's own POCI_FAILURE mapping
//     already documents).
//   - acf::WireErrorCode::UnauthorizedAccess: state would otherwise permit
//     the write, but writer specifically is not authorized for it
//     (REQ-LIFECYCLE-027/030/031/036).
//
// Implemented via two calls to field_writable() itself (the real writer,
// then a maximally-privileged one) rather than a second, separately-
// maintained copy of its state/kind table — the two can never drift out of
// sync with each other by construction.
inline std::optional<acf::WireErrorCode> field_write_error(ServerState state, FieldKind kind,
                                                             WriterCtx writer) noexcept {
    if (field_writable(state, kind, writer)) return std::nullopt;

    WriterCtx best;
    best.via_root_client_ep0   = true;
    best.via_owning_stream     = true;
    best.via_non_unicast_frame = false;
    best.via_discovery_stream  = true;

    if (!field_writable(state, kind, best)) return acf::WireErrorCode::LockedMemAccess;
    return acf::WireErrorCode::UnauthorizedAccess;
}

// REQ-RMAP-055: TC18's own W+ (explicitly lockable) access type — distinct
// from every FieldKind above. A W+ field follows the SAME lifecycle-state/
// writer rule as FunctionalWStar, PLUS an INDEPENDENT lock the configuring
// instance may set at any time to protect the table from further
// modification, "independently of the lifecycle state that governs W and
// W*" (TC18's own words). `locked` is that additional, caller-supplied bit
// — true always overrides whatever the state/writer rule would otherwise
// permit, in any lifecycle state. Deliberately a separate function, not a
// new FieldKind value threaded through field_writable() itself — mirrors
// c-RCP's own rationale (lifecycle.h) for keeping this additive rather than
// widening field_writable()'s signature for every existing caller.
inline bool field_writable_w_plus(ServerState state, WriterCtx writer, bool locked) noexcept {
    if (locked) return false; // the field's own explicit lock always wins
    return field_writable(state, FieldKind::FunctionalWStar, writer);
}

// The field_write_error()-style error classification for a W+ field, with a
// third input (`locked`) folded in: LockedMemAccess when `locked` is true
// (this field's own explicit lock always wins, unconditionally) OR the
// underlying FunctionalWStar state rule would deny even a maximally-
// privileged writer; UnauthorizedAccess when the underlying state would
// otherwise permit the write but writer specifically does not; std::nullopt
// when writable.
inline std::optional<acf::WireErrorCode> field_write_error_w_plus(ServerState state, WriterCtx writer,
                                                                    bool locked) noexcept {
    if (field_writable_w_plus(state, writer, locked)) return std::nullopt;
    if (locked) return acf::WireErrorCode::LockedMemAccess;

    WriterCtx best;
    best.via_root_client_ep0   = true;
    best.via_owning_stream     = true;
    best.via_non_unicast_frame = false;
    best.via_discovery_stream  = true;

    if (!field_writable_w_plus(state, best, false)) return acf::WireErrorCode::LockedMemAccess;
    return acf::WireErrorCode::UnauthorizedAccess;
}

} // namespace lifecycle
} // namespace rcp

// Enable std::error_code construction from rcp::lifecycle::LifecycleErrc.
namespace std {
template <>
struct is_error_code_enum<rcp::lifecycle::LifecycleErrc> : true_type {};
} // namespace std
