// fusa:req REQ-WIRE-001
// fusa:req REQ-WIRE-002
// fusa:req REQ-WIRE-003
// fusa:req REQ-WIRE-007
// fusa:req REQ-WIRE-011
// fusa:req REQ-WIRE-013

// TC18 wire codec, framing half — IEEE 1722 AVTPDU framing (NTSCF/TSCF) that
// the OPEN Alliance TC18 Remote Control Protocol Specification v0.5.1_RC
// layers its ACF messages (rcp/acf.hpp) on top of.
//
// ROADMAP.md milestone 44, "Wire Format Core (v2.0.0)": this header (plus
// rcp/acf.hpp) replaces the old bespoke 16-byte frame this file used to
// define (preserved as-is, unrenamed in behavior, under rcp/legacy_wire.hpp
// for rcp/udp.hpp's benefit from v2.0.0 through v2.12.0, until rcp/udp.hpp
// was itself rebuilt directly on this header and rcp/acf.hpp — and
// rcp/legacy_wire.hpp deleted outright — at milestone 57, v2.13.0).
// Originally landed as a single rcp/wire.hpp; split into rcp/avtp.hpp (this
// file, AVTPDU header framing) and rcp/acf.hpp (ACF_ABB/ACF_GBB message
// format) per RELAY spec §13.7.2's standard module-name registry, which
// names these two concerns separately.
//
// Scope note: this module is a pure wire codec. It has no dependency on
// rcp.hpp's Zone/Command/Controller/Registry model, and no dependency on any
// RC Server lifecycle state, register-map, discovery, or endpoint-specific
// behavior — those are later Phase 13 milestones (v2.1.0 onward), sequenced
// on top of this header, not into it. Conditional request kinds beyond the
// mandatory "standard" kind (v2.5.0), the E2E CRC safe-point mechanism
// (v2.6.0), and fragmentation (deferred indefinitely, v2.8.0) are likewise
// out of scope here; the fields they need already exist in rcp/acf.hpp so
// later milestones can be layered on without reshaping either header.
//
// Transport-agnostic by design: nothing in this module assumes the AVTPDU
// travels over raw Ethernet. The same framing is meant to sit under a native
// IEEE 1722 Ethernet transport, IEEE 1722-over-UDP/IP (v2.13.0), or a
// CAN(FD/XL) transport — no socket, header, or byte-order assumption here is
// specific to any one of those.
//
// Field names and behavior below implement TC18's *behavior* as described in
// an internal structured extraction of the specification named above; no
// text from that document is reproduced here. Wire conformance note (v2.19.0,
// issue cpp-RCP-04): the NTSCF/TSCF bit-packing below was re-derived field by
// field from the specification's own header diagrams (bit-position figures,
// not prose) and cross-checked against a second, independent worked example
// elsewhere in the specification that plugs concrete field values into the
// same header shape. Both derivations agree, which is the strongest
// consistency check available without a second, independent TC18
// implementation to interoperate against directly — see this repository's
// pull request description for the full derivation and its honestly-stated
// confidence level. The one part of this file *not* re-derived from a figure
// is byte order/endianness of multi-byte integer fields (big-endian
// throughout), which was already big-endian before this pass and is kept
// unchanged.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <system_error>
#include <vector>

