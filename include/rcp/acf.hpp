// fusa:req REQ-WIRE-004
// fusa:req REQ-WIRE-005
// fusa:req REQ-WIRE-006
// fusa:req REQ-WIRE-008
// fusa:req REQ-WIRE-009
// fusa:req REQ-WIRE-010
// fusa:req REQ-WIRE-012
// fusa:req REQ-WIRE-013
// fusa:req REQ-WIRE-014
// fusa:req REQ-WIRE-015

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
// text from that document is reproduced here. Wire conformance note
// (v2.19.0, issue cpp-RCP-04): the ACF Message Info bit-packing below was
// re-derived field by field from the specification's own header diagram
// (its bit-position figure, not prose) and cross-checked two ways: (1)
// against a second, independent diagram of the same header shape used for a
// different message subtype elsewhere in the specification, and (2) against
// two fully worked numeric examples elsewhere in the specification (one
// ACF_ABB, one ACF_GBB) that state concrete byte counts and an
// acf_msg_length value for a chosen payload/padding — this file's own
// quadlet-counting logic reproduces both stated values exactly. See this
// repository's pull request description for the full derivation and its
// honestly-stated confidence level.
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
//
// Wire layout — 8 bytes total (re-derived from the specification's own
// bit-position diagram for this header; several fields are *not* whole
// octets, which only that diagram shows):
//   byte0 bits7:1    acf_msg_type   — 7 bits (0x0D ACF_GBB / 0x0E ACF_ABB),
//                    not a full octet
//   byte0 bit0 +
//   byte1            acf_msg_length — 9 bits total (byte0's LSB is the MSB
//                    of the value, byte1 is its low 8 bits); quadlets,
//                    including this shared header
//   byte2 bits7:6    pad            — 2 bits, 0–3 trailing pad octets
//   byte2 bit5       mtv            — message_timestamp valid (ACF_GBB only)
//   byte2 bits4:3    rsv            — always 0; not surfaced as a field
//   byte2 bits2:0 +
//   byte3            byte_bus_id    — 11 bits total (byte2's low 3 bits are
//                    its MSBs, byte3 is its low 8 bits)
//   byte4 bits7:4    evt            — 4 bits; evt_ack is evt's top bit
//                    (evt[3]), evt_op is its low 3 bits (evt[2:0]) — see
//                    evt_ack/evt_op field comments below for why this codec
//                    keeps that split rather than one raw nibble
//   byte4 bits3:2    rsv            — always 0; not surfaced as a field
//   byte4 bit1       hs
//   byte4 bit0       cs
//   byte5            transaction_num — a full octet
//   byte6 bit7       op
//   byte6 bit6       rsp
//   byte6 bit5       err
//   byte6 bit4       ms
//   byte6 bits3:0 +
//   byte7            read_size_or_segment_num — 12 bits total (byte6's low
//                    4 bits are its MSBs, byte7 is its low 8 bits)
//
// This 8-byte total (not the 10 bytes this codec used before this pass) is
// independently confirmed by two fully worked numeric examples elsewhere in
// the specification that label this exact header "8 byte Message Info" and
// state concrete acf_msg_length values this file's own quadlet-counting
// reproduces exactly — see the header comment above and this repository's
// pull request description.

