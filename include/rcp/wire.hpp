// fusa:req REQ-WIRE-001
// fusa:req REQ-WIRE-002
// fusa:req REQ-WIRE-003
// fusa:req REQ-WIRE-004
// fusa:req REQ-WIRE-005
// fusa:req REQ-WIRE-006
// fusa:req REQ-WIRE-007
// fusa:req REQ-WIRE-008
// fusa:req REQ-WIRE-009
// fusa:req REQ-WIRE-010
// fusa:req REQ-WIRE-011
// fusa:req REQ-WIRE-012
// fusa:req REQ-WIRE-013
// fusa:req REQ-WIRE-014

// TC18 wire codec — IEEE 1722 AVTPDU framing (NTSCF/TSCF) plus the ACF_ABB /
// ACF_GBB message types the OPEN Alliance TC18 Remote Control Protocol
// Specification v0.5.1_RC layers on top of them.
//
// ROADMAP.md milestone 44, "Wire Format Core (v2.0.0)": this header replaces
// the old bespoke 16-byte frame this file used to define (now preserved
// as-is, unrenamed in behavior, under rcp/legacy_wire.hpp for rcp/udp.hpp's
// benefit until the transport itself is rebuilt at v2.13.0).
//
// Scope note: this module is a pure wire codec. It has no dependency on
// rcp.hpp's Zone/Command/Controller/Registry model, and no dependency on any
// RC Server lifecycle state, register-map, discovery, or endpoint-specific
// behavior — those are later Phase 13 milestones (v2.1.0 onward), sequenced
// on top of this header, not into it. Conditional request kinds beyond the
// mandatory "standard" kind (v2.5.0), the E2E CRC safe-point mechanism
// (v2.6.0), and fragmentation (deferred indefinitely, v2.8.0) are likewise
// out of scope here; the fields they need already exist below so later
// milestones can be layered on without reshaping this header.
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
// behavior for milestone 44 — full bit-for-bit wire conformance against
// other TC18 implementations is not claimed until v2.6.0 lands, per the
// Phase 13 introduction in ROADMAP.md.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace rcp {
namespace wire {

// ── AVTPDU subtype identifiers ────────────────────────────────────────────────
// From the IEEE 1722 AVTP subtype space — a separate, publicly published
// standard from the confidential TC18 document cited above. NTSCF and TSCF
// are the two control formats TC18 builds its messaging on (extraction §2.2).

constexpr uint8_t kSubtypeTscf  = 0x05; // Time-Synchronous Control Format
constexpr uint8_t kSubtypeNtscf = 0x82; // Non-Time-Synchronous Control Format

// ── ACF message types ─────────────────────────────────────────────────────────
// The two ACF (AVTP Control Format) message types RCP defines on top of the
// AVTPDU framing above (extraction §2.3).

constexpr uint8_t kAcfMsgTypeGbb = 0x0D; // ACF_GBB — carries a 64-bit message_timestamp slot
constexpr uint8_t kAcfMsgTypeAbb = 0x0E; // ACF_ABB — no timestamp field at all

// ── Errors ────────────────────────────────────────────────────────────────────

enum class WireErrc : int {
    short_buffer     = 1, // fewer bytes available than the header/field requires
    bad_subtype      = 2, // AVTPDU subtype byte is neither NTSCF nor TSCF
    bad_acf_msg_type = 3, // acf_msg_type is neither ACF_ABB nor ACF_GBB
    length_mismatch  = 4, // control_data_length / acf_msg_length disagrees with buffer size
};

inline const std::error_category& wire_category() noexcept {
    struct Cat : std::error_category {
        const char* name() const noexcept override { return "rcp.wire"; }
        std::string message(int ev) const override {
            switch (static_cast<WireErrc>(ev)) {
            case WireErrc::short_buffer:     return "rcp/wire: buffer too short";
            case WireErrc::bad_subtype:      return "rcp/wire: unrecognized AVTPDU subtype";
            case WireErrc::bad_acf_msg_type: return "rcp/wire: unrecognized ACF message type";
            case WireErrc::length_mismatch:  return "rcp/wire: length field does not match buffer size";
            default:                         return "rcp/wire: unknown error";
            }
        }
    };
    static Cat instance;
    return instance;
}

inline std::error_code make_error_code(WireErrc e) noexcept {
    return {static_cast<int>(e), wire_category()};
}

// ── Internal helpers ──────────────────────────────────────────────────────────

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
// echo the byte_bus_id of the request it answers, unchanged. This module
// enforces the echo half of that rule structurally: see make_response()
// below, which always copies byte_bus_id (and transaction_num) from the
// request it is built from rather than accepting them as free parameters.

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
    if (len < 1) return make_error_code(WireErrc::short_buffer);
    if (b[0] != kSubtypeNtscf) return make_error_code(WireErrc::bad_subtype);
    if (len < kNtscfHeaderLen) return make_error_code(WireErrc::short_buffer);
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
    if (len < 1) return make_error_code(WireErrc::short_buffer);
    if (b[0] != kSubtypeTscf) return make_error_code(WireErrc::bad_subtype);
    if (len < kTscfHeaderLen) return make_error_code(WireErrc::short_buffer);
    out.timestamp_valid     = (b[1] & detail::kFlagTimestampValid) != 0;
    out.sequence_num        = detail::get_u16(&b[2]);
    out.stream_id           = StreamId::from_u64(detail::get_u64(&b[4]));
    out.control_data_length = detail::get_u16(&b[12]);
    out.avtp_timestamp      = detail::get_u32(&b[14]);
    return {};
}

