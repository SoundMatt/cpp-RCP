// fusa:test REQ-L2-001
// fusa:test REQ-L2-002
// fusa:test REQ-L2-003
// fusa:test REQ-L2-004
// fusa:test REQ-L2-005
// fusa:test REQ-L2-006
// fusa:test REQ-L2-007
// fusa:test REQ-L2-009
// fusa:test REQ-L2-010

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
//
// The "── FrameHandler dispatch-wiring gap ──" section below (Phase 5 Wave 1,
// cpp-RCP issue #129) additionally includes <rcp/mock.hpp> — still no socket,
// still privilege-free — to prove the FrameHandler entry point this batch
// adds actually reaches rcp::mock::Server's own Table 24 suppression,
// conditional/cancellation-opcode routing, and E2E dispatch, none of which
// rcp::l2::Server::Handler's pre-existing single-message signature could
// ever reach (it carries no stream_id parameter at all, and always routes
// through mock::Server::dispatch() rather than decode_and_dispatch()/
// dispatch_frame()/dispatch_frame_e2e()).

#include <catch2/catch_test_macros.hpp>
#include <rcp/l2.hpp>
#include <rcp/mock.hpp>

#include <set>

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

// ── is_unicast_mac (content gap vs. c-RCP's rcp_l2_mac_is_unicast(), REQ-L2-011) ──

TEST_CASE("is_unicast_mac reports true for a unicast MAC (I/G bit clear)", "[l2][REQ-L2-010]") {
    MacAddress mac = make_mac(0x02); // 0x02 & 0x01 == 0
    REQUIRE(is_unicast_mac(mac));
}

TEST_CASE("is_unicast_mac reports false for a multicast MAC (I/G bit set)", "[l2][REQ-L2-010]") {
    MacAddress mac = make_mac(0x01); // 0x01 & 0x01 == 1
    REQUIRE_FALSE(is_unicast_mac(mac));
}

TEST_CASE("is_unicast_mac reports false for the all-ones broadcast address", "[l2][REQ-L2-010]") {
    MacAddress mac;
    mac.fill(0xFF);
    REQUIRE_FALSE(is_unicast_mac(mac));
}

// ── pending_key — Client response-correlation key (pure, no socket) ─────────
//
// rcp::l2::Client::request()/read_loop() correlate an inbound response to
// its outstanding request via pending_key(byte_bus_id, transaction_num),
// the same formula rcp/udp.hpp's own top-level pending_key uses (that
// file's own comment documents the collision this widening fixes) — no
// real AF_PACKET socket needed to exercise the pure key-computation logic
// itself (it is unconditional of RCP_L2_LINUX, like is_unicast_mac just
// above), so this stays in this privilege-free file rather than
// tests/l2_veth_roundtrip.cpp (this file's own header comment).
TEST_CASE("pending_key does not collide for byte_bus_id values that differ by a "
          "multiple of 256 and share a transaction_num",
          "[l2]") {
    // byte_bus_id is an 11-bit wire field (0-2047; avtp::ByteBusId's own
    // comment, acf.hpp's detail::kByteBusIdMask) -- 5 and 261 are both
    // wire-legal and differ by exactly 256. A previous version of
    // pending_key returned uint16_t, computing this same left-shift-by-8
    // but then truncating the result back down to 16 bits, so these two
    // collided into the identical map key (0x0507) whenever they shared a
    // transaction_num (cpp-RCP v3.0.0 deep audit finding) — silently
    // misdelivering one Client::request() caller's response to another's
    // promise, or hanging one of them indefinitely.
    REQUIRE(pending_key(5, 7) != pending_key(261, 7));
    REQUIRE(pending_key(5, 7) == pending_key(5, 7));

    // Sweep every byte_bus_id that used to alias to the same 16-bit key
    // under the old truncation (i.e. every value 256 apart, across the
    // whole 11-bit range) crossed with a few transaction_num values, and
    // confirm every (byte_bus_id, transaction_num) pair now maps to a
    // distinct key.
    std::set<uint32_t> seen;
    for (uint32_t bus = 0; bus <= 2047; bus += 256) {
        for (uint32_t txn = 0; txn <= 255; txn += 85) {
            auto key = pending_key(static_cast<avtp::ByteBusId>(bus), static_cast<uint8_t>(txn));
            REQUIRE(seen.insert(key).second); // must be a fresh key, never seen before
        }
    }
}

