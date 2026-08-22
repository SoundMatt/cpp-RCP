// fusa:test REQ-RMAP-059
// fusa:test REQ-RMAP-061
// fusa:test REQ-RMAP-062
// fusa:test REQ-RMAP-063
// fusa:test REQ-RMAP-064
// fusa:test REQ-RMAP-065
// fusa:test REQ-RMAP-085
//
// Batch 11 fixup: REQ-SRV-017 (server.hpp) is cross-module -- its own
// scope note says server.hpp's part is content-modeling/admission only,
// so the real test target for its actual claim (a response queue with
// nothing to transmit still emits a heartbeat once Flush_time elapses)
// is here, dual-tagged onto REQ-RMAP-065's existing test:
// fusa:test REQ-SRV-017

// Tests for rcp/respqueue.hpp -- the outbound per-response/ack-stream
// transmit queue (TC18 §12.7.9 Table 27, §12.9.4/§12.9.5), brand new to
// cpp-RCP. Ported from c-RCP's tests/test_respqueue.c.
//
// Not ported: c-RCP's own test_push_fails_when_frame_dup_allocation_fails
// has no C++ analog -- cpp-RCP has no allocation-failure-injection seam
// (rcp/alloc.h's hook table is c-RCP-only), and std::vector's own
// allocator cannot be made to fail deterministically and portably the way
// that test does. RespQueue's push()/push_seq() otherwise cannot fail for
// any reason other than the two explicit, directly-tested ceiling checks
// below.

#include <catch2/catch_test_macros.hpp>
#include <rcp/acf.hpp>
#include <rcp/respqueue.hpp>

#include <cstring>
#include <vector>

using namespace rcp::respqueue;

// ── Basic FIFO push/pop ─────────────────────────────────────────────────────

TEST_CASE("push/pop is FIFO", "[respqueue][REQ-RMAP-059]") {
    RespQueue q;
    const uint8_t a[] = {1, 2, 3};
    const uint8_t b[] = {4, 5};

    REQUIRE(q.len() == 0);
    REQUIRE(q.octets() == 0);

    REQUIRE(q.push(a, sizeof(a)));
    REQUIRE(q.push(b, sizeof(b)));
    REQUIRE(q.len() == 2);
    REQUIRE(q.octets() == 5);

    std::vector<uint8_t> out;
    REQUIRE(q.pop(out));
    REQUIRE(out.size() == 3);
    REQUIRE(std::memcmp(a, out.data(), sizeof(a)) == 0);
    REQUIRE(q.octets() == 2);

    REQUIRE(q.pop(out));
    REQUIRE(out.size() == 2);
    REQUIRE(std::memcmp(b, out.data(), sizeof(b)) == 0);

    REQUIRE(q.len() == 0);
    REQUIRE(q.octets() == 0);
}

TEST_CASE("pop on an empty queue returns false and leaves out_frame untouched",
          "[respqueue][REQ-RMAP-059]") {
    RespQueue q;
    std::vector<uint8_t> out = {0xDE, 0xAD}; // sentinel

    REQUIRE_FALSE(q.pop(out));
    REQUIRE(out == std::vector<uint8_t>{0xDE, 0xAD});
}

TEST_CASE("push_seq accepts a zero-length frame", "[respqueue][REQ-RMAP-085]") {
    RespQueue q;

    REQUIRE(q.push_seq(nullptr, 0, 1));
    REQUIRE(q.len() == 1);

    std::vector<uint8_t> out;
    REQUIRE(q.pop(out));
    REQUIRE(out.empty());
}

// ── REQ-RMAP-059: capacity is an octet budget, not an entry-count limit ────

TEST_CASE("zero capacity is unbounded", "[respqueue][REQ-RMAP-059]") {
    RespQueue q;
    std::vector<uint8_t> big(4096, 0);

    REQUIRE(q.push(big.data(), big.size()));
    REQUIRE(q.octets() == big.size());
}