// ── ACF shared header ("byte_message_info") ───────────────────────────────────
// Fields common to both ACF_ABB and ACF_GBB messages (extraction §2.4). Every
// field the roadmap calls out is represented individually below rather than
// left packed in an opaque byte, so later milestones (compound requests,
// safety variants, …) can extend behavior around a given field without
// having to re-derive its bit position from scratch.

struct AcfMessageInfo {
    uint8_t   acf_msg_type   = kAcfMsgTypeAbb; // ACF_ABB or ACF_GBB
    uint16_t  acf_msg_length = 0;              // quadlets, including this shared header
    uint8_t   pad            = 0;              // 0–3 trailing pad bytes added for quadlet alignment
    bool      mtv            = false;          // message_timestamp valid (ACF_GBB only)
    ByteBusId byte_bus_id    = 0;               // target endpoint, unique within stream_id only
    bool      evt_ack        = false;          // evt[3] — acknowledge flag
    uint8_t   evt_op         = 0;               // evt[2:0] — endpoint-defined sub-opcode
    bool      hs             = false;          // reserved for endpoint-specific use (e.g. I2C high-speed)
    bool      cs             = false;          // conditional-start; meaning depends on request kind (v2.5.0)
    uint8_t   transaction_num = 0;              // correlates a request with its response/ack
    bool      op             = false;          // false = read, true = write (this codec's convention)
    bool      rsp            = false;          // set on every response/ack, clear on requests
    bool      err            = false;          // set alongside rsp for an error response
    bool      ms             = false;          // "more segments" — fragmentation, deferred to v2.8.0
    uint16_t  read_size_or_segment_num = 0;    // read_size when !ms, segment_num when ms
};

constexpr size_t kAcfCommonHeaderLen = 10;
constexpr size_t kAcfGbbTimestampLen = 8;

namespace detail {
constexpr uint8_t kByte3MtvBit   = 0x80;
constexpr uint8_t kByte3PadShift = 5;
constexpr uint8_t kByte3PadMask  = 0x03;
constexpr uint8_t kByte5AckBit   = 0x80;
constexpr uint8_t kByte5OpShift  = 4;
constexpr uint8_t kByte5OpMask   = 0x07;
constexpr uint8_t kByte5HsBit    = 0x08;
constexpr uint8_t kByte5CsBit    = 0x04;
constexpr uint8_t kByte7OpBit    = 0x80;
constexpr uint8_t kByte7RspBit   = 0x40;
constexpr uint8_t kByte7ErrBit   = 0x20;
constexpr uint8_t kByte7MsBit    = 0x10;
} // namespace detail

inline void encode_acf_message_info(const AcfMessageInfo& info, uint8_t* out10) noexcept {
    out10[0] = info.acf_msg_type;
    detail::put_u16(&out10[1], info.acf_msg_length);
    out10[3] = static_cast<uint8_t>(
        (info.mtv ? detail::kByte3MtvBit : 0) |
        (static_cast<uint8_t>(info.pad & detail::kByte3PadMask) << detail::kByte3PadShift));
    out10[4] = info.byte_bus_id;
    out10[5] = static_cast<uint8_t>(
        (info.evt_ack ? detail::kByte5AckBit : 0) |
        (static_cast<uint8_t>(info.evt_op & detail::kByte5OpMask) << detail::kByte5OpShift) |
        (info.hs ? detail::kByte5HsBit : 0) |
        (info.cs ? detail::kByte5CsBit : 0));
    out10[6] = info.transaction_num;
    out10[7] = static_cast<uint8_t>(
        (info.op  ? detail::kByte7OpBit  : 0) |
        (info.rsp ? detail::kByte7RspBit : 0) |
        (info.err ? detail::kByte7ErrBit : 0) |
        (info.ms  ? detail::kByte7MsBit  : 0));
    detail::put_u16(&out10[8], info.read_size_or_segment_num);
}

