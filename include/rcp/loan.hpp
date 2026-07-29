// fusa:req REQ-LOAN-001
// fusa:req REQ-LOAN-002
// fusa:req REQ-LOAN-003
// fusa:req REQ-LOAN-004
// fusa:req REQ-LOAN-005
// fusa:req REQ-LOAN-006

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
#pragma once

#include "rcp.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

namespace rcp {
namespace loan {

class BufferPool {
public:
    ~BufferPool() { close(); }

    // loan returns a zeroed buffer of exactly size bytes, drawn from the
    // pool if a same-or-larger buffer is available for reuse, or freshly
    // allocated otherwise.
    std::error_code loan(int size, std::unique_ptr<rcp::Loan>& out) {
        if (closed_.load(std::memory_order_acquire)) return ErrClosed;
        if (size < 0) return std::make_error_code(std::errc::invalid_argument);

        std::vector<uint8_t> buf;
        {
            std::lock_guard<std::mutex> lk(pool_mu_);
            if (!pool_.empty()) {
                buf = std::move(*pool_.back());
                pool_.pop_back();
            }
        }
        buf.assign(static_cast<size_t>(size), 0); // re-zero: no stale data leaks across reuse

        auto* raw = new std::vector<uint8_t>(std::move(buf));
        out = std::make_unique<rcp::Loan>(
            *raw,
            [this, raw]() mutable {
                std::lock_guard<std::mutex> lk(pool_mu_);
                pool_.push_back(std::unique_ptr<std::vector<uint8_t>>(raw));
            });
        return {};
    }

    // close is idempotent — safe to call more than once, including while
    // Loans obtained before the call are still alive (their eventual
    // release simply grows a pool nobody will draw from again).
    void close() { closed_.store(true, std::memory_order_release); }

    bool ok() const noexcept { return !closed_.load(std::memory_order_acquire); }

    // pooled_count reports how many released buffers are currently held
    // for reuse — introspection for tests, not part of the loan/release
    // contract itself.
    size_t pooled_count() const {
        std::lock_guard<std::mutex> lk(pool_mu_);
        return pool_.size();
    }

private:
    std::atomic<bool> closed_{false};
    mutable std::mutex pool_mu_;
    std::vector<std::unique_ptr<std::vector<uint8_t>>> pool_;
};

inline std::unique_ptr<BufferPool> new_buffer_pool() {
    return std::make_unique<BufferPool>();
}

} // namespace loan
} // namespace rcp
