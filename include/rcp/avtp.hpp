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
// define (now preserved as-is, unrenamed in behavior, under
// rcp/legacy_wire.hpp for rcp/udp.hpp's benefit until the transport itself
// is rebuilt at v2.13.0). Originally landed as a single rcp/wire.hpp; split
// into rcp/avtp.hpp (this file, AVTPDU header framing) and rcp/acf.hpp
// (ACF_ABB/ACF_GBB message format) per RELAY spec §13.7.2's standard
// module-name registry, which names these two concerns separately.
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
// text from that document is reproduced here. The concrete bit-packing
// chosen in this file is this implementation's own encoding of that
// behavior for milestone 44; full bit-for-bit wire conformance against
// other TC18 implementations is not claimed until v2.6.0 lands, per the
// Phase 13 introduction in ROADMAP.md.
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

using ByteBusId = uint8_t;

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

struct NtscfHeader {
    StreamId stream_id{};
    uint16_t sequence_num        = 0; // per-stream_id AVTPDU counter
    uint16_t control_data_length = 0; // byte length of the ACF message that follows
};

struct TscfHeader {
    StreamId stream_id{};
    uint16_t sequence_num        = 0;
    uint16_t control_data_length = 0;
    bool     timestamp_valid     = false; // "tv" — whether avtp_timestamp below is meaningful
    uint32_t avtp_timestamp      = 0;     // 32-bit; TSCF-only (extraction §2.6)
};

constexpr size_t kNtscfHeaderLen = 14; // 1 subtype + 1 flags + 2 seq + 8 stream_id + 2 cdl
constexpr size_t kTscfHeaderLen  = 18; // kNtscfHeaderLen + 4 avtp_timestamp

namespace detail {
constexpr uint8_t kFlagStreamValid = 0x80; // "sv" — always set for these control formats
constexpr uint8_t kFlagTimestampValid = 0x40; // "tv" — TSCF only
} // namespace detail

inline std::vector<uint8_t> encode_ntscf_header(const NtscfHeader& h) {
    std::vector<uint8_t> buf(kNtscfHeaderLen, 0);
    buf[0] = kSubtypeNtscf;
    buf[1] = detail::kFlagStreamValid;
    detail::put_u16(&buf[2], h.sequence_num);
    detail::put_u64(&buf[4], h.stream_id.to_u64());
    detail::put_u16(&buf[12], h.control_data_length);
    return buf;
}

inline std::error_code decode_ntscf_header(const uint8_t* b, size_t len, NtscfHeader& out) {
    if (len < 1) return make_error_code(AvtpErrc::short_buffer);
    if (b[0] != kSubtypeNtscf) return make_error_code(AvtpErrc::bad_subtype);
    if (len < kNtscfHeaderLen) return make_error_code(AvtpErrc::short_buffer);
    out.sequence_num        = detail::get_u16(&b[2]);
    out.stream_id           = StreamId::from_u64(detail::get_u64(&b[4]));
    out.control_data_length = detail::get_u16(&b[12]);
    return {};
}

inline std::vector<uint8_t> encode_tscf_header(const TscfHeader& h) {
    std::vector<uint8_t> buf(kTscfHeaderLen, 0);
    buf[0] = kSubtypeTscf;
    buf[1] = static_cast<uint8_t>(detail::kFlagStreamValid |
                                  (h.timestamp_valid ? detail::kFlagTimestampValid : 0));
    detail::put_u16(&buf[2], h.sequence_num);
    detail::put_u64(&buf[4], h.stream_id.to_u64());
    detail::put_u16(&buf[12], h.control_data_length);
    detail::put_u32(&buf[14], h.avtp_timestamp);
    return buf;
}

inline std::error_code decode_tscf_header(const uint8_t* b, size_t len, TscfHeader& out) {
    if (len < 1) return make_error_code(AvtpErrc::short_buffer);
    if (b[0] != kSubtypeTscf) return make_error_code(AvtpErrc::bad_subtype);
    if (len < kTscfHeaderLen) return make_error_code(AvtpErrc::short_buffer);
    out.timestamp_valid     = (b[1] & detail::kFlagTimestampValid) != 0;
    out.sequence_num        = detail::get_u16(&b[2]);
    out.stream_id           = StreamId::from_u64(detail::get_u64(&b[4]));
    out.control_data_length = detail::get_u16(&b[12]);
    out.avtp_timestamp      = detail::get_u32(&b[14]);
    return {};
}

} // namespace avtp
} // namespace rcp