inline void decode_acf_message_info(const uint8_t* in10, AcfMessageInfo& out) noexcept {
    out.acf_msg_type   = in10[0];
    out.acf_msg_length = detail::get_u16(&in10[1]);
    out.mtv            = (in10[3] & detail::kByte3MtvBit) != 0;
    out.pad            = static_cast<uint8_t>((in10[3] >> detail::kByte3PadShift) & detail::kByte3PadMask);
    out.byte_bus_id    = in10[4];
    out.evt_ack        = (in10[5] & detail::kByte5AckBit) != 0;
    out.evt_op         = static_cast<uint8_t>((in10[5] >> detail::kByte5OpShift) & detail::kByte5OpMask);
    out.hs             = (in10[5] & detail::kByte5HsBit) != 0;
    out.cs             = (in10[5] & detail::kByte5CsBit) != 0;
    out.transaction_num = in10[6];
    out.op             = (in10[7] & detail::kByte7OpBit) != 0;
    out.rsp            = (in10[7] & detail::kByte7RspBit) != 0;
    out.err            = (in10[7] & detail::kByte7ErrBit) != 0;
    out.ms             = (in10[7] & detail::kByte7MsBit) != 0;
    out.read_size_or_segment_num = detail::get_u16(&in10[8]);
}

// ── ACF_ABB / ACF_GBB message encode / decode ─────────────────────────────────
// ACF_ABB carries no timestamp field at all; ACF_GBB always reserves a
// 64-bit message_timestamp slot immediately after the shared header,
// regardless of whether `mtv` marks it valid (extraction §2.3, §2.7).

inline std::vector<uint8_t> encode_acf_abb(AcfMessageInfo info,
                                            const std::vector<uint8_t>& payload) {
    info.acf_msg_type = kAcfMsgTypeAbb;
    info.mtv          = false;
    std::vector<uint8_t> buf(kAcfCommonHeaderLen + payload.size());
    encode_acf_message_info(info, buf.data());
    std::copy(payload.begin(), payload.end(), buf.begin() + static_cast<long>(kAcfCommonHeaderLen));
    return buf;
}

inline std::error_code decode_acf_abb(const uint8_t* b, size_t len,
                                       AcfMessageInfo& out_info,
                                       std::vector<uint8_t>& out_payload) {
    if (len < 1) return make_error_code(WireErrc::short_buffer);
    if (b[0] != kAcfMsgTypeAbb) return make_error_code(WireErrc::bad_acf_msg_type);
    if (len < kAcfCommonHeaderLen) return make_error_code(WireErrc::short_buffer);
    decode_acf_message_info(b, out_info);
    out_payload.assign(b + kAcfCommonHeaderLen, b + len);
    return {};
}

// message_timestamp is the 64-bit ACF_GBB timestamp slot (extraction §2.7,
// §2.9). Its validity is carried by AcfMessageInfo::mtv, not by the value
// itself — an all-zero timestamp is a legitimate valid value.
inline std::vector<uint8_t> encode_acf_gbb(AcfMessageInfo info, uint64_t message_timestamp,
                                            const std::vector<uint8_t>& payload) {
    info.acf_msg_type = kAcfMsgTypeGbb;
    std::vector<uint8_t> buf(kAcfCommonHeaderLen + kAcfGbbTimestampLen + payload.size());
    encode_acf_message_info(info, buf.data());
    detail::put_u64(&buf[kAcfCommonHeaderLen], message_timestamp);
    std::copy(payload.begin(), payload.end(),
              buf.begin() + static_cast<long>(kAcfCommonHeaderLen + kAcfGbbTimestampLen));
    return buf;
}

