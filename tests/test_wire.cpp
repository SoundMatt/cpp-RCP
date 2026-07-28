// fusa:test REQ-WIRE-001
// fusa:test REQ-WIRE-002
// fusa:test REQ-WIRE-003
// fusa:test REQ-WIRE-004
// fusa:test REQ-WIRE-005
// fusa:test REQ-WIRE-006
// fusa:test REQ-WIRE-007
// fusa:test REQ-WIRE-008
// fusa:test REQ-WIRE-009
// fusa:test REQ-WIRE-010
// fusa:test REQ-WIRE-011
// fusa:test REQ-WIRE-012
// fusa:test REQ-WIRE-013
// fusa:test REQ-WIRE-014

// Tests for rcp/wire.hpp — the TC18 AVTPDU/ACF wire codec (ROADMAP.md
// milestone 44, "Wire Format Core", v2.0.0).

#include <catch2/catch_test_macros.hpp>
#include <rcp/wire.hpp>

using namespace rcp::wire;

namespace {

StreamId make_stream_id(uint8_t mac_seed, uint16_t suffix) {
    StreamId id;
    for (auto& b : id.mac) b = mac_seed++;
    id.suffix = suffix;
    return id;
}

} // namespace

// ── StreamId ───────────────────────────────────────────────────────────────────

TEST_CASE("StreamId round-trips through to_u64/from_u64", "[wire][REQ-WIRE-007]") {
    auto id = make_stream_id(0x02, 0xBEEF);
    auto back = StreamId::from_u64(id.to_u64());
    REQUIRE(back == id);
    REQUIRE(back.suffix == 0xBEEF);
}

TEST_CASE("Distinct MAC halves produce distinct StreamId values", "[wire][REQ-WIRE-007]") {
    auto a = make_stream_id(0x02, 0x0001);
    auto b = make_stream_id(0x03, 0x0001);
    REQUIRE(a != b);
}

// ── NTSCF header ───────────────────────────────────────────────────────────────

TEST_CASE("NTSCF header round-trips stream_id, sequence_num, control_data_length", "[wire][REQ-WIRE-001]") {
    NtscfHeader hdr;
    hdr.stream_id           = make_stream_id(0xAA, 7);
    hdr.sequence_num        = 4242;
    hdr.control_data_length = 128;

    auto buf = encode_ntscf_header(hdr);
    REQUIRE(buf.size() == kNtscfHeaderLen);
    REQUIRE(buf[0] == kSubtypeNtscf);

    NtscfHeader out;
    REQUIRE_FALSE(decode_ntscf_header(buf.data(), buf.size(), out));
    REQUIRE(out.stream_id == hdr.stream_id);
    REQUIRE(out.sequence_num == hdr.sequence_num);
    REQUIRE(out.control_data_length == hdr.control_data_length);
}

// ── TSCF header ────────────────────────────────────────────────────────────────

TEST_CASE("TSCF header round-trips avtp_timestamp when timestamp_valid is set", "[wire][REQ-WIRE-002][REQ-WIRE-011]") {
    TscfHeader hdr;
    hdr.stream_id           = make_stream_id(0x10, 99);
    hdr.sequence_num        = 5;
    hdr.control_data_length = 64;
    hdr.timestamp_valid     = true;
    hdr.avtp_timestamp      = 0xCAFEBABE;

    auto buf = encode_tscf_header(hdr);
    REQUIRE(buf.size() == kTscfHeaderLen);
    REQUIRE(buf[0] == kSubtypeTscf);

    TscfHeader out;
    REQUIRE_FALSE(decode_tscf_header(buf.data(), buf.size(), out));
    REQUIRE(out.timestamp_valid);
    REQUIRE(out.avtp_timestamp == hdr.avtp_timestamp);
    REQUIRE(out.stream_id == hdr.stream_id);
}

TEST_CASE("TSCF header marks timestamp invalid distinctly from a zero timestamp", "[wire][REQ-WIRE-011]") {
    TscfHeader hdr;
    hdr.timestamp_valid = false;
    hdr.avtp_timestamp  = 0;

    auto buf = encode_tscf_header(hdr);
    TscfHeader out;
    REQUIRE_FALSE(decode_tscf_header(buf.data(), buf.size(), out));
    REQUIRE_FALSE(out.timestamp_valid);
}

// ── AVTPDU subtype validation ──────────────────────────────────────────────────

TEST_CASE("decode_ntscf_header rejects a TSCF-subtype buffer", "[wire][REQ-WIRE-003]") {
    TscfHeader tscf;
    auto buf = encode_tscf_header(tscf);
    NtscfHeader out;
    auto ec = decode_ntscf_header(buf.data(), buf.size(), out);
    REQUIRE(ec);
    REQUIRE(ec == make_error_code(WireErrc::bad_subtype));
}

