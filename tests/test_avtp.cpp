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

// ── Phase 17 (c-RCP port): sv/version/mr/tu/reserved0/reserved1 ──────────────
// Additive fields ported from c-RCP's rcp_avtp_ntscf_header_t/
// rcp_avtp_tscf_header_t; defaults reproduce this codec's own pre-existing
// hardcoded encode behavior (sv=1, version=0, mr=0) exactly.

TEST_CASE("NTSCF header round-trips sv/version", "[avtp]") {
    NtscfHeader hdr;
    hdr.sv               = true;
    hdr.version          = 3; // TC18 fixes this at 0 for this spec revision, but the wire
                               // codec itself must round-trip whatever value it is given.
    hdr.stream_id        = make_stream_id(0x02, 1);

    auto buf = encode_ntscf_header(hdr);
    NtscfHeader out;
    REQUIRE_FALSE(decode_ntscf_header(buf.data(), buf.size(), out));
    REQUIRE(out.sv == hdr.sv);
    REQUIRE(out.version == hdr.version);
}

TEST_CASE("TSCF header round-trips sv/version/mr/tu", "[avtp]") {
    TscfHeader hdr;
    hdr.sv               = true;
    hdr.version          = 3; // same round-trip-whatever-given rationale as the NTSCF test above
    hdr.timestamp_valid  = true;
    hdr.tu               = false;
    hdr.mr               = true;
    hdr.sequence_num     = 0x99;
    hdr.stream_id        = make_stream_id(0x02, 2);
    hdr.avtp_timestamp   = 0x12345678;

    auto buf = encode_tscf_header(hdr);
    TscfHeader out;
    REQUIRE_FALSE(decode_tscf_header(buf.data(), buf.size(), out));
    REQUIRE(out.sv == hdr.sv);
    REQUIRE(out.version == hdr.version);
    REQUIRE(out.mr == hdr.mr);
    REQUIRE(out.timestamp_valid == hdr.timestamp_valid);
    REQUIRE(out.tu == hdr.tu);
    REQUIRE(out.sequence_num == hdr.sequence_num);
    REQUIRE(out.avtp_timestamp == hdr.avtp_timestamp);
    REQUIRE(out.stream_id == hdr.stream_id);
}

// ── §13.3 tu=1/tu=0 equivalence (REQ-AVTP-023) ────────────────────────────────

TEST_CASE("TSCF header decode reports tu=1 and tu=0 faithfully", "[avtp]") {
    TscfHeader hdr;
    hdr.stream_id = make_stream_id(0x02, 1);
    hdr.tu        = true;

    auto buf = encode_tscf_header(hdr);
    TscfHeader out;
    REQUIRE_FALSE(decode_tscf_header(buf.data(), buf.size(), out));
    REQUIRE(out.tu == true);

    hdr.tu = false;
    buf    = encode_tscf_header(hdr);
    REQUIRE_FALSE(decode_tscf_header(buf.data(), buf.size(), out));
    REQUIRE(out.tu == false);
}

// ── REQ-TIMED-012: TSCF avtp_timestamp -> gPTP-domain reconstruction
// (ported from c-RCP's rcp_avtp_extend_timestamp()) ───────────────────────────

TEST_CASE("extend_timestamp: wire_ts's low bits already equal reference_now's own", "[avtp]") {
    uint64_t now = 0x0000123456789ABCull;
    REQUIRE(extend_timestamp(static_cast<uint32_t>(now), now) == now);
}

TEST_CASE("extend_timestamp: near-future value within half a period needs no wraparound", "[avtp]") {
    uint64_t now     = 0x0000000100000000ull; // low 32 bits == 0
    uint32_t wire_ts = 1000u;
    REQUIRE(extend_timestamp(wire_ts, now) == now + 1000u);
}

TEST_CASE("extend_timestamp: near-past value within half a period needs no wraparound", "[avtp]") {
    uint64_t now     = 0x0000000100001000ull; // low 32 bits == 0x1000
    uint32_t wire_ts = 0x1000u - 500u;
    REQUIRE(extend_timestamp(wire_ts, now) == now - 500u);
}

TEST_CASE("extend_timestamp wraps forward when wire_ts is just past a 2^32 boundary", "[avtp]") {
    // reference_now sits just below a 2^32 boundary; wire_ts's own low bits
    // are numerically small (just above 0), which naive zero-extension
    // would misread as ~4.29 seconds in the past. The correct
    // reconstruction recognizes wire_ts is actually ~100ns in the FUTURE,
    // one period up from the naive candidate.
    uint64_t now     = (uint64_t{1} << 32) - 100u; // 100ns before the boundary
    uint32_t wire_ts = 0u; // the boundary itself, i.e. now + 100
    REQUIRE(extend_timestamp(wire_ts, now) == now + 100u);
}

TEST_CASE("extend_timestamp wraps backward when wire_ts is just before a 2^32 boundary", "[avtp]") {
    // Symmetric case: reference_now sits just above a 2^32 boundary;
    // wire_ts's own low bits are numerically large (near 2^32-1), which
    // naive zero-extension would misread as ~4.29 seconds in the future.
    uint64_t now     = (uint64_t{1} << 32) + 100u; // 100ns after the boundary
    uint32_t wire_ts = 0xFFFFFFFFu; // the boundary minus 1, i.e. now - 101
    REQUIRE(extend_timestamp(wire_ts, now) == now - 101u);
}