// ── AVTP envelope-only decode (decode_avtp_frame_header/decode_l2_frame_header) ──

TEST_CASE("decode_avtp_frame_header decodes an NTSCF envelope and reports the raw ACF offset",
          "[l2][REQ-L2-009]") {
    MultiFrame f;
    f.use_tscf     = false;
    f.stream_id    = make_stream_id(0x02, 0x1234);
    f.sequence_num = 7;
    acf::AcfEntry m;
    m.info    = standard_request(/*bus_id=*/3, /*transaction_num=*/9, /*write=*/true);
    m.payload = {0x01, 0x02, 0x03, 0x04};
    f.messages = {m};

    auto bytes = encode_multi_frame(f);

    AvtpFrameHeader hdr;
    REQUIRE_FALSE(decode_avtp_frame_header(bytes.data(), bytes.size(), hdr));
    REQUIRE_FALSE(hdr.use_tscf);
    REQUIRE(hdr.stream_id == f.stream_id);
    REQUIRE(hdr.sequence_num == f.sequence_num);
    REQUIRE(hdr.acf_offset == avtp::kNtscfHeaderLen);

    // The raw bytes at acf_offset must decode identically to what
    // decode_multi_frame() (the full parse) itself produces for the same
    // wire bytes -- this is the exact raw span Server::serve()'s
    // FrameHandler path hands to a caller.
    std::vector<acf::AcfEntry> parsed;
    REQUIRE_FALSE(acf::decode_acf_messages(bytes.data() + hdr.acf_offset,
                                            bytes.size() - hdr.acf_offset, parsed));
    REQUIRE(parsed.size() == 1);
    REQUIRE(parsed[0].info.byte_bus_id == 3);
    REQUIRE(parsed[0].payload == m.payload);
}

TEST_CASE("decode_avtp_frame_header decodes a TSCF envelope with its timestamp fields",
          "[l2][REQ-L2-009]") {
    Frame f;
    f.use_tscf        = true;
    f.stream_id       = make_stream_id(0xAA, 0x0001);
    f.sequence_num    = 42;
    f.timestamp_valid = true;
    f.avtp_timestamp  = 0xCAFEBABE;
    f.info            = standard_request(/*bus_id=*/1, /*transaction_num=*/5);
    f.payload         = {0xFF};

    auto bytes = encode_frame(f);

    AvtpFrameHeader hdr;
    REQUIRE_FALSE(decode_avtp_frame_header(bytes.data(), bytes.size(), hdr));
    REQUIRE(hdr.use_tscf);
    REQUIRE(hdr.timestamp_valid);
    REQUIRE(hdr.avtp_timestamp == 0xCAFEBABE);
    REQUIRE(hdr.acf_offset == avtp::kTscfHeaderLen);
}

TEST_CASE("decode_avtp_frame_header rejects a truncated buffer the same way decode_multi_frame does",
          "[l2][REQ-L2-009]") {
    MultiFrame f;
    f.stream_id = make_stream_id(0x01, 1);
    acf::AcfEntry m;
    m.info = standard_request(1, 1);
    f.messages = {m};
    auto bytes = encode_multi_frame(f);

    AvtpFrameHeader hdr;
    REQUIRE(decode_avtp_frame_header(bytes.data(), avtp::kNtscfHeaderLen - 1, hdr));
}

TEST_CASE("decode_l2_frame_header combines the Ethernet header decode with the AVTP envelope decode",
          "[l2][REQ-L2-009]") {
    MacAddress dst = make_mac(0x10);
    MacAddress src = make_mac(0x20);

    MultiFrame f;
    f.use_tscf     = false;
    f.stream_id    = make_stream_id(0x02, 0x5555);
    f.sequence_num = 3;
    acf::AcfEntry m;
    m.info    = standard_request(/*bus_id=*/4, /*transaction_num=*/12, /*write=*/true);
    m.payload = {0xAA, 0xBB, 0xCC, 0xDD};
    f.messages = {m};

    auto bytes = encode_l2_multi_frame(dst, src, f);

    EthHeader       eth_hdr;
    AvtpFrameHeader avtp_hdr;
    REQUIRE_FALSE(decode_l2_frame_header(bytes.data(), bytes.size(), eth_hdr, avtp_hdr));
    REQUIRE(eth_hdr.dst == dst);
    REQUIRE(eth_hdr.src == src);
    REQUIRE(eth_hdr.ethertype == kEtherType);
    REQUIRE(avtp_hdr.stream_id == f.stream_id);
    REQUIRE(avtp_hdr.sequence_num == 3);

    const size_t acf_off = kEthHeaderLen + avtp_hdr.acf_offset;
    std::vector<acf::AcfEntry> parsed;
    REQUIRE_FALSE(acf::decode_acf_messages(bytes.data() + acf_off, bytes.size() - acf_off, parsed));
    REQUIRE(parsed.size() == 1);
    REQUIRE(parsed[0].info.byte_bus_id == 4);
    REQUIRE(parsed[0].payload == m.payload);
}

