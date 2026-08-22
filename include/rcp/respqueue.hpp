// fusa:req REQ-RMAP-059
// fusa:req REQ-RMAP-061
// fusa:req REQ-RMAP-062
// fusa:req REQ-RMAP-063
// fusa:req REQ-RMAP-064
// fusa:req REQ-RMAP-065
// fusa:req REQ-RMAP-085

// Per-response/acknowledge-stream transmit queue for the TC18 Remote
// Control Protocol wire layer (TC18 §12.7.9 Table 27, §12.9.4/§12.9.5) --
// brand new to cpp-RCP.
//
// TC18 §12.7.9 Table 27 describes a per-response/ack-stream transmit queue
// with a configured memory reservation (queue_size, in 32-bit words) that
// responses and acknowledges from the RC Server's own endpoints are
// collected into for aggregated transmission. Nothing in cpp-RCP modeled
// that queue before this module: any per-endpoint inbound request queue
// elsewhere in this tree holds INBOUND requests awaiting execution, a
// structurally different concept -- this module is the OUTBOUND queue of
// framed responses/acknowledges awaiting transmission cpp-RCP was missing
// entirely. Ported from c-RCP's include/rcp/respqueue.h + src/respqueue.c,
// this project's RC5-spec-conformant reference implementation for this
// module. No spec prose or numeric constant is reproduced here.
//
// This module owns no register-map instance of its own (rcp/regmap.hpp's
// ResponseQueueConfig::queue_size/max_avtpdu_size are the configured values
// a caller reads and passes to RespQueue's constructor below -- the same
// "caller supplies already-classified inputs" convention this codebase
// uses throughout), no transport, and no knowledge of ACF/AVTP framing --
// push()/pop() operate on caller-supplied byte buffers.
//
// ── queue_size (capacity_octets) overflow: evict-lowest-sequence_num ───────
//
// TC18 §12.9.4 (response queue) and §12.9.5 (acknowledge queue) both give
// the same additional, mandatory rule: once a queue is completely full and
// not yet sent while the next response/acknowledge is delivered by an
// endpoint, the entry with the lowest sequence_num is removed from the
// queue to make space for the new one, and the overflow bit is set.
// push()/push_seq() implement exactly that -- entries are evicted in
// ascending sequence_num order (a genuine numeric minimum over every
// queued entry's own sequence_num each iteration, not merely the
// FIFO-oldest), repeatedly, as many times as needed to free enough BYTES
// for the incoming frame (a single eviction frees only its own evicted
// entry's own byte size, which may be smaller than the incoming frame).
// A frame whose own length exceeds capacity_octets outright is refused
// (queue unchanged, no eviction attempted) -- no amount of eviction could
// ever make room for it.
//
// ── kMaxEntries: a universal, fixed slot-count bound (no dynamic
//    allocation of the queue's own storage) ─────────────────────────────────
//
// Ported from c-RCP's RCP_RESPQUEUE_MAX_ENTRIES (64, the same
// fixed-capacity convention c-RCP applies to every one of its own
// repeated-row tables): entries_ below is a std::array<Entry, kMaxEntries>
// embedded directly in RespQueue, not a realloc()/std::vector-grown
// container, so RespQueue itself carries no heap allocation of its own
// regardless of how capacity_octets is configured (only each queued
// frame's own payload bytes remain individually heap-allocated, as a
// std::vector<uint8_t> per entry -- see this file's own file comment for
// why that particular allocation stays out of scope: frame_len is
// caller/message-dependent, not a compile-time protocol constant).
// Consequently the eviction loop triggers on EITHER condition:
// capacity_octets != 0 and accepting frame_len would exceed the remaining
// byte budget, OR entries_len has reached kMaxEntries outright -- not only
// as a capacity_octets == 0 fallback. entries_len can never exceed
// kMaxEntries under any configuration, by construction.
//
// push() itself assigns sequence_num automatically, from an internal
// wrapping uint8_t counter; push_seq() is the same operation with an
// explicitly-supplied sequence_num, for a caller that already tracks its
// own (e.g. one that wants queue-internal sequence_num to agree with the
// eventual AVTPDU header's own sequence_num field).
//
// ── FIFO order, byte-budget capacity ────────────────────────────────────────
//
// Entries drain in the order they were pushed. Capacity is primarily
// enforced in OCTETS, not entry count -- TC18's own queue_size register is
// a memory reservation ("assigned memory in 32bit words"), not a
// message-count limit. capacity_octets == 0 means unbounded byte budget
// (no reservation configured at all).
//
// ── Per-message Max_AVTPDUsize ceiling ──────────────────────────────────────
//
// REQ-RMAP-061 (TC18 §12.7.9): the maximum length of an AVTPDU sent by the
// RC Server is independently configurable and enforced -- a DIFFERENT
// ceiling than capacity_octets (the queue's own aggregate reservation),
// checked independently, per-message, on every push() call: a frame whose
// own length alone exceeds max_avtpdu_size_octets is refused (queue
// unchanged), never silently truncated or split (this module has no
// ACF/framing knowledge of its own to split with). A caller with a payload
// too large for one AVTPDU fragments it FIRST (max_fragment_payload()
// below, together with rcp/fragment.hpp's plan_count()/plan()) and pushes
// each resulting fragment as its own, individually-bounded push() call.
// max_avtpdu_size_octets == 0 means unbounded, the same fail-open default
// as capacity_octets.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace rcp {
namespace respqueue {

// Ported from c-RCP's RCP_RESPQUEUE_MAX_ENTRIES.
constexpr size_t kMaxEntries = 64;

// The outbound per-response/ack-stream transmit queue (TC18 §12.7.9 Table
// 27, §12.9.4/§12.9.5) -- see this file's own header comment.
class RespQueue {
public:
    // Constructs an empty queue with the given octet capacity and
    // per-message ceiling (rcp/regmap.hpp's ResponseQueueConfig::
    // queue_size and max_avtpdu_size, already converted from quadlets to
    // octets by the caller -- this module does that conversion nowhere).
    // 0 for either means unbounded.
    explicit RespQueue(size_t capacity_octets = 0, size_t max_avtpdu_size_octets = 0) noexcept
        : capacity_octets_(capacity_octets), max_avtpdu_size_octets_(max_avtpdu_size_octets) {}

