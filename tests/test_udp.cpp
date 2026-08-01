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

#include <catch2/catch_test_macros.hpp>
#include <rcp/udp.hpp>

#include <algorithm>
#include <chrono>

using namespace rcp;
using namespace rcp::udp;

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

    server.set_handler([](size_t, const acf::AcfMessageInfo& req,
                           const std::vector<uint8_t>& req_payload,
                           acf::AcfMessageInfo& out_resp, std::vector<uint8_t>& out_resp_payload) {
        out_resp_payload = req_payload;
        out_resp         = acf::make_response(req, acf::ResponseKind::ReadResponse);
        return std::error_code{};
    });

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
    server.set_handler([&](size_t, const acf::AcfMessageInfo& req,
                            const std::vector<uint8_t>& req_payload,
                            acf::AcfMessageInfo& out_resp, std::vector<uint8_t>& out_resp_payload) {
        seen_bus_ids.push_back(req.byte_bus_id);
        out_resp_payload = req_payload;
        out_resp         = acf::make_response(req, acf::ResponseKind::ReadResponse);
        return std::error_code{};
    });

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
    server.set_handler([&](size_t client, const acf::AcfMessageInfo& req,
                            const std::vector<uint8_t>&,
                            acf::AcfMessageInfo& out_resp, std::vector<uint8_t>&) {
        seen.push_back(client);
        out_resp = acf::make_response(req, acf::ResponseKind::Acknowledge);
        return std::error_code{};
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

    server.set_handler([](size_t, const acf::AcfMessageInfo& req,
                           const std::vector<uint8_t>&,
                           acf::AcfMessageInfo& out_resp, std::vector<uint8_t>& out_resp_payload) {
        out_resp         = acf::make_response(req, acf::ResponseKind::ReadResponse);
        // avtp::ByteBusId widened to uint16_t (rcp/avtp.hpp v2.19.0 wire
        // conformance pass, issue cpp-RCP-04, since byte_bus_id is an
        // 11-bit wire field) — narrow explicitly for this single-byte test
        // payload rather than relying on an implicit narrowing conversion.
        out_resp_payload = {static_cast<uint8_t>(req.byte_bus_id), req.transaction_num};
        return std::error_code{};
    });

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

    server.set_handler([](size_t, const acf::AcfMessageInfo& req,
                           const std::vector<uint8_t>&,
                           acf::AcfMessageInfo& out_resp, std::vector<uint8_t>&) {
        out_resp = acf::make_response(req, acf::ResponseKind::Acknowledge);
        return std::error_code{};
    });

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
