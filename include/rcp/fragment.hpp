// fusa:req REQ-FRAG-001
// fusa:req REQ-FRAG-002
// fusa:req REQ-FRAG-003
// fusa:req REQ-FRAG-004
// fusa:req REQ-FRAG-005
// fusa:req REQ-FRAG-006
// fusa:req REQ-FRAG-007
// fusa:req REQ-FRAG-008
// fusa:req REQ-FRAG-009
// fusa:req REQ-FRAG-010
// fusa:req REQ-FRAG-011
// fusa:req REQ-FRAG-012
// fusa:req REQ-FRAG-013
// fusa:req REQ-FRAG-014
// fusa:req REQ-FRAG-015
// fusa:req REQ-FRAG-016
// fusa:req REQ-FRAG-017
// fusa:req REQ-FRAG-018

// Multi-AVTPDU message fragmentation and reassembly for the TC18 Remote
// Control Protocol wire layer (TC18 §13.7.11.3) — brand new to cpp-RCP.
//
// ROADMAP.md "Phase 17" (cpp-RCP issue #129): cpp-RCP made an explicit
// no-go call on fragmentation at milestone 52 (v2.8.0) — rcp/can.hpp,
// rcp/spi.hpp, rcp/uart.hpp, rcp/acf.hpp, and rcp/avtp.hpp all still carry
// "fragmentation deferred" comments, and rcp/regmap.hpp's kOptFragmentation
// capability bit is reserved and never set. This module is the generic
// primitive that closes the groundwork gap: it interprets ACF's own
// dual-purpose read_size_or_segment_num field and ms bit (rcp/acf.hpp's
// AcfMessageInfo already reserves both wire slots, per that header's own
// comment) without depending on rcp/acf.hpp, rcp/avtp.hpp, or any endpoint
// header at all — same "own small pure primitive, operate on caller-owned
// data" layering discipline rcp/deadline.hpp and rcp/watchdog.hpp already
// use. Wiring this primitive into rcp/can.hpp/rcp/mock.hpp real dispatch is
// explicitly OUT of scope for this pass (left for Phase 3/4); this header
// is the fragmentation/reassembly primitive only.
//
// Ported from c-RCP's include/rcp/fragment.h + src/fragment.c, this
// project's RC5-spec-conformant reference implementation for this module.
// No spec prose, bit layout, or numeric constant is reproduced here.
//
// ── Wire semantics this module encodes/decodes against ─────────────────────
//
// A single logical ACF payload — what a caller would otherwise hand
// straight to an ACF_ABB/ACF_GBB encoder as one contiguous buffer — is
// instead split into an ordered sequence of one or more fragments, each
// sharing the same acf_msg_type/byte_bus_id/op/transaction_num as the
// logical message they jointly carry. Every fragment but the last sets
// ms=true and carries, in read_size_or_segment_num, this fragment's own
// zero-based index within the sequence (Segment::segment_num — see
// plan() below). The final fragment sets ms=false; per ACF's own field
// semantics that reverts read_size_or_segment_num to its ordinary
// non-fragmentation meaning, so this module never writes a segment number
// for the final fragment — a caller fills that field in with whatever
// value it ordinarily carries for the message kind involved. A message
// that never needed fragmenting in the first place is simply a
// one-fragment sequence: ms=false on its only (and therefore also final)
// fragment, indistinguishable on the wire from ordinary single-frame
// traffic — this module's mechanism is a strict superset of "no
// fragmentation", not a parallel wire format.
//
// segment_num is 12 bits wide on the wire (ACF's
// read_size_or_segment_num[11:0]), giving 4096 distinct values (0..4095) —
// see kMaxIntermediateSegments below.
//
// ── Reassembly: a small caller-owned accumulator, not global state ─────────
//
// Reassembler is a small, caller-owned, explicitly-constructed piece of
// mutable state, one instance per request stream a caller is reassembling
// fragments for. It bounds the reassembled payload to a caller-supplied
// max_total_len, failing closed with ReasmResult::kErrTooLarge rather than
// growing without bound.
//
// Segment ordering is enforced strictly: the first ms=true fragment fed to
// a freshly-constructed-or-reset Reassembler must carry segment_num == 0,
// and every subsequent ms=true fragment must carry exactly one more than
// the previous fragment's segment_num, or ReasmResult::kErrOutOfOrder is
// returned and the fragment is not appended.
//
// ── Fixed-capacity from day one (cleaner than c-RCP's own C version) ───────
//
// c-RCP's own rcp_fragment_reassembler_t accumulates into a realloc()-grown
// heap buffer (src/fragment.c's append() calls rcp_realloc()), bounded only
// by the caller-supplied max_total_len at each feed() call — a genuine
// allocation-failure path (RCP_FRAGMENT_REASM_ERR_ALLOC) exists purely
// because that growth can fail. cpp-RCP has no allocation seam yet (that
// lands in a later phase) and this project's own ASIL-D-oriented
// no-dynamic-allocation convention (already applied to
// RCP_RESPQUEUE_MAX_ENTRIES/RCP_LOAN_POOL_MAX_ENTRIES on the c-RCP side, and
// to this same phase's rcp/respqueue.hpp and rcp/loan.hpp ports) argues for
// going further here, not just matching c-RCP: Reassembler below is backed
// by a single std::array<uint8_t, kDefaultReassemblyCapacity> member — a
// plain, embedded, compile-time-sized buffer, not a pointer to
// realloc()-grown heap storage. There is therefore no allocation to fail on
// the reassembly path at all, and no ReasmResult analogous to
// RCP_FRAGMENT_REASM_ERR_ALLOC: any attempt to accumulate past the fixed
// capacity — exactly like any attempt to exceed the caller's own
// max_total_len — is reported as ReasmResult::kErrTooLarge, the state left
// untouched. This is a deliberate improvement over c-RCP's own design, not
// a regression from it (see this file's own kDefaultReassemblyCapacity
// comment for the bound's provenance).
//
// ── The oversized-reassembly lesson (c-RCP issues #614/#616) ───────────────
//
// c-RCP's own history here is directly instructive. Once fragmentation
// landed (PRs #612/#613), issue #614 (fixed by #616) found that a request
// whose EVERY fragment, and whose combined E2E CRC, were genuinely valid
// could still reassemble successfully and then vanish with no observable
// error: the reassembled payload didn't fit back into a single
// ACF_ABB/ACF_GBB frame (RCP_ACF_ABB_MAX_PAYLOAD/RCP_ACF_GBB_MAX_PAYLOAD —
// acf_msg_length's own 9-bit wire-format ceiling, TC18 §13.6), the
// downstream re-encode call failed, and the caller (c-RCP's mock.c
// dispatch layer) originally just returned "rejected" with no response
// body constructed at all — a client left only able to time out. The fix
// was NOT in fragment.c/fragment.h at all (this module's own reassembler
// already reported RCP_FRAGMENT_REASM_COMPLETE correctly, with the full,
// correctly-reassembled — if oversized — payload); the fix was entirely in
// the caller's own dispatch code, which had to check the reassembled
// result against the frame-format ceiling BEFORE attempting to re-encode
// it, and build a real Table 27 RCP_ERROR_REQUEST_REJECTED response when
// that check fails, rather than silently dropping the request.
//
// This module is deliberately as ACF-agnostic in C++ as it is in C — it
// has no RCP_ACF_ABB_MAX_PAYLOAD/RCP_ACF_GBB_MAX_PAYLOAD constant of its
// own to check reassembled results against, matching rcp/acf.hpp's own
// kAcfAbbMaxPayload/kAcfGbbMaxPayload staying in that header, not this one.
// The lesson this module DOES bake in from day one: Reassembler::data()/
// size() are fully inspectable after every ReasmResult::kComplete, so a
// future caller wiring this primitive into a real dispatch path (Phase
// 3/4, explicitly out of scope here) has everything it needs to run that
// same size check itself, immediately after kComplete and before ever
// attempting to re-encode — the same check c-RCP's mock.c now performs.
// Silently dropping an oversized-but-successfully-reassembled result is a
// caller bug, not something this primitive can prevent on its own (it has
// no framing knowledge to know what "too large" even means) — but this
// primitive must never make that bug easy to fall into by hiding or
// truncating the reassembled result itself, and it does not: get()/data()/
// size() always report the true, complete, reassembled length.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <system_error>

