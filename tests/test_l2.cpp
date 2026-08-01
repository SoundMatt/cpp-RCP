// fusa:test REQ-L2-001
// fusa:test REQ-L2-002
// fusa:test REQ-L2-003
// fusa:test REQ-L2-004
// fusa:test REQ-L2-005
// fusa:test REQ-L2-006
// fusa:test REQ-L2-007

// Tests for rcp/l2.hpp — the native IEEE 1722-over-Ethernet (raw L2)
// transport added alongside the rcp/udp.hpp Annex J conformance fix. Every
// TEST_CASE below exercises pure encode/decode functions only (EthHeader,
// Frame, MultiFrame, and the combined L2 wire-frame codec) — no AF_PACKET
// socket is opened, so this file compiles and runs on every platform this
// repository targets (including macOS and Windows, where rcp/l2.hpp's own
// Server/Client are the function_not_supported stub — see that header's
// own comment) with no elevated privileges and no Linux requirement.
//
// The real raw-socket Server/Client round trip (needs CAP_NET_RAW/root and
// a real Linux interface pair) is exercised separately by
// tests/l2_veth_roundtrip.cpp, run only by the dedicated "l2-veth" CI job
// (.github/workflows/ci.yml) against a real `veth` pair — not part of this
// file or the normal `ctest` run.

#include <catch2/catch_test_macros.hpp>
#include <rcp/l2.hpp>

using namespace rcp;
using namespace rcp::l2;

namespace {

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

MacAddress make_mac(uint8_t seed) {
    MacAddress mac{};
    for (auto& b : mac) b = seed++;
    return mac;
}

} // namespace

// ── Ethernet header codec (pure, no socket) ──────────────────────────────────

TEST_CASE("encode_eth_header/decode_eth_header round-trip dst/src MAC and EtherType",
          "[l2][REQ-L2-001]") {
    MacAddress dst = make_mac(0x02);
    MacAddress src = make_mac(0xAA);

    auto bytes = encode_eth_header(dst, src);
    REQUIRE(bytes.size() == kEthHeaderLen);

    EthHeader out;
    REQUIRE_FALSE(decode_eth_header(bytes.data(), bytes.size(), out));
    REQUIRE(out.dst == dst);
    REQUIRE(out.src == src);
    REQUIRE(out.ethertype == kEtherType);
}

TEST_CASE("decode_eth_header rejects a buffer shorter than 14 bytes", "[l2][REQ-L2-002]") {
    MacAddress dst = make_mac(0x02);
    MacAddress src = make_mac(0xAA);
    auto bytes = encode_eth_header(dst, src);

    EthHeader out;
    auto ec = decode_eth_header(bytes.data(), bytes.size() - 1, out);
    REQUIRE(ec);
    REQUIRE(ec == avtp::make_error_code(avtp::AvtpErrc::short_buffer));
}

// ── Frame encode/decode (pure codec, no sockets) ─────────────────────────────

TEST_CASE("Frame round-trips an NTSCF-framed ACF_ABB request", "[l2][REQ-L2-003]") {
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
    REQUIRE(out.payload == f.payload);
}

