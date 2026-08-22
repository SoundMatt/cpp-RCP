// fusa:test REQ-UDP-001
// fusa:test REQ-UDP-002
// fusa:test REQ-UDP-003
// fusa:test REQ-UDP-004
// fusa:test REQ-UDP-005
// fusa:test REQ-UDP-006
// fusa:test REQ-UDP-007
// fusa:test REQ-UDP-008
// fusa:test REQ-UDP-009
// fusa:test REQ-UDP-010
// fusa:test REQ-UDP-011
// fusa:test REQ-UDP-012
// fusa:test REQ-UDP-013
// fusa:test REQ-UDP-014

// Tests for rcp/udp.hpp — the native IEEE 1722-over-UDP/IP transport
// (ROADMAP.md milestone 57, "Native Transport Rebuild — UDP/IP (Annex J)",
// v2.13.0). See that header's own comment for why no legacy-shim split file
// was needed for this rebuild, unlike rcp/mock.hpp's at v2.12.0.
//
// This file also covers cpp-RCP issue #129 Phase 5 wave 1's own
// dispatch-wiring fix: udp::Server::Handler is now FRAME-level (raw
// ACF-region bytes in, std::vector<FrameResponse> out — matching
// rcp::mock::Server::dispatch_frame()/dispatch_frame_e2e()'s own shared
// shape) rather than the old single-already-isolated-message shape that
// matched rcp::mock::Server::dispatch() alone. The "Server wired to
// mock::Server::..." tests below (search that string) are the NEW coverage
// this fix adds — proving a multi-member, Table-24-suppressed, conditional-
// opcode, or E2E-CRC-protected request arriving over real UDP now gets
// EXACTLY the behavior dispatching it directly against rcp::mock::Server
// would, not a silently downgraded default. Every other TEST_CASE below is
// pre-existing coverage, ported onto the new frame-level Handler shape.

#include <catch2/catch_test_macros.hpp>
#include <rcp/mock.hpp>
#include <rcp/udp.hpp>

#include <algorithm>
#include <chrono>

using namespace rcp;
using namespace rcp::udp;
using rcp::endpoint::WriteSemantics;

namespace {

#if defined(RCP_UDP_POSIX)
// A bound UDP socket that accepts datagrams but never replies. Used only by
// the timeout test below, so a Client waiting on it genuinely times out
// instead of racing an OS-level "port unreachable" ICMP notification against
// an already-closed port (platform-dependent and not what REQ-UDP-010 means
// to exercise).
class BlackHole {
public:
    BlackHole() {
        fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
        sockaddr_in sa{};
        sa.sin_family      = AF_INET;
        sa.sin_port        = 0;
        sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        ::bind(fd_, reinterpret_cast<sockaddr*>(&sa), sizeof(sa));
    }
    ~BlackHole() { if (fd_ >= 0) ::close(fd_); }

    uint16_t port() const {
        sockaddr_in sa{};
        socklen_t len = sizeof(sa);
        ::getsockname(fd_, reinterpret_cast<sockaddr*>(&sa), &len);
        return ntohs(sa.sin_port);
    }

private:
    int fd_ = -1;
};
#endif

avtp::StreamId make_stream_id(uint8_t mac_seed, uint16_t suffix) {
    avtp::StreamId id;
    for (auto& b : id.mac) b = mac_seed++;
    id.suffix = suffix;
    return id;
}

acf::AcfMessageInfo standard_request(avtp::ByteBusId bus_id, uint8_t transaction_num,
                                      bool write = false, uint16_t read_size = 0) {
    return acf::make_standard_request(bus_id, transaction_num, write, read_size);
}

#if defined(RCP_UDP_POSIX)
// make_echo_handler builds a udp::Server::Handler that decodes every ACF
// member out of the raw frame bytes Server::serve() now hands it
// (acf::decode_acf_messages — this file's own helper tests use, not any
// mock.hpp dispatch machinery) and calls `build` once per member to
// produce its FrameResponse — the generic "decode, respond per member"
// shape several of the pre-existing tests below reuse instead of the old
// single-message Handler contract udp::Server::Handler used to have.
template <typename BuildFn>
Server::Handler make_echo_handler(BuildFn build) {
    return [build](size_t /*client*/, avtp::StreamId /*stream_id*/, uint8_t /*sequence_num*/,
                    const std::vector<uint8_t>& frame, std::vector<FrameResponse>& out) -> size_t {
        std::vector<acf::AcfEntry> members;
        if (acf::decode_acf_messages(frame.data(), frame.size(), members)) return 0;
        out.reserve(members.size());
        for (auto& m : members) out.push_back(build(m.info, m.payload));
        return out.size();
    };
}

// forward_to_mock builds a udp::Server::Handler that hands `frame` straight
// to `dispatch` (typically a lambda closing over an rcp::mock::Server and
// calling its own dispatch_frame()/dispatch_frame_e2e()) and translates
// each rcp::mock::FrameMemberResult it gets back into a udp::FrameResponse
// — exactly the "small glue lambda" this file's own udp.hpp header comment
// (Server::Handler's own doc comment) describes as the intended way to
// wire an in-process rcp::mock::Server as this transport's handler.
template <typename DispatchFn>
Server::Handler forward_to_mock(DispatchFn dispatch) {
    return [dispatch](size_t client, avtp::StreamId stream_id, uint8_t sequence_num,
                       const std::vector<uint8_t>& frame, std::vector<FrameResponse>& out) -> size_t {
        std::vector<mock::FrameMemberResult> results;
        size_t n = dispatch(client, stream_id, sequence_num, frame, results);
        out.reserve(results.size());
        for (auto& r : results) {
            FrameResponse fr;
            fr.info    = std::move(r.response);
            fr.payload = std::move(r.response_payload);
            out.push_back(std::move(fr));
        }
        return n;
    };
}
#endif

} // namespace

