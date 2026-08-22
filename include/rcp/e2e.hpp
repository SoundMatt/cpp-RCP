// fusa:req REQ-E2E-001
// fusa:req REQ-E2E-002
// fusa:req REQ-E2E-003
// fusa:req REQ-E2E-004
// fusa:req REQ-E2E-005
// fusa:req REQ-E2E-006
// fusa:req REQ-E2E-007
// fusa:req REQ-E2E-008
// fusa:req REQ-E2E-009
// fusa:req REQ-E2E-010
// fusa:req REQ-E2E-011
// fusa:req REQ-E2E-012
// fusa:req REQ-E2E-013
// fusa:req REQ-E2E-014
// fusa:req REQ-E2E-021
// fusa:req REQ-E2E-028
// fusa:req REQ-E2E-029
// fusa:req REQ-E2E-035
// fusa:req REQ-E2E-038
// fusa:req REQ-E2E-045
// fusa:req REQ-E2E-046

// End-to-end CRC safe points — the OPEN Alliance TC18 Remote Control
// Protocol Specification v0.5.1_RC's actual E2E integrity mechanism, plus
// the per-request-stream watchdog and safe-state primitives that the
// safety-tagged (0x8x) request variants (rcp/request.hpp) depend on
// (extraction §3.8, §4.4, §4.7, §6 item 4).
//
// ROADMAP.md milestone 50, "E2E CRC Safe Points & Safety-Request Variants
// (v2.6.0)": this header REPLACES this file's pre-replacement content in
// full, per the Satellite Package Disposition table's entry for `e2e.hpp`
// — the prior ad-hoc CRC-16/CCITT-FALSE + sequence-counter + replay-window
// wrapper around rcp.hpp's Controller is discarded, not adapted. Nothing
// else in this tree depended on that old API (only this file's own test
// did), so no legacy shim is needed here, unlike rcp/avtp.hpp's/rcp/acf.hpp's
// rcp/legacy_wire.hpp split at v2.0.0.
//
// This header rides on top of rcp/acf.hpp's AcfMessageInfo and
// rcp/avtp.hpp's StreamId (v2.0.0, split from the original rcp/wire.hpp per
// RELAY spec §13.7.2), rcp/regmap.hpp's RequestStreamConfig/EndpointGenericConfig
// (v2.1.0, expanded to their full v2.6.0 field set alongside this header),
// and rcp/request.hpp's RequestRecord/RequestLedger/SequencerTable
// (v2.5.0/v2.6.0, renamed from rcp/sequencer.hpp per RELAY spec §13.7.2)
// without modifying any of their core framing — the CRC coverage builder
// below serializes an already-built AcfMessageInfo rather than reaching
// into rcp/acf.hpp's codec internals, and the watchdog-overflow queue
// behavior reuses rcp::request::RequestLedger::cancel_all(non_safestate_only)
// directly rather than reimplementing cancellation.
//
// Field names and behavior below implement TC18's *behavior* as described
// in an internal structured extraction of the specification named above;
// no text from that document is reproduced here. The CRC32 algorithm itself
// (polynomial, init/refin/refout/xorout) was already independently
// verifiable and unchanged by this note. The coverage buffer's *byte
// layout* was corrected alongside rcp/acf.hpp's/rcp/avtp.hpp's own wire
// conformance pass (v2.19.0, issue cpp-RCP-N2-03): it now inserts an
// ACF_GBB message's 8-byte message_timestamp between the Message Info
// header and the payload — matching what rcp/acf.hpp's encode_acf_gbb
// actually transmits — where it previously omitted that field from
// coverage entirely. See rcp/acf.hpp's and rcp/avtp.hpp's own header
// comments, and this repository's pull request description, for the
// specification derivation and its honestly-stated confidence level; this
// file's coverage_buffer is a direct, mechanical consequence of that
// derivation rather than a second independent one. This header provides
// primitives, not a running scheduler or timer thread — deciding *when* to
// call overflowed()/kick()/should_emit_info_notification() is left to the
// embedding application, same as every other header in this codebase.
//
// ── Phase 2 content-parity pass (cpp-RCP issue #129) ──────────────────────
// Re-derived against c-RCP's `include/rcp/e2e.h`/`src/e2e.c` — the
// RC5-spec-conformant source of truth — which has diverged from this file
// considerably since v2.19.0/v2.22.0. Deltas found and fixed:
//
//   1. CRC coverage was missing THREE leading bytes (c-RCP issue #465,
//      "Figure 20/21 header-CRC bytes"): TC18 §13.6 Figures 20/21's own
//      orange "header CRC" region is avtp_subtype + header_octet1 + a tu
//      byte, ahead of stream_id/avtp_timestamp — this file's
//      coverage_buffer()/compute_crc()/verify_crc() never carried those
//      three bytes at all prior to this pass, a real wire-format-affecting
//      gap (a genuinely spec-conformant peer's CRC32 would not match this
//      library's prior output). Fixed by threading avtp_subtype/
//      header_octet1/tu through every CRC-computing function below,
//      mirroring c-RCP's rcp_e2e_compute_crc() exactly. See "CRC coverage &
//      the trailing-CRC length pre-adjustment" below.
//   2. wrap()/unwrap() (and their _framed() convenience wrappers) did not
//      exist at all: this file only ever exposed append_crc(), which
//      appends the CRC trailer at the very end of whatever buffer it is
//      given. TC18 §13.6 Figures 20/21 place the CRC32 immediately after
//      the REAL (unpadded) payload, with any quadlet-alignment pad octets
//      AFTER the trailer — [header][real payload][CRC32][pad], never
//      [header][real payload][pad][CRC32] (c-RCP issue #420). A caller
//      relying on append_crc() alone against a payload that already
//      includes its own trailing pad bytes (AcfMessageInfo::pad) would
//      silently produce the wrong wire order. wrap()/unwrap() below are new
//      — behavior this file never had — ported from c-RCP's rcp_e2e_wrap()/
//      _unwrap().
//   3. RxSequenceGuard's rx_enforce_seq/rx_seq_safestate_enable modeling was
//      substantively wrong, not merely incomplete: it compared `seq` as a
//      plain ever-increasing uint32_t (`seq <= last_seq_`), which cannot
//      express AVTPDU sequence_num's real 8-bit rolling nature — a
//      long-lived stream would spuriously reject every request the instant
//      the counter first wrapped 0xFF -> 0x00. It also never consulted
//      cfg.rx_seq_safestate_enable at all (the config field existed and was
//      silently unused) and had no concept of "discontinuity" (an
//      increase-but-not-by-exactly-one gap) at all. Replaced with the RFC
//      1982 forward-window comparison c-RCP's rcp_e2e_seq_evaluate() uses.
//      See "RxSequenceGuard" below for the full rationale — this is the
//      mechanism HARA.md's H-004 and FORMAL_VERIFICATION.md's RxSequenceGuard.tla
//      both describe; this pass corrects its *content* to match c-RCP, but
//      does NOT wire it into any dispatch path (see this repository's PR
//      description / HARA.md's own corrected H-004 section for why that is
//      explicitly out of scope here).
//   4. endpoint_in_configured_safe_state()'s RunSafeSequencer branch was
//      missing REQ-SEQ-012's fail-closed rule (TC18 Table 28): a
//      manually-disabled sequencer (state == 0) conveys no application-
//      state information and must never itself satisfy a safe-state check,
//      even if rx_safe_sequencer_state also happens to be (mis)configured
//      to 0. c-RCP's rcp_e2e_endpoint_in_safe_state() already applies this;
//      this file did not.
//   5. Missing entirely: fragment_carries_crc()/compute_fragmented_crc()
//      (REQ-E2E-010/038, the fragmentation/CRC interaction rule),
//      crc_error_should_enter_safe_state() (REQ-E2E-045, rx_enforce_e2e's
//      second, independent consequence per TC18 §12.7.7 Table 24 —
//      "Safe state will be entered", not just the stream-latch this file's
//      pre-existing RxStreamGuard already modeled), StreamFaultTracker
//      (REQ-E2E-021, a bounded multi-stream keyed wrapper around
//      RxStreamGuard — a real server tracks more than one stream's CRC
//      fault latch), and StreamStatus (REQ-E2E-046, TC18 0.5.1_RC5's
//      rx_stream_status aggregate: crc/seq/wd/overflow-blocked, OR'd
//      together). All four are new, additive sections below, ported from
//      c-RCP's own equivalents of the same name. Also newly added, three
//      small pure-arithmetic/predicate helpers this file never exposed
//      standalone: length_with_crc() (REQ-E2E-004), data_length_for_
//      protected_members() (REQ-E2E-037), and overflow_should_enter_safe_
//      state() (REQ-E2E-030) — ported from c-RCP's rcp_e2e_length_with_crc()/
//      _data_length_for_protected_members()/_overflow_should_enter_safe_state().
//
// What this pass deliberately does NOT do (see this repository's PR
// description and HARA.md's corrected H-004 section): wire RxSequenceGuard,
// StreamFaultTracker, or StreamStatus into rcp/mock.hpp's dispatch, or any
// transport Server — that is Phase 4 (server/dispatch) scope, matching
// c-RCP's own mock.c wiring (frame_seq_gate_admits(), dispatch_frame_e2e())
// being a materially separate architecture item from this file's own
// primitives. This file remains "primitives, not a running dispatcher",
// same as every other header in this codebase.
//
// UPDATE (Phase 4/Phase 17 batch C, cpp-RCP issue #129): the deferral two
// paragraphs above is now closed for the single-member dispatch case —
// rcp/mock.hpp's Server::dispatch_e2e() wires RxSequenceGuard (via its own
// seq_gate_admits()), StreamFaultTracker, RxWatchdog, and StreamStatus
// into a real dispatch path, matching c-RCP's own rcp_mock_server_
// dispatch_e2e() (src/mock.c:1892-2038). The full multi-member AVTPDU
// frame-level version (matching c-RCP's own frame_seq_gate_admits()/
// dispatch_frame_e2e(), src/mock.c:3038-3552) remains a later batch's job
// — see mock.hpp's own dispatch_e2e() doc comment for the exact split and
// what state a frame-level pass will reuse unchanged. The primitives below
// are still unmodified by this update: only their wiring, one file over,
// changed.
#pragma once