TEST_CASE("Frame round-trips a TSCF-framed request with avtp_timestamp", "[l2][REQ-L2-004]") {
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

// ── Full L2 wire frame (Ethernet header + AVTPDU) ────────────────────────────

TEST_CASE("encode_l2_frame/decode_l2_frame round-trip dst/src MAC, EtherType, and the AVTPDU",
          "[l2][REQ-L2-003]") {
    MacAddress dst = make_mac(0x10);
    MacAddress src = make_mac(0x20);

    Frame f;
    f.use_tscf     = false;
    f.stream_id    = make_stream_id(0x02, 0x5555);
    f.sequence_num = 3;
    f.info         = standard_request(/*bus_id=*/4, /*transaction_num=*/12, /*write=*/true);
    f.payload      = {0xAA, 0xBB, 0xCC};

    auto bytes = encode_l2_frame(dst, src, f);
    REQUIRE(bytes.size() == kEthHeaderLen + encode_frame(f).size());

    EthHeader hdr;
    Frame     out;
    REQUIRE_FALSE(decode_l2_frame(bytes.data(), bytes.size(), hdr, out));
    REQUIRE(hdr.dst == dst);
    REQUIRE(hdr.src == src);
    REQUIRE(hdr.ethertype == kEtherType);
    REQUIRE(out.info.byte_bus_id == 4);
    REQUIRE(out.info.transaction_num == 12);
    REQUIRE(out.payload == f.payload);
}

TEST_CASE("decode_l2_frame rejects a frame whose EtherType is not 0x22F0",
          "[l2][REQ-L2-005]") {
    MacAddress dst = make_mac(0x10);
    MacAddress src = make_mac(0x20);

    Frame f;
    f.stream_id = make_stream_id(0x01, 1);
    f.info      = standard_request(1, 1);
    auto bytes  = encode_l2_frame(dst, src, f);

    // Corrupt the EtherType field (bytes 12-13) to something other than
    // 0x22F0 — e.g. 0x0800 (IPv4).
    bytes[12] = 0x08;
    bytes[13] = 0x00;

    EthHeader hdr;
    Frame     out;
    auto ec = decode_l2_frame(bytes.data(), bytes.size(), hdr, out);
    REQUIRE(ec);
    REQUIRE(ec == make_error_code(L2Errc::bad_ethertype));
}

TEST_CASE("decode_l2_frame rejects a buffer shorter than the Ethernet header",
          "[l2][REQ-L2-006]") {
    MacAddress dst = make_mac(0x10);
    MacAddress src = make_mac(0x20);

    Frame f;
    f.stream_id = make_stream_id(0x01, 1);
    f.info      = standard_request(1, 1);
    auto bytes  = encode_l2_frame(dst, src, f);

    EthHeader hdr;
    Frame     out;
    REQUIRE(decode_l2_frame(bytes.data(), kEthHeaderLen - 1, hdr, out));
}

// ── MultiFrame — multiple ACF requests in one L2 frame ───────────────────────

TEST_CASE("encode_l2_multi_frame/decode_l2_multi_frame round-trip two ACF_ABB messages "
          "packed into one Ethernet frame",
          "[l2][REQ-L2-007]") {
    MacAddress dst = make_mac(0x30);
    MacAddress src = make_mac(0x40);

    // Payloads are quadlet-aligned (4 bytes each), same precondition
    // rcp/udp.hpp's own equivalent test documents (see
    // acf::decode_acf_messages's own scope-limit comment).
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

    auto bytes = encode_l2_multi_frame(dst, src, f);

    EthHeader  hdr;
    MultiFrame out;
    REQUIRE_FALSE(decode_l2_multi_frame(bytes.data(), bytes.size(), hdr, out));
    REQUIRE(hdr.dst == dst);
    REQUIRE(hdr.src == src);
    REQUIRE(out.messages.size() == 2);
    REQUIRE(out.messages[0].info.byte_bus_id == 1);
    REQUIRE(out.messages[0].payload == a.payload);
    REQUIRE(out.messages[1].info.byte_bus_id == 2);
    REQUIRE(out.messages[1].payload == b.payload);
}

// ── Non-Linux stub surface (Windows/macOS — see rcp/l2.hpp's own comment) ───

#if !defined(RCP_L2_LINUX)
TEST_CASE("Non-Linux stub reports function_not_supported", "[l2]") {
    Server server(make_stream_id(0x02, 1), "eth0");
    REQUIRE_FALSE(server.ok());

    MacAddress dest_mac{};
    Client client(make_stream_id(0x03, 1), "eth0", dest_mac);
    REQUIRE_FALSE(client.ok());

    auto ctx = Context::background();
    acf::AcfMessageInfo   resp;
    std::vector<uint8_t>  resp_payload;
    auto ec = client.request(ctx, standard_request(1, 1), {}, resp, resp_payload);
    REQUIRE(ec == std::make_error_code(std::errc::function_not_supported));
}
#endif // !RCP_L2_LINUX