TEST_CASE("push is refused outright when a frame exceeds total capacity_octets",
          "[respqueue][REQ-RMAP-059]") {
    RespQueue q(10, 0);
    const uint8_t five[5]    = {1, 2, 3, 4, 5};
    const uint8_t eleven[11] = {0};

    REQUIRE(q.push(five, sizeof(five)));
    REQUIRE(q.octets() == 5);

    // 11 > capacity 10 outright: refused regardless of current occupancy.
    REQUIRE_FALSE(q.push(eleven, sizeof(eleven)));
    REQUIRE(q.len() == 1);
    REQUIRE(q.octets() == 5);
    REQUIRE_FALSE(q.overflow());

    // Exactly at the remaining budget: accepted, no eviction needed.
    REQUIRE(q.push(five, sizeof(five)));
    REQUIRE(q.len() == 2);
    REQUIRE(q.octets() == 10);
    REQUIRE_FALSE(q.overflow());
}

TEST_CASE("pop frees capacity for a later push", "[respqueue][REQ-RMAP-059]") {
    RespQueue q(8, 0);
    const uint8_t frame[8]     = {0};
    const uint8_t oversized[9] = {0};

    REQUIRE(q.push(frame, sizeof(frame)));
    REQUIRE_FALSE(q.push(oversized, sizeof(oversized)));

    std::vector<uint8_t> out;
    REQUIRE(q.pop(out));
    REQUIRE(q.octets() == 0);

    REQUIRE(q.push(frame, sizeof(frame)));
}

TEST_CASE("a queue can be destroyed non-empty without leaking (RAII)",
          "[respqueue][REQ-RMAP-059]") {
    const uint8_t frame[3] = {1, 2, 3};
    {
        RespQueue q;
        REQUIRE(q.push(frame, sizeof(frame)));
        REQUIRE(q.push(frame, sizeof(frame)));
    } // destructor runs here; ASan-checked in CI
    SUCCEED();
}

// ── REQ-RMAP-061: a per-message Max_AVTPDUsize ceiling, independent of
//    the aggregate queue_size capacity ─────────────────────────────────────

TEST_CASE("zero max_avtpdu_size is unbounded", "[respqueue][REQ-RMAP-061]") {
    RespQueue q;
    std::vector<uint8_t> frame(64, 0);

    REQUIRE(q.push(frame.data(), frame.size()));
}

TEST_CASE("push is refused once a single frame exceeds max_avtpdu_size",
          "[respqueue][REQ-RMAP-061]") {
    RespQueue q(1000, 10);
    uint8_t ok[10]   = {0};
    uint8_t over[11] = {0};

    REQUIRE(q.push(ok, sizeof(ok)));
    REQUIRE_FALSE(q.push(over, sizeof(over)));
    REQUIRE(q.len() == 1);
    REQUIRE(q.octets() == 10);
}

// ── TC18 §12.9.4/§12.9.5 (REQ-RMAP-085): slot-count eviction + overflow ────

TEST_CASE("push evicts the lowest sequence_num entry, not the FIFO-oldest",
          "[respqueue][REQ-RMAP-085]") {
    RespQueue q; // unbounded byte budget/message ceiling

    // Fill all kMaxEntries slots. The first kMaxEntries-1 pushes get
    // increasing sequence numbers (1..63); the last one pushed (the
    // FIFO-newest entry) is deliberately given sequence_num 0 -- lower
    // than every entry already queued. This makes "lowest sequence_num"
    // and "FIFO-oldest" name two different entries.
    uint8_t frame[1];
    for (size_t i = 0; i + 1 < kMaxEntries; i++) {
        frame[0] = static_cast<uint8_t>(i);
        REQUIRE(q.push_seq(frame, 1, static_cast<uint8_t>(i + 1)));
    }
    frame[0] = 0xAA;
    REQUIRE(q.push_seq(frame, 1, 0));
    REQUIRE(q.len() == kMaxEntries);
    REQUIRE_FALSE(q.overflow());

    // Queue is now completely full by slot count. TC18 §12.9.4/§12.9.5
    // requires evicting the LOWEST-sequence_num entry (seq 0, the 0xAA
    // entry just pushed), NOT index 0 (seq 1, the true FIFO-oldest).
    frame[0] = 0xBB;
    REQUIRE(q.push_seq(frame, 1, 200));
    REQUIRE(q.len() == kMaxEntries);
    REQUIRE(q.overflow());

    // The true FIFO-oldest entry (payload 0, sequence_num 1) must still
    // be present and still at the front.
    std::vector<uint8_t> out;
    REQUIRE(q.pop(out));
    REQUIRE(out.size() == 1);
    REQUIRE(out[0] == 0);

    bool saw_evicted = false, saw_new = false;
    while (q.pop(out)) {
        if (out.size() == 1 && out[0] == 0xAA) saw_evicted = true;
        if (out.size() == 1 && out[0] == 0xBB) saw_new = true;
    }
    REQUIRE_FALSE(saw_evicted);
    REQUIRE(saw_new);
}

