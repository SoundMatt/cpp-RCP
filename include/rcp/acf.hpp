// fusa:req REQ-WIRE-004
// fusa:req REQ-WIRE-005
// fusa:req REQ-WIRE-006
// fusa:req REQ-WIRE-008
// fusa:req REQ-WIRE-009
// fusa:req REQ-WIRE-010
// fusa:req REQ-WIRE-012
// fusa:req REQ-WIRE-013
// fusa:req REQ-WIRE-014

// TC18 wire codec, message half — the ACF_ABB / ACF_GBB message types (ACF —
// AVTP Control Format) the OPEN Alliance TC18 Remote Control Protocol
// Specification v0.5.1_RC layers on top of rcp/avtp.hpp's AVTPDU framing.
//
// ROADMAP.md milestone 44, "Wire Format Core (v2.0.0)": originally landed
// together with the AVTPDU framing code as a single rcp/wire.hpp; split into
// rcp/avtp.hpp (AVTPDU header framing) and rcp/acf.hpp (this file,
// ACF_ABB/ACF_GBB message format) per RELAY spec §13.7.2's standard
// module-name registry, which names these two concerns separately.
//
// Scope note: this module is a pure wire codec, same as rcp/avtp.hpp — no
// dependency on rcp.hpp's Zone/Command/Controller/Registry model, and no
// dependency on any RC Server lifecycle state, register-map, discovery, or
// endpoint-specific behavior. Conditional request kinds beyond the mandatory
// "standard" kind (v2.5.0, rcp/request.hpp), the E2E CRC safe-point
// mechanism (v2.6.0, rcp/e2e.hpp), and fragmentation (deferred indefinitely,
// v2.8.0) are out of scope here; the fields they need (`cs`, `ms`,
// `read_size_or_segment_num`) already exist below so later milestones can be
// layered on without reshaping this header.
//
// Field names and behavior below implement TC18's *behavior* as described in
// an internal structured extraction of the specification named above; no
// text from that document is reproduced here. The concrete bit-packing
// chosen in this file is this implementation's own encoding of that
// behavior for milestone 44 — full bit-for-bit wire conformance against
// other TC18 implementations is not claimed until v2.6.0 lands, per the
// Phase 13 introduction in ROADMAP.md.
#pragma once