    // Appends a copy of frame[0..frame_len) to the tail (frame may be
    // nullptr iff frame_len == 0), tagged with an internally-assigned
    // sequence_num (then advances that counter, wrapping mod 256).
    // Identical in every other respect to push_seq() below -- see its own
    // doc comment for the full byte-budget-and-eviction behavior and the
    // kMaxEntries slot-count ceiling.
    bool push(const uint8_t* frame, size_t frame_len) noexcept {
        bool ok = push_seq(frame, frame_len, next_sequence_num_);
        if (ok) next_sequence_num_++; // wraps mod 256: next_sequence_num_ is uint8_t
        return ok;
    }

    // Same as push() above, except sequence_num is supplied by the caller
    // rather than assigned from this queue's own internal counter (and
    // next_sequence_num_ is left untouched). Returns true and grows
    // octets() by frame_len on success. Returns false, leaving the queue
    // entirely unchanged, if:
    //   - max_avtpdu_size_octets is nonzero and frame_len exceeds it
    //     (checked first, independently of capacity_octets); or
    //   - capacity_octets is nonzero and frame_len exceeds it outright (no
    //     amount of eviction could ever make room for a frame larger than
    //     the entire configured queue_size budget, even against a fully
    //     empty queue).
    //
    // Otherwise, TC18 §12.9.4/§12.9.5 applies: entries are evicted in
    // ascending sequence_num order -- always the queued entry with the
    // currently-lowest sequence_num, never merely the FIFO-oldest --
    // repeatedly, once for each of the following that still holds, until
    // neither does:
    //   - capacity_octets is nonzero and accepting frame_len would still
    //     exceed the remaining budget; or
    //   - entries_len has reached kMaxEntries, this queue's own
    //     fixed-capacity slot-count ceiling -- enforced unconditionally,
    //     independently of capacity_octets.
    //
    // Every eviction latches overflow() true. This is NOT simply "evict
    // the FIFO-oldest entry" -- sequence_num-order and FIFO-order coincide
    // only until sequence_num wraps (256 values, uint8_t).
    bool push_seq(const uint8_t* frame, size_t frame_len, uint8_t sequence_num) noexcept {
        if (max_avtpdu_size_octets_ != 0 && frame_len > max_avtpdu_size_octets_) return false;
        if (capacity_octets_ != 0 && frame_len > capacity_octets_) return false;

        while ((capacity_octets_ != 0 && frame_len > capacity_octets_ - octets_) ||
               entries_len_ == kMaxEntries) {
            size_t lowest_idx = 0;
            for (size_t i = 1; i < entries_len_; i++) {
                if (entries_[i].sequence_num < entries_[lowest_idx].sequence_num) lowest_idx = i;
            }

            octets_ -= entries_[lowest_idx].data.size();

            // Close the gap left by the evicted slot, preserving FIFO
            // order for the remaining entries.
            for (size_t i = lowest_idx + 1; i < entries_len_; i++) {
                entries_[i - 1] = std::move(entries_[i]);
            }
            entries_len_--;
            overflow_ = true;
        }

        Entry& e = entries_[entries_len_];
        e.data.assign(frame, frame_len == 0 ? frame : frame + frame_len);
        e.sequence_num = sequence_num;
        entries_len_++;
        octets_ += frame_len;
        return true;
    }

