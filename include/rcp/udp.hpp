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
// raw Ethernet, for links where native AVTP framing (destination MAC +
// EtherType 0x22F0) is not available. This implementation's own reading of
// that behavior — consistent with rcp/avtp.hpp's own header comment that its
// NTSCF/TSCF framing is transport-agnostic by design — is that the AVTPDU
// bytes rcp/avtp.hpp and rcp/acf.hpp already produce are carried unmodified
// as the UDP payload; no additional encapsulation header is layered on top
// here, and IP/UDP addressing substitutes for the Ethernet destination
// address a native AVTP link would otherwise use. Full bit-for-bit
// conformance against other TC18/Annex-J implementations is not claimed,
// same as the equivalent disclaimers in rcp/avtp.hpp, rcp/acf.hpp, and
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
    } else {
        avtp::NtscfHeader hdr;
        if (auto ec = avtp::decode_ntscf_header(b, len, hdr)) return ec;
        out.stream_id       = hdr.stream_id;
        out.sequence_num    = hdr.sequence_num;
        out.timestamp_valid = false;
        out.avtp_timestamp  = 0;
        acf_off = avtp::kNtscfHeaderLen;
    }

    if (len < acf_off + 1) return avtp::make_error_code(avtp::AvtpErrc::short_buffer);
    if (b[acf_off] == acf::kAcfMsgTypeGbb) {
        return acf::decode_acf_gbb(b + acf_off, len - acf_off, out.info,
                                    out.message_timestamp, out.payload);
    }
    out.message_timestamp = 0;
    return acf::decode_acf_abb(b + acf_off, len - acf_off, out.info, out.payload);
}

// ── Server ────────────────────────────────────────────────────────────────────
// Server binds a UDP socket, decodes each inbound datagram as a Frame, and
// dispatches the carried ACF request to a caller-supplied Handler — shaped
// to match rcp::mock::Server::dispatch's signature — then encodes and sends
// the handler's response back to the sender under the same header kind
// (NTSCF/TSCF) the request arrived under. Malformed datagrams (short buffer,
// bad subtype, unrecognized ACF message type) are dropped silently, the same
// "drop rather than partially process" choice rcp/discovery.hpp documents
// for its own decode path.
class Server {
public:
    using Handler = std::function<std::error_code(size_t client,
                                                    const acf::AcfMessageInfo& req,
                                                    const std::vector<uint8_t>& req_payload,
                                                    acf::AcfMessageInfo& out_resp,
                                                    std::vector<uint8_t>& out_resp_payload)>;

    Server(avtp::StreamId stream_id, const char* addr, uint16_t port)
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

private:
    avtp::StreamId stream_id_;
    int  fd_;
    std::atomic<bool>     closed_{false};
    std::atomic<uint16_t> seq_{0};
    std::mutex   mu_;
    Handler      handler_;
    std::thread  serve_thread_;

    // client_ids_ assigns each distinct sender address a stable, opaque
    // size_t identity, first-seen order — the same role rcp/regmap.hpp's
    // Ep0 root-client index plays for its own callers, just derived from
    // the UDP sender address instead of an in-process connection index.
    std::map<std::string, size_t> client_ids_;
    size_t next_client_id_ = 0;

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

            Frame req;
            if (decode_frame(buf.data(), static_cast<size_t>(n), req)) continue;

            acf::AcfMessageInfo   out_info;
            std::vector<uint8_t>  out_payload;
            size_t client;
            {
                std::lock_guard<std::mutex> lk(mu_);
                client = client_id_for(from);
                if (handler_) {
                    auto ec = handler_(client, req.info, req.payload, out_info, out_payload);
                    (void)ec; // Handler always populates out_info even on failure,
                              // same contract rcp::mock::Server::dispatch documents.
                } else {
                    out_info = acf::make_response(req.info, acf::ResponseKind::Acknowledge);
                }
            }

            Frame resp;
            resp.use_tscf        = req.use_tscf;
            resp.stream_id       = stream_id_;
            resp.sequence_num    = static_cast<uint16_t>(++seq_);
            resp.timestamp_valid = req.timestamp_valid;
            resp.avtp_timestamp  = req.avtp_timestamp;
            resp.info            = out_info;
            resp.payload         = std::move(out_payload);

            auto out_frame = encode_frame(resp);
            ::sendto(fd_, out_frame.data(), out_frame.size(), 0,
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
class Client {
public:
    Client(avtp::StreamId stream_id, const char* server_host, uint16_t server_port)
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

        auto frame_bytes = encode_frame(out);
        if (::send(fd_, frame_bytes.data(), frame_bytes.size(), 0) < 0) {
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
    int  fd_;
    std::atomic<bool>     closed_{false};
    std::atomic<uint16_t> seq_{0};
    std::mutex mu_;
    std::map<uint16_t, std::shared_ptr<std::promise<Frame>>> pending_;
    std::thread read_thread_;

    void read_loop() {
        std::vector<uint8_t> buf(kMaxDatagram);
        while (!closed_.load()) {
            ssize_t n = ::recv(fd_, buf.data(), buf.size(), 0);
            if (n <= 0) break;

            Frame resp;
            if (decode_frame(buf.data(), static_cast<size_t>(n), resp)) continue;

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
    using Handler = std::function<std::error_code(size_t, const acf::AcfMessageInfo&,
                                                    const std::vector<uint8_t>&,
                                                    acf::AcfMessageInfo&, std::vector<uint8_t>&)>;

    Server(avtp::StreamId, const char*, uint16_t) {}
    std::string addr_string() const { return {}; }
    uint16_t    port()        const { return 0; }
    void set_handler(Handler) {}
    void close() {}
    bool ok() const noexcept { return false; }
};

class Client {
public:
    Client(avtp::StreamId, const char*, uint16_t) {}
    std::error_code request(const rcp::Context&, const acf::AcfMessageInfo&,
                             const std::vector<uint8_t>&,
                             acf::AcfMessageInfo&, std::vector<uint8_t>&,
                             bool = false, uint32_t = 0, uint64_t = 0) {
        return std::make_error_code(std::errc::function_not_supported);
    }
    std::error_code close() { return {}; }
    bool ok() const noexcept { return false; }
};

#endif // RCP_UDP_POSIX

} // namespace udp
} // namespace rcp
