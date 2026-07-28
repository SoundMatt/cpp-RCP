// fusa:req REQ-PWR-001
// fusa:req REQ-PWR-002
// fusa:req REQ-PWR-003
// fusa:req REQ-PWR-004
// fusa:req REQ-PWR-005
// fusa:req REQ-PWR-006
// fusa:req REQ-PWR-007
// fusa:req REQ-PWR-008
// fusa:req REQ-PWR-009
// fusa:req REQ-PWR-010
// fusa:req REQ-PWR-011
// fusa:req REQ-PWR-012
// fusa:req REQ-PWR-013
// fusa:req REQ-PWR-014

// Power management — the OPEN Alliance TC18 Remote Control Protocol
// Specification v0.5.1_RC's actual power-mode model (`Normal`/`StandBy`/
// `Sleep`/`Unpowered`), the entry-refusal conditions guarding a transition
// into either low-power mode, and the hot-start-from-Sleep wake handshake
// (extraction §3.3, §3.4).
//
// ROADMAP.md milestone 53, "Power Management Rebuild (v2.9.0)", opening
// Phase 14: this header REPLACES this file's pre-replacement content in
// full, per the Satellite Package Disposition table's entry for
// `powerstate.hpp` — the prior ad-hoc `Active`/`Sleeping`/`BusOff` model
// built on rcp.hpp's `Zone`/`CommandType`/`Controller` is discarded, not
// adapted. Nothing else in this tree depended on that old API (only this
// file's own test did), so no legacy shim is needed here, same as
// rcp/e2e.hpp's equivalent note at v2.6.0.
//
// This header rides directly on rcp/wakeup.hpp's `WakeupEndpoint`
// (v2.7.0) — the wake-source/handshake primitives it exposes
// (`wakeup_message_pending`/`acknowledge_wakeup`) are driven from here
// rather than reimplemented, same layering style rcp/e2e.hpp established
// for reusing rcp/request.hpp's `RequestLedger::cancel_all` — and on
// rcp/lifecycle.hpp's `ServerLifecycle` only informally, in that both
// headers model a forward-driven state machine gated by caller-supplied
// plausibility/readiness checks rather than any shared base type. This
// header does not itself reach into rcp/regmap.hpp's register-map types:
// the "non-idle endpoint" and "non-empty response/ack queue" entry-refusal
// checks are exposed as caller-supplied predicates (`Hooks::endpoints_idle`,
// `Hooks::response_ack_queues_empty` below) rather than this header walking
// a `RegisterMap` itself, mirroring rcp/lifecycle.hpp's own
// `PlausibilityCheck` pattern for the same reason: keeping this header
// usable without hard-wiring it to one concrete register-map shape.
//
// Field names and behavior below implement TC18's *behavior* as described
// in an internal structured extraction of the specification named above;
// no text from that document is reproduced here. The concrete class shapes,
// the wake-handshake repeat-limit default, and the exact split of
// responsibility across the methods below are this implementation's own
// encoding of that behavior — full bit-for-bit conformance against other
// TC18 implementations is not claimed, same as the equivalent disclaimers
// in rcp/lifecycle.hpp, rcp/regmap.hpp, and rcp/wakeup.hpp. This header
// provides primitives, not a running scheduler, timer, or transport of its
// own — deciding *when* to retransmit the WakeUp message or poll for its
// echo is left to the embedding application's driver layer, same
// disclaimer as every other endpoint/lifecycle header in this codebase.
#pragma once

#include <rcp/wakeup.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <system_error>