TEST_CASE("overflow flag latches until cleared", "[respqueue][REQ-RMAP-085]") {
    RespQueue q;
    uint8_t frame[1] = {0};

    for (size_t i = 0; i < kMaxEntries; i++) {
        REQUIRE(q.push(frame, 1));
    }
    REQUIRE_FALSE(q.overflow()); // not full-and-pushed-past yet

    REQUIRE(q.push(frame, 1)); // triggers eviction, latches overflow
    REQUIRE(q.overflow());

    q.clear_overflow();
    REQUIRE_FALSE(q.overflow());

    // Pop one entry so the queue is no longer completely full, then push
    // again: no eviction this time, so overflow must stay clear.
    std::vector<uint8_t> out;
    REQUIRE(q.pop(out));
    REQUIRE(q.push(frame, 1));
    REQUIRE_FALSE(q.overflow());
}

TEST_CASE("capacity_octets nonzero still enforces the kMaxEntries slot bound",
          "[respqueue][REQ-RMAP-085]") {
    size_t n = kMaxEntries + 10;
    RespQueue q(n, 0); // n octets capacity, 1 octet per entry
    uint8_t frame[1] = {0};

    for (size_t i = 0; i < n; i++) {
        REQUIRE(q.push(frame, 1));
    }
    REQUIRE(q.len() == kMaxEntries);
    REQUIRE(q.octets() == kMaxEntries);
    REQUIRE(q.overflow());
}

TEST_CASE("max_avtpdu_size rejection is unaffected by slot-count eviction",
          "[respqueue][REQ-RMAP-061][REQ-RMAP-085]") {
    RespQueue q(1000, 10);
    uint8_t ok[10]   = {0};
    uint8_t over[11] = {0};

    REQUIRE(q.push_seq(ok, sizeof(ok), 1));
    REQUIRE_FALSE(q.push_seq(over, sizeof(over), 2));
    REQUIRE(q.len() == 1);
    REQUIRE(q.octets() == 10);
    REQUIRE_FALSE(q.overflow());
}

// ── capacity_octets exhaustion triggers eviction, not reject-and-unchanged ──

TEST_CASE("push evicts a single entry when capacity_octets would be exceeded",
          "[respqueue][REQ-RMAP-085]") {
    RespQueue q(10, 0);
    const uint8_t five[5] = {1, 2, 3, 4, 5};
    const uint8_t six[6]  = {1, 2, 3, 4, 5, 6};

    REQUIRE(q.push_seq(five, sizeof(five), 7));
    REQUIRE(q.len() == 1);
    REQUIRE(q.octets() == 5);
    REQUIRE_FALSE(q.overflow());

    // 5 (queued) + 6 (incoming) = 11 > capacity 10: the only queued entry
    // (seq 7) is evicted, freeing 5 octets, and the 6-octet frame admits.
    REQUIRE(q.push_seq(six, sizeof(six), 8));
    REQUIRE(q.len() == 1);
    REQUIRE(q.octets() == 6);
    REQUIRE(q.overflow());
}

