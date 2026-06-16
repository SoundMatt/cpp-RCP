// fusa:req REQ-AUTH-001
// fusa:req REQ-AUTH-002
// fusa:req REQ-AUTH-003
// fusa:req REQ-AUTH-004
// fusa:req REQ-AUTH-005
// fusa:req REQ-AUTH-006
// fusa:req REQ-AUTH-007
// fusa:req REQ-AUTH-008

// Command-level access control (ISO 21434 / IEC 62443 SL-2).
//
// AuthController wraps any rcp::Controller and checks each Command against a
// signed AccessPolicy before forwarding.  A policy declares which caller
// identities may send which CommandType values to which zones.
//
// The default Identity is a string (certificate CN or pre-shared key label).
// Full certificate-chain validation is the responsibility of the TLS layer
// (include/rcp/tls.hpp); authz only checks the presented identity against the
// policy table.
#pragma once

#include "rcp.hpp"

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

namespace rcp {
namespace authz {

// ── Identity ──────────────────────────────────────────────────────────────────

// Identity is the caller's verified string identifier (certificate CN or
// pre-shared label).  Presented by the caller via Context::set_identity()
// or a thread-local before each send().
using Identity = std::string;

// ── PolicyEntry ───────────────────────────────────────────────────────────────

struct PolicyEntry {
    Identity                          identity;
    std::unordered_set<Zone>          zones;       // empty = all zones
    std::unordered_set<CommandType>   cmd_types;   // empty = all types
};

// ── AccessPolicy ──────────────────────────────────────────────────────────────

class AccessPolicy {
public:
    // allow adds a permission entry.
    void allow(PolicyEntry entry) {
        std::lock_guard<std::mutex> lk(mu_);
        entries_.push_back(std::move(entry));
    }

    // permit checks whether identity may send cmd to zone.
    bool permit(const Identity& id, Zone zone, CommandType type) const {
        std::lock_guard<std::mutex> lk(mu_);
        for (auto& e : entries_) {
            if (e.identity != id) continue;
            bool zone_ok = e.zones.empty()     || e.zones.count(zone);
            bool type_ok = e.cmd_types.empty() || e.cmd_types.count(type);
            if (zone_ok && type_ok) return true;
        }
        return false;
    }

private:
    mutable std::mutex         mu_;
    std::vector<PolicyEntry>   entries_;
};

// ── ErrForbidden ──────────────────────────────────────────────────────────────

enum class AuthzErrc : int { forbidden = 1 };

inline const std::error_category& authz_category() noexcept {
    struct Cat : std::error_category {
        const char* name() const noexcept override { return "rcp.authz"; }
        std::string message(int ev) const override {
            if (static_cast<AuthzErrc>(ev) == AuthzErrc::forbidden)
                return "rcp/authz: command forbidden by access policy";
            return "rcp/authz: unknown error";
        }
    };
    static Cat instance;
    return instance;
}

inline std::error_code make_error_code(AuthzErrc e) noexcept {
    return {static_cast<int>(e), authz_category()};
}

inline const std::error_code ErrForbidden = make_error_code(AuthzErrc::forbidden);

// ── AuthController ────────────────────────────────────────────────────────────

// AuthController wraps any Controller and enforces an AccessPolicy.
// Set the caller identity via set_identity() before each send() call.
class AuthController final : public rcp::Controller {
public:
    using IdentityFn = std::function<Identity()>;

    // Construct with a shared policy and a resolver that returns the current
    // caller identity (e.g. a thread-local or TLS session attribute).
    AuthController(std::shared_ptr<rcp::Controller> inner,
                   std::shared_ptr<AccessPolicy>    policy,
                   IdentityFn                       identity_fn = {})
        : inner_(std::move(inner))
        , policy_(std::move(policy))
        , identity_fn_(std::move(identity_fn)) {}

    Zone zone() const noexcept override { return inner_->zone(); }

    std::error_code send(const rcp::Context& ctx,
                          const Command&      cmd,
                          Response&           out) override {
        Identity id = identity_fn_ ? identity_fn_() : identity_;
        if (!policy_->permit(id, cmd.zone, cmd.type))
            return ErrForbidden;
        return inner_->send(ctx, cmd, out);
    }

    std::error_code subscribe(const rcp::Context&             ctx,
                               std::shared_ptr<StatusChannel>& out) override {
        return inner_->subscribe(ctx, out);
    }

    std::error_code close() override { return inner_->close(); }

    // set_identity sets a fixed caller identity for this controller instance.
    void set_identity(Identity id) { identity_ = std::move(id); }

private:
    std::shared_ptr<rcp::Controller> inner_;
    std::shared_ptr<AccessPolicy>    policy_;
    IdentityFn                       identity_fn_;
    Identity                         identity_;
};

inline std::shared_ptr<AuthController> new_controller(
        std::shared_ptr<rcp::Controller> inner,
        std::shared_ptr<AccessPolicy>    policy,
        AuthController::IdentityFn       id_fn = {}) {
    return std::make_shared<AuthController>(
        std::move(inner), std::move(policy), std::move(id_fn));
}

} // namespace authz
} // namespace rcp

namespace std {
template <>
struct is_error_code_enum<rcp::authz::AuthzErrc> : true_type {};
} // namespace std
