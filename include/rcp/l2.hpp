// fusa:req REQ-L2-001
// fusa:req REQ-L2-002
// fusa:req REQ-L2-003
// fusa:req REQ-L2-004
// fusa:req REQ-L2-005
// fusa:req REQ-L2-006
// fusa:req REQ-L2-007
// fusa:req REQ-L2-008
// fusa:req REQ-L2-009
// fusa:req REQ-L2-010

// Native IEEE 1722-over-Ethernet (raw L2) transport for the OPEN Alliance
// TC18 Remote Control Protocol Specification v0.5.1_RC — carries real
// AVTPDU (NTSCF/TSCF, rcp/avtp.hpp) frames wrapping ACF_ABB/ACF_GBB messages
// (rcp/acf.hpp) directly at layer 2, under EtherType 0x22F0, the framing
// rcp/udp.hpp's own header comment names as the alternative to IEEE 1722's
// Annex J UDP/IP encapsulation (which rcp/udp.hpp implements). This header
// is the native-Ethernet transport that comment named as deliberately
// deferred; it now exists alongside rcp/udp.hpp as a permanent, equally
// supported option — neither transport is preferred over or deprecates the
// other, and callers pick whichever fits their link.
//
// Wire frame: destination MAC (6 bytes) + source MAC (6 bytes) + EtherType
// 0x22F0 (2 bytes, big-endian) + the AVTPDU bytes directly. Unlike
// rcp/udp.hpp's Annex J UDP payload, there is NO 4-byte encapsulation
// sequence number here — that field is specific to the UDP/IP encapsulation
// case (see rcp/udp.hpp's own header comment for the two independent public
// secondary sources — a Wireshark issue tracker discussion of the real
// Annex J text, and the COVESA Open1722 open-source reference
// implementation, github.com/COVESA/Open1722 — this codebase's Annex J
// reading rests on, since this codebase has no access to the paywalled IEEE
// 1722-2016 standard text itself). Native Ethernet framing carries the
// AVTPDU unmodified, which is a more directly verifiable claim than the
// Annex J UDP encapsulation shape, since it follows straight from IEEE
// 1722's own "EtherType 0x22F0" identification quoted in TC18 §10.1 rather
// than from a secondary source's reading of Annex J specifically. Full
// bit-for-bit conformance against other TC18 implementations is still not
// claimed, same as the equivalent disclaimers in rcp/avtp.hpp, rcp/acf.hpp,
// rcp/udp.hpp, and rcp/discovery.hpp.
//
// Platform scope — deliberately narrower than rcp/udp.hpp's POSIX-wide
// support: raw Ethernet framing needs a socket API that gives this module
// direct control over the L2 header, which on Linux is AF_PACKET/SOCK_RAW.
// macOS and other BSDs expose a materially different raw-capture API
// (BPF, /dev/bpf*) this module does not implement; Windows has no POSIX
// raw-socket equivalent at all. So Server/Client below are Linux-only
// (guarded by `#ifdef __linux__`, matching how rcp/udp.hpp itself guards
// its POSIX-vs-Windows-stub split at the top of that file) — every other
// platform gets the same "compiles cleanly, every operation returns
// std::errc::function_not_supported" stub rcp/udp.hpp's own Windows branch
// already establishes as this codebase's precedent, followed directly here.
// Opening the raw socket needs CAP_NET_RAW (or root) — every real Server/
// Client construction below will otherwise silently fail (ok() == false),
// the same "construct, then check ok()" contract rcp/udp.hpp's Server/
// Client already use for their own bind()/connect() failure path.
//
// One deliberate structural difference from rcp/udp.hpp: rcp/udp.hpp's
// Frame/MultiFrame codec (encode_frame/decode_frame/encode_multi_frame/
// decode_multi_frame) is compiled only under its POSIX guard, with a
// separate do-nothing stub redefined for the Windows branch, because that
// header treats "codec" and "socket transport" as one unit. This header
// keeps its own Frame/MultiFrame codec and Ethernet-header codec
// (encode_eth_header/decode_eth_header, encode_l2_frame/decode_l2_frame,
// encode_l2_multi_frame/decode_l2_multi_frame) available on every platform,
// unconditionally — none of that code touches a socket, so there is no
// platform reason to hide it, and doing so lets the pure encode/decode
// logic be exercised by tests/test_l2.cpp on every CI platform (including
// macOS and Windows) with no privileges and no Linux requirement, while
// only the raw-socket Server/Client classes below are actually gated to
// Linux.
//
// Symmetric division of responsibility with rcp/udp.hpp::Client, which
// takes a caller-supplied host/port rather than deriving one: Client below
// takes a caller-supplied destination MAC address (unicast or multicast).
// It does not derive or allocate a multicast MAC itself — that mapping (if
// IEEE 1722 defines one for AVTP streams) lives in the base standard, which
// this codebase does not have verified access to; the caller decides.
#pragma once

#include "acf.hpp"
#include "avtp.hpp"
#include "rcp.hpp" // for rcp::Context/ErrClosed/ErrTimeout only — see rcp/udp.hpp's equivalent scope note

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined(__linux__)
#  include <arpa/inet.h>
#  include <linux/if_ether.h>
#  include <linux/if_packet.h>
#  include <net/if.h>
#  include <sys/ioctl.h>
#  include <sys/socket.h>
#  include <sys/time.h>
#  include <unistd.h>
#  define RCP_L2_LINUX 1
#endif

