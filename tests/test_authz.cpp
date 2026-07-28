// fusa:test REQ-AUTH-001
// fusa:test REQ-AUTH-002
// fusa:test REQ-AUTH-003
// fusa:test REQ-AUTH-004
// fusa:test REQ-AUTH-005
// fusa:test REQ-AUTH-006
// fusa:test REQ-AUTH-007
// fusa:test REQ-AUTH-008

// Tests for rcp/authz.hpp — identity/(server, endpoint, request kind)
// access-policy check (ROADMAP.md milestone 55, "Authorization &
// Admission-Control Rebind", v2.11.0).

#include <catch2/catch_test_macros.hpp>
#include <rcp/authz.hpp>

#include <atomic>
#include <thread>
#include <vector>

using namespace rcp;
using rcp::request::RequestCategory;

namespace {

constexpr uint64_t kStreamA = 0x0102030405060001ULL;
constexpr uint64_t kStreamB = 0x0102030405060002ULL;

} // namespace

// ── Basic permit / deny ───────────────────────────────────────────────────────

TEST_CASE("authz: exact-match entry permits the identity", "[authz][REQ-AUTH-001]") {
    authz::AccessPolicy policy;
    policy.allow({"alice", {kStreamA}, {5}, {RequestCategory::Standard}});

    REQUIRE(policy.permit("alice", kStreamA, 5, RequestCategory::Standard));
}

TEST_CASE("authz: mismatched stream/endpoint/kind is denied independently", "[authz][REQ-AUTH-001]") {
    authz::AccessPolicy policy;
    policy.allow({"alice", {kStreamA}, {5}, {RequestCategory::Standard}});

    REQUIRE_FALSE(policy.permit("alice", kStreamB, 5, RequestCategory::Standard));
    REQUIRE_FALSE(policy.permit("alice", kStreamA, 6, RequestCategory::Standard));
    REQUIRE_FALSE(policy.permit("alice", kStreamA, 5, RequestCategory::Triggered));
}

// ── Empty-set-means-any axes ──────────────────────────────────────────────────

TEST_CASE("authz: empty streams/endpoints/kinds sets mean unrestricted on that axis",
          "[authz][REQ-AUTH-002]") {
    authz::AccessPolicy policy;
    policy.allow({"admin", {}, {}, {}}); // unrestricted on every axis

    REQUIRE(policy.permit("admin", kStreamA, 1, RequestCategory::Standard));
    REQUIRE(policy.permit("admin", kStreamB, 200, RequestCategory::Compound));
}

TEST_CASE("authz: one restricted axis still gates while the others stay open",
          "[authz][REQ-AUTH-002]") {
    authz::AccessPolicy policy;
    policy.allow({"bob", {kStreamA}, {}, {}}); // only the stream axis is restricted

    REQUIRE(policy.permit("bob", kStreamA, 7, RequestCategory::Chained));
    REQUIRE_FALSE(policy.permit("bob", kStreamB, 7, RequestCategory::Chained));
}

// ── Identity not covered by any entry ─────────────────────────────────────────

TEST_CASE("authz: unknown identity is denied", "[authz][REQ-AUTH-003]") {
    authz::AccessPolicy policy;
    policy.allow({"alice", {}, {}, {}});

    REQUIRE_FALSE(policy.permit("eve", kStreamA, 1, RequestCategory::Standard));
}

// ── Thread safety ─────────────────────────────────────────────────────────────

TEST_CASE("authz: AccessPolicy permits concurrently without data races",
          "[authz][REQ-AUTH-004]") {
    authz::AccessPolicy policy;
    policy.allow({"alice", {kStreamA}, {5}, {RequestCategory::Standard}});

    std::atomic<int> permits{0};
    std::vector<std::thread> ts;
    for (int t = 0; t < 8; ++t) {
        ts.emplace_back([&] {
            for (int i = 0; i < 5000; ++i) {
                if (policy.permit("alice", kStreamA, 5, RequestCategory::Standard))
                    permits.fetch_add(1, std::memory_order_relaxed);
                if ((i & 0x3ff) == 0)
                    policy.allow({"tmp", {kStreamB}, {}, {}});
            }
        });
    }
    for (auto& th : ts) th.join();
    REQUIRE(permits.load() == 8 * 5000);
}

// ── check() error-code idiom ──────────────────────────────────────────────────

TEST_CASE("authz: check() returns ErrForbidden when permit() would deny",
          "[authz][REQ-AUTH-005]") {
    authz::AccessPolicy policy;
    policy.allow({"alice", {kStreamA}, {5}, {RequestCategory::Standard}});

    auto ec = authz::check(policy, "alice", kStreamB, 5, RequestCategory::Standard);
    REQUIRE(ec == authz::ErrForbidden);
}

TEST_CASE("authz: check() returns success when permit() would allow",
          "[authz][REQ-AUTH-006]") {
    authz::AccessPolicy policy;
    policy.allow({"alice", {kStreamA}, {5}, {RequestCategory::Standard}});

    auto ec = authz::check(policy, "alice", kStreamA, 5, RequestCategory::Standard);
    REQUIRE_FALSE(ec);
}

// ── Default deny ──────────────────────────────────────────────────────────────

TEST_CASE("authz: an identity with zero policy entries is denied by default",
          "[authz][REQ-AUTH-007]") {
    authz::AccessPolicy policy;
    REQUIRE_FALSE(policy.permit("nobody", kStreamA, 1, RequestCategory::Standard));
}

// ── ErrForbidden identity ─────────────────────────────────────────────────────

TEST_CASE("authz: ErrForbidden is a distinct, non-zero error code", "[authz][REQ-AUTH-008]") {
    REQUIRE(authz::ErrForbidden);
    REQUIRE(std::string(authz::ErrForbidden.category().name()) == "rcp.authz");
}
