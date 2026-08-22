// fusa:req REQ-LOAN-001
// fusa:req REQ-LOAN-002
// fusa:req REQ-LOAN-003
// fusa:req REQ-LOAN-004
// fusa:req REQ-LOAN-005
// fusa:req REQ-LOAN-006
// fusa:req REQ-LOAN-008
// fusa:req REQ-LOAN-009

// BufferPool — zero-copy payload loaning for AVTPDU-framed request/response
// construction, via a pool of pre-allocated byte vectors.
//
// ROADMAP.md milestone 58, "Auxiliary Transport & Cross-Cutting Rebind
// (v2.14.0)": this header is ADAPTed, per the Satellite Package
// Disposition table's entry for `loan.hpp` — the pre-replacement
// loan::Controller wrapped rcp::LoaningController, a chokepoint that no
// longer exists (there is no unified client-side send() interface yet;
// that unification, if any, does not land until the CLI/capi/adapt
// rebuilds at v2.16.0 — see rcp/authz.hpp's equivalent v2.11.0 note). This
// rebind drops the wrapper and keeps the pool as a standalone primitive a
// caller draws a buffer from immediately before building an
// AVTPDU/ACF-framed request (rcp/avtp.hpp, rcp/acf.hpp) — e.g. the
// req_payload/out_resp_payload arguments to rcp/udp.hpp's Client::request
// — same "primitives driven by the embedding application" convention every
// Phase 14/15 header has used since v2.9.0. This likely matters more now
// than it did against the old 16-byte header: AVTPDU + ACF_ABB/ACF_GBB
// framing carries more bytes worth avoiding a fresh heap allocation for on
// a hot request path.
//
// loan() obtains a zeroed buffer from the pool; the returned rcp::Loan (a
// generic RAII buffer holder — not itself part of the pre-replacement
// Zone/Command/Controller/Registry model rcp/rcp.hpp's own header comment
// warns against building on) returns its buffer to the pool automatically
// once it goes out of scope, or immediately via its own ret() method.
//
// ── Phase 17 fixed-capacity conversion (cpp-RCP issue #129) ─────────────────
//
// Ported from c-RCP's include/rcp/loan.h + src/loan.c, this project's
// RC5-spec-conformant reference implementation for this module: c-RCP's own
// rcp_loan_pool_t bounds its free-list bookkeeping to a fixed
// RCP_LOAN_POOL_MAX_ENTRIES (64) array embedded directly in the pool
// struct, not a realloc()-grown container — "once the free list already
// holds this many returned buffers, a further release simply frees the
// returned buffer outright instead of pooling it" (c-RCP's own loan.h
// comment). Only the ENTRY-COUNT bound is fixed on the c-RCP side; each
// individual pooled buffer's own payload bytes stay heap-allocated (sized
// by the caller's own runtime `size` argument, not a compile-time protocol
// constant) — c-RCP does not additionally cap payload size.
//
// This header previously diverged from that design in the one dimension
// that matters most for an ASIL-D-oriented no-dynamic-allocation-growth
// posture: BufferPool's own free list (`pool_`) was a plain
// `std::vector<std::unique_ptr<std::vector<uint8_t>>>` with no capacity
// ceiling at all — unlike every other fixed-capacity table this codebase
// now uses (rcp/respqueue.hpp's kMaxEntries, this same phase), it could
// grow without bound as buffers were returned. Fixed below: `entries_` is
// now a `std::array<PoolEntry, kPoolMaxEntries>` of exactly
// c-RCP's own RCP_LOAN_POOL_MAX_ENTRIES (64) — matching c-RCP's chosen
// entry-count bound exactly, not inventing a stricter or looser one — with
// each individual buffer's own payload bytes still heap-allocated exactly
// as c-RCP's own design leaves them (matching, not tightening, that half of
// the design). Once the free list already holds kPoolMaxEntries returned
// buffers, a further release() call frees the returned buffer outright
// (see loan()'s own release closure below) instead of growing the free
// list further — the same degradation c-RCP's own pool_append() documents,
// never a leak or corruption, only a forfeited reuse opportunity.
//
// ── Phase 17 allocation fault-injection seam (cpp-RCP issue #129) ───────────
// loan()'s cache-miss branch below (`new std::vector<uint8_t>(size, 0)`) is
// a genuine heap allocation this codebase previously had no way to
// deterministically fail in a test -- unlike this file's own kPoolMaxEntries
// bound above, which is a size_ >= Capacity comparison with nothing to
// allocate at all. BufferPool now optionally accepts an rcp::alloc::
// FaultInjector* (default nullptr -- fully backward compatible; every
// existing construction site, including new_buffer_pool(), is unaffected)
// so that call site can be exercised via fault injection. See rcp/
// alloc.hpp's own header comment for why this is an opt-in, instance-owned
// seam rather than a global allocator hook.
//
// ── Loan lifetime independent of BufferPool (audit fix) ─────────────────────
// BufferPool's own free-list bookkeeping (the fixed-capacity array above,
// its mutex, entries_len_, and the fault_injector_ seam) lives in a private
// Impl struct held via std::shared_ptr, not directly as BufferPool data
// members. loan()'s release closure captures that shared_ptr<Impl> *by
// value*, not `this` (the BufferPool*): a Loan handed out by loan() may
// therefore safely outlive the BufferPool object it was drawn from — e.g. a
// Loan stored in an outer scope while the BufferPool that produced it goes
// out of scope (and destructs) first. Impl's storage is only actually freed
// once its last owner — the BufferPool wrapper itself, or the release
// closure of the last still-outstanding Loan — releases its
// shared_ptr<Impl>, never while any Loan drawn from this pool could still
// reach it. This does not change close()'s own documented behavior (still
// idempotent, still safe with outstanding Loans alive) — see close()'s own
// doc comment below.
#pragma once

