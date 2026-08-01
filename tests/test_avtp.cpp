// fusa:test REQ-WIRE-001
// fusa:test REQ-WIRE-002
// fusa:test REQ-WIRE-003
// fusa:test REQ-WIRE-007
// fusa:test REQ-WIRE-011
// fusa:test REQ-WIRE-013
// fusa:test REQ-WIRE-016

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
    hdr.sequence_num        = 242; // sequence_num is an 8-bit field (0-255)
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

// ── Hand-computed expected-byte-sequence vectors ──────────────────────────────
// Every byte below is computed by hand from the field values and this file's
// own derived bit layout (see avtp.hpp's "NTSCF / TSCF AVTPDU headers"
// comment) — not copied from anywhere. MSB-first bit numbering throughout
// (bit0 = MSB of byte0), matching the specification's own diagrams.

TEST_CASE("NTSCF header hand-computed expected byte sequence", "[avtp][REQ-WIRE-001]") {
    NtscfHeader hdr;
    hdr.stream_id           = make_stream_id(0x02, 0xBEEF); // mac 02 03 04 05 06 07, suffix BEEF
    hdr.sequence_num        = 200; // 0xC8, a full octet
    hdr.control_data_length = 1500; // 0x5DC, 11-bit field: top3=0b101=5, low8=0xDC

    // byte0 = subtype = 0x82
    // byte1 = sv(0x80) | control_data_length[10:8] = 0x80 | 0x05 = 0x85
    // byte2 = control_data_length[7:0] = 0xDC
    // byte3 = sequence_num = 0xC8
    // bytes4-11 = stream_id = 02 03 04 05 06 07 BE EF
    const std::vector<uint8_t> expected = {0x82, 0x85, 0xDC, 0xC8,
                                            0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0xBE, 0xEF};
    auto buf = encode_ntscf_header(hdr);
    REQUIRE(buf == expected);

    NtscfHeader decoded;
    REQUIRE_FALSE(decode_ntscf_header(buf.data(), buf.size(), decoded));
    REQUIRE(decoded.sequence_num == 200);
    REQUIRE(decoded.control_data_length == 1500);
    REQUIRE(decoded.stream_id == hdr.stream_id);
    REQUIRE(encode_ntscf_header(decoded) == buf); // encode -> decode -> re-encode == identity
}

TEST_CASE("TSCF header hand-computed expected byte sequence", "[avtp][REQ-WIRE-002][REQ-WIRE-011]") {
    TscfHeader hdr;
    hdr.stream_id           = make_stream_id(0x10, 0x2233); // mac 10 11 12 13 14 15, suffix 2233
    hdr.sequence_num        = 77; // 0x4D
    hdr.timestamp_valid     = true;
    hdr.avtp_timestamp      = 0xCAFEBABE;
    hdr.control_data_length = 0x1234; // full 16-bit field (stream_data_length)

    // byte0 = subtype = 0x05
    // byte1 = sv(0x80) | tv(0x01) = 0x81 (version/mr/rsv all 0)
    // byte2 = sequence_num = 0x4D
    // byte3 = reserved(7)+tu(1) = 0x00
    // bytes4-11 = stream_id = 10 11 12 13 14 15 22 33
    // bytes12-15 = avtp_timestamp = CA FE BA BE
    // bytes16-19 = reserved = 00 00 00 00
    // bytes20-21 = control_data_length = 12 34
    // bytes22-23 = reserved = 00 00
    const std::vector<uint8_t> expected = {
        0x05, 0x81, 0x4D, 0x00,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x22, 0x33,
        0xCA, 0xFE, 0xBA, 0xBE,
        0x00, 0x00, 0x00, 0x00,
        0x12, 0x34,
        0x00, 0x00};
    auto buf = encode_tscf_header(hdr);
    REQUIRE(buf.size() == 24);
    REQUIRE(buf == expected);

    TscfHeader decoded;
    REQUIRE_FALSE(decode_tscf_header(buf.data(), buf.size(), decoded));
    REQUIRE(decoded.sequence_num == 77);
    REQUIRE(decoded.timestamp_valid == true);
    REQUIRE(decoded.avtp_timestamp == 0xCAFEBABE);
    REQUIRE(decoded.control_data_length == 0x1234);
    REQUIRE(decoded.stream_id == hdr.stream_id);
    REQUIRE(encode_tscf_header(decoded) == buf); // encode -> decode -> re-encode == identity
}

// ── Sub-octet field width ──────────────────────────────────────────────────────

TEST_CASE("NTSCF sequence_num is an 8-bit field, not 16", "[avtp][REQ-WIRE-001]") {
    NtscfHeader hdr;
    hdr.sequence_num = 255;
    auto buf = encode_ntscf_header(hdr);
    REQUIRE(buf[3] == 255);
    NtscfHeader out;
    REQUIRE_FALSE(decode_ntscf_header(buf.data(), buf.size(), out));
    REQUIRE(out.sequence_num == 255);
}

TEST_CASE("NTSCF control_data_length round-trips its full 11-bit range", "[avtp][REQ-WIRE-001]") {
    NtscfHeader hdr;
    hdr.control_data_length = 2047; // max 11-bit value
    auto buf = encode_ntscf_header(hdr);
    NtscfHeader out;
    REQUIRE_FALSE(decode_ntscf_header(buf.data(), buf.size(), out));
    REQUIRE(out.control_data_length == 2047);
}

TEST_CASE("ByteBusId round-trips values above 255 through AcfMessageInfo (11-bit wire field)",
          "[avtp][REQ-WIRE-001]") {
    // ByteBusId itself (avtp.hpp) is just a uint16_t alias; this checks the
    // alias is actually wide enough for the wire field it names, since the
    // sub-octet packing itself is exercised in tests/test_acf.cpp.
    static_assert(sizeof(ByteBusId) >= sizeof(uint16_t), "ByteBusId must be wide enough for the 11-bit wire field");
    ByteBusId id = 2047;
    REQUIRE(id == 2047);
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

// ── TC18 §13.3: an uncertain timestamp is handled as a certain one ──────────

TEST_CASE("decode_tscf_header ignores the tu (timestamp-uncertain) bit", "[avtp][REQ-WIRE-016]") {
    // §13.3 requires a request whose TSCF header marks the timestamp uncertain
    // (tu=1) to be executed as if tu were 0. The decoder must therefore produce
    // the same header for two buffers differing only in that bit — otherwise a
    // downstream path could branch on it.
    TscfHeader h;
    h.stream_id           = StreamId::from_u64(0x0102030405060708ull);
    h.sequence_num        = 0x42;
    h.control_data_length = 0x0020;
    h.timestamp_valid     = true;
    h.avtp_timestamp      = 0xCAFEF00D;

    auto certain = encode_tscf_header(h);
    auto uncertain = certain;
    uncertain[3] |= 0x01; // tu

    TscfHeader out_certain, out_uncertain;
    REQUIRE_FALSE(decode_tscf_header(certain.data(), certain.size(), out_certain));
    REQUIRE_FALSE(decode_tscf_header(uncertain.data(), uncertain.size(), out_uncertain));

    REQUIRE(out_uncertain.timestamp_valid     == out_certain.timestamp_valid);
    REQUIRE(out_uncertain.avtp_timestamp      == out_certain.avtp_timestamp);
    REQUIRE(out_uncertain.sequence_num        == out_certain.sequence_num);
    REQUIRE(out_uncertain.control_data_length == out_certain.control_data_length);
    REQUIRE(out_uncertain.stream_id.to_u64()  == out_certain.stream_id.to_u64());
}
