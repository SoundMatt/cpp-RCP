// fusa:req REQ-UDP-001
// fusa:req REQ-UDP-002
// fusa:req REQ-UDP-003
// fusa:req REQ-UDP-004
// fusa:req REQ-UDP-005
// fusa:req REQ-UDP-006
// fusa:req REQ-UDP-007
// fusa:req REQ-UDP-008
// fusa:req REQ-UDP-009
// fusa:req REQ-UDP-010
// fusa:req REQ-UDP-011
// fusa:req REQ-UDP-012
// fusa:req REQ-UDP-013
// fusa:req REQ-UDP-014

// Native IEEE 1722-over-UDP/IP transport for the OPEN Alliance TC18 Remote
// Control Protocol Specification v0.5.1_RC — carries real AVTPDU (NTSCF/
// TSCF, rcp/avtp.hpp) frames wrapping ACF_ABB/ACF_GBB messages (rcp/acf.hpp)
// as raw UDP datagram payloads.
//
// On POSIX (Linux, macOS): full implementation using BSD sockets.
// On Windows: stub that returns std::errc::function_not_supported.
//
// ROADMAP.md milestone 57, "Native Transport Rebuild — UDP/IP (Annex J),
// v2.13.0": this header REPLACES this file's pre-replacement content in
// full, per the Satellite Package Disposition table's entry for `udp.hpp`
// — the bespoke `R`/`C`-magic 16-byte Zone/Command/Response/Status frame
// this file used to carry (rcp/legacy_wire.hpp) is discarded outright, not
// adapted, since it *is* the old wire format this whole roadmap replaces
// (the roadmap's own words for this milestone). Unlike rcp/mock.hpp's split
// at v2.12.0, no legacy shim file was created here: grepping the tree found
// no consumer of the old udp::ZoneServer/udp::Controller/udp::Registry API
// beyond this file's own (now-deleted) test and rcp/tsn.hpp's doc comment
// — and rcp/tsn.hpp itself only ever depended on the generic rcp::Controller
// interface plus a raw socket fd, never on udp:: types directly, so it needed
// no change here. rcp/legacy_wire.hpp is deleted in the same change as this
// file for the same reason.
//
// This module does not build on rcp.hpp's Zone/Command/Controller/Registry
// model — per rcp.hpp's own header comment, nothing new should. Instead it
// addresses the way rcp/avtp.hpp and rcp/acf.hpp already do: a StreamId per
// endpoint plus a byte_bus_id/transaction_num pair per request, with request/
// response correlation performed via the echo rule rcp/acf.hpp's
// make_response() documents rather than a locally invented 32-bit request
// id. Server's request handler is deliberately shaped to match
// rcp::mock::Server::dispatch's signature (v2.12.0) so an in-process
// simulator can be wired up as this transport's handler directly, without
// this header needing to depend on rcp/mock.hpp itself.
//
// IEEE 1722's own Annex J describes carrying AVTPDUs over UDP/IP instead of
// raw Ethernet (rcp/l2.hpp, added alongside this fix), for links where
// native AVTP framing (destination MAC + EtherType 0x22F0) is not
// available. An earlier revision of this file's comment claimed the AVTPDU
// bytes rcp/avtp.hpp/rcp/acf.hpp produce are carried unmodified as the UDP
// payload with no additional encapsulation — that claim was wrong. Per two
// independent public secondary sources — a Wireshark issue tracker
// discussion of the real Annex J wire text, and the COVESA Open1722
// open-source reference implementation's actual `Avtp_Udp_t` header struct
// (`include/avtp/Udp.h`, BSD-3-Clause, github.com/COVESA/Open1722) — an
// Annex J UDP payload actually begins with a 4-byte (32-bit) big-endian
// "encapsulation sequence number" field before the AVTPDU itself
// (kEncapSeqLen/encode_annexj_datagram/decode_annexj_datagram below), and
// the standard destination UDP ports are 17220 ("Continuous"/streaming) and
// 17221 ("Discrete"/control — kAnnexJControlPort, the default this module's
// Server/Client now bind/connect to when a caller doesn't override it).
// THIS PROVENANCE CAVEAT APPLIES EVERYWHERE THIS FILE CITES THOSE TWO
// FACTS: this codebase has no access to the paywalled IEEE 1722-2016
// standard text itself, so "Annex J conformant" below means "conformant
// with these two independent secondary sources' reading of Annex J," not
// verified against the primary standard. Full bit-for-bit conformance
// against other TC18/Annex-J implementations is not claimed, same as the
// equivalent disclaimers in rcp/avtp.hpp, rcp/acf.hpp, and
// rcp/discovery.hpp.
#pragma once