#include <rcp/acf.hpp>
#include <rcp/avtp.hpp>
#include <rcp/regmap.hpp>
#include <rcp/request.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace rcp {
namespace e2e {

// ── Errors ────────────────────────────────────────────────────────────────────

enum class E2eErrc : int {
    crc_error          = 1, // CRC_ERROR — computed CRC does not match the received trailer, or the stream is latched
    sequence_violation = 2, // rx_enforce_seq: received sequence number is not strictly increasing
    short_frame        = 3, // unwrap(): frame too short to contain a CRC32 trailer (+ any claimed pad octets)
};

inline const std::error_category& e2e_category() noexcept {
    struct Cat : std::error_category {
        const char* name() const noexcept override { return "rcp.e2e"; }
        std::string message(int ev) const override {
            switch (static_cast<E2eErrc>(ev)) {
            case E2eErrc::crc_error:          return "rcp/e2e: CRC_ERROR";
            case E2eErrc::sequence_violation: return "rcp/e2e: sequence number is not strictly increasing";
            case E2eErrc::short_frame:        return "rcp/e2e: frame too short for a CRC32 trailer";
            default:                          return "rcp/e2e: unknown error";
            }
        }
    };
    static Cat instance;
    return instance;
}

inline std::error_code make_error_code(E2eErrc e) noexcept {
    return {static_cast<int>(e), e2e_category()};
}

// ── Numeric TC18 wire error code (extraction Table 27; issue cpp-RCP-08) ─────
// The E2eErrc enum's own ordinal values (1, 2, ...) are this codec's
// internal std::error_code values — unrelated to, and not meant to collide
// with, the numeric error codes TC18 actually puts in an error response's
// byte_msg_payload (extraction Table 27, "Error codes in responses"; no
// other error enum in this codebase currently exposes that numeric-code
// mapping either, so there is no existing convention here to match beyond
// keeping the C++ enum's own ordinal untouched). Of E2eErrc's three values,
// only crc_error has a direct TC18 wire error code: POCI_FAILURE (12),
// "CRC of request does not match". sequence_violation has no dedicated TC18
// error code of its own in that table, and short_frame is a purely local
// framing outcome that never reaches the point of being a transmittable
// Response at all (mirrors c-RCP's rcp_e2e_wire_error()'s own treatment of
// RCP_E2E_ERR_SHORT_FRAME) — wire_error_code() reports std::nullopt for
// both rather than guessing a mapping the specification does not state.

constexpr int kPociFailureErrorCode = 12; // TC18 POCI_FAILURE — CRC of request does not match

inline std::optional<int> wire_error_code(E2eErrc e) noexcept {
    switch (e) {
    case E2eErrc::crc_error: return kPociFailureErrorCode;
    default:                 return std::nullopt;
    }
}

// ── CRC32 primitive (extraction §4.7) ────────────────────────────────────────
// 32-bit width, polynomial 0xF4ACFB13, initial value 0xFFFFFFFF, both input
// and output reflected, final XOR 0xFFFFFFFF. This replaces the ad-hoc
// CRC-16/CCITT-FALSE this file used prior to v2.6.0 in full — the two
// schemes share no code.
//
// Implemented as the standard table-less right-shifting technique for a
// RefIn=true/RefOut=true CRC (the same structure zlib's crc32 uses for its
// own, unrelated, polynomial): the working polynomial is bit-reflected once
// up front, after which every byte is folded in LSB-first with no
// per-byte reflection step needed. This is a well-known, independently
// verifiable construction, not something guessed at from the confidential
// specification text.

namespace detail {

constexpr uint32_t kCrc32PolyNormal = 0xF4ACFB13; // TC18 end-to-end CRC polynomial, non-reflected form

constexpr uint32_t reflect32(uint32_t v) noexcept {
    uint32_t r = 0;
    for (int i = 0; i < 32; ++i) {
        r = (r << 1) | (v & 1u);
        v >>= 1;
    }
    return r;
}

inline uint32_t crc32_reflected_poly() noexcept {
    static const uint32_t poly = reflect32(kCrc32PolyNormal);
    return poly;
}

inline uint32_t crc32_update(uint32_t crc, uint8_t byte_in) noexcept {
    crc ^= byte_in;
    const uint32_t poly = crc32_reflected_poly();
    for (int i = 0; i < 8; ++i) {
        crc = (crc & 1u) ? ((crc >> 1) ^ poly) : (crc >> 1);
    }
    return crc;
}

} // namespace detail

// crc32 computes the TC18 end-to-end CRC over an arbitrary byte range
// (extraction §4.7).
inline uint32_t crc32(const uint8_t* data, size_t len) noexcept {
    uint32_t crc = 0xFFFFFFFFu; // initial value
    for (size_t i = 0; i < len; ++i) crc = detail::crc32_update(crc, data[i]);
    return crc ^ 0xFFFFFFFFu; // final XOR
}

inline uint32_t crc32(const std::vector<uint8_t>& data) noexcept {
    return crc32(data.data(), data.size());
}

// ── CRC coverage & the trailing-CRC length pre-adjustment (extraction §4.7) ──
// Coverage is, in order (c-RCP issue #465, "Figure 20/21 header-CRC bytes" —
// see this file's own top-of-file "Phase 2 content-parity pass" note,
// item 1): avtp_subtype (1 byte — 0x05 TSCF / 0x82 NTSCF, avtp::kSubtypeTscf/
// kSubtypeNtscf), header_octet1 (1 byte — TSCF's packed sv|version|mr|rsv|tv
// or NTSCF's packed sv|version|r, exactly as transmitted; this codec has no
// way to derive this byte on the caller's behalf — see coverage_buffer()'s
// own doc comment below), a tu byte (1 byte, LSB = tu; forced 0 under NTSCF
// framing, which has no tu bit of its own), stream_id + avtp_timestamp
// (zero-filled when the frame rides under an NTSCF header, since NTSCF
// carries no avtp_timestamp field to begin with) + the complete ACF header +
// the complete payload. "Complete ACF header" (issue cpp-RCP-N2-03, fixed
// alongside cpp-RCP-04): for ACF_GBB this means the 8-byte Message Info
// *and* the 8-byte message_timestamp field that immediately follows it on
// the wire — the same message_timestamp encode_acf_gbb (rcp/acf.hpp) places
// right after the Message Info — not the Message Info alone. ACF_ABB has no
// message_timestamp field at all, so its coverage is the Message Info only,
// same as before. Because the CRC is computed *over* the ACF header, and the
// header's own acf_msg_length field must already reflect the trailer's
// length before that header is serialized, the length adjustment has to be
// applied first, not patched in after the fact.

constexpr uint16_t kCrcLengthAdjustQuadlets = 1; // +1 quadlet on AcfMessageInfo::acf_msg_length
constexpr uint16_t kCrcLengthAdjustOctets   = 4; // +4 octets on an outer AVTPDU frame-length field

// apply_acf_length_adjustment mutates `info.acf_msg_length` in place, per
// kCrcLengthAdjustQuadlets. This function only ever *adds* one quadlet to
// whatever `info.acf_msg_length` already holds — it does not compute a base
// length itself — so a CRC-protected caller must call
// rcp::acf::compute_acf_msg_length(info.acf_msg_type, payload.size()) (or
// otherwise set `info.acf_msg_length` to the correct base value) FIRST, and
// only then call this function, before calling encode_acf_abb/encode_acf_gbb
// (whose own cpp-RCP-01 auto-fill only fires when acf_msg_length is still
// the 0 default — it never overwrites a nonzero value this function already
// adjusted). This two-step order is required so the trailing CRC's length
// is already baked into the header before that header is serialized both
// for the real wire frame and for coverage_buffer()'s own CRC-coverage
// header encode below.
//
// TODO(phase2-followup): c-RCP's own adapt_acf_msg_length() fails safe
// (leaves the field unchanged, reports failure) if the +1 adjustment would
// push acf_msg_length past the wire field's real 9-bit ceiling (0x1FF) —
// this function has no such bounds check and silently produces an
// out-of-range value in that case. Not fixed in this pass: every existing
// caller in this tree builds acf_msg_length from real, small payloads
// (nowhere near 511 quadlets), so the practical exposure is low, and adding
// a fallible return type here would touch every existing call site
// (test_e2e.cpp's own worked-example tests among them) for a defect this
// pass has not observed manifesting. Left as a named, explicit gap rather
// than silently ported around.
inline void apply_acf_length_adjustment(acf::AcfMessageInfo& info) noexcept {
    info.acf_msg_length = static_cast<uint16_t>(info.acf_msg_length + kCrcLengthAdjustQuadlets);
}

// apply_frame_length_adjustment mutates an outer AVTPDU header's
// control_data_length in place, per kCrcLengthAdjustOctets — the paired
// +4 octet adjustment at the frame level. Templated over
// avtp::NtscfHeader/avtp::TscfHeader since both carry a control_data_length
// field of the same name but rcp/avtp.hpp deliberately keeps them as two
// independent structs with no common base to adjust through instead.
template <typename Header>
inline void apply_frame_length_adjustment(Header& hdr) noexcept {
    hdr.control_data_length = static_cast<uint16_t>(hdr.control_data_length + kCrcLengthAdjustOctets);
}

// length_with_crc is the pure arithmetic expression of the same
// kCrcLengthAdjustOctets adjustment wrap() applies internally (ported from
// c-RCP's rcp_e2e_length_with_crc()) — for a caller that wants to pre-size
// its own buffer before building a frame directly, rather than going
// through wrap(). Saturates at SIZE_MAX rather than wrapping if
// `payload_len` is already within kCrcLengthAdjustOctets of SIZE_MAX.
inline size_t length_with_crc(size_t payload_len) noexcept {
    if (payload_len > static_cast<size_t>(-1) - kCrcLengthAdjustOctets) return static_cast<size_t>(-1);
    return payload_len + kCrcLengthAdjustOctets;
}

// data_length_for_protected_members is TC18 §13.6's own data-length
// accounting rule, named as its own function (REQ-E2E-037; ported from
// c-RCP's rcp_e2e_data_length_for_protected_members()): an AVTPDU's
// ntscf_data_length/stream_data_length field must grow by
// kCrcLengthAdjustOctets for every E2E-protected ACF message its payload
// carries. rcp/avtp.hpp's encode_ntscf()/encode_tscf() already satisfy this
// automatically — both recompute the field from the actual payload buffer
// length they are given, never from a caller-supplied value, so the
// +4-per-protected-member accounting is always right as long as the caller
// concatenated wrap()'s (or wrap_framed()'s) own output for each protected
// member before calling either encoder. This function exists for a caller
// that wants to reason about, or pre-validate, the expected delta
// independently of actually building the payload. Saturates at SIZE_MAX on
// overflow, same discipline as length_with_crc() above.
inline size_t data_length_for_protected_members(size_t protected_member_count) noexcept {
    if (protected_member_count > static_cast<size_t>(-1) / kCrcLengthAdjustOctets) return static_cast<size_t>(-1);
    return protected_member_count * kCrcLengthAdjustOctets;
}

namespace detail {
inline void put_u32_be(std::vector<uint8_t>& buf, uint32_t v) {
    buf.push_back(static_cast<uint8_t>(v >> 24));
    buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>(v & 0xFF));
}
inline void put_u64_be(std::vector<uint8_t>& buf, uint64_t v) {
    for (int i = 0; i < 8; ++i) buf.push_back(static_cast<uint8_t>((v >> (56 - 8 * i)) & 0xFF));
}
inline uint32_t get_u32_be(const uint8_t* p) noexcept {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}
} // namespace detail

