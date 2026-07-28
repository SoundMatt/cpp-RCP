// fusa:req REQ-LIFECYCLE-001
// fusa:req REQ-LIFECYCLE-002
// fusa:req REQ-LIFECYCLE-003
// fusa:req REQ-LIFECYCLE-004
// fusa:req REQ-LIFECYCLE-005
// fusa:req REQ-LIFECYCLE-006

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
// concern, same as rcp/wire.hpp's disclaimer for the wire codec.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
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

// ── ServerLifecycle ───────────────────────────────────────────────────────────
// PlausibilityCheck lets the embedding application supply its own notion of
// "is the configuration applied so far internally consistent" without this
// header needing to know concrete register layouts — the register-map types
// that would typically back such a check live in rcp/regmap.hpp, one layer
// above this one.
using PlausibilityCheck = std::function<bool()>;

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
            state_ = ServerState::HwConfigured;
            return {};
        }
        if (state_ == ServerState::HwConfigured && target == ServerState::RcpConfigured) {
            if (rcp_cfg_check_ && !rcp_cfg_check_())
                return make_error_code(LifecycleErrc::rcp_cfg_inconsistent);
            state_ = ServerState::RcpConfigured;
            return {};
        }
        return make_error_code(LifecycleErrc::invalid_transition);
    }

    // deconfigure is the one sanctioned backward path: an explicit reset to
    // HW_UNCONFIGURED (e.g. on a hardware reset or an operator-triggered
    // reconfiguration), unlocking every register block again.
    void deconfigure() noexcept { state_ = ServerState::HwUnconfigured; }

    // Register-locking queries (extraction §3.2's locking behavior, this
    // implementation's own realization of it — see class comment above).
    bool generic_config_locked() const noexcept {
        return state_ == ServerState::HwConfigured || state_ == ServerState::RcpConfigured;
    }
    bool functional_config_locked() const noexcept {
        return state_ == ServerState::RcpConfigured;
    }

private:
    ServerState        state_ = ServerState::HwUnconfigured;
    PlausibilityCheck  hw_cfg_check_;
    PlausibilityCheck  rcp_cfg_check_;
};

} // namespace lifecycle
} // namespace rcp

// Enable std::error_code construction from rcp::lifecycle::LifecycleErrc.
namespace std {
template <>
struct is_error_code_enum<rcp::lifecycle::LifecycleErrc> : true_type {};
} // namespace std