TEST_CASE("push evicts multiple entries when one eviction is not enough bytes",
          "[respqueue][REQ-RMAP-085]") {
    RespQueue q(10, 0);
    const uint8_t a[2] = {0xA1, 0xA2};
    const uint8_t b[3] = {0xB1, 0xB2, 0xB3};
    const uint8_t c[2] = {0xC1, 0xC2};
    const uint8_t d[8] = {0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8};

    REQUIRE(q.push_seq(a, sizeof(a), 1)); // octets: 2
    REQUIRE(q.push_seq(b, sizeof(b), 2)); // octets: 5
    REQUIRE(q.push_seq(c, sizeof(c), 3)); // octets: 7
    REQUIRE(q.len() == 3);
    REQUIRE(q.octets() == 7);
    REQUIRE_FALSE(q.overflow());

    // Incoming d needs 8 octets; only 3 free. Evicting `a` (seq 1, lowest,
    // 2 octets) frees 5 total -- still not enough. `b` (seq 2, now
    // lowest remaining) must also be evicted (8 free total) before d
    // fits. `c` (seq 3) must survive.
    REQUIRE(q.push_seq(d, sizeof(d), 4));
    REQUIRE(q.len() == 2); // c and d remain
    REQUIRE(q.octets() == 10);
    REQUIRE(q.overflow());

    bool saw_a = false, saw_b = false, saw_c = false, saw_d = false;
    std::vector<uint8_t> out;
    while (q.pop(out)) {
        if (out.size() == sizeof(a) && std::memcmp(out.data(), a, sizeof(a)) == 0) saw_a = true;
        if (out.size() == sizeof(b) && std::memcmp(out.data(), b, sizeof(b)) == 0) saw_b = true;
        if (out.size() == sizeof(c) && std::memcmp(out.data(), c, sizeof(c)) == 0) saw_c = true;
        if (out.size() == sizeof(d) && std::memcmp(out.data(), d, sizeof(d)) == 0) saw_d = true;
    }
    REQUIRE_FALSE(saw_a); // evicted
    REQUIRE_FALSE(saw_b); // evicted
    REQUIRE(saw_c);       // survived: never the lowest remaining seq
    REQUIRE(saw_d);       // the push that triggered the eviction
}

TEST_CASE("capacity_octets eviction prefers lowest sequence_num, not FIFO-oldest",
          "[respqueue][REQ-RMAP-085]") {
    RespQueue q(8, 0);
    const uint8_t oldest_but_highest_seq[4] = {1, 1, 1, 1};
    const uint8_t newest_but_lowest_seq[4]  = {2, 2, 2, 2};
    const uint8_t incoming[4]               = {3, 3, 3, 3};

    REQUIRE(q.push_seq(oldest_but_highest_seq, 4, 200));
    REQUIRE(q.push_seq(newest_but_lowest_seq, 4, 5));
    REQUIRE(q.octets() == 8);

    // capacity 8, already full: incoming 4-octet frame needs an eviction.
    // Lowest sequence_num is 5 (the FIFO-newest entry) -- it must be
    // evicted, not index 0 (the FIFO-oldest, seq 200).
    REQUIRE(q.push_seq(incoming, 4, 201));
    REQUIRE(q.len() == 2);
    REQUIRE(q.octets() == 8);
    REQUIRE(q.overflow());

    std::vector<uint8_t> out;
    REQUIRE(q.pop(out));
    REQUIRE(std::memcmp(out.data(), oldest_but_highest_seq, 4) == 0); // survived

    REQUIRE(q.pop(out));
    REQUIRE(std::memcmp(out.data(), incoming, 4) == 0);
}

TEST_CASE("capacity_octets == 0 falls back to the kMaxEntries bound",
          "[respqueue][REQ-RMAP-085]") {
    RespQueue q;
    uint8_t frame[1] = {0};

    for (size_t i = 0; i < kMaxEntries; i++) {
        REQUIRE(q.push(frame, 1));
    }
    REQUIRE_FALSE(q.overflow());

    REQUIRE(q.push(frame, 1));
    REQUIRE(q.len() == kMaxEntries);
    REQUIRE(q.overflow());
}

// ── REQ-RMAP-062: the fragmentation-budget helper ───────────────────────────

TEST_CASE("max_fragment_payload reserves the header and worst-case pad",
          "[respqueue][REQ-RMAP-062]") {
    // max_avtpdu_size_octets=32, ABB header=8: 32 - 8 - 3(pad) = 21.
    REQUIRE(RespQueue::max_fragment_payload(32, rcp::acf::kAcfCommonHeaderLen) == 21);

    // Same total, but the larger GBB header (16) leaves less budget.
    REQUIRE(RespQueue::max_fragment_payload(32, rcp::acf::kAcfGbbMessageInfoLen) == 13);
}

TEST_CASE("max_fragment_payload is zero when unbounded or no budget remains",
          "[respqueue][REQ-RMAP-062]") {
    REQUIRE(RespQueue::max_fragment_payload(0, rcp::acf::kAcfCommonHeaderLen) == 0);
    REQUIRE(RespQueue::max_fragment_payload(11, rcp::acf::kAcfCommonHeaderLen) == 0);
    REQUIRE(RespQueue::max_fragment_payload(5, rcp::acf::kAcfCommonHeaderLen) == 0);
}

