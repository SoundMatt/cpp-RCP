// fusa:test REQ-DISC-001
// fusa:test REQ-DISC-002
// fusa:test REQ-DISC-003
// fusa:test REQ-DISC-004
// fusa:test REQ-DISC-005
// fusa:test REQ-DISC-006
// fusa:test REQ-DISC-007
// fusa:test REQ-DISC-008
// fusa:test REQ-DISC-009
// fusa:test REQ-DISC-010
// fusa:test REQ-DISC-011
// fusa:test REQ-DISC-012
// fusa:test REQ-DISC-013
// fusa:test REQ-DISC-014
// fusa:test REQ-DISC-015
// fusa:test REQ-DISC-016
// fusa:test REQ-DISC-017
// fusa:test REQ-DISC-018
// fusa:test REQ-DISC-019
// fusa:test REQ-DISC-020
// fusa:test REQ-DISC-021
// fusa:test REQ-DISC-022
// fusa:test REQ-DISC-023
// fusa:test REQ-DISC-024
// fusa:test REQ-DISC-025
// fusa:test REQ-DISC-026
// fusa:test REQ-DISC-027
// fusa:test REQ-DISC-028
// fusa:test REQ-DISC-029
// fusa:test REQ-DISC-030

// Tests for rcp/discovery.hpp — the RC Server discovery request/response
// exchange, its Phase 20 fragmented-response counterpart, discovery-stream
// claiming, and the client-side discovery-result cache (ROADMAP.md Phase
// 17, cpp-RCP issue #129, "Phase 4"). Ported from c-RCP's
// tests/test_discovery.c (this project's RC5-spec-conformant reference
// test suite for this module) — there is no separate
// test_tc18_gaps_discovery.c file; this module's gap coverage lives
// entirely here, same as in c-RCP.

#include <catch2/catch_test_macros.hpp>
#include <rcp/discovery.hpp>
#include <rcp/fragment.hpp>

using namespace rcp::discovery;
using rcp::lifecycle::ServerState;

namespace {

using Clock     = DiscoveryClaim::Clock;
using TimePoint = DiscoveryClaim::TimePoint;

TimePoint at(int64_t ms) {
    return TimePoint(std::chrono::milliseconds(ms));
}

rcp::avtp::StreamId make_stream_id(std::array<uint8_t, 6> mac, uint16_t suffix) {
    rcp::avtp::StreamId sid;
    sid.mac    = mac;
    sid.suffix = suffix;
    return sid;
}

const rcp::avtp::StreamId kClientSid = make_stream_id({0x02, 0x00, 0x00, 0x00, 0x00, 0x01}, 7);
const rcp::avtp::StreamId kServerSid = make_stream_id({0x02, 0x00, 0x00, 0x00, 0x00, 0x02}, 3);
const rcp::avtp::StreamId kOtherSid  = make_stream_id({0x02, 0x00, 0x00, 0x00, 0x00, 0x03}, 2);

rcp::regmap::GeneralMap sample_map() {
    rcp::regmap::GeneralMap map;
    map.magic        = 0xC0FFEE01u;
    // A 32-bit value whose upper half is non-zero, so a 16-bit svr_version
    // field cannot round-trip it -- see the octet-layout test below.
    map.svr_version  = 0x00010501u;
    map.vendor_id    = 0x1234u;
    map.device_id    = 0x5678u;
    map.svr_ep_count = 9u;
    return map;
}

DiscoveryResult sample_result(std::array<uint8_t, 6> mac, uint16_t suffix, uint16_t device_id) {
    DiscoveryResult r;
    r.valid            = true;
    r.server_stream_id = make_stream_id(mac, suffix);
    r.magic            = 0xAAAAAAAAu;
    r.svr_version      = 1;
    r.vendor_id        = 2;
    r.device_id        = device_id;
    r.svr_ep_count     = 3;
    return r;
}

// Builds a raw NTSCF-headed ACF_ABB frame directly from an arbitrary
// (possibly deliberately invalid) AcfMessageInfo — used by the negative
// decode tests below to construct frames decode_discovery_request()/
// decode_discovery_response() must reject, the same way c-RCP's own test
// file builds these by hand rather than through encode_discovery_request()/
// _response() (which can only ever produce valid frames).
std::vector<uint8_t> raw_ntscf_abb_frame(const rcp::acf::AcfMessageInfo& hdr,
                                          const rcp::avtp::StreamId& stream_id) {
    auto acf_msg = rcp::acf::encode_acf_abb(hdr, {});

    rcp::avtp::NtscfHeader ntscf_hdr;
    ntscf_hdr.stream_id           = stream_id;
    ntscf_hdr.control_data_length = static_cast<uint16_t>(acf_msg.size());

    auto out = rcp::avtp::encode_ntscf_header(ntscf_hdr);
    out.insert(out.end(), acf_msg.begin(), acf_msg.end());
    return out;
}

std::vector<uint8_t> raw_ntscf_gbb_frame(const rcp::acf::AcfMessageInfo& hdr,
                                          const rcp::avtp::StreamId& stream_id) {
    auto acf_msg = rcp::acf::encode_acf_gbb(hdr, /*message_timestamp=*/0, {});

    rcp::avtp::NtscfHeader ntscf_hdr;
    ntscf_hdr.stream_id           = stream_id;
    ntscf_hdr.control_data_length = static_cast<uint16_t>(acf_msg.size());

    auto out = rcp::avtp::encode_ntscf_header(ntscf_hdr);
    out.insert(out.end(), acf_msg.begin(), acf_msg.end());
    return out;
}

std::vector<uint8_t> raw_tscf_abb_frame(const rcp::acf::AcfMessageInfo& hdr,
                                         const rcp::avtp::StreamId& stream_id) {
    auto acf_msg = rcp::acf::encode_acf_abb(hdr, {});

    rcp::avtp::TscfHeader tscf_hdr;
    tscf_hdr.stream_id           = stream_id;
    tscf_hdr.control_data_length = static_cast<uint16_t>(acf_msg.size());

    auto out = rcp::avtp::encode_tscf_header(tscf_hdr);
    out.insert(out.end(), acf_msg.begin(), acf_msg.end());
    return out;
}

} // namespace

