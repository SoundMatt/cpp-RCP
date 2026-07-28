// fusa:test REQ-WIRE-001
// fusa:test REQ-WIRE-002
// fusa:test REQ-WIRE-003
// fusa:test REQ-WIRE-007
// fusa:test REQ-WIRE-011
// fusa:test REQ-WIRE-013

// Tests for rcp/avtp.hpp — the TC18 AVTPDU header-framing half of the wire
// codec (ROADMAP.md milestone 44, "Wire Format Core", v2.0.0; split from a
// single rcp/wire.hpp into rcp/avtp.hpp + rcp/acf.hpp per RELAY spec
// §13.7.2's standard module-name registry). ACF_ABB/ACF_GBB message-format
// tests live in tests/test_acf.cpp.

#include <catch2/catch_test_macros.hpp>
#include <rcp/avtp.hpp>

using namespace rcp::avtp;

namespace {

StreamId make_stream_id(uint8_t mac_seed, uint16_t suffix) {
    StreamId id;
    for (auto& b : id.mac) b = mac_seed++;
    id.suffix = suffix;
    return id;
}

} // namespace

// ── StreamId ───────────────────────────────────────────────────────────────────

TEST_CASE("StreamId round-trips through to_u64/from_u64", "[avtp][REQ-WIRE-007]") {
    auto id = make_stream_id(0x02, 0xBEEF);
    auto back = StreamId::from_u64(id.to_u64());
    REQUIRE(back == id);
    REQUIRE(back.suffix == 0xBEEF);
}

TEST_CASE("Distinct MAC halves produce distinct StreamId values", "[avtp][REQ-WIRE-007]") {
    auto a = make_stream_id(0x02, 0x0001);
    auto b = make_stream_id(0x03, 0x0001);
    REQUIRE(a != b);
}

// ── NTSCF header ───────────────────────────────────────────────────────────────

TEST_CASE("NTSCF header round-trips stream_id, sequence_num, control_data_length", "[avtp][REQ-WIRE-001]") {
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

TEST_CASE("TSCF header round-trips avtp_timestamp when timestamp_valid is set", "[avtp][REQ-WIRE-002][REQ-WIRE-011]") {
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

TEST_CASE("TSCF header marks timestamp invalid distinctly from a zero timestamp", "[avtp][REQ-WIRE-011]") {
    TscfHeader hdr;
    hdr.timestamp_valid = false;
    hdr.avtp_timestamp  = 0;

    auto buf = encode_tscf_header(hdr);
    TscfHeader out;
    REQUIRE_FALSE(decode_tscf_header(buf.data(), buf.size(), out));
    REQUIRE_FALSE(out.timestamp_valid);
}

// ── AVTPDU subtype validation ──────────────────────────────────────────────────

TEST_CASE("decode_ntscf_header rejects a TSCF-subtype buffer", "[avtp][REQ-WIRE-003]") {
    TscfHeader tscf;
    auto buf = encode_tscf_header(tscf);
    NtscfHeader out;
    auto ec = decode_ntscf_header(buf.data(), buf.size(), out);
    REQUIRE(ec);
    REQUIRE(ec == make_error_code(AvtpErrc::bad_subtype));
}

TEST_CASE("decode_tscf_header rejects an NTSCF-subtype buffer", "[avtp][REQ-WIRE-003]") {
    NtscfHeader ntscf;
    auto buf = encode_ntscf_header(ntscf);
    TscfHeader out;
    auto ec = decode_tscf_header(buf.data(), buf.size(), out);
    REQUIRE(ec);
    REQUIRE(ec == make_error_code(AvtpErrc::bad_subtype));
}

// ── Short-buffer validation ─────────────────────────────────────────────────────

TEST_CASE("decode_ntscf_header rejects a buffer shorter than the fixed header", "[avtp][REQ-WIRE-013]") {
    NtscfHeader ntscf_out;
    std::vector<uint8_t> too_short(kNtscfHeaderLen - 1, 0);
    REQUIRE(decode_ntscf_header(too_short.data(), too_short.size(), ntscf_out));
}

TEST_CASE("decode_tscf_header rejects a buffer shorter than the fixed header", "[avtp][REQ-WIRE-013]") {
    TscfHeader tscf_out;
    std::vector<uint8_t> too_short(kTscfHeaderLen - 1, 0);
    REQUIRE(decode_tscf_header(too_short.data(), too_short.size(), tscf_out));
}