namespace rcp {
namespace l2 {

// ── Wire constants ───────────────────────────────────────────────────────────

using MacAddress = std::array<uint8_t, 6>;

constexpr size_t   kMacLen       = 6;
constexpr uint16_t kEtherType    = 0x22F0; // TC18 §10.1's own cited EtherType
constexpr size_t   kEthHeaderLen = 2 * kMacLen + 2; // dst(6) + src(6) + ethertype(2)

// kMaxFrame: receive-buffer ceiling for one raw Ethernet frame. Standard
// Ethernet MTU is 1500 bytes of payload (1514 total with this module's
// 14-byte L2 header); this also covers common jumbo-frame configurations
// (up to a 9000-byte payload) so a receive buffer sized to it does not
// truncate on links that enable them, the same conservative-sizing spirit
// as rcp/udp.hpp's kMaxDatagram.
constexpr size_t kMaxFrame = 9018;

// ── Errors ────────────────────────────────────────────────────────────────────
// short_buffer is reused from rcp/avtp.hpp (its own header comment already
// documents it as shared across modules) for "fewer bytes than required"
// conditions; bad_ethertype is specific to this module — rcp/avtp.hpp's own
// bad_subtype is documented as meaning "AVTPDU subtype byte is neither
// NTSCF nor TSCF," a different field than this header's EtherType check, so
// it is not reused for that case here.

enum class L2Errc : int {
    bad_ethertype = 1, // received frame's EtherType is not kEtherType (0x22F0)
};

inline const std::error_category& l2_category() noexcept {
    struct Cat : std::error_category {
        const char* name() const noexcept override { return "rcp.l2"; }
        std::string message(int ev) const override {
            switch (static_cast<L2Errc>(ev)) {
            case L2Errc::bad_ethertype: return "rcp/l2: EtherType is not 0x22F0";
            default:                    return "rcp/l2: unknown error";
            }
        }
    };
    static Cat instance;
    return instance;
}

inline std::error_code make_error_code(L2Errc e) noexcept {
    return {static_cast<int>(e), l2_category()};
}

// ── Ethernet header codec (pure, no socket, every platform) ─────────────────

struct EthHeader {
    MacAddress dst{};
    MacAddress src{};
    uint16_t   ethertype = 0;
};

inline std::vector<uint8_t> encode_eth_header(const MacAddress& dst, const MacAddress& src) {
    std::vector<uint8_t> out(kEthHeaderLen);
    std::copy(dst.begin(), dst.end(), out.begin());
    std::copy(src.begin(), src.end(), out.begin() + static_cast<long>(kMacLen));
    avtp::detail::put_u16(&out[2 * kMacLen], kEtherType);
    return out;
}

inline std::error_code decode_eth_header(const uint8_t* b, size_t len, EthHeader& out) {
    if (len < kEthHeaderLen) return avtp::make_error_code(avtp::AvtpErrc::short_buffer);
    std::copy(b, b + kMacLen, out.dst.begin());
    std::copy(b + kMacLen, b + 2 * kMacLen, out.src.begin());
    out.ethertype = avtp::detail::get_u16(b + 2 * kMacLen);
    return {};
}

// is_unicast_mac — content gap found during this batch's own c-RCP delta
// verification pass: ported from c-RCP's rcp_l2_mac_is_unicast()
// (c-RCP/src/l2.c:79-82, REQ-L2-011), which this header had no equivalent
// of at all before this pass. True iff mac is a unicast address: the I/G
// (individual/group) bit — the least-significant bit of the first octet —
// is 0. False for any multicast address, including the all-ones broadcast
// address (ff:ff:ff:ff:ff:ff), itself a special case of multicast under
// this same bit test — standard IEEE 802.3 addressing, not TC18-specific.
// rcp::lifecycle::WriterCtx::via_non_unicast_frame (rcp/lifecycle.hpp,
// REQ-LIFECYCLE-027) is the intended consumer, matching c-RCP's own header
// comment for rcp_l2_mac_is_unicast() exactly ("the primitive an
// integrator uses to classify a frame's destination MAC... before
// constructing that writer context") — TC18 §12.3.1.1/.2/.3 requires a
// write request be accepted only when its frame's destination MAC is
// unicast. Nothing in this rewrite's dispatch path (rcp/mock.hpp,
// rcp/regmap.hpp::writer_ctx()) actually derives via_unicast from a live
// destination MAC yet — regmap::writer_ctx() takes it as an
// already-classified caller-supplied bool, and no production call site
// calls writer_ctx() at all today (confirmed by repo-wide search) — so
// wiring THIS primitive into that gate is a separate, broader gap than
// this batch's own dispatch-wiring scope (Server/dispatch, not transport);
// this function exists so that future wiring has the primitive ready,
// exactly as c-RCP already does.
inline bool is_unicast_mac(const MacAddress& mac) noexcept {
    return (mac[0] & 0x01u) == 0u;
}

// ── Frame ─────────────────────────────────────────────────────────────────────
// Same AVTPDU shape as rcp/udp.hpp::Frame (one NTSCF/TSCF header wrapping
// one ACF_ABB/ACF_GBB message) — deliberately duplicated here rather than
// reused from rcp/udp.hpp, matching this codebase's documented preference
// (RELAY-side guidance for this change) for parallel concrete types over a
// shared Transport interface: this repository has no such interface today,
// and one is not introduced by this header either. encode_frame/decode_frame
// are pure functions — no socket I/O — exactly like rcp/udp.hpp's.
struct Frame {
    bool            use_tscf        = false; // false = NTSCF, true = TSCF
    avtp::StreamId  stream_id{};
    uint16_t        sequence_num    = 0;
    bool            timestamp_valid = false; // TSCF "tv" bit; ignored under NTSCF
    uint32_t        avtp_timestamp  = 0;     // TSCF-only; ignored under NTSCF