namespace rcp {
namespace fragment {

// ── Errors (encode/plan side) ───────────────────────────────────────────────

enum class FragmentErrc : int {
    disabled           = 1, // max_fragment_payload == 0 and the payload does not fit in a single fragment
    too_many_segments  = 2, // the split would need more intermediate segments than segment_num's 12-bit width can address
    bad_segment_count  = 3, // segment_count passed to plan() does not match plan_count()'s answer for the same inputs
};

inline const std::error_category& fragment_category() noexcept {
    struct Cat : std::error_category {
        const char* name() const noexcept override { return "rcp.fragment"; }
        std::string message(int ev) const override {
            switch (static_cast<FragmentErrc>(ev)) {
            case FragmentErrc::disabled:          return "rcp/fragment: fragmentation disabled for this max_fragment_payload";
            case FragmentErrc::too_many_segments: return "rcp/fragment: payload needs more segments than segment_num can address";
            case FragmentErrc::bad_segment_count: return "rcp/fragment: segment_count does not match plan_count()";
            default:                              return "rcp/fragment: unknown error";
            }
        }
    };
    static Cat instance;
    return instance;
}

inline std::error_code make_error_code(FragmentErrc e) noexcept {
    return {static_cast<int>(e), fragment_category()};
}

inline const std::error_code ErrDisabled         = make_error_code(FragmentErrc::disabled);
inline const std::error_code ErrTooManySegments  = make_error_code(FragmentErrc::too_many_segments);
inline const std::error_code ErrBadSegmentCount  = make_error_code(FragmentErrc::bad_segment_count);

// The largest number of ms=true (intermediate) fragments a single
// reassembled message can be split into: segment_num is 12 bits wide
// (ACF's read_size_or_segment_num[11:0]), giving 4096 distinct values
// (0..4095) — every one usable by an intermediate fragment, since (unlike
// the final fragment) an intermediate fragment's segment_num is always
// this module's own sequence index. Ported from c-RCP's
// RCP_FRAGMENT_MAX_INTERMEDIATE_SEGMENTS.
constexpr size_t kMaxIntermediateSegments = 4096;

// ── Planning (encode side) ──────────────────────────────────────────────────

// One planned fragment: which slice [offset, offset+len) of the original
// payload it carries, whether it is an intermediate (ms=true) or the final
// (ms=false) fragment, and — meaningful only when ms is true — this
// fragment's own segment_num. A final fragment's segment_num is left at 0:
// that wire slot means something else once ms=false, and it is the
// caller's job to fill it in for the message kind involved.
struct Segment {
    size_t   offset      = 0;
    size_t   len          = 0;
    bool     ms           = false;
    uint16_t segment_num  = 0;
};

// The number of fragments plan() would produce for payload_len octets split
// into fragments of at most max_fragment_payload octets each (every
// fragment but the last carries exactly max_fragment_payload octets). A
// payload_len of 0 always plans to exactly one (empty, ms=false) fragment,
// regardless of max_fragment_payload. A payload_len that already fits
// within max_fragment_payload likewise always plans to exactly one
// (ms=false) fragment. Returns 0 — a value no valid plan ever produces —
// if max_fragment_payload == 0 and payload_len exceeds it
// (FragmentErrc::disabled, see plan()), or if the resulting split would
// need more than kMaxIntermediateSegments intermediate fragments to
// represent (FragmentErrc::too_many_segments).
inline size_t plan_count(size_t payload_len, size_t max_fragment_payload) noexcept {
    if (payload_len == 0) return 1;
    if (max_fragment_payload == 0) return 0;
    if (payload_len <= max_fragment_payload) return 1;

    size_t count        = (payload_len + max_fragment_payload - 1) / max_fragment_payload;
    size_t intermediate  = count - 1; // every segment but the last is intermediate
    if (intermediate > kMaxIntermediateSegments) return 0;

    return count;
}

// Fills out_segments[0..segment_count) with this module's own greedy,
// fixed-size splitting plan for payload_len octets split into fragments of
// at most max_fragment_payload octets each — see plan_count() above.
// segment_count must equal plan_count(payload_len, max_fragment_payload)
// exactly (ErrBadSegmentCount otherwise, out_segments left untouched).
// Returns ErrDisabled or ErrTooManySegments under the same conditions
// plan_count() returns 0 for (checked before the segment_count match, so
// either can be diagnosed from the same call). On success (empty
// std::error_code), a caller assembles fragment i's own ACF payload as the
// original payload's [out_segments[i].offset, out_segments[i].offset +
// out_segments[i].len) slice and encodes it with ms = out_segments[i].ms
// and, iff out_segments[i].ms, read_size_or_segment_num =
// out_segments[i].segment_num.
inline std::error_code plan(size_t payload_len, size_t max_fragment_payload,
                             Segment* out_segments, size_t segment_count) noexcept {
    size_t expected = plan_count(payload_len, max_fragment_payload);

    if (expected == 0) {
        return (max_fragment_payload == 0) ? ErrDisabled : ErrTooManySegments;
    }
    if (segment_count != expected) return ErrBadSegmentCount;

    if (expected == 1) {
        out_segments[0] = Segment{0, payload_len, false, 0};
        return {};
    }

    size_t off = 0;
    for (size_t i = 0; i + 1 < expected; i++) {
        out_segments[i] = Segment{off, max_fragment_payload, true, static_cast<uint16_t>(i)};
        off += max_fragment_payload;
    }

    out_segments[expected - 1] = Segment{off, payload_len - off, false, 0};

    return {};
}

// ── Reassembly (decode side) ────────────────────────────────────────────────

enum class ReasmResult {
    kContinue        = 0, // fragment accepted; more expected
    kComplete        = 1, // fragment accepted; reassembly finished -- call data()/size()
    kErrOutOfOrder   = 2, // an ms=true fragment's segment_num was not the expected next value; the fragment was not appended
    kErrTooLarge     = 3, // appending this fragment would exceed max_total_len (or this Reassembler's own fixed capacity); the fragment was not appended
};

// Human-readable, non-null, distinct message for every ReasmResult value,
// including unrecognized ones. Mirrors c-RCP's own
// rcp_fragment_reasm_result_string(); kept as a standalone function (rather
// than folded into an std::error_code category, as FragmentErrc's plan()
// errors are above) because ReasmResult's two non-error outcomes
// (kContinue/kComplete) have no analog in std::error_code's
// zero-means-success convention.
inline const char* to_string(ReasmResult r) noexcept {
    switch (r) {
    case ReasmResult::kContinue:      return "rcp/fragment: fragment accepted, more expected";
    case ReasmResult::kComplete:      return "rcp/fragment: fragment accepted, reassembly complete";
    case ReasmResult::kErrOutOfOrder: return "rcp/fragment: out-of-order segment_num";
    case ReasmResult::kErrTooLarge:   return "rcp/fragment: reassembled payload would exceed max_total_len";
    default:                          return "rcp/fragment: unknown result";
    }
}

// The compile-time capacity backing every Reassembler's own embedded
// std::array<uint8_t, kDefaultReassemblyCapacity> buffer — see this file's
// header comment ("Fixed-capacity from day one") for why this module owns
// no heap-growable storage at all. Sized with headroom over c-RCP's own
// documented worst real case (TC18 §13.7.11.3, CAN XL's 2048-data-octet
// write producing a 2058-octet combined reassembled payload — the exact
// figure c-RCP issue #614/#616's own writeup cites, and the same figure
// that turned out to exceed RCP_ACF_ABB_MAX_PAYLOAD, motivating that fix)
// while also covering this module's two other documented deferred callers
// (UART RX FIFO drains, discovery general-register-slice reads), neither of
// which needs anywhere close to this many octets.
constexpr size_t kDefaultReassemblyCapacity = 4096;

// Caller-owned reassembly accumulator for one request stream's worth of
// in-flight fragmented messages — see this file's header comment. One
// instance per stream a caller is reassembling fragments for.
class Reassembler {
public:
    // Constructs r as empty/not-collecting, bounding the eventual
    // reassembled payload to max_total_len octets (further capped, always,
    // by this Reassembler's own fixed kDefaultReassemblyCapacity — see the
    // file header). max_total_len defaults to that same fixed capacity.
    explicit Reassembler(size_t max_total_len = kDefaultReassemblyCapacity) noexcept
        : max_total_len_(max_total_len) {}