TEST_CASE("decode_l2_frame_header rejects a frame whose EtherType is not 0x22F0, same as decode_l2_multi_frame",
          "[l2][REQ-L2-009]") {
    MacAddress dst = make_mac(0x10);
    MacAddress src = make_mac(0x20);

    MultiFrame f;
    f.stream_id = make_stream_id(0x01, 1);
    acf::AcfEntry m;
    m.info = standard_request(1, 1);
    f.messages = {m};
    auto bytes = encode_l2_multi_frame(dst, src, f);
    bytes[12] = 0x08;
    bytes[13] = 0x00;

    EthHeader       eth_hdr;
    AvtpFrameHeader avtp_hdr;
    auto ec = decode_l2_frame_header(bytes.data(), bytes.size(), eth_hdr, avtp_hdr);
    REQUIRE(ec);
    REQUIRE(ec == make_error_code(L2Errc::bad_ethertype));
}

// ── FrameHandler dispatch-wiring gap (Phase 5 Wave 1, cpp-RCP issue #129) ────
// The tests below prove rcp::l2::Server::FrameHandler (set via
// set_frame_handler()) reaches the rcp::mock::Server behaviors the OLD
// per-message rcp::l2::Server::Handler could never reach: Table 24 response
// suppression, conditional/cancellation-opcode routing, multi-member frame
// dispatch, and E2E dispatch. No socket is opened anywhere below — these
// exercise the pure decode_avtp_frame_header()/decode_l2_frame_header() split
// plus rcp::mock::Server's own frame-level entry points directly, the exact
// shape rcp::l2::Server::serve()'s own FrameHandler branch drives.

TEST_CASE("mock::Server::dispatch_frame(), reached via decode_l2_frame_header()'s own "
          "wire stream_id, applies Table 24 response suppression that the old "
          "rcp::l2::Server::Handler signature (no stream_id parameter at all) could never reach",
          "[l2][mock][REQ-L2-009]") {
    mock::Server server;
    REQUIRE_FALSE(server.advance_to_rcp_configured());

    const auto stream_id = make_stream_id(0x02, 0xABCD);

    // Table 24 (REQ-RMAP-048/049): rx_resp_stream_index == 0 means "no
    // response is to be sent" for every non-Acknowledge response kind on
    // this request stream.
    regmap::RequestStreamConfig cfg{};
    cfg.stream_id            = stream_id;
    cfg.rx_ack_stream_index  = 1;
    cfg.rx_resp_stream_index = 0;
    REQUIRE(server.set_request_stream_cfg({cfg}));

    MultiFrame req_frame;
    req_frame.use_tscf     = false;
    req_frame.stream_id    = stream_id;
    req_frame.sequence_num = 1;
    acf::AcfEntry read_req;
    read_req.info = standard_request(mock::kGpioByteBusId, /*transaction_num=*/1, /*write=*/false);
    req_frame.messages = {read_req};

    auto wire = encode_multi_frame(req_frame);
    AvtpFrameHeader hdr;
    REQUIRE_FALSE(decode_avtp_frame_header(wire.data(), wire.size(), hdr));
    REQUIRE(hdr.stream_id == stream_id);
    std::vector<uint8_t> acf_bytes(wire.begin() + static_cast<long>(hdr.acf_offset), wire.end());

    std::vector<mock::FrameMemberResult> results;
    REQUIRE(server.dispatch_frame(/*client=*/0, hdr.stream_id, acf_bytes, results) == 1);
    REQUIRE(results.size() == 1);
    REQUIRE_FALSE(results[0].response.rsp); // suppressed -- Table 24 correctly reached via stream_id

    // Contrast: the same GPIO read dispatched through mock::Server::dispatch()
    // with the default (unconfigured) stream_id -- the only stream_id an old
    // rcp::l2::Server::Handler could ever have supplied, since its signature
    // carried none at all -- is NOT suppressed.
    acf::AcfMessageInfo   unsuppressed_resp;
    std::vector<uint8_t>  unsuppressed_payload;
    server.dispatch(0, read_req.info, {}, unsuppressed_resp, unsuppressed_payload);
    REQUIRE(unsuppressed_resp.rsp);
}

