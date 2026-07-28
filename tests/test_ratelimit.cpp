// fusa:test REQ-RL-001
// fusa:test REQ-RL-002
// fusa:test REQ-RL-003
// fusa:test REQ-RL-004
// fusa:test REQ-RL-005
// fusa:test REQ-RL-006
// fusa:test REQ-RL-007
// fusa:test REQ-RL-008

// Tests for rcp/ratelimit.hpp — per-endpoint token-bucket admission control
// (ROADMAP.md milestone 55, "Authorization & Admission-Control Rebind",
// v2.11.0).

#include <catch2/catch_test_macros.hpp>
#include <rcp/ratelimit.hpp>

using namespace rcp;

namespace {

ratelimit::EndpointKey make_key(uint64_t stream_key, avtp::ByteBusId bus) {
    ratelimit::EndpointKey k;
    k.stream_key  = stream_key;
    k.byte_bus_id = bus;
    return k;
}

} // namespace

// ── Basic admission ───────────────────────────────────────────────────────────

TEST_CASE("ratelimit: admit succeeds while the domain's bucket has tokens",
          "[ratelimit][REQ-RL-001]") {
    ratelimit::Config cfg;
    cfg.rate  = 1000;
    cfg.burst = 10;
    ratelimit::Manager mgr(cfg);

    auto key = make_key(1, 5);
    REQUIRE_FALSE(mgr.admit(key, /*is_safety_tagged=*/false, /*now_ms=*/0));
}

// ── Refill proportional to elapsed time ───────────────────────────────────────

TEST_CASE("ratelimit: tokens refill proportionally to elapsed now_ms",
          "[ratelimit][REQ-RL-002]") {
    ratelimit::Config cfg;
    cfg.rate  = 1.0; // 1 token/second
    cfg.burst = 1;
    ratelimit::Manager mgr(cfg);
    auto key = make_key(1, 5);

    // Drain the single burst token at t=0.
    REQUIRE_FALSE(mgr.admit(key, false, 0));
    // Immediately retrying at the same instant finds nothing refilled yet.
    REQUIRE(mgr.admit(key, false, 0) == ratelimit::ErrAdmissionDenied);
    // 1000ms later, exactly one token has refilled at 1 token/second.
    REQUIRE_FALSE(mgr.admit(key, false, 1000));
}

// ── Exhaustion ────────────────────────────────────────────────────────────────

TEST_CASE("ratelimit: admit returns ErrAdmissionDenied once the bucket is exhausted",
          "[ratelimit][REQ-RL-003]") {
    ratelimit::Config cfg;
    cfg.rate  = 0.0; // no refill at all
    cfg.burst = 2;
    ratelimit::Manager mgr(cfg);
    auto key = make_key(1, 5);

    REQUIRE_FALSE(mgr.admit(key, false, 0));
    REQUIRE_FALSE(mgr.admit(key, false, 0));
    REQUIRE(mgr.admit(key, false, 0) == ratelimit::ErrAdmissionDenied);
}

// ── Safety-tagged exemption ───────────────────────────────────────────────────

TEST_CASE("ratelimit: safety-tagged requests bypass the bucket when exempted",
          "[ratelimit][REQ-RL-004]") {
    ratelimit::Config cfg;
    cfg.rate                 = 0.0;
    cfg.burst                = 1;
    cfg.exempt_safety_tagged = true;
    ratelimit::Manager mgr(cfg);
    auto key = make_key(1, 5);

    REQUIRE_FALSE(mgr.admit(key, false, 0)); // drains the one token
    REQUIRE(mgr.admit(key, false, 0) == ratelimit::ErrAdmissionDenied);
    // A safety-tagged request still gets through despite the exhausted bucket.
    REQUIRE_FALSE(mgr.admit(key, /*is_safety_tagged=*/true, 0));
}

TEST_CASE("ratelimit: safety-tagged requests are throttled when not exempted",
          "[ratelimit][REQ-RL-005]") {
    ratelimit::Config cfg;
    cfg.rate                 = 0.0;
    cfg.burst                = 1;
    cfg.exempt_safety_tagged = false;
    ratelimit::Manager mgr(cfg);
    auto key = make_key(1, 5);

    REQUIRE_FALSE(mgr.admit(key, false, 0));
    REQUIRE(mgr.admit(key, /*is_safety_tagged=*/true, 0) == ratelimit::ErrAdmissionDenied);
}

// ── Independent per-domain buckets ────────────────────────────────────────────

TEST_CASE("ratelimit: distinct (stream, endpoint) domains have independent buckets",
          "[ratelimit][REQ-RL-006]") {
    ratelimit::Config cfg;
    cfg.rate  = 0.0;
    cfg.burst = 1;
    ratelimit::Manager mgr(cfg);

    auto key_a = make_key(1, 5);
    auto key_b = make_key(1, 6);   // same stream, different endpoint
    auto key_c = make_key(2, 5);   // different stream, same endpoint

    REQUIRE_FALSE(mgr.admit(key_a, false, 0));
    REQUIRE(mgr.admit(key_a, false, 0) == ratelimit::ErrAdmissionDenied);

    // Neither sibling domain was affected by draining key_a's bucket.
    REQUIRE_FALSE(mgr.admit(key_b, false, 0));
    REQUIRE_FALSE(mgr.admit(key_c, false, 0));
    REQUIRE(mgr.domain_count() == 3);
}

// ── reset() ───────────────────────────────────────────────────────────────────

TEST_CASE("ratelimit: reset() rebuilds a domain's bucket at full burst",
          "[ratelimit][REQ-RL-007]") {
    ratelimit::Config cfg;
    cfg.rate  = 0.0;
    cfg.burst = 1;
    ratelimit::Manager mgr(cfg);
    auto key = make_key(1, 5);

    REQUIRE_FALSE(mgr.admit(key, false, 0));
    REQUIRE(mgr.admit(key, false, 0) == ratelimit::ErrAdmissionDenied);

    mgr.reset(key);
    REQUIRE_FALSE(mgr.admit(key, false, 0));
}

// ── Error identity ─────────────────────────────────────────────────────────────

TEST_CASE("ratelimit: ErrAdmissionDenied is a distinct, non-zero error code",
          "[ratelimit][REQ-RL-008]") {
    REQUIRE(ratelimit::ErrAdmissionDenied);
    REQUIRE(std::string(ratelimit::ErrAdmissionDenied.category().name()) == "rcp.ratelimit");
}
