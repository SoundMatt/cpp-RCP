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

// Tests for rcp/udp.hpp — the native IEEE 1722-over-UDP/IP transport
// (ROADMAP.md milestone 57, "Native Transport Rebuild — UDP/IP (Annex J)",
// v2.13.0). See that header's own comment for why no legacy-shim split file
// was needed for this rebuild, unlike rcp/mock.hpp's at v2.12.0.

#include <catch2/catch_test_macros.hpp>
#include <rcp/udp.hpp>

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

// Server/Client exercise real UDP sockets, which only exist on the POSIX
// build (RCP_UDP_POSIX, set by rcp/udp.hpp itself) — on Windows both classes
// are the function_not_supported stub rcp/udp.hpp's own header comment
// documents, so REQ-UDP-007..012 have nothing real to exercise there.
#if defined(RCP_UDP_POSIX)

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
        out_resp_payload = {req.byte_bus_id, req.transaction_num};
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

#endif // RCP_UDP_POSIX