#include <rcp/avtp.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace rcp {
namespace acf {

// ── ACF message types ─────────────────────────────────────────────────────────
// The two ACF (AVTP Control Format) message types RCP defines on top of the
// AVTPDU framing in rcp/avtp.hpp (extraction §2.3).

constexpr uint8_t kAcfMsgTypeGbb = 0x0D; // ACF_GBB — carries a 64-bit message_timestamp slot
constexpr uint8_t kAcfMsgTypeAbb = 0x0E; // ACF_ABB — no timestamp field at all

// ── Errors ────────────────────────────────────────────────────────────────────
// short-buffer conditions are reported via avtp::AvtpErrc::short_buffer
// (rcp/avtp.hpp), which this module already depends on, rather than a
// second short_buffer value defined here.

enum class AcfErrc : int {
    bad_acf_msg_type = 1, // acf_msg_type is neither ACF_ABB nor ACF_GBB
};

inline const std::error_category& acf_category() noexcept {
    struct Cat : std::error_category {
        const char* name() const noexcept override { return "rcp.acf"; }
        std::string message(int ev) const override {
            switch (static_cast<AcfErrc>(ev)) {
            case AcfErrc::bad_acf_msg_type: return "rcp/acf: unrecognized ACF message type";
            default:                        return "rcp/acf: unknown error";
            }
        }
    };
    static Cat instance;
    return instance;
}

inline std::error_code make_error_code(AcfErrc e) noexcept {
    return {static_cast<int>(e), acf_category()};
}

// ── ACF shared header ("byte_message_info") ───────────────────────────────────
// Fields common to both ACF_ABB and ACF_GBB messages (extraction §2.4). Every
// field the roadmap calls out is represented individually below rather than
// left packed in an opaque byte, so later milestones (compound requests,
// safety variants, …) can extend behavior around a given field without
// having to re-derive its bit position from scratch.

struct AcfMessageInfo {
    uint8_t          acf_msg_type   = kAcfMsgTypeAbb; // ACF_ABB or ACF_GBB
    uint16_t         acf_msg_length = 0;              // quadlets, including this shared header
    uint8_t          pad            = 0;              // 0–3 trailing pad bytes added for quadlet alignment
    bool             mtv            = false;          // message_timestamp valid (ACF_GBB only)
    avtp::ByteBusId  byte_bus_id    = 0;               // target endpoint, unique within stream_id only
    bool             evt_ack        = false;          // evt[3] — acknowledge flag
    uint8_t          evt_op         = 0;               // evt[2:0] — endpoint-defined sub-opcode
    bool             hs             = false;          // reserved for endpoint-specific use (e.g. I2C high-speed)
    bool             cs             = false;          // conditional-start; meaning depends on request kind (v2.5.0)
    uint8_t          transaction_num = 0;              // correlates a request with its response/ack
    bool             op             = false;          // false = read, true = write (this codec's convention)
    bool             rsp            = false;          // set on every response/ack, clear on requests
    bool             err            = false;          // set alongside rsp for an error response
    bool             ms             = false;          // "more segments" — fragmentation, deferred to v2.8.0
    uint16_t         read_size_or_segment_num = 0;    // read_size when !ms, segment_num when ms
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
    avtp::detail::put_u16(&out10[1], info.acf_msg_length);
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
    avtp::detail::put_u16(&out10[8], info.read_size_or_segment_num);
}

inline void decode_acf_message_info(const uint8_t* in10, AcfMessageInfo& out) noexcept {
    out.acf_msg_type   = in10[0];
    out.acf_msg_length = avtp::detail::get_u16(&in10[1]);
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
    out.read_size_or_segment_num = avtp::detail::get_u16(&in10[8]);
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
    if (len < 1) return avtp::make_error_code(avtp::AvtpErrc::short_buffer);
    if (b[0] != kAcfMsgTypeAbb) return make_error_code(AcfErrc::bad_acf_msg_type);
    if (len < kAcfCommonHeaderLen) return avtp::make_error_code(avtp::AvtpErrc::short_buffer);
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
    avtp::detail::put_u64(&buf[kAcfCommonHeaderLen], message_timestamp);
    std::copy(payload.begin(), payload.end(),
              buf.begin() + static_cast<long>(kAcfCommonHeaderLen + kAcfGbbTimestampLen));
    return buf;
}

inline std::error_code decode_acf_gbb(const uint8_t* b, size_t len,
                                       AcfMessageInfo& out_info,
                                       uint64_t& out_message_timestamp,
                                       std::vector<uint8_t>& out_payload) {
    if (len < 1) return avtp::make_error_code(avtp::AvtpErrc::short_buffer);
    if (b[0] != kAcfMsgTypeGbb) return make_error_code(AcfErrc::bad_acf_msg_type);
    if (len < kAcfCommonHeaderLen + kAcfGbbTimestampLen)
        return avtp::make_error_code(avtp::AvtpErrc::short_buffer);
    decode_acf_message_info(b, out_info);
    out_message_timestamp = avtp::detail::get_u64(&b[kAcfCommonHeaderLen]);
    out_payload.assign(b + kAcfCommonHeaderLen + kAcfGbbTimestampLen, b + len);
    return {};
}

// ── Standard request kind & response semantics ────────────────────────────────
// The mandatory baseline request kind: best-effort, unconditional dispatch
// via ACF_ABB (extraction §2.7). Conditional kinds (compound, triggered,
// timed, chained, …) are a later milestone (v2.5.0, rcp/request.hpp) layered
// on top of the `AcfMessageInfo` fields already defined above; this module
// only wires up the one kind every RC Server must support.

enum class RequestKind : uint8_t {
    Standard = 0,
};

inline AcfMessageInfo make_standard_request(avtp::ByteBusId bus_id, uint8_t transaction_num,
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
// avtp_timestamp (TSCF-only, 32-bit, rcp/avtp.hpp) and message_timestamp
// (ACF_GBB-only, 64-bit, valid only when `mtv` is set) are two independent,
// optional sources of timing information for the same logical message
// (extraction §2.6, §2.9). effective_timestamp prefers the TSCF timestamp
// when present and valid, falls back to a valid ACF_GBB message_timestamp,
// and returns std::nullopt — rather than silently defaulting to zero — when
// neither source is usable, so callers can distinguish "no timestamp" from
// "timestamp is zero".

inline std::optional<uint64_t> effective_timestamp(const avtp::TscfHeader* tscf,
                                                     const AcfMessageInfo* acf_info,
                                                     uint64_t message_timestamp) noexcept {
    if (tscf != nullptr && tscf->timestamp_valid)
        return uint64_t{tscf->avtp_timestamp};
    if (acf_info != nullptr && acf_info->acf_msg_type == kAcfMsgTypeGbb && acf_info->mtv)
        return message_timestamp;
    return std::nullopt;
}

} // namespace acf
} // namespace rcp