// coverage_buffer assembles the exact byte sequence the E2E CRC is computed
// over (extraction §4.7; c-RCP issue #465's header-CRC-bytes fix — see this
// file's own top-of-file note). `avtp_subtype` is one of avtp::kSubtypeTscf/
// avtp::kSubtypeNtscf. `header_octet1` is the real, already-encoded (or
// about-to-be-decoded) second wire octet of the outer NTSCF/TSCF header
// (TSCF's packed sv|version|mr|rsv|tv, or NTSCF's packed sv|version|r),
// exactly as transmitted — mr and tv are genuinely per-message wire values
// this codec cannot derive on a caller's behalf (mirroring c-RCP's own
// rationale, e2e.h), so it remains a required, explicit parameter on every
// CRC-computing function in this section; prefer the `_framed` convenience
// wrappers below when `header_octet1` is already in hand from a decoded/
// about-to-be-encoded avtp::NtscfHeader/TscfHeader (`sv`, `version`, and for
// TSCF `mr`/`timestamp_valid`, packed by the caller the same way
// rcp/avtp.hpp's own encode_ntscf/encode_tscf pack them). `tu` is the TSCF
// "avtp_timestamp uncertain" bit — pass false for NTSCF, which has no tu bit
// of its own (the `_framed` wrappers below force this automatically).
// `avtp_timestamp` is std::nullopt when the frame rides under an NTSCF
// header — coverage_buffer supplies the documented zero-filled stand-in in
// that case rather than requiring the caller to pass an explicit zero
// (making the NTSCF case a visible, named choice at every call site instead
// of an easy-to-miss "just pass 0"). `message_timestamp` is std::nullopt for
// ACF_ABB (which has no such field) and the actual 64-bit value passed to
// encode_acf_gbb for ACF_GBB — coverage_buffer only inserts it into the
// buffer when `info.acf_msg_type == acf::kAcfMsgTypeGbb`, regardless of what
// the caller passes for a non-GBB `info`, so a caller cannot accidentally
// cover 8 bytes of timestamp for an ACF_ABB message. When it is inserted it
// goes at its real wire position — immediately after the Message Info
// block's two header quadlets, contiguous, not spliced between them (see
// acf.hpp's own kAcfGbbTimestampOffset). `info` must already have
// apply_acf_length_adjustment() applied if the caller wants the trailer's
// length reflected in the coverage — this function only serializes whatever
// AcfMessageInfo (and message_timestamp) it is given.
inline std::vector<uint8_t> coverage_buffer(uint8_t avtp_subtype, uint8_t header_octet1, bool tu,
                                             const avtp::StreamId& stream_id,
                                             std::optional<uint32_t> avtp_timestamp,
                                             const acf::AcfMessageInfo& info,
                                             std::optional<uint64_t> message_timestamp,
                                             const std::vector<uint8_t>& payload) {
    const bool is_gbb = (info.acf_msg_type == acf::kAcfMsgTypeGbb);

    std::vector<uint8_t> buf;
    buf.reserve(3 + 8 + 4 + acf::kAcfCommonHeaderLen +
                (is_gbb ? acf::kAcfGbbTimestampLen : 0) + payload.size());

    // TC18 §13.6 Figures 20/21's own orange "header CRC" bytes, in their
    // exact left-to-right wire order (c-RCP issue #465).
    buf.push_back(avtp_subtype);
    buf.push_back(header_octet1);
    buf.push_back(tu ? uint8_t{0x01} : uint8_t{0x00});

    detail::put_u64_be(buf, stream_id.to_u64());
    detail::put_u32_be(buf, avtp_timestamp.value_or(0)); // zero-filled stand-in under NTSCF

    // The Message Info block is serialized here in exactly the byte order
    // it has on the wire, which differs between the two message types: for
    // ACF_ABB the 8 header bytes are contiguous, while for ACF_GBB the
    // 64-bit message_timestamp follows immediately after the complete
    // 8-byte header. Going through acf::encode_acf_gbb_message_info rather
    // than re-laying-out the fields here keeps this coverage buffer and
    // acf::encode_acf_gbb byte-identical by construction — the CRC must
    // cover the bytes that are actually transmitted, so any divergence
    // between the two would make every ACF_GBB CRC wrong on the wire.
    if (is_gbb) {
        uint8_t hdr[acf::kAcfGbbMessageInfoLen];
        acf::encode_acf_gbb_message_info(info, message_timestamp.value_or(0), hdr);
        buf.insert(buf.end(), hdr, hdr + acf::kAcfGbbMessageInfoLen);
    } else {
        uint8_t hdr[acf::kAcfCommonHeaderLen];
        acf::encode_acf_message_info(info, hdr);
        buf.insert(buf.end(), hdr, hdr + acf::kAcfCommonHeaderLen);
    }

    buf.insert(buf.end(), payload.begin(), payload.end());
    return buf;
}