namespace rcp {
namespace powerstate {

// ── PowerMode ─────────────────────────────────────────────────────────────────
// The four power modes an RC Server can be in (extraction §3.3). Values are
// this implementation's own numbering — the specification's own bit-level
// encoding, if any, is not claimed here.

enum class PowerMode : uint8_t {
    Normal    = 0, // fully operational
    StandBy   = 1, // low-power; always resumed via a hot start (see StartKind)
    Sleep     = 2, // low-power; always resumed via a cold start (see StartKind)
    Unpowered = 3, // no power at all; entered/exited by hardware events, not requested
};

inline std::string to_string(PowerMode m) {
    switch (m) {
    case PowerMode::Normal:    return "normal";
    case PowerMode::StandBy:   return "standby";
    case PowerMode::Sleep:     return "sleep";
    case PowerMode::Unpowered: return "unpowered";
    default:                   return "unknown";
    }
}

// ── StartKind ─────────────────────────────────────────────────────────────────
// Whether resuming to Normal from a given low-power mode is a hot or cold
// start (extraction §3.4). This is a fixed property of *which* low-power
// mode was entered, not a runtime choice: StandBy is always a hot start
// (state is retained; PowerManager::resume_from_standby needs no handshake
// at all) and Sleep is always a cold start (state is not retained; the
// hot-start-from-Sleep *handshake* below is the wire-level wake procedure
// that precedes that cold start, not a contradiction of it — see
// PowerManager's class comment).

enum class StartKind : uint8_t {
    Hot  = 0,
    Cold = 1,
};

inline std::string to_string(StartKind k) {
    return k == StartKind::Hot ? "hot" : "cold";
}

// start_kind_on_exit reports the fixed start kind for resuming from `from`.
// Only meaningful for StandBy/Sleep; Normal/Unpowered have no defined
// "resume" transition of their own (Unpowered's recovery is necessarily a
// cold start too, by construction — see notify_power_restored below — but
// that is a distinct, hardware-driven path, not one this function reports
// on).
constexpr StartKind start_kind_on_exit(PowerMode from) noexcept {
    return from == PowerMode::StandBy ? StartKind::Hot : StartKind::Cold;
}

// ── Errors ────────────────────────────────────────────────────────────────────

enum class PowerErrc : int {
    invalid_transition           = 1, // requested transition is not defined from the current mode
    unacknowledged_wakeup_event  = 2, // entry refusal: WakeupEndpoint still owes a WakeUp repetition
    endpoint_not_idle            = 3, // entry refusal: caller's endpoints_idle hook reported false
    response_ack_queue_not_empty = 4, // entry refusal: caller's response_ack_queues_empty hook reported false
    not_asleep                   = 5, // wake-from-sleep handshake step requested while mode() != Sleep
    handshake_repeat_limit_exceeded = 6, // WakeUp message repeated cfg.wakeup_repeat_limit times with no echo
};

inline const std::error_category& power_category() noexcept {
    struct Cat : std::error_category {
        const char* name() const noexcept override { return "rcp.powerstate"; }
        std::string message(int ev) const override {
            switch (static_cast<PowerErrc>(ev)) {
            case PowerErrc::invalid_transition:
                return "rcp/powerstate: requested transition is not defined from the current mode";
            case PowerErrc::unacknowledged_wakeup_event:
                return "rcp/powerstate: entry refused — an unacknowledged wake-up event is pending";
            case PowerErrc::endpoint_not_idle:
                return "rcp/powerstate: entry refused — at least one endpoint is not idle";
            case PowerErrc::response_ack_queue_not_empty:
                return "rcp/powerstate: entry refused — a response/ack queue is not empty";
            case PowerErrc::not_asleep:
                return "rcp/powerstate: wake-from-sleep handshake requested while not in Sleep";
            case PowerErrc::handshake_repeat_limit_exceeded:
                return "rcp/powerstate: WakeUp message repeat limit exceeded without an echo";
            default:
                return "rcp/powerstate: unknown error";
            }
        }
    };
    static Cat instance;
    return instance;
}

inline std::error_code make_error_code(PowerErrc e) noexcept {
    return {static_cast<int>(e), power_category()};
}

// ── WakeStage ─────────────────────────────────────────────────────────────────
// Progress of the hot-start-from-Sleep handshake (extraction §3.3). Idle
// outside of an active wake-from-sleep sequence.

enum class WakeStage : uint8_t {
    Idle            = 0, // no wake-from-sleep sequence in progress
    HandshakeActive = 1, // network interface re-enabled; repeating WakeUp until echoed or limit hit
    Complete        = 2, // echoed, response/ack queues re-enabled, mode() == Normal
    Failed          = 3, // repeat limit exceeded without an echo; mode() is still Sleep
};

// ── Config ────────────────────────────────────────────────────────────────────

struct Config {
    // The specification leaves the concrete WakeUp-message repeat limit to
    // the implementation (extraction §3.3); this default is this
    // implementation's own choice.
    uint32_t wakeup_repeat_limit = 8;
};

inline Config default_config() { return Config{}; }

// ── PowerManager ──────────────────────────────────────────────────────────────
// PowerManager owns the current PowerMode and drives every transition
// described above. It has no clock, thread, or transport of its own — see
// this file's header comment.
//
// A note on naming: extraction §3.3 names the wire-level wake-up procedure
// used when leaving Sleep the "hot start" handshake — network-interface
// re-enablement followed by a repeating WakeUp message — even though
// leaving Sleep is *itself* always a cold start of the server's retained
// state (extraction §3.4; see StartKind above). Those are two different
// axes: StartKind describes whether application-level state survived the
// low-power mode; the handshake below describes the wire-level procedure
// used to bring the network interface back up before that cold start can
// proceed. This header follows the extraction's own terminology for the
// handshake's name (begin_wake_from_sleep, not begin_cold_start) rather
// than reconciling the two into a single term of this implementation's own
// invention.
class PowerManager {
public:
    // true return means "every managed endpoint is idle" / "every
    // response/ack queue is empty" — see this file's header comment for why
    // these are caller-supplied hooks rather than a direct rcp/regmap.hpp
    // dependency.
    using EndpointIdleCheck = std::function<bool()>;
    using QueueEmptyCheck   = std::function<bool()>;
    using NetworkReenable   = std::function<void()>;
    using QueueReenable     = std::function<void()>;