namespace rcp {
namespace avtp {

// ── AVTPDU subtype identifiers ────────────────────────────────────────────────
// From the IEEE 1722 AVTP subtype space — a separate, publicly published
// standard from the confidential TC18 document cited above. NTSCF and TSCF
// are the two control formats TC18 builds its messaging on (extraction §2.2).

constexpr uint8_t kSubtypeTscf  = 0x05; // Time-Synchronous Control Format
constexpr uint8_t kSubtypeNtscf = 0x82; // Non-Time-Synchronous Control Format

// ── Errors ────────────────────────────────────────────────────────────────────
// short_buffer is reused by rcp/acf.hpp for its own decode functions (which
// include this header) rather than duplicated there — both modules report
// the same "fewer bytes available than required" condition.

enum class AvtpErrc : int {
    short_buffer    = 1, // fewer bytes available than the header/field requires
    bad_subtype     = 2, // AVTPDU subtype byte is neither NTSCF nor TSCF
    length_mismatch = 3, // control_data_length disagrees with buffer size
};

inline const std::error_category& avtp_category() noexcept {
    struct Cat : std::error_category {
        const char* name() const noexcept override { return "rcp.avtp"; }
        std::string message(int ev) const override {
            switch (static_cast<AvtpErrc>(ev)) {
            case AvtpErrc::short_buffer:    return "rcp/avtp: buffer too short";
            case AvtpErrc::bad_subtype:     return "rcp/avtp: unrecognized AVTPDU subtype";
            case AvtpErrc::length_mismatch: return "rcp/avtp: length field does not match buffer size";
            default:                        return "rcp/avtp: unknown error";
            }
        }
    };
    static Cat instance;
    return instance;
}

inline std::error_code make_error_code(AvtpErrc e) noexcept {
    return {static_cast<int>(e), avtp_category()};
}

// ── Internal helpers ──────────────────────────────────────────────────────────
// Shared big-endian byte-packing helpers. rcp/acf.hpp and other headers
// (e.g. rcp/gpio.hpp) reuse these rather than re-deriving byte order.

namespace detail {

inline void put_u16(uint8_t* p, uint16_t v) noexcept {
    p[0] = static_cast<uint8_t>(v >> 8);
    p[1] = static_cast<uint8_t>(v & 0xFF);
}
inline void put_u32(uint8_t* p, uint32_t v) noexcept {
    p[0] = static_cast<uint8_t>(v >> 24);
    p[1] = static_cast<uint8_t>((v >> 16) & 0xFF);
    p[2] = static_cast<uint8_t>((v >>  8) & 0xFF);
    p[3] = static_cast<uint8_t>(v & 0xFF);
}
inline void put_u64(uint8_t* p, uint64_t v) noexcept {
    for (int i = 0; i < 8; ++i)
        p[i] = static_cast<uint8_t>((v >> (56 - 8 * i)) & 0xFF);
}
inline uint16_t get_u16(const uint8_t* p) noexcept {
    return static_cast<uint16_t>((uint16_t(p[0]) << 8) | p[1]);
}
inline uint32_t get_u32(const uint8_t* p) noexcept {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
           (uint32_t(p[2]) <<  8) |  uint32_t(p[3]);
}
inline uint64_t get_u64(const uint8_t* p) noexcept {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i)
        v = (v << 8) | uint64_t(p[i]);
    return v;
}

} // namespace detail

// ── StreamId ──────────────────────────────────────────────────────────────────
// stream_id addresses the sender of an AVTPDU: the sender's 48-bit MAC
// address plus a 16-bit suffix the sender assigns locally to distinguish its
// own concurrent streams (extraction §2.1). Two different senders' stream_id
// values are only guaranteed distinct because their MAC halves differ; the
// suffix alone is not a global identifier.

struct StreamId {
    std::array<uint8_t, 6> mac{};
    uint16_t               suffix = 0;

    uint64_t to_u64() const noexcept {
        uint64_t v = 0;
        for (uint8_t b : mac) v = (v << 8) | uint64_t(b);
        return (v << 16) | uint64_t(suffix);
    }

