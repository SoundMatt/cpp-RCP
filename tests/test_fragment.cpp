// fusa:test REQ-FRAG-001
// fusa:test REQ-FRAG-002
// fusa:test REQ-FRAG-003
// fusa:test REQ-FRAG-004
// fusa:test REQ-FRAG-005
// fusa:test REQ-FRAG-006
// fusa:test REQ-FRAG-007
// fusa:test REQ-FRAG-008
// fusa:test REQ-FRAG-009
// fusa:test REQ-FRAG-010
// fusa:test REQ-FRAG-011
// fusa:test REQ-FRAG-012
// fusa:test REQ-FRAG-013
// fusa:test REQ-FRAG-014
// fusa:test REQ-FRAG-015
// fusa:test REQ-FRAG-017
// fusa:test REQ-FRAG-018

// Tests for rcp/fragment.hpp -- multi-AVTPDU fragmentation/reassembly, the
// generic primitive underlying TC18 §13.7.11.3 (Phase 17, cpp-RCP issue
// #129). Ported from c-RCP's tests/test_fragment.c.
//
// REQ-FRAG-010/016 (rcp_fragment_reassembler_destroy() /
// RCP_FRAGMENT_REASM_ERR_ALLOC) have no direct C++ analog here: this
// module's Reassembler is backed by a fixed std::array member (see
// fragment.hpp's own "Fixed-capacity from day one" comment) rather than
// c-RCP's realloc()-grown heap buffer, so there is no separate
// destroy()/free step (RAII handles it) and no allocation-failure outcome
// to report -- any attempt to grow past the fixed capacity is folded into
// kErrTooLarge instead, exercised below by
// "feed() rejects a payload exceeding this Reassembler's own fixed
// capacity".

#include <catch2/catch_test_macros.hpp>
#include <rcp/fragment.hpp>

#include <cstring>
#include <vector>

using namespace rcp::fragment;

// ── strerror / to_string ────────────────────────────────────────────────────

TEST_CASE("FragmentErrc category messages are non-null and distinct",
          "[fragment][REQ-FRAG-001]") {
    auto a = ErrDisabled.message();
    auto b = ErrTooManySegments.message();
    auto c = ErrBadSegmentCount.message();

    REQUIRE_FALSE(a.empty());
    REQUIRE_FALSE(b.empty());
    REQUIRE_FALSE(c.empty());
    REQUIRE(a != b);
    REQUIRE(b != c);

    // Unrecognized value still yields a non-empty message.
    REQUIRE_FALSE(fragment_category().message(999).empty());
}

TEST_CASE("to_string(ReasmResult) is non-null and distinct for every value, including unrecognized",
          "[fragment][REQ-FRAG-007]") {
    const char* a   = to_string(ReasmResult::kContinue);
    const char* b   = to_string(ReasmResult::kComplete);
    const char* c   = to_string(ReasmResult::kErrOutOfOrder);
    const char* d   = to_string(ReasmResult::kErrTooLarge);
    const char* unk = to_string(static_cast<ReasmResult>(999));

    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    REQUIRE(c != nullptr);
    REQUIRE(d != nullptr);
    REQUIRE(unk != nullptr);

    REQUIRE(std::strcmp(a, b) != 0);
    REQUIRE(std::strcmp(b, c) != 0);
    REQUIRE(std::strcmp(c, d) != 0);
}

// ── plan_count ───────────────────────────────────────────────────────────────

TEST_CASE("plan_count: empty payload always plans one segment", "[fragment][REQ-FRAG-002]") {
    REQUIRE(plan_count(0, 0) == 1);
    REQUIRE(plan_count(0, 10) == 1);
}

TEST_CASE("plan_count: payload fitting in one fragment plans one segment", "[fragment][REQ-FRAG-002]") {
    REQUIRE(plan_count(10, 10) == 1);
    REQUIRE(plan_count(5, 10) == 1);
}

TEST_CASE("plan_count: disabled when payload exceeds a zero cap", "[fragment][REQ-FRAG-003]") {
    REQUIRE(plan_count(1, 0) == 0);
}

TEST_CASE("plan_count: exact multiple", "[fragment][REQ-FRAG-002]") {
    REQUIRE(plan_count(30, 10) == 3);
}

TEST_CASE("plan_count: remainder rounds up", "[fragment][REQ-FRAG-002]") {
    REQUIRE(plan_count(31, 10) == 4);
    REQUIRE(plan_count(11, 10) == 2);
}

