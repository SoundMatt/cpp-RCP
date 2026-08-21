// Tests for rcp/alloc.hpp's FaultInjector — the deterministic
// allocation-failure fault-injection seam (Phase 17, cpp-RCP issue #129).
// See rcp/alloc.hpp's own header comment for the design rationale and the
// REQ-ALLOC-* transfer audit against c-RCP's alloc.h/alloc.c.

#include <catch2/catch_test_macros.hpp>

#include "rcp/alloc.hpp"
#include "rcp/loan.hpp"

using rcp::alloc::AllocErrc;
using rcp::alloc::FaultInjector;

// ── FaultInjector — disarmed by default ──────────────────────────────────────

TEST_CASE("FaultInjector::should_fail always returns false with nothing armed", "[alloc]") {
    FaultInjector fi;
    REQUIRE_FALSE(fi.armed());
    REQUIRE(fi.remaining() == 0);
    REQUIRE_FALSE(fi.should_fail());
    REQUIRE_FALSE(fi.should_fail());
}

// ── arm(count) — fires exactly `count` times, then reverts to disarmed ──────

TEST_CASE("FaultInjector::arm(1) fires exactly once, then reverts to disarmed", "[alloc]") {
    FaultInjector fi;
    fi.arm();
    REQUIRE(fi.armed());
    REQUIRE(fi.remaining() == 1);

    REQUIRE(fi.should_fail());
    REQUIRE_FALSE(fi.armed());
    REQUIRE(fi.remaining() == 0);
    REQUIRE_FALSE(fi.should_fail()); // consumed; back to passthrough
}

TEST_CASE("FaultInjector::arm(N) fires exactly N times", "[alloc]") {
    FaultInjector fi;
    fi.arm(3);

    REQUIRE(fi.should_fail());
    REQUIRE(fi.remaining() == 2);
    REQUIRE(fi.should_fail());
    REQUIRE(fi.remaining() == 1);
    REQUIRE(fi.should_fail());
    REQUIRE(fi.remaining() == 0);

    REQUIRE_FALSE(fi.should_fail()); // exhausted
    REQUIRE_FALSE(fi.armed());
}

// ── arm(-1) — fires indefinitely until disarm() ──────────────────────────────

TEST_CASE("FaultInjector::arm(-1) fires indefinitely until disarm()", "[alloc]") {
    FaultInjector fi;
    fi.arm(-1);
    REQUIRE(fi.remaining() == -1);

    for (int i = 0; i < 25; ++i) {
        REQUIRE(fi.should_fail());
        REQUIRE(fi.remaining() == -1); // never decrements while armed forever
    }

    fi.disarm();
    REQUIRE_FALSE(fi.armed());
    REQUIRE_FALSE(fi.should_fail());
}

// ── disarm() / re-arm() ───────────────────────────────────────────────────────

TEST_CASE("FaultInjector::disarm before any should_fail call cancels a pending arm", "[alloc]") {
    FaultInjector fi;
    fi.arm(5);
    fi.disarm();
    REQUIRE_FALSE(fi.should_fail());
}

TEST_CASE("FaultInjector can be re-armed after exhausting a prior arm", "[alloc]") {
    FaultInjector fi;
    fi.arm(1);
    REQUIRE(fi.should_fail());
    REQUIRE_FALSE(fi.should_fail());

    fi.arm(2);
    REQUIRE(fi.should_fail());
    REQUIRE(fi.should_fail());
    REQUIRE_FALSE(fi.should_fail());
}

TEST_CASE("FaultInjector::arm(0) is equivalent to disarm", "[alloc]") {
    FaultInjector fi;
    fi.arm(4);
    fi.arm(0);
    REQUIRE_FALSE(fi.armed());
    REQUIRE_FALSE(fi.should_fail());
}

// ── Error code plumbing ───────────────────────────────────────────────────────

TEST_CASE("AllocErrc::simulated_allocation_failure round-trips through std::error_code", "[alloc]") {
    std::error_code ec = rcp::alloc::make_error_code(AllocErrc::simulated_allocation_failure);
    REQUIRE(ec == AllocErrc::simulated_allocation_failure);
    REQUIRE(static_cast<bool>(ec));
    REQUIRE(std::string(ec.category().name()) == "rcp.alloc");
    REQUIRE_FALSE(ec.message().empty());
}

// ── Integration: rcp/loan.hpp's BufferPool cache-miss allocation ────────────
// The one genuine (non-bounded-capacity) allocation call site this pass
// wired the seam into — see rcp/loan.hpp's own header comment.

TEST_CASE("BufferPool without a FaultInjector allocates normally (backward compatible)", "[alloc][loan]") {
    rcp::loan::BufferPool pool; // default construction, unaffected by this pass
    std::unique_ptr<rcp::Loan> l;
    REQUIRE_FALSE(pool.loan(64, l));
    REQUIRE(l != nullptr);
}

TEST_CASE("An armed FaultInjector makes BufferPool::loan's cache-miss allocation fail", "[alloc][loan]") {
    FaultInjector fi;
    rcp::loan::BufferPool pool(&fi);

    fi.arm(1);
    std::unique_ptr<rcp::Loan> l;
    std::error_code ec = pool.loan(64, l);
    REQUIRE(ec == AllocErrc::simulated_allocation_failure);
    REQUIRE(l == nullptr);
    REQUIRE(pool.pooled_count() == 0); // nothing was taken from (or added to) the free list
}

TEST_CASE("After a simulated failure is consumed, the next BufferPool::loan call succeeds normally",
          "[alloc][loan]") {
    FaultInjector fi;
    rcp::loan::BufferPool pool(&fi);

    fi.arm(1);
    std::unique_ptr<rcp::Loan> failed;
    REQUIRE(pool.loan(64, failed) == AllocErrc::simulated_allocation_failure);

    std::unique_ptr<rcp::Loan> ok;
    REQUIRE_FALSE(pool.loan(64, ok)); // fault was one-shot; this attempt allocates for real
    REQUIRE(ok != nullptr);
}

TEST_CASE("A FaultInjector armed on a BufferPool does not affect a free-list cache hit", "[alloc][loan]") {
    FaultInjector fi;
    rcp::loan::BufferPool pool(&fi);

    std::unique_ptr<rcp::Loan> first;
    REQUIRE_FALSE(pool.loan(64, first)); // real allocation, injector not yet armed
    first.reset();                       // returned to the pool's free list
    REQUIRE(pool.pooled_count() == 1);

    // Arm the injector *after* a reusable buffer is already pooled: the
    // cache-hit branch never reaches the allocation call site at all, so
    // an armed-but-unconsumed injector must not block a hit.
    fi.arm(1);
    std::unique_ptr<rcp::Loan> second;
    REQUIRE_FALSE(pool.loan(64, second)); // served from the free list, not a new allocation
    REQUIRE(second != nullptr);
    REQUIRE(fi.armed()); // the arm was never consumed -- confirms it was never consulted
}

TEST_CASE("nullptr FaultInjector (the default) never blocks BufferPool allocation", "[alloc][loan]") {
    rcp::loan::BufferPool pool(static_cast<FaultInjector*>(nullptr));
    std::unique_ptr<rcp::Loan> l;
    REQUIRE_FALSE(pool.loan(8, l));
    REQUIRE(l != nullptr);
}