inline std::error_code decode_acf_gbb(const uint8_t* b, size_t len,
                                       AcfMessageInfo& out_info,
                                       uint64_t& out_message_timestamp,
                                       std::vector<uint8_t>& out_payload) {
    if (len < 1) return make_error_code(WireErrc::short_buffer);
    if (b[0] != kAcfMsgTypeGbb) return make_error_code(WireErrc::bad_acf_msg_type);
    if (len < kAcfCommonHeaderLen + kAcfGbbTimestampLen)
        return make_error_code(WireErrc::short_buffer);
    decode_acf_message_info(b, out_info);
    out_message_timestamp = detail::get_u64(&b[kAcfCommonHeaderLen]);
    out_payload.assign(b + kAcfCommonHeaderLen + kAcfGbbTimestampLen, b + len);
    return {};
}

// ── Standard request kind & response semantics ────────────────────────────────
// The mandatory baseline request kind: best-effort, unconditional dispatch
// via ACF_ABB (extraction §2.7). Conditional kinds (compound, triggered,
// timed, chained, …) are a later milestone (v2.5.0) layered on top of the
// `AcfMessageInfo` fields already defined above; this module only wires up
// the one kind every RC Server must support.

enum class RequestKind : uint8_t {
    Standard = 0,
};

inline AcfMessageInfo make_standard_request(ByteBusId bus_id, uint8_t transaction_num,
                                             bool write, uint16_t read_size) noexcept {
    AcfMessageInfo info;
    info.acf_msg_type    = kAcfMsgTypeAbb;
    info.byte_bus_id     = bus_id;
    info.transaction_num = transaction_num;
    info.op              = write;
    info.rsp             = false;
    info.err             = false;
    info.read_size_or_segment_num = write ? 0 : read_size;
    return info;
}

// The four response semantic types a request can produce, mapped onto the
// shared header's evt/op/rsp/err fields (extraction §2.7, §2.8).
enum class ResponseKind : uint8_t {
    Acknowledge   = 0, // received, no data — evt_ack set, rsp clear
    WriteResponse = 1, // completed write — rsp set, op set, err clear
    ReadResponse  = 2, // completed read, payload carries the data — rsp set, op clear, err clear
    ErrorResponse = 3, // failed — rsp set, err set
};

inline ResponseKind response_kind_of(const AcfMessageInfo& info) noexcept {
    if (!info.rsp) return ResponseKind::Acknowledge;
    if (info.err)  return ResponseKind::ErrorResponse;
    return info.op ? ResponseKind::WriteResponse : ResponseKind::ReadResponse;
}

// make_response builds the header for a response/ack to `request`. It always
// copies byte_bus_id and transaction_num from the request unchanged — this
// is the mandatory echo-back rule for byte_bus_id (extraction §2.1) —
// callers cannot construct a response with a different byte_bus_id than the
// request it answers through this function.
inline AcfMessageInfo make_response(const AcfMessageInfo& request, ResponseKind kind) noexcept {
    AcfMessageInfo resp;
    resp.acf_msg_type    = request.acf_msg_type;
    resp.byte_bus_id     = request.byte_bus_id;
    resp.transaction_num = request.transaction_num;
    resp.evt_ack = (kind == ResponseKind::Acknowledge);
    resp.rsp     = (kind != ResponseKind::Acknowledge);
    resp.err     = (kind == ResponseKind::ErrorResponse);
    resp.op      = (kind == ResponseKind::WriteResponse);
    return resp;
}

// ── Timestamp fallback rules ──────────────────────────────────────────────────
// avtp_timestamp (TSCF-only, 32-bit) and message_timestamp (ACF_GBB-only,
// 64-bit, valid only when `mtv` is set) are two independent, optional
// sources of timing information for the same logical message (extraction
// §2.6, §2.9). effective_timestamp prefers the TSCF timestamp when present
// and valid, falls back to a valid ACF_GBB message_timestamp, and returns
// std::nullopt — rather than silently defaulting to zero — when neither
// source is usable, so callers can distinguish "no timestamp" from
// "timestamp is zero".

inline std::optional<uint64_t> effective_timestamp(const TscfHeader* tscf,
                                                     const AcfMessageInfo* acf_info,
                                                     uint64_t message_timestamp) noexcept {
    if (tscf != nullptr && tscf->timestamp_valid)
        return uint64_t{tscf->avtp_timestamp};
    if (acf_info != nullptr && acf_info->acf_msg_type == kAcfMsgTypeGbb && acf_info->mtv)
        return message_timestamp;
    return std::nullopt;
}

} // namespace wire
} // namespace rcp