TEST_CASE("plan_count: too many segments returns 0", "[fragment][REQ-FRAG-003]") {
    REQUIRE(plan_count(kMaxIntermediateSegments + 2, 1) == 0);
}

TEST_CASE("plan_count: exactly at the max-intermediate boundary succeeds", "[fragment][REQ-FRAG-003]") {
    REQUIRE(plan_count(kMaxIntermediateSegments + 1, 1) == kMaxIntermediateSegments + 1);
}

// ── plan ─────────────────────────────────────────────────────────────────────

TEST_CASE("plan: no fragmentation needed produces one ms=false segment", "[fragment][REQ-FRAG-004]") {
    Segment seg;
    auto ec = plan(7, 10, &seg, 1);
    REQUIRE_FALSE(ec);
    REQUIRE(seg.offset == 0);
    REQUIRE(seg.len == 7);
    REQUIRE_FALSE(seg.ms);
}

TEST_CASE("plan: empty payload", "[fragment][REQ-FRAG-004]") {
    Segment seg;
    auto ec = plan(0, 4, &seg, 1);
    REQUIRE_FALSE(ec);
    REQUIRE(seg.offset == 0);
    REQUIRE(seg.len == 0);
    REQUIRE_FALSE(seg.ms);
}

TEST_CASE("plan: multi-segment layout and numbering", "[fragment][REQ-FRAG-005]") {
    // 25 octets, 10 per fragment -> 3 fragments: [0,10) ms=1 seg0,
    // [10,20) ms=1 seg1, [20,25) ms=0 final.
    Segment segs[3];
    auto ec = plan(25, 10, segs, 3);
    REQUIRE_FALSE(ec);

    REQUIRE(segs[0].offset == 0);
    REQUIRE(segs[0].len == 10);
    REQUIRE(segs[0].ms);
    REQUIRE(segs[0].segment_num == 0);

    REQUIRE(segs[1].offset == 10);
    REQUIRE(segs[1].len == 10);
    REQUIRE(segs[1].ms);
    REQUIRE(segs[1].segment_num == 1);

    REQUIRE(segs[2].offset == 20);
    REQUIRE(segs[2].len == 5);
    REQUIRE_FALSE(segs[2].ms);
}

TEST_CASE("plan: segment_num above 255 does not truncate (12-bit field)", "[fragment][REQ-FRAG-005]") {
    // 300 fragments of 1 byte each: 299 intermediate (segment_num
    // 0..298, well past an 8-bit ceiling) plus 1 final.
    std::vector<Segment> segs(300);
    size_t count = plan_count(300, 1);
    REQUIRE(count == 300);
    auto ec = plan(300, 1, segs.data(), count);
    REQUIRE_FALSE(ec);

    REQUIRE(segs[255].ms);
    REQUIRE(segs[255].segment_num == 255);
    REQUIRE(segs[298].ms);
    REQUIRE(segs[298].segment_num == 298);
    REQUIRE_FALSE(segs[299].ms);
}

TEST_CASE("plan: segments contiguously cover the entire payload", "[fragment][REQ-FRAG-005]") {
    size_t payload_len = 67;
    size_t max_frag     = 10;
    size_t count = plan_count(payload_len, max_frag);
    REQUIRE(count == 7);

    std::vector<Segment> segs(count);
    auto ec = plan(payload_len, max_frag, segs.data(), count);
    REQUIRE_FALSE(ec);

    size_t covered = 0;
    for (size_t i = 0; i < count; i++) {
        REQUIRE(segs[i].offset == covered);
        covered += segs[i].len;
        if (i + 1 < count) {
            REQUIRE(segs[i].ms);
            REQUIRE(segs[i].segment_num == static_cast<uint16_t>(i));
        } else {
            REQUIRE_FALSE(segs[i].ms);
        }
    }
    REQUIRE(covered == payload_len);
}

TEST_CASE("plan: disabled", "[fragment][REQ-FRAG-006]") {
    Segment seg;
    auto ec = plan(5, 0, &seg, 999);
    REQUIRE(ec == ErrDisabled);
}

TEST_CASE("plan: too many segments", "[fragment][REQ-FRAG-006]") {
    Segment seg;
    auto ec = plan(kMaxIntermediateSegments + 2, 1, &seg, kMaxIntermediateSegments + 2);
    REQUIRE(ec == ErrTooManySegments);
}

TEST_CASE("plan: bad segment count", "[fragment][REQ-FRAG-006]") {
    Segment segs[3];
    auto ec = plan(25, 10, segs, 2);
    REQUIRE(ec == ErrBadSegmentCount);
}