struct AcfMessageInfo {
    uint8_t          acf_msg_type   = kAcfMsgTypeAbb; // ACF_ABB or ACF_GBB — 7-bit wire field, values 0x0D/0x0E fit either way
    uint16_t         acf_msg_length = 0;              // quadlets, including this shared header — 9-bit wire field (0–511), encoder masks
    uint8_t          pad            = 0;              // 0–3 trailing pad bytes added for quadlet alignment
    bool             mtv            = false;          // message_timestamp valid (ACF_GBB only)
    avtp::ByteBusId  byte_bus_id    = 0;               // target endpoint, unique within stream_id only — 11-bit wire field (0–2047), encoder masks
    // evt is a 4-bit wire field (evt[3:0]) whose interpretation is message-
    // kind-dependent per the specification's own response/request tables:
    // for a "clear specific request" and for every response this codec
    // builds via make_response() below, evt[3] is cleanly an
    // acknowledge-request/acknowledge-response flag and evt[2:0] is either
    // reserved or a small counter — which is exactly the evt_ack (bit3) +
    // evt_op (bits2:0) split kept here. For a standard request, evt[3:0] is
    // instead fully endpoint-defined; callers in that role are still free to
    // use evt_op alone (masked to 3 bits) or set evt_ack for whatever their
    // endpoint defines bit3 to mean. Either way the two fields together
    // always reproduce the exact 4-bit wire value: evt = (evt_ack<<3)|evt_op.
    bool             evt_ack        = false;          // evt[3]
    uint8_t          evt_op         = 0;               // evt[2:0], masked to 3 bits on encode
    bool             hs             = false;          // reserved for endpoint-specific use (e.g. I2C high-speed)
    bool             cs             = false;          // conditional-start; meaning depends on request kind (v2.5.0)
    uint8_t          transaction_num = 0;              // correlates a request with its response/ack
    bool             op             = false;          // false = read, true = write (this codec's convention)
    bool             rsp            = false;          // set on every response (including Acknowledge — extraction Table 15), clear on requests
    bool             err            = false;          // set alongside rsp for an error response
    bool             ms             = false;          // "more segments" — fragmentation, deferred to v2.8.0
    uint16_t         read_size_or_segment_num = 0;    // read_size when !ms, segment_num when ms — 12-bit wire field (0–4095), encoder masks
};

constexpr size_t kAcfCommonHeaderLen = 8;
constexpr size_t kAcfGbbTimestampLen = 8;

// evt[3:0] == 0xF identifies a response as an Acknowledge (extraction
// Table 15) — the wire value this codec's own make_response()/
// response_kind_of() below use for that purpose. Not meaningful for
// standard requests, whose evt is fully endpoint-defined.
constexpr uint8_t kEvtAcknowledge = 0x0F;

namespace detail {
constexpr uint8_t  kMsgTypeMask       = 0x7F; // acf_msg_type: 7 bits
constexpr uint16_t kMsgLengthMask     = 0x01FF; // acf_msg_length: 9 bits
constexpr uint8_t  kPadMask           = 0x03; // pad: 2 bits
constexpr uint8_t  kByte2MtvBit       = 0x20; // mtv: byte2 bit5
constexpr uint16_t kByteBusIdMask     = 0x07FF; // byte_bus_id: 11 bits
constexpr uint8_t  kEvtMask           = 0x0F; // evt: 4 bits
constexpr uint8_t  kEvtOpMask         = 0x07; // evt_op: low 3 bits of evt
constexpr uint8_t  kEvtAckBit         = 0x08; // evt_ack: top bit of evt
constexpr uint8_t  kByte4HsBit        = 0x02;
constexpr uint8_t  kByte4CsBit        = 0x01;
constexpr uint8_t  kByte6OpBit        = 0x80;
constexpr uint8_t  kByte6RspBit       = 0x40;
constexpr uint8_t  kByte6ErrBit       = 0x20;
constexpr uint8_t  kByte6MsBit        = 0x10;
constexpr uint16_t kReadSizeMask      = 0x0FFF; // read_size_or_segment_num: 12 bits
} // namespace detail