// compute_crc is coverage_buffer() + crc32() in one call — the usual way a
// caller actually wants this used.
inline uint32_t compute_crc(uint8_t avtp_subtype, uint8_t header_octet1, bool tu,
                             const avtp::StreamId& stream_id, std::optional<uint32_t> avtp_timestamp,
                             const acf::AcfMessageInfo& info, std::optional<uint64_t> message_timestamp,
                             const std::vector<uint8_t>& payload) {
    return crc32(coverage_buffer(avtp_subtype, header_octet1, tu, stream_id, avtp_timestamp, info,
                                  message_timestamp, payload));
}

// compute_crc_framed is compute_crc()'s framing-safe convenience wrapper
// (ported from c-RCP's rcp_e2e_wrap_framed()'s own derivation of these same
// three parameters): derives avtp_subtype from is_ntscf_framed itself
// (avtp::kSubtypeNtscf/kSubtypeTscf — hardcoded internally so a caller
// cannot pass a subtype inconsistent with its own is_ntscf_framed argument),
// and forces the CRC's avtp_timestamp contribution to the documented
// zero stand-in AND its tu contribution to false when is_ntscf_framed is
// true (NTSCF carries neither field), or passes avtp_timestamp/tu through
// unchanged when is_ntscf_framed is false. header_octet1 is always passed
// through unchanged regardless of framing — both TSCF and NTSCF have a
// real, meaningful second header octet of their own.
inline uint32_t compute_crc_framed(bool is_ntscf_framed, uint8_t header_octet1,
                                    const avtp::StreamId& stream_id, bool tu,
                                    std::optional<uint32_t> avtp_timestamp, const acf::AcfMessageInfo& info,
                                    std::optional<uint64_t> message_timestamp,
                                    const std::vector<uint8_t>& payload) {
    const uint8_t subtype = is_ntscf_framed ? avtp::kSubtypeNtscf : avtp::kSubtypeTscf;
    return compute_crc(subtype, header_octet1, is_ntscf_framed ? false : tu, stream_id,
                        is_ntscf_framed ? std::optional<uint32_t>{} : avtp_timestamp, info,
                        message_timestamp, payload);
}

// append_crc appends the 4-octet big-endian CRC trailer to `frame` — the
// kCrcLengthAdjustOctets the frame-length field above must already account
// for.
inline void append_crc(std::vector<uint8_t>& frame, uint32_t crc) {
    frame.push_back(static_cast<uint8_t>(crc >> 24));
    frame.push_back(static_cast<uint8_t>((crc >> 16) & 0xFF));
    frame.push_back(static_cast<uint8_t>((crc >> 8) & 0xFF));
    frame.push_back(static_cast<uint8_t>(crc & 0xFF));
}

// verify_crc recomputes the CRC over
// avtp_subtype/header_octet1/tu/stream_id/avtp_timestamp/info/message_timestamp/payload
// and compares it against `received_crc` (as decoded from a frame's
// trailing 4 octets). Returns E2eErrc::crc_error — the CRC_ERROR failure
// path, whose numeric TC18 wire error code is e2e::kPociFailureErrorCode /
// e2e::wire_error_code() above — on mismatch.
inline std::error_code verify_crc(uint8_t avtp_subtype, uint8_t header_octet1, bool tu,
                                   const avtp::StreamId& stream_id, std::optional<uint32_t> avtp_timestamp,
                                   const acf::AcfMessageInfo& info, std::optional<uint64_t> message_timestamp,
                                   const std::vector<uint8_t>& payload, uint32_t received_crc) {
    if (compute_crc(avtp_subtype, header_octet1, tu, stream_id, avtp_timestamp, info, message_timestamp,
                     payload) != received_crc)
        return make_error_code(E2eErrc::crc_error);
    return {};
}

// verify_crc_framed is verify_crc()'s own framing-safe convenience wrapper,
// mirroring compute_crc_framed() above.
inline std::error_code verify_crc_framed(bool is_ntscf_framed, uint8_t header_octet1,
                                          const avtp::StreamId& stream_id, bool tu,
                                          std::optional<uint32_t> avtp_timestamp,
                                          const acf::AcfMessageInfo& info,
                                          std::optional<uint64_t> message_timestamp,
                                          const std::vector<uint8_t>& payload, uint32_t received_crc) {
    if (compute_crc_framed(is_ntscf_framed, header_octet1, stream_id, tu, avtp_timestamp, info,
                            message_timestamp, payload) != received_crc)
        return make_error_code(E2eErrc::crc_error);
    return {};
}

// ── wrap / unwrap (c-RCP issue #420; new — see this file's own top-of-file
// "Phase 2 content-parity pass" note, item 2) ─────────────────────────────
// TC18 §13.6 Figures 20/21 place the trailing CRC32 immediately after the
// REAL (unpadded) header-and-payload region, with any quadlet-alignment pad
// octets AFTER the trailer: [header][real payload][CRC32][pad], never
// [header][real payload][pad][CRC32] — append_crc() alone cannot express
// this when `payload` already carries its own trailing pad bytes
// (AcfMessageInfo::pad, this codec's own caller-owns-padding convention;
// see acf.hpp's file header). wrap()/unwrap() are the composed entry points
// that get this order right, ported from c-RCP's rcp_e2e_wrap()/_unwrap().