// ── Reassembler: init/reset postconditions ──────────────────────────────────

TEST_CASE("Reassembler starts empty and not collecting", "[fragment][REQ-FRAG-008]") {
    Reassembler r(1024);
    REQUIRE_FALSE(r.is_collecting());
    REQUIRE(r.size() == 0);
    REQUIRE(r.data() == nullptr);
}

// ── Reassembler: single-segment (never fragmented) messages ────────────────

TEST_CASE("Reassembler: a single ms=false fragment completes immediately",
          "[fragment][REQ-FRAG-011]") {
    Reassembler r(1024);
    const uint8_t data[] = {0xAA, 0xBB, 0xCC};

    auto rc = r.feed(false, 0, data, sizeof(data));
    REQUIRE(rc == ReasmResult::kComplete);
    REQUIRE_FALSE(r.is_collecting());

    REQUIRE(r.size() == sizeof(data));
    REQUIRE(std::memcmp(r.data(), data, sizeof(data)) == 0);
}

TEST_CASE("Reassembler: an empty single-segment message completes with size 0",
          "[fragment][REQ-FRAG-011]") {
    Reassembler r(16);
    auto rc = r.feed(false, 0, nullptr, 0);
    REQUIRE(rc == ReasmResult::kComplete);
    REQUIRE(r.size() == 0);
}

// ── Reassembler: multi-segment sequences ────────────────────────────────────

TEST_CASE("Reassembler: multi-segment sequence round-trips plan()'s own output",
          "[fragment][REQ-FRAG-013][REQ-FRAG-014][REQ-FRAG-018]") {
    uint8_t payload[67];
    for (size_t i = 0; i < sizeof(payload); i++) payload[i] = static_cast<uint8_t>(i * 7 + 1);

    size_t count = plan_count(sizeof(payload), 10);
    std::vector<Segment> segs(count);
    REQUIRE_FALSE(plan(sizeof(payload), 10, segs.data(), count));

    Reassembler r(1024);
    for (size_t i = 0; i < count; i++) {
        auto rc = r.feed(segs[i].ms, segs[i].segment_num, &payload[segs[i].offset], segs[i].len);
        if (i + 1 < count) {
            REQUIRE(rc == ReasmResult::kContinue);
            REQUIRE(r.is_collecting());
        } else {
            REQUIRE(rc == ReasmResult::kComplete);
            REQUIRE_FALSE(r.is_collecting());
        }
    }

    REQUIRE(r.size() == sizeof(payload));
    REQUIRE(std::memcmp(r.data(), payload, sizeof(payload)) == 0);
}

TEST_CASE("Reassembler: out-of-order first segment is rejected", "[fragment][REQ-FRAG-012]") {
    Reassembler r(1024);
    const uint8_t data[4] = {1, 2, 3, 4};

    auto rc = r.feed(true, 1 /* should be 0 */, data, sizeof(data));
    REQUIRE(rc == ReasmResult::kErrOutOfOrder);
    REQUIRE_FALSE(r.is_collecting());
}

TEST_CASE("Reassembler: out-of-order mid-sequence segment preserves already-collected state",
          "[fragment][REQ-FRAG-013]") {
    Reassembler r(1024);
    const uint8_t data[4] = {1, 2, 3, 4};

    REQUIRE(r.feed(true, 0, data, sizeof(data)) == ReasmResult::kContinue);

    // Expected next is 1; skip to 2.
    auto rc = r.feed(true, 2, data, sizeof(data));
    REQUIRE(rc == ReasmResult::kErrOutOfOrder);
    REQUIRE(r.is_collecting()); // already-collected segment 0 untouched
}

TEST_CASE("Reassembler: final ms=false fragment's segment_num field is ignored",
          "[fragment][REQ-FRAG-014]") {
    Reassembler r(1024);
    const uint8_t seg0[4] = {1, 2, 3, 4};
    const uint8_t fin[2]  = {9, 9};

    REQUIRE(r.feed(true, 0, seg0, sizeof(seg0)) == ReasmResult::kContinue);

    // An arbitrary value here (77) means something else once ms=false --
    // must not be checked against the sequence counter.
    auto rc = r.feed(false, 77, fin, sizeof(fin));
    REQUIRE(rc == ReasmResult::kComplete);

    REQUIRE(r.size() == 6);
    REQUIRE(r.data()[0] == 1);
    REQUIRE(r.data()[5] == 9);
}