// ── NTSCF-only rule ────────────────────────────────────────────────────────── (REQ-DISC-001)

TEST_CASE("should_drop_discovery is true for a TSCF-headed frame", "[discovery][REQ-DISC-001]") {
    REQUIRE(should_drop_discovery(rcp::avtp::kSubtypeTscf));
}

TEST_CASE("should_drop_discovery is false for an NTSCF-headed frame", "[discovery][REQ-DISC-001]") {
    REQUIRE_FALSE(should_drop_discovery(rcp::avtp::kSubtypeNtscf));
}

TEST_CASE("should_drop_discovery is true for an unrecognized subtype", "[discovery][REQ-DISC-001]") {
    REQUIRE(should_drop_discovery(0x00));
}

// ── Discovery request framing ────────────────────────────────────────────────

TEST_CASE("make_discovery_request targets byte_bus_id 0 as an unconditional read", "[discovery][REQ-DISC-002]") {
    auto info = make_discovery_request(/*transaction_num=*/7);
    REQUIRE(info.byte_bus_id == kDiscoveryByteBusId);
    REQUIRE(info.byte_bus_id == 0);
    REQUIRE_FALSE(info.op); // read, not write
    REQUIRE_FALSE(info.rsp);
    REQUIRE(info.transaction_num == 7);
    REQUIRE(info.read_size_or_segment_num == kDiscoveryDefaultReadSize);
}

TEST_CASE("kDiscoveryRegisterAddress fixes the discovery read at register-map address 0",
          "[discovery][REQ-DISC-002]") {
    REQUIRE(kDiscoveryRegisterAddress == 0);
}

// ── Discovery request round-trip ────────────────────────────────────────────── (REQ-DISC-002/003)

TEST_CASE("decode_discovery_request round-trips an encoded discovery request", "[discovery][REQ-DISC-002][REQ-DISC-003]") {
    auto frame = encode_discovery_request(kClientSid, /*sequence_num=*/3, /*transaction_num=*/42, /*read_size=*/12);

    rcp::avtp::NtscfHeader    hdr;
    rcp::acf::AcfMessageInfo  info;
    std::vector<uint8_t>       payload;
    auto ec = decode_discovery_request(frame.data(), frame.size(), hdr, info, payload);

    REQUIRE_FALSE(ec);
    REQUIRE(hdr.stream_id == kClientSid);
    REQUIRE(hdr.sequence_num == 3);
    REQUIRE(info.byte_bus_id == kDiscoveryByteBusId);
    REQUIRE(info.read_size_or_segment_num == 12);
    REQUIRE(info.transaction_num == 42);
    REQUIRE_FALSE(info.op);
}

TEST_CASE("encode_discovery_request always produces an NTSCF-headed frame", "[discovery][REQ-DISC-002]") {
    auto frame = encode_discovery_request(kClientSid, /*sequence_num=*/0, /*transaction_num=*/1);
    REQUIRE_FALSE(frame.empty());
    REQUIRE(frame[0] == rcp::avtp::kSubtypeNtscf);
}

// ── Discovery request validation ───────────────────────────────────────────── (REQ-DISC-004..008)

TEST_CASE("A TSCF-headed discovery request is dropped, not decoded", "[discovery][REQ-DISC-004]") {
    rcp::acf::AcfMessageInfo hdr;
    hdr.byte_bus_id = kDiscoveryByteBusId;
    hdr.op           = false;
    auto frame = raw_tscf_abb_frame(hdr, kClientSid);

    rcp::avtp::NtscfHeader    out_hdr;
    rcp::acf::AcfMessageInfo out_info;
    std::vector<uint8_t>      out_payload;
    auto ec = decode_discovery_request(frame.data(), frame.size(), out_hdr, out_info, out_payload);

    REQUIRE(ec);
    REQUIRE(ec == make_error_code(DiscoveryErrc::tscf_headed_request_dropped));
}

TEST_CASE("decode_discovery_request rejects a non-ACF_ABB message type", "[discovery][REQ-DISC-005]") {
    rcp::acf::AcfMessageInfo hdr;
    hdr.byte_bus_id = kDiscoveryByteBusId;
    hdr.op           = false;
    auto frame = raw_ntscf_gbb_frame(hdr, kClientSid);

    rcp::avtp::NtscfHeader    out_hdr;
    rcp::acf::AcfMessageInfo out_info;
    std::vector<uint8_t>      out_payload;
    auto ec = decode_discovery_request(frame.data(), frame.size(), out_hdr, out_info, out_payload);

    REQUIRE(ec == make_error_code(DiscoveryErrc::bad_msg_type));
}

