// Deterministic allocation-failure fault injection (Phase 17, cpp-RCP issue
// #129 — ROADMAP.md Phase 17 §2, "a fresh allocation seam (dependency-
// injected/std::pmr-style, not a global hook table) with fault-injection
// tests from day one").
//
// ── Why this exists ──────────────────────────────────────────────────────────
// A prior audit this session found cpp-RCP had no equivalent at all to
// c-RCP's rcp_alloc_set_hooks() (include/rcp/alloc.h + src/alloc.c): no way
// to deterministically exercise an allocation-failure branch in a test, and
// (confirmed by grepping this tree) no try/catch(std::bad_alloc) or any
// other allocation-failure handling anywhere in this codebase at all —
// every allocating call site here (std::vector growth, std::make_unique,
// bare `new`) currently just lets std::bad_alloc propagate uncaught.
//
// ── Why this is NOT a port of c-RCP's alloc.h/alloc.c mechanism ─────────────
// c-RCP's rcp_alloc_set_hooks() is a single process-wide global function-
// pointer table that every rcp_malloc()/rcp_calloc()/rcp_realloc()/
// rcp_free() call in that codebase routes through. That design is the
// *wrong* pattern to port literally here, and c-RCP's own history is the
// evidence: issue #600/PR #615 ([c-RCP-23b]) had to retrofit
// rcp_alloc_lock_hooks()/rcp_alloc_unlock_hooks() as a second mechanism
// bolted on top, purely to close an access-control gap the global table
// itself created — with no lock engaged, ANY caller anywhere in the same
// process (including unrelated QM-rated code) could call
// rcp_alloc_set_hooks() at any time and silently redirect the allocator
// backing c-RCP's own ASIL-B-rated e2e.c/watchdog.c call sites, with no
// detection and no attribution (see c-RCP's alloc.h header comment, "Locking
// the hook table" section). That is exactly the class of bug cpp-RCP's
// existing architecture already avoids by construction: every stateful
// primitive in this codebase (rcp/faultinject.hpp's Interceptor, rcp/
// loan.hpp's BufferPool, rcp/watchdog.hpp's Manager, ...) is an
// instance-owned object a caller explicitly holds and passes by reference —
// there is no global, ambiently-reachable mutable state anywhere for an
// unrelated caller to silently repoint. Introducing a global operator-new
// override (the only way to literally intercept "every std::vector/new in
// the whole program", short of a hook table) would reintroduce precisely
// that problem, and would not even be fully portable across this project's
// target platforms (macOS's two-level namespace, MSVC's own linker model —
// the identical portability motivation c-RCP's own alloc.h header comment
// gives for why it built a function-pointer indirection in the first place,
// rather than relying on malloc-symbol interposition).
//
// ── Design ────────────────────────────────────────────────────────────────
// FaultInjector below follows rcp/faultinject.hpp's own idiom exactly: a
// small, instance-owned, std::mutex-guarded class a caller constructs, holds
// (typically via reference or pointer, dependency-injected into whatever it
// is testing), and explicitly arms/disarms — never a singleton, never
// ambiently reachable. It does not attempt to intercept allocation
// transparently; instead, a specific, already-allocating call site opts in
// by calling FaultInjector::should_fail() immediately before it would
// otherwise allocate, and takes that as a signal to report a simulated
// failure through this codebase's own std::error_code convention (matching
// this codebase's established convention elsewhere — see rcp/request.hpp's
// RequestErrc, rcp/watchdog.hpp's WatchdogErrc, etc. — rather than throwing,
// since nothing in this codebase currently catches an allocation-related
// exception at all).
//
// ── Scope: Phase 1's bounded-capacity paths were audited, not wired in ──────
// The task guidance for this pass named four Phase 1 fixed-capacity modules
// as candidates for fault-injection integration: rcp/request.hpp's
// BoundedVector/RequestLedger, rcp/fragment.hpp's Reassembler, rcp/
// respqueue.hpp's RespQueue, and rcp/loan.hpp's BufferPool's *free-list*
// bookkeeping. All four were already re-read for this pass (test_request.cpp
// "fixed-capacity" tests, test_fragment.cpp's "exceeding max_total_len"/
// "exceeding this Reassembler's own fixed capacity" tests, test_respqueue.cpp
// capacity_octets/kMaxEntries tests, test_loan.cpp's "beyond the free list's
// fixed capacity" test) and found already directly, adequately tested — and,
// more fundamentally, each of those specific bounded-capacity-exceeded paths
// backs a compile-time-sized std::array (BoundedVector<T,N>'s std::array<T,N>
// data_, Reassembler's own fixed segment buffer, RespQueue's kMaxEntries
// array, BufferPool's kPoolMaxEntries free-list array): reaching capacity is
// detected by a plain size_ >= Capacity comparison *before* touching memory,
// not by attempting and failing a heap allocation. There is no allocation on
// those specific paths for a fault injector to intercept — wiring one in
// would be inert by construction, exactly the "artificial integration into
// Phase 1's already-solid modules" the task guidance says to avoid in that
// case.
//
// What genuinely does allocate, and is not already fault-injectable, is
// rcp/loan.hpp's BufferPool::loan() *cache-miss* path — `new
// std::vector<uint8_t>(size, 0)`, a real heap allocation whose failure this
// codebase currently cannot simulate at all. BufferPool now optionally
// accepts a FaultInjector* (default nullptr, fully backward compatible —
// see loan.hpp's own header comment) so that one genuine allocation call
// site is fault-injectable end to end, as a concrete, non-artificial
// demonstration that this seam actually composes with a real caller rather
// than existing only in isolation. FaultInjector itself is otherwise a
// general-purpose, ready-to-use utility for future phases' own allocating
// call sites (e.g. Phase 4's mock.hpp/regmap.hpp dispatch tables), matching
// this file's own "smaller, more general-purpose FaultInjector utility"
// fallback design the task guidance names explicitly.
//
// ── REQ-ALLOC-* transfer audit (c-RCP .fusa-reqs.json, 11 requirements) ─────
// c-RCP's REQ-ALLOC-001..011 describe rcp_alloc_set_hooks()'s specific
// mechanics (a global hook table, its NULL-means-reset convenience, its
// malloc/calloc/realloc/free passthrough shape, and REQ-ALLOC-007..011's
// rcp_alloc_lock_hooks()/_unlock_hooks()/_hooks_locked() access-control
// retrofit). None of the eleven transfer as written: REQ-ALLOC-001/002/010/
// 011 (install/reset hooks, rejected-while-locked) describe a global
// mutable-table API this design deliberately does not have; REQ-ALLOC-
// 003/004/005/006 (malloc/calloc/realloc/free passthrough-or-hook
// semantics) describe C's own four-allocator-function surface, which C++
// does not have as separate entry points (std::vector/std::make_unique/`new`
// each pick their own allocation strategy internally; there is no equivalent
// single indirection point to hook transparently without a global operator-
// new override — see this file's "Why this is NOT a port" section above);
// REQ-ALLOC-007/008/009 (lock/unlock/query-locked) exist purely to retrofit
// access control onto REQ-ALLOC-001/002's own global table, so they have no
// referent once that table doesn't exist — an instance-owned FaultInjector
// only affects the specific call sites a caller explicitly constructed it
// into, so there is no shared, ambiently-reachable state to protect from
// unrelated callers in the first place; the access-control problem those
// three requirements solve for c-RCP's design does not arise here by
// construction. The one *concept* that does transfer is REQ-ALLOC-003/004/
// 006's underlying intent — "give the test suite a portable, deterministic
// way to force an allocation-failure branch without a real,
// practically-unreachable OOM condition or a non-portable malloc-
// interposition trick" — which is exactly what FaultInjector::should_fail()
// below provides, just via an opt-in call at the allocation site rather than
// a transparent global hook.
#pragma once

