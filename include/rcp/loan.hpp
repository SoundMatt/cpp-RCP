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
#pragma once

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
    ~BufferPool() {
        close();
        for (size_t i = 0; i < entries_len_; i++) delete entries_[i];
    }

    // loan returns a zeroed buffer of exactly size bytes, drawn from the
    // pool if a same-or-larger buffer is available for reuse, or freshly
    // allocated otherwise.
    std::error_code loan(int size, std::unique_ptr<rcp::Loan>& out) {
        if (closed_.load(std::memory_order_acquire)) return ErrClosed;
        if (size < 0) return std::make_error_code(std::errc::invalid_argument);

        std::vector<uint8_t>* raw = nullptr;
        {
            std::lock_guard<std::mutex> lk(pool_mu_);
            for (size_t i = 0; i < entries_len_; i++) {
                if (entries_[i]->size() >= static_cast<size_t>(size)) {
                    raw = entries_[i];
                    entries_[i] = entries_[entries_len_ - 1];
                    entries_len_--;
                    break;
                }
            }
        }

        if (raw) {
            raw->assign(static_cast<size_t>(size), 0); // re-zero: no stale data leaks across reuse
        } else {
            raw = new std::vector<uint8_t>(static_cast<size_t>(size), 0);
        }

        out = std::make_unique<rcp::Loan>(
            *raw, // Loan owns its own copy of the payload (rcp::Loan's own by-value contract)
            [this, raw]() mutable {
                std::lock_guard<std::mutex> lk(pool_mu_);
                if (entries_len_ < kPoolMaxEntries) {
                    entries_[entries_len_] = raw;
                    entries_len_++;
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
    // kPoolMaxEntries).
    void close() { closed_.store(true, std::memory_order_release); }

    bool ok() const noexcept { return !closed_.load(std::memory_order_acquire); }

    // pooled_count reports how many released buffers are currently held
    // for reuse — introspection for tests, not part of the loan/release
    // contract itself. Always <= kPoolMaxEntries, by construction.
    size_t pooled_count() const {
        std::lock_guard<std::mutex> lk(pool_mu_);
        return entries_len_;
    }

private:
    std::atomic<bool> closed_{false};
    mutable std::mutex pool_mu_;
    // Fixed-capacity free list (ported from c-RCP's RCP_LOAN_POOL_MAX_ENTRIES
    // — see this file's own header comment): entries_ is a plain
    // std::array of raw pointers, not a realloc()/std::vector-grown
    // container — the SLOTS are static, each individual buffer's own bytes
    // are not (matching c-RCP's own design exactly).
    std::array<std::vector<uint8_t>*, kPoolMaxEntries> entries_{};
    size_t                                              entries_len_ = 0; // always <= kPoolMaxEntries
};

inline std::unique_ptr<BufferPool> new_buffer_pool() {
    return std::make_unique<BufferPool>();
}

} // namespace loan
} // namespace rcp