TEST_CASE("decode_discovery_request rejects the wrong byte_bus_id", "[discovery][REQ-DISC-006]") {
    rcp::acf::AcfMessageInfo hdr;
    hdr.byte_bus_id = 7u; // not the discovery bus
    hdr.op           = false;
    auto frame = raw_ntscf_abb_frame(hdr, kClientSid);

    rcp::avtp::NtscfHeader    out_hdr;
    rcp::acf::AcfMessageInfo out_info;
    std::vector<uint8_t>      out_payload;
    auto ec = decode_discovery_request(frame.data(), frame.size(), out_hdr, out_info, out_payload);

    REQUIRE(ec == make_error_code(DiscoveryErrc::wrong_bus));
}

TEST_CASE("decode_discovery_request rejects a write op", "[discovery][REQ-DISC-007]") {
    rcp::acf::AcfMessageInfo hdr;
    hdr.byte_bus_id = kDiscoveryByteBusId;
    hdr.op           = true; // discovery is always a read
    auto frame = raw_ntscf_abb_frame(hdr, kClientSid);

    rcp::avtp::NtscfHeader    out_hdr;
    rcp::acf::AcfMessageInfo out_info;
    std::vector<uint8_t>      out_payload;
    auto ec = decode_discovery_request(frame.data(), frame.size(), out_hdr, out_info, out_payload);

    REQUIRE(ec == make_error_code(DiscoveryErrc::wrong_op));
}

TEST_CASE("decode_discovery_request rejects a short frame", "[discovery][REQ-DISC-008]") {
    uint8_t tiny[2] = {rcp::avtp::kSubtypeNtscf, 0};
    rcp::avtp::NtscfHeader    out_hdr;
    rcp::acf::AcfMessageInfo out_info;
    std::vector<uint8_t>      out_payload;

    auto ec = decode_discovery_request(tiny, sizeof(tiny), out_hdr, out_info, out_payload);
    REQUIRE(ec == make_error_code(DiscoveryErrc::short_frame));
}

TEST_CASE("decode_discovery_request rejects an empty buffer", "[discovery][REQ-DISC-008]") {
    rcp::avtp::NtscfHeader    out_hdr;
    rcp::acf::AcfMessageInfo out_info;
    std::vector<uint8_t>      out_payload;

    auto ec = decode_discovery_request(nullptr, 0, out_hdr, out_info, out_payload);
    REQUIRE(ec == make_error_code(DiscoveryErrc::short_frame));
}

// ── Any-state answering ──────────────────────────────────────────────────────

TEST_CASE("A server answers discovery in every lifecycle state", "[discovery][REQ-DISC-009]") {
    REQUIRE(should_answer_discovery(ServerState::HwUnconfigured));
    REQUIRE(should_answer_discovery(ServerState::HwConfigured));
    REQUIRE(should_answer_discovery(ServerState::RcpConfigured));
}

// ── Discovery response ──────────────────────────────────────────────────────── (REQ-DISC-009..014)

TEST_CASE("encode/decode_discovery_response round-trip at exactly kDiscoveryGeneralSliceLen",
          "[discovery][REQ-DISC-009][REQ-DISC-010][REQ-DISC-012]") {
    auto map = sample_map();
    auto frame = encode_discovery_response(map, kServerSid, /*sequence_num=*/0, /*transaction_num=*/9,
                                             static_cast<uint8_t>(kDiscoveryGeneralSliceLen));

    DiscoveryResult result;
    auto ec = decode_discovery_response(frame.data(), frame.size(), result);

    REQUIRE_FALSE(ec);
    REQUIRE(result.valid);
    REQUIRE(result.server_stream_id == kServerSid);
    REQUIRE(result.magic == map.magic);
    REQUIRE(result.svr_version == map.svr_version);
    REQUIRE(result.vendor_id == map.vendor_id);
    REQUIRE(result.device_id == map.device_id);
    REQUIRE(result.svr_ep_count == map.svr_ep_count);
}