inline void encode_acf_message_info(const AcfMessageInfo& info, uint8_t* out8) noexcept {
    const uint16_t msg_length = static_cast<uint16_t>(info.acf_msg_length & detail::kMsgLengthMask);
    const uint16_t bus_id     = static_cast<uint16_t>(info.byte_bus_id & detail::kByteBusIdMask);
    const uint8_t  evt        = static_cast<uint8_t>(
        (info.evt_ack ? detail::kEvtAckBit : 0) | (info.evt_op & detail::kEvtOpMask));
    const uint16_t read_size  = static_cast<uint16_t>(info.read_size_or_segment_num & detail::kReadSizeMask);

    out8[0] = static_cast<uint8_t>(((info.acf_msg_type & detail::kMsgTypeMask) << 1) |
                                    ((msg_length >> 8) & 0x01));
    out8[1] = static_cast<uint8_t>(msg_length & 0xFF);
    out8[2] = static_cast<uint8_t>((static_cast<uint8_t>(info.pad & detail::kPadMask) << 6) |
                                    (info.mtv ? detail::kByte2MtvBit : 0) |
                                    ((bus_id >> 8) & 0x07));
    out8[3] = static_cast<uint8_t>(bus_id & 0xFF);
    out8[4] = static_cast<uint8_t>((evt << 4) |
                                    (info.hs ? detail::kByte4HsBit : 0) |
                                    (info.cs ? detail::kByte4CsBit : 0));
    out8[5] = info.transaction_num;
    out8[6] = static_cast<uint8_t>(
        (info.op  ? detail::kByte6OpBit  : 0) |
        (info.rsp ? detail::kByte6RspBit : 0) |
        (info.err ? detail::kByte6ErrBit : 0) |
        (info.ms  ? detail::kByte6MsBit  : 0) |
        ((read_size >> 8) & 0x0F));
    out8[7] = static_cast<uint8_t>(read_size & 0xFF);
}

inline void decode_acf_message_info(const uint8_t* in8, AcfMessageInfo& out) noexcept {
    out.acf_msg_type   = static_cast<uint8_t>(in8[0] >> 1);
    out.acf_msg_length = static_cast<uint16_t>(((in8[0] & 0x01) << 8) | in8[1]);
    out.pad            = static_cast<uint8_t>((in8[2] >> 6) & detail::kPadMask);
    out.mtv            = (in8[2] & detail::kByte2MtvBit) != 0;
    out.byte_bus_id     = static_cast<avtp::ByteBusId>(((in8[2] & 0x07) << 8) | in8[3]);
    const uint8_t evt   = static_cast<uint8_t>((in8[4] >> 4) & detail::kEvtMask);
    out.evt_ack         = (evt & detail::kEvtAckBit) != 0;
    out.evt_op          = static_cast<uint8_t>(evt & detail::kEvtOpMask);
    out.hs              = (in8[4] & detail::kByte4HsBit) != 0;
    out.cs              = (in8[4] & detail::kByte4CsBit) != 0;
    out.transaction_num = in8[5];
    out.op              = (in8[6] & detail::kByte6OpBit) != 0;
    out.rsp             = (in8[6] & detail::kByte6RspBit) != 0;
    out.err             = (in8[6] & detail::kByte6ErrBit) != 0;
    out.ms              = (in8[6] & detail::kByte6MsBit) != 0;
    out.read_size_or_segment_num = static_cast<uint16_t>(((in8[6] & 0x0F) << 8) | in8[7]);
}

// ── ACF_ABB / ACF_GBB message encode / decode ─────────────────────────────────
// ACF_ABB carries no timestamp field at all; ACF_GBB always reserves a
// 64-bit message_timestamp slot immediately after the shared header,
// regardless of whether `mtv` marks it valid (extraction §2.3, §2.7).

// peek_acf_msg_type reads the 7-bit acf_msg_type field out of a raw buffer's
// first octet without decoding the rest of the header. Needed because
// acf_msg_type is *not* a whole octet on the wire (byte0 bit0 is
// acf_msg_length's MSB, not part of the type) — callers that need to decide
// which of decode_acf_abb/decode_acf_gbb to call before fully decoding
// (e.g. rcp/udp.hpp's Frame decoder) must go through this rather than
// comparing a raw buffer byte against kAcfMsgTypeAbb/kAcfMsgTypeGbb
// directly, which would almost never match. `len` must be >= 1.
inline uint8_t peek_acf_msg_type(const uint8_t* b) noexcept {
    return static_cast<uint8_t>(b[0] >> 1);
}

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
    if (peek_acf_msg_type(b) != kAcfMsgTypeAbb) return make_error_code(AcfErrc::bad_acf_msg_type);
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
    if (peek_acf_msg_type(b) != kAcfMsgTypeGbb) return make_error_code(AcfErrc::bad_acf_msg_type);
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

