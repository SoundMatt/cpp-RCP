// fusa:req REQ-AUTH-001
// fusa:req REQ-AUTH-002
// fusa:req REQ-AUTH-003
// fusa:req REQ-AUTH-004
// fusa:req REQ-AUTH-005
// fusa:req REQ-AUTH-006
// fusa:req REQ-AUTH-007
// fusa:req REQ-AUTH-008

// Identity-based access-policy check (ISO 21434 / IEC 62443 SL-2) — a gate
// an embedding request-dispatch call site consults *in addition to*
// rcp/regmap.hpp's own root-client/per-endpoint-owner access model, not a
// replacement for it.
//
// ROADMAP.md milestone 55, "Authorization & Admission-Control Rebind
// (v2.11.0)": this header ADAPTs its pre-replacement content per the
// Satellite Package Disposition table's entry for `authz.hpp` — the
// `AccessPolicy`/`PolicyEntry`/`AuthzErrc` shapes survive, rebound from
// keying on (`Identity`, the pre-replacement `Zone`, the pre-replacement
// `CommandType`) onto (`Identity`, target server, target endpoint, request
// kind), addressed the way the rest of this codebase has addressed those
// concepts since v2.0.0/v2.1.0/v2.5.0: a target server is the opaque
// per-connection `uint64_t` stream key rcp/regmap.hpp's `Ep0` and
// rcp/watchdog.hpp's `Manager` already use (typically an
// `avtp::StreamId::to_u64()` — this header does not know or care how that
// key maps to a stream, only that it is stable for the lifetime of the
// connection, same disclaimer as those two headers); a target endpoint is
// an `avtp::ByteBusId`, unique only within its owning stream (extraction
// §2.1); a request kind is `rcp::request::RequestCategory` (v2.5.0),
// already the single taxonomy spanning the mandatory standard kind and
// every conditional/cancellation kind.
//
// `rcp/regmap.hpp`'s `Ep0::claim_root_client`/`check_write_access` already
// grant a claimed root client whole-register-map write access, and a
// non-root owner endpoint-scoped write access, natively at the protocol
// level (v2.1.0) — this package's job narrows to policy layered *on top
// of* that (the roadmap's own framing), not reimplementing it. A server
// dispatch site that wants both gates enforced calls this header's
// `check`/`permit` and `regmap::Ep0`'s access checks independently; this
// header has no dependency on `regmap.hpp` and does not call into it.
//
// There is no longer a single unified `Controller::send()` chokepoint
// (that unification, if any, does not land until the CLI/capi/adapt
// rebuilds at v2.16.0 per the roadmap), so — unlike the pre-replacement
// `AuthController` wrapper — this header does not wrap or own a send
// path. `AccessPolicy::permit`/`check` are primitives a decode/dispatch
// call site invokes directly before acting on a decoded request, the same
// "primitives driven by the embedding application" pattern
// rcp/e2e.hpp/rcp/watchdog.hpp/rcp/request.hpp already established for
// every other Phase 13/14 header.
//
// Field names and behavior below implement TC18's *behavior* as described
// in an internal structured extraction of the OPEN Alliance TC18 Remote
// Control Protocol Specification v0.5.1_RC; no text from that document is
// reproduced here. The policy-table shape and stream/endpoint-keying
// convention chosen in this file are this implementation's own encoding,
// same as the equivalent disclaimers in rcp/regmap.hpp, rcp/request.hpp,
// rcp/e2e.hpp, and rcp/watchdog.hpp.
#pragma once

#include <rcp/avtp.hpp>
#include <rcp/request.hpp>

#include <cstdint>
#include <mutex>
#include <string>
#include <system_error>
#include <unordered_set>
#include <vector>

namespace rcp {
namespace authz {

// ── Identity ──────────────────────────────────────────────────────────────────

// Identity is the caller's verified string identifier (certificate CN or
// pre-shared label). Full certificate-chain validation is the
// responsibility of the TLS layer (include/rcp/tls.hpp); this header only
// checks a presented identity against the policy table below.
using Identity = std::string;

// ── PolicyEntry ───────────────────────────────────────────────────────────────
// A policy entry grants `identity` access to any combination of streams,
// endpoints, and request kinds it names; an empty set on any one of the
// three axes means "unrestricted on that axis", mirroring the
// pre-replacement PolicyEntry's own empty-set-means-all convention.

struct PolicyEntry {
    Identity identity;

    // Target server: empty = any stream. Populated with
    // avtp::StreamId::to_u64() values (or whatever opaque per-connection
    // key the embedding transport assigns — see this file's header
    // comment).
    std::unordered_set<uint64_t> streams;

    // Target endpoint: empty = any endpoint on a permitted stream. A
    // byte_bus_id is only unique within its owning stream, so this axis is
    // meaningful only in combination with the streams axis above, not on
    // its own.
    std::unordered_set<avtp::ByteBusId> endpoints;

    // Request kind: empty = any kind.
    std::unordered_set<request::RequestCategory> kinds;
};

// ── AccessPolicy ──────────────────────────────────────────────────────────────

class AccessPolicy {
public:
    // allow adds a permission entry.
    void allow(PolicyEntry entry) {
        std::lock_guard<std::mutex> lk(mu_);
        entries_.push_back(std::move(entry));
    }

    // permit checks whether `id` may send a `kind` request to `endpoint` on
    // `stream` — true iff at least one entry for `id` matches all three
    // axes (an empty set on an axis matches anything on that axis).
    bool permit(const Identity& id, uint64_t stream, avtp::ByteBusId endpoint,
                request::RequestCategory kind) const {
        std::lock_guard<std::mutex> lk(mu_);
        for (auto& e : entries_) {
            if (e.identity != id) continue;
            bool stream_ok   = e.streams.empty()   || e.streams.count(stream);
            bool endpoint_ok = e.endpoints.empty() || e.endpoints.count(endpoint);
            bool kind_ok     = e.kinds.empty()     || e.kinds.count(kind);
            if (stream_ok && endpoint_ok && kind_ok) return true;
        }
        return false;
    }

private:
    mutable std::mutex        mu_;
    std::vector<PolicyEntry>  entries_;
};

// ── ErrForbidden ──────────────────────────────────────────────────────────────

enum class AuthzErrc : int { forbidden = 1 };

inline const std::error_category& authz_category() noexcept {
    struct Cat : std::error_category {
        const char* name() const noexcept override { return "rcp.authz"; }
        std::string message(int ev) const override {
            if (static_cast<AuthzErrc>(ev) == AuthzErrc::forbidden)
                return "rcp/authz: request forbidden by access policy";
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

// check is permit() re-expressed as the error-code idiom
// rcp/regmap.hpp's Ep0::check_write_access already uses elsewhere in this
// codebase — a dispatch call site that wants to `return` straight out of a
// failed check gets ErrForbidden without re-deriving it from a bool at
// every call site.
inline std::error_code check(const AccessPolicy& policy, const Identity& id, uint64_t stream,
                              avtp::ByteBusId endpoint, request::RequestCategory kind) {
    return policy.permit(id, stream, endpoint, kind) ? std::error_code{} : ErrForbidden;
}

} // namespace authz
} // namespace rcp

namespace std {
template <>
struct is_error_code_enum<rcp::authz::AuthzErrc> : true_type {};
} // namespace std