TEST_CASE("decode_tscf_header rejects an NTSCF-subtype buffer", "[wire][REQ-WIRE-003]") {
    NtscfHeader ntscf;
    auto buf = encode_ntscf_header(ntscf);
    TscfHeader out;
    auto ec = decode_tscf_header(buf.data(), buf.size(), out);
    REQUIRE(ec);
    REQUIRE(ec == make_error_code(WireErrc::bad_subtype));
}

// ── ACF_ABB ─────────────────────────────────────────────────────────────────────

TEST_CASE("ACF_ABB round-trips the shared header and payload with no timestamp field", "[wire][REQ-WIRE-004][REQ-WIRE-006]") {
    AcfMessageInfo info;
    info.byte_bus_id     = 7;
    info.transaction_num = 42;
    info.evt_ack          = true;
    info.evt_op            = 5;
    info.hs                = true;
    info.cs                = false;
    info.op                = true;
    info.rsp                = false;
    info.err                = false;
    info.ms                  = false;
    info.pad                 = 2;
    info.read_size_or_segment_num = 16;

    std::vector<uint8_t> payload = {0x01, 0x02, 0x03};
    auto frame = encode_acf_abb(info, payload);
    REQUIRE(frame.size() == kAcfCommonHeaderLen + payload.size());
    REQUIRE(frame[0] == kAcfMsgTypeAbb);

    AcfMessageInfo out;
    std::vector<uint8_t> out_payload;
    REQUIRE_FALSE(decode_acf_abb(frame.data(), frame.size(), out, out_payload));
    REQUIRE(out.acf_msg_type == kAcfMsgTypeAbb);
    REQUIRE(out.byte_bus_id == info.byte_bus_id);
    REQUIRE(out.transaction_num == info.transaction_num);
    REQUIRE(out.evt_ack == info.evt_ack);
    REQUIRE(out.evt_op == info.evt_op);
    REQUIRE(out.hs == info.hs);
    REQUIRE(out.cs == info.cs);
    REQUIRE(out.op == info.op);
    REQUIRE(out.rsp == info.rsp);
    REQUIRE(out.err == info.err);
    REQUIRE(out.ms == info.ms);
    REQUIRE(out.pad == info.pad);
    REQUIRE(out.mtv == false); // ACF_ABB never carries a timestamp
    REQUIRE(out.read_size_or_segment_num == info.read_size_or_segment_num);
    REQUIRE(out_payload == payload);
}

// ── ACF_GBB ─────────────────────────────────────────────────────────────────────

TEST_CASE("ACF_GBB round-trips a 64-bit message_timestamp alongside the shared header", "[wire][REQ-WIRE-005][REQ-WIRE-006]") {
    AcfMessageInfo info;
    info.byte_bus_id     = 3;
    info.transaction_num = 9;
    info.mtv              = true;
    info.rsp                = true;
    info.op                 = false;

    uint64_t ts = 0x0123456789ABCDEFULL;
    std::vector<uint8_t> payload = {0xAA, 0xBB, 0xCC, 0xDD};
    auto frame = encode_acf_gbb(info, ts, payload);
    REQUIRE(frame.size() == kAcfCommonHeaderLen + kAcfGbbTimestampLen + payload.size());
    REQUIRE(frame[0] == kAcfMsgTypeGbb);

    AcfMessageInfo out;
    uint64_t out_ts = 0;
    std::vector<uint8_t> out_payload;
    REQUIRE_FALSE(decode_acf_gbb(frame.data(), frame.size(), out, out_ts, out_payload));
    REQUIRE(out.mtv == true);
    REQUIRE(out_ts == ts);
    REQUIRE(out_payload == payload);
}

TEST_CASE("decode_acf_abb rejects an ACF_GBB-typed buffer and vice versa", "[wire][REQ-WIRE-014]") {
    AcfMessageInfo info;
    auto gbb_frame = encode_acf_gbb(info, 0, {});
    AcfMessageInfo out;
    std::vector<uint8_t> out_payload;
    auto ec = decode_acf_abb(gbb_frame.data(), gbb_frame.size(), out, out_payload);
    REQUIRE(ec);
    REQUIRE(ec == make_error_code(WireErrc::bad_acf_msg_type));

    auto abb_frame = encode_acf_abb(info, {});
    uint64_t ts = 0;
    auto ec2 = decode_acf_gbb(abb_frame.data(), abb_frame.size(), out, ts, out_payload);
    REQUIRE(ec2);
    REQUIRE(ec2 == make_error_code(WireErrc::bad_acf_msg_type));
}

// ── Short-buffer validation ─────────────────────────────────────────────────────

