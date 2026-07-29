// fusa:test REQ-LOAN-001
// fusa:test REQ-LOAN-002
// fusa:test REQ-LOAN-003
// fusa:test REQ-LOAN-004
// fusa:test REQ-LOAN-005
// fusa:test REQ-LOAN-006

// Tests for rcp/loan.hpp — BufferPool, the zero-copy payload-loaning
// primitive (ROADMAP.md milestone 58, "Auxiliary Transport & Cross-Cutting
// Rebind", v2.14.0).

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