// TC18 v0.5.1_RC §12.7.5 "RC Server Register map - General part", Table 18
// "RC Server configuration static part". The absolute addresses and widths
// of the leading, device-recognition part of the block are:
//
//   0x0000  svr_oa_tc18_magic_nr   32 bit  R
//   0x0004  svr_version            32 bit  R
//   0x0008  svr_vendor_id          16 bit  R
//   0x000A  svr_device_id          16 bit  R
//   0x000C  svr_ep_count           16 bit  R
//
// svr_version is 32 bit, so vendor_id starts at 0x0008 -- not 0x0006, as a
// 16-bit svr_version would put it. This test pins each field to its cited
// absolute address in the encoded payload, so the two-octet regression
// (which would shift vendor_id, device_id and svr_ep_count each two octets
// early, misparsing all three for any conforming peer) cannot come back
// unnoticed. Mirrors c-RCP's own test_response_general_slice_octet_layout()
// (tests/test_discovery.c).
TEST_CASE("The discovery response's general slice is laid out at its documented absolute addresses",
          "[discovery][REQ-DISC-010]") {
    auto map = sample_map();
    auto frame = encode_discovery_response(map, kServerSid, /*sequence_num=*/0, /*transaction_num=*/9,
                                             static_cast<uint8_t>(kDiscoveryGeneralSliceLen));

    rcp::avtp::NtscfHeader     ntscf_hdr;
    rcp::acf::AcfMessageInfo  acf_hdr;
    std::vector<uint8_t>       payload;
    REQUIRE_FALSE(rcp::avtp::decode_ntscf_header(frame.data(), frame.size(), ntscf_hdr));
    REQUIRE_FALSE(rcp::acf::decode_acf_abb(frame.data() + rcp::avtp::kNtscfHeaderLen,
                                            frame.size() - rcp::avtp::kNtscfHeaderLen, acf_hdr, payload));

    REQUIRE(kDiscoveryGeneralSliceLen == 14u);
    REQUIRE(payload.size() == kDiscoveryGeneralSliceLen);

    // 0x0000 svr_oa_tc18_magic_nr, 32 bit big-endian: 0xC0FFEE01
    REQUIRE(payload[0x00] == 0xC0u);
    REQUIRE(payload[0x01] == 0xFFu);
    REQUIRE(payload[0x02] == 0xEEu);
    REQUIRE(payload[0x03] == 0x01u);
    // 0x0004 svr_version, 32 bit big-endian: 0x00010501
    REQUIRE(payload[0x04] == 0x00u);
    REQUIRE(payload[0x05] == 0x01u);
    REQUIRE(payload[0x06] == 0x05u);
    REQUIRE(payload[0x07] == 0x01u);
    // 0x0008 svr_vendor_id, 16 bit big-endian: 0x1234
    REQUIRE(payload[0x08] == 0x12u);
    REQUIRE(payload[0x09] == 0x34u);
    // 0x000A svr_device_id, 16 bit big-endian: 0x5678
    REQUIRE(payload[0x0A] == 0x56u);
    REQUIRE(payload[0x0B] == 0x78u);
    // 0x000C svr_ep_count, 16 bit big-endian: 9
    REQUIRE(payload[0x0C] == 0x00u);
    REQUIRE(payload[0x0D] == 0x09u);
}

TEST_CASE("A discovery response's payload always spans exactly read_size octets", "[discovery][REQ-DISC-009]") {
    auto map = sample_map();
    auto frame_small = encode_discovery_response(map, kServerSid, 0, 1, /*read_size=*/4);
    auto frame_large = encode_discovery_response(map, kServerSid, 0, 1, /*read_size=*/40);

    rcp::avtp::NtscfHeader     ntscf_hdr;
    rcp::acf::AcfMessageInfo  acf_hdr;
    std::vector<uint8_t>       payload;

    REQUIRE_FALSE(rcp::avtp::decode_ntscf_header(frame_small.data(), frame_small.size(), ntscf_hdr));
    REQUIRE_FALSE(rcp::acf::decode_acf_abb(frame_small.data() + rcp::avtp::kNtscfHeaderLen,
                                            frame_small.size() - rcp::avtp::kNtscfHeaderLen, acf_hdr, payload));
    REQUIRE(payload.size() == 4);

    REQUIRE_FALSE(rcp::avtp::decode_ntscf_header(frame_large.data(), frame_large.size(), ntscf_hdr));
    REQUIRE_FALSE(rcp::acf::decode_acf_abb(frame_large.data() + rcp::avtp::kNtscfHeaderLen,
                                            frame_large.size() - rcp::avtp::kNtscfHeaderLen, acf_hdr, payload));
    REQUIRE(payload.size() == 40);
}

TEST_CASE("decode_discovery_response treats a slice truncated below kDiscoveryGeneralSliceLen as short",
          "[discovery][REQ-DISC-013]") {
    auto map = sample_map();
    // read_size of 4 -- only room for the magic field.
    auto frame = encode_discovery_response(map, kServerSid, 0, 1, /*read_size=*/4);

    DiscoveryResult result;
    auto ec = decode_discovery_response(frame.data(), frame.size(), result);

    // Too short to extract a full generic slice -- must be treated as
    // short, not silently fabricate zeros for the missing fields.
    REQUIRE(ec == make_error_code(DiscoveryErrc::short_frame));
}

TEST_CASE("A discovery response zero-fills any octets beyond the general slice", "[discovery][REQ-DISC-011]") {
    auto map = sample_map();
    auto frame = encode_discovery_response(map, kServerSid, 0, 1,
                                             static_cast<uint8_t>(kDiscoveryGeneralSliceLen + 4));

    rcp::avtp::NtscfHeader     ntscf_hdr;
    rcp::acf::AcfMessageInfo  acf_hdr;
    std::vector<uint8_t>       payload;
    REQUIRE_FALSE(rcp::avtp::decode_ntscf_header(frame.data(), frame.size(), ntscf_hdr));
    REQUIRE_FALSE(rcp::acf::decode_acf_abb(frame.data() + rcp::avtp::kNtscfHeaderLen,
                                            frame.size() - rcp::avtp::kNtscfHeaderLen, acf_hdr, payload));

    REQUIRE(payload.size() == kDiscoveryGeneralSliceLen + 4);
    for (size_t i = kDiscoveryGeneralSliceLen; i < payload.size(); ++i) {
        REQUIRE(payload[i] == 0);
    }
}