    struct Hooks {
        EndpointIdleCheck endpoints_idle;              // entry-refusal condition (b)
        QueueEmptyCheck   response_ack_queues_empty;    // entry-refusal condition (c)
        NetworkReenable   reenable_network_interface;   // hot-start-from-Sleep step 1
        QueueReenable     reenable_response_ack_queues; // hot-start-from-Sleep step 3
    };

    explicit PowerManager(wakeup::WakeupEndpoint& wakeup_ep, Hooks hooks = {},
                           Config cfg = default_config()) noexcept
        : wakeup_ep_(wakeup_ep), hooks_(std::move(hooks)), cfg_(cfg) {}

    PowerMode mode() const noexcept { return mode_; }
    WakeStage wake_stage() const noexcept { return wake_stage_; }
    uint32_t  wake_attempts() const noexcept { return wake_attempts_; }

    // pending_start_kind reports the fixed start kind that resuming from the
    // current mode will be (only meaningful while mode() is StandBy or
    // Sleep — see start_kind_on_exit).
    StartKind pending_start_kind() const noexcept { return start_kind_on_exit(mode_); }

    // enter_standby / enter_sleep request a transition from Normal into the
    // named low-power mode. Both apply the same three entry-refusal checks,
    // in the fixed order below, since extraction §3.3/§3.4 define them
    // identically regardless of which low-power mode is being entered:
    //   1. an unacknowledged wake-up event (WakeupEndpoint still owes a
    //      WakeUp repetition from a prior cycle)
    //   2. a non-idle endpoint (Hooks::endpoints_idle)
    //   3. a non-empty response/ack queue (Hooks::response_ack_queues_empty)
    // A hook left unset is treated as "check passes" (nothing to refuse
    // on), same convention as rcp/lifecycle.hpp's PlausibilityCheck.
    std::error_code enter_standby() noexcept { return enter_low_power(PowerMode::StandBy); }
    std::error_code enter_sleep() noexcept { return enter_low_power(PowerMode::Sleep); }

    // resume_from_standby implements StandBy's fixed hot-start rule
    // directly: no network re-enablement, no WakeUp handshake — state was
    // retained, so the manager returns straight to Normal.
    std::error_code resume_from_standby() noexcept {
        if (mode_ != PowerMode::StandBy)
            return make_error_code(PowerErrc::invalid_transition);
        mode_ = PowerMode::Normal;
        wake_stage_ = WakeStage::Idle;
        return {};
    }