// Every TEST_CASE below exercises the real POSIX implementation (Frame
// codec, Server, Client) — on Windows, rcp/udp.hpp's own header comment
// documents that all of these are the function_not_supported stub, so
// there is nothing real left for REQ-UDP-001..012 to exercise there.
#if defined(RCP_UDP_POSIX)

// ── Frame encode/decode (pure codec, no sockets) ─────────────────────────────

TEST_CASE("Frame round-trips an NTSCF-framed ACF_ABB request", "[udp][REQ-UDP-001]") {
    Frame f;
    f.use_tscf     = false;
    f.stream_id    = make_stream_id(0x02, 0x1234);
    f.sequence_num = 7;
    f.info         = standard_request(/*bus_id=*/3, /*transaction_num=*/9, /*write=*/true);
    f.payload      = {0x01, 0x02, 0x03};

    auto bytes = encode_frame(f);
    REQUIRE(bytes[0] == avtp::kSubtypeNtscf);

    Frame out;
    REQUIRE_FALSE(decode_frame(bytes.data(), bytes.size(), out));
    REQUIRE_FALSE(out.use_tscf);
    REQUIRE(out.stream_id == f.stream_id);
    REQUIRE(out.sequence_num == f.sequence_num);
    REQUIRE(out.info.byte_bus_id == f.info.byte_bus_id);
    REQUIRE(out.info.transaction_num == f.info.transaction_num);
    REQUIRE(out.info.op == f.info.op);
    REQUIRE(out.payload == f.payload);
}

TEST_CASE("Frame round-trips a TSCF-framed request with avtp_timestamp", "[udp][REQ-UDP-002]") {
    Frame f;
    f.use_tscf        = true;
    f.stream_id       = make_stream_id(0xAA, 0x0001);
    f.sequence_num    = 42;
    f.timestamp_valid = true;
    f.avtp_timestamp  = 0xCAFEBABE;
    f.info            = standard_request(/*bus_id=*/1, /*transaction_num=*/5);
    f.payload         = {0xFF};

    auto bytes = encode_frame(f);
    REQUIRE(bytes[0] == avtp::kSubtypeTscf);

    Frame out;
    REQUIRE_FALSE(decode_frame(bytes.data(), bytes.size(), out));
    REQUIRE(out.use_tscf);
    REQUIRE(out.timestamp_valid);
    REQUIRE(out.avtp_timestamp == 0xCAFEBABE);
    REQUIRE(out.info.byte_bus_id == 1);
}

TEST_CASE("Frame round-trips an ACF_GBB message_timestamp", "[udp][REQ-UDP-003]") {
    Frame f;
    f.use_tscf           = false;
    f.stream_id          = make_stream_id(0x10, 0x2222);
    f.sequence_num       = 1;
    f.info               = standard_request(/*bus_id=*/2, /*transaction_num=*/1);
    f.info.acf_msg_type  = acf::kAcfMsgTypeGbb;
    f.info.mtv           = true;
    f.message_timestamp  = 0x1122334455667788ULL;
    f.payload            = {0xAB, 0xCD};

    auto bytes = encode_frame(f);

    Frame out;
    REQUIRE_FALSE(decode_frame(bytes.data(), bytes.size(), out));
    REQUIRE(out.info.acf_msg_type == acf::kAcfMsgTypeGbb);
    REQUIRE(out.info.mtv);
    REQUIRE(out.message_timestamp == 0x1122334455667788ULL);
    REQUIRE(out.payload == f.payload);
}

TEST_CASE("decode_frame rejects a buffer shorter than the AVTPDU header", "[udp][REQ-UDP-004]") {
    Frame f;
    f.stream_id = make_stream_id(0x01, 1);
    f.info      = standard_request(1, 1);
    auto bytes  = encode_frame(f);

    Frame out;
    REQUIRE(decode_frame(bytes.data(), avtp::kNtscfHeaderLen - 1, out));
}

TEST_CASE("decode_frame rejects a buffer shorter than the ACF common header",
          "[udp][REQ-UDP-005]") {
    Frame f;
    f.stream_id = make_stream_id(0x01, 1);
    f.info      = standard_request(1, 1);
    auto bytes  = encode_frame(f);

    Frame out;
    REQUIRE(decode_frame(bytes.data(), avtp::kNtscfHeaderLen + 1, out));
}

TEST_CASE("decode_frame rejects an unrecognized ACF message type", "[udp][REQ-UDP-006]") {
    Frame f;
    f.stream_id = make_stream_id(0x01, 1);
    f.info      = standard_request(1, 1);
    auto bytes  = encode_frame(f);
    bytes[avtp::kNtscfHeaderLen] = 0x00; // neither kAcfMsgTypeAbb nor kAcfMsgTypeGbb

    Frame out;
    REQUIRE(decode_frame(bytes.data(), bytes.size(), out));
}

TEST_CASE("decode_frame rejects a buffer whose control_data_length disagrees with its actual size",
          "[udp][REQ-UDP-006]") {
    // cpp-RCP-A3: control_data_length was decoded but never checked against
    // the buffer it actually came with.
    Frame f;
    f.stream_id = make_stream_id(0x01, 2);
    f.info      = standard_request(1, 1);
    f.payload   = {0x01, 0x02, 0x03};
    auto bytes  = encode_frame(f);
    bytes.push_back(0xFF); // one extra byte the NTSCF header's own length field does not account for

    Frame out;
    auto ec = decode_frame(bytes.data(), bytes.size(), out);
    REQUIRE(ec);
    REQUIRE(ec == avtp::make_error_code(avtp::AvtpErrc::length_mismatch));
}