TEST_CASE("extend_timestamp: exactly half a period ahead prefers the un-wrapped candidate", "[avtp]") {
    // The tie-break condition is "> half", not ">=", so exactly half stays
    // with the un-wrapped (forward) candidate.
    uint64_t now     = 0x0000000200000000ull; // low 32 bits == 0
    uint32_t wire_ts = static_cast<uint32_t>((uint64_t{1} << 32) / 2); // 2^31
    REQUIRE(extend_timestamp(wire_ts, now) == now + (uint64_t{1} << 31));
}

TEST_CASE("extend_timestamp: exactly half a period behind prefers the un-wrapped candidate", "[avtp]") {
    uint64_t now     = 0x0000000300000000ull | (uint64_t{1} << 31); // low 32 bits == 2^31
    uint32_t wire_ts = 0u;
    REQUIRE(extend_timestamp(wire_ts, now) == now - (uint64_t{1} << 31));
}

// ── Subtype dispatch & the TSCF-without-time-sync drop rule (ported from
// c-RCP's rcp_avtp_peek_subtype()/_should_drop_tscf()/_tscf_reserved_all_zero()) ──

TEST_CASE("peek_subtype reads the first byte", "[avtp]") {
    NtscfHeader hdr;
    hdr.stream_id = make_stream_id(0x02, 1);
    auto buf = encode_ntscf_header(hdr);

    uint8_t subtype = 0;
    REQUIRE_FALSE(peek_subtype(buf.data(), buf.size(), subtype));
    REQUIRE(subtype == kSubtypeNtscf);
}

TEST_CASE("peek_subtype rejects an empty buffer", "[avtp]") {
    uint8_t subtype = 0;
    REQUIRE(peek_subtype(nullptr, 0, subtype));
}

TEST_CASE("should_drop_tscf drops a TSCF frame when time sync is unsupported and the policy is Drop",
          "[avtp]") {
    // TC18 §11.1's own unconditional wording, and this codec's original
    // (still default) disposition.
    REQUIRE(should_drop_tscf(false, kSubtypeTscf, TscfFallback::Drop));
}

TEST_CASE("should_drop_tscf never drops a TSCF frame when time sync is supported", "[avtp]") {
    REQUIRE_FALSE(should_drop_tscf(true, kSubtypeTscf, TscfFallback::Drop));
}

TEST_CASE("should_drop_tscf never drops an NTSCF frame regardless of policy or time sync", "[avtp]") {
    REQUIRE_FALSE(should_drop_tscf(false, kSubtypeNtscf, TscfFallback::Drop));
    REQUIRE_FALSE(should_drop_tscf(true, kSubtypeNtscf, TscfFallback::Drop));
    REQUIRE_FALSE(should_drop_tscf(false, kSubtypeNtscf, TscfFallback::Ignore));
    REQUIRE_FALSE(should_drop_tscf(true, kSubtypeNtscf, TscfFallback::Ignore));
}

TEST_CASE("should_drop_tscf with TscfFallback::Ignore does not drop an unsupported-time-sync TSCF frame",
          "[avtp]") {
    // TC18 §13.3's own configurable alternative to §11.1's unconditional
    // wording — same inputs as the Drop-policy test above, only the policy
    // differs, isolating this behavior from every other case.
    REQUIRE_FALSE(should_drop_tscf(false, kSubtypeTscf, TscfFallback::Ignore));
}

TEST_CASE("should_drop_tscf: Ignore policy is irrelevant once time sync is supported", "[avtp]") {
    REQUIRE_FALSE(should_drop_tscf(true, kSubtypeTscf, TscfFallback::Ignore));
}

TEST_CASE("tscf_reserved_all_zero is true for a freshly decoded conformant header", "[avtp]") {
    TscfHeader hdr;
    hdr.stream_id = make_stream_id(0x02, 1);
    auto buf = encode_tscf_header(hdr);

    // encode_tscf_header always zero-fills the reserved octets regardless
    // of hdr's own (here default, but irrelevant) reserved0/reserved1 — see
    // TscfHeader's own doc comment — so a conformant round trip always
    // decodes all-zero.
    TscfHeader decoded;
    REQUIRE_FALSE(decode_tscf_header(buf.data(), buf.size(), decoded));
    REQUIRE(tscf_reserved_all_zero(decoded));
}

TEST_CASE("tscf_reserved_all_zero is false when reserved0 or reserved1 is nonzero", "[avtp]") {
    TscfHeader hdr;
    hdr.reserved0 = 1;
    REQUIRE_FALSE(tscf_reserved_all_zero(hdr));

    TscfHeader hdr2;
    hdr2.reserved1 = 1;
    REQUIRE_FALSE(tscf_reserved_all_zero(hdr2));
}

TEST_CASE("decode_tscf_header reads nonzero reserved bytes off the wire", "[avtp]") {
    // Decoding a hand-built wire frame whose own bytes 16-19 are nonzero
    // (simulating a non-conformant or future-revision sender) proves decode
    // actually reads the reserved octets off the wire, not merely that a
    // hand-set struct field round-trips.
    std::vector<uint8_t> b(kTscfHeaderLen, 0);
    b[0] = kSubtypeTscf;
    b[1] = static_cast<uint8_t>(1u << 7); // sv=1
    b[16] = 0xDE; b[17] = 0xAD; b[18] = 0xBE; b[19] = 0xEF; // reserved0
    b[22] = 0xAA; b[23] = 0xBB; // reserved1

    TscfHeader decoded;
    REQUIRE_FALSE(decode_tscf_header(b.data(), b.size(), decoded));
    REQUIRE(decoded.reserved0 == 0xDEADBEEFu);
    REQUIRE(decoded.reserved1 == 0xAABBu);
    REQUIRE_FALSE(tscf_reserved_all_zero(decoded));
}