    // Discards any in-progress reassembly and any previously reassembled
    // payload, returning this Reassembler to the same freshly-constructed
    // state (same max_total_len it already had) — safe to call between
    // logical messages to reuse one Reassembler for a whole stream's
    // lifetime, and safe to call at any point (mid-reassembly or not) to
    // abandon whatever has been collected so far.
    void reset() noexcept {
        collecting_           = false;
        expected_segment_num_ = 0;
        len_                  = 0;
    }

    // Feeds one already-decoded ACF fragment's ms bit and payload into
    // this Reassembler. segment_num is read_size_or_segment_num's raw wire
    // value; it is consulted only when ms is true — pass whatever value
    // the frame actually carried when ms is false, it is ignored. payload
    // may be nullptr iff payload_len == 0.
    //
    // A message that was never fragmented in the first place is fed as a
    // single ms=false fragment to a freshly-constructed-or-reset
    // Reassembler: this yields kComplete immediately, with payload as the
    // whole reassembled result, without ever entering the collecting
    // state. Otherwise, the first fragment fed must be an ms=true fragment
    // carrying segment_num == 0 (any other segment_num yields
    // kErrOutOfOrder, state left untouched); every subsequent ms=true
    // fragment must carry exactly one more than the previous fragment's
    // segment_num (same error otherwise); a final ms=false fragment
    // completes the sequence regardless of its own segment_num field
    // value. kErrTooLarge is returned, and state is left unchanged (this
    // fragment is not appended), if accepting payload_len more octets
    // would exceed max_total_len OR this Reassembler's own fixed
    // kDefaultReassemblyCapacity.
    ReasmResult feed(bool ms, uint16_t segment_num, const uint8_t* payload,
                      size_t payload_len) noexcept {
        if (!collecting_) {
            if (!fits(payload_len)) return ReasmResult::kErrTooLarge;

            if (!ms) {
                append(payload, payload_len);
                return ReasmResult::kComplete;
            }

            if (segment_num != 0) return ReasmResult::kErrOutOfOrder;

            append(payload, payload_len);
            collecting_           = true;
            expected_segment_num_ = 1;
            return ReasmResult::kContinue;
        }

        if (ms && segment_num != expected_segment_num_) return ReasmResult::kErrOutOfOrder;
        if (!fits(payload_len)) return ReasmResult::kErrTooLarge;

        append(payload, payload_len);

        if (ms) {
            expected_segment_num_ = static_cast<uint16_t>(expected_segment_num_ + 1);
            return ReasmResult::kContinue;
        }

        collecting_ = false;
        return ReasmResult::kComplete;
    }