    acf::AcfMessageInfo   info{};
    uint64_t              message_timestamp = 0; // honored only when info.acf_msg_type == kAcfMsgTypeGbb
    std::vector<uint8_t>  payload;
};

inline std::vector<uint8_t> encode_frame(const Frame& f) {
    std::vector<uint8_t> acf_msg = (f.info.acf_msg_type == acf::kAcfMsgTypeGbb)
        ? acf::encode_acf_gbb(f.info, f.message_timestamp, f.payload)
        : acf::encode_acf_abb(f.info, f.payload);

    std::vector<uint8_t> out;
    if (f.use_tscf) {
        avtp::TscfHeader hdr;
        hdr.stream_id           = f.stream_id;
        // avtp::TscfHeader::sequence_num is the real 8-bit wire field
        // (avtp.hpp's own header comment: "sequence_num -- 8 bits, not
        // 16"); Frame::sequence_num above is 16 bits wide to match
        // rcp/udp.hpp::Frame's identical field, so this narrowing is
        // intentional -- explicit cast needed for MSVC's /W4 (matches this
        // codebase's existing narrowing-cast convention elsewhere).
        hdr.sequence_num        = static_cast<uint8_t>(f.sequence_num);
        hdr.control_data_length = static_cast<uint16_t>(acf_msg.size());
        hdr.timestamp_valid     = f.timestamp_valid;
        hdr.avtp_timestamp      = f.avtp_timestamp;
        out = avtp::encode_tscf_header(hdr);
    } else {
        avtp::NtscfHeader hdr;
        hdr.stream_id           = f.stream_id;
        hdr.sequence_num        = static_cast<uint8_t>(f.sequence_num);
        hdr.control_data_length = static_cast<uint16_t>(acf_msg.size());
        out = avtp::encode_ntscf_header(hdr);
    }
    out.insert(out.end(), acf_msg.begin(), acf_msg.end());
    return out;
}

inline std::error_code decode_frame(const uint8_t* b, size_t len, Frame& out) {
    if (len < 1) return avtp::make_error_code(avtp::AvtpErrc::short_buffer);

    size_t acf_off;
    out.use_tscf = (b[0] == avtp::kSubtypeTscf);
    if (out.use_tscf) {
        avtp::TscfHeader hdr;
        if (auto ec = avtp::decode_tscf_header(b, len, hdr)) return ec;
        out.stream_id       = hdr.stream_id;
        out.sequence_num    = hdr.sequence_num;
        out.timestamp_valid = hdr.timestamp_valid;
        out.avtp_timestamp  = hdr.avtp_timestamp;
        acf_off = avtp::kTscfHeaderLen;
        if (static_cast<size_t>(hdr.control_data_length) != len - acf_off)
            return avtp::make_error_code(avtp::AvtpErrc::length_mismatch);
    } else {
        avtp::NtscfHeader hdr;
        if (auto ec = avtp::decode_ntscf_header(b, len, hdr)) return ec;
        out.stream_id       = hdr.stream_id;
        out.sequence_num    = hdr.sequence_num;
        out.timestamp_valid = false;
        out.avtp_timestamp  = 0;
        acf_off = avtp::kNtscfHeaderLen;
        if (static_cast<size_t>(hdr.control_data_length) != len - acf_off)
            return avtp::make_error_code(avtp::AvtpErrc::length_mismatch);
    }

    if (len < acf_off + 1) return avtp::make_error_code(avtp::AvtpErrc::short_buffer);
    if (acf::peek_acf_msg_type(b + acf_off) == acf::kAcfMsgTypeGbb) {
        return acf::decode_acf_gbb(b + acf_off, len - acf_off, out.info,
                                    out.message_timestamp, out.payload);
    }
    out.message_timestamp = 0;
    return acf::decode_acf_abb(b + acf_off, len - acf_off, out.info, out.payload);
}

// ── MultiFrame — multiple ACF requests in one AVTPDU (extraction §12.9.1.1) ──
// Same role as rcp/udp.hpp::MultiFrame: one-or-more acf::AcfEntry messages
// under a single NTSCF/TSCF header, so Server below can answer every
// request a single Ethernet frame carries, per-request, in one reply frame.
struct MultiFrame {
    bool            use_tscf        = false; // false = NTSCF, true = TSCF
    avtp::StreamId  stream_id{};
    uint16_t        sequence_num    = 0;
    bool            timestamp_valid = false; // TSCF "tv" bit; ignored under NTSCF
    uint32_t        avtp_timestamp  = 0;     // TSCF-only; ignored under NTSCF