#include "alloc.hpp"
#include "rcp.hpp"

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

namespace rcp {
namespace loan {

// Ported from c-RCP's RCP_LOAN_POOL_MAX_ENTRIES — see this file's own
// header comment for the fixed-capacity conversion this bound closes.
constexpr size_t kPoolMaxEntries = 64;

class BufferPool {
public:
    BufferPool() = default;

    // fault_injector, when non-null, is consulted (should_fail()) before
    // this pool's one genuine allocation call site — the cache-miss branch
    // of loan() below — actually allocates. Optional and defaulted to
    // nullptr so every pre-existing construction site (including
    // new_buffer_pool()) is unaffected; see this file's own header comment.
    // fault_injector is not owned by this pool and must outlive it.
    explicit BufferPool(alloc::FaultInjector* fault_injector)
        : impl_(std::make_shared<Impl>(fault_injector)) {}

    // Non-copyable: mirrors this class's shape before the shared_ptr<Impl>
    // refactor below, where non-copyable members (std::mutex, std::atomic)
    // made copying implicitly deleted. Also non-movable: the user-declared
    // destructor below suppresses the implicit move members, exactly as it
    // did before this refactor.
    BufferPool(const BufferPool&) = delete;
    BufferPool& operator=(const BufferPool&) = delete;

    ~BufferPool() {
        // Marks the pool closed. Impl's own storage (the free list, its
        // mutex, fault_injector_) is only actually torn down once the last
        // owning std::shared_ptr<Impl> — this member's own impl_, or the
        // release closure of any Loan drawn from this pool that is still
        // alive — releases it. See this file's "Loan lifetime independent
        // of BufferPool" header comment.
        close();
    }