// wrap adapts a copy of `info` (apply_acf_length_adjustment(), +1 quadlet)
// and returns the complete wire frame: the freshly-encoded ACF_ABB/ACF_GBB
// header, the REAL (unpadded) prefix of `payload` (payload.size() -
// info.pad bytes), the 4-octet CRC32 trailer computed over exactly that
// header-and-real-payload region, and finally `info.pad` trailing pad
// octets copied unchanged from the tail of `payload` (a faithful transport
// for whatever bytes the caller's own pad octets held, not a memset(0) —
// matching unwrap()'s own byte-identical round-trip contract). Dispatches
// on `info.acf_msg_type` to call acf::encode_acf_gbb (with
// `message_timestamp.value_or(0)`) or acf::encode_acf_abb. Returns an empty
// vector, `info` left unmodified, if `info.pad` exceeds `payload.size()`
// (malformed input — nothing to reseat).
inline std::vector<uint8_t> wrap(uint8_t avtp_subtype, uint8_t header_octet1, bool tu,
                                  const avtp::StreamId& stream_id, std::optional<uint32_t> avtp_timestamp,
                                  acf::AcfMessageInfo info, std::optional<uint64_t> message_timestamp,
                                  const std::vector<uint8_t>& payload) {
    const size_t pad_octets = info.pad;
    if (pad_octets > payload.size()) return {};
    const size_t real_len = payload.size() - pad_octets;

    std::vector<uint8_t> real_payload(payload.begin(), payload.begin() + static_cast<long>(real_len));

    // Mirror acf::encode_acf_abb()/_gbb()'s own cpp-RCP-01 auto-fill (0
    // means "compute it for me from the real payload I'm actually giving
    // you") BEFORE applying the +1 adjustment below — apply_acf_length_
    // adjustment()'s own doc comment requires its caller to have already
    // set a correct base length; doing that here, once, means a wrap()
    // caller does not have to remember acf::compute_acf_msg_length() as a
    // separate precondition (a caller that already set a nonzero
    // info.acf_msg_length is, as always, respected unchanged).
    if (info.acf_msg_length == 0)
        info.acf_msg_length = acf::compute_acf_msg_length(info.acf_msg_type, real_payload.size());
    apply_acf_length_adjustment(info); // +1 quadlet, reflected in the header below

    const uint32_t crc = compute_crc(avtp_subtype, header_octet1, tu, stream_id, avtp_timestamp, info,
                                      message_timestamp, real_payload);

    std::vector<uint8_t> out = (info.acf_msg_type == acf::kAcfMsgTypeGbb)
                                    ? acf::encode_acf_gbb(info, message_timestamp.value_or(0), real_payload)
                                    : acf::encode_acf_abb(info, real_payload);
    append_crc(out, crc);
    out.insert(out.end(), payload.begin() + static_cast<long>(real_len), payload.end());
    return out;
}

// wrap_framed is wrap()'s own framing-safe convenience wrapper (see
// compute_crc_framed()'s own doc comment above for the exact forcing rule).
inline std::vector<uint8_t> wrap_framed(bool is_ntscf_framed, uint8_t header_octet1, bool tu,
                                         const avtp::StreamId& stream_id,
                                         std::optional<uint32_t> avtp_timestamp, acf::AcfMessageInfo info,
                                         std::optional<uint64_t> message_timestamp,
                                         const std::vector<uint8_t>& payload) {
    const uint8_t subtype = is_ntscf_framed ? avtp::kSubtypeNtscf : avtp::kSubtypeTscf;
    return wrap(subtype, header_octet1, is_ntscf_framed ? false : tu, stream_id,
                is_ntscf_framed ? std::optional<uint32_t>{} : avtp_timestamp, info, message_timestamp,
                payload);
}

// UnwrapResult carries unwrap()'s own two outputs: whether the CRC matched
// (`ok`, an E2eErrc — short_frame if `frame` was too short to contain both
// the header and a CRC32 trailer, crc_error on mismatch, or a default
// std::error_code{} on success) and the reassembled ACF header-and-payload
// region (`acf_frame`) — header-and-real-payload immediately followed by
// the original pad octets, acf_msg_length adapted back down by one quadlet,
// ready to hand to acf::decode_acf_abb()/decode_acf_gbb() unmodified.
// `acf_frame` is still populated on a crc_error verdict (for diagnostic
// use) but must not be treated as a validated payload; it is left empty on
// short_frame (nothing to reassemble).
struct UnwrapResult {
    std::error_code       ec;
    std::vector<uint8_t>  acf_frame;
};

// unwrap reverses wrap(): `frame` is [real header-and-payload][CRC32][pad
// octets] (TC18 §13.6 Figures 20/21), not a trailer simply appended to the
// end. The real/pad split is read directly out of the header's own `pad`
// field (byte 2, bits 7:6 — acf::decode_acf_message_info) rather than
// requiring the caller to pre-split `frame`, mirroring c-RCP's own
// acf_pad_octets() convention (there implemented as a raw-byte read; here,
// since this codec already models `pad` as an AcfMessageInfo field, this
// function decodes the header once via acf::decode_acf_message_info()/
// decode_acf_gbb_message_info() to recover it, then reconstructs the exact
// AcfMessageInfo + message_timestamp coverage_buffer() needs). Requires
// `frame.size()` to be at least acf::kAcfCommonHeaderLen (to read the pad
// field at all) and at least real_len + kCrcLengthAdjustOctets + pad_octets
// (to actually contain the CRC and every claimed pad octet); returns
// UnwrapResult{short_frame, {}} otherwise.
inline UnwrapResult unwrap(uint8_t avtp_subtype, uint8_t header_octet1, bool tu,
                            const avtp::StreamId& stream_id, std::optional<uint32_t> avtp_timestamp,
                            const std::vector<uint8_t>& frame) {
    if (frame.size() < acf::kAcfCommonHeaderLen)
        return {make_error_code(E2eErrc::short_frame), {}};

    acf::AcfMessageInfo peek;
    acf::decode_acf_message_info(frame.data(), peek);
    const bool   is_gbb       = (peek.acf_msg_type == acf::kAcfMsgTypeGbb);
    const size_t header_len   = is_gbb ? acf::kAcfGbbMessageInfoLen : acf::kAcfCommonHeaderLen;
    const size_t pad_octets   = peek.pad;

    // frame must be at least large enough to contain a full header, the
    // CRC32 trailer, and every claimed pad octet — i.e. real_len (computed
    // below) must not fall short of header_len, or the header/payload slice
    // below would be sliced out of bounds.
    if (frame.size() < header_len + kCrcLengthAdjustOctets + pad_octets)
        return {make_error_code(E2eErrc::short_frame), {}};

    const size_t real_len = frame.size() - kCrcLengthAdjustOctets - pad_octets;

    acf::AcfMessageInfo info;
    uint64_t            message_timestamp = 0;
    std::optional<uint64_t> ts_opt;
    if (is_gbb) {
        acf::decode_acf_gbb_message_info(frame.data(), info, message_timestamp);
        ts_opt = message_timestamp;
    } else {
        acf::decode_acf_message_info(frame.data(), info);
    }

    const std::vector<uint8_t> real_payload(frame.begin() + static_cast<long>(header_len),
                                             frame.begin() + static_cast<long>(real_len));

    const uint32_t got  = detail::get_u32_be(frame.data() + real_len);
    const uint32_t want = compute_crc(avtp_subtype, header_octet1, tu, stream_id, avtp_timestamp, info,
                                       ts_opt, real_payload);

    // Reassemble the plain ACF message acf::decode_acf_abb()/_gbb() expect:
    // header-and-real-payload immediately followed by the pad octets, with
    // the CRC32 that used to sit between them on the wire excised, and
    // acf_msg_length adapted back down by one quadlet (mirrors wrap()'s +1
    // in reverse). Re-encoding the header from the already-decoded `info`
    // is byte-identical to the source bytes by construction (encode/decode
    // are exact inverses for every field this struct models), so this is
    // not a second, independently-risked serialization of the header.
    std::vector<uint8_t> body(frame.begin(), frame.begin() + static_cast<long>(header_len));
    body.insert(body.end(), real_payload.begin(), real_payload.end()); // real_payload already excludes the header
    body.insert(body.end(), frame.begin() + static_cast<long>(real_len + kCrcLengthAdjustOctets), frame.end());

    // Un-adapt acf_msg_length by one quadlet and re-stamp the header bytes
    // in place — harmless (no-op on the reconstructed body) if
    // acf_msg_length is already 0, which acf::decode_acf_abb/_gbb will
    // reject as a short/invalid frame regardless.
    info.acf_msg_length = static_cast<uint16_t>(info.acf_msg_length - kCrcLengthAdjustQuadlets);
    if (is_gbb) {
        acf::encode_acf_gbb_message_info(info, message_timestamp, body.data());
    } else {
        acf::encode_acf_message_info(info, body.data());
    }

    std::error_code ec = (got == want) ? std::error_code{} : make_error_code(E2eErrc::crc_error);
    return {ec, std::move(body)};
}

// unwrap_framed is unwrap()'s own framing-safe convenience wrapper.
inline UnwrapResult unwrap_framed(bool is_ntscf_framed, uint8_t header_octet1, bool tu,
                                   const avtp::StreamId& stream_id, std::optional<uint32_t> avtp_timestamp,
                                   const std::vector<uint8_t>& frame) {
    const uint8_t subtype = is_ntscf_framed ? avtp::kSubtypeNtscf : avtp::kSubtypeTscf;
    return unwrap(subtype, header_octet1, is_ntscf_framed ? false : tu, stream_id,
                  is_ntscf_framed ? std::optional<uint32_t>{} : avtp_timestamp, frame);
}