    // begin_wake_from_sleep starts the hot-start-from-Sleep handshake
    // (extraction §3.3): step 1, network-interface re-enablement, runs
    // synchronously here via Hooks::reenable_network_interface. Requires
    // mode() == Sleep.
    std::error_code begin_wake_from_sleep() noexcept {
        if (mode_ != PowerMode::Sleep)
            return make_error_code(PowerErrc::not_asleep);
        if (hooks_.reenable_network_interface) hooks_.reenable_network_interface();
        wake_attempts_ = 0;
        wake_stage_ = WakeStage::HandshakeActive;
        return {};
    }

    // note_wakeup_attempt_sent records that the caller's transport layer
    // just (re)transmitted the WakeUp message once — step 2's repetition
    // (extraction §3.3). Must be called only while wake_stage() ==
    // HandshakeActive. Once wakeup_ep_'s handshake is no longer pending
    // (already echoed, e.g. acknowledge_wakeup() was called concurrently by
    // the driver layer) this is a harmless no-op. Exceeding
    // cfg.wakeup_repeat_limit moves wake_stage() to Failed and leaves
    // mode() at Sleep — the caller decides whether/when to retry.
    std::error_code note_wakeup_attempt_sent() noexcept {
        if (wake_stage_ != WakeStage::HandshakeActive)
            return make_error_code(PowerErrc::not_asleep);
        if (!wakeup_ep_.wakeup_message_pending()) return {};
        if (++wake_attempts_ > cfg_.wakeup_repeat_limit) {
            wake_stage_ = WakeStage::Failed;
            return make_error_code(PowerErrc::handshake_repeat_limit_exceeded);
        }
        return {};
    }

    // acknowledge_wakeup completes step 2 once the far end has echoed the
    // WakeUp message, then carries out step 3 (Hooks::reenable_response_ack_queues)
    // and returns the manager to Normal. Must be called only while
    // wake_stage() == HandshakeActive.
    std::error_code acknowledge_wakeup() noexcept {
        if (wake_stage_ != WakeStage::HandshakeActive)
            return make_error_code(PowerErrc::not_asleep);
        wakeup_ep_.acknowledge_wakeup();
        if (hooks_.reenable_response_ack_queues) hooks_.reenable_response_ack_queues();
        mode_ = PowerMode::Normal;
        wake_stage_ = WakeStage::Complete;
        return {};
    }

    // notify_power_removed models an unconditional hardware power-loss
    // event. Unlike enter_standby/enter_sleep, there is no refusal path
    // here: power removal is reported, not requested.
    void notify_power_removed() noexcept {
        mode_ = PowerMode::Unpowered;
        wake_stage_ = WakeStage::Idle;
    }

    // notify_power_restored models power returning. Recovering from
    // Unpowered necessarily loses all volatile state, so — like waking from
    // Sleep — it is a cold start; this header does not model the full
    // re-initialization sequence beyond returning mode() to Normal, since
    // register-map/lifecycle reconfiguration after a cold start is
    // rcp/lifecycle.hpp's concern, not this one's.
    void notify_power_restored() noexcept {
        mode_ = PowerMode::Normal;
        wake_stage_ = WakeStage::Idle;
    }

private:
    wakeup::WakeupEndpoint& wakeup_ep_;
    Hooks                   hooks_;
    Config                  cfg_;
    PowerMode               mode_       = PowerMode::Normal;
    WakeStage               wake_stage_ = WakeStage::Idle;
    uint32_t                wake_attempts_ = 0;

    std::error_code enter_low_power(PowerMode target) noexcept {
        if (mode_ != PowerMode::Normal)
            return make_error_code(PowerErrc::invalid_transition);
        if (wakeup_ep_.wakeup_message_pending())
            return make_error_code(PowerErrc::unacknowledged_wakeup_event);
        if (hooks_.endpoints_idle && !hooks_.endpoints_idle())
            return make_error_code(PowerErrc::endpoint_not_idle);
        if (hooks_.response_ack_queues_empty && !hooks_.response_ack_queues_empty())
            return make_error_code(PowerErrc::response_ack_queue_not_empty);
        mode_ = target;
        wake_stage_ = WakeStage::Idle;
        return {};
    }
};

} // namespace powerstate
} // namespace rcp

// Enable std::error_code construction from rcp::powerstate::PowerErrc.
namespace std {
template <>
struct is_error_code_enum<rcp::powerstate::PowerErrc> : true_type {};
} // namespace std