    // loan returns a zeroed buffer of exactly size bytes, drawn from the
    // pool if a same-or-larger buffer is available for reuse, or freshly
    // allocated otherwise. Returns alloc::AllocErrc::simulated_allocation_
    // failure, unchanged, if a cache-miss allocation would be needed and
    // this pool's fault_injector (if any) reports it should fail —
    // the pool's own state is left exactly as it was before the call in
    // that case (no partial buffer taken from the free list is lost: the
    // free-list search below only removes an entry on a cache *hit*, which
    // never reaches the fault-injection check at all).
    std::error_code loan(int size, std::unique_ptr<rcp::Loan>& out) {
        if (impl_->closed_.load(std::memory_order_acquire)) return ErrClosed;
        if (size < 0) return std::make_error_code(std::errc::invalid_argument);

        std::vector<uint8_t>* raw = nullptr;
        {
            std::lock_guard<std::mutex> lk(impl_->pool_mu_);
            for (size_t i = 0; i < impl_->entries_len_; i++) {
                if (impl_->entries_[i]->size() >= static_cast<size_t>(size)) {
                    raw = impl_->entries_[i];
                    impl_->entries_[i] = impl_->entries_[impl_->entries_len_ - 1];
                    impl_->entries_len_--;
                    break;
                }
            }
        }

        if (raw) {
            raw->assign(static_cast<size_t>(size), 0); // re-zero: no stale data leaks across reuse
        } else {
            if (impl_->fault_injector_ && impl_->fault_injector_->should_fail())
                return alloc::make_error_code(alloc::AllocErrc::simulated_allocation_failure);
            raw = new std::vector<uint8_t>(static_cast<size_t>(size), 0);
        }

        // The release closure captures impl_ (the shared_ptr itself, by
        // value) rather than `this`: a Loan may legitimately outlive the
        // BufferPool object that handed it out (see this file's "Loan
        // lifetime independent of BufferPool" header comment), and impl_'s
        // refcount is what keeps the pool's free list / mutex /
        // fault_injector_ alive for exactly as long as any outstanding
        // Loan still needs them, even after this BufferPool wrapper itself
        // has been destroyed.
        auto impl = impl_;
        out = std::make_unique<rcp::Loan>(
            *raw, // Loan owns its own copy of the payload (rcp::Loan's own by-value contract)
            [impl, raw]() mutable {
                std::lock_guard<std::mutex> lk(impl->pool_mu_);
                if (impl->entries_len_ < kPoolMaxEntries) {
                    impl->entries_[impl->entries_len_] = raw;
                    impl->entries_len_++;
                    return;
                }
                // Free list already at c-RCP's own RCP_LOAN_POOL_MAX_ENTRIES
                // bound (see this file's header comment): no reuse
                // possible, free the buffer outright rather than growing
                // entries_ past its fixed capacity.
                delete raw;
            });
        return {};
    }

    // close is idempotent — safe to call more than once, including while
    // Loans obtained before the call are still alive (their eventual
    // release simply grows a pool nobody will draw from again, up to
    // kPoolMaxEntries) — and it remains safe to call from this BufferPool's
    // own destructor even with outstanding Loans alive, since it sets a
    // flag on Impl (kept alive by shared_ptr refcounting), never
    // dereferences a dangling `this`.
    void close() { impl_->closed_.store(true, std::memory_order_release); }

    bool ok() const noexcept { return !impl_->closed_.load(std::memory_order_acquire); }

    // pooled_count reports how many released buffers are currently held
    // for reuse — introspection for tests, not part of the loan/release
    // contract itself. Always <= kPoolMaxEntries, by construction.
    size_t pooled_count() const {
        std::lock_guard<std::mutex> lk(impl_->pool_mu_);
        return impl_->entries_len_;
    }

private:
    // Impl holds every piece of this pool's state that loan()'s release
    // closure needs to reach after a successful loan() call: the fixed-
    // capacity free list, its mutex, and the (non-owned) fault_injector_.
    // BufferPool itself is now just a thin std::shared_ptr<Impl> wrapper;
    // splitting the state out this way is what lets the release closure
    // capture impl_ (the shared_ptr) *by value* instead of `this` (see
    // loan()'s own comment above) — the closure's own copy of impl_ keeps
    // this storage alive by refcount for as long as the Loan itself is
    // alive, independent of whether the BufferPool wrapper that produced
    // it still exists. See this file's "Loan lifetime independent of
    // BufferPool" header comment.
    struct Impl {
        std::atomic<bool> closed_{false};
        std::mutex pool_mu_;
        // Fixed-capacity free list (ported from c-RCP's RCP_LOAN_POOL_MAX_ENTRIES
        // — see this file's own header comment): entries_ is a plain
        // std::array of raw pointers, not a realloc()/std::vector-grown
        // container — the SLOTS are static, each individual buffer's own bytes
        // are not (matching c-RCP's own design exactly).
        std::array<std::vector<uint8_t>*, kPoolMaxEntries> entries_{};
        size_t                                              entries_len_ = 0; // always <= kPoolMaxEntries

        alloc::FaultInjector* fault_injector_ = nullptr; // not owned; see BufferPool's constructor doc comment

        explicit Impl(alloc::FaultInjector* fault_injector) : fault_injector_(fault_injector) {}

        // Frees whatever's still in the free list once the last owner of
        // this Impl — this BufferPool's own impl_ member, or the release
        // closure of the last still-outstanding Loan — releases its
        // shared_ptr<Impl>.
        ~Impl() {
            for (size_t i = 0; i < entries_len_; i++) delete entries_[i];
        }
    };

    std::shared_ptr<Impl> impl_ = std::make_shared<Impl>(nullptr);
};

inline std::unique_ptr<BufferPool> new_buffer_pool() {
    return std::make_unique<BufferPool>();
}

} // namespace loan
} // namespace rcp
