// fusa:test REQ-LOAN-001
// fusa:test REQ-LOAN-002
// fusa:test REQ-LOAN-003
// fusa:test REQ-LOAN-004
// fusa:test REQ-LOAN-005
// fusa:test REQ-LOAN-006
// fusa:test REQ-LOAN-008
// fusa:test REQ-LOAN-009

// Tests for rcp/loan.hpp — BufferPool, the zero-copy payload-loaning
// primitive (ROADMAP.md milestone 58, "Auxiliary Transport & Cross-Cutting
// Rebind", v2.14.0; Phase 17 fixed-capacity conversion, cpp-RCP issue
// #129, ported from c-RCP's RCP_LOAN_POOL_MAX_ENTRIES).
//
// Not ported: c-RCP's own test_acquire_returns_null_when_loan_struct_
// allocation_fails / test_acquire_returns_null_when_release_ctx_
// allocation_fails have no C++ analog -- cpp-RCP has no
// allocation-failure-injection seam (c-RCP's rcp/alloc.h hook table is
// C-only), and std::make_unique/`new` cannot be made to fail
// deterministically and portably the way those tests do.

#include <catch2/catch_test_macros.hpp>
#include <rcp/loan.hpp>

using namespace rcp;

// ── loan() ────────────────────────────────────────────────────────────────────

TEST_CASE("loan::BufferPool::loan returns a zeroed buffer of exactly size bytes",
          "[loan][REQ-LOAN-001]") {
    loan::BufferPool pool;

    std::unique_ptr<Loan> loan_out;
    REQUIRE_FALSE(pool.loan(16, loan_out));
    REQUIRE(loan_out != nullptr);
    REQUIRE(loan_out->payload.size() == 16);
    for (auto b : loan_out->payload) REQUIRE(b == 0);
}

TEST_CASE("loan::BufferPool::loan returns ErrClosed once the pool is closed",
          "[loan][REQ-LOAN-002]") {
    loan::BufferPool pool;
    pool.close();

    std::unique_ptr<Loan> loan_out;
    REQUIRE(pool.loan(8, loan_out) == ErrClosed);
}

TEST_CASE("loan::BufferPool::loan returns an error for negative size",
          "[loan][REQ-LOAN-003]") {
    loan::BufferPool pool;

    std::unique_ptr<Loan> loan_out;
    auto ec = pool.loan(-1, loan_out);
    REQUIRE(ec == std::make_error_code(std::errc::invalid_argument));
}

// ── Pool reuse ────────────────────────────────────────────────────────────────

TEST_CASE("A released Loan's buffer becomes available for a subsequent loan() to reuse",
          "[loan][REQ-LOAN-004]") {
    loan::BufferPool pool;
    REQUIRE(pool.pooled_count() == 0);

    {
        std::unique_ptr<Loan> loan_out;
        REQUIRE_FALSE(pool.loan(64, loan_out));
    } // destructor releases the buffer back to the pool

    REQUIRE(pool.pooled_count() == 1);

    std::unique_ptr<Loan> loan_out2;
    REQUIRE_FALSE(pool.loan(64, loan_out2)); // drawn from the pool, not freshly allocated
    REQUIRE(pool.pooled_count() == 0);
}

// ── Re-zeroing on reuse ───────────────────────────────────────────────────────

TEST_CASE("A released buffer is re-zeroed before being handed out again",
          "[loan][REQ-LOAN-005]") {
    loan::BufferPool pool;

    {
        std::unique_ptr<Loan> loan_out;
        REQUIRE_FALSE(pool.loan(4, loan_out));
        loan_out->payload = {0xDE, 0xAD, 0xBE, 0xEF}; // stamp with non-zero data
    } // released back to the pool with that data still present

    std::unique_ptr<Loan> loan_out2;
    REQUIRE_FALSE(pool.loan(4, loan_out2));
    for (auto b : loan_out2->payload) REQUIRE(b == 0); // no stale data leaked across reuse
}

// ── Idempotent close ──────────────────────────────────────────────────────────

TEST_CASE("loan::BufferPool::close is idempotent and safe with outstanding Loans still alive",
          "[loan][REQ-LOAN-006]") {
    loan::BufferPool pool;
    std::unique_ptr<Loan> loan_out;
    REQUIRE_FALSE(pool.loan(8, loan_out));

    pool.close();
    pool.close(); // second call must not crash

    // The still-alive Loan's eventual release (destructor below) must not
    // crash even though the pool has already been closed.
    loan_out.reset();
}

// ── new_buffer_pool() ────────────────────────────────────────────────────────

TEST_CASE("new_buffer_pool returns a valid, open, empty pool", "[loan][REQ-LOAN-009]") {
    auto pool = loan::new_buffer_pool();
    REQUIRE(pool != nullptr);
    REQUIRE(pool->ok());
    REQUIRE(pool->pooled_count() == 0);

    std::unique_ptr<Loan> loan_out;
    REQUIRE_FALSE(pool->loan(8, loan_out));
    REQUIRE(loan_out != nullptr);
}

// ── pool destruction frees every free-listed buffer (REQ-LOAN-008) ─────────

TEST_CASE("BufferPool destruction frees every buffer still in the free list",
          "[loan][REQ-LOAN-008]") {
    loan::BufferPool pool;

    for (int i = 0; i < 8; i++) {
        std::unique_ptr<Loan> loan_out;
        REQUIRE_FALSE(pool.loan(16 * (i + 1), loan_out));
        loan_out.reset(); // returns the buffer to the pool's free list
    }
    REQUIRE(pool.pooled_count() == 8);

    // pool's destructor (end of scope) must free every remaining
    // free-listed buffer without leaking (ASan-checked in CI) -- nothing
    // further to assert here beyond "does not crash", matching c-RCP's own
    // test_pool_destroy_frees_every_returned_buffer.
}

// ── Fixed-capacity free list (Phase 17: was an unbounded std::vector) ──────

TEST_CASE("A released buffer beyond the free list's fixed capacity is freed outright, "
          "and the pool remains fully usable",
          "[loan][REQ-LOAN-004]") {
    loan::BufferPool pool;
    std::vector<std::unique_ptr<Loan>> loans;
    loans.reserve(loan::kPoolMaxEntries);

    // Distinct sizes: every loan() below is a fresh allocation (the free
    // list starts empty, so nothing to reuse yet).
    for (size_t i = 0; i < loan::kPoolMaxEntries; i++) {
        std::unique_ptr<Loan> loan_out;
        REQUIRE_FALSE(pool.loan(static_cast<int>(i + 1), loan_out));
        loans.push_back(std::move(loan_out));
    }
    for (auto& l : loans) l.reset(); // free list now holds exactly kPoolMaxEntries
    REQUIRE(pool.pooled_count() == loan::kPoolMaxEntries);

    // A size larger than every already-freed capacity: guaranteed not to
    // reuse, so this is a genuinely fresh allocation whose eventual
    // release is the one that finds the free list already full.
    std::unique_ptr<Loan> overflow_loan;
    REQUIRE_FALSE(pool.loan(static_cast<int>(loan::kPoolMaxEntries) + 1, overflow_loan));
    overflow_loan.reset(); // free list full: buffer freed outright (not pooled)

    // pooled_count must not have grown past kPoolMaxEntries, and the pool
    // must still be fully usable afterward.
    REQUIRE(pool.pooled_count() == loan::kPoolMaxEntries);

    std::unique_ptr<Loan> reacquired;
    REQUIRE_FALSE(pool.loan(1, reacquired));
    REQUIRE(reacquired != nullptr);
}