// REQ-DISC-014: decode_discovery_response() applies the same AVTP/ACF-level
// checks decode_discovery_request() does (both go through the shared
// detail::decode_common_frame()) -- one test per condition, mirroring the
// request-side tests above but through the response entry point.
TEST_CASE("decode_discovery_response is dropped when TSCF-headed", "[discovery][REQ-DISC-014]") {
    rcp::acf::AcfMessageInfo hdr;
    hdr.byte_bus_id = kDiscoveryByteBusId;
    hdr.op           = false;
    hdr.rsp          = true;
    auto frame = raw_tscf_abb_frame(hdr, kServerSid);

    DiscoveryResult result;
    auto ec = decode_discovery_response(frame.data(), frame.size(), result);
    REQUIRE(ec == make_error_code(DiscoveryErrc::tscf_headed_request_dropped));
}

TEST_CASE("decode_discovery_response rejects a non-ACF_ABB message type", "[discovery][REQ-DISC-014]") {
    rcp::acf::AcfMessageInfo hdr;
    hdr.byte_bus_id = kDiscoveryByteBusId;
    hdr.op           = false;
    hdr.rsp          = true;
    auto frame = raw_ntscf_gbb_frame(hdr, kServerSid);

    DiscoveryResult result;
    auto ec = decode_discovery_response(frame.data(), frame.size(), result);
    REQUIRE(ec == make_error_code(DiscoveryErrc::bad_msg_type));
}

TEST_CASE("decode_discovery_response rejects the wrong byte_bus_id", "[discovery][REQ-DISC-014]") {
    rcp::acf::AcfMessageInfo hdr;
    hdr.byte_bus_id = 7u;
    hdr.op           = false;
    hdr.rsp          = true;
    auto frame = raw_ntscf_abb_frame(hdr, kServerSid);

    DiscoveryResult result;
    auto ec = decode_discovery_response(frame.data(), frame.size(), result);
    REQUIRE(ec == make_error_code(DiscoveryErrc::wrong_bus));
}

TEST_CASE("decode_discovery_response rejects a write op", "[discovery][REQ-DISC-014]") {
    rcp::acf::AcfMessageInfo hdr;
    hdr.byte_bus_id = kDiscoveryByteBusId;
    hdr.op           = true;
    hdr.rsp          = true;
    auto frame = raw_ntscf_abb_frame(hdr, kServerSid);

    DiscoveryResult result;
    auto ec = decode_discovery_response(frame.data(), frame.size(), result);
    REQUIRE(ec == make_error_code(DiscoveryErrc::wrong_op));
}

// ── Fragmented response (Phase 20, rcp/fragment.hpp) ────────────────────────── (REQ-DISC-025..028)

TEST_CASE("discovery_response_fragment_count is 1 when unfragmented", "[discovery][REQ-DISC-025]") {
    REQUIRE(discovery_response_fragment_count(12, 100) == 1);
    REQUIRE(discovery_response_fragment_count(0, 0) == 1);
}

TEST_CASE("An unfragmented encode_discovery_response_fragmented matches the single-frame path",
          "[discovery][REQ-DISC-025][REQ-DISC-026]") {
    auto map = sample_map();
    auto plain = encode_discovery_response(map, kServerSid, 0, 9, static_cast<uint8_t>(kDiscoveryGeneralSliceLen));

    auto fragmented = encode_discovery_response_fragmented(
        map, kServerSid, 0, 9, static_cast<uint8_t>(kDiscoveryGeneralSliceLen), /*max_fragment_payload=*/255);

    REQUIRE(fragmented.size() == 1);
    REQUIRE(fragmented[0] == plain);
}

// Closes the deferred single-AVTPDU-worst-case scenario: exercises
// rcp/fragment.hpp's ms/segment_num mechanism against this module's own
// NTSCF+ACF wire codec end-to-end, using a deliberately small
// max_fragment_payload -- read_size's one-octet width means genuine
// discovery traffic never actually needs more than one fragment in
// practice; this test proves the mechanism composes correctly regardless.
TEST_CASE("A deliberately small fragment cap round-trips through the reassembler",
          "[discovery][REQ-DISC-026][REQ-DISC-027][REQ-DISC-028]") {
    auto map = sample_map();
    const uint8_t read_size            = 20;
    const size_t   max_fragment_payload = 6;

    REQUIRE(discovery_response_fragment_count(read_size, max_fragment_payload) == 4); // ceil(20/6)

    auto frames = encode_discovery_response_fragmented(map, kServerSid, 0, 42, read_size, max_fragment_payload);
    REQUIRE(frames.size() == 4);

    rcp::fragment::Reassembler reasm(read_size);
    for (size_t i = 0; i < frames.size(); ++i) {
        rcp::avtp::StreamId    from_stream;
        bool                    ms = false;
        uint8_t                 segnum = 0;
        std::vector<uint8_t>    payload;

        auto ec = decode_discovery_response_fragment(frames[i].data(), frames[i].size(), from_stream, ms, segnum,
                                                        payload);
        REQUIRE_FALSE(ec);
        REQUIRE(from_stream == kServerSid);

        auto rc = reasm.feed(ms, segnum, payload.data(), payload.size());
        if (i + 1 < frames.size()) {
            REQUIRE(rc == rcp::fragment::ReasmResult::kContinue);
        } else {
            REQUIRE(rc == rcp::fragment::ReasmResult::kComplete);
        }
    }

    REQUIRE(reasm.size() == read_size);

    DiscoveryResult result;
    auto ec = decode_discovery_reassembled_response(reasm.data(), reasm.size(), kServerSid, result);
    REQUIRE_FALSE(ec);
    REQUIRE(result.valid);
    REQUIRE(result.server_stream_id == kServerSid);
    REQUIRE(result.magic == map.magic);
    REQUIRE(result.svr_version == map.svr_version);
    REQUIRE(result.vendor_id == map.vendor_id);
    REQUIRE(result.device_id == map.device_id);
    REQUIRE(result.svr_ep_count == map.svr_ep_count);
}

