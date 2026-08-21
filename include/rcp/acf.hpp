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
// Field names and behavior below implement TC18's *behavior*, ported from
// c-RCP's acf.h/acf.c (Phase 17, cpp-RCP issue #129), which is this
// project's RC5-spec-conformant reference implementation for this wire
// layer; no text from the specification is reproduced here.
//
// byte_message_info bit-packing (encode_acf_message_info/
// decode_acf_message_info below) was already correct as of this codec's
// v2.19.0 pass and is unchanged by the Phase 17 port — verified bit-for-bit
// against c-RCP's own pack/unpack golden-vector tests (test_acf.c's
// test_pack_header_bit_positions/test_unpack_header_bit_positions_from_
// raw_bytes), which this file's own equivalent tests below now also pin.
//
// acf_msg_length auto-fill (v2.20.0, issue cpp-RCP-01, retained by the
// Phase 17 port): AcfMessageInfo::acf_msg_length defaults to 0;
// encode_acf_abb()/encode_acf_gbb() compute it from the caller's actual
// payload size (compute_acf_msg_length()) whenever the caller leaves it at
// that 0 default, and take a nonzero value as an explicit override,
// re-serializing it unchanged instead. This is a deliberate divergence from
// c-RCP's own rcp_acf_encode_abb()/_gbb(), which always recompute
// acf_msg_length (and always auto-pad the payload to a quadlet boundary via
// rcp_acf_pad_len(), overwriting whatever the caller's own hdr->pad held) —
// this codec's "0 = auto, nonzero = override" contract, and its
// caller-owns-padding convention, are load-bearing for rcp/e2e.hpp's
// apply_acf_length_adjustment(), which must bake a trailing CRC32 trailer's
// +1 quadlet into the header *before* it is serialized for CRC coverage,
// and are exercised by rcp/e2e.hpp's own tests (tests/test_e2e.cpp). Phase
// 17 (this pass) is scoped to acf.hpp/avtp.hpp only and does not touch
// rcp/e2e.hpp, so this codec deliberately keeps both conventions rather
// than adopting c-RCP's always-recompute/always-pad behavior, which would
// silently break that contract. See pad_len() below (new, additive —
// ported from rcp_acf_pad_len() as a pure utility any caller MAY use) and
// this file's own "// TODO(phase1-followup)" markers for the specific
// c-RCP behaviors this pass intentionally left unported for that reason.
//
// ACF_GBB Message Info geometry (Phase 17 wire-format fix, issue
// cpp-RCP-GBB-TS): a prior pass (v2.22.0) placed the 64-bit
// message_timestamp *spliced between* the shared header's two quadlets
// (wire octet 4, pushing evt/hs/cs/transaction_num/op/rsp/err/ms/
// read_size_or_segment_num to octet 12). c-RCP's acf.h/acf.c — this
// project's RC5-conformant reference for this module — is unambiguous that
// the real layout is CONTIGUOUS instead: the complete 8-byte
// byte_message_info header at octets 0..7, the 8-byte message_timestamp
// immediately after it at octets 8..15, then byte_msg_payload at octet 16
// onward (RCP_ACF_GBB_HEADER_LEN == RCP_ACF_ABB_HEADER_LEN + 8, per c-RCP's
// acf.h file comment, its rcp_acf_encode_gbb()/_decode_gbb() implementation
// in acf.c, its own test_peek_gbb_request_type() pinning byte 8 as the
// first byte after a *contiguous* 8-byte header, and its
// .fusa-reqs.json REQ-ACF-044 citation "ACF_GBB's additional 8-byte
// message_timestamp, e.g. TC18.txt L1447-1451"). The v2.22.0 splice was
// therefore a regression, not a fix; it is reverted below. Every real
// caller in this tree (rcp/e2e.hpp, rcp/l2.hpp, rcp/udp.hpp,
// rcp/record.hpp, rcp/request.hpp) reaches the Message Info block only
// through encode_acf_gbb_message_info()/decode_acf_gbb_message_info()/
// encode_acf_gbb()/decode_acf_gbb() below, never through a raw byte offset
// of its own, so this fix changes wire *content* for those callers but not
// their source. The one exception is tests/test_e2e.cpp, which pins the
// old (spliced) layout via hardcoded byte literals — those assertions are
// now wrong and need a follow-up fix in a later phase that touches
// rcp/e2e.hpp; see this pass's own PR description for the specific test
// cases affected. This is a wire-format-breaking change against every
// cpp-RCP release since v2.22.0 — ACF_ABB is entirely unaffected, since it
// has no timestamp field.
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
    bad_acf_msg_type   = 1, // acf_msg_type is neither ACF_ABB nor ACF_GBB
    bad_acf_msg_length = 2, // a decoded acf_msg_length does not fit the buffer it was found in
};