#include <cstddef>
#include <mutex>
#include <string>
#include <system_error>

namespace rcp {
namespace alloc {

// ── Errors ────────────────────────────────────────────────────────────────────

enum class AllocErrc : int {
    simulated_allocation_failure = 1, // an armed FaultInjector reported this attempt should fail
};

inline const std::error_category& alloc_category() noexcept {
    struct Cat : std::error_category {
        const char* name() const noexcept override { return "rcp.alloc"; }
        std::string message(int ev) const override {
            switch (static_cast<AllocErrc>(ev)) {
            case AllocErrc::simulated_allocation_failure:
                return "rcp/alloc: FaultInjector simulated an allocation failure";
            default:
                return "rcp/alloc: unknown error";
            }
        }
    };
    static Cat instance;
    return instance;
}

inline std::error_code make_error_code(AllocErrc e) noexcept {
    return {static_cast<int>(e), alloc_category()};
}

// ── FaultInjector ─────────────────────────────────────────────────────────────
// Instance-owned, std::mutex-guarded deterministic allocation-failure
// trigger — see this file's header comment for the full design rationale.
// Not a singleton: a caller constructs one, holds it (typically passing a
// pointer or reference into whatever it wants to fault-inject), and arms it
// explicitly. With nothing armed, should_fail() always returns false — a
// FaultInjector nobody has armed is a silent no-op, matching rcp/
// faultinject.hpp's Interceptor's own "no active rule -> pass straight
// through" default.
class FaultInjector {
public:
    // arm schedules should_fail() to report failure for the next `count`
    // calls (default 1); count < 0 arms it indefinitely (every future
    // should_fail() call reports failure until disarm()), matching
    // rcp::faultinject::Rule::count's identical -1-means-forever
    // convention. arm(0) disarms (equivalent to disarm()).
    void arm(int count = 1) {
        std::lock_guard<std::mutex> lk(mu_);
        remaining_ = count;
    }

    void disarm() {
        std::lock_guard<std::mutex> lk(mu_);
        remaining_ = 0;
    }

    // armed reports whether the next should_fail() call would currently
    // report failure.
    bool armed() const {
        std::lock_guard<std::mutex> lk(mu_);
        return remaining_ != 0;
    }

    // should_fail is called by an opted-in allocation call site immediately
    // before it would otherwise perform a real allocation. Returns true
    // (and, unless armed indefinitely, consumes one count) exactly when
    // that attempt should simulate failure instead of allocating; the
    // caller is expected to translate that into its own failure signal
    // (typically make_error_code(AllocErrc::simulated_allocation_failure),
    // or a module-specific std::error_code of its own) rather than
    // proceeding with the real allocation.
    bool should_fail() noexcept {
        std::lock_guard<std::mutex> lk(mu_);
        if (remaining_ == 0) return false;
        if (remaining_ > 0) --remaining_;
        return true;
    }

    // remaining reports how many more should_fail() calls will currently
    // report failure (-1 if armed indefinitely, 0 if disarmed). Test
    // introspection, not part of the arm/should_fail contract itself.
    int remaining() const {
        std::lock_guard<std::mutex> lk(mu_);
        return remaining_;
    }

private:
    mutable std::mutex mu_;
    int                 remaining_ = 0; // 0 = disarmed; -1 = fires forever; >0 = fires N more times
};

} // namespace alloc
} // namespace rcp

// Enable std::error_code construction from rcp::alloc::AllocErrc.
namespace std {
template <>
struct is_error_code_enum<rcp::alloc::AllocErrc> : true_type {};
} // namespace std