// ── Annex J encapsulation sequence number (pure codec, no sockets) ──────────
// See rcp/udp.hpp's own header comment for the two independent public
// secondary sources (a Wireshark issue tracker discussion and the COVESA
// Open1722 reference implementation) this 4-byte-prefix reading of Annex J
// rests on — this codebase has no verified access to the paywalled IEEE
// 1722-2016 standard text itself.

TEST_CASE("encode_annexj_datagram/decode_annexj_datagram round-trip the sequence number "
          "and AVTPDU bytes unchanged",
          "[udp][REQ-UDP-013]") {
    Frame f;
    f.stream_id = make_stream_id(0x02, 1);
    f.info      = standard_request(1, 1);
    f.payload   = {0x01, 0x02, 0x03};
    auto avtpdu = encode_frame(f);

    auto wire = encode_annexj_datagram(0xDEADBEEF, avtpdu);
    REQUIRE(wire.size() == kEncapSeqLen + avtpdu.size());

    uint32_t       out_seq = 0;
    const uint8_t* out_avtpdu = nullptr;
    size_t         out_len = 0;
    REQUIRE_FALSE(decode_annexj_datagram(wire.data(), wire.size(), out_seq, out_avtpdu, out_len));
    REQUIRE(out_seq == 0xDEADBEEF);
    REQUIRE(out_len == avtpdu.size());
    REQUIRE(std::equal(avtpdu.begin(), avtpdu.end(), out_avtpdu));
}

TEST_CASE("decode_annexj_datagram rejects a buffer shorter than the 4-byte sequence number",
          "[udp][REQ-UDP-013]") {
    std::vector<uint8_t> short_buf = {0x01, 0x02, 0x03};
    uint32_t       out_seq = 0;
    const uint8_t* out_avtpdu = nullptr;
    size_t         out_len = 0;
    auto ec = decode_annexj_datagram(short_buf.data(), short_buf.size(), out_seq, out_avtpdu, out_len);
    REQUIRE(ec);
    REQUIRE(ec == avtp::make_error_code(avtp::AvtpErrc::short_buffer));
}

TEST_CASE("encode_annexj_datagram produces a monotonically increasing wire prefix "
          "when a caller increments its own counter",
          "[udp][REQ-UDP-013]") {
    std::vector<uint8_t> avtpdu = {0xAA};
    uint32_t prev_seq = 0;
    for (uint32_t i = 1; i <= 5; ++i) {
        auto wire = encode_annexj_datagram(i, avtpdu);
        uint32_t       out_seq = 0;
        const uint8_t* out_avtpdu = nullptr;
        size_t         out_len = 0;
        REQUIRE_FALSE(decode_annexj_datagram(wire.data(), wire.size(), out_seq, out_avtpdu, out_len));
        REQUIRE(out_seq == i);
        REQUIRE(out_seq > prev_seq);
        prev_seq = out_seq;
    }
}

// ── MultiFrame — multiple ACF requests in one AVTPDU (cpp-RCP-04-fresh) ──────

TEST_CASE("MultiFrame round-trips two ACF_ABB messages packed into one AVTPDU",
          "[udp][REQ-UDP-001]") {
    // Payloads are quadlet-aligned (4 bytes each) so the first message's
    // acf_msg_length is byte-exact and the second message's boundary can be
    // found precisely — see acf::decode_acf_messages's own documented scope
    // limit for the non-aligned case.
    MultiFrame f;
    f.use_tscf     = false;
    f.stream_id    = make_stream_id(0x02, 0x5555);
    f.sequence_num = 3;

    acf::AcfEntry a;
    a.info    = standard_request(/*bus_id=*/1, /*transaction_num=*/10, /*write=*/true);
    a.payload = {0x01, 0x02, 0x03, 0x04};
    acf::AcfEntry b;
    b.info    = standard_request(/*bus_id=*/2, /*transaction_num=*/20, /*write=*/false);
    b.payload = {0x05, 0x06, 0x07, 0x08};
    f.messages = {a, b};

    auto bytes = encode_multi_frame(f);

    MultiFrame out;
    REQUIRE_FALSE(decode_multi_frame(bytes.data(), bytes.size(), out));
    REQUIRE(out.messages.size() == 2);
    REQUIRE(out.messages[0].info.byte_bus_id == 1);
    REQUIRE(out.messages[0].info.transaction_num == 10);
    REQUIRE(out.messages[0].payload == a.payload);
    REQUIRE(out.messages[1].info.byte_bus_id == 2);
    REQUIRE(out.messages[1].info.transaction_num == 20);
    REQUIRE(out.messages[1].payload == b.payload);
}

// ── Server (real UDP sockets, loopback) ──────────────────────────────────────

