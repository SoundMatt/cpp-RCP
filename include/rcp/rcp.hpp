// fusa:req REQ-ERR-001
// fusa:req REQ-ERR-002
// fusa:req REQ-ERR-003
// fusa:req REQ-ERR-004
// fusa:req REQ-ERR-005
// fusa:req REQ-ERR-006
// fusa:req REQ-ERR-007
// fusa:req REQ-ERR-008
// fusa:req REQ-ERR-009
// fusa:req REQ-ERR-010
// fusa:req REQ-LOAN-007

// Core rcp:: primitives shared codebase-wide: the `rcp::Errc` sentinel error
// category, `rcp::Context` (an alias for relay::Context, §18.2), and
// `rcp::Loan`, a generic RAII buffer-loan holder.
//
// RELAY conformance: include <relay/relay.hpp> for relay:: namespace types.
// The RC-Server-addressed request/response model itself — the TC18 wire
// codec, register map, lifecycle, and application-facing Adapt()/RequestFn
// surface — lives in rcp/avtp.hpp, rcp/acf.hpp, rcp/regmap.hpp,
// rcp/lifecycle.hpp, and rcp/adapt.hpp, not here.
//
// Retirement notice (cpp-RCP-FS-01, 2026-07): this file used to also define
// a pre-TC18 placeholder object model (`Zone`, `Priority`, `CommandType`,
// `Command`, `Response`, `Status`, `StatusChannel`, `Controller`,
// `LoaningController`, `Registry`, and `Errc::zone_mismatch`/
// `ErrZoneMismatch`), predating cpp-RCP's alignment to the OPEN Alliance
// TC18 Remote Control Protocol Specification v0.5.1_RC. RELAY spec v2.0
// §15.5 states that placeholder model is retired and that a conformant
// implementation does not also speak it as a compatibility shim, so that
// entire surface — the last live compiled consumer was rcp/config.hpp — was
// deleted outright rather than migrated; there is no direct replacement
// symbol for `Zone`/`Command`/`Controller`/`Registry` individually, since
// TC18 addresses Endpoints on an RC Server by StreamID/ByteBusID rather
// than by zone/controller lookup. See rcp/adapt.hpp's `RequestFn` and
// `Adapt()` for the current application-facing entry point, and
// ROADMAP.md's Satellite Package Disposition table for how each former
// dependent (`legacy_mock.hpp`, `config.hpp`, `faultinject.hpp`,
// `redundancy.hpp`, `admin.hpp`) was rebound or removed. `rcp::Loan` is
// unaffected by this retirement — it never depended on Zone/Command and is
// still the buffer type `rcp/loan.hpp`'s `BufferPool` hands out.
#pragma once

#include <relay/relay.hpp>

#include <functional>
#include <string>
#include <system_error>
#include <vector>

namespace rcp {

// ── Error codes ───────────────────────────────────────────────────────────────

enum class Errc : int {
    closed         = 1,
    not_found      = 2,
    already_exists = 3,
    timeout        = 4,
    busy           = 5,
};

inline const std::error_category& rcp_category() noexcept {
    struct Cat : std::error_category {
        const char* name() const noexcept override { return "rcp"; }
        std::string message(int ev) const override {
            switch (static_cast<Errc>(ev)) {
            case Errc::closed:         return "rcp: closed";
            case Errc::not_found:      return "rcp: not found";
            case Errc::already_exists: return "rcp: already exists";
            case Errc::timeout:        return "rcp: timeout";
            case Errc::busy:           return "rcp: busy";
            default:                   return "rcp: unknown error";
            }
        }
        // Map rcp::Errc codes to relay::Errc conditions (§5.2, §5.3, §5.4).
        bool equivalent(int code, const std::error_condition& cond) const noexcept override {
            if (cond.category() != relay::relay_category()) return false;
            auto re = static_cast<relay::Errc>(cond.value());
            switch (static_cast<Errc>(code)) {
            case Errc::closed:         return re == relay::Errc::closed;
            case Errc::timeout:        return re == relay::Errc::timeout;
            case Errc::busy:           return re == relay::Errc::timeout;
            case Errc::not_found:      return re == relay::Errc::not_connected;
            case Errc::already_exists: return false;  // standalone per §5.4 — not a relay sentinel
            default:                   return false;
            }
        }
    };
    static Cat instance;
    return instance;
}

inline std::error_code make_error_code(Errc e) noexcept {
    return {static_cast<int>(e), rcp_category()};
}

// Sentinel error codes — analogous to Go's package-level sentinel errors.
inline const std::error_code ErrClosed        = make_error_code(Errc::closed);
inline const std::error_code ErrNotFound      = make_error_code(Errc::not_found);
inline const std::error_code ErrAlreadyExists = make_error_code(Errc::already_exists);
inline const std::error_code ErrTimeout       = make_error_code(Errc::timeout);
inline const std::error_code ErrBusy          = make_error_code(Errc::busy);

// ── Context — relay::Context alias (§18.2) ────────────────────────────────────
// rcp::Context is an alias for relay::Context per §18.2 conformance.

using Context = relay::Context;

// ── Loan — generic RAII buffer-loan holder ────────────────────────────────────
// A payload buffer borrowed from a pool (rcp/loan.hpp's BufferPool). The
// caller MUST either consume payload directly or let the Loan go out of
// scope (which calls the release function and returns the buffer to its
// pool), or release it early via ret().
class Loan {
public:
    std::vector<uint8_t> payload;

    Loan() = default;
    Loan(std::vector<uint8_t> p, std::function<void()> r)
        : payload(std::move(p)), release_(std::move(r)) {}

    ~Loan() { if (release_) release_(); }

    Loan(const Loan&) = delete;
    Loan& operator=(const Loan&) = delete;

    Loan(Loan&& o) noexcept
        : payload(std::move(o.payload)), release_(std::move(o.release_)) {
        o.release_ = nullptr;
    }
    Loan& operator=(Loan&&) = delete;

    // Return releases the Loan back to the pool without sending.
    void ret() {
        if (release_) { release_(); release_ = nullptr; }
    }

private:
    std::function<void()> release_;
};

} // namespace rcp

// Enable std::error_code construction from rcp::Errc.
namespace std {
template <>
struct is_error_code_enum<rcp::Errc> : true_type {};
} // namespace std