// The actual worst case fragment.hpp's segment_num width is sized against:
// read_size = 255 (its own max value) with max_fragment_payload = 1, so the
// plan needs exactly 254 intermediate (ms=true) segments plus one final —
// well inside fragment::kMaxIntermediateSegments (4096).
TEST_CASE("Maximum read_size with minimum fragment payload round-trips", "[discovery][REQ-DISC-026]") {
    auto map = sample_map();
    const uint8_t read_size            = 255;
    const size_t   max_fragment_payload = 1;

    REQUIRE(discovery_response_fragment_count(read_size, max_fragment_payload) == 255);

    auto frames = encode_discovery_response_fragmented(map, kServerSid, 0, 7, read_size, max_fragment_payload);
    REQUIRE(frames.size() == 255);

    rcp::fragment::Reassembler reasm(read_size);
    for (size_t i = 0; i < frames.size(); ++i) {
        rcp::avtp::StreamId    from_stream;
        bool                    ms = false;
        uint8_t                 segnum = 0;
        std::vector<uint8_t>    payload;

        auto ec = decode_discovery_response_fragment(frames[i].data(), frames[i].size(), from_stream, ms, segnum,
                                                        payload);
        REQUIRE_FALSE(ec);

        auto rc = reasm.feed(ms, segnum, payload.data(), payload.size());
        if (i + 1 < frames.size()) {
            REQUIRE(rc == rcp::fragment::ReasmResult::kContinue);
        } else {
            REQUIRE(rc == rcp::fragment::ReasmResult::kComplete);
        }
    }

    DiscoveryResult result;
    auto ec = decode_discovery_reassembled_response(reasm.data(), reasm.size(), kServerSid, result);
    REQUIRE_FALSE(ec);
    REQUIRE(result.valid);
}

TEST_CASE("encode_discovery_response_fragmented is disabled when max_fragment_payload is 0 and oversized",
          "[discovery][REQ-DISC-026]") {
    auto map = sample_map();
    auto frames = encode_discovery_response_fragmented(map, kServerSid, 0, 1, /*read_size=*/20,
                                                          /*max_fragment_payload=*/0);
    REQUIRE(frames.empty());
}

// REQ-DISC-027: decode_discovery_response_fragment() applies the same
// AVTP/ACF-level validation decode_discovery_response() does (both go
// through detail::decode_common_frame()) -- one representative condition
// proves the fragment entry point actually reaches that shared check,
// rather than bypassing it.
TEST_CASE("decode_discovery_response_fragment rejects the wrong byte_bus_id", "[discovery][REQ-DISC-027]") {
    rcp::acf::AcfMessageInfo hdr;
    hdr.byte_bus_id = 7u;
    hdr.op           = false;
    hdr.rsp          = true;
    hdr.ms           = true;
    auto frame = raw_ntscf_abb_frame(hdr, kServerSid);

    rcp::avtp::StreamId from_stream;
    bool                  ms = false;
    uint8_t               segnum = 0;
    std::vector<uint8_t>  payload;
    auto ec = decode_discovery_response_fragment(frame.data(), frame.size(), from_stream, ms, segnum, payload);
    REQUIRE(ec == make_error_code(DiscoveryErrc::wrong_bus));
}

// REQ-DISC-028: a reassembled buffer shorter than kDiscoveryGeneralSliceLen
// is rejected, same short-frame-not-fabricated-zeros discipline as the
// truncated-slice response test above.
TEST_CASE("decode_discovery_reassembled_response rejects a short buffer", "[discovery][REQ-DISC-028]") {
    std::array<uint8_t, kDiscoveryGeneralSliceLen - 1> short_buf{};
    DiscoveryResult result;

    auto ec = decode_discovery_reassembled_response(short_buf.data(), short_buf.size(), kServerSid, result);
    REQUIRE(ec == make_error_code(DiscoveryErrc::short_frame));
}

// ── Discovery-stream claiming ────────────────────────────────────────────────── (REQ-DISC-015..022/029)

TEST_CASE("A freshly-constructed DiscoveryClaim is open and unheld", "[discovery][REQ-DISC-015][REQ-DISC-016]") {
    DiscoveryClaim claim;
    REQUIRE_FALSE(claim.has_active_claim(at(0)));
    REQUIRE(claim.current_holder(at(0)) == std::nullopt);
}