TEST_CASE("Server dispatches a decoded request through its handler and answers the sender",
          "[udp][REQ-UDP-007]") {
    Server server(make_stream_id(0x02, 1), "127.0.0.1", 0);
    REQUIRE(server.ok());

    server.set_handler(make_echo_handler([](const acf::AcfMessageInfo& req, const std::vector<uint8_t>& payload) {
        FrameResponse r;
        r.payload = payload;
        r.info    = acf::make_response(req, acf::ResponseKind::ReadResponse);
        return r;
    }));

    Client client(make_stream_id(0x03, 1), "127.0.0.1", server.port());
    REQUIRE(client.ok());

    auto req = standard_request(/*bus_id=*/5, /*transaction_num=*/11);
    acf::AcfMessageInfo   resp;
    std::vector<uint8_t>  resp_payload;
    auto ctx = Context::with_timeout(std::chrono::seconds(2));
    auto ec  = client.request(ctx, req, {0x11, 0x22}, resp, resp_payload);

    REQUIRE_FALSE(ec);
    REQUIRE(acf::response_kind_of(resp) == acf::ResponseKind::ReadResponse);
    REQUIRE(resp.byte_bus_id == 5);
    REQUIRE(resp.transaction_num == 11);
    REQUIRE(resp_payload == std::vector<uint8_t>{0x11, 0x22});

    server.close();
    client.close();
}

TEST_CASE("Server handles multiple requests packed into a single datagram individually "
          "(extraction §12.9.1.1)",
          "[udp][REQ-UDP-007]") {
    // rcp::udp::Client always sends one request per datagram (see Client's
    // own header comment), so this test drives Server with a raw socket to
    // actually exercise the multi-request-in-one-frame path (cpp-RCP-04-fresh).
    Server server(make_stream_id(0x02, 7), "127.0.0.1", 0);
    REQUIRE(server.ok());

    std::vector<size_t> seen_bus_ids;
    server.set_handler(make_echo_handler([&](const acf::AcfMessageInfo& req, const std::vector<uint8_t>& payload) {
        seen_bus_ids.push_back(req.byte_bus_id);
        FrameResponse r;
        r.payload = payload;
        r.info    = acf::make_response(req, acf::ResponseKind::ReadResponse);
        return r;
    }));

    MultiFrame req_frame;
    req_frame.use_tscf     = false;
    req_frame.stream_id    = make_stream_id(0x03, 7);
    req_frame.sequence_num = 1;

    acf::AcfEntry req_a;
    req_a.info    = standard_request(/*bus_id=*/1, /*transaction_num=*/1);
    req_a.payload = {0xAA, 0xAA, 0xAA, 0xAA};
    acf::AcfEntry req_b;
    req_b.info    = standard_request(/*bus_id=*/2, /*transaction_num=*/2);
    req_b.payload = {0xBB, 0xBB, 0xBB, 0xBB};
    req_frame.messages = {req_a, req_b};

    // Since this drives Server with a raw socket rather than rcp::udp::Client,
    // the Annex J encapsulation sequence number Server now expects on every
    // inbound datagram (this file's header comment, cpp-RCP UDP Annex J fix)
    // has to be prepended here explicitly too.
    auto req_bytes = encode_annexj_datagram(/*encap_seq=*/0x11223344, encode_multi_frame(req_frame));

    int raw_fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    REQUIRE(raw_fd >= 0);
    sockaddr_in server_addr{};
    server_addr.sin_family      = AF_INET;
    server_addr.sin_port        = htons(server.port());
    server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    auto sent = ::sendto(raw_fd, req_bytes.data(), req_bytes.size(), 0,
                          reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr));
    REQUIRE(sent == static_cast<ssize_t>(req_bytes.size()));

    std::vector<uint8_t> recv_buf(kMaxDatagram);
    ssize_t n = ::recv(raw_fd, recv_buf.data(), recv_buf.size(), 0);
    REQUIRE(n > 0);
    ::close(raw_fd);

    uint32_t       resp_encap_seq = 0;
    const uint8_t* resp_avtpdu     = nullptr;
    size_t         resp_avtpdu_len = 0;
    REQUIRE_FALSE(decode_annexj_datagram(recv_buf.data(), static_cast<size_t>(n),
                                          resp_encap_seq, resp_avtpdu, resp_avtpdu_len));

    MultiFrame resp_frame;
    REQUIRE_FALSE(decode_multi_frame(resp_avtpdu, resp_avtpdu_len, resp_frame));
    REQUIRE(resp_frame.messages.size() == 2);
    REQUIRE(resp_frame.messages[0].info.byte_bus_id == 1);
    REQUIRE(resp_frame.messages[0].payload == req_a.payload);
    REQUIRE(resp_frame.messages[1].info.byte_bus_id == 2);
    REQUIRE(resp_frame.messages[1].payload == req_b.payload);
    REQUIRE(seen_bus_ids == std::vector<size_t>{1, 2}); // both dispatched individually, in order

    // Server's own outgoing encapsulation sequence number counter (distinct
    // from the arbitrary one this test sent inbound) started at 0 and this
    // is its first reply, so it should read 1 — REQ-UDP-013.
    REQUIRE(resp_encap_seq == 1);
    REQUIRE(server.last_recv_encap_seq(0) == 0x11223344);

    server.close();
}

TEST_CASE("Server answers Acknowledge by default when no handler is registered",
          "[udp][REQ-UDP-008]") {
    Server server(make_stream_id(0x02, 2), "127.0.0.1", 0);
    REQUIRE(server.ok());

    Client client(make_stream_id(0x03, 2), "127.0.0.1", server.port());
    REQUIRE(client.ok());

    auto req = standard_request(/*bus_id=*/1, /*transaction_num=*/1);
    acf::AcfMessageInfo   resp;
    std::vector<uint8_t>  resp_payload;
    auto ctx = Context::with_timeout(std::chrono::seconds(2));
    REQUIRE_FALSE(client.request(ctx, req, {}, resp, resp_payload));
    REQUIRE(acf::response_kind_of(resp) == acf::ResponseKind::Acknowledge);

    server.close();
    client.close();
}