// The four response semantic types a request can produce (extraction
// Table 14/Table 15). Per Table 15, IEEE1722 formally only knows
// "responses" — RCP's Acknowledge is one of the four response variants, not
// a separate rsp=0 message: every response, Acknowledge included, carries
// rsp=1, and Acknowledge is distinguished purely by evt[3:0] == 0xF
// (kEvtAcknowledge). This is a correction from this codec's pre-v2.19.0
// behavior, which modeled Acknowledge as rsp=0 — that matched neither this
// header's own request-vs-response `rsp` field meaning (0 = request,
// 1 = response — Acknowledge is unambiguously a response) nor evt[3:0]'s
// real role as the field that actually distinguishes a plain/counted
// response from an acknowledge.
enum class ResponseKind : uint8_t {
    Acknowledge   = 0, // rsp set, evt[3:0] == 0xF (kEvtAcknowledge)
    WriteResponse = 1, // completed write — rsp set, op set, err clear, evt[3:0] != 0xF
    ReadResponse  = 2, // completed read, payload carries the data — rsp set, op clear, err clear, evt[3:0] != 0xF
    ErrorResponse = 3, // failed — rsp set, err set, evt[3:0] != 0xF
};

inline ResponseKind response_kind_of(const AcfMessageInfo& info) noexcept {
    const uint8_t evt = static_cast<uint8_t>((info.evt_ack ? detail::kEvtAckBit : 0) |
                                              (info.evt_op & detail::kEvtOpMask));
    if (evt == kEvtAcknowledge) return ResponseKind::Acknowledge;
    if (info.err)               return ResponseKind::ErrorResponse;
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
    resp.rsp = true; // every response, including Acknowledge, carries rsp=1
    if (kind == ResponseKind::Acknowledge) {
        resp.evt_ack = true;
        resp.evt_op  = detail::kEvtOpMask; // together, evt[3:0] == 0xF == kEvtAcknowledge
    }
    resp.err = (kind == ResponseKind::ErrorResponse);
    resp.op  = (kind == ResponseKind::WriteResponse);
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

// ── RELAY spec §15.5 canonical types (additive; issue cpp-RCP-FS-06) ─────────
// RELAY spec §15.5 ("RCP — StreamID, ByteBusID, TransactionNum, ControlFlags,
// Message") defines a canonical, protocol-agnostic shape for a decoded
// ACF_ABB/ACF_GBB request/response/acknowledge, matching go-RCP's own types
// field-for-field for zero-copy casting across the two implementations.
// ControlFlags and Message below are new, additive types implementing that
// canonical shape as a thin, documented view over this file's own
// AcfMessageInfo (which remains the codec's own working representation —
// nothing above this section changes to accommodate the mapping below).
//
// Two field widths intentionally differ from AcfMessageInfo/StreamId's own,
// because RELAY spec §15.5 fixes them independently of TC18's real wire
// widths (re-derived elsewhere in this file, see the "ACF shared header"
// section above):
//   - ControlFlags::ByteBusID's underlying type in the spec is uint8_t,
//     narrower than avtp::ByteBusId's 11-bit range; to_message() truncates,
//     from_message() zero-extends. Round-trips losslessly only for
//     byte_bus_id values 0–255.
//   - Message::TransactionNum is uint16_t, wider than AcfMessageInfo's
//     8-bit transaction_num; the conversion is always lossless in that
//     direction (a widening store), never the reverse.

enum class ControlFlags : uint8_t {
    None             = 0,
    FlagAck          = 1u << 7,
    FlagRead         = 1u << 6,
    FlagWrite        = 1u << 5,
    FlagResponse     = 1u << 4,
    FlagError        = 1u << 3,
    FlagMoreSegments = 1u << 2,
};

// ControlFlags is a bitmask enum by design (RELAY spec §15.5's own Go
// definition is likewise a set of independent bit constants, not an
// exhaustive enumeration); operator|/operator& legitimately produce/consume
// combined values with no enumerator of their own, which is exactly what
// clang-analyzer-optin.core.EnumCastOutOfRange (a strict-by-design check
// aimed at accidental enum casts, not intentional bitmask ones) flags below
// — NOLINT'd on both cast lines as a deliberate, reviewed exception.
constexpr ControlFlags operator|(ControlFlags a, ControlFlags b) noexcept {
    return static_cast<ControlFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b)); // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange)
}
constexpr ControlFlags operator&(ControlFlags a, ControlFlags b) noexcept {
    return static_cast<ControlFlags>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b)); // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange)
}
constexpr bool has_flag(ControlFlags value, ControlFlags flag) noexcept {
    return (value & flag) == flag;
}