TEST_CASE("The first discovery request in HW_UNCONFIGURED claims the discovery stream",
          "[discovery][REQ-DISC-017]") {
    DiscoveryClaim claim;
    auto outcome = claim.on_discovery_request(/*client=*/1, ServerState::HwUnconfigured, at(0));
    REQUIRE(outcome == DiscoveryClaim::ClaimOutcome::Claimed);
    REQUIRE(claim.has_active_claim(at(0)));
    REQUIRE(claim.current_holder(at(0)) == std::optional<size_t>(1));
}

TEST_CASE("The first discovery request in HW_CONFIGURED also claims the discovery stream",
          "[discovery][REQ-DISC-017]") {
    DiscoveryClaim claim;
    auto outcome = claim.on_discovery_request(/*client=*/5, ServerState::HwConfigured, at(0));
    REQUIRE(outcome == DiscoveryClaim::ClaimOutcome::Claimed);
    REQUIRE(claim.may_configure(5, at(1)));
}

TEST_CASE("A discovery request in RCP_CONFIGURED never claims the stream", "[discovery][REQ-DISC-017]") {
    DiscoveryClaim claim;
    auto outcome = claim.on_discovery_request(/*client=*/1, ServerState::RcpConfigured, at(0));
    REQUIRE(outcome == DiscoveryClaim::ClaimOutcome::NotEligible);
    REQUIRE_FALSE(claim.has_active_claim(at(0)));
    REQUIRE_FALSE(claim.may_configure(1, at(0)));
}

// REQ-DISC-029: TC18 Figure 17's two "Discovery request received"
// transitions apply uniformly regardless of requester identity -- neither
// a different client's request nor the current claimant's own
// re-request re-claims or refreshes an already-active claim.
TEST_CASE("A second client's request during an active claim does not preempt or dislodge it",
          "[discovery][REQ-DISC-018][REQ-DISC-029]") {
    DiscoveryClaim claim;
    REQUIRE(claim.on_discovery_request(1, ServerState::HwUnconfigured, at(0)) == DiscoveryClaim::ClaimOutcome::Claimed);

    auto outcome = claim.on_discovery_request(2, ServerState::HwUnconfigured, at(5));
    REQUIRE(outcome == DiscoveryClaim::ClaimOutcome::HeldByOther);
    // Read-only discovery is unaffected regardless of claim state.
    REQUIRE(should_answer_discovery(ServerState::HwUnconfigured));
    // The original holder's claim survives the other client's request.
    REQUIRE(claim.may_configure(1, at(6)));
}

TEST_CASE("The claim holder re-requesting discovery before it lapses is reported as AlreadyHeld, "
          "not re-claimed",
          "[discovery][REQ-DISC-018][REQ-DISC-029]") {
    DiscoveryClaim claim;
    REQUIRE(claim.on_discovery_request(1, ServerState::HwUnconfigured, at(0)) == DiscoveryClaim::ClaimOutcome::Claimed);
    auto outcome = claim.on_discovery_request(1, ServerState::HwUnconfigured, at(2));
    REQUIRE(outcome == DiscoveryClaim::ClaimOutcome::AlreadyHeld);
}

TEST_CASE("A claim lapses after Discovery_TimeOut elapses and reopens to a new claimant",
          "[discovery][REQ-DISC-016][REQ-DISC-017]") {
    DiscoveryClaim claim(std::chrono::milliseconds(20));
    REQUIRE(claim.on_discovery_request(1, ServerState::HwUnconfigured, at(0)) == DiscoveryClaim::ClaimOutcome::Claimed);
    REQUIRE(claim.may_configure(1, at(19)));

    // Past the timeout, the claim has lapsed: the original holder may no
    // longer configure ...
    REQUIRE_FALSE(claim.may_configure(1, at(21)));
    REQUIRE_FALSE(claim.has_active_claim(at(21)));

    // ... and a different client's next discovery request is free to claim
    // the now-lapsed stream.
    auto outcome = claim.on_discovery_request(2, ServerState::HwUnconfigured, at(25));
    REQUIRE(outcome == DiscoveryClaim::ClaimOutcome::Claimed);
    REQUIRE(claim.may_configure(2, at(25)));
    REQUIRE_FALSE(claim.may_configure(1, at(25)));
}

TEST_CASE("Discovery_TimeOut defaults to 20ms", "[discovery][REQ-DISC-015]") {
    REQUIRE(DiscoveryClaim::kDefaultTimeout == std::chrono::milliseconds(20));
}

TEST_CASE("may_configure is false for a client that never held the claim", "[discovery][REQ-DISC-019]") {
    DiscoveryClaim claim;
    REQUIRE_FALSE(claim.may_configure(1, at(0)));
}

// Bug fix pinned (this port, Phase 4): a configuration request from the
// claim holder must EXTEND the deadline, not consume/release the claim --
// see DiscoveryClaim::on_configuration_request()'s own doc comment. Mirrors
// c-RCP's test_claim_config_write_refreshes_deadline_for_claimant()
// (tests/test_discovery.c) exactly, including its own numbers.
TEST_CASE("A configuration request from the claim holder refreshes the deadline instead of consuming the claim",
          "[discovery][REQ-DISC-020]") {
    DiscoveryClaim claim(std::chrono::milliseconds(20));
    REQUIRE(claim.on_discovery_request(1, ServerState::HwUnconfigured, at(1000)) == DiscoveryClaim::ClaimOutcome::Claimed);

    REQUIRE(claim.on_configuration_request(1, at(1015)));
    // Without the refresh the claim would have lapsed at 1020; the write
    // at t=1015 should have pushed the deadline out to 1035.
    REQUIRE(claim.may_configure(1, at(1025)));
    REQUIRE(claim.has_active_claim(at(1025)));
}