// ── Fragmentation/CRC interaction (REQ-E2E-010/038; modeled here, activated
// once a fragmentation-aware codec exists — see rcp/fragment.hpp) ──────────
// Only the last fragment of a multi-segment message carries a CRC (computed
// across the fully reassembled payload); the length-accounting
// pre-adjustment applies only to that final segment.

// fragment_carries_crc is the pure, directly-testable expression of that
// rule — literally is_last_fragment, since only a multi-segment message's
// final fragment ever does.
constexpr bool fragment_carries_crc(bool is_last_fragment) noexcept { return is_last_fragment; }

// compute_fragmented_crc is TC18 §13.6's fragmented-message CRC coverage
// rule, the one case compute_crc() alone cannot express: for a message
// split across more than one AVTPDU, the CRC32 spans avtp_subtype +
// header_octet1 + tu + stream_id + avtp_timestamp (as always) followed by
// the FIRST fragment's ACF header — not the last fragment's, even though
// the trailer this CRC produces is the one appended to (and only to) the
// last fragment's own message — followed by the concatenated payload of
// EVERY segment in order. `first_fragment_header` is the first fragment's
// own encoded byte_message_info bytes (acf::kAcfCommonHeaderLen or
// acf::kAcfGbbMessageInfoLen octets); `reassembled_payload` is the full
// concatenation of every segment's own payload slice in order, NOT any
// single fragment's own slice.
inline uint32_t compute_fragmented_crc(uint8_t avtp_subtype, uint8_t header_octet1, bool tu,
                                        const avtp::StreamId& stream_id,
                                        std::optional<uint32_t> avtp_timestamp,
                                        const std::vector<uint8_t>& first_fragment_header,
                                        const std::vector<uint8_t>& reassembled_payload) {
    std::vector<uint8_t> buf;
    buf.reserve(3 + 8 + 4 + first_fragment_header.size() + reassembled_payload.size());
    buf.push_back(avtp_subtype);
    buf.push_back(header_octet1);
    buf.push_back(tu ? uint8_t{0x01} : uint8_t{0x00});
    detail::put_u64_be(buf, stream_id.to_u64());
    detail::put_u32_be(buf, avtp_timestamp.value_or(0));
    buf.insert(buf.end(), first_fragment_header.begin(), first_fragment_header.end());
    buf.insert(buf.end(), reassembled_payload.begin(), reassembled_payload.end());
    return crc32(buf);
}

// ── Per-endpoint opt-in safe mode (extraction §4.4, §4.7) ────────────────────
// Which of an ACF message's three roles the caller is currently
// checking/producing a CRC for — regmap::EndpointGenericConfig's three
// ep_*_crc_enable toggles are independently settable per role.

enum class MessageRole { Request, Acknowledge, Response };

// crc_required reports whether `cfg` opts the given message role into E2E
// CRC checking. A caller that gets `false` back should neither compute nor
// expect a trailing CRC for that role.
inline bool crc_required(const regmap::EndpointGenericConfig& cfg, MessageRole role) noexcept {
    switch (role) {
    case MessageRole::Request:     return cfg.ep_req_crc_enable;
    case MessageRole::Acknowledge: return cfg.ep_ack_crc_enable;
    case MessageRole::Response:    return cfg.ep_response_crc_enable;
    }
    return false;
}

// implemented_options_bit reports regmap::kOptSafetyRequests when the
// caller's server actually implements the E2E CRC safe-point / safety-
// request mechanism — an explicit, testable single call site rather than a
// bit a caller might set by hand and forget to gate on real support,
// mirroring rcp::request::implemented_options_bits' equivalent pattern.
inline uint32_t implemented_options_bit(bool safety_requests_implemented) noexcept {
    return safety_requests_implemented ? regmap::kOptSafetyRequests : 0;
}

// ── RxStreamGuard — rx_enforce_e2e (extraction §3.8) ─────────────────────────
// Implements the per-request-drop vs. whole-stream-latch choice: with
// rx_enforce_e2e clear, one failing request's CRC_ERROR is reported for
// that request alone; with it set, the first failure additionally latches
// the whole stream so every subsequent request also reports CRC_ERROR
// until reset_latch() is called (e.g. on stream reconfiguration).
class RxStreamGuard {
public:
    // record_crc_result is called once per checked request on this stream.
    // `ok` is the outcome of that request's own verify_crc() call.
    std::error_code record_crc_result(const regmap::RequestStreamConfig& cfg, bool ok) noexcept {
        if (latched_) return make_error_code(E2eErrc::crc_error);
        if (ok) return {};
        if (cfg.rx_enforce_e2e) latched_ = true;
        return make_error_code(E2eErrc::crc_error);
    }

    bool latched() const noexcept { return latched_; }
    void reset_latch() noexcept { latched_ = false; }

private:
    bool latched_ = false;
};

// crc_error_should_enter_safe_state names rx_enforce_e2e's own second,
// independent consequence (REQ-E2E-045; c-RCP issue #256 Group I): TC18
// §12.7.7 Table 24 documents 0x000D.0 rx_enforce_e2e's 1b value as
// triggering BOTH "stream is blocked until released" (RxStreamGuard's own
// latch above) AND, in the same sentence, "Safe state will be entered".
// Unlike its wd/overflow/seq siblings, rx_enforce_e2e has no separate
// dedicated safestate-enable bit of its own gating this — the one bit
// drives both consequences, so this is simply rx_enforce_e2e's own value,
// not a second input ANDed against it.
constexpr bool crc_error_should_enter_safe_state(bool rx_enforce_e2e) noexcept { return rx_enforce_e2e; }

// ── StreamFaultTracker — the multi-stream keyed wrapper RxStreamGuard needs
// (REQ-E2E-021; c-RCP issue #201) ─────────────────────────────────────────
// RxStreamGuard above is one stream's own fault latch; a real server tracks
// more than one request stream, each with its own independent latch. This
// is the caller-owned, keyed-by-stream_id wrapper holding one RxStreamGuard
// PER STREAM, ported from c-RCP's rcp_e2e_stream_fault_tracker_t. Fixed
// capacity, std::array-backed — no heap allocation, matching this
// codebase's own established "bounded structure, ported from c-RCP's own
// alloc.h-seamed capacity choice" convention (rcp/fragment.hpp's
// reassembler, rcp/respqueue.hpp, rcp/loan.hpp). c-RCP's own capacity,
// RCP_E2E_STREAM_FAULT_TRACKER_MAX_STREAMS (16, e2e.h), is not itself
// spec-derived — c-RCP's own header comment names it "matching the scale of
// RCP_MOCK_MAX_ENDPOINTS as a plausible real-device stream count" — so this
// port reuses the same number for the same reason, not a value this file
// independently chose.
class StreamFaultTracker {
public:
    static constexpr size_t kMaxStreams = 16; // c-RCP: RCP_E2E_STREAM_FAULT_TRACKER_MAX_STREAMS

    // on_crc_error applies a CRC_ERROR observed on `stream_id` (configured
    // with `rx_enforce_e2e`) to that stream's own tracked fault state,
    // registering stream_id as newly-tracked on its first touch if capacity
    // remains. Returns true for a stream that was or became tracked; false,
    // leaving the tracker entirely unchanged, only if stream_id is not
    // already tracked AND every slot is already in use — an honest
    // capacity-exhaustion degrade, not silently dropped state.
    bool on_crc_error(uint64_t stream_id, bool rx_enforce_e2e) noexcept {
        Slot* slot = find(stream_id);
        if (!slot) {
            slot = find_free();
            if (!slot) return false;
            slot->used      = true;
            slot->stream_id = stream_id;
            slot->guard     = RxStreamGuard{};
        }
        regmap::RequestStreamConfig cfg;
        cfg.rx_enforce_e2e = rx_enforce_e2e;
        (void)slot->guard.record_crc_result(cfg, /*ok=*/false);
        return true;
    }

    // True iff stream_id is currently tracked AND latched faulted. False,
    // not an error, for a stream_id this tracker has never seen (vacuously
    // not faulted).
    bool is_faulted(uint64_t stream_id) const noexcept {
        const Slot* slot = find(stream_id);
        return slot && slot->guard.latched();
    }