TEST_CASE("Decoders reject buffers shorter than their fixed header", "[wire][REQ-WIRE-013]") {
    NtscfHeader ntscf_out;
    std::vector<uint8_t> too_short(kNtscfHeaderLen - 1, 0);
    REQUIRE(decode_ntscf_header(too_short.data(), too_short.size(), ntscf_out));

    AcfMessageInfo abb_out;
    std::vector<uint8_t> abb_payload;
    std::vector<uint8_t> too_short_acf(kAcfCommonHeaderLen - 1, 0);
    REQUIRE(decode_acf_abb(too_short_acf.data(), too_short_acf.size(), abb_out, abb_payload));
}

// ── byte_bus_id echo rule ───────────────────────────────────────────────────────

TEST_CASE("make_response echoes byte_bus_id and transaction_num from the request unchanged", "[wire][REQ-WIRE-008]") {
    auto req = make_standard_request(/*bus_id=*/9, /*transaction_num=*/17, /*write=*/false, /*read_size=*/4);
    auto resp = make_response(req, ResponseKind::ReadResponse);
    REQUIRE(resp.byte_bus_id == req.byte_bus_id);
    REQUIRE(resp.transaction_num == req.transaction_num);
}

// ── Standard request kind ───────────────────────────────────────────────────────

TEST_CASE("make_standard_request builds an unconditional ACF_ABB read request", "[wire][REQ-WIRE-009]") {
    auto req = make_standard_request(1, 1, /*write=*/false, /*read_size=*/8);
    REQUIRE(req.acf_msg_type == kAcfMsgTypeAbb);
    REQUIRE_FALSE(req.op);
    REQUIRE_FALSE(req.rsp);
    REQUIRE(req.read_size_or_segment_num == 8);
}

TEST_CASE("make_standard_request builds an unconditional ACF_ABB write request", "[wire][REQ-WIRE-009]") {
    auto req = make_standard_request(1, 1, /*write=*/true, /*read_size=*/0);
    REQUIRE(req.acf_msg_type == kAcfMsgTypeAbb);
    REQUIRE(req.op);
    REQUIRE_FALSE(req.rsp);
}

// ── Four response semantic types ────────────────────────────────────────────────

TEST_CASE("The four response kinds map onto distinct, recoverable header states", "[wire][REQ-WIRE-010]") {
    auto req = make_standard_request(2, 5, false, 4);

    auto ack = make_response(req, ResponseKind::Acknowledge);
    REQUIRE(response_kind_of(ack) == ResponseKind::Acknowledge);

    auto write_resp = make_response(req, ResponseKind::WriteResponse);
    REQUIRE(response_kind_of(write_resp) == ResponseKind::WriteResponse);

    auto read_resp = make_response(req, ResponseKind::ReadResponse);
    REQUIRE(response_kind_of(read_resp) == ResponseKind::ReadResponse);

    auto err_resp = make_response(req, ResponseKind::ErrorResponse);
    REQUIRE(response_kind_of(err_resp) == ResponseKind::ErrorResponse);

    // All four are pairwise distinguishable through the shared header alone.
    REQUIRE(response_kind_of(ack)        != response_kind_of(write_resp));
    REQUIRE(response_kind_of(write_resp) != response_kind_of(read_resp));
    REQUIRE(response_kind_of(read_resp)  != response_kind_of(err_resp));
    REQUIRE(response_kind_of(err_resp)   != response_kind_of(ack));
}

// ── Timestamp fallback rules ────────────────────────────────────────────────────

TEST_CASE("effective_timestamp prefers a valid TSCF avtp_timestamp", "[wire][REQ-WIRE-012]") {
    TscfHeader tscf;
    tscf.timestamp_valid = true;
    tscf.avtp_timestamp  = 111;

    AcfMessageInfo gbb_info;
    gbb_info.acf_msg_type = kAcfMsgTypeGbb;
    gbb_info.mtv           = true;

    auto ts = effective_timestamp(&tscf, &gbb_info, /*message_timestamp=*/222);
    REQUIRE(ts.has_value());
    REQUIRE(*ts == 111);
}

TEST_CASE("effective_timestamp falls back to a valid ACF_GBB message_timestamp", "[wire][REQ-WIRE-012]") {
    AcfMessageInfo gbb_info;
    gbb_info.acf_msg_type = kAcfMsgTypeGbb;
    gbb_info.mtv           = true;

    auto ts = effective_timestamp(nullptr, &gbb_info, /*message_timestamp=*/333);
    REQUIRE(ts.has_value());
    REQUIRE(*ts == 333);
}

TEST_CASE("effective_timestamp is nullopt, not zero, when neither source is valid", "[wire][REQ-WIRE-012]") {
    TscfHeader tscf; // timestamp_valid defaults to false
    AcfMessageInfo abb_info;
    abb_info.acf_msg_type = kAcfMsgTypeAbb; // ACF_ABB never has a timestamp

    auto ts = effective_timestamp(&tscf, &abb_info, /*message_timestamp=*/0);
    REQUIRE_FALSE(ts.has_value());

    auto ts2 = effective_timestamp(nullptr, nullptr, 0);
    REQUIRE_FALSE(ts2.has_value());
}