    // TC18 §12.9.4/§12.9.5's overflow bit: true iff push()/push_seq() has
    // evicted at least one entry since this queue was last constructed or
    // last had clear_overflow() called.
    bool overflow() const noexcept { return overflow_; }

    // Clears overflow() back to false. Safe to call whether or not it was
    // ever set.
    void clear_overflow() noexcept { overflow_ = false; }

    // REQ-RMAP-061's own remaining "MTU-consistency" half (TC18 §12.7.9:
    // "The Max_AVTPDUsize shall always be configured such that the final
    // network frame does not exceed the maximum transmit unit size of the
    // network"). A config-time check, not a per-message queue concern --
    // push() already enforces the transmit-bounding half against a fixed,
    // already-accepted max_avtpdu_size_octets; this function is what a
    // caller uses BEFORE ever constructing a RespQueue, to decide whether
    // a candidate Max_AVTPDUsize value is even acceptable for its own
    // network. mtu_budget_octets is the caller's own already-adjusted
    // ceiling (whatever "how many Max_AVTPDUsize octets fit under this
    // deployment's real MTU" resolves to, netted of any header overhead
    // its own network stack adds) -- this function does not itself add or
    // assume any such overhead. Returns true iff max_avtpdu_size_octets
    // does not exceed mtu_budget_octets; max_avtpdu_size_octets == 0
    // (unbounded) is never within budget for a nonzero mtu_budget_octets,
    // and is vacuously true only when mtu_budget_octets is also 0.
    static bool max_avtpdu_size_within_mtu(size_t max_avtpdu_size_octets,
                                            size_t mtu_budget_octets) noexcept {
        if (max_avtpdu_size_octets == 0) return mtu_budget_octets == 0;
        return max_avtpdu_size_octets <= mtu_budget_octets;
    }

    // Dequeues the oldest entry (FIFO) into out_frame and shrinks
    // octets() by its length. Returns true on success. Returns false,
    // leaving out_frame untouched, iff the queue is empty.
    bool pop(std::vector<uint8_t>& out_frame) noexcept {
        if (entries_len_ == 0) return false;

        out_frame = std::move(entries_[0].data);
        octets_ -= out_frame.size();
        for (size_t i = 1; i < entries_len_; i++) {
            entries_[i - 1] = std::move(entries_[i]);
        }
        entries_len_--;
        return true;
    }

    // Number of entries currently queued.
    size_t len() const noexcept { return entries_len_; }

    // Sum of every currently-queued entry's own length, in octets -- the
    // same quantity push() checks against capacity_octets.
    size_t octets() const noexcept { return octets_; }

    // REQ-RMAP-062 (TC18 §12.7.9): "In case an AVTPDU containing a single
    // ACF_type would exceed the Max_AVTPDUsize, fragmentation... by the
    // ms-bit will be performed." Computes the max_fragment_payload a
    // caller hands to rcp/fragment.hpp's plan_count()/plan() so that
    // every resulting fragment's own encoded AVTPDU (fixed ACF header +
    // fragment payload + trailing pad) stays within max_avtpdu_size_octets
    // -- header_len is the caller's own already-known ACF fixed-header
    // length for the message kind being sent (rcp/acf.hpp's
    // kAcfCommonHeaderLen or kAcfGbbMessageInfoLen; this module has no ACF
    // knowledge of its own). Conservatively reserves the worst case 3
    // octets of trailing pad so the result is safe regardless of the
    // actual pad a given fragment ends up needing.
    //
    // Returns 0 (matching FragmentErrc::disabled's own "fragmentation
    // disabled" convention, rcp/fragment.hpp) if max_avtpdu_size_octets ==
    // 0 (unbounded) or if header_len + 3 already meets or exceeds
    // max_avtpdu_size_octets (no payload budget remains at all once the
    // fixed header and worst-case pad are reserved).
    static size_t max_fragment_payload(size_t max_avtpdu_size_octets, size_t header_len) noexcept {
        if (max_avtpdu_size_octets == 0) return 0;

        size_t reserved = header_len + 3; // fixed header + worst-case trailing pad
        if (reserved >= max_avtpdu_size_octets) return 0;

        return max_avtpdu_size_octets - reserved;
    }