TEST_CASE("A conditional/cancellation-opcode GBB request is correctly queued when routed through "
          "mock::Server::decode_and_dispatch() (what FrameHandler-wired dispatch_frame() calls per "
          "member), not silently executed as an ordinary Standard write the way dispatching it "
          "straight through mock::Server::dispatch() -- the old rcp::l2::Server::Handler's only "
          "reachable entry point -- would",
          "[l2][mock][REQ-L2-009]") {
    mock::Server server;
    REQUIRE_FALSE(server.advance_to_rcp_configured());

    // A genuine conditional request (request::RequestTypeOpcode::Timed) addressed to GPIO,
    // whose ordinary evt/op header bits ALSO look like a completely valid immediate GPIO
    // Reconfigure write -- exactly the shape a real conditional/cancellation request has on
    // the wire (rcp/request.hpp's own header comment: the repurposing trick touches only
    // message_timestamp, never the shared evt/op header bits decode_acf_gbb() also decodes).
    acf::AcfMessageInfo hdr = request::make_conditional_request(/*bus_id=*/mock::kGpioByteBusId,
                                                                  /*transaction_num=*/1, /*cs=*/false);
    hdr.op     = true;
    hdr.evt_op = static_cast<uint8_t>(endpoint::WriteSemantics::Reconfigure);
    const uint64_t repurposed_ts = request::encode_request_type(request::RequestTypeOpcode::Timed, {});
    auto payload = gpio::encode_gpio_payload(0x0000'000F);
    auto raw      = acf::encode_acf_gbb(hdr, repurposed_ts, payload);

    // Path A -- the OLD/naive wiring this batch fixes: decode as if it were an ordinary GBB
    // Standard request (message_timestamp's real repurposed meaning discarded), then hand the
    // decoded req straight to mock::Server::dispatch() -- exactly what rcp::l2::Server::Handler's
    // own single-message signature did before set_frame_handler() existed.
    acf::AcfMessageInfo   naive_req;
    uint64_t              naive_ts = 0;
    std::vector<uint8_t>  naive_payload;
    REQUIRE_FALSE(acf::decode_acf_gbb(raw.data(), raw.size(), naive_req, naive_ts, naive_payload));
    acf::AcfMessageInfo   naive_resp;
    std::vector<uint8_t>  naive_resp_payload;
    server.dispatch(0, naive_req, naive_payload, naive_resp, naive_resp_payload);
    REQUIRE(naive_resp.rsp); // wrongly executed immediately, as an ordinary write

    // Path B -- the CORRECT wiring: mock::Server::dispatch_frame() (what a FrameHandler wires
    // to) routes each member through decode_and_dispatch() internally, which peeks the
    // repurposed opcode FIRST (peek_conditional_request_type()) and routes to admission
    // instead of dispatch()'s ordinary ABB-oriented path.
    std::vector<mock::FrameMemberResult> results;
    REQUIRE(server.dispatch_frame(0, avtp::StreamId{}, raw, results) == 1);
    REQUIRE(results.size() == 1);
    REQUIRE_FALSE(results[0].response.rsp); // queued, not executed immediately
}

TEST_CASE("FrameHandler wired to mock::Server::dispatch_frame() dispatches every member of a "
          "multi-member L2 frame independently, exactly the adapter pattern documented in "
          "rcp/l2.hpp's own FrameHandler comment",
          "[l2][mock][REQ-L2-009]") {
    mock::Server mock_server;
    REQUIRE_FALSE(mock_server.advance_to_rcp_configured());

    FrameHandler fh = [&](size_t client, avtp::StreamId sid, uint8_t /*seq*/,
                           const std::vector<uint8_t>& acf,
                           std::vector<FrameMemberResult>& out) -> size_t {
        std::vector<mock::FrameMemberResult> mres;
        size_t n = mock_server.dispatch_frame(client, sid, acf, mres);
        out.reserve(mres.size());
        for (auto& m : mres) {
            FrameMemberResult r;
            r.result            = m.result;
            r.byte_bus_id       = m.byte_bus_id;
            r.response          = m.response;
            r.response_payload  = std::move(m.response_payload);
            out.push_back(std::move(r));
        }
        return n;
    };

    // Two-member frame: a plain GPIO read (answered normally) and an
    // unmapped byte_bus_id (answered with an ErrorResponse) -- both must
    // come back, in order, as separate FrameMemberResults.
    MultiFrame req_frame;
    req_frame.use_tscf     = false;
    req_frame.stream_id    = make_stream_id(0x02, 1);
    req_frame.sequence_num = 5;
    acf::AcfEntry gpio_read;
    gpio_read.info = standard_request(mock::kGpioByteBusId, /*transaction_num=*/1, /*write=*/false);
    acf::AcfEntry bad_bus;
    bad_bus.info = standard_request(/*bus_id=*/200, /*transaction_num=*/2, /*write=*/false);
    req_frame.messages = {gpio_read, bad_bus};

    auto wire = encode_multi_frame(req_frame);
    AvtpFrameHeader hdr;
    REQUIRE_FALSE(decode_avtp_frame_header(wire.data(), wire.size(), hdr));
    std::vector<uint8_t> raw(wire.begin() + static_cast<long>(hdr.acf_offset), wire.end());

    std::vector<FrameMemberResult> results;
    size_t n = fh(/*client=*/0, hdr.stream_id, static_cast<uint8_t>(hdr.sequence_num), raw, results);
    REQUIRE(n == 2);
    REQUIRE(results.size() == 2);
    REQUIRE(results[0].byte_bus_id == mock::kGpioByteBusId);
    REQUIRE(results[0].response.rsp);
    REQUIRE(acf::response_kind_of(results[0].response) == acf::ResponseKind::ReadResponse);
    REQUIRE(results[1].response.rsp);
    REQUIRE(acf::response_kind_of(results[1].response) == acf::ResponseKind::ErrorResponse);
}

TEST_CASE("FrameHandler wired to mock::Server::dispatch_frame_e2e() is reachable through the same "
          "adapter pattern, correctly dispatching a member on an endpoint with E2E CRC not enabled",
          "[l2][mock][REQ-L2-009]") {
    mock::Server mock_server;
    REQUIRE_FALSE(mock_server.advance_to_rcp_configured());

    FrameHandler fh = [&](size_t client, avtp::StreamId sid, uint8_t seq,
                           const std::vector<uint8_t>& acf,
                           std::vector<FrameMemberResult>& out) -> size_t {
        std::vector<mock::FrameMemberResult> mres;
        size_t n = mock_server.dispatch_frame_e2e(client, sid, seq, acf, mres);
        out.reserve(mres.size());
        for (auto& m : mres) {
            FrameMemberResult r;
            r.result           = m.result;
            r.byte_bus_id       = m.byte_bus_id;
            r.response          = m.response;
            r.response_payload  = std::move(m.response_payload);
            out.push_back(std::move(r));
        }
        return n;
    };

    MultiFrame req_frame;
    req_frame.use_tscf     = false;
    req_frame.stream_id    = make_stream_id(0x03, 1);
    req_frame.sequence_num = 1;
    acf::AcfEntry gpio_read;
    gpio_read.info = standard_request(mock::kGpioByteBusId, /*transaction_num=*/1, /*write=*/false);
    req_frame.messages = {gpio_read};

    auto wire = encode_multi_frame(req_frame);
    AvtpFrameHeader hdr;
    REQUIRE_FALSE(decode_avtp_frame_header(wire.data(), wire.size(), hdr));
    std::vector<uint8_t> raw(wire.begin() + static_cast<long>(hdr.acf_offset), wire.end());

    std::vector<FrameMemberResult> results;
    size_t n = fh(/*client=*/0, hdr.stream_id, static_cast<uint8_t>(hdr.sequence_num), raw, results);
    REQUIRE(n == 1);
    REQUIRE(results.size() == 1);
    REQUIRE(results[0].response.rsp);
    REQUIRE(acf::response_kind_of(results[0].response) == acf::ResponseKind::ReadResponse);
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