inline const std::error_category& acf_category() noexcept {
    struct Cat : std::error_category {
        const char* name() const noexcept override { return "rcp.acf"; }
        std::string message(int ev) const override {
            switch (static_cast<AcfErrc>(ev)) {
            case AcfErrc::bad_acf_msg_type:   return "rcp/acf: unrecognized ACF message type";
            case AcfErrc::bad_acf_msg_length: return "rcp/acf: acf_msg_length inconsistent with buffer size";
            default:                          return "rcp/acf: unknown error";
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

// ── ACF_GBB Message Info wire geometry ────────────────────────────────────────
// For ACF_GBB, the 64-bit message_timestamp sits immediately AFTER the
// complete, contiguous 8-byte byte_message_info header — not spliced
// between its two quadlets. An ACF_GBB Message Info block is therefore 16
// bytes laid out as:
//
//   offset  0..7   the complete byte_message_info header, same contiguous
//                  8-octet layout ACF_ABB uses (acf_msg_type /
//                  acf_msg_length / pad / mtv / rsv / byte_bus_id / evt /
//                  rsv / hs / cs / transaction_num / op / rsp / err / ms /
//                  read_size_or_segment_num)
//   offset  8..15  message_timestamp, 64-bit big-endian
//   offset 16..    byte_msg_payload
//
// This is ported directly from c-RCP's acf.h/acf.c (this project's
// RC5-conformant reference for this module — see this file's own header
// comment for the full derivation and the specific byte-geometry bug this
// reverts): RCP_ACF_GBB_HEADER_LEN there is defined as exactly
// RCP_ACF_ABB_HEADER_LEN + 8, rcp_acf_encode_gbb() packs the full 8-byte
// header contiguously via rcp_acf_pack_header() and then writes
// message_timestamp at that fixed offset 8, and its own
// test_peek_gbb_request_type() pins byte 8 as the first byte immediately
// following a *contiguous* 8-byte header (the message_timestamp region's
// own leading octet, repurposed by the conditional-request modules).
constexpr size_t kAcfGbbTimestampOffset = kAcfCommonHeaderLen; // 8: right after the contiguous header
constexpr size_t kAcfGbbMessageInfoLen  = kAcfCommonHeaderLen + kAcfGbbTimestampLen; // 16

// compute_acf_msg_length computes the wire acf_msg_length value (quadlets,
// counted over the *entire* ACF message: the 8-byte shared header, the
// 8-byte message_timestamp for ACF_GBB, and the payload as given) for a
// message about to be encoded (issue cpp-RCP-01). Confirmed against the
// specification's own two fully worked numeric examples:
//   ACF_ABB, 6-byte payload + 2 pad bytes already folded into the payload
//   the caller passes (8 bytes total) + a 4-byte CRC32 trailer the caller
//   accounts for separately (not part of `payload_len` here, see
//   rcp/e2e.hpp's apply_acf_length_adjustment): header(8) + payload(8) = 16
//   bytes = 4 quadlets; +1 quadlet for the trailer = 5 = 0x05.
//   ACF_GBB, 7-byte payload + 1 pad byte already folded in (8 bytes total):
//   header(8) + timestamp(8) + payload(8) = 24 bytes = 6 quadlets; +1
//   quadlet for the trailer = 7 = 0x07.
// Both match the specification's stated acf_msg_length for those examples
// exactly (see this repository's pull request description for the
// byte-by-byte derivation).
//
// Known scope limit: this function counts whatever `payload_len` it is
// given — it does not itself round `payload_len` up to a quadlet boundary
// or append real pad octets, since padding has always been a caller-owned
// concern in this codec (the `pad` field exists precisely so a caller that
// *does* pad its payload can still tell a decoder how many trailing octets
// to treat as padding — see the "ACF shared header" section above). A
// caller whose payload is not already quadlet-aligned and wants a
// byte-exact acf_msg_length must pad the payload itself (adjusting `pad`
// to match) before calling this — the same convention every existing
// caller of AcfMessageInfo::pad in this codebase already follows.
inline uint16_t compute_acf_msg_length(uint8_t acf_msg_type, size_t payload_len) noexcept {
    const size_t fixed_len = kAcfCommonHeaderLen + (acf_msg_type == kAcfMsgTypeGbb ? kAcfGbbTimestampLen : 0);
    const size_t total     = fixed_len + payload_len;
    return static_cast<uint16_t>((total + 3) / 4); // ceiling division to whole quadlets
}

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

// ── ACF_GBB Message Info: header + timestamp, contiguous ─────────────────────
// encode_acf_gbb_message_info/decode_acf_gbb_message_info are the ACF_GBB
// analogues of encode_acf_message_info/decode_acf_message_info above. They
// deliberately reuse those two functions for the bit-level packing (so the
// bit layout has exactly one definition in this file) and only add the
// ACF_GBB-specific byte geometry: the complete 8-byte header, then the
// 64-bit message_timestamp immediately after it — see the
// kAcfGbbTimestampOffset comment block above for the c-RCP-derived
// verification of that ordering. `out16`/`in16` must point at
// kAcfGbbMessageInfoLen (16) accessible bytes.
inline void encode_acf_gbb_message_info(const AcfMessageInfo& info, uint64_t message_timestamp,
                                         uint8_t* out16) noexcept {
    encode_acf_message_info(info, out16);
    avtp::detail::put_u64(out16 + kAcfGbbTimestampOffset, message_timestamp);
}

inline void decode_acf_gbb_message_info(const uint8_t* in16, AcfMessageInfo& out_info,
                                         uint64_t& out_message_timestamp) noexcept {
    decode_acf_message_info(in16, out_info);
    out_message_timestamp = avtp::detail::get_u64(in16 + kAcfGbbTimestampOffset);
}

// ── ACF_ABB / ACF_GBB message encode / decode ─────────────────────────────────
// ACF_ABB carries no timestamp field at all; ACF_GBB always reserves a
// 64-bit message_timestamp slot immediately after the Message Info header
// (contiguous, see above), regardless of whether `mtv` marks it valid
// (extraction §2.3, §2.7).

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

// peek_msg_type is peek_acf_msg_type's checked, whole-buffer-validating
// counterpart (ported from c-RCP's rcp_acf_peek_msg_type()): it also
// validates `len >= 1` itself, rather than requiring the caller to do so
// before calling peek_acf_msg_type() directly.
inline std::error_code peek_msg_type(const uint8_t* b, size_t len, uint8_t& out_msg_type) noexcept {
    if (len < 1) return avtp::make_error_code(avtp::AvtpErrc::short_buffer);
    out_msg_type = peek_acf_msg_type(b);
    return {};
}

// pad_len (ported from c-RCP's rcp_acf_pad_len()) returns the number of
// zero pad octets (0-3) needed to bring `unpadded_len` octets of
// header(+timestamp)+payload up to a whole number of quadlets — the unit
// acf_msg_length is expressed in. A pure, additive utility: encode_acf_abb()
// /encode_acf_gbb() below do NOT call this automatically (see this file's
// own header comment for why — rcp/e2e.hpp's caller-owns-padding contract),
// but a caller that wants c-RCP's own auto-pad accounting for a payload it
// is about to hand to encode_acf_abb()/_gbb() can compute it here first.
inline uint8_t pad_len(size_t unpadded_len) noexcept {
    return static_cast<uint8_t>((4u - (unpadded_len % 4u)) % 4u);
}

// acf_msg_length is a 9-bit quadlet count (Table 4) — the largest ACF
// message (header/timestamp + payload + pad) this codec can represent.
// Ported from c-RCP's RCP_ACF_MAX_QUADLETS/RCP_ACF_ABB_MAX_PAYLOAD/
// RCP_ACF_GBB_MAX_PAYLOAD. Informational only: encode_acf_abb()/
// encode_acf_gbb() below mask acf_msg_length to 9 bits on encode (see
// detail::kMsgLengthMask) rather than rejecting an oversized payload
// outright, matching this codec's existing "always returns bytes, never an
// error code" contract for those two functions (see this file's header
// comment) — a caller that must not silently wrap can check a payload's
// size against these bounds itself before encoding.
constexpr uint16_t kAcfMaxQuadlets   = 0x1FFu;
constexpr size_t   kAcfAbbMaxPayload = static_cast<size_t>(kAcfMaxQuadlets) * 4u - kAcfCommonHeaderLen;
constexpr size_t   kAcfGbbMaxPayload = static_cast<size_t>(kAcfMaxQuadlets) * 4u - kAcfGbbMessageInfoLen;

inline std::vector<uint8_t> encode_acf_abb(AcfMessageInfo info,
                                            const std::vector<uint8_t>& payload) {
    info.acf_msg_type = kAcfMsgTypeAbb;
    info.mtv          = false;
    // acf_msg_length auto-fill (issue cpp-RCP-01): 0 is never a valid real
    // wire value (the shared header alone is already 2 quadlets), so a
    // caller leaving AcfMessageInfo::acf_msg_length at its default means
    // "compute it for me from the payload I'm actually giving you" —
    // exactly what every real caller in this tree does today. A caller
    // that has already computed and set a specific nonzero value itself
    // (e.g. rcp/e2e.hpp's apply_acf_length_adjustment(), to bake a trailing
    // CRC's +1 quadlet in before this header is serialized for CRC
    // coverage) is always respected unchanged.
    if (info.acf_msg_length == 0)
        info.acf_msg_length = compute_acf_msg_length(info.acf_msg_type, payload.size());
    std::vector<uint8_t> buf(kAcfCommonHeaderLen + payload.size());
    encode_acf_message_info(info, buf.data());
    std::copy(payload.begin(), payload.end(), buf.begin() + static_cast<long>(kAcfCommonHeaderLen));
    return buf;
}

// TODO(phase1-followup): c-RCP's rcp_acf_decode_abb()/_decode_gbb() are
// stricter than this pair: they treat the decoded acf_msg_length*4 as the
// message's authoritative byte length (rejecting a buffer shorter than
// that declared length, RCP_ACF_ERR_SHORT_FRAME) and trim `pad` trailing
// octets off of *out_payload_len so a caller never sees pad bytes as
// payload. This codec deliberately keeps its existing, more lenient
// contract instead — payload is simply "everything from the header to the
// end of the buffer given" — because it is load-bearing for callers
// outside this pass's Phase 17 scope (acf.hpp/avtp.hpp only): every real
// payload builder in this tree (rcp/gpio.hpp, rcp/pwm.hpp, rcp/spi.hpp,
// etc., dispatched through rcp/mock.hpp/rcp/l2.hpp/rcp/udp.hpp) hands
// encode_acf_abb()/_gbb() an arbitrary-length payload without pre-padding
// it to a quadlet boundary or setting AcfMessageInfo::pad, so
// compute_acf_msg_length()'s ceiling-rounded acf_msg_length is not
// generally byte-exact for those callers' frames — enforcing it strictly
// on decode would reject frames this library's own encoders legitimately
// produce today. tests/test_e2e.cpp also asserts today that decode does
// NOT trim pad (its "ACF_GBB full-message layout..." test checks
// `decoded_payload == payload` where `payload` still includes the literal
// trailing pad byte). Revisit this once a later phase makes every payload
// builder in this tree pre-pad via pad_len() (added above) and sets `pad`
// accordingly, at which point c-RCP's stricter decode can be adopted
// without rejecting frames this library itself still emits.
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
    // acf_msg_length auto-fill (issue cpp-RCP-01) — see encode_acf_abb's
    // equivalent comment above for the full rationale; same "0 means
    // compute it, nonzero is an explicit caller override" contract.
    if (info.acf_msg_length == 0)
        info.acf_msg_length = compute_acf_msg_length(info.acf_msg_type, payload.size());
    std::vector<uint8_t> buf(kAcfGbbMessageInfoLen + payload.size());
    encode_acf_gbb_message_info(info, message_timestamp, buf.data());
    std::copy(payload.begin(), payload.end(),
              buf.begin() + static_cast<long>(kAcfGbbMessageInfoLen));
    return buf;
}

inline std::error_code decode_acf_gbb(const uint8_t* b, size_t len,
                                       AcfMessageInfo& out_info,
                                       uint64_t& out_message_timestamp,
                                       std::vector<uint8_t>& out_payload) {
    if (len < 1) return avtp::make_error_code(avtp::AvtpErrc::short_buffer);
    if (peek_acf_msg_type(b) != kAcfMsgTypeGbb) return make_error_code(AcfErrc::bad_acf_msg_type);
    if (len < kAcfGbbMessageInfoLen)
        return avtp::make_error_code(avtp::AvtpErrc::short_buffer);
    decode_acf_gbb_message_info(b, out_info, out_message_timestamp);
    out_payload.assign(b + kAcfGbbMessageInfoLen, b + len);
    return {};
}

// ── Multiple ACF requests in one frame (extraction §12.9.1.1; issue cpp-RCP-04-fresh) ──
// "An RC Server shall support to handle multiple requests in one frame and
// check each of them individually if to be processed or not ... The RC
// Server shall support the handling of multiple request types in one
// frame." decode_acf_abb/decode_acf_gbb above only ever decode a single
// message occupying the *entire* remaining buffer — correct for the common
// one-request-per-AVTPDU case, but unable to find where a second, packed-in
// request begins. decode_acf_messages below walks a buffer that may contain
// more than one ACF_ABB/ACF_GBB message back to back, using each message's
// own acf_msg_length (now populated for real by encode_acf_abb/
// encode_acf_gbb, cpp-RCP-01 above) to find the next one.

// AcfEntry is one decoded ACF_ABB/ACF_GBB message out of a possibly-multi-
// message buffer — the same three outputs decode_acf_abb/decode_acf_gbb
// split into separate out-parameters, bundled here so decode_acf_messages
// can return a sequence of them.
struct AcfEntry {
    AcfMessageInfo         info;
    uint64_t               message_timestamp = 0; // meaningful only when info.acf_msg_type == kAcfMsgTypeGbb
    std::vector<uint8_t>   payload;
};

// decode_acf_messages decodes every well-formed ACF_ABB/ACF_GBB message in
// [b, b+len), advancing by each message's own acf_msg_length*4 bytes to
// find the next one. The first message is held to the same strict rules
// decode_acf_abb/decode_acf_gbb apply to a single-message buffer (a short
// buffer or an unrecognized acf_msg_type there is a hard error). Once at
// least one message has decoded successfully, this function treats a
// declared acf_msg_length that does not fit the bytes actually remaining as
// "trust the buffer, not the length" and consumes the rest of the buffer as
// that message's payload instead of failing outright — the same behavior
// decode_acf_abb/decode_acf_gbb have always had for a lone message — so a
// sender whose acf_msg_length is a caller-computed estimate rather than a
// byte-exact count (see compute_acf_msg_length's own documented scope
// limit: it does not itself pad a non-quadlet-aligned payload) still
// decodes correctly as long as it is the last (or only) message in the
// buffer. A message boundary can only be found precisely when the sender's
// acf_msg_length is byte-exact, which requires the payload to already be
// quadlet-aligned (as e.g. rcp/gpio.hpp's and rcp/pwm.hpp's fixed 4-byte
// payloads always are) — a non-final message with a non-aligned payload is
// a known residual gap of this pass, noted in this repository's pull
// request description.
inline std::error_code decode_acf_messages(const uint8_t* b, size_t len, std::vector<AcfEntry>& out) {
    out.clear();
    size_t off = 0;
    while (off < len) {
        const size_t remaining = len - off;
        const bool is_gbb = (peek_acf_msg_type(b + off) == kAcfMsgTypeGbb);
        const bool is_abb = (peek_acf_msg_type(b + off) == kAcfMsgTypeAbb);
        if (!is_gbb && !is_abb) {
            if (out.empty()) return make_error_code(AcfErrc::bad_acf_msg_type);
            break;
        }

        // For ACF_GBB the fixed part is the whole 16-byte Message Info
        // block (8-byte header + 8-byte message_timestamp, contiguous —
        // see the "ACF_GBB Message Info wire geometry" section above); for
        // ACF_ABB it is just the 8-byte header.
        const size_t fixed_len = is_gbb ? kAcfGbbMessageInfoLen : kAcfCommonHeaderLen;
        if (remaining < fixed_len) {
            if (out.empty()) return avtp::make_error_code(avtp::AvtpErrc::short_buffer);
            break;
        }

        AcfMessageInfo info;
        uint64_t       message_timestamp = 0;
        if (is_gbb) decode_acf_gbb_message_info(b + off, info, message_timestamp);
        else        decode_acf_message_info(b + off, info);

        size_t msg_bytes = static_cast<size_t>(info.acf_msg_length) * 4;
        if (msg_bytes < fixed_len || msg_bytes > remaining) {
            // Declared length doesn't fit this message's own fixed header,
            // or overclaims past the end of the buffer (see this function's
            // own comment above) — fall back to "consume the rest of the
            // buffer", same convention decode_acf_abb/decode_acf_gbb use.
            msg_bytes = remaining;
        }

        AcfEntry entry;
        entry.info = info;
        const size_t payload_off = off + fixed_len;
        const size_t payload_len = msg_bytes - fixed_len;
        entry.message_timestamp = message_timestamp; // 0 for ACF_ABB, which has no such field
        entry.payload.assign(b + payload_off, b + payload_off + payload_len);
        out.push_back(std::move(entry));

        off += msg_bytes;
    }
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

// ── Wire error codes in error responses (extraction Table 27; issue cpp-RCP-02) ──
// "The error response shall contain a byte_msg_payload with an error code"
// (extraction §12.9.6). WireErrorCode below fixes the 17 numeric values
// Table 27 assigns — the byte_msg_payload this codec builds for an
// ErrorResponse (acf::ResponseKind::ErrorResponse) is that one octet, per
// encode_error_payload(). Before this pass, nothing in this codebase built
// that payload at all: every ErrorResponse call site in rcp/mock.hpp left
// out_resp_payload empty, and the only existing numeric Table 27 mapping
// anywhere in the tree (rcp/e2e.hpp's kPociFailureErrorCode) was never
// actually wired into a response payload either. This enum is intentionally
// independent of any endpoint/subsystem's own internal std::error_code
// ordinals (e.g. rcp/regmap.hpp's RegMapErrc, whose own enumerator values
// 1..4 do NOT match Table 27's numbering for the same-named codes — compare
// RegMapErrc::unauthorized_access == 1 there against
// WireErrorCode::UnauthorizedAccess == 3 here) — callers translate their own
// internal error condition to a WireErrorCode explicitly (see
// rcp/mock.hpp's wire_error_code_for()) rather than this codec guessing a
// mapping from an arbitrary std::error_code.
enum class WireErrorCode : uint8_t {
    UnsupportedCmd          = 1,  // requested feature/command not supported
    SequencerNotKnown       = 2,
    UnauthorizedAccess      = 3,
    LockedMemAccess         = 4,
    RequestCanceled         = 5,
    RequestNotFound         = 6,
    EpError                 = 7,  // error occurred during request execution; see ep_status
    EpNotFound              = 8,  // a Trigger request refers to a nonexisting EP
    PwmInNoSignal           = 9,
    ReqStorageOverflow      = 10,
    RequestRejected          = 11, // other than STANDARD request during RC Server initial config phase
    PociFailure              = 12, // CRC of request does not match
    PresentationTimeTooFar   = 13,
    GptpFail                 = 14,
    InvalidParameter         = 15, // request parameter out of range
    ChainAborted             = 16,
    ChainError               = 17,
};

// encode_error_payload builds the single-octet byte_msg_payload an
// ErrorResponse carries the numeric Table 27 code in.
inline std::vector<uint8_t> encode_error_payload(WireErrorCode code) {
    return {static_cast<uint8_t>(code)};
}

// ── Response builders (ported from c-RCP's rcp_acf_build_error_response()/
// _build_acknowledge_response()/_build_acknowledge_rejected_response()) ──────
// These build a complete, ready-to-send ACF_ABB frame from just
// (byte_bus_id, transaction_num[, error code]) — the minimum a caller
// answering a request already knows — rather than requiring the caller to
// first assemble an AcfMessageInfo (make_response() above still exists for
// that lower-level use). All three encode as ACF_ABB (no timestamp),
// matching c-RCP's own ABB/GBB-split convention; a caller needing a
// timestamped variant builds its own ACF_GBB header with these same field
// values and calls encode_acf_gbb() directly.

// build_error_response builds a TC18 §12.9.6 Error Response: "The error
// response shall contain the byte_bus_id and transaction number of the
// request. The error response shall contain a byte_msg_payload with an
// error code." evt = 0 (any value other than kEvtAcknowledge classifies the
// same way once err is set — see response_kind_of()'s own logic above), err
// = true, rsp = true. op is set true (c-RCP's RCP_ACF_OP_NONE, which always
// wire-encodes as the write/no-data-response bit — see AcfMessageInfo::op's
// own doc comment) to match c-RCP's rcp_acf_build_error_response() bit for
// bit, even though op does not affect response_kind_of()'s classification
// once err is set.
inline std::vector<uint8_t> build_error_response(avtp::ByteBusId byte_bus_id,
                                                  uint8_t transaction_num,
                                                  WireErrorCode error_code) {
    AcfMessageInfo info;
    info.byte_bus_id     = byte_bus_id;
    info.transaction_num = transaction_num;
    info.op               = true;
    info.rsp               = true;
    info.err                = true;
    return encode_acf_abb(info, encode_error_payload(error_code));
}

// evt_requests_acknowledge: TC18 §13.5's own opening statement, before its
// per-endpoint-type evt[2:0] table: "evt[3] is used to request an
// acknowledge. I.e. evt[3]=1 requests acknowledge." Universal across every
// endpoint type (unlike evt[2:0], which is per-endpoint-type — see
// evt_row2_is_plain() below).
inline bool evt_requests_acknowledge(uint8_t evt) noexcept {
    return (evt & 0x08u) != 0u;
}

// build_acknowledge_response builds a genuine Acknowledge
// (ResponseKind::Acknowledge, evt[3:0] == kEvtAcknowledge) for a request
// whose own evt[3] asked for one (evt_requests_acknowledge()) and that was
// accepted into request storage — err = false, per TC18 §12.3.1.3 ("if
// requested an acknowledge is sent after storing the request").
inline std::vector<uint8_t> build_acknowledge_response(avtp::ByteBusId byte_bus_id,
                                                         uint8_t transaction_num) {
    AcfMessageInfo info;
    info.byte_bus_id     = byte_bus_id;
    info.transaction_num = transaction_num;
    info.evt_ack           = true;
    info.evt_op             = detail::kEvtOpMask; // together, evt[3:0] == 0xF == kEvtAcknowledge
    info.op                  = true; // c-RCP's RCP_ACF_OP_NONE — see build_error_response()'s doc comment
    info.rsp                  = true;
    return encode_acf_abb(info, {});
}

// build_acknowledge_rejected_response builds TC18 §11.3.1's OTHER
// Acknowledge shape, distinct from build_acknowledge_response() above: same
// evt[3:0] == kEvtAcknowledge, but for a request that was never filed into
// request storage at all — "err = 1 indicates that the request has been
// rejected. The byte_msg_payload contains an error code." This is NOT the
// same wire shape as build_error_response()'s §11.3.4 Error Response
// (evt[3:0] < 0x9, err = 1): that shape is for a request already accepted
// whose later execution fails; this one is for admission itself refusing to
// file the request (e.g. request-store full, a malformed opcode). Both
// shapes decode with err=1, but only this function's evt=0xF makes
// response_kind_of() classify the response as Acknowledge rather than
// ErrorResponse.
inline std::vector<uint8_t> build_acknowledge_rejected_response(avtp::ByteBusId byte_bus_id,
                                                                  uint8_t transaction_num,
                                                                  WireErrorCode error_code) {
    AcfMessageInfo info;
    info.byte_bus_id     = byte_bus_id;
    info.transaction_num = transaction_num;
    info.evt_ack           = true;
    info.evt_op             = detail::kEvtOpMask; // together, evt[3:0] == 0xF == kEvtAcknowledge
    info.op                  = true; // c-RCP's RCP_ACF_OP_NONE — see build_error_response()'s doc comment
    info.rsp                  = true;
    info.err                  = true;
    return encode_acf_abb(info, encode_error_payload(error_code));
}

// ── Request-side header validation (ported from c-RCP's
// rcp_acf_request_header_constraints_valid()/_header_is_request()) ───────────

// header_is_request: TC18's own rsp field description (Table 4) states
// rsp=1b identifies a response; a decoded message with rsp=1 must not be
// admitted as a request. A caller decoding an inbound frame it intends to
// treat as a request should call this before admission and refuse the
// frame if it returns false.
inline bool header_is_request(const AcfMessageInfo& hdr) noexcept {
    return !hdr.rsp;
}

// request_header_constraints_valid: true iff hdr's hs/rsp/err fields are
// the fixed value TC18 requires on an encoded REQUEST: hs=false, rsp=false,
// err=false unconditionally, and cs=false UNLESS cs_has_meaning is true —
// compound-wait (TC18 §11.2.2.3 Table 8) and chained (§11.2.2.6 Table 11)
// are the only two request kinds that assign cs a meaning of its own, so a
// caller building one of those two kinds passes true; every other request
// kind passes false. A pure, directly-testable validator, not an
// encode_acf_abb()/_gbb()-time enforcement — those two functions are shared
// by request AND response encoding (e.g. build_error_response() above
// deliberately sets rsp=err=true), so they cannot force these fields to
// their request-only values unconditionally.
inline bool request_header_constraints_valid(const AcfMessageInfo& hdr, bool cs_has_meaning) noexcept {
    if (hdr.hs) return false;
    if (hdr.rsp) return false;
    if (hdr.err) return false;
    if (!cs_has_meaning && hdr.cs) return false;
    return true;
}

// ── TC18 §13.5 Table 33's shared evt[2:0] rule for the {ADC, PWM_IN, I2C,
// LIN, CAN, UART, ISELED, MDIO} endpoint-type row ─────────────────────────────
// evt[2:0] = 000b is the only value a plain (non-configuration) request in
// this row may carry — every other value is either reserved (001b-110b,
// request shall be rejected with error code UNSUPPORTED_CMD) or selects an
// entirely different, configuration-write-shaped request (111b, TC18
// §12.7.1 Figure 18) that a plain read/write decoder should never accept.
// Not meaningful for SPI or GPIO/PWM_OUT, which have their own dedicated
// Table 33 rows with their own distinct rules.
inline bool evt_row2_is_plain(uint8_t evt) noexcept {
    return (evt & 0x7u) == 0u;
}

// ── TC18 §13.5.1: compound-wait's own, endpoint-type-independent evt[2:0]
// rule ─────────────────────────────────────────────────────────────────────
// compound-wait gives evt[2:0] an entirely different meaning than Table 33
// gives it for a Standard request: it selects one of eight ways to compare
// that request's own byte_msg_payload against the addressed endpoint's
// current status, and this rule is the SAME across every endpoint type —
// unlike Table 33, there is no per-endpoint-type row.

namespace detail {
constexpr uint8_t kCompoundWaitModeExact    = 0x0u;
constexpr uint8_t kCompoundWaitModeAndOnes  = 0x1u;
constexpr uint8_t kCompoundWaitModeAndZeros = 0x2u;
constexpr uint8_t kCompoundWaitModeReserved = 0x3u;
constexpr uint8_t kCompoundWaitModeHiGe     = 0x4u;
constexpr uint8_t kCompoundWaitModeHiLe     = 0x5u;
constexpr uint8_t kCompoundWaitModeLoGe     = 0x6u;
constexpr uint8_t kCompoundWaitModeLoLe     = 0x7u;

inline uint16_t be16(const uint8_t* p) noexcept {
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}
} // namespace detail

// compound_wait_evt_valid: true iff (evt & 0x7) != 0x3 — every value except
// the reserved 011b, which callers must reject with error code
// UNSUPPORTED_CMD rather than passing to compound_wait_match() below (that
// function's return value for a reserved evt is not a meaningful
// "never matches" result — see its own doc comment).
inline bool compound_wait_evt_valid(uint8_t evt) noexcept {
    return (evt & 0x7u) != detail::kCompoundWaitModeReserved;
}

// compound_wait_match evaluates whether payload[0..payload_len) matches
// status[0..status_len) under the comparison mode evt[2:0] selects. Callers
// must call compound_wait_evt_valid(evt) first and reject a false result
// (UNSUPPORTED_CMD) rather than calling this function — its own return
// value for evt[2:0] == 011b is unconditionally false, not a meaningful
// "reserved" signal distinct from a real non-match.
//
// Length rule (applies before any mode-specific comparison, per the
// specification's own wording and its own SPI example — "only the first
// four out of 20 received bytes will be checked when the byte_msg_payload
// in the compound wait has only four bytes"): if status_len < payload_len
// the condition never matches (false, regardless of mode and buffer
// contents); otherwise status is compared only against its own first
// payload_len bytes.
//
// Modes (evt[2:0]):
//   000b exact match:        payload[0..n) == status[0..n), byte for byte.
//   001b AND-with-1s-mask:   for every byte i, (payload[i] & status[i]) |
//                            ~payload[i] == 0xFF — every payload bit that
//                            is 1 must also be 1 in status.
//   010b AND-with-0s-mask:   for every byte i, payload[i] & status[i] ==
//                            0x00 — every payload bit that is 1 must be 0
//                            in status.
//   100b/101b: the first two bytes of payload's own leading quadlet, read
//              big-endian, are >= (100b) or <= (101b) the same two bytes of
//              status. Returns false (never reads OOB) if payload_len < 4.
//   110b/111b: same as 100b/101b, but the LAST two bytes of the leading
//              quadlet (payload[2..4)), same payload_len < 4 fail-safe.
//
// status/payload may be nullptr iff their respective length is 0.
inline bool compound_wait_match(uint8_t evt, const uint8_t* payload, size_t payload_len,
                                 const uint8_t* status, size_t status_len) noexcept {
    const uint8_t mode = static_cast<uint8_t>(evt & 0x7u);

    if (status_len < payload_len) return false;

    switch (mode) {
    case detail::kCompoundWaitModeExact:
        if (payload_len == 0u) return true;
        return std::equal(payload, payload + payload_len, status);

    case detail::kCompoundWaitModeAndOnes:
        for (size_t i = 0; i < payload_len; ++i) {
            const uint8_t v = static_cast<uint8_t>((payload[i] & status[i]) | static_cast<uint8_t>(~payload[i]));
            if (v != 0xFFu) return false;
        }
        return true;

    case detail::kCompoundWaitModeAndZeros:
        for (size_t i = 0; i < payload_len; ++i) {
            if (static_cast<uint8_t>(payload[i] & status[i]) != 0x00u) return false;
        }
        return true;

    case detail::kCompoundWaitModeHiGe:
    case detail::kCompoundWaitModeHiLe:
        if (payload_len < 4u) return false;
        return (mode == detail::kCompoundWaitModeHiGe) ? (detail::be16(payload) >= detail::be16(status))
                                                         : (detail::be16(payload) <= detail::be16(status));

    case detail::kCompoundWaitModeLoGe:
    case detail::kCompoundWaitModeLoLe:
        if (payload_len < 4u) return false;
        return (mode == detail::kCompoundWaitModeLoGe) ? (detail::be16(&payload[2]) >= detail::be16(&status[2]))
                                                         : (detail::be16(&payload[2]) <= detail::be16(&status[2]));

    case detail::kCompoundWaitModeReserved:
    default:
        return false;
    }
}

// ── REQ-RMAP-069 (TC18 §13.7.1.2): EP0 register-write effective length ──────
// "Effective number of bytes to be written to register map = (acf_msg_length
// - 3) x 4 - pad - 2." acf_msg_length/pad are the decoded header fields of
// the same name; this function does no decoding of its own. FIXED per c-RCP
// (spec rebaseline to TC18 0.5.1_RC5, 2026-08-11): the 0.5.1_RC baseline's
// formula omitted the trailing "- 2" term (the 2-octet register start
// address that leads the byte payload); RC5 corrects it. Returns 0, never
// underflowing to a huge size_t, if acf_msg_length is too small to contain
// the fixed 3-quadlet region at all (< 3), or if pad plus the 2-octet
// address exceeds what remains after subtracting it — both describe a
// malformed or adversarial frame, and 0 effective data octets is this
// function's own fail-safe reading of that, not an out-of-band error code.
inline size_t reg_write_len(uint16_t acf_msg_length, uint8_t pad) noexcept {
    if (acf_msg_length < 3u) return 0;
    const size_t total_octets = static_cast<size_t>(acf_msg_length - 3u) * 4u;
    const size_t overhead     = static_cast<size_t>(pad) + 2u;
    if (overhead > total_octets) return 0;
    return total_octets - overhead;
}

// ── Peeking a GBB frame's own request_type without a full kind-specific
// decode ─────────────────────────────────────────────────────────────────────
// Every conditional-request module (compound/triggered/chained/timed)
// places its own request_type opcode at the SAME fixed offset: octet 0 of
// the 8-byte message_timestamp region (frame offset kAcfCommonHeaderLen,
// i.e. 8 — see the "ACF_GBB Message Info wire geometry" section above),
// repurposed identically by every one of those modules. Returns true and
// sets `out_request_type` to frame[8] iff frame_len >= 9 and the header's
// own acf_msg_type is kAcfMsgTypeGbb; returns false (`out_request_type`
// left unchanged) for an ACF_ABB frame (no request_type concept exists on
// that wire shape) or a frame too short to hold byte_message_info(8) +
// request_type(1). Does NOT itself validate that the returned byte is one
// of the currently-defined request_type values.
inline bool peek_gbb_request_type(const uint8_t* frame, size_t frame_len,
                                   uint8_t& out_request_type) noexcept {
    if (frame_len < 9u) return false;
    AcfMessageInfo hdr;
    decode_acf_message_info(frame, hdr);
    if (hdr.acf_msg_type != kAcfMsgTypeGbb) return false;
    out_request_type = frame[kAcfCommonHeaderLen];
    return true;
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