// ── REQ-RMAP-063: the flush_on_count trigger and AVTPDU packing plan ───────

TEST_CASE("should_flush triggers once octets reach the threshold", "[respqueue][REQ-RMAP-063]") {
    RespQueue q;
    uint8_t frame[5] = {0};

    REQUIRE_FALSE(q.should_flush(8)); // empty

    REQUIRE(q.push(frame, sizeof(frame)));
    REQUIRE_FALSE(q.should_flush(8)); // 5 < 8

    REQUIRE(q.push(frame, sizeof(frame)));
    REQUIRE(q.should_flush(8)); // 10 >= 8
}

TEST_CASE("should_flush with a zero threshold means any nonempty queue",
          "[respqueue][REQ-RMAP-063]") {
    RespQueue q;
    uint8_t frame[1] = {0};

    REQUIRE_FALSE(q.should_flush(0));
    REQUIRE(q.push(frame, sizeof(frame)));
    REQUIRE(q.should_flush(0));
}

TEST_CASE("plan_batch is zero for an empty queue", "[respqueue][REQ-RMAP-063]") {
    RespQueue q;
    REQUIRE(q.plan_batch(100) == 0);
}

TEST_CASE("plan_batch always keeps at least one entry", "[respqueue][REQ-RMAP-063]") {
    RespQueue q;
    std::vector<uint8_t> big(50, 0);

    REQUIRE(q.push(big.data(), big.size()));

    // A budget smaller than the one queued entry still plans that one
    // entry -- push()'s own REQ-RMAP-061 enforcement already guarantees
    // every queued entry individually fits.
    REQUIRE(q.plan_batch(10) == 1);
}

TEST_CASE("plan_batch packs as many entries as fit", "[respqueue][REQ-RMAP-063]") {
    RespQueue q;
    uint8_t frame[5] = {0};

    REQUIRE(q.push(frame, sizeof(frame)));
    REQUIRE(q.push(frame, sizeof(frame)));
    REQUIRE(q.push(frame, sizeof(frame)));

    // Budget 12: entries 1+2 (10 octets) fit, entry 3 would push to 15.
    REQUIRE(q.plan_batch(12) == 2);

    // Budget 0 (unbounded): every remaining entry packs into one AVTPDU.
    REQUIRE(q.plan_batch(0) == 3);
}

// ── REQ-RMAP-064/065: the Flush_time trigger, independent of queue state ───

TEST_CASE("should_flush_by_time: zero flush_time means count-only", "[respqueue][REQ-RMAP-064]") {
    REQUIRE_FALSE(RespQueue::should_flush_by_time(0, 0));
    REQUIRE_FALSE(RespQueue::should_flush_by_time(1000000, 0));
}

TEST_CASE("should_flush_by_time fires at or past the configured interval",
          "[respqueue][REQ-RMAP-064]") {
    REQUIRE_FALSE(RespQueue::should_flush_by_time(999, 1000));
    REQUIRE(RespQueue::should_flush_by_time(1000, 1000));
    REQUIRE(RespQueue::should_flush_by_time(1001, 1000));
}

TEST_CASE("should_flush_by_time is independent of queue state",
          "[respqueue][REQ-RMAP-065][REQ-SRV-017]") {
    RespQueue q;
    uint8_t frame[3] = {0};

    // REQ-RMAP-065: the Flush_time trigger must fire the same way whether
    // the queue is empty or not -- unlike should_flush(), the
    // flush_on_count trigger, which is false for an empty queue.
    // REQ-SRV-017 (cross-module, batch 11): this is the exact "a response
    // queue with nothing to transmit still emits a heartbeat once
    // Flush_time elapses" claim -- the assertion below, on an empty q,
    // before any push(), is that behavior. server.hpp's own scope for
    // this id is content-modeling/admission only (its own catalog text),
    // so the respqueue.hpp mechanism this test exercises is the real,
    // and only, test target.
    REQUIRE(RespQueue::should_flush_by_time(2000, 1000));
    REQUIRE(q.plan_batch(100) == 0);

    REQUIRE(q.push(frame, sizeof(frame)));
    REQUIRE(RespQueue::should_flush_by_time(2000, 1000));
    REQUIRE(q.plan_batch(100) == 1);
}