TEST_CASE("Server assigns stable per-sender client ids", "[udp][REQ-UDP-009]") {
    Server server(make_stream_id(0x02, 3), "127.0.0.1", 0);
    REQUIRE(server.ok());

    std::vector<size_t> seen;
    server.set_handler([&](size_t client, avtp::StreamId /*stream_id*/, uint8_t /*sequence_num*/,
                            const std::vector<uint8_t>& frame, std::vector<FrameResponse>& out) -> size_t {
        seen.push_back(client);
        std::vector<acf::AcfEntry> members;
        if (acf::decode_acf_messages(frame.data(), frame.size(), members)) return 0;
        for (auto& m : members) {
            FrameResponse r;
            r.info = acf::make_response(m.info, acf::ResponseKind::Acknowledge);
            out.push_back(std::move(r));
        }
        return out.size();
    });

    Client client_a(make_stream_id(0x03, 3), "127.0.0.1", server.port());
    Client client_b(make_stream_id(0x04, 3), "127.0.0.1", server.port());
    REQUIRE(client_a.ok());
    REQUIRE(client_b.ok());

    acf::AcfMessageInfo   resp;
    std::vector<uint8_t>  resp_payload;
    auto ctx = Context::with_timeout(std::chrono::seconds(2));

    REQUIRE_FALSE(client_a.request(ctx, standard_request(1, 1), {}, resp, resp_payload));
    REQUIRE_FALSE(client_a.request(ctx, standard_request(1, 2), {}, resp, resp_payload));
    REQUIRE_FALSE(client_b.request(ctx, standard_request(1, 1), {}, resp, resp_payload));

    REQUIRE(seen.size() == 3);
    REQUIRE(seen[0] == seen[1]); // same sender address, same client id
    REQUIRE(seen[0] != seen[2]); // different sender address, different id

    server.close();
    client_a.close();
    client_b.close();
}

// ── Client (real UDP sockets, loopback) ──────────────────────────────────────

TEST_CASE("Client::request returns ErrTimeout when no response arrives",
          "[udp][REQ-UDP-010]") {
    BlackHole sink; // listens, but never replies
    Client client(make_stream_id(0x03, 4), "127.0.0.1", sink.port());
    REQUIRE(client.ok());

    auto req = standard_request(1, 1);
    acf::AcfMessageInfo   resp;
    std::vector<uint8_t>  resp_payload;
    auto ctx = Context::with_timeout(std::chrono::milliseconds(50));
    auto ec  = client.request(ctx, req, {}, resp, resp_payload);
    REQUIRE(ec == ErrTimeout);

    client.close();
}

TEST_CASE("Client::request correlates concurrent requests by byte_bus_id/transaction_num",
          "[udp][REQ-UDP-011]") {
    Server server(make_stream_id(0x02, 5), "127.0.0.1", 0);
    REQUIRE(server.ok());

    server.set_handler(make_echo_handler([](const acf::AcfMessageInfo& req, const std::vector<uint8_t>&) {
        FrameResponse r;
        r.info = acf::make_response(req, acf::ResponseKind::ReadResponse);
        // avtp::ByteBusId widened to uint16_t (rcp/avtp.hpp v2.19.0 wire
        // conformance pass, issue cpp-RCP-04, since byte_bus_id is an
        // 11-bit wire field) — narrow explicitly for this single-byte test
        // payload rather than relying on an implicit narrowing conversion.
        r.payload = {static_cast<uint8_t>(req.byte_bus_id), req.transaction_num};
        return r;
    }));

    Client client(make_stream_id(0x03, 5), "127.0.0.1", server.port());
    REQUIRE(client.ok());

    auto ctx = Context::with_timeout(std::chrono::seconds(2));

    acf::AcfMessageInfo   resp1, resp2;
    std::vector<uint8_t>  payload1, payload2;
    REQUIRE_FALSE(client.request(ctx, standard_request(1, 1), {}, resp1, payload1));
    REQUIRE_FALSE(client.request(ctx, standard_request(2, 1), {}, resp2, payload2));

    REQUIRE(payload1 == std::vector<uint8_t>{1, 1});
    REQUIRE(payload2 == std::vector<uint8_t>{2, 1});

    server.close();
    client.close();
}

// ── Annex J encapsulation sequence number (real sockets) ────────────────────

TEST_CASE("Client's Annex J encapsulation sequence number increments monotonically "
          "across successive requests, and Server observes it",
          "[udp][REQ-UDP-013]") {
    Server server(make_stream_id(0x02, 9), "127.0.0.1", 0);
    REQUIRE(server.ok());

    server.set_handler(make_echo_handler([](const acf::AcfMessageInfo& req, const std::vector<uint8_t>&) {
        FrameResponse r;
        r.info = acf::make_response(req, acf::ResponseKind::Acknowledge);
        return r;
    }));

    Client client(make_stream_id(0x03, 9), "127.0.0.1", server.port());
    REQUIRE(client.ok());
    REQUIRE(client.last_sent_encap_seq() == 0); // nothing sent yet

    auto ctx = Context::with_timeout(std::chrono::seconds(2));
    acf::AcfMessageInfo   resp;
    std::vector<uint8_t>  resp_payload;

    REQUIRE_FALSE(client.request(ctx, standard_request(1, 1), {}, resp, resp_payload));
    REQUIRE(client.last_sent_encap_seq() == 1);
    REQUIRE(client.last_recv_encap_seq() > 0); // Server's own reply counter, opaque value

    REQUIRE_FALSE(client.request(ctx, standard_request(1, 2), {}, resp, resp_payload));
    REQUIRE(client.last_sent_encap_seq() == 2); // strictly increasing, one per request

    REQUIRE_FALSE(client.request(ctx, standard_request(1, 3), {}, resp, resp_payload));
    REQUIRE(client.last_sent_encap_seq() == 3);

    // Server saw the same client (same UDP sender address) across all three
    // requests, so its per-client last_recv_encap_seq() also strictly
    // increased and reflects the most recent (third) request's own counter.
    REQUIRE(server.last_recv_encap_seq(0) == 3);

    server.close();
    client.close();
}