    static StreamId from_u64(uint64_t v) noexcept {
        StreamId id;
        id.suffix = static_cast<uint16_t>(v & 0xFFFF);
        uint64_t mac_bits = v >> 16;
        for (int i = 5; i >= 0; --i) {
            id.mac[static_cast<size_t>(i)] = static_cast<uint8_t>(mac_bits & 0xFF);
            mac_bits >>= 8;
        }
        return id;
    }
};

inline bool operator==(const StreamId& a, const StreamId& b) noexcept {
    return a.mac == b.mac && a.suffix == b.suffix;
}
inline bool operator!=(const StreamId& a, const StreamId& b) noexcept { return !(a == b); }

// ── ByteBusId ─────────────────────────────────────────────────────────────────
// byte_bus_id addresses a target endpoint's request path *within* one
// stream_id. Rule (extraction §2.1): byte_bus_id is unique only within the
// owning stream, never globally — two different clients' streams may reuse
// the same byte_bus_id for unrelated endpoints — and a response or ack MUST
// echo the byte_bus_id of the request it answers, unchanged. rcp/acf.hpp
// enforces the echo half of that rule structurally: see its make_response(),
// which always copies byte_bus_id (and transaction_num) from the request it
// is built from rather than accepting them as free parameters.
//
// Width: on the wire (rcp/acf.hpp's ACF Message Info header) byte_bus_id is
// an 11-bit field (0–2047), not a full octet — a detail only visible in the
// specification's bit-position diagram for that header, not in prose. This
// alias is uint16_t, wide enough for that full range; rcp/acf.hpp's encoder
// masks to 11 bits, matching every other sub-octet field in that header.

using ByteBusId = uint16_t;

// ── NTSCF / TSCF AVTPDU headers ───────────────────────────────────────────────
//
// NTSCF is non-time-synchronous and, per the target protocol, server-only
// outbound; TSCF is time-synchronous and client-only (extraction §2.2). This
// codec does not know which local role is encoding or decoding a given
// frame, so it cannot itself refuse a TSCF frame built by "the server" —
// enforcing that direction rule is the transport/application layer's job
// once it knows its own role; this module only documents it here and
// exposes the two header shapes distinctly enough that misusing one for the
// other's direction is a visible, deliberate choice rather than an accident.

// Bit layouts below (extraction §2.2's figures — the specification's actual
// header diagrams, each with a per-bit ruler, not the prose) diverge from
// generic IEEE 1722 subtype-header assumptions in field widths, so every
// width here was counted from those diagrams rather than assumed:
//
// NTSCF, 12 bytes total:
//   byte0            subtype (0x82)
//   byte1 bit7       sv (stream_valid — always written 1 here)
//   byte1 bits6:4    version (always 0)
//   byte1 bit3       r (reserved, always 0; not surfaced as a field)
//   byte1 bits2:0 +
//   byte2            ntscf_data_length — 11 bits total (byte1's low 3 bits
//                    are its MSBs, byte2 is its low 8 bits): byte length of
//                    the ACF message that follows, same quantity
//                    NtscfHeader::control_data_length already held, just
//                    narrower than a full octet pair on the wire
//   byte3            sequence_num — 8 bits (not 16 — a per-stream_id
//                    rolling counter that wraps at 256)
//   bytes4-11        stream_id, 64 bits
//
// TSCF, 24 bytes total — noticeably longer than the 18 bytes this codec
// used before this pass, because the real header carries a 32-bit reserved
// word (bytes16-19) and a 16-bit reserved half (bytes22-23 — the other half
// of the 32-bit word carrying stream_data_length) that this codec
// previously omitted entirely:
//   byte0            subtype (0x05)
//   byte1 bit7       sv (always 1)
//   byte1 bits6:4    version (always 0)
//   byte1 bit3       mr (reserved here, always 0; not surfaced)
//   byte1 bits2:1    rsv (always 0)
//   byte1 bit0       tv (timestamp_valid)
//   byte2            sequence_num — 8 bits, same rolling-counter field as
//                    NTSCF's
//   byte3            reserved (7 bits) + tu (1 bit) — always 0; not
//                    surfaced (no RCP-specific behavior found for either)
//   bytes4-11        stream_id, 64 bits
//   bytes12-15       avtp_timestamp, 32 bits
//   bytes16-19       reserved, always 0
//   bytes20-21       stream_data_length(octets) — 16 bits; this is
//                    TscfHeader::control_data_length's wire position
//   bytes22-23       reserved, always 0
// sv/version below are additive fields (Phase 17, ported from c-RCP's
// rcp_avtp_ntscf_header_t/rcp_avtp_tscf_header_t, which round-trip both
// through encode/decode rather than hardcoding them). Their defaults (true
// / 0) reproduce this codec's own pre-existing hardcoded encode behavior
// exactly, so no existing caller that leaves them at their defaults sees
// any wire change.
struct NtscfHeader {
    bool     sv                  = true;  // stream_id valid; TC18 always sets this for NTSCF
    uint8_t  version              = 0;     // AVTP version; always 0 in this spec revision
    StreamId stream_id{};
    uint8_t  sequence_num        = 0; // per-stream_id AVTPDU counter, 8-bit rolling
    uint16_t control_data_length = 0; // byte length of the ACF message that follows; wire field is 11 bits (0–2047)
};

// mr/tu/reserved0/reserved1 below are additive fields (Phase 17, ported
// from c-RCP's rcp_avtp_tscf_header_t). mr and tv/tu are round-tripped
// through encode/decode; reserved0/reserved1 are decode-only (encode always
// zero-fills bytes 16-19/22-23 on the wire regardless of what a caller sets
// here, matching c-RCP's rcp_avtp_encode_tscf() and this struct's own
// pre-existing control_data_length/ntscf_data_length "derived, never
// trusted, on encode" convention) — a genuinely nonzero reserved0/reserved1
// after decode can only come from a real (possibly non-conformant, or
// future-revision) wire frame; see tscf_reserved_all_zero() below for TC18
// §13.3's own rule built on top of them.
struct TscfHeader {
    bool     sv                  = true; // stream_id valid; always 1 for TC18 use
    uint8_t  version              = 0;    // AVTP version; always 0 in this spec revision
    bool     mr                   = false; // media clock restart; round-tripped, no RCP-specific behavior
    StreamId stream_id{};
    uint8_t  sequence_num        = 0; // per-stream_id AVTPDU counter, 8-bit rolling
    uint16_t control_data_length = 0; // byte length of the ACF message that follows; wire field is a full 16 bits
    bool     timestamp_valid     = false; // "tv" — whether avtp_timestamp below is meaningful
    uint32_t avtp_timestamp      = 0;     // 32-bit; TSCF-only (extraction §2.6)
    // "tu" — avtp_timestamp uncertain. TC18 §13.3's third rule: "In case
    // the time stamp is uncertain (i.e. tu = 1), then this shall be
    // executed as if tu = 0" — this codec does not itself branch on tu
    // anywhere; it is decoded purely so a caller can still inspect the
    // wire value (diagnostics, a future revision that does need it).
    bool     tu                   = false;
    uint32_t reserved0            = 0; // bytes 16-19; decode-only, see struct comment above
    uint16_t reserved1            = 0; // bytes 22-23; decode-only, see struct comment above
};

constexpr size_t kNtscfHeaderLen = 12; // 1 subtype + 1 flags/length-hi + 1 length-lo + 1 seq + 8 stream_id
constexpr size_t kTscfHeaderLen  = 24; // kNtscfHeaderLen + 4 avtp_timestamp + 4 reserved + 2 stream_data_length + 2 reserved

namespace detail {
constexpr uint8_t  kFlagStreamValid      = 0x80; // "sv" — always set for these control formats, byte1 bit7 in both headers
constexpr uint8_t  kFlagTimestampValid   = 0x01; // "tv" — TSCF only, byte1 bit0 (NOT bit6 — see header comment above)
constexpr uint8_t  kFlagMediaRestart     = 0x08; // "mr" — TSCF only, byte1 bit3
constexpr uint8_t  kFlagTimestampUncertain = 0x01; // "tu" — TSCF only, byte3 bit0
constexpr uint16_t kNtscfDataLengthMask  = 0x07FF; // ntscf_data_length is 11 bits wide
} // namespace detail

inline std::vector<uint8_t> encode_ntscf_header(const NtscfHeader& h) {
    std::vector<uint8_t> buf(kNtscfHeaderLen, 0);
    const uint16_t data_len = static_cast<uint16_t>(h.control_data_length & detail::kNtscfDataLengthMask);
    buf[0] = kSubtypeNtscf;
    buf[1] = static_cast<uint8_t>((h.sv ? detail::kFlagStreamValid : 0) |
                                  ((h.version & 0x07) << 4) |
                                  ((data_len >> 8) & 0x07));
    buf[2] = static_cast<uint8_t>(data_len & 0xFF);
    buf[3] = h.sequence_num;
    detail::put_u64(&buf[4], h.stream_id.to_u64());
    return buf;
}

inline std::error_code decode_ntscf_header(const uint8_t* b, size_t len, NtscfHeader& out) {
    if (len < 1) return make_error_code(AvtpErrc::short_buffer);
    if (b[0] != kSubtypeNtscf) return make_error_code(AvtpErrc::bad_subtype);
    if (len < kNtscfHeaderLen) return make_error_code(AvtpErrc::short_buffer);
    out.sv                  = (b[1] & detail::kFlagStreamValid) != 0;
    out.version              = static_cast<uint8_t>((b[1] >> 4) & 0x07);
    out.control_data_length = static_cast<uint16_t>(((b[1] & 0x07) << 8) | b[2]);
    out.sequence_num        = b[3];
    out.stream_id           = StreamId::from_u64(detail::get_u64(&b[4]));
    return {};
}

inline std::vector<uint8_t> encode_tscf_header(const TscfHeader& h) {
    std::vector<uint8_t> buf(kTscfHeaderLen, 0);
    buf[0] = kSubtypeTscf;
    buf[1] = static_cast<uint8_t>((h.sv ? detail::kFlagStreamValid : 0) |
                                  ((h.version & 0x07) << 4) |
                                  (h.mr ? detail::kFlagMediaRestart : 0) |
                                  (h.timestamp_valid ? detail::kFlagTimestampValid : 0));
    buf[2] = h.sequence_num;
    buf[3] = static_cast<uint8_t>(h.tu ? detail::kFlagTimestampUncertain : 0);
    detail::put_u64(&buf[4], h.stream_id.to_u64());
    detail::put_u32(&buf[12], h.avtp_timestamp);
    // buf[16..19] (reserved0) stays 0 — always zero-filled on encode,
    // regardless of h.reserved0 (decode-only field, see struct comment).
    detail::put_u16(&buf[20], h.control_data_length);
    // buf[22..23] (reserved1) stays 0 — same rule as reserved0 above.
    return buf;
}

inline std::error_code decode_tscf_header(const uint8_t* b, size_t len, TscfHeader& out) {
    if (len < 1) return make_error_code(AvtpErrc::short_buffer);
    if (b[0] != kSubtypeTscf) return make_error_code(AvtpErrc::bad_subtype);
    if (len < kTscfHeaderLen) return make_error_code(AvtpErrc::short_buffer);
    out.sv                  = (b[1] & detail::kFlagStreamValid) != 0;
    out.version              = static_cast<uint8_t>((b[1] >> 4) & 0x07);
    out.mr                   = (b[1] & detail::kFlagMediaRestart) != 0;
    out.timestamp_valid     = (b[1] & detail::kFlagTimestampValid) != 0;
    out.sequence_num        = b[2];
    out.tu                   = (b[3] & detail::kFlagTimestampUncertain) != 0;
    out.stream_id           = StreamId::from_u64(detail::get_u64(&b[4]));
    out.avtp_timestamp      = detail::get_u32(&b[12]);
    out.reserved0            = detail::get_u32(&b[16]);
    out.control_data_length = detail::get_u16(&b[20]);
    out.reserved1            = detail::get_u16(&b[22]);
    return {};
}

// ── REQ-TIMED-012, TC18 §11.2/§11.2.1: 48-bit gPTP-domain reconstruction of
// a TSCF avtp_timestamp ────────────────────────────────────────────────────
// "If received under TSCF header, [a request's] execution is postponed
// until the presentation time has occurred" — a rule that applies to every
// request kind, not just a timed-request-specific one. Evaluating it means
// comparing avtp_timestamp (this header's own 32-bit, nanoseconds-modulo-
// 2^32 IEEE 1722 field) against a 48-bit gPTP-domain clock — but a 32-bit
// field cannot itself carry which of the (2^48 / 2^32) possible 48-bit
// instants congruent to it mod 2^32 was actually intended, and IEEE 1722
// leaves that reconstruction to the receiver.
//
// extend_timestamp resolves that ambiguity the same way every real
// AVTP/gPTP receiver does (standard IEEE 1722 presentation-time
// reconstruction, ported from c-RCP's rcp_avtp_extend_timestamp()): of the
// several 48-bit instants congruent to wire_ts modulo 2^32, it returns
// whichever is CLOSEST to reference_now. Naively zero-extending wire_ts (OR
// -ing it onto reference_now's own high bits, unadjusted) is wrong whenever
// wire_ts's low bits happen to be numerically smaller than reference_now's
// — that reads a request meant for ~100ms in the future as ~4.29 seconds
// (2^32 ns) in the past instead.
//
// The result is intended to be computed ONCE, at admission time
// (reference_now = the current gPTP-domain clock at that moment), then
// compared on every later tick against that same fixed result —
// reference_now is a resolution anchor, not something the caller
// re-supplies per tick.
inline uint64_t extend_timestamp(uint32_t wire_ts, uint64_t reference_now) noexcept {
    constexpr uint64_t period = uint64_t{1} << 32;
    constexpr uint64_t half   = period / 2;
    const uint64_t base       = reference_now & ~(period - 1);
    uint64_t       candidate  = base | uint64_t{wire_ts};

    if (candidate > reference_now && (candidate - reference_now) > half) {
        // candidate is more than half a period ahead of reference_now —
        // the instant one period earlier is the closer match.
        candidate -= period;
    } else if (candidate < reference_now && (reference_now - candidate) > half) {
        // Symmetric case: one period later is closer.
        candidate += period;
    }
    return candidate;
}

// ── Subtype dispatch & the TSCF-without-time-sync drop rule ────────────────
// Ported from c-RCP's rcp_avtp_should_drop_tscf()/_tscf_reserved_all_zero(),
// both governed by TC18 §13.3's own configurable disposition for a
// TSCF-headed AVTPDU an RC Server cannot (or chooses not to) honor the
// ordinary way.

// peek_subtype reads just the subtype byte (offset 0) from a received
// AVTPDU, so a caller can decide which of decode_ntscf_header()/
// decode_tscf_header() to invoke without a full decode attempt first.
inline std::error_code peek_subtype(const uint8_t* b, size_t len, uint8_t& out_subtype) noexcept {
    if (len < 1) return make_error_code(AvtpErrc::short_buffer);
    out_subtype = b[0];
    return {};
}

// TscfFallback selects between TC18 §11.1's unconditional wording ("AVTPDUs
// having a TSCF header are dropped, and no response send") and §13.3's own
// alternative, more specific, explicitly configurable rule ("...or dropped,
// depending on the configuration of the RC Server") for a TSCF-headed
// AVTPDU an RC Server cannot honor the ordinary way — shared by
// should_drop_tscf()'s unsupported-time-sync rule and
// tscf_reserved_all_zero()'s reserved-bytes rule below, since both of
// §13.3's own sentences describing them have the identical "...or dropped,
// depending on the configuration..." shape. Drop (0) reproduces this
// codec's own original, unconditional-drop disposition — a caller that
// does not opt in sees no behavior change.
enum class TscfFallback : uint8_t {
    Drop   = 0, // drop the frame outright (this codec's default disposition)
    Ignore = 1, // ignore the TSCF-specific semantics that could not be
                // honored and process the request as if no presentation
                // time were included
};

// REQ-AVTP-014/021: true iff `subtype` is kSubtypeTscf, time sync is not
// supported, and `unsupported_time_sync_policy` is TscfFallback::Drop.
// Returns false for every subtype other than kSubtypeTscf, regardless of
// server_time_sync_supported or unsupported_time_sync_policy — this rule is
// TSCF-only.
inline bool should_drop_tscf(bool server_time_sync_supported, uint8_t subtype,
                              TscfFallback unsupported_time_sync_policy) noexcept {
    if (subtype != kSubtypeTscf) return false;
    if (!server_time_sync_supported) return unsupported_time_sync_policy == TscfFallback::Drop;
    return false;
}

// REQ-AVTP-022, TC18 §13.3's second configurable rule: "If the reserved
// bytes in the header are all zero, then the request shall be queued as if
// the header was in NTSCF format or dropped, depending on configuration."
// Returns true iff hdr's own reserved0/reserved1 are both zero — callers
// combine this with a caller-owned TscfFallback exactly the way
// should_drop_tscf()'s own unsupported_time_sync_policy parameter is used.
inline bool tscf_reserved_all_zero(const TscfHeader& hdr) noexcept {
    return hdr.reserved0 == 0 && hdr.reserved1 == 0;
}

} // namespace avtp
} // namespace rcp