    // True iff this Reassembler currently has a fragment sequence in
    // progress (has accepted at least one ms=true fragment since the last
    // completed reassembly or reset). A pure query.
    bool is_collecting() const noexcept { return collecting_; }

    // Valid only immediately after feed() has returned kComplete
    // (undefined otherwise). The pointer is owned by this Reassembler,
    // valid until the next feed()/reset() call — not transferred to the
    // caller, since this payload was assembled out of possibly-several
    // original fragment buffers and has no single one of them to borrow
    // from. May be nullptr iff size() == 0.
    const uint8_t* data() const noexcept { return len_ == 0 ? nullptr : buf_.data(); }
    size_t         size() const noexcept { return len_; }

private:
    bool fits(size_t append_len) const noexcept {
        size_t cap = std::min(max_total_len_, kDefaultReassemblyCapacity);
        return append_len <= cap - len_;
    }

    void append(const uint8_t* payload, size_t append_len) noexcept {
        if (append_len == 0) return;
        std::copy(payload, payload + append_len, buf_.begin() + static_cast<ptrdiff_t>(len_));
        len_ += append_len;
    }

    std::array<uint8_t, kDefaultReassemblyCapacity> buf_{};
    size_t   len_                  = 0;
    size_t   max_total_len_        = 0;
    bool     collecting_           = false;
    uint16_t expected_segment_num_ = 0;
};

} // namespace fragment
} // namespace rcp

// Enable std::error_code construction from rcp::fragment::FragmentErrc.
namespace std {
template <>
struct is_error_code_enum<rcp::fragment::FragmentErrc> : true_type {};
} // namespace std