// Message is the RELAY spec §15.5 canonical decoded-frame shape: a
// protocol/direction-agnostic view of one ACF_ABB/ACF_GBB request, response,
// or acknowledge, independent of the AVTPDU (NTSCF/TSCF, rcp/avtp.hpp) that
// carried it — stream_id/avtp_timestamp are deliberately not part of this
// type, same as the Go definition it mirrors.
struct Message {
    uint8_t               byte_bus_id             = 0; // RELAY spec ByteBusID — see width note above
    uint16_t               transaction_num         = 0; // RELAY spec TransactionNum — see width note above
    ControlFlags           control                 = ControlFlags::None;
    uint16_t               read_size_or_segment     = 0; // read_size (FlagMoreSegments clear) or segment_num (set)
    uint64_t               timestamp               = 0; // meaningful only for the ACF_GBB (mtv=1) encoding
    std::vector<uint8_t>   body;
};

// to_message converts a decoded AcfMessageInfo + its ACF_GBB message_timestamp
// (pass 0 for ACF_ABB, which has none) + payload into the RELAY spec §15.5
// canonical shape.
inline Message to_message(const AcfMessageInfo& info, uint64_t message_timestamp,
                           std::vector<uint8_t> body) {
    Message msg;
    msg.byte_bus_id     = static_cast<uint8_t>(info.byte_bus_id & 0xFF);
    msg.transaction_num = info.transaction_num;
    ControlFlags flags  = ControlFlags::None;
    if (info.evt_ack) flags = flags | ControlFlags::FlagAck;
    flags = flags | (info.op ? ControlFlags::FlagWrite : ControlFlags::FlagRead);
    if (info.rsp) flags = flags | ControlFlags::FlagResponse;
    if (info.err) flags = flags | ControlFlags::FlagError;
    if (info.ms)  flags = flags | ControlFlags::FlagMoreSegments;
    msg.control              = flags;
    msg.read_size_or_segment = info.read_size_or_segment_num;
    msg.timestamp             = (info.acf_msg_type == kAcfMsgTypeGbb) ? message_timestamp : 0;
    msg.body                  = std::move(body);
    return msg;
}

// from_message is to_message's inverse: it rebuilds an AcfMessageInfo (plus
// the separate message_timestamp/body outputs encode_acf_abb/encode_acf_gbb
// take) from a canonical Message. Since Message carries no acf_msg_type of
// its own, the caller states whether to target ACF_ABB or ACF_GBB; mtv is
// set exactly when the caller asks for ACF_GBB with a non-zero timestamp
// (an all-zero valid timestamp cannot be distinguished from "no timestamp"
// through this narrower canonical type alone — pass `mtv` explicitly when
// that distinction matters, same as AcfMessageInfo::mtv always requires).
inline AcfMessageInfo from_message(const Message& msg, bool as_gbb, bool mtv,
                                    uint64_t& out_message_timestamp) noexcept {
    AcfMessageInfo info;
    info.acf_msg_type    = as_gbb ? kAcfMsgTypeGbb : kAcfMsgTypeAbb;
    info.mtv              = as_gbb && mtv;
    info.byte_bus_id      = msg.byte_bus_id;
    info.evt_ack           = has_flag(msg.control, ControlFlags::FlagAck);
    info.op                = has_flag(msg.control, ControlFlags::FlagWrite);
    info.rsp                = has_flag(msg.control, ControlFlags::FlagResponse);
    info.err                = has_flag(msg.control, ControlFlags::FlagError);
    info.ms                  = has_flag(msg.control, ControlFlags::FlagMoreSegments);
    info.transaction_num     = static_cast<uint8_t>(msg.transaction_num & 0xFF);
    info.read_size_or_segment_num = msg.read_size_or_segment;
    out_message_timestamp     = msg.timestamp;
    return info;
}

} // namespace acf
} // namespace rcp
