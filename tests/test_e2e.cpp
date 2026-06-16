// fusa:test REQ-E2E-001
// fusa:test REQ-E2E-002
// fusa:test REQ-E2E-003
// fusa:test REQ-E2E-004
// fusa:test REQ-E2E-005
// fusa:test REQ-E2E-006
// fusa:test REQ-E2E-007
// fusa:test REQ-E2E-008

#include <catch2/catch_test_macros.hpp>
#include <rcp/e2e.hpp>

using namespace rcp::e2e;

// ── wrap / unwrap round-trip ──────────────────────────────────────────────────

TEST_CASE("wrap / unwrap round-trip preserves payload", "[e2e][REQ-E2E-001]") {
    std::vector<uint8_t> payload{1, 2, 3, 4, 5};
    auto frame = wrap(1, payload);
    REQUIRE(frame.size() == payload.size() + HeaderLen);

    uint32_t seq_out;
    std::vector<uint8_t> payload_out;
    REQUIRE_FALSE(unwrap(frame, seq_out, payload_out));
    REQUIRE(seq_out     == 1);
    REQUIRE(payload_out == payload);
}

TEST_CASE("wrap produces frame with E2E header prepended", "[e2e][REQ-E2E-002]") {
    std::vector<uint8_t> payload{0xAB, 0xCD};
    auto frame = wrap(0xCAFEBABE, payload);
    REQUIRE(frame.size() == 2 + HeaderLen);
    // First 4 bytes are big-endian seq
    uint32_t seq = (uint32_t(frame[0]) << 24) | (uint32_t(frame[1]) << 16)
                 | (uint32_t(frame[2]) << 8)  |  uint32_t(frame[3]);
    REQUIRE(seq == 0xCAFEBABE);
}

TEST_CASE("unwrap rejects truncated frame", "[e2e][REQ-E2E-003]") {
    std::vector<uint8_t> short_frame(HeaderLen - 1, 0);
    uint32_t seq;
    std::vector<uint8_t> out;
    REQUIRE(unwrap(short_frame, seq, out));
}

TEST_CASE("unwrap rejects corrupt CRC", "[e2e][REQ-E2E-004]") {
    std::vector<uint8_t> payload{1, 2, 3};
    auto frame = wrap(1, payload);
    frame.back() ^= 0xFF; // corrupt last byte (CRC)
    uint32_t seq;
    std::vector<uint8_t> out;
    REQUIRE(unwrap(frame, seq, out));
}

// ── ReplayGuard ───────────────────────────────────────────────────────────────

TEST_CASE("ReplayGuard accepts fresh sequence numbers", "[e2e][REQ-E2E-005]") {
    ReplayGuard guard;
    REQUIRE_FALSE(guard.check(1));
    REQUIRE_FALSE(guard.check(2));
    REQUIRE_FALSE(guard.check(3));
}

TEST_CASE("ReplayGuard rejects duplicates", "[e2e][REQ-E2E-006]") {
    ReplayGuard guard;
    REQUIRE_FALSE(guard.check(10));
    REQUIRE(guard.check(10)); // duplicate
}

TEST_CASE("ReplayGuard rejects old seq numbers outside window", "[e2e][REQ-E2E-007]") {
    ReplayGuard guard;
    // Advance window past ReplayWindowSize
    for (uint32_t i = 1; i <= ReplayWindowSize + 5; ++i) {
        REQUIRE_FALSE(guard.check(i));
    }
    // seq=1 is now outside the window
    REQUIRE(guard.check(1));
}

TEST_CASE("ReplayGuard accepts seq=0 exactly once", "[e2e][REQ-E2E-008]") {
    ReplayGuard guard;
    REQUIRE_FALSE(guard.check(0));
    REQUIRE(guard.check(0));
}