TEST_CASE("A configuration request from a client that does not hold the claim is refused, "
          "leaving the real holder's claim untouched",
          "[discovery][REQ-DISC-021]") {
    DiscoveryClaim claim(std::chrono::milliseconds(20));
    REQUIRE(claim.on_discovery_request(1, ServerState::HwUnconfigured, at(1000)) == DiscoveryClaim::ClaimOutcome::Claimed);

    REQUIRE_FALSE(claim.on_configuration_request(2, at(1005)));
    // Client 1's claim survives client 2's rejected attempt, and still
    // lapses on its own original schedule (unrefreshed).
    REQUIRE(claim.may_configure(1, at(1019)));
    REQUIRE_FALSE(claim.may_configure(1, at(1020)));
}

TEST_CASE("A configuration request never resurrects an already-lapsed claim", "[discovery][REQ-DISC-021]") {
    DiscoveryClaim claim(std::chrono::milliseconds(20));
    REQUIRE(claim.on_discovery_request(1, ServerState::HwUnconfigured, at(1000)) == DiscoveryClaim::ClaimOutcome::Claimed);

    REQUIRE_FALSE(claim.on_configuration_request(1, at(1025))); // already lapsed
    REQUIRE_FALSE(claim.has_active_claim(at(1025)));
}

TEST_CASE("release unconditionally drops the claim", "[discovery][REQ-DISC-022]") {
    DiscoveryClaim claim(std::chrono::milliseconds(20));
    REQUIRE(claim.on_discovery_request(1, ServerState::HwUnconfigured, at(1000)) == DiscoveryClaim::ClaimOutcome::Claimed);
    REQUIRE(claim.has_active_claim(at(1000)));

    claim.release();

    REQUIRE_FALSE(claim.has_active_claim(at(1000)));
    REQUIRE_FALSE(claim.may_configure(1, at(1000)));
}

// ── Client-side discovery result persistence ─────────────────────────────────── (REQ-DISC-023/030)

TEST_CASE("A freshly-constructed DiscoveryCache starts empty", "[discovery][REQ-DISC-023]") {
    DiscoveryCache cache;
    REQUIRE(cache.size() == 0);
}

TEST_CASE("DiscoveryCache::put then find round-trips a result", "[discovery][REQ-DISC-023][REQ-DISC-030]") {
    DiscoveryCache cache;
    auto r = sample_result({0x02, 0x00, 0x00, 0x00, 0x00, 0x02}, 1, 100);

    cache.put(r);
    REQUIRE(cache.size() == 1);

    auto* found = cache.find(r.server_stream_id);
    REQUIRE(found != nullptr);
    REQUIRE(found->device_id == 100);
}

TEST_CASE("DiscoveryCache::put updates an existing entry in place", "[discovery][REQ-DISC-023]") {
    DiscoveryCache cache;
    auto r1 = sample_result({0x02, 0x00, 0x00, 0x00, 0x00, 0x02}, 1, 100);
    auto r2 = sample_result({0x02, 0x00, 0x00, 0x00, 0x00, 0x02}, 1, 200); // same stream_id

    cache.put(r1);
    cache.put(r2);

    REQUIRE(cache.size() == 1); // updated, not appended

    auto* found = cache.find(r1.server_stream_id);
    REQUIRE(found != nullptr);
    REQUIRE(found->device_id == 200);
}

TEST_CASE("DiscoveryCache::find returns nullptr on a miss", "[discovery][REQ-DISC-030]") {
    DiscoveryCache cache;
    REQUIRE(cache.find(kOtherSid) == nullptr);
}

TEST_CASE("DiscoveryCache grows past any small initial capacity", "[discovery][REQ-DISC-023]") {
    DiscoveryCache cache;
    for (uint16_t i = 0; i < 40; ++i) {
        cache.put(sample_result({0x02, 0x00, 0x00, 0x00, 0x00, 0x02}, i, i));
    }
    REQUIRE(cache.size() == 40);
}

// ── DiscoveryErrc category sanity ────────────────────────────────────────────── (REQ-DISC-024)

TEST_CASE("Every DiscoveryErrc reports a unique, non-empty message in its own category",
          "[discovery][REQ-DISC-024]") {
    const DiscoveryErrc codes[] = {
        DiscoveryErrc::short_frame,
        DiscoveryErrc::tscf_headed_request_dropped,
        DiscoveryErrc::bad_msg_type,
        DiscoveryErrc::wrong_bus,
        DiscoveryErrc::wrong_op,
    };

    for (size_t i = 0; i < std::size(codes); ++i) {
        auto ec = make_error_code(codes[i]);
        REQUIRE(ec.category() == discovery_category());
        REQUIRE_FALSE(ec.message().empty());
        for (size_t j = 0; j < i; ++j) {
            REQUIRE(ec.message() != make_error_code(codes[j]).message());
        }
    }
}