    // Clears stream_id's own tracked fault state back to not-faulted
    // (TC18 §12.7.7's own "until released" — the release mechanism itself
    // is a caller concern this function does not model). A no-op, not an
    // error, for a stream_id this tracker has never seen.
    void reset(uint64_t stream_id) noexcept {
        Slot* slot = find(stream_id);
        if (slot) slot->guard.reset_latch();
    }

private:
    struct Slot {
        uint64_t      stream_id = 0;
        bool          used      = false;
        RxStreamGuard guard;
    };

    Slot* find(uint64_t stream_id) noexcept {
        for (auto& s : slots_)
            if (s.used && s.stream_id == stream_id) return &s;
        return nullptr;
    }
    const Slot* find(uint64_t stream_id) const noexcept {
        for (auto& s : slots_)
            if (s.used && s.stream_id == stream_id) return &s;
        return nullptr;
    }
    Slot* find_free() noexcept {
        for (auto& s : slots_)
            if (!s.used) return &s;
        return nullptr;
    }

    std::array<Slot, kMaxStreams> slots_{};
};

// ── StreamStatus — the aggregate rx_stream_status bit (REQ-E2E-046;
// c-RCP issue #201/#336) ──────────────────────────────────────────────────
// TC18 0.5.1_RC5's own rx_stream_status (0x000D.7, read-only) is set
// automatically as a reaction to a CRC error, sequence error, watchdog
// overflow, or request-storage overflow, whichever of those four is
// enabled for the stream. This is a passive, client-polled AGGREGATE
// distinct from each cause's own one-shot "should enter safe state now"
// verdict (RxSequenceGuard::evaluate()'s SeqResult::enter_safe_state,
// RxWatchdog::overflowed() && cfg.rx_wd_safestate_enable, ...) — it is
// instead a PERSISTED "is the stream currently blocked" state a client can
// poll at any later time, the same shape RxStreamGuard already establishes
// for the CRC cause alone. Reuses RxStreamGuard for the CRC latch
// unchanged (composition, not duplication) and adds three sibling bool
// latches of the identical shape for the other three fault classes. Each
// latch has its own independent reset, since TC18 gives each of the four
// underlying fault classes its own distinct release condition. rx_blocked()
// is the pure aggregate read: true iff ANY of the four latches is currently
// set — TC18's own "set...as a reaction to either CRC error, sequence
// error, watchdog overflow, EP overflow, when enabled" read plainly as a
// logical OR across whichever causes are enabled for that stream.
class StreamStatus {
public:
    // CRC cause: applies a CRC_ERROR exactly as RxStreamGuard::record_crc_result
    // does (delegates to it directly).
    void note_crc_error(bool rx_enforce_e2e) noexcept {
        regmap::RequestStreamConfig cfg;
        cfg.rx_enforce_e2e = rx_enforce_e2e;
        (void)crc_.record_crc_result(cfg, /*ok=*/false);
    }
    // Sequence/watchdog/overflow causes: latches the corresponding bit
    // permanently (until the matching reset_*()) iff `enter_safe_state` is
    // true — a verdict the caller already computed via
    // RxSequenceGuard::evaluate()'s SeqResult::enter_safe_state,
    // RxWatchdog::overflowed(cfg, now) && cfg.rx_wd_safestate_enable, or
    // apply_queue_overflow()'s own decision, respectively.
    void note_seq(bool enter_safe_state) noexcept { if (enter_safe_state) seq_blocked_ = true; }
    void note_wd(bool enter_safe_state) noexcept { if (enter_safe_state) wd_blocked_ = true; }
    void note_overflow(bool enter_safe_state) noexcept { if (enter_safe_state) overflow_blocked_ = true; }

    void reset_crc() noexcept { crc_.reset_latch(); }
    void reset_seq() noexcept { seq_blocked_ = false; }
    void reset_wd() noexcept { wd_blocked_ = false; }
    void reset_overflow() noexcept { overflow_blocked_ = false; }

    // The rx_stream_status wire bit itself: true iff any of the four
    // latches (crc, seq_blocked, wd_blocked, overflow_blocked) is currently
    // set.
    bool rx_blocked() const noexcept {
        return crc_.latched() || seq_blocked_ || wd_blocked_ || overflow_blocked_;
    }

private:
    RxStreamGuard crc_;
    bool          seq_blocked_      = false;
    bool          wd_blocked_       = false;
    bool          overflow_blocked_ = false;
};

// ── RxSequenceGuard — rx_enforce_seq / rx_seq_safestate_enable ───────────────
// Monotonic sequence-number check, orthogonal to the watchdog below — a
// stream can enforce either, both, or neither independently (extraction
// §3.8, TC18 §12.7.7 Table 24). Content-corrected against c-RCP's
// rcp_e2e_seq_evaluate() (REQ-E2E-028/029) during the Phase 2 pass — see
// this file's own top-of-file note, item 3, for what was wrong before.
//
// TC18 Table 24 defines two independently-configurable reactions to a
// request stream's AVTPDU sequence_num, evaluated together here because
// both compare the same incoming seq against the same tracked state, but
// deliberately not collapsed into one bool — they answer different
// questions and either can be enabled without the other:
//
//   - rx_enforce_seq: "Requests are only filed for execution if sequence
//     number in AVTPDU is increased" — the coarser, admission-gating check.
//     SeqResult::accept is true whenever !rx_enforce_seq (the gate is off)
//     or seq is strictly ahead of the tracked value (see the wraparound
//     note below); false when seq is stale (a replay or reorder) and
//     rx_enforce_seq is on.
//
//   - rx_seq_safestate_enable: "bring all endpoints to safety state if
//     Sequence_Nr has no single increment" — a stricter, independent check
//     for a *gap* (seq advanced by more than one, e.g. a request was lost
//     in transit), which fires even when SeqResult::accept is true, because
//     an increase-but-not-by-exactly-one is still evidence something is
//     wrong even though ordering itself was preserved.
//
// Wraparound: AVTPDU sequence_num (avtp::TscfHeader::sequence_num /
// avtp::NtscfHeader::sequence_num) is a plain uint8_t that free-runs and
// wraps 0xFF -> 0x00 over any long-lived stream — TC18's own prose ("a
// strict monotonous increasing sequence number of the requests can be
// enforced") does not spell out modular comparison, but a literal
// always-greater-than reading would make rx_enforce_seq reject every single
// request once the counter first wraps, which cannot be the intended
// behavior of a mechanism meant to run indefinitely. This class instead
// uses the standard serial-number comparison technique (RFC 1982): seq is
// "ahead" of the tracked value iff their unsigned difference, taken modulo
// 256, lies in [1, 127] — the nearer half of the circle in the forward
// direction — which treats 0x00 as ahead of 0xFF (a real wrap) while still
// rejecting a seq that jumped backward by any amount up to half the space
// (a replay). "Exactly one increment" for discontinuity is unambiguous
// regardless: seq == (uint8_t)(tracked + 1).
//
// Tracked state advances only when SeqResult::accept is true: the "last
// accepted sequence number" is specifically the previously ACCEPTED request
// on this stream, not merely the last seq observed — advancing on a
// rejected (stale/replayed) seq would drag the reference point backward and
// weaken this same check's detection of the genuine next request.
struct SeqResult {
    bool accept;           // the request may be filed for execution
    bool discontinuity;    // seq did not advance by exactly one increment
                            // from the previously tracked value (never true
                            // on the first call — nothing to compare against yet)
    bool enter_safe_state; // discontinuity && rx_seq_safestate_enable
};

class RxSequenceGuard {
public:
    // evaluate is this class's own primary entry point, matching c-RCP's
    // rcp_e2e_seq_evaluate() field for field. The first observed sequence
    // number on a stream is always accepted (bootstrapping the comparison)
    // and can never itself be a discontinuity.
    SeqResult evaluate(const regmap::RequestStreamConfig& cfg, uint8_t seq) noexcept {
        if (!has_last_) {
            has_last_ = true;
            last_seq_ = seq;
            return {true, false, false};
        }

        const uint8_t fwd_distance = static_cast<uint8_t>(seq - last_seq_); // (seq - last) mod 256

        SeqResult r;
        r.accept          = !cfg.rx_enforce_seq || (fwd_distance >= 1u && fwd_distance <= 127u);
        r.discontinuity   = (fwd_distance != 1u);
        r.enter_safe_state = r.discontinuity && cfg.rx_seq_safestate_enable;

        if (r.accept) last_seq_ = seq;
        return r;
    }