TEST_CASE("Reassembler: exceeding max_total_len rejects and preserves state",
          "[fragment][REQ-FRAG-015]") {
    Reassembler r(8); // max_total_len == 8
    const uint8_t seg0[4] = {1, 2, 3, 4};
    const uint8_t big[10] = {0};

    REQUIRE(r.feed(true, 0, seg0, sizeof(seg0)) == ReasmResult::kContinue);

    // 4 already accumulated + 10 more would exceed 8.
    auto rc = r.feed(true, 1, big, sizeof(big));
    REQUIRE(rc == ReasmResult::kErrTooLarge);
    REQUIRE(r.is_collecting());
}

TEST_CASE("Reassembler: exactly at max_total_len succeeds", "[fragment][REQ-FRAG-015]") {
    Reassembler r(8);
    const uint8_t seg0[4] = {1, 2, 3, 4};
    const uint8_t fin[4]  = {5, 6, 7, 8};

    REQUIRE(r.feed(true, 0, seg0, sizeof(seg0)) == ReasmResult::kContinue);
    REQUIRE(r.feed(false, 0, fin, sizeof(fin)) == ReasmResult::kComplete);
    REQUIRE(r.size() == 8);
}

// New vs. c-RCP: c-RCP's own reassembler grows a realloc()-backed heap
// buffer up to max_total_len, with a separate RCP_FRAGMENT_REASM_ERR_ALLOC
// outcome purely for allocation failure. This Reassembler has no heap
// buffer at all (see fragment.hpp's "Fixed-capacity from day one" comment)
// -- exceeding its own fixed kDefaultReassemblyCapacity is folded into the
// same kErrTooLarge outcome as exceeding a caller-configured max_total_len,
// even when max_total_len itself is left at its (larger, or default)
// value.
TEST_CASE("Reassembler: feed() rejects a payload exceeding this Reassembler's own fixed capacity",
          "[fragment][REQ-FRAG-015]") {
    // max_total_len deliberately far larger than kDefaultReassemblyCapacity:
    // the fixed std::array capacity, not max_total_len, is what actually
    // binds here.
    Reassembler r(kDefaultReassemblyCapacity * 4);
    std::vector<uint8_t> huge(kDefaultReassemblyCapacity + 1, 0x5A);

    auto rc = r.feed(false, 0, huge.data(), huge.size());
    REQUIRE(rc == ReasmResult::kErrTooLarge);
    REQUIRE_FALSE(r.is_collecting());
}

TEST_CASE("Reassembler: a payload exactly at the fixed capacity boundary succeeds",
          "[fragment][REQ-FRAG-015]") {
    Reassembler r(kDefaultReassemblyCapacity);
    std::vector<uint8_t> exact(kDefaultReassemblyCapacity, 0x11);

    auto rc = r.feed(false, 0, exact.data(), exact.size());
    REQUIRE(rc == ReasmResult::kComplete);
    REQUIRE(r.size() == kDefaultReassemblyCapacity);
}

// ── Reassembler: reset/reuse across messages ────────────────────────────────

TEST_CASE("Reassembler: reset() discards in-progress state and allows reuse",
          "[fragment][REQ-FRAG-009][REQ-FRAG-017]") {
    Reassembler r(1024);
    const uint8_t a[3] = {1, 2, 3};
    const uint8_t b[2] = {9, 8};

    REQUIRE(r.feed(true, 0, a, sizeof(a)) == ReasmResult::kContinue);
    REQUIRE(r.is_collecting());

    r.reset();
    REQUIRE_FALSE(r.is_collecting());

    // A fresh, unrelated single-segment message must work cleanly after
    // reset, with no leftover state from the abandoned sequence.
    auto rc = r.feed(false, 0, b, sizeof(b));
    REQUIRE(rc == ReasmResult::kComplete);
    REQUIRE(r.size() == sizeof(b));
    REQUIRE(std::memcmp(r.data(), b, sizeof(b)) == 0);
}

TEST_CASE("Reassembler: a completed reassembly can be reused for the next message without an explicit reset",
          "[fragment][REQ-FRAG-011]") {
    Reassembler r(1024);
    const uint8_t a[3] = {1, 2, 3};
    const uint8_t b[3] = {4, 5, 6};

    REQUIRE(r.feed(false, 0, a, sizeof(a)) == ReasmResult::kComplete);

    // Without an explicit reset, feeding the next logical message's own
    // single-segment fragment appends atop (does not clear) the previous
    // result -- matching c-RCP's own documented behavior exactly.
    REQUIRE(r.feed(false, 0, b, sizeof(b)) == ReasmResult::kComplete);
    REQUIRE(r.size() == sizeof(a) + sizeof(b));
}