    std::vector<acf::AcfEntry> messages; // one or more ACF_ABB/ACF_GBB messages, in wire order
};

inline std::vector<uint8_t> encode_multi_frame(const MultiFrame& f) {
    std::vector<uint8_t> acf_bytes;
    for (const auto& m : f.messages) {
        auto enc = (m.info.acf_msg_type == acf::kAcfMsgTypeGbb)
            ? acf::encode_acf_gbb(m.info, m.message_timestamp, m.payload)
            : acf::encode_acf_abb(m.info, m.payload);
        acf_bytes.insert(acf_bytes.end(), enc.begin(), enc.end());
    }

    std::vector<uint8_t> out;
    if (f.use_tscf) {
        avtp::TscfHeader hdr;
        hdr.stream_id           = f.stream_id;
        // See encode_frame's identical cast above for why this narrowing
        // is intentional and needs to be explicit for MSVC's /W4.
        hdr.sequence_num        = static_cast<uint8_t>(f.sequence_num);
        hdr.control_data_length = static_cast<uint16_t>(acf_bytes.size());
        hdr.timestamp_valid     = f.timestamp_valid;
        hdr.avtp_timestamp      = f.avtp_timestamp;
        out = avtp::encode_tscf_header(hdr);
    } else {
        avtp::NtscfHeader hdr;
        hdr.stream_id           = f.stream_id;
        hdr.sequence_num        = static_cast<uint8_t>(f.sequence_num);
        hdr.control_data_length = static_cast<uint16_t>(acf_bytes.size());
        out = avtp::encode_ntscf_header(hdr);
    }
    out.insert(out.end(), acf_bytes.begin(), acf_bytes.end());
    return out;
}

inline std::error_code decode_multi_frame(const uint8_t* b, size_t len, MultiFrame& out) {
    if (len < 1) return avtp::make_error_code(avtp::AvtpErrc::short_buffer);

    size_t acf_off;
    out.use_tscf = (b[0] == avtp::kSubtypeTscf);
    if (out.use_tscf) {
        avtp::TscfHeader hdr;
        if (auto ec = avtp::decode_tscf_header(b, len, hdr)) return ec;
        out.stream_id       = hdr.stream_id;
        out.sequence_num    = hdr.sequence_num;
        out.timestamp_valid = hdr.timestamp_valid;
        out.avtp_timestamp  = hdr.avtp_timestamp;
        acf_off = avtp::kTscfHeaderLen;
        if (static_cast<size_t>(hdr.control_data_length) != len - acf_off)
            return avtp::make_error_code(avtp::AvtpErrc::length_mismatch);
    } else {
        avtp::NtscfHeader hdr;
        if (auto ec = avtp::decode_ntscf_header(b, len, hdr)) return ec;
        out.stream_id       = hdr.stream_id;
        out.sequence_num    = hdr.sequence_num;
        out.timestamp_valid = false;
        out.avtp_timestamp  = 0;
        acf_off = avtp::kNtscfHeaderLen;
        if (static_cast<size_t>(hdr.control_data_length) != len - acf_off)
            return avtp::make_error_code(avtp::AvtpErrc::length_mismatch);
    }

    if (len < acf_off + 1) return avtp::make_error_code(avtp::AvtpErrc::short_buffer);
    return acf::decode_acf_messages(b + acf_off, len - acf_off, out.messages);
}

// ── Full L2 wire frame (Ethernet header + AVTPDU), pure, every platform ─────

inline std::vector<uint8_t> encode_l2_frame(const MacAddress& dst, const MacAddress& src,
                                             const Frame& f) {
    auto out  = encode_eth_header(dst, src);
    auto body = encode_frame(f);
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

inline std::error_code decode_l2_frame(const uint8_t* b, size_t len,
                                        EthHeader& out_hdr, Frame& out_frame) {
    if (auto ec = decode_eth_header(b, len, out_hdr)) return ec;
    if (out_hdr.ethertype != kEtherType) return make_error_code(L2Errc::bad_ethertype);
    return decode_frame(b + kEthHeaderLen, len - kEthHeaderLen, out_frame);
}

inline std::vector<uint8_t> encode_l2_multi_frame(const MacAddress& dst, const MacAddress& src,
                                                   const MultiFrame& f) {
    auto out  = encode_eth_header(dst, src);
    auto body = encode_multi_frame(f);
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

inline std::error_code decode_l2_multi_frame(const uint8_t* b, size_t len,
                                              EthHeader& out_hdr, MultiFrame& out_frame) {
    if (auto ec = decode_eth_header(b, len, out_hdr)) return ec;
    if (out_hdr.ethertype != kEtherType) return make_error_code(L2Errc::bad_ethertype);
    return decode_multi_frame(b + kEthHeaderLen, len - kEthHeaderLen, out_frame);
}

// ── AVTP envelope-only decode (pure, no socket, every platform) ─────────────
// AvtpFrameHeader/decode_avtp_frame_header/decode_l2_frame_header decode just
// the NTSCF/TSCF envelope of one inbound frame — use_tscf, stream_id,
// sequence_num, the TSCF timestamp fields, and where the ACF payload region
// begins — WITHOUT parsing that payload into individual acf::AcfEntry
// messages the way decode_multi_frame()/decode_l2_multi_frame() above do.
// Server::serve()'s FrameHandler path (below) needs exactly this: raw,
// unparsed ACF bytes to hand to a caller like rcp::mock::Server::
// dispatch_frame()/dispatch_frame_e2e(), which do their own member-splitting
// (mock.hpp's own split_frame_members()), conditional/cancellation-opcode
// peeking (mock.hpp's own peek_conditional_request_type()), and E2E CRC
// verification directly off the wire bytes — CRC coverage in particular is
// computed over the raw header octets themselves (see mock.hpp's own
// dispatch_e2e() doc comment), so a decode-then-re-encode round trip through
// acf::AcfEntry is not just unnecessary here but the wrong shape of input for
// that caller.
struct AvtpFrameHeader {
    bool            use_tscf        = false; // false = NTSCF, true = TSCF
    avtp::StreamId  stream_id{};
    uint16_t        sequence_num    = 0;
    bool            timestamp_valid = false; // TSCF "tv" bit; false under NTSCF
    uint32_t        avtp_timestamp  = 0;     // TSCF-only; 0 under NTSCF
    size_t          acf_offset      = 0;     // offset of the raw ACF payload region within the buffer this was decoded from
};

inline std::error_code decode_avtp_frame_header(const uint8_t* b, size_t len, AvtpFrameHeader& out) {
    if (len < 1) return avtp::make_error_code(avtp::AvtpErrc::short_buffer);

    out.use_tscf = (b[0] == avtp::kSubtypeTscf);
    if (out.use_tscf) {
        avtp::TscfHeader hdr;
        if (auto ec = avtp::decode_tscf_header(b, len, hdr)) return ec;
        out.stream_id       = hdr.stream_id;
        out.sequence_num    = hdr.sequence_num;
        out.timestamp_valid = hdr.timestamp_valid;
        out.avtp_timestamp  = hdr.avtp_timestamp;
        out.acf_offset      = avtp::kTscfHeaderLen;
        if (static_cast<size_t>(hdr.control_data_length) != len - out.acf_offset)
            return avtp::make_error_code(avtp::AvtpErrc::length_mismatch);
    } else {
        avtp::NtscfHeader hdr;
        if (auto ec = avtp::decode_ntscf_header(b, len, hdr)) return ec;
        out.stream_id       = hdr.stream_id;
        out.sequence_num    = hdr.sequence_num;
        out.timestamp_valid = false;
        out.avtp_timestamp  = 0;
        out.acf_offset      = avtp::kNtscfHeaderLen;
        if (static_cast<size_t>(hdr.control_data_length) != len - out.acf_offset)
            return avtp::make_error_code(avtp::AvtpErrc::length_mismatch);
    }
    return {};
}

inline std::error_code decode_l2_frame_header(const uint8_t* b, size_t len,
                                               EthHeader& out_hdr, AvtpFrameHeader& out_avtp) {
    if (auto ec = decode_eth_header(b, len, out_hdr)) return ec;
    if (out_hdr.ethertype != kEtherType) return make_error_code(L2Errc::bad_ethertype);
    return decode_avtp_frame_header(b + kEthHeaderLen, len - kEthHeaderLen, out_avtp);
}

// ── FrameMemberResult / FrameHandler — frame-level dispatch wiring ──────────
// The dispatch-wiring gap this pair closes: rcp::l2::Server::Handler below
// (unchanged) dispatches ONE already-decoded ACF message at a time — the
// same shape rcp::mock::Server::dispatch() takes — which is the obvious,
// natural thing to wire a caller's own request handler to, but silently
// bypasses every one of rcp::mock::Server's frame-level behaviors: TC18
// Table 24 response/ack routing suppression (mock::Server::
// suppress_response_per_stream_cfg(), applied inside every one of that
// class's ten dispatch_<type>() wrappers, never inside its single-message
// dispatch() itself), conditional/cancellation-opcode routing
// (mock::Server::peek_conditional_request_type(), reachable only through
// mock::Server::decode_and_dispatch()/dispatch_frame()/dispatch_frame_e2e(),
// never through dispatch()), and E2E/fragment-aware dispatch
// (mock::Server::dispatch_frame_e2e(), which independently CRC-verifies and
// reassembles each frame member). A caller that wires Handler straight to
// mock::Server::dispatch() gets every operational request answered, but with
// every one of those three behaviors silently downgraded.
//
// FrameHandler/set_frame_handler() below is the correct wiring path: it
// receives the RAW, unparsed ACF payload of one whole inbound frame (every
// member concatenated, exactly as split_frame_members() expects) plus the
// frame's own stream_id and sequence_num, and returns one FrameMemberResult
// per member — the same "no wire response was built for this member" (
// response.rsp == false) contract mock::Server::FrameMemberResult's own doc
// comment establishes for the four cases that produce one: Table 24
// suppression, an admission outcome with no evt[3] ack
// (Queued/Pending/Cancellation/Suspended), and (dispatch_frame_e2e() only) a
// frame-level sequence-gate rejection. Server::serve() (below) drops every
// such member from the outgoing response rather than encoding an empty
// response for it, and sends nothing at all if every member in the frame was
// dropped this way — matching the "0 means send nothing" half of Table 24
// (REQ-RMAP-048/049) that mock::Server itself has no transport to actually
// enforce (see suppress_response_per_stream_cfg()'s own doc comment).
//
// FrameMemberResult is a deliberate PARALLEL type, not a shared one:
// mock::Server::FrameMemberResult (rcp/mock.hpp) has the identical field
// shape (result/byte_bus_id/response/response_payload) but this header does
// not include rcp/mock.hpp or depend on its type — matching this file's own
// documented preference (see Frame's own header comment above) for parallel
// concrete types over a shared interface. A caller wiring
// mock::Server::dispatch_frame()/dispatch_frame_e2e() as a FrameHandler
// copies each mock::Server::FrameMemberResult into one of these, field for
// field — e.g.:
//
//   l2_server.set_frame_handler([&](size_t client, avtp::StreamId sid,
//                                    uint8_t seq, const std::vector<uint8_t>& acf,
//                                    std::vector<l2::FrameMemberResult>& out) {
//       std::vector<mock::FrameMemberResult> mres;
//       size_t n = mock_server.dispatch_frame_e2e(client, sid, seq, acf, mres);
//       out.reserve(mres.size());
//       for (auto& m : mres)
//           out.push_back({m.result, m.byte_bus_id, m.response, std::move(m.response_payload)});
//       return n;
//   });
//
// message_timestamp is deliberately not carried here, matching
// mock::Server::FrameMemberResult's own identical omission — a GBB-typed
// response (make_response() echoes the request's own acf_msg_type,
// rcp/acf.hpp) loses its message_timestamp through this path exactly as it
// already does through Server::Handler's own pre-existing single-message
// signature below, which has never carried one either; not a regression
// this pair introduces.
struct FrameMemberResult {
    std::error_code       result;
    avtp::ByteBusId        byte_bus_id = 0; // 0 when result could not even be determined
    acf::AcfMessageInfo    response;         // default-constructed (rsp == false) unless a genuine wire response was built
    std::vector<uint8_t>   response_payload;
};

// FrameHandler — see the doc comment above this struct for the full
// contract. Takes the raw ACF payload of one whole inbound frame (`acf`,
// every member concatenated, unparsed) plus that frame's own `stream_id`/
// `sequence_num`, fills `out_results` with one FrameMemberResult per member
// it dispatched, and returns how many that was (mirroring
// mock::Server::dispatch_frame()/dispatch_frame_e2e()'s own `size_t`
// return). Declared at namespace scope (not nested in Server) so it is
// visible identically on every platform, matching this header's own
// Handler-is-visible-everywhere-even-on-the-stub convention below.
using FrameHandler = std::function<size_t(size_t client, avtp::StreamId stream_id,
                                           uint8_t sequence_num,
                                           const std::vector<uint8_t>& acf,
                                           std::vector<FrameMemberResult>& out_results)>;

#if defined(RCP_L2_LINUX)

// kRecvTimeoutMillis: SO_RCVTIMEO applied to every real Server/Client socket
// below (detail::set_recv_timeout) — see that function's own comment for
// why AF_PACKET needs this where rcp/udp.hpp's INET sockets don't. Bounds
// how long close()/~Server()/~Client() can block waiting for their
// serve()/read_loop() thread to notice and exit.
constexpr int kRecvTimeoutMillis = 200;

namespace detail {

// mac_to_hex renders a MacAddress as "aa:bb:cc:dd:ee:ff" — used only as an
// internal map key (Server::client_id_for), not part of this header's wire
// format.
inline std::string mac_to_hex(const MacAddress& mac) {
    static const char* kHex = "0123456789abcdef";
    std::string out;
    out.reserve(17);
    for (size_t i = 0; i < mac.size(); ++i) {
        if (i) out.push_back(':');
        out.push_back(kHex[(mac[i] >> 4) & 0xF]);
        out.push_back(kHex[mac[i] & 0xF]);
    }
    return out;
}

// get_ifindex/get_hwaddr wrap the two ioctls this module needs to turn a
// caller-supplied interface name (e.g. "eth0") into what AF_PACKET/SOCK_RAW
// actually needs: an interface index to bind()/sendto() against
// (SIOCGIFINDEX) and this host's own MAC address on that interface
// (SIOCGIFHWADDR), auto-read so callers never have to supply their own MAC
// — symmetric to how rcp/udp.hpp never asks a caller for its own local IP.
inline bool get_ifindex(int fd, const char* ifname, int& out_ifindex) {
    struct ifreq ifr{};
    std::strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    if (::ioctl(fd, SIOCGIFINDEX, &ifr) < 0) return false;
    out_ifindex = ifr.ifr_ifindex;
    return true;
}

inline bool get_hwaddr(int fd, const char* ifname, MacAddress& out_mac) {
    struct ifreq ifr{};
    std::strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    if (::ioctl(fd, SIOCGIFHWADDR, &ifr) < 0) return false;
    std::memcpy(out_mac.data(), ifr.ifr_hwaddr.sa_data, kMacLen);
    return true;
}

// set_recv_timeout: unlike rcp/udp.hpp's Server/Client, which unblock a
// thread parked in a blocking recv/recvfrom by calling ::shutdown() on the
// socket from close(), AF_PACKET sockets do not support shutdown() on Linux
// (the kernel's packet socket proto_ops leaves shutdown unimplemented, so it
// is a silent no-op) — a blocked recvfrom() on an AF_PACKET socket is not
// woken up by shutdown() the way rcp/udp.hpp's INET sockets are. Instead,
// every real socket below sets a short SO_RCVTIMEO so its serve()/read_loop()
// wakes up periodically on its own to recheck the closed_ flag, bounding
// close()'s wait to about this timeout rather than hanging indefinitely.
inline bool set_recv_timeout(int fd, int millis) {
    struct timeval tv{};
    tv.tv_sec  = millis / 1000;
    tv.tv_usec = (millis % 1000) * 1000;
    return ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0;
}

} // namespace detail

// ── Server ────────────────────────────────────────────────────────────────────
// Server opens an AF_PACKET/SOCK_RAW socket bound to one network interface,
// filtered to EtherType 0x22F0 at socket-creation time, reads this host's
// own MAC address off that interface (detail::get_hwaddr) to use as the
// source address on every reply it sends, decodes each inbound frame's own
// AVTP envelope, and dispatches every ACF request the frame carries, then
// sends the reply back to the sender's own source MAC on the same interface.
// Needs CAP_NET_RAW (or root) to open the socket at all; construct, then
// call ok().
//
// Two handler shapes, set independently via set_handler()/set_frame_handler()
// below — if both are set, set_frame_handler()'s FrameHandler takes priority
// for every inbound frame (see serve()'s own comment):
//
//  - Handler: dispatches ONE already-decoded ACF message at a time, same
//    shape as rcp::mock::Server::dispatch() and rcp/udp.hpp::Server's own
//    Handler. Simple, and sufficient for a caller (e.g.
//    tests/l2_veth_roundtrip.cpp) that only needs to answer plain,
//    unconditional Standard requests with no Table 24 routing config, no
//    conditional/cancellation opcodes, and no E2E/fragmentation in play.
//  - FrameHandler (this file's own namespace-scope type, see its own doc
//    comment above): dispatches one WHOLE frame's raw ACF bytes at a time,
//    the shape rcp::mock::Server::dispatch_frame()/dispatch_frame_e2e()
//    take — this is the entry point that actually gets Table 24 suppression,
//    conditional/cancellation-opcode routing, and E2E/fragment dispatch, and
//    is the one a caller wiring this Server to a real rcp::mock::Server (or
//    any dispatcher with the same frame-level contract) should use. See this
//    file's own FrameHandler doc comment above for why Handler alone,
//    wired straight to mock::Server::dispatch(), silently downgrades all
//    three of those behaviors.
class Server {
public:
    using Handler = std::function<std::error_code(size_t client,
                                                    const acf::AcfMessageInfo& req,
                                                    const std::vector<uint8_t>& req_payload,
                                                    acf::AcfMessageInfo& out_resp,
                                                    std::vector<uint8_t>& out_resp_payload)>;

    Server(avtp::StreamId stream_id, const char* ifname)
        : stream_id_(stream_id), fd_(-1) {
        fd_ = ::socket(AF_PACKET, SOCK_RAW, htons(kEtherType));
        if (fd_ < 0) return;

        if (!detail::get_ifindex(fd_, ifname, ifindex_) ||
            !detail::get_hwaddr(fd_, ifname, local_mac_)) {
            ::close(fd_);
            fd_ = -1;
            return;
        }

        sockaddr_ll sll{};
        sll.sll_family   = AF_PACKET;
        sll.sll_protocol = htons(kEtherType);
        sll.sll_ifindex  = ifindex_;
        if (::bind(fd_, reinterpret_cast<sockaddr*>(&sll), sizeof(sll)) < 0) {
            ::close(fd_);
            fd_ = -1;
            return;
        }
        // See detail::set_recv_timeout's own comment: AF_PACKET has no
        // working shutdown(), so serve() needs to wake up on its own to
        // notice close() was called.
        detail::set_recv_timeout(fd_, kRecvTimeoutMillis);
        serve_thread_ = std::thread([this]{ serve(); });
    }

    ~Server() { close(); }

    MacAddress local_mac() const noexcept { return local_mac_; }

    void set_handler(Handler h) {
        std::lock_guard<std::mutex> lk(mu_);
        handler_ = std::move(h);
    }

    // set_frame_handler — see this class's own header comment for the full
    // contract and why this is the entry point a caller wiring a real
    // rcp::mock::Server (or any frame-level dispatcher) should use instead of
    // set_handler() above. Takes priority over Handler for every inbound
    // frame once set (serve()'s own comment) — set both only if intentionally
    // relying on that priority (e.g. tests exercising both paths against the
    // same Server).
    void set_frame_handler(FrameHandler h) {
        std::lock_guard<std::mutex> lk(mu_);
        frame_handler_ = std::move(h);
    }

    void close() {
        if (!closed_.exchange(true)) {
            if (fd_ >= 0) {
                ::shutdown(fd_, SHUT_RDWR);
                ::close(fd_);
                fd_ = -1;
            }
        }
        if (serve_thread_.joinable()) serve_thread_.join();
    }

    bool ok() const noexcept { return fd_ >= 0; }

private:
    avtp::StreamId stream_id_;
    int  fd_;
    int  ifindex_ = -1;
    MacAddress local_mac_{};
    std::atomic<bool>     closed_{false};
    std::atomic<uint16_t> seq_{0};
    std::mutex   mu_;
    Handler      handler_;
    FrameHandler frame_handler_;
    std::thread  serve_thread_;

    // client_ids_ assigns each distinct sender MAC a stable, opaque size_t
    // identity, first-seen order — same role as rcp/udp.hpp::Server's own
    // client_ids_, keyed by source MAC instead of "host:port".
    std::map<std::string, size_t> client_ids_;
    size_t next_client_id_ = 0;

    size_t client_id_for(const MacAddress& src) {
        auto key = detail::mac_to_hex(src);
        auto it  = client_ids_.find(key);
        if (it != client_ids_.end()) return it->second;
        size_t id = next_client_id_++;
        client_ids_.emplace(key, id);
        return id;
    }

    void serve() {
        std::vector<uint8_t> buf(kMaxFrame);
        while (!closed_.load()) {
            sockaddr_ll from{};
            socklen_t   flen = sizeof(from);
            ssize_t n = ::recvfrom(fd_, buf.data(), buf.size(), 0,
                                    reinterpret_cast<sockaddr*>(&from), &flen);
            if (n < 0) {
                // EAGAIN/EWOULDBLOCK is just the SO_RCVTIMEO timeout
                // (detail::set_recv_timeout's own comment) firing so this
                // loop can recheck closed_ — not a real error.
                if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
                break;
            }
            if (n == 0) continue;
            // AF_PACKET sockets bound to an interface also see a copy of
            // this host's own outgoing frames on that interface
            // (sll_pkttype == PACKET_OUTGOING); without this check, a
            // Server's own reply would loop back into its own receive path
            // and be mistaken for a fresh inbound request.
            if (from.sll_pkttype == PACKET_OUTGOING) continue;

            EthHeader       eth_hdr;
            AvtpFrameHeader avtp_hdr;
            if (decode_l2_frame_header(buf.data(), static_cast<size_t>(n), eth_hdr, avtp_hdr)) continue;
            const size_t acf_off = kEthHeaderLen + avtp_hdr.acf_offset;

            MultiFrame resp;
            resp.use_tscf        = avtp_hdr.use_tscf;
            resp.stream_id       = stream_id_;
            resp.sequence_num    = static_cast<uint16_t>(++seq_);
            resp.timestamp_valid = avtp_hdr.timestamp_valid;
            resp.avtp_timestamp  = avtp_hdr.avtp_timestamp;

            // Snapshot both handlers under mu_ (protects against a
            // concurrent set_handler()/set_frame_handler() call, same as
            // this loop's own pre-existing client_id_for() lock scope) and
            // release the lock again before running either one — a Handler/
            // FrameHandler body has no business running while this Server's
            // own mutex is held.
            size_t       client;
            Handler      handler_copy;
            FrameHandler frame_handler_copy;
            {
                std::lock_guard<std::mutex> lk(mu_);
                client             = client_id_for(eth_hdr.src);
                handler_copy       = handler_;
                frame_handler_copy = frame_handler_;
            }

            if (frame_handler_copy) {
                // FrameHandler path — see this class's own header comment
                // and this file's own FrameMemberResult/FrameHandler doc
                // comment above: Table 24 suppression, conditional/
                // cancellation-opcode routing, and E2E/fragment dispatch are
                // all the FrameHandler's own job (rcp::mock::Server::
                // dispatch_frame()/dispatch_frame_e2e() already apply every
                // one of them internally) — this Server's only remaining
                // job is handing it the raw ACF bytes and re-framing
                // whatever member responses it returns.
                std::vector<uint8_t> acf_bytes(buf.begin() + static_cast<long>(acf_off),
                                                buf.begin() + n);
                std::vector<FrameMemberResult> results;
                frame_handler_copy(client, avtp_hdr.stream_id, avtp_hdr.sequence_num, acf_bytes, results);

                resp.messages.reserve(results.size());
                for (auto& r : results) {
                    // response.rsp == false means "nothing to send for this
                    // member" (Table 24 suppression, an evt[3]-less
                    // admission outcome, or a frame-level sequence-gate
                    // rejection) — dropped, not encoded as an empty
                    // response. See this file's own FrameHandler doc
                    // comment above.
                    if (!r.response.rsp) continue;
                    acf::AcfEntry entry;
                    entry.info    = r.response;
                    entry.payload = std::move(r.response_payload);
                    resp.messages.push_back(std::move(entry));
                }
                // Every member in this frame was suppressed/produced no
                // wire response: send nothing at all, matching Table 24's
                // own "0 means send nothing" contract rather than emitting
                // an empty AVTPDU.
                if (resp.messages.empty()) continue;
            } else {
                std::vector<acf::AcfEntry> req_messages;
                if (acf::decode_acf_messages(buf.data() + acf_off, static_cast<size_t>(n) - acf_off,
                                              req_messages)) {
                    continue;
                }
                resp.messages.reserve(req_messages.size());
                // §12.9.1.1: "check each of them individually if to be
                // processed or not" — each request in the frame is
                // dispatched and answered on its own, not as a batch.
                for (const auto& m : req_messages) {
                    acf::AcfEntry out_entry;
                    if (handler_copy) {
                        auto ec = handler_copy(client, m.info, m.payload, out_entry.info, out_entry.payload);
                        (void)ec; // Handler always populates out_entry.info even on
                                  // failure, same contract rcp::mock::Server::dispatch
                                  // and rcp/udp.hpp::Server document.
                    } else {
                        out_entry.info = acf::make_response(m.info, acf::ResponseKind::Acknowledge);
                    }
                    resp.messages.push_back(std::move(out_entry));
                }
            }

            auto out_bytes = encode_l2_multi_frame(eth_hdr.src, local_mac_, resp);
            sockaddr_ll to{};
            to.sll_family   = AF_PACKET;
            to.sll_ifindex  = ifindex_;
            to.sll_halen    = kMacLen;
            std::copy(eth_hdr.src.begin(), eth_hdr.src.end(), to.sll_addr);
            ::sendto(fd_, out_bytes.data(), out_bytes.size(), 0,
                     reinterpret_cast<sockaddr*>(&to), sizeof(to));
        }
    }
};

// ── Client ────────────────────────────────────────────────────────────────────
// Client opens an AF_PACKET/SOCK_RAW socket bound to one network interface
// and sends AVTPDU-framed ACF requests to one caller-supplied destination
// MAC address (unicast or multicast — this header does not derive one, see
// this file's own header comment), correlating each response by the
// (byte_bus_id, transaction_num) pair rcp/acf.hpp's make_response() echoes
// back — identical correlation strategy, and near-identical request()
// signature, to rcp/udp.hpp::Client, so a caller can swap one transport for
// the other with minimal call-site change.
class Client {
public:
    Client(avtp::StreamId stream_id, const char* ifname, const MacAddress& dest_mac)
        : stream_id_(stream_id), dest_mac_(dest_mac), fd_(-1) {
        fd_ = ::socket(AF_PACKET, SOCK_RAW, htons(kEtherType));
        if (fd_ < 0) return;

        if (!detail::get_ifindex(fd_, ifname, ifindex_) ||
            !detail::get_hwaddr(fd_, ifname, local_mac_)) {
            ::close(fd_);
            fd_ = -1;
            return;
        }

        sockaddr_ll sll{};
        sll.sll_family   = AF_PACKET;
        sll.sll_protocol = htons(kEtherType);
        sll.sll_ifindex  = ifindex_;
        if (::bind(fd_, reinterpret_cast<sockaddr*>(&sll), sizeof(sll)) < 0) {
            ::close(fd_);
            fd_ = -1;
            return;
        }
        // See detail::set_recv_timeout's own comment: AF_PACKET has no
        // working shutdown(), so read_loop() needs to wake up on its own to
        // notice close() was called.
        detail::set_recv_timeout(fd_, kRecvTimeoutMillis);
        read_thread_ = std::thread([this]{ read_loop(); });
    }

    ~Client() { auto ec = close(); (void)ec; }

    MacAddress local_mac() const noexcept { return local_mac_; }
    MacAddress dest_mac()  const noexcept { return dest_mac_; }

    // request sends one ACF request — ABB or GBB per req.info's own
    // acf_msg_type, wrapped in the AVTPDU header rcp/avtp.hpp defines for
    // whichever kind `use_tscf` selects — and blocks until the matching
    // response arrives or `ctx` is done. Same signature as
    // rcp/udp.hpp::Client::request.
    std::error_code request(const rcp::Context& ctx,
                             const acf::AcfMessageInfo& req,
                             const std::vector<uint8_t>& req_payload,
                             acf::AcfMessageInfo&        out_resp,
                             std::vector<uint8_t>&       out_resp_payload,
                             bool     use_tscf = false,
                             uint32_t avtp_timestamp = 0,
                             uint64_t message_timestamp = 0) {
        if (closed_.load()) return ErrClosed;
        if (ctx.done())     return ErrTimeout;

        Frame out;
        out.use_tscf          = use_tscf;
        out.stream_id         = stream_id_;
        out.sequence_num      = static_cast<uint16_t>(++seq_);
        out.timestamp_valid   = use_tscf;
        out.avtp_timestamp    = avtp_timestamp;
        out.info              = req;
        out.message_timestamp = message_timestamp;
        out.payload            = req_payload;

        const uint16_t key = pending_key(req.byte_bus_id, req.transaction_num);
        auto result = std::make_shared<std::promise<Frame>>();
        auto future = result->get_future();
        {
            std::lock_guard<std::mutex> lk(mu_);
            pending_[key] = result;
        }
        auto cleanup = [&]{
            std::lock_guard<std::mutex> lk(mu_);
            pending_.erase(key);
        };

        auto wire_bytes = encode_l2_frame(dest_mac_, local_mac_, out);
        sockaddr_ll to{};
        to.sll_family  = AF_PACKET;
        to.sll_ifindex = ifindex_;
        to.sll_halen   = kMacLen;
        std::copy(dest_mac_.begin(), dest_mac_.end(), to.sll_addr);
        if (::sendto(fd_, wire_bytes.data(), wire_bytes.size(), 0,
                      reinterpret_cast<sockaddr*>(&to), sizeof(to)) < 0) {
            cleanup();
            return ErrClosed;
        }

        std::future_status st;
        if (ctx.deadline()) {
            st = future.wait_until(*ctx.deadline());
        } else {
            future.wait();
            st = std::future_status::ready;
        }
        cleanup();
        if (st == std::future_status::timeout) return ErrTimeout;
        if (!future.valid()) return ErrClosed;

        Frame resp = future.get();
        out_resp         = resp.info;
        out_resp_payload = std::move(resp.payload);
        return {};
    }

    std::error_code close() {
        if (!closed_.exchange(true)) {
            if (fd_ >= 0) {
                ::shutdown(fd_, SHUT_RDWR);
                ::close(fd_);
            }
        }
        if (read_thread_.joinable()) read_thread_.join();
        return {};
    }

    bool ok() const noexcept { return fd_ >= 0; }

private:
    static uint16_t pending_key(avtp::ByteBusId bus_id, uint8_t transaction_num) noexcept {
        return static_cast<uint16_t>((static_cast<uint16_t>(bus_id) << 8) | transaction_num);
    }

    avtp::StreamId stream_id_;
    MacAddress dest_mac_{};
    MacAddress local_mac_{};
    int  fd_;
    int  ifindex_ = -1;
    std::atomic<bool>     closed_{false};
    std::atomic<uint16_t> seq_{0};
    std::mutex mu_;
    std::map<uint16_t, std::shared_ptr<std::promise<Frame>>> pending_;
    std::thread read_thread_;

    void read_loop() {
        std::vector<uint8_t> buf(kMaxFrame);
        while (!closed_.load()) {
            sockaddr_ll from{};
            socklen_t   flen = sizeof(from);
            ssize_t n = ::recvfrom(fd_, buf.data(), buf.size(), 0,
                                    reinterpret_cast<sockaddr*>(&from), &flen);
            if (n < 0) {
                // See Server::serve()'s identical comment: SO_RCVTIMEO
                // firing, not a real error.
                if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
                break;
            }
            if (n == 0) continue;
            // See Server::serve()'s identical comment: skip this Client's
            // own transmitted requests looping back as PACKET_OUTGOING.
            if (from.sll_pkttype == PACKET_OUTGOING) continue;

            EthHeader hdr;
            Frame     resp;
            if (decode_l2_frame(buf.data(), static_cast<size_t>(n), hdr, resp)) continue;

            const uint16_t key = pending_key(resp.info.byte_bus_id, resp.info.transaction_num);
            std::lock_guard<std::mutex> lk(mu_);
            auto it = pending_.find(key);
            if (it != pending_.end()) {
                it->second->set_value(std::move(resp));
                pending_.erase(it);
            }
        }
    }
};

#else // !RCP_L2_LINUX (macOS/Windows/other stub — see this file's header comment)

class Server {
public:
    using Handler = std::function<std::error_code(size_t, const acf::AcfMessageInfo&,
                                                    const std::vector<uint8_t>&,
                                                    acf::AcfMessageInfo&, std::vector<uint8_t>&)>;

    Server(avtp::StreamId, const char*) {}
    MacAddress local_mac() const noexcept { return {}; }
    void set_handler(Handler) {}
    void set_frame_handler(FrameHandler) {}
    void close() {}
    bool ok() const noexcept { return false; }
};

class Client {
public:
    Client(avtp::StreamId, const char*, const MacAddress&) {}
    MacAddress local_mac() const noexcept { return {}; }
    MacAddress dest_mac()  const noexcept { return {}; }
    std::error_code request(const rcp::Context&, const acf::AcfMessageInfo&,
                             const std::vector<uint8_t>&,
                             acf::AcfMessageInfo&, std::vector<uint8_t>&,
                             bool = false, uint32_t = 0, uint64_t = 0) {
        return std::make_error_code(std::errc::function_not_supported);
    }
    std::error_code close() { return {}; }
    bool ok() const noexcept { return false; }
};

#endif // RCP_L2_LINUX

} // namespace l2
} // namespace rcp