    // check is evaluate()'s std::error_code-returning convenience form, for
    // a caller that only cares about the coarse accept/reject admission
    // outcome (this header's own pre-existing idiom, matching every other
    // primitive in this file) — sequence_violation iff !result.accept.
    std::error_code check(const regmap::RequestStreamConfig& cfg, uint8_t seq) noexcept {
        return evaluate(cfg, seq).accept ? std::error_code{} : make_error_code(E2eErrc::sequence_violation);
    }

    bool has_tracked_value() const noexcept { return has_last_; }
    uint8_t last_accepted_seq() const noexcept { return last_seq_; }

private:
    bool    has_last_ = false;
    uint8_t last_seq_ = 0;
};

// ── RxWatchdog — rx_wd_* / rx_ovrflw_safestate_enable (extraction §3.8) ──────
// Per-request-stream watchdog and safe-state latch. Deciding *when* to call
// kick()/overflowed() (i.e. running a timer loop) is left to the embedding
// application, same as rcp::request::select_next_due's equivalent
// "primitives, not a scheduler" split.
class RxWatchdog {
public:
    // kick resets the watchdog's last-seen-activity clock to `now_ms`.
    // Callers invoke this once per accepted request on the stream.
    void kick(uint64_t now_ms) noexcept {
        last_kick_ms_ = now_ms;
        kicked_       = true;
    }

    // overflowed reports whether more than cfg.rx_wd_timeout_interval
    // milliseconds have elapsed since the last kick(), gated on
    // cfg.rx_wd_enable — a disabled watchdog never overflows, and one that
    // has never been kicked has nothing to time out yet.
    bool overflowed(const regmap::RequestStreamConfig& cfg, uint64_t now_ms) const noexcept {
        if (!cfg.rx_wd_enable || !kicked_) return false;
        return (now_ms - last_kick_ms_) > cfg.rx_wd_timeout_interval;
    }

    // Safe-state latch. Entering safe state on watchdog/queue overflow is
    // gated by the caller (see apply_watchdog_overflow/apply_queue_overflow
    // below) on the relevant *_safestate_enable field — a register-map
    // field existing does not by itself mean the behavior it gates is on.
    void enter_safe_state() noexcept { in_safe_state_ = true; }
    void clear_safe_state() noexcept { in_safe_state_ = false; }
    bool in_safe_state() const noexcept { return in_safe_state_; }

    // should_emit_info_notification reports whether a repeating
    // "still in safe state" notification (rx_wd_info_enable) should be
    // sent right now — true exactly while latched into safe state with
    // that feature enabled. Repeat cadence/timing is left to the embedding
    // application.
    bool should_emit_info_notification(const regmap::RequestStreamConfig& cfg) const noexcept {
        return in_safe_state_ && cfg.rx_wd_info_enable;
    }

private:
    bool     kicked_        = false;
    uint64_t last_kick_ms_  = 0;
    bool     in_safe_state_ = false;
};

// apply_watchdog_overflow implements the roadmap's purge-normal/retain-
// safety rule for a watchdog-expiry trigger: when cfg.rx_wd_safestate_enable
// is set, `wd` latches into safe state and every pending/started *normal*
// (non-safety) request already queued on `ledger` for this stream is
// canceled exactly as clear-non-safestate (0x06) would, while safety-tagged
// (0x8x) records are left untouched so they can go on to drive the
// endpoint through its safe-state sequence. This reuses
// request::RequestLedger::cancel_all directly rather than reimplementing
// cancellation — see rcp/request.hpp's header comment. Returns the count
// cancel_all() itself returns (0 if the feature is disabled, since nothing
// is purged in that case).
inline size_t apply_watchdog_overflow(const regmap::RequestStreamConfig& cfg, RxWatchdog& wd,
                                       request::RequestLedger& ledger) noexcept {
    if (!cfg.rx_wd_safestate_enable) return 0;
    wd.enter_safe_state();
    return ledger.cancel_all(/*non_safestate_only=*/true);
}

// apply_queue_overflow is the analogous rule for a request-queue overrun —
// a distinct trigger from watchdog expiry (extraction §3.8) that can also
// be configured, via cfg.rx_ovrflw_safestate_enable, to drive the endpoint
// into safe state and purge normal requests the same way.
inline size_t apply_queue_overflow(const regmap::RequestStreamConfig& cfg, RxWatchdog& wd,
                                    request::RequestLedger& ledger) noexcept {
    if (!cfg.rx_ovrflw_safestate_enable) return 0;
    wd.enter_safe_state();
    return ledger.cancel_all(/*non_safestate_only=*/true);
}

// overflow_should_enter_safe_state is TC18 §12.7.7 Table 24's
// rx_ovrflw_safestate_enable, named as its own pure, directly-testable
// predicate (REQ-E2E-030; ported from c-RCP's
// rcp_e2e_overflow_should_enter_safe_state()) — the same decision
// apply_queue_overflow() above already makes inline, exposed standalone for
// a caller (e.g. StreamStatus::note_overflow()'s own doc comment above)
// that already knows overflow has occurred and wants just the verdict,
// without driving a full RxWatchdog/RequestLedger purge.
constexpr bool overflow_should_enter_safe_state(bool rx_ovrflw_safestate_enable) noexcept {
    return rx_ovrflw_safestate_enable;
}

// ── Safe-state gating for safety-tagged (0x8x) requests ──────────────────────
// The load-bearing rule the roadmap calls out as new relative to the
// pre-replacement design: a safety-tagged request only actually executes
// once the endpoint is in its configured safe state.

// endpoint_in_configured_safe_state implements the two rx_safety_measure
// strategies (extraction §3.8): RxSafetyMeasure::ForceHighImpedance's
// "safe state" is an external boolean this header does not derive on its
// own — there is no sequencer to consult, so the embedding hardware layer
// reports it directly (typically via an RxWatchdog::in_safe_state() this
// class already tracks once entered, but callers with a different safe-
// state source may pass anything here); RxSafetyMeasure::RunSafeSequencer's
// "safe state" is structural: cfg.rx_safestate_sequencer currently holding
// exactly cfg.rx_safe_sequencer_state — AND that current state is not 0
// (REQ-SEQ-012, TC18 Table 28: a manually-disabled sequencer conveys no
// application-state information at all — it is "off," not "reached state
// 0" — so it can never itself satisfy a safe-state check, even if
// rx_safe_sequencer_state also happens to be (mis)configured to 0; ported
// from c-RCP's rcp_e2e_endpoint_in_safe_state() during the Phase 2 pass —
// this file did not apply this fail-closed rule before). An unrecognized
// rx_safety_measure, or a RunSafeSequencer configuration whose
// rx_safestate_sequencer index isn't valid in the table supplied, both fail
// *closed*: this function reports false, so a safety-tagged request stays
// blocked rather than executing against an unverifiable safe-state claim.
inline bool endpoint_in_configured_safe_state(const regmap::RequestStreamConfig& cfg,
                                               const request::SequencerTable& sequencers,
                                               bool force_high_impedance_asserted) noexcept {
    if (cfg.rx_safety_measure == regmap::RxSafetyMeasure::ForceHighImpedance)
        return force_high_impedance_asserted;
    regmap::SequencerState cur = 0;
    auto ec = sequencers.state_of(cfg.rx_safestate_sequencer, cur);
    if (ec) return false;
    if (cur == 0) return false; // REQ-SEQ-012: a disabled sequencer is never "safe"
    return cur == cfg.rx_safe_sequencer_state;
}

// may_execute_now gates dispatch of one decoded request record: a
// non-safety record is always eligible as far as this rule is concerned
// (subject to whatever other checks the caller already applies elsewhere);
// a safety-tagged (0x8x, request::RequestRecord::is_safety) record is
// only eligible once `endpoint_in_safe_state` is true.
inline bool may_execute_now(const request::RequestRecord& rec, bool endpoint_in_safe_state) noexcept {
    return !rec.is_safety || endpoint_in_safe_state;
}

} // namespace e2e
} // namespace rcp

// Enable std::error_code construction from rcp::e2e::E2eErrc.
namespace std {
template <>
struct is_error_code_enum<rcp::e2e::E2eErrc> : true_type {};
} // namespace std