#include "acf.hpp"
#include "avtp.hpp"
#include "rcp.hpp" // for rcp::Context only — see this header's own scope note above

#include <atomic>
#include <cstdint>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#  include <arpa/inet.h>
#  include <netdb.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#  include <unistd.h>
#  define RCP_UDP_POSIX 1
#endif

namespace rcp {
namespace udp {

// A UDP datagram is capped at 65507 payload bytes (65535 minus the IPv4 and
// UDP headers) regardless of AVTPDU/ACF content — the same ceiling
// rcp/legacy_wire.hpp's MaxPayload used to size against.
constexpr size_t kMaxDatagram = 65507;

// kAnnexJControlPort (17221): the standard destination UDP port for
// "Discrete" (control-plane) Annex J traffic, per the two secondary sources
// this file's header comment names — RCP requests/responses/acknowledges
// are control-plane traffic, so this is the applicable port, and the one
// Server/Client below now default to when a caller doesn't pass one
// explicitly. 17220 ("Continuous"/streaming traffic) has no consumer in
// this codebase today and is not defined here.
constexpr uint16_t kAnnexJControlPort = 17221;

// kEncapSeqLen: width, in bytes, of the Annex J UDP-payload encapsulation
// sequence number (see header comment) that precedes every AVTPDU this
// module sends/receives over UDP — absent entirely from rcp/l2.hpp's native
// Ethernet framing, which carries the AVTPDU directly.
constexpr size_t kEncapSeqLen = 4;

// encode_annexj_datagram/decode_annexj_datagram — pure, socket-free codec
// for the Annex J UDP-payload envelope: a 4-byte big-endian encapsulation
// sequence number followed by the AVTPDU bytes unchanged. Big-endian to
// match this repo's existing AVTPDU byte-order convention
// (avtp::detail::put_u32/get_u32, reused directly rather than re-derived).
// These are free functions, independent of RCP_UDP_POSIX, so they — and the
// tests exercising them — compile and run on every platform, including the
// Windows stub build, with no socket involved.
//
// This module tracks and exposes the encapsulation sequence number it
// sends and the most recent one it has received (Client::last_sent_encap_seq/
// last_recv_encap_seq, Server::last_recv_encap_seq(client)) so a future
// caller has the raw data available for e.g. loss detection — but no such
// detection is implemented here. Annex J's own exact intended semantics for
// this field (e.g. whether gaps are meant to signal loss, whether it resets
// per-flow) are NOT verified against the primary standard; treat the value
// as an opaque per-sender monotonic counter only.
inline std::vector<uint8_t> encode_annexj_datagram(uint32_t encap_seq,
                                                     const std::vector<uint8_t>& avtpdu) {
    std::vector<uint8_t> out(kEncapSeqLen + avtpdu.size());
    avtp::detail::put_u32(out.data(), encap_seq);
    std::copy(avtpdu.begin(), avtpdu.end(), out.begin() + static_cast<long>(kEncapSeqLen));
    return out;
}

inline std::error_code decode_annexj_datagram(const uint8_t* b, size_t len,
                                                uint32_t& out_encap_seq,
                                                const uint8_t*& out_avtpdu,
                                                size_t& out_avtpdu_len) {
    if (len < kEncapSeqLen) return avtp::make_error_code(avtp::AvtpErrc::short_buffer);
    out_encap_seq  = avtp::detail::get_u32(b);
    out_avtpdu     = b + kEncapSeqLen;
    out_avtpdu_len = len - kEncapSeqLen;
    return {};
}

// FrameResponse — one dispatched member's own outcome, exactly enough of it
// for Server::serve() (below) to decide whether to put a reply message on
// the wire for it, and what that message says. Mirrors the response half of
// rcp::mock::FrameMemberResult (rcp/mock.hpp) without this header needing to
// #include rcp/mock.hpp itself — this file's own header comment already
// establishes that the Server/Client here stay usable without ever
// depending on the in-process simulator; a caller wiring
// rcp::mock::Server::dispatch_frame()/dispatch_frame_e2e() to Handler below
// supplies a small glue lambda translating each mock::FrameMemberResult it
// gets back into this shape (see tests/test_udp.cpp). Declared outside the
// RCP_UDP_POSIX split below (it depends only on acf.hpp, included
// unconditionally) so both the real POSIX Server and the Windows stub
// Server share the exact same Handler contract.
//
// info.rsp == false (acf::AcfMessageInfo's own default) is a valid,
// EXPECTED outcome for a member Server::serve() must NOT put a reply
// message on the wire for at all — Table 24 response suppression
// (REQ-RMAP-048/049, rcp::mock::suppress_response_per_stream_cfg), a
// queued/pending/suspended/still-reassembling-a-fragment admission outcome,
// or any other "no wire response" case rcp::mock::DispatchErrc documents
// (rcp/mock.hpp). serve() below drops any entry with info.rsp == false from
// the outgoing MultiFrame instead of encoding it — exactly what a caller
// going through rcp::mock::Server::dispatch_frame()/_e2e() directly would
// do after inspecting .response.rsp itself.
struct FrameResponse {
    acf::AcfMessageInfo   info;
    std::vector<uint8_t>  payload;
};

// pending_key combines byte_bus_id (an 11-bit wire field — avtp::ByteBusId's
// own comment, and acf.hpp's detail::kByteBusIdMask) and transaction_num (a
// full 8-bit field) into one collision-free correlation key for
// Client::pending_ below. A pure function, independent of RCP_UDP_POSIX like
// encode_annexj_datagram/decode_annexj_datagram above, so it — and the test
// exercising it directly — compiles and runs on every platform.
//
// Shifting bus_id left by 8 keeps every (byte_bus_id, transaction_num) pair
// distinct: transaction_num occupies bits 0-7 and byte_bus_id (0-2047, 11
// bits) occupies bits 8-18 of the returned uint32_t, so the two fields never
// overlap. A previous version of this function returned uint16_t, computing
// the identical shift but then truncating the result back down to 16 bits —
// silently dropping byte_bus_id's top 3 bits, so e.g. byte_bus_id 5 and 261
// (differing by exactly 256) collided whenever they shared a
// transaction_num, misdelivering one Client's pending response to another
// (or hanging it indefinitely) under concurrent requests. cpp-RCP v3.0.0
// deep audit finding; fixed by widening this key to uint32_t.
inline uint32_t pending_key(avtp::ByteBusId bus_id, uint8_t transaction_num) noexcept {
    return (static_cast<uint32_t>(bus_id) << 8) | static_cast<uint32_t>(transaction_num);
}

#if defined(RCP_UDP_POSIX)

// ── Frame ─────────────────────────────────────────────────────────────────────
// Frame is one encapsulated AVTPDU: an NTSCF or TSCF header (rcp/avtp.hpp)
// wrapping one ACF_ABB or ACF_GBB message (rcp/acf.hpp, selected by
// info.acf_msg_type). encode_frame/decode_frame compose those two headers'
// existing codecs rather than re-deriving any bit layout, and are pure
// functions — no socket I/O — so they can be exercised directly in tests
// without a real UDP round trip.
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
        hdr.sequence_num        = f.sequence_num;
        hdr.control_data_length = static_cast<uint16_t>(acf_msg.size());
        hdr.timestamp_valid     = f.timestamp_valid;
        hdr.avtp_timestamp      = f.avtp_timestamp;
        out = avtp::encode_tscf_header(hdr);
    } else {
        avtp::NtscfHeader hdr;
        hdr.stream_id           = f.stream_id;
        hdr.sequence_num        = f.sequence_num;
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
        // control_data_length integrity check (issue cpp-RCP-A3): the
        // header's own declared ACF byte length must match what's actually
        // left in the buffer, or a receiver trusting it for anything else
        // (e.g. finding where a *next* AVTPDU starts in a larger buffer)
        // would be silently misled by a corrupt/spoofed length field.
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
    // acf_msg_type is a 7-bit field, not a whole octet (rcp/acf.hpp v2.19.0
    // wire conformance pass, issue cpp-RCP-04) — go through
    // acf::peek_acf_msg_type() rather than comparing b[acf_off] directly,
    // which would almost never match kAcfMsgTypeGbb now that byte0's LSB is
    // acf_msg_length's MSB.
    if (acf::peek_acf_msg_type(b + acf_off) == acf::kAcfMsgTypeGbb) {
        return acf::decode_acf_gbb(b + acf_off, len - acf_off, out.info,
                                    out.message_timestamp, out.payload);
    }
    out.message_timestamp = 0;
    return acf::decode_acf_abb(b + acf_off, len - acf_off, out.info, out.payload);
}

// ── MultiFrame — multiple ACF requests in one AVTPDU (extraction §12.9.1.1; issue cpp-RCP-04-fresh) ──
// "An RC Server shall support to handle multiple requests in one frame and
// check each of them individually if to be processed or not ... The RC
// Server shall support the handling of multiple request types in one
// frame." Frame/encode_frame/decode_frame above model exactly one ACF_ABB/
// ACF_GBB message per AVTPDU — correct for the common case, but unable to
// represent more than one request (or response) sharing a single NTSCF/TSCF
// header. MultiFrame is the same AVTPDU envelope carrying one-or-more
// acf::AcfEntry messages (rcp/acf.hpp's decode_acf_messages, cpp-RCP-01's
// acf_msg_length fix above); Server and Client below are built on this, not
// on Frame, so a single incoming request continues to behave exactly as it
// did through Frame (MultiFrame::messages.size() == 1 is the common case),
// while a sender that packs several requests into one datagram is now
// actually handled per-request rather than having its later requests
// silently swallowed into the first one's payload or dropped.
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
        hdr.sequence_num        = f.sequence_num;
        hdr.control_data_length = static_cast<uint16_t>(acf_bytes.size());
        hdr.timestamp_valid     = f.timestamp_valid;
        hdr.avtp_timestamp      = f.avtp_timestamp;
        out = avtp::encode_tscf_header(hdr);
    } else {
        avtp::NtscfHeader hdr;
        hdr.stream_id           = f.stream_id;
        hdr.sequence_num        = f.sequence_num;
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
        // control_data_length integrity check (issue cpp-RCP-A3) — see
        // decode_frame's equivalent comment above.
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

// ── Server ────────────────────────────────────────────────────────────────────
// Server binds a UDP socket (port 17221/kAnnexJControlPort by default — see
// this file's header comment for the secondary-source provenance of that
// number — unless a caller passes a different one), decodes each inbound
// datagram as an Annex J envelope (4-byte encapsulation sequence number +
// MultiFrame AVTPDU), and hands the *raw* ACF-region bytes (the AVTPDU's
// payload immediately after its own NTSCF/TSCF header — one, the common
// case, or more than one ACF_ABB/ACF_GBB member packed back to back,
// extraction §12.9.1.1) to a caller-supplied frame-level Handler exactly
// once per datagram, then encodes every non-suppressed FrameResponse it
// gets back into a single reply MultiFrame (frame order preserved),
// re-wraps it in a fresh encapsulation sequence number, and sends it back
// to the sender under the same header kind (NTSCF/TSCF) the request
// arrived under — UNLESS every member's own response was suppressed (Table
// 24) or the handler produced nothing at all, in which case no reply
// datagram is sent, matching what "no response is to be sent" actually
// means on the wire. Malformed datagrams (too short for even the
// encapsulation sequence number, short buffer, bad subtype, unrecognized
// ACF message type on the very first message) are dropped silently, the
// same "drop rather than partially process" choice rcp/discovery.hpp
// documents for its own decode path.
//
// Handler is deliberately FRAME-level, not dispatch()'s own single-message
// shape (issue cpp-RCP-udp-01, cpp-RCP issue #129 Phase 5 wave 1): an
// earlier revision of this Handler matched rcp::mock::Server::dispatch's
// single already-isolated-member contract directly (mock.hpp:1006), which
// meant a caller wiring this Handler straight to mock::Server::dispatch —
// the obvious, natural thing to do — silently lost Table 24 response
// suppression, conditional/cancellation-opcode routing (peek_conditional_
// request_type, mock.hpp:1763), and E2E/fragmentation handling for every
// request that arrived over UDP: dispatch()/dispatch_e2e()/
// dispatch_e2e_fragment() only ever see ONE already-isolated member, so
// none of mock.hpp's own Phase 4 batch D2 frame-level machinery
// (dispatch_frame()/dispatch_frame_e2e(), mock.hpp:1536/1587, which apply
// Table 24 suppression via decode_and_dispatch()'s own admit_and_
// classify()/suppress_response_per_stream_cfg() calls internally) was ever
// reached. Handler's signature now matches dispatch_frame()'s/
// dispatch_frame_e2e()'s own shared (client, stream_id, frame) shape (plus
// the AVTPDU's own Sequence_Nr, needed only by dispatch_frame_e2e()'s own
// once-per-frame REQ-E2E-028/029 gate) instead, so a caller can wire either
// one directly (via a thin glue lambda translating
// std::vector<mock::FrameMemberResult> to std::vector<FrameResponse> — see
// tests/test_udp.cpp) and get the exact same behavior dispatching against
// mock::Server directly would.
class Server {
public:
    // sequence_num is the enclosing AVTPDU's own Sequence_Nr (avtp::
    // NtscfHeader::sequence_num/avtp::TscfHeader::sequence_num, 8-bit
    // rolling counter) — required by rcp::mock::Server::dispatch_frame_e2e()'s
    // own frame-level sequence gate (mock.hpp:1572-1586) even though a
    // caller wiring dispatch_frame() instead (no E2E) has no use for it.
    // Returns the number of FrameResponse entries appended to
    // out_responses (out_responses is NOT cleared first, mirroring
    // rcp::mock::Server::dispatch_frame()'s own "size_t dispatched count,
    // caller passes an empty vector" contract — every out_responses this
    // file's own Handler is ever invoked with already starts empty, see
    // serve() below).
    using Handler = std::function<size_t(size_t client, avtp::StreamId stream_id,
                                          uint8_t sequence_num,
                                          const std::vector<uint8_t>& acf_frame,
                                          std::vector<FrameResponse>& out_responses)>;

    Server(avtp::StreamId stream_id, const char* addr, uint16_t port = kAnnexJControlPort)
        : stream_id_(stream_id), fd_(-1) {
        fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (fd_ < 0) return;
        sockaddr_in sa{};
        sa.sin_family = AF_INET;
        sa.sin_port   = htons(port);
        if (!addr || addr[0] == '\0')
            sa.sin_addr.s_addr = INADDR_ANY;
        else
            ::inet_pton(AF_INET, addr, &sa.sin_addr);
        if (::bind(fd_, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) < 0) {
            ::close(fd_);
            fd_ = -1;
            return;
        }
        serve_thread_ = std::thread([this]{ serve(); });
    }

    ~Server() { close(); }

    // addr_string returns "host:port" for this server's bound address.
    std::string addr_string() const {
        sockaddr_in sa{};
        socklen_t len = sizeof(sa);
        if (fd_ < 0 || ::getsockname(fd_, reinterpret_cast<sockaddr*>(&sa), &len) < 0)
            return {};
        char buf[INET_ADDRSTRLEN];
        ::inet_ntop(AF_INET, &sa.sin_addr, buf, sizeof(buf));
        return std::string(buf) + ":" + std::to_string(ntohs(sa.sin_port));
    }

    uint16_t port() const {
        sockaddr_in sa{};
        socklen_t len = sizeof(sa);
        if (fd_ < 0 || ::getsockname(fd_, reinterpret_cast<sockaddr*>(&sa), &len) < 0)
            return 0;
        return ntohs(sa.sin_port);
    }

    void set_handler(Handler h) {
        std::lock_guard<std::mutex> lk(mu_);
        handler_ = std::move(h);
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

    // last_recv_encap_seq returns the Annex J encapsulation sequence number
    // (see this file's header comment) most recently received from the
    // given client id, or 0 if that client has not sent anything yet — raw
    // data only, no loss-detection semantics implied or implemented (see
    // kEncapSeqLen's own comment above).
    uint32_t last_recv_encap_seq(size_t client) const {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = last_recv_encap_seq_.find(client);
        return it != last_recv_encap_seq_.end() ? it->second : 0;
    }

private:
    avtp::StreamId stream_id_;
    int  fd_;
    std::atomic<bool>     closed_{false};
    std::atomic<uint16_t> seq_{0};
    std::atomic<uint32_t> encap_seq_{0}; // Annex J encapsulation sequence number, outgoing replies
    mutable std::mutex   mu_;
    Handler      handler_;
    std::thread  serve_thread_;

    // client_ids_ assigns each distinct sender address a stable, opaque
    // size_t identity, first-seen order — the same role rcp/regmap.hpp's
    // Ep0 root-client index plays for its own callers, just derived from
    // the UDP sender address instead of an in-process connection index.
    std::map<std::string, size_t> client_ids_;
    size_t next_client_id_ = 0;
    // last_recv_encap_seq_ tracks the most recent inbound Annex J
    // encapsulation sequence number per client id — see
    // last_recv_encap_seq()'s own comment above for scope.
    std::map<size_t, uint32_t> last_recv_encap_seq_;

    static std::string addr_key(const sockaddr_in& sa) {
        char buf[INET_ADDRSTRLEN];
        ::inet_ntop(AF_INET, &sa.sin_addr, buf, sizeof(buf));
        return std::string(buf) + ":" + std::to_string(ntohs(sa.sin_port));
    }

    size_t client_id_for(const sockaddr_in& from) {
        auto key = addr_key(from);
        auto it  = client_ids_.find(key);
        if (it != client_ids_.end()) return it->second;
        size_t id = next_client_id_++;
        client_ids_.emplace(key, id);
        return id;
    }

    void serve() {
        std::vector<uint8_t> buf(kMaxDatagram);
        sockaddr_in from{};
        socklen_t   flen = sizeof(from);
        while (!closed_.load()) {
            ssize_t n = ::recvfrom(fd_, buf.data(), buf.size(), 0,
                                    reinterpret_cast<sockaddr*>(&from), &flen);
            if (n <= 0) break;

            // Strip the Annex J encapsulation sequence number before
            // handing the remainder to the existing AVTPDU/ACF decode path
            // (this file's header comment).
            uint32_t       in_encap_seq = 0;
            const uint8_t* avtpdu       = nullptr;
            size_t         avtpdu_len   = 0;
            if (decode_annexj_datagram(buf.data(), static_cast<size_t>(n),
                                        in_encap_seq, avtpdu, avtpdu_len))
                continue;

            // decode_multi_frame validates the NTSCF/TSCF header (subtype,
            // control_data_length) and, for the no-handler-registered
            // fallback below, decodes its member list — but Handler above
            // wants the RAW ACF-region bytes, not the decoded messages, so
            // a frame-level dispatcher (rcp::mock::Server::dispatch_frame()/
            // dispatch_frame_e2e()) can apply its own splitting/suppression/
            // conditional-opcode/E2E logic against the unmodified wire
            // bytes, exactly as it would for any other caller of those
            // entry points.
            MultiFrame req;
            if (decode_multi_frame(avtpdu, avtpdu_len, req)) continue;
            const size_t acf_off = req.use_tscf ? avtp::kTscfHeaderLen : avtp::kNtscfHeaderLen;
            const std::vector<uint8_t> acf_frame(avtpdu + acf_off, avtpdu + avtpdu_len);
            const uint8_t sequence_num = static_cast<uint8_t>(req.sequence_num);

            MultiFrame resp;
            resp.use_tscf        = req.use_tscf;
            resp.stream_id       = stream_id_;
            resp.sequence_num    = static_cast<uint16_t>(++seq_);
            resp.timestamp_valid = req.timestamp_valid;
            resp.avtp_timestamp  = req.avtp_timestamp;

            std::vector<FrameResponse> results;
            {
                std::lock_guard<std::mutex> lk(mu_);
                size_t client = client_id_for(from);
                last_recv_encap_seq_[client] = in_encap_seq;
                if (handler_) {
                    handler_(client, req.stream_id, sequence_num, acf_frame, results);
                } else {
                    // No handler registered: acknowledge every member
                    // individually — this class's own pre-existing default,
                    // now built from req.messages (decode_multi_frame's own
                    // already-decoded member list) rather than raw bytes,
                    // since there is no dispatch logic here to hand raw
                    // bytes to.
                    results.reserve(req.messages.size());
                    for (const auto& m : req.messages) {
                        FrameResponse r;
                        r.info = acf::make_response(m.info, acf::ResponseKind::Acknowledge);
                        results.push_back(std::move(r));
                    }
                }
            }

            // §12.9.1.1: each request in the datagram was checked and
            // dispatched individually — now assemble the reply MultiFrame
            // from whichever members actually produced a genuine wire
            // response (FrameResponse::info.rsp == true; see that struct's
            // own doc comment for why info.rsp == false is a valid,
            // non-error "do not reply to this one" outcome, e.g. Table 24
            // suppression).
            resp.messages.reserve(results.size());
            for (auto& r : results) {
                if (!r.info.rsp) continue;
                acf::AcfEntry entry;
                entry.info    = std::move(r.info);
                entry.payload = std::move(r.payload);
                resp.messages.push_back(std::move(entry));
            }
            if (resp.messages.empty()) continue; // every member suppressed / nothing to send back

            auto out_frame  = encode_multi_frame(resp);
            auto out_wire   = encode_annexj_datagram(++encap_seq_, out_frame);
            ::sendto(fd_, out_wire.data(), out_wire.size(), 0,
                     reinterpret_cast<sockaddr*>(&from), flen);
        }
    }
};

// ── Client ────────────────────────────────────────────────────────────────────
// Client connects to one Server address and sends AVTPDU-framed ACF requests,
// correlating each response by the (byte_bus_id, transaction_num) pair
// rcp/acf.hpp's make_response echoes back unchanged — there is no locally
// invented request id the way rcp.hpp's old Command::id was, since the new
// addressing model has no analog of it. Callers build `req` themselves (e.g.
// via acf::make_standard_request or rcp/discovery.hpp's
// make_discovery_request) so this transport stays a pure carrier, not a
// second place request semantics are decided.
//
// Client::request() always sends exactly one ACF request per call, via the
// single-message Frame codec above — so a Server it talks to always sees
// exactly one message in that datagram, and always answers with exactly one
// message bundled in its MultiFrame reply (see Server::serve() above),
// which decode_frame below decodes correctly. §12.9.1.1's "shall support
// multiple requests in one frame" is a requirement on the RC *Server* side
// (which Server above now implements via MultiFrame/decode_acf_messages,
// cpp-RCP-04-fresh) — this Client intentionally does not add a batching API
// of its own client-side in this pass.
class Client {
public:
    Client(avtp::StreamId stream_id, const char* server_host,
           uint16_t server_port = kAnnexJControlPort)
        : stream_id_(stream_id), fd_(-1) {
        fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (fd_ < 0) return;

        sockaddr_in sa{};
        sa.sin_family = AF_INET;
        sa.sin_port   = htons(server_port);
        ::inet_pton(AF_INET, server_host, &sa.sin_addr);

        if (::connect(fd_, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) < 0) {
            ::close(fd_);
            fd_ = -1;
            return;
        }
        read_thread_ = std::thread([this]{ read_loop(); });
    }

    ~Client() { auto ec = close(); (void)ec; }

    // request sends one ACF request — ABB or GBB per req.info's own
    // acf_msg_type, wrapped in the AVTPDU header rcp/avtp.hpp defines for
    // whichever kind `use_tscf` selects — and blocks until the matching
    // response arrives or `ctx` is done. message_timestamp is only sent
    // (and only meaningful) when req.acf_msg_type is kAcfMsgTypeGbb.
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
        out.payload           = req_payload;

        const uint32_t key = pending_key(req.byte_bus_id, req.transaction_num);
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

        auto frame_bytes = encode_frame(out);
        auto wire_bytes  = encode_annexj_datagram(++encap_seq_, frame_bytes);
        if (::send(fd_, wire_bytes.data(), wire_bytes.size(), 0) < 0) {
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

    // last_sent_encap_seq/last_recv_encap_seq expose the Annex J
    // encapsulation sequence number (this file's header comment) this
    // Client most recently sent/received — raw counter values only, no
    // loss-detection semantics implied or implemented (kEncapSeqLen's own
    // comment above).
    uint32_t last_sent_encap_seq() const noexcept { return encap_seq_.load(); }
    uint32_t last_recv_encap_seq() const noexcept { return last_recv_encap_seq_.load(); }

private:
    avtp::StreamId stream_id_;
    int  fd_;
    std::atomic<bool>     closed_{false};
    std::atomic<uint16_t> seq_{0};
    std::atomic<uint32_t> encap_seq_{0};          // Annex J encapsulation sequence number, outgoing
    std::atomic<uint32_t> last_recv_encap_seq_{0}; // most recent one seen on an inbound datagram
    std::mutex mu_;
    std::map<uint32_t, std::shared_ptr<std::promise<Frame>>> pending_;
    std::thread read_thread_;

    void read_loop() {
        std::vector<uint8_t> buf(kMaxDatagram);
        while (!closed_.load()) {
            ssize_t n = ::recv(fd_, buf.data(), buf.size(), 0);
            if (n <= 0) break;

            // Strip the Annex J encapsulation sequence number before
            // handing the remainder to the existing AVTPDU/ACF decode path.
            uint32_t       in_encap_seq = 0;
            const uint8_t* avtpdu       = nullptr;
            size_t         avtpdu_len   = 0;
            if (decode_annexj_datagram(buf.data(), static_cast<size_t>(n),
                                        in_encap_seq, avtpdu, avtpdu_len))
                continue;
            last_recv_encap_seq_.store(in_encap_seq);

            Frame resp;
            if (decode_frame(avtpdu, avtpdu_len, resp)) continue;

            const uint32_t key = pending_key(resp.info.byte_bus_id, resp.info.transaction_num);
            std::lock_guard<std::mutex> lk(mu_);
            auto it = pending_.find(key);
            if (it != pending_.end()) {
                it->second->set_value(std::move(resp));
                pending_.erase(it);
            }
        }
    }
};

#else // !RCP_UDP_POSIX (Windows stub)

struct Frame {
    bool                   use_tscf        = false;
    avtp::StreamId         stream_id{};
    uint16_t               sequence_num    = 0;
    bool                   timestamp_valid = false;
    uint32_t               avtp_timestamp  = 0;
    acf::AcfMessageInfo    info{};
    uint64_t               message_timestamp = 0;
    std::vector<uint8_t>   payload;
};

inline std::vector<uint8_t> encode_frame(const Frame&) { return {}; }
inline std::error_code decode_frame(const uint8_t*, size_t, Frame&) {
    return std::make_error_code(std::errc::function_not_supported);
}

class Server {
public:
    // Same shape as the real POSIX Server::Handler above — see that one's
    // own doc comment for why it is frame-level, not single-message.
    using Handler = std::function<size_t(size_t, avtp::StreamId, uint8_t,
                                          const std::vector<uint8_t>&,
                                          std::vector<FrameResponse>&)>;

    Server(avtp::StreamId, const char*, uint16_t = kAnnexJControlPort) {}
    std::string addr_string() const { return {}; }
    uint16_t    port()        const { return 0; }
    void set_handler(Handler) {}
    void close() {}
    bool ok() const noexcept { return false; }
    uint32_t last_recv_encap_seq(size_t) const { return 0; }
};

class Client {
public:
    Client(avtp::StreamId, const char*, uint16_t = kAnnexJControlPort) {}
    std::error_code request(const rcp::Context&, const acf::AcfMessageInfo&,
                             const std::vector<uint8_t>&,
                             acf::AcfMessageInfo&, std::vector<uint8_t>&,
                             bool = false, uint32_t = 0, uint64_t = 0) {
        return std::make_error_code(std::errc::function_not_supported);
    }
    std::error_code close() { return {}; }
    bool ok() const noexcept { return false; }
    uint32_t last_sent_encap_seq() const noexcept { return 0; }
    uint32_t last_recv_encap_seq() const noexcept { return 0; }
};

#endif // RCP_UDP_POSIX

} // namespace udp
} // namespace rcp