    // REQ-RMAP-063 (TC18 §12.7.9, Table 27 relative address 0x0006):
    // "Once a queue is filled with an amount of quadlets that is equal or
    // larger than given by flush_on_count, the transmission of one or
    // multiple AVTPDUs shall be initiated." True iff this queue currently
    // holds at least flush_on_count_octets octets -- flush_on_count_octets
    // is the register's own quadlet value already converted to octets by
    // the caller (flush_on_count x 4). flush_on_count_octets == 0 always
    // returns true iff the queue is non-empty (the register's own
    // documented default, 1 quadlet, is the smallest possible nonzero
    // threshold -- "immediate transmission").
    bool should_flush(size_t flush_on_count_octets) const noexcept {
        if (entries_len_ == 0) return false;
        if (flush_on_count_octets == 0) return true;

        return octets_ >= flush_on_count_octets;
    }

    // REQ-RMAP-063: "Hereby only as much as fitting to the MAX_AVTPDUsize
    // ACF_types will be included in a generated AVTPDU. Basically all
    // ACF_types including the one which was exceeding the Flush_on_Count
    // value will be transmitted, packed in a fitting number of AVTPDUs."
    // Reports how many of this queue's own FIFO-ordered entries, starting
    // from the front, fit together within max_avtpdu_size_octets octets
    // total -- the membership of ONE generated AVTPDU. A caller builds
    // that AVTPDU by calling pop() exactly that many times (draining
    // exactly those entries), then calls this function again for the next
    // AVTPDU, repeating until len() reaches 0.
    //
    // Always plans at least 1 entry when the queue is non-empty (an entry
    // too large for max_avtpdu_size_octets on its own could never have
    // been pushed in the first place -- push()'s own REQ-RMAP-061
    // enforcement already guarantees every queued entry individually
    // fits). Returns 0 iff the queue is empty. max_avtpdu_size_octets == 0
    // (unbounded) plans every remaining entry into one AVTPDU.
    size_t plan_batch(size_t max_avtpdu_size_octets) const noexcept {
        if (entries_len_ == 0) return 0;
        if (max_avtpdu_size_octets == 0) return entries_len_;

        size_t total = 0;
        size_t i;
        for (i = 0; i < entries_len_; i++) {
            size_t next_total = total + entries_[i].data.size();
            if (i > 0 && next_total > max_avtpdu_size_octets) break;
            total = next_total;
        }
        return i;
    }

    // REQ-RMAP-064 (TC18 §12.7.9, Table 27 relative address 0x0008,
    // microseconds, default 0 meaning "flush only by count"): the server
    // shall initiate transmission from a response queue whenever the time
    // since that queue's last transmission is equal to or greater than
    // Flush_time, independently of flush_on_count. True iff flush_time_us
    // is nonzero and elapsed_since_last_transmit_us has reached it.
    // elapsed_since_last_transmit_us is a caller-tracked duration -- this
    // module owns no clock.
    //
    // REQ-RMAP-065: deliberately independent of queue occupancy -- unlike
    // should_flush() above (which has nothing meaningful to report on an
    // empty queue and so treats one as never due), this trigger fires the
    // same way whether the queue is empty or not, because the server must
    // still transmit an empty heartbeat AVTPDU even when nothing is
    // queued. A caller composes the full behaviour as: once this function
    // returns true, either call plan_batch()+drain+encode a real batch (a
    // nonzero plan_batch() result), or -- if plan_batch() reports 0 --
    // encode an empty-payload heartbeat AVTPDU directly. Actually
    // SCHEDULING that composition against a real clock is a
    // caller/integrator concern this library does not take on.
    static bool should_flush_by_time(uint64_t elapsed_since_last_transmit_us,
                                      uint64_t flush_time_us) noexcept {
        if (flush_time_us == 0) return false;

        return elapsed_since_last_transmit_us >= flush_time_us;
    }

private:
    struct Entry {
        std::vector<uint8_t> data;
        uint8_t               sequence_num = 0;
    };

    std::array<Entry, kMaxEntries> entries_{};       // fixed-capacity (no realloc/vector growth)
    size_t                          entries_len_ = 0; // always <= kMaxEntries, by construction
    size_t                          octets_      = 0; // running total of entries_[i].data.size(), 0 <= i < entries_len_
    size_t                          capacity_octets_;
    size_t                          max_avtpdu_size_octets_;
    uint8_t                         next_sequence_num_ = 0;
    bool                            overflow_           = false;
};

} // namespace respqueue
} // namespace rcp