TEST_CASE("Server/Client default to the Annex J control port (17221) when no port is given",
          "[udp][REQ-UDP-014]") {
    REQUIRE(kAnnexJControlPort == 17221);

    // Bound to 127.0.0.1 with no explicit port argument — exercises the new
    // defaulted constructor parameter, not just the constant's value.
    Server server(make_stream_id(0x02, 10), "127.0.0.1");
    REQUIRE(server.ok());
    REQUIRE(server.port() == kAnnexJControlPort);
    server.close();
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────

TEST_CASE("Server and Client close() are idempotent and requests after close fail",
          "[udp][REQ-UDP-012]") {
    Server server(make_stream_id(0x02, 6), "127.0.0.1", 0);
    Client client(make_stream_id(0x03, 6), "127.0.0.1", server.port());
    REQUIRE(server.ok());
    REQUIRE(client.ok());

    server.close();
    server.close(); // second call must not hang or crash

    REQUIRE_FALSE(client.close());
    REQUIRE_FALSE(client.close()); // second call must not hang or crash

    acf::AcfMessageInfo   resp;
    std::vector<uint8_t>  resp_payload;
    auto ctx = Context::background();
    REQUIRE(client.request(ctx, standard_request(1, 1), {}, resp, resp_payload) == ErrClosed);
}

// ── Server wired to a real rcp::mock::Server (cpp-RCP issue #129, Phase 5 ──
//    wave 1: udp.hpp/mock.hpp dispatch-wiring fix) ─────────────────────────
// Server::Handler used to be shaped to match rcp::mock::Server::dispatch's
// own single-already-isolated-message contract directly — a caller wiring
// it straight to mock::Server::dispatch, the obvious, natural thing to do,
// silently lost Table 24 response suppression, conditional/cancellation-
// opcode routing, and E2E/fragmentation handling for every request that
// arrived over UDP, because dispatch() never sees a whole frame, only one
// already-isolated member. Handler is now frame-level (see udp.hpp's own
// Server::Handler doc comment) and these four tests prove the fix: each
// one drives a REAL rcp::mock::Server, wired via forward_to_mock() above
// (the exact "small glue lambda" pattern Handler's own doc comment
// describes), over a REAL UDP round trip, and checks for the SAME behavior
// dispatching directly against that mock::Server would give — not a
// downgraded default.

TEST_CASE("Server wired to mock::Server::dispatch_frame dispatches a multi-member datagram "
          "exactly as calling dispatch_frame directly would",
          "[udp][mock][REQ-UDP-007][REQ-MOCK-019]") {
    mock::Server sim;
    gpio::PinMask ignored = 0;
    (void)sim.gpio().handle_write(WriteSemantics::Reconfigure, 0xFFFF'FFFFu, ignored);
    REQUIRE_FALSE(sim.advance_to_rcp_configured());

    auto stream_id = make_stream_id(0x02, 0x1010);
    Server server(stream_id, "127.0.0.1", 0);
    REQUIRE(server.ok());
    server.set_handler(forward_to_mock([&](size_t client, avtp::StreamId sid, uint8_t /*sequence_num*/,
                                            const std::vector<uint8_t>& frame,
                                            std::vector<mock::FrameMemberResult>& results) {
        return sim.dispatch_frame(client, sid, frame, results);
    }));

    MultiFrame req_frame;
    req_frame.use_tscf     = false;
    req_frame.stream_id    = stream_id;
    req_frame.sequence_num = 1;

    acf::AcfEntry write_req;
    write_req.info        = standard_request(mock::kGpioByteBusId, /*transaction_num=*/1, /*write=*/true);
    write_req.info.evt_op = static_cast<uint8_t>(WriteSemantics::Or);
    write_req.payload     = gpio::encode_gpio_payload(0x0000'000F);

    acf::AcfEntry read_req;
    read_req.info = standard_request(mock::kGpioByteBusId, /*transaction_num=*/2, /*write=*/false);

    req_frame.messages = {write_req, read_req};

    auto req_bytes = encode_annexj_datagram(/*encap_seq=*/1, encode_multi_frame(req_frame));

    int raw_fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    REQUIRE(raw_fd >= 0);
    sockaddr_in server_addr{};
    server_addr.sin_family      = AF_INET;
    server_addr.sin_port        = htons(server.port());
    server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    auto sent = ::sendto(raw_fd, req_bytes.data(), req_bytes.size(), 0,
                          reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr));
    REQUIRE(sent == static_cast<ssize_t>(req_bytes.size()));

    std::vector<uint8_t> recv_buf(kMaxDatagram);
    ssize_t n = ::recv(raw_fd, recv_buf.data(), recv_buf.size(), 0);
    REQUIRE(n > 0);
    ::close(raw_fd);

    uint32_t       resp_seq = 0;
    const uint8_t* resp_avtpdu = nullptr;
    size_t         resp_len = 0;
    REQUIRE_FALSE(decode_annexj_datagram(recv_buf.data(), static_cast<size_t>(n), resp_seq, resp_avtpdu, resp_len));

    MultiFrame resp;
    REQUIRE_FALSE(decode_multi_frame(resp_avtpdu, resp_len, resp));
    REQUIRE(resp.messages.size() == 2);
    REQUIRE(acf::response_kind_of(resp.messages[0].info) == acf::ResponseKind::WriteResponse);
    REQUIRE(acf::response_kind_of(resp.messages[1].info) == acf::ResponseKind::ReadResponse);

    gpio::PinMask read_back = 0;
    REQUIRE_FALSE(gpio::decode_gpio_payload(resp.messages[1].payload.data(), resp.messages[1].payload.size(),
                                             read_back));
    REQUIRE(read_back == 0x0000'000F);
    REQUIRE(sim.gpio().read() == 0x0000'000F); // the second member's own side effect actually landed

    server.close();
}

TEST_CASE("Server wired to mock::Server::dispatch_frame honors Table 24 response suppression "
          "end-to-end over UDP — a request stream configured with no ack/response routing "
          "produces NO reply datagram at all, not a downgraded default response",
          "[udp][mock][REQ-UDP-007][REQ-RMAP-048][REQ-RMAP-049]") {
    mock::Server sim;
    REQUIRE_FALSE(sim.advance_to_rcp_configured());

    auto client_stream_id = make_stream_id(0x05, 0x4242);
    regmap::RequestStreamConfig cfg;
    cfg.stream_id            = client_stream_id;
    cfg.rx_ack_stream_index  = 0; // REQ-RMAP-048: struct default already 0 ("no acknowledge is to be sent")
    cfg.rx_resp_stream_index = 0; // REQ-RMAP-049: struct DEFAULT is 1 (regmap.hpp's own "a freshly
                                   // reset server can answer discovery before any config is written"
                                   // power-on rationale) — must be set to 0 explicitly here to actually
                                   // exercise Table 24's "no response is to be sent" encoding, applied by
                                   // mock::Server's own suppress_response_per_stream_cfg() (mock.hpp)
                                   // inside dispatch_frame().
    REQUIRE(sim.set_request_stream_cfg({cfg}));

    Server server(make_stream_id(0x02, 1), "127.0.0.1", 0);
    REQUIRE(server.ok());
    server.set_handler(forward_to_mock([&](size_t client, avtp::StreamId sid, uint8_t /*sequence_num*/,
                                            const std::vector<uint8_t>& frame,
                                            std::vector<mock::FrameMemberResult>& results) {
        return sim.dispatch_frame(client, sid, frame, results);
    }));

    Client client(client_stream_id, "127.0.0.1", server.port());
    REQUIRE(client.ok());

    acf::AcfMessageInfo   resp;
    std::vector<uint8_t>  resp_payload;
    auto ctx = Context::with_timeout(std::chrono::milliseconds(200));
    auto ec  = client.request(ctx, standard_request(mock::kGpioByteBusId, 1, /*write=*/false), {}, resp, resp_payload);

    // A genuine, successful GPIO ReadResponse was built and then suppressed
    // at the source (mock::Server's own Table 24 logic) — the client
    // genuinely gets nothing back, not a default Acknowledge or any other
    // stand-in response.
    REQUIRE(ec == ErrTimeout);

    server.close();
    client.close();
}

TEST_CASE("Server wired to mock::Server::dispatch_frame routes a Triggered conditional-opcode "
          "request to Pending admission instead of silently treating it as a Standard request",
          "[udp][mock][REQ-UDP-007][REQ-SRV-016]") {
    mock::Server sim;
    REQUIRE_FALSE(sim.advance_to_rcp_configured());

    auto stream_id = make_stream_id(0x02, 0x7777);
    Server server(stream_id, "127.0.0.1", 0);
    REQUIRE(server.ok());
    server.set_handler(forward_to_mock([&](size_t client, avtp::StreamId sid, uint8_t /*sequence_num*/,
                                            const std::vector<uint8_t>& frame,
                                            std::vector<mock::FrameMemberResult>& results) {
        return sim.dispatch_frame(client, sid, frame, results);
    }));

    Client client(stream_id, "127.0.0.1", server.port());
    REQUIRE(client.ok());

    request::TriggeredStep step;
    step.trigger_source_ep = 1;
    step.trigger_signal_nr = 0;
    step.trigger_threshold = 0;

    acf::AcfMessageInfo trig_info;
    trig_info.acf_msg_type    = acf::kAcfMsgTypeGbb;
    trig_info.byte_bus_id     = mock::kGpioByteBusId;
    trig_info.transaction_num = 3;
    trig_info.evt_ack         = true; // request an Acknowledge so this is observable over the wire
    // mtv left at its own default (false) — a conditional-opcode GBB
    // message's message_timestamp field is repurposed to carry the opcode/
    // params instead of a real timestamp (mock::Server's own
    // peek_conditional_request_type()); mtv must stay clear for that peek
    // to recognize this as one at all.

    const uint64_t ts = request::encode_request_type(request::RequestTypeOpcode::Triggered,
                                                       request::encode_triggered_step_params(step));

    acf::AcfMessageInfo   resp;
    std::vector<uint8_t>  resp_payload;
    auto ctx = Context::with_timeout(std::chrono::seconds(2));
    auto ec  = client.request(ctx, trig_info, {}, resp, resp_payload, /*use_tscf=*/false,
                               /*avtp_timestamp=*/0, /*message_timestamp=*/ts);

    REQUIRE_FALSE(ec);
    // A Pending admission's own Acknowledge shape (REQ-SRV-016) — NOT a
    // WriteResponse/ReadResponse, which would mean the raw opcode/param
    // bytes were misread as an ordinary Standard request's own
    // message_timestamp and dispatched straight to GpioEndpoint instead of
    // being stored as a conditional request.
    REQUIRE(acf::response_kind_of(resp) == acf::ResponseKind::Acknowledge);
    REQUIRE(sim.pending_count(mock::kGpioByteBusId) == 1);

    server.close();
    client.close();
}

TEST_CASE("Server wired to mock::Server::dispatch_frame_e2e validates a genuine E2E CRC and "
          "dispatches the unwrapped request over UDP exactly as dispatch_frame_e2e() would directly",
          "[udp][mock][REQ-UDP-007][REQ-E2E-021]") {
    mock::Server sim;
    gpio::PinMask ignored = 0;
    (void)sim.gpio().handle_write(WriteSemantics::Reconfigure, 0xFFFF'FFFFu, ignored);
    REQUIRE_FALSE(sim.advance_to_rcp_configured());
    sim.registers().generic_configs[mock::kGpioEndpointId - 1].ep_req_crc_enable = true;

    const auto stream_id = make_stream_id(0x02, 0x9999);
    regmap::RequestStreamConfig cfg;
    cfg.stream_id      = stream_id;
    cfg.rx_enforce_e2e = true;
    REQUIRE(sim.set_request_stream_cfg({cfg}));

    Server server(make_stream_id(0x03, 1), "127.0.0.1", 0);
    REQUIRE(server.ok());
    server.set_handler(forward_to_mock([&](size_t client, avtp::StreamId sid, uint8_t sequence_num,
                                            const std::vector<uint8_t>& frame,
                                            std::vector<mock::FrameMemberResult>& results) {
        return sim.dispatch_frame_e2e(client, sid, sequence_num, frame, results);
    }));

    acf::AcfMessageInfo gpio_info;
    gpio_info.byte_bus_id     = mock::kGpioByteBusId;
    gpio_info.transaction_num = 7;
    gpio_info.op              = true;
    gpio_info.evt_op          = static_cast<uint8_t>(WriteSemantics::Or);
    auto payload = gpio::encode_gpio_payload(0x0000'00F0);
    // e2e::wrap_framed() (rcp/e2e.hpp) builds the real, CRC32-protected ACF
    // region bytes — Client::request() has no CRC-wrapping option of its
    // own (this is transport-independent wire content mock::Server's own
    // E2E layer produces/consumes, not something udp.hpp's Frame codec
    // knows about), so this frame is assembled and sent over a raw socket
    // instead, the same pattern the multi-member test above already uses.
    auto wrapped = e2e::wrap_framed(/*is_ntscf_framed=*/true, /*header_octet1=*/0x00, /*tu=*/false, stream_id,
                                     /*avtp_timestamp=*/std::nullopt, gpio_info,
                                     /*message_timestamp=*/std::nullopt, payload);

    avtp::NtscfHeader hdr;
    hdr.stream_id           = stream_id;
    hdr.sequence_num        = 0;
    hdr.control_data_length = static_cast<uint16_t>(wrapped.size());
    auto avtpdu = avtp::encode_ntscf_header(hdr);
    avtpdu.insert(avtpdu.end(), wrapped.begin(), wrapped.end());

    auto req_bytes = encode_annexj_datagram(/*encap_seq=*/1, avtpdu);

    int raw_fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    REQUIRE(raw_fd >= 0);
    sockaddr_in server_addr{};
    server_addr.sin_family      = AF_INET;
    server_addr.sin_port        = htons(server.port());
    server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    auto sent = ::sendto(raw_fd, req_bytes.data(), req_bytes.size(), 0,
                          reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr));
    REQUIRE(sent == static_cast<ssize_t>(req_bytes.size()));

    std::vector<uint8_t> recv_buf(kMaxDatagram);
    ssize_t n = ::recv(raw_fd, recv_buf.data(), recv_buf.size(), 0);
    REQUIRE(n > 0);
    ::close(raw_fd);

    uint32_t       resp_seq = 0;
    const uint8_t* resp_avtpdu = nullptr;
    size_t         resp_len = 0;
    REQUIRE_FALSE(decode_annexj_datagram(recv_buf.data(), static_cast<size_t>(n), resp_seq, resp_avtpdu, resp_len));

    MultiFrame resp;
    REQUIRE_FALSE(decode_multi_frame(resp_avtpdu, resp_len, resp));
    REQUIRE(resp.messages.size() == 1);
    REQUIRE(acf::response_kind_of(resp.messages[0].info) == acf::ResponseKind::WriteResponse);
    REQUIRE(sim.gpio().read() == 0x0000'00F0); // the CRC-unwrapped request's own side effect landed

    server.close();
}

#else // !RCP_UDP_POSIX

// On Windows rcp/udp.hpp has no real transport (see its own header comment)
// — this exercises the function_not_supported stub surface instead, so this
// binary still registers a real assertion rather than reporting "No tests
// ran" (which Catch2, and therefore ctest, treats as a failure).
TEST_CASE("Windows stub reports function_not_supported", "[udp]") {
    Server server(make_stream_id(0x02, 1), "127.0.0.1", 0);
    REQUIRE_FALSE(server.ok());

    Client client(make_stream_id(0x03, 1), "127.0.0.1", 0);
    REQUIRE_FALSE(client.ok());

    auto ctx = Context::background();
    acf::AcfMessageInfo   resp;
    std::vector<uint8_t>  resp_payload;
    auto ec = client.request(ctx, standard_request(1, 1), {}, resp, resp_payload);
    REQUIRE(ec == std::make_error_code(std::errc::function_not_supported));
}

#endif // RCP_UDP_POSIX
