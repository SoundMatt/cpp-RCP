// fusa:test REQ-WIRE-004
// fusa:test REQ-WIRE-005
// fusa:test REQ-WIRE-006
// fusa:test REQ-WIRE-008
// fusa:test REQ-WIRE-009
// fusa:test REQ-WIRE-010
// fusa:test REQ-WIRE-012
// fusa:test REQ-WIRE-013
// fusa:test REQ-WIRE-014
// fusa:test REQ-WIRE-015
// fusa:test REQ-ACF-001
// fusa:test REQ-ACF-004
// fusa:test REQ-ACF-012
// fusa:test REQ-ACF-013
// fusa:test REQ-ACF-014
// fusa:test REQ-ACF-017
// fusa:test REQ-ACF-019
// fusa:test REQ-ACF-021
// fusa:test REQ-ACF-023
// fusa:test REQ-ACF-024
// fusa:test REQ-ACF-025
// fusa:test REQ-ACF-026
// fusa:test REQ-ACF-027
// fusa:test REQ-ACF-028
// fusa:test REQ-ACF-029
// fusa:test REQ-ACF-030
// fusa:test REQ-ACF-031
// fusa:test REQ-ACF-032
// fusa:test REQ-ACF-033
// fusa:test REQ-ACF-038
// fusa:test REQ-ACF-044
// fusa:test REQ-ACF-047
// fusa:test REQ-ACF-048
// fusa:test REQ-ACF-049
// fusa:test REQ-ACF-050
// fusa:test REQ-ACF-051
// fusa:test REQ-ACF-052
// fusa:test REQ-ACF-053
// fusa:test REQ-WIREERR-001
// fusa:test REQ-ADC-034

// Tests for rcp/acf.hpp — the ACF_ABB/ACF_GBB message-format half of the TC18
// wire codec (ROADMAP.md milestone 44, "Wire Format Core", v2.0.0; split
// from a single rcp/wire.hpp into rcp/avtp.hpp + rcp/acf.hpp per RELAY spec
// §13.7.2's standard module-name registry). AVTPDU header-framing tests
// live in tests/test_avtp.cpp.

#include <catch2/catch_test_macros.hpp>
#include <rcp/acf.hpp>

using namespace rcp::acf;
using rcp::avtp::TscfHeader;

// ── ACF_ABB ─────────────────────────────────────────────────────────────────────

TEST_CASE("ACF_ABB round-trips the shared header and payload with no timestamp field", "[acf][REQ-WIRE-004][REQ-WIRE-006][REQ-ACF-014][REQ-ACF-019][REQ-ACF-049]") {
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
    // acf_msg_type is a 7-bit field sharing byte0 with acf_msg_length's MSB
    // (see acf.hpp's "ACF shared header" comment) — frame[0] is therefore
    // NOT acf_msg_type directly; peek_acf_msg_type() extracts it correctly.
    REQUIRE(peek_acf_msg_type(frame.data()) == kAcfMsgTypeAbb);

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

TEST_CASE("ACF_GBB round-trips a 64-bit message_timestamp alongside the shared header", "[acf][REQ-WIRE-005][REQ-WIRE-006][REQ-ACF-044]") {
    AcfMessageInfo info;
    info.byte_bus_id     = 3;
    info.transaction_num = 9;
    info.mtv              = true;
    info.rsp                = true;
    info.op                 = false;

    uint64_t ts = 0x0123456789ABCDEFULL;
    std::vector<uint8_t> payload = {0xAA, 0xBB, 0xCC, 0xDD};
    auto frame = encode_acf_gbb(info, ts, payload);
    REQUIRE(frame.size() == kAcfGbbMessageInfoLen + payload.size());
    REQUIRE(peek_acf_msg_type(frame.data()) == kAcfMsgTypeGbb);

    AcfMessageInfo out;
    uint64_t out_ts = 0;
    std::vector<uint8_t> out_payload;
    REQUIRE_FALSE(decode_acf_gbb(frame.data(), frame.size(), out, out_ts, out_payload));
    REQUIRE(out.mtv == true);
    REQUIRE(out_ts == ts);
    REQUIRE(out_payload == payload);
}

// ── Hand-computed expected-byte-sequence vectors ──────────────────────────────
// Every byte below is computed by hand from the field values and this
// file's own derived bit layout (see acf.hpp's "ACF shared header" comment)
// — not copied from anywhere, and in particular not read back out of the
// encoder's own output (which would only prove the encoder agrees with
// itself). Byte-by-byte derivation (MSB-first bit numbering, bit0 = MSB of
// byte0):
//
// byte_message_info, octets 0..7 (identical layout for ACF_ABB and
// ACF_GBB — see acf.hpp's "ACF shared header" comment):
//   byte0 = (acf_msg_type[6:0] << 1) | acf_msg_length[8]
//   byte1 = acf_msg_length[7:0]
//   byte2 = (pad[1:0] << 6) | (mtv << 5) | (rsv=00 << 3) | byte_bus_id[10:8]
//   byte3 = byte_bus_id[7:0]
//   byte4 = (evt[3:0] << 4) | (rsv=00 << 2) | (hs << 1) | cs
//     where evt[3:0] = (evt_ack << 3) | evt_op[2:0]
//   byte5 = transaction_num
//   byte6 = (op << 7) | (rsp << 6) | (err << 5) | (ms << 4) | read_size[11:8]
//   byte7 = read_size[7:0]
//
// ACF_ABB has no message_timestamp field at all — byte_msg_payload begins
// immediately at octet 8. ACF_GBB appends the 64-bit message_timestamp
// immediately AFTER this same 8-byte header (octets 8..15), then
// byte_msg_payload at octet 16 — ported from c-RCP's acf.h/acf.c, this
// project's RC5-conformant reference for this module (see acf.hpp's own
// "ACF_GBB Message Info wire geometry" comment for the full derivation,
// including c-RCP's own test_peek_gbb_request_type() and its
// .fusa-reqs.json REQ-ACF-044 citation).

TEST_CASE("ACF_ABB hand-computed expected byte sequence", "[acf][REQ-WIRE-004][REQ-WIRE-006]") {
    AcfMessageInfo info;
    info.acf_msg_length = 5;    // 9 bits: 0b0_0000_0101
    info.pad             = 2;    // 2 bits: 0b10
    info.byte_bus_id      = 7;    // 11 bits: 0b000_0000_0111
    info.evt_ack           = true; // evt[3]
    info.evt_op             = 5;    // evt[2:0] = 0b101 -> evt = 0b1101 = 0xD
    info.hs                  = true;
    info.cs                  = false;
    info.transaction_num      = 42;   // 0x2A
    info.op                    = true;
    info.rsp                    = false;
    info.err                     = false;
    info.ms                       = false;
    info.read_size_or_segment_num = 16; // 12 bits: 0b0000_0001_0000

    // byte0 = (0x0E << 1) | (5 >> 8 & 1) = 0x1C | 0 = 0x1C
    // byte1 = 5 & 0xFF = 0x05
    // byte2 = (2 << 6) | (0 << 5) | (7 >> 8 & 7) = 0x80 | 0 | 0 = 0x80
    // byte3 = 7 & 0xFF = 0x07
    // byte4 = (0xD << 4) | (1 << 1) | 0 = 0xD0 | 0x02 = 0xD2
    // byte5 = 42 = 0x2A
    // byte6 = (1<<7) | 0 | 0 | 0 | (16 >> 8 & 0xF) = 0x80 | 0 = 0x80
    // byte7 = 16 & 0xFF = 0x10
    const std::vector<uint8_t> expected_header = {0x1C, 0x05, 0x80, 0x07, 0xD2, 0x2A, 0x80, 0x10};
    const std::vector<uint8_t> payload = {0x01, 0x02, 0x03};

    auto frame = encode_acf_abb(info, payload);
    REQUIRE(frame.size() == expected_header.size() + payload.size());
    std::vector<uint8_t> header(frame.begin(), frame.begin() + static_cast<long>(kAcfCommonHeaderLen));
    REQUIRE(header == expected_header);
    REQUIRE(std::vector<uint8_t>(frame.begin() + static_cast<long>(kAcfCommonHeaderLen), frame.end()) ==
            payload);

    // Re-encoding a decode of this exact hand-computed buffer must reproduce
    // it byte for byte (encode -> decode -> re-encode == identity).
    AcfMessageInfo decoded;
    std::vector<uint8_t> decoded_payload;
    REQUIRE_FALSE(decode_acf_abb(frame.data(), frame.size(), decoded, decoded_payload));
    auto re_encoded = encode_acf_abb(decoded, decoded_payload);
    REQUIRE(re_encoded == frame);
}

TEST_CASE("ACF_GBB hand-computed expected byte sequence", "[acf][REQ-WIRE-005][REQ-WIRE-006][REQ-ACF-044]") {
    AcfMessageInfo info;
    info.acf_msg_length = 7;    // 9 bits
    info.pad             = 1;    // 2 bits
    info.mtv               = true;
    info.byte_bus_id        = 300;  // 11 bits, exercises the >8-bit range: 0b001_0010_1100
    info.evt_ack             = false;
    info.evt_op               = 3;    // evt = 0b0011 = 0x3
    info.hs                    = false;
    info.cs                    = true;
    info.transaction_num        = 200;  // 0xC8
    info.op                      = false;
    info.rsp                      = true;
    info.err                       = true;
    info.ms                         = true;
    info.read_size_or_segment_num = 4095; // 12 bits, max value: 0xFFF

    // byte_message_info, octets 0..7 (contiguous — see this file's own
    // layout comment above):
    //   byte0 = (0x0D << 1) | (7 >> 8 & 1) = 0x1A | 0 = 0x1A
    //   byte1 = 7 & 0xFF = 0x07
    //   byte2 = (1 << 6) | (1 << 5) | (300 >> 8 & 7) = 0x40 | 0x20 | 0x01 = 0x61
    //   byte3 = 300 & 0xFF = 0x2C
    //   byte4 = (0x3 << 4) | (0 << 1) | 1 = 0x30 | 0x01 = 0x31
    //   byte5 = 200 = 0xC8
    //   byte6 = 0 | (1<<6) | (1<<5) | (1<<4) | (4095 >> 8 & 0xF) = 0x40|0x20|0x10|0x0F = 0x7F
    //   byte7 = 4095 & 0xFF = 0xFF
    const std::vector<uint8_t> expected_header = {0x1A, 0x07, 0x61, 0x2C, 0x31, 0xC8, 0x7F, 0xFF};
    const uint64_t ts = 0x1122334455667788ULL;
    const std::vector<uint8_t> expected_ts = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
    const std::vector<uint8_t> payload = {0xAA, 0xBB, 0xCC, 0xDD};

    // The whole expected ACF_GBB message, written out as one literal at the
    // spec-derived octet positions rather than assembled from the encoder's
    // own constants: byte_message_info || message_timestamp || payload.
    const std::vector<uint8_t> expected_frame = {
        // octets 0..7   — byte_message_info (contiguous, same layout ACF_ABB uses)
        0x1A, 0x07, 0x61, 0x2C, 0x31, 0xC8, 0x7F, 0xFF,
        // octets 8..15  — message_timestamp, big-endian
        0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
        // octets 16..19 — byte_msg_payload
        0xAA, 0xBB, 0xCC, 0xDD,
    };

    auto frame = encode_acf_gbb(info, ts, payload);
    REQUIRE(frame.size() == 20); // 8 + 8 + 4
    REQUIRE(frame == expected_frame);

    // Same assertion again, sliced field by field at literal offsets, so a
    // failure names which field moved rather than just "the buffer differs".
    REQUIRE(std::vector<uint8_t>(frame.begin() + 0,  frame.begin() + 8)  == expected_header);
    REQUIRE(std::vector<uint8_t>(frame.begin() + 8,  frame.begin() + 16) == expected_ts);
    REQUIRE(std::vector<uint8_t>(frame.begin() + 16, frame.end())        == payload);

    // Regression guard for the spliced-layout bug this pass reverts: under
    // that (wrong) layout, octet 4 held quadlet 1's evt/hs/cs byte (0x31)
    // and octet 8 held the timestamp's first byte (0x11). Under the correct
    // contiguous layout it's the other way around.
    REQUIRE(frame[4] == 0x31); // still inside the contiguous header, not the timestamp
    REQUIRE(frame[8] == 0x11); // timestamp MSB, right after the 8-byte header

    // Decoding the hand-written literal (not the encoder's output) must
    // recover every field — this is the direction that proves the decoder
    // reads real spec-shaped bytes, not just its own encoder's bytes.
    {
        AcfMessageInfo from_literal;
        uint64_t from_literal_ts = 0;
        std::vector<uint8_t> from_literal_payload;
        REQUIRE_FALSE(decode_acf_gbb(expected_frame.data(), expected_frame.size(), from_literal,
                                      from_literal_ts, from_literal_payload));
        REQUIRE(from_literal.acf_msg_type == kAcfMsgTypeGbb);
        REQUIRE(from_literal.acf_msg_length == 7);
        REQUIRE(from_literal.pad == 1);
        REQUIRE(from_literal.mtv == true);
        REQUIRE(from_literal.byte_bus_id == 300);
        REQUIRE(from_literal.evt_ack == false);
        REQUIRE(from_literal.evt_op == 3);
        REQUIRE(from_literal.hs == false);
        REQUIRE(from_literal.cs == true);
        REQUIRE(from_literal.transaction_num == 200);
        REQUIRE(from_literal.op == false);
        REQUIRE(from_literal.rsp == true);
        REQUIRE(from_literal.err == true);
        REQUIRE(from_literal.ms == true);
        REQUIRE(from_literal.read_size_or_segment_num == 4095);
        REQUIRE(from_literal_ts == ts);
        REQUIRE(from_literal_payload == payload);
    }

    AcfMessageInfo decoded;
    uint64_t decoded_ts = 0;
    std::vector<uint8_t> decoded_payload;
    REQUIRE_FALSE(decode_acf_gbb(frame.data(), frame.size(), decoded, decoded_ts, decoded_payload));
    REQUIRE(decoded.byte_bus_id == 300);
    auto re_encoded = encode_acf_gbb(decoded, decoded_ts, decoded_payload);
    REQUIRE(re_encoded == frame);
}

// This is an independent cross-check against a fully worked numeric example
// elsewhere in the specification (an ACF_ABB message with a 6-byte payload
// and 2 bytes of padding): quadlet-counting the 8-byte Message Info (2
// quadlets) + 8-byte padded payload (2 quadlets) + a 4-byte CRC trailer (1
// quadlet, per rcp/e2e.hpp's kCrcLengthAdjustQuadlets) gives 5 quadlets —
// this test only checks that this file's own encoder faithfully places
// whatever acf_msg_length/pad values a caller (such as rcp/e2e.hpp) computed
// into the correct wire bits; it does not itself compute quadlet counts.
TEST_CASE("acf_msg_length/pad wire bits round-trip the specification's own worked-example values",
          "[acf][REQ-WIRE-004]") {
    AcfMessageInfo info;
    info.acf_msg_length = 5; // Message Info (2 quadlets) + padded payload (2) + CRC trailer (1)
    info.pad             = 2; // 6 payload bytes + 2 pad bytes = 8 bytes = 2 quadlets
    std::vector<uint8_t> payload(8, 0); // 6 "real" + 2 padding bytes, caller-padded per convention

    auto frame = encode_acf_abb(info, payload);
    AcfMessageInfo out;
    std::vector<uint8_t> out_payload;
    REQUIRE_FALSE(decode_acf_abb(frame.data(), frame.size(), out, out_payload));
    REQUIRE(out.acf_msg_length == 5);
    REQUIRE(out.pad == 2);
}

// ── evt[3:0] / response semantics ──────────────────────────────────────────────

TEST_CASE("evt occupies exactly 4 bits and evt_ack/evt_op together reproduce evt[3:0] == 0xF "
          "for kEvtAcknowledge",
          "[acf][REQ-WIRE-010]") {
    AcfMessageInfo info;
    info.evt_ack = true;
    info.evt_op  = 0x7; // together: 0b1111 = 0xF

    uint8_t hdr[kAcfCommonHeaderLen];
    encode_acf_message_info(info, hdr);
    REQUIRE(((hdr[4] >> 4) & 0x0F) == kEvtAcknowledge);

    AcfMessageInfo out;
    decode_acf_message_info(hdr, out);
    REQUIRE(out.evt_ack == true);
    REQUIRE(out.evt_op == 0x7);
}

TEST_CASE("make_response sets rsp on every response kind, including Acknowledge",
          "[acf][REQ-WIRE-010]") {
    // Table 15 (extraction): every response — Acknowledge included — carries
    // rsp=1; only requests carry rsp=0. This is a correction from this
    // codec's pre-v2.19.0 behavior, which modeled Acknowledge as rsp=0.
    auto req = make_standard_request(1, 1, false, 4);
    for (auto kind : {ResponseKind::Acknowledge, ResponseKind::WriteResponse,
                       ResponseKind::ReadResponse, ResponseKind::ErrorResponse}) {
        auto resp = make_response(req, kind);
        REQUIRE(resp.rsp == true);
        REQUIRE(response_kind_of(resp) == kind);
    }
}

// ── Sub-octet field width masking ─────────────────────────────────────────────

TEST_CASE("byte_bus_id round-trips values above 255 (11-bit wire field)", "[acf][REQ-WIRE-004]") {
    AcfMessageInfo info;
    info.byte_bus_id = 2047; // max 11-bit value
    uint8_t hdr[kAcfCommonHeaderLen];
    encode_acf_message_info(info, hdr);
    AcfMessageInfo out;
    decode_acf_message_info(hdr, out);
    REQUIRE(out.byte_bus_id == 2047);
}

TEST_CASE("read_size_or_segment_num round-trips its full 12-bit range", "[acf][REQ-WIRE-004]") {
    AcfMessageInfo info;
    info.read_size_or_segment_num = 4095; // max 12-bit value
    uint8_t hdr[kAcfCommonHeaderLen];
    encode_acf_message_info(info, hdr);
    AcfMessageInfo out;
    decode_acf_message_info(hdr, out);
    REQUIRE(out.read_size_or_segment_num == 4095);
}

TEST_CASE("decode_acf_abb rejects an ACF_GBB-typed buffer and vice versa", "[acf][REQ-WIRE-014]") {
    AcfMessageInfo info;
    auto gbb_frame = encode_acf_gbb(info, 0, {});
    AcfMessageInfo out;
    std::vector<uint8_t> out_payload;
    auto ec = decode_acf_abb(gbb_frame.data(), gbb_frame.size(), out, out_payload);
    REQUIRE(ec);
    REQUIRE(ec == make_error_code(AcfErrc::bad_acf_msg_type));

    auto abb_frame = encode_acf_abb(info, {});
    uint64_t ts = 0;
    auto ec2 = decode_acf_gbb(abb_frame.data(), abb_frame.size(), out, ts, out_payload);
    REQUIRE(ec2);
    REQUIRE(ec2 == make_error_code(AcfErrc::bad_acf_msg_type));
}

// ── Short-buffer validation ─────────────────────────────────────────────────────

TEST_CASE("Decoders reject buffers shorter than their fixed header", "[acf][REQ-WIRE-013]") {
    AcfMessageInfo abb_out;
    std::vector<uint8_t> abb_payload;
    std::vector<uint8_t> too_short_acf(kAcfCommonHeaderLen - 1, 0);
    REQUIRE(decode_acf_abb(too_short_acf.data(), too_short_acf.size(), abb_out, abb_payload));
}

// ── Multiple ACF requests in one frame (extraction §12.9.1.1; issue cpp-RCP-04-fresh) ──

TEST_CASE("decode_acf_messages decodes two ACF_ABB requests packed back to back",
          "[acf][REQ-WIRE-004]") {
    AcfMessageInfo first;
    first.byte_bus_id     = 1;
    first.transaction_num = 10;
    std::vector<uint8_t> first_payload(4, 0xAA); // quadlet-aligned so acf_msg_length is byte-exact

    AcfMessageInfo second;
    second.byte_bus_id     = 2;
    second.transaction_num = 20;
    std::vector<uint8_t> second_payload(4, 0xBB);

    auto first_bytes  = encode_acf_abb(first, first_payload);
    auto second_bytes = encode_acf_abb(second, second_payload);
    std::vector<uint8_t> buf = first_bytes;
    buf.insert(buf.end(), second_bytes.begin(), second_bytes.end());

    std::vector<AcfEntry> out;
    REQUIRE_FALSE(decode_acf_messages(buf.data(), buf.size(), out));
    REQUIRE(out.size() == 2);
    REQUIRE(out[0].info.byte_bus_id == 1);
    REQUIRE(out[0].info.transaction_num == 10);
    REQUIRE(out[0].payload == first_payload);
    REQUIRE(out[1].info.byte_bus_id == 2);
    REQUIRE(out[1].info.transaction_num == 20);
    REQUIRE(out[1].payload == second_payload);
}

TEST_CASE("decode_acf_messages decodes a single ACF_ABB message occupying the whole buffer",
          "[acf][REQ-WIRE-004]") {
    AcfMessageInfo info;
    info.byte_bus_id = 5;
    std::vector<uint8_t> payload{0x01, 0x02, 0x03}; // not quadlet-aligned
    auto buf = encode_acf_abb(info, payload);

    std::vector<AcfEntry> out;
    REQUIRE_FALSE(decode_acf_messages(buf.data(), buf.size(), out));
    REQUIRE(out.size() == 1);
    REQUIRE(out[0].payload == payload);
}

TEST_CASE("decode_acf_messages handles a mix of ACF_ABB and ACF_GBB in one buffer",
          "[acf][REQ-WIRE-004][REQ-WIRE-005]") {
    AcfMessageInfo abb;
    abb.byte_bus_id = 1;
    std::vector<uint8_t> abb_payload(4, 0x11);

    AcfMessageInfo gbb;
    gbb.byte_bus_id = 2;
    gbb.mtv          = true;
    std::vector<uint8_t> gbb_payload(4, 0x22);
    uint64_t ts = 0xAABBCCDDEEFF0011ULL;

    auto abb_bytes = encode_acf_abb(abb, abb_payload);
    auto gbb_bytes = encode_acf_gbb(gbb, ts, gbb_payload);
    std::vector<uint8_t> buf = abb_bytes;
    buf.insert(buf.end(), gbb_bytes.begin(), gbb_bytes.end());

    std::vector<AcfEntry> out;
    REQUIRE_FALSE(decode_acf_messages(buf.data(), buf.size(), out));
    REQUIRE(out.size() == 2);
    REQUIRE(out[0].info.acf_msg_type == kAcfMsgTypeAbb);
    REQUIRE(out[0].payload == abb_payload);
    REQUIRE(out[1].info.acf_msg_type == kAcfMsgTypeGbb);
    REQUIRE(out[1].message_timestamp == ts);
    REQUIRE(out[1].payload == gbb_payload);
}

TEST_CASE("decode_acf_messages rejects an empty/unrecognized-type buffer outright", "[acf][REQ-WIRE-004]") {
    std::vector<uint8_t> bogus(kAcfCommonHeaderLen, 0x00); // acf_msg_type bits decode to neither ABB nor GBB
    std::vector<AcfEntry> out;
    auto ec = decode_acf_messages(bogus.data(), bogus.size(), out);
    REQUIRE(ec);
    REQUIRE(out.empty());
}

// ── byte_bus_id echo rule ───────────────────────────────────────────────────────

TEST_CASE("make_response echoes byte_bus_id and transaction_num from the request unchanged", "[acf][REQ-WIRE-008]") {
    auto req = make_standard_request(/*bus_id=*/9, /*transaction_num=*/17, /*write=*/false, /*read_size=*/4);
    auto resp = make_response(req, ResponseKind::ReadResponse);
    REQUIRE(resp.byte_bus_id == req.byte_bus_id);
    REQUIRE(resp.transaction_num == req.transaction_num);
}

// ── Standard request kind ───────────────────────────────────────────────────────

TEST_CASE("make_standard_request builds an unconditional ACF_ABB read request", "[acf][REQ-WIRE-009]") {
    auto req = make_standard_request(1, 1, /*write=*/false, /*read_size=*/8);
    REQUIRE(req.acf_msg_type == kAcfMsgTypeAbb);
    REQUIRE_FALSE(req.op);
    REQUIRE_FALSE(req.rsp);
    REQUIRE(req.read_size_or_segment_num == 8);
}

TEST_CASE("make_standard_request builds an unconditional ACF_ABB write request", "[acf][REQ-WIRE-009]") {
    auto req = make_standard_request(1, 1, /*write=*/true, /*read_size=*/0);
    REQUIRE(req.acf_msg_type == kAcfMsgTypeAbb);
    REQUIRE(req.op);
    REQUIRE_FALSE(req.rsp);
}

// ── Four response semantic types ────────────────────────────────────────────────

TEST_CASE("The four response kinds map onto distinct, recoverable header states", "[acf][REQ-WIRE-010]") {
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

TEST_CASE("effective_timestamp prefers a valid TSCF avtp_timestamp", "[acf][REQ-WIRE-012][REQ-ACF-012]") {
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

TEST_CASE("effective_timestamp falls back to a valid ACF_GBB message_timestamp", "[acf][REQ-WIRE-012][REQ-ACF-012]") {
    AcfMessageInfo gbb_info;
    gbb_info.acf_msg_type = kAcfMsgTypeGbb;
    gbb_info.mtv           = true;

    auto ts = effective_timestamp(nullptr, &gbb_info, /*message_timestamp=*/333);
    REQUIRE(ts.has_value());
    REQUIRE(*ts == 333);
}

TEST_CASE("effective_timestamp is nullopt, not zero, when neither source is valid", "[acf][REQ-WIRE-012][REQ-ACF-012]") {
    TscfHeader tscf; // timestamp_valid defaults to false
    AcfMessageInfo abb_info;
    abb_info.acf_msg_type = kAcfMsgTypeAbb; // ACF_ABB never has a timestamp

    auto ts = effective_timestamp(&tscf, &abb_info, /*message_timestamp=*/0);
    REQUIRE_FALSE(ts.has_value());

    auto ts2 = effective_timestamp(nullptr, nullptr, 0);
    REQUIRE_FALSE(ts2.has_value());
}

// ── RELAY spec §15.5 canonical types (cpp-RCP-FS-06) ──────────────────────────

TEST_CASE("ControlFlags bit values match RELAY spec §15.5 exactly", "[acf][relay-spec][REQ-WIRE-015]") {
    REQUIRE(static_cast<uint8_t>(ControlFlags::FlagAck)          == 0x80);
    REQUIRE(static_cast<uint8_t>(ControlFlags::FlagRead)         == 0x40);
    REQUIRE(static_cast<uint8_t>(ControlFlags::FlagWrite)        == 0x20);
    REQUIRE(static_cast<uint8_t>(ControlFlags::FlagResponse)     == 0x10);
    REQUIRE(static_cast<uint8_t>(ControlFlags::FlagError)        == 0x08);
    REQUIRE(static_cast<uint8_t>(ControlFlags::FlagMoreSegments) == 0x04);
}

TEST_CASE("to_message/from_message round-trip an ACF_ABB write request", "[acf][relay-spec][REQ-WIRE-015]") {
    AcfMessageInfo info;
    info.byte_bus_id     = 9;
    info.transaction_num = 3;
    info.op                = true; // write
    info.ms                  = false;
    info.read_size_or_segment_num = 0;
    std::vector<uint8_t> body = {0x01, 0x02};

    auto msg = to_message(info, /*message_timestamp=*/0, body);
    REQUIRE(msg.byte_bus_id == 9);
    REQUIRE(msg.transaction_num == 3);
    REQUIRE(has_flag(msg.control, ControlFlags::FlagWrite));
    REQUIRE_FALSE(has_flag(msg.control, ControlFlags::FlagRead));
    REQUIRE_FALSE(has_flag(msg.control, ControlFlags::FlagResponse));
    REQUIRE(msg.timestamp == 0);
    REQUIRE(msg.body == body);

    uint64_t out_ts = 0xFFFFFFFFFFFFFFFFULL; // poisoned, must be overwritten to 0 for ABB
    auto back = from_message(msg, /*as_gbb=*/false, /*mtv=*/false, out_ts);
    REQUIRE(back.byte_bus_id == info.byte_bus_id);
    REQUIRE(back.transaction_num == info.transaction_num);
    REQUIRE(back.op == info.op);
    REQUIRE(out_ts == msg.timestamp);
}

TEST_CASE("to_message reflects Acknowledge/Response/Error/MoreSegments flags", "[acf][relay-spec][REQ-WIRE-015]") {
    auto req  = make_standard_request(1, 1, false, 4);
    auto ack  = make_response(req, ResponseKind::Acknowledge);
    auto msg  = to_message(ack, 0, {});
    REQUIRE(has_flag(msg.control, ControlFlags::FlagAck));
    REQUIRE(has_flag(msg.control, ControlFlags::FlagResponse));

    auto err_resp = make_response(req, ResponseKind::ErrorResponse);
    auto err_msg  = to_message(err_resp, 0, {});
    REQUIRE(has_flag(err_msg.control, ControlFlags::FlagError));
    REQUIRE(has_flag(err_msg.control, ControlFlags::FlagResponse));

    AcfMessageInfo frag_info;
    frag_info.ms = true;
    auto frag_msg = to_message(frag_info, 0, {});
    REQUIRE(has_flag(frag_msg.control, ControlFlags::FlagMoreSegments));
}

TEST_CASE("to_message carries message_timestamp only for ACF_GBB", "[acf][relay-spec][REQ-WIRE-015]") {
    AcfMessageInfo abb_info;
    abb_info.acf_msg_type = kAcfMsgTypeAbb;
    auto abb_msg = to_message(abb_info, /*message_timestamp=*/0xDEADBEEF, {});
    REQUIRE(abb_msg.timestamp == 0); // ACF_ABB has no timestamp field; not leaked through

    AcfMessageInfo gbb_info;
    gbb_info.acf_msg_type = kAcfMsgTypeGbb;
    auto gbb_msg = to_message(gbb_info, /*message_timestamp=*/0xDEADBEEF, {});
    REQUIRE(gbb_msg.timestamp == 0xDEADBEEF);
}

// ── Phase 17 (c-RCP port): message-type constants & GBB/ABB header-length relation ──

TEST_CASE("ACF_ABB/ACF_GBB message type wire values", "[acf][REQ-ACF-017][REQ-ACF-048]") {
    REQUIRE(kAcfMsgTypeAbb == 0x0E);
    REQUIRE(kAcfMsgTypeGbb == 0x0D);
}

TEST_CASE("kAcfGbbMessageInfoLen is exactly kAcfCommonHeaderLen + 8", "[acf]") {
    // The presence/absence of message_timestamp is the only structural
    // difference between the two variants.
    REQUIRE(kAcfCommonHeaderLen + 8 == kAcfGbbMessageInfoLen);
}

// ── pad_len (ported from c-RCP's rcp_acf_pad_len()) ───────────────────────────

TEST_CASE("pad_len computes the octets needed to reach the next quadlet boundary", "[acf][REQ-ACF-047]") {
    REQUIRE(pad_len(8) == 0);
    REQUIRE(pad_len(9) == 3);
    REQUIRE(pad_len(10) == 2);
    REQUIRE(pad_len(11) == 1);
    REQUIRE(pad_len(12) == 0);
}

// ── acf_msg_length / payload bounds (ported from c-RCP's RCP_ACF_MAX_QUADLETS
// / RCP_ACF_ABB_MAX_PAYLOAD / RCP_ACF_GBB_MAX_PAYLOAD) ────────────────────────

TEST_CASE("kAcfAbbMaxPayload/kAcfGbbMaxPayload are derived from the 9-bit acf_msg_length field", "[acf]") {
    REQUIRE(kAcfMaxQuadlets == 0x1FF);
    REQUIRE(kAcfAbbMaxPayload == static_cast<size_t>(kAcfMaxQuadlets) * 4 - kAcfCommonHeaderLen);
    REQUIRE(kAcfGbbMaxPayload == static_cast<size_t>(kAcfMaxQuadlets) * 4 - kAcfGbbMessageInfoLen);
    REQUIRE(kAcfGbbMaxPayload < kAcfAbbMaxPayload); // GBB's fixed region is 8 bytes larger
}

// ── peek_msg_type (ported from c-RCP's rcp_acf_peek_msg_type()) ──────────────

TEST_CASE("peek_msg_type reads the first byte's acf_msg_type", "[acf][REQ-ACF-013]") {
    AcfMessageInfo info;
    auto frame = encode_acf_abb(info, {});
    uint8_t msg_type = 0;
    REQUIRE_FALSE(peek_msg_type(frame.data(), frame.size(), msg_type));
    REQUIRE(msg_type == kAcfMsgTypeAbb);
}

TEST_CASE("peek_msg_type rejects an empty buffer", "[acf][REQ-ACF-013]") {
    uint8_t msg_type = 0;
    REQUIRE(peek_msg_type(nullptr, 0, msg_type));
}

// ── header_is_request / request_header_constraints_valid (ported from c-RCP's
// rcp_acf_header_is_request()/_request_header_constraints_valid()) ───────────

TEST_CASE("header_is_request is true for rsp=0 and false for rsp=1", "[acf][REQ-ACF-050]") {
    AcfMessageInfo hdr;
    REQUIRE(header_is_request(hdr)); // rsp=0: a request

    hdr.rsp = true;
    REQUIRE_FALSE(header_is_request(hdr)); // rsp=1: a response
}

TEST_CASE("request_header_constraints_valid accepts a fresh, unmodified request header", "[acf][REQ-ACF-021]") {
    AcfMessageInfo hdr;
    REQUIRE(request_header_constraints_valid(hdr, /*cs_has_meaning=*/false));
}

TEST_CASE("request_header_constraints_valid rejects hs/rsp/err set on a request", "[acf][REQ-ACF-021]") {
    AcfMessageInfo hs_hdr;
    hs_hdr.hs = true;
    REQUIRE_FALSE(request_header_constraints_valid(hs_hdr, false));

    AcfMessageInfo rsp_hdr;
    rsp_hdr.rsp = true;
    REQUIRE_FALSE(request_header_constraints_valid(rsp_hdr, false));

    AcfMessageInfo err_hdr;
    err_hdr.err = true;
    REQUIRE_FALSE(request_header_constraints_valid(err_hdr, false));
}

TEST_CASE("request_header_constraints_valid rejects cs=1 unless cs_has_meaning", "[acf][REQ-ACF-021]") {
    AcfMessageInfo hdr;
    hdr.cs = true;
    REQUIRE_FALSE(request_header_constraints_valid(hdr, /*cs_has_meaning=*/false));
    REQUIRE(request_header_constraints_valid(hdr, /*cs_has_meaning=*/true));
}

// ── evt_row2_is_plain (TC18 §13.5 Table 33's ADC/PWM_IN/I2C/LIN/CAN/UART/
// ISELED/MDIO row) ─────────────────────────────────────────────────────────

TEST_CASE("evt_row2_is_plain is true only for evt[2:0] == 0", "[acf][REQ-ACF-023]") {
    REQUIRE(evt_row2_is_plain(0x0));
    for (uint8_t v = 1; v <= 6; ++v) {
        REQUIRE_FALSE(evt_row2_is_plain(v));
    }
    REQUIRE_FALSE(evt_row2_is_plain(0x7)); // reserved config-write selector
}

TEST_CASE("evt_row2_is_plain ignores evt[3] (the ack-request bit)", "[acf][REQ-ACF-023]") {
    // evt[3] is outside evt[2:0]'s 3-bit scope — a request with evt[3] set
    // but evt[2:0] = 000b is still plain.
    REQUIRE(evt_row2_is_plain(0x8));
}

// ── evt_requests_acknowledge (TC18 §13.5: "evt[3] is used to request an
// acknowledge") ───────────────────────────────────────────────────────────

TEST_CASE("evt_requests_acknowledge reflects evt[3] regardless of evt[2:0]", "[acf]") {
    REQUIRE_FALSE(evt_requests_acknowledge(0x00));
    REQUIRE_FALSE(evt_requests_acknowledge(0x07));
    REQUIRE(evt_requests_acknowledge(0x08));
    REQUIRE(evt_requests_acknowledge(0x0F));
}

// ── TC18 §13.5.1 compound-wait evt[2:0] comparison rule (ported from c-RCP's
// rcp_acf_compound_wait_evt_valid()/_compound_wait_match()) ──────────────────

TEST_CASE("compound_wait_evt_valid is true for every mode but the reserved one", "[acf][REQ-ACF-024]") {
    REQUIRE(compound_wait_evt_valid(0x0));
    REQUIRE(compound_wait_evt_valid(0x1));
    REQUIRE(compound_wait_evt_valid(0x2));
    REQUIRE(compound_wait_evt_valid(0x4));
    REQUIRE(compound_wait_evt_valid(0x5));
    REQUIRE(compound_wait_evt_valid(0x6));
    REQUIRE(compound_wait_evt_valid(0x7));
}

TEST_CASE("compound_wait_evt_valid is false for evt[2:0] == 011b regardless of the upper bits", "[acf][REQ-ACF-024]") {
    REQUIRE_FALSE(compound_wait_evt_valid(0x3));
    REQUIRE_FALSE(compound_wait_evt_valid(0xB)); // 1011b
    REQUIRE_FALSE(compound_wait_evt_valid(0xFB & 0x0F));
}

TEST_CASE("compound_wait_match: status shorter than payload never matches", "[acf][REQ-ACF-025]") {
    const uint8_t payload[4] = {0x01, 0x02, 0x03, 0x04};
    const uint8_t status[3]  = {0x01, 0x02, 0x03};

    // Exact match on the shared 3-byte prefix would otherwise succeed — the
    // length rule must short-circuit before any mode-specific comparison.
    REQUIRE_FALSE(compound_wait_match(0x0, payload, sizeof(payload), status, sizeof(status)));
}

TEST_CASE("compound_wait_match caps status to payload_length (the specification's own SPI example)", "[acf][REQ-ACF-025]") {
    const uint8_t payload[4] = {0x00, 0x00, 0x00, 0x02};
    uint8_t       status[20];
    std::fill(std::begin(status), std::end(status), uint8_t{0xAA}); // tail bytes: never read
    status[0] = 0x00; status[1] = 0x00; status[2] = 0x00; status[3] = 0x02;

    REQUIRE(compound_wait_match(0x0, payload, sizeof(payload), status, sizeof(status)));

    // Changing a byte within the compared prefix must still be seen.
    status[3] = 0x03;
    REQUIRE_FALSE(compound_wait_match(0x0, payload, sizeof(payload), status, sizeof(status)));
}

TEST_CASE("compound_wait_match exact-match mode (evt[2:0] = 000b)", "[acf][REQ-ACF-026][REQ-ADC-034]") {
    const uint8_t payload[2] = {0x01, 0x02};
    const uint8_t equal[2]   = {0x01, 0x02};
    const uint8_t differs[2] = {0x01, 0x03};

    REQUIRE(compound_wait_match(0x0, payload, 2, equal, 2));
    REQUIRE_FALSE(compound_wait_match(0x0, payload, 2, differs, 2));
    REQUIRE(compound_wait_match(0x0, nullptr, 0, nullptr, 0));
}

TEST_CASE("compound_wait_match AND-with-1s-mask mode (evt[2:0] = 001b)", "[acf][REQ-ACF-027]") {
    // The specification's own example: byte_msg_payload = 0x00000002 checks
    // whether the second IO pin (bit 1) is asserted.
    const uint8_t payload[4]    = {0x00, 0x00, 0x00, 0x02};
    const uint8_t bit_set[4]    = {0x00, 0x00, 0x00, 0x02};
    const uint8_t bit_clear[4]  = {0x00, 0x00, 0x00, 0x00};
    const uint8_t other_bits[4] = {0xFF, 0xFF, 0xFF, 0xFF};

    REQUIRE(compound_wait_match(0x1, payload, 4, bit_set, 4));
    REQUIRE_FALSE(compound_wait_match(0x1, payload, 4, bit_clear, 4));
    // Payload's own 0-bits are don't-care: status's other set bits (which
    // correspond to payload 0-bits) must not affect the outcome.
    REQUIRE(compound_wait_match(0x1, payload, 4, other_bits, 4));
}

TEST_CASE("compound_wait_match AND-with-0s-mask mode (evt[2:0] = 010b)", "[acf][REQ-ACF-028]") {
    const uint8_t payload[4]         = {0x00, 0x00, 0x00, 0x02};
    const uint8_t bit_clear[4]       = {0x00, 0x00, 0x00, 0x00};
    const uint8_t bit_set[4]         = {0x00, 0x00, 0x00, 0x02};
    const uint8_t other_bits_only[4] = {0xFF, 0xFF, 0xFF, 0xFD};

    REQUIRE(compound_wait_match(0x2, payload, 4, bit_clear, 4));
    REQUIRE_FALSE(compound_wait_match(0x2, payload, 4, bit_set, 4));
    REQUIRE(compound_wait_match(0x2, payload, 4, other_bits_only, 4));
}

TEST_CASE("compound_wait_match leading-quadlet hi-word >= mode (evt[2:0] = 100b)", "[acf][REQ-ACF-029]") {
    const uint8_t payload[4] = {0x00, 0x0A, 0x00, 0x00}; // hi word = 10
    const uint8_t lower[4]   = {0x00, 0x05, 0x00, 0x00}; // hi word = 5
    const uint8_t higher[4]  = {0x00, 0x0F, 0x00, 0x00}; // hi word = 15
    const uint8_t equal[4]   = {0x00, 0x0A, 0x00, 0x00};

    REQUIRE(compound_wait_match(0x4, payload, 4, lower, 4));   // 10>=5
    REQUIRE_FALSE(compound_wait_match(0x4, payload, 4, higher, 4)); // 10>=15
    REQUIRE(compound_wait_match(0x4, payload, 4, equal, 4));   // 10>=10
}

TEST_CASE("compound_wait_match leading-quadlet hi-word <= mode (evt[2:0] = 101b)", "[acf][REQ-ACF-052]") {
    const uint8_t payload[4] = {0x00, 0x0A, 0x00, 0x00}; // hi word = 10
    const uint8_t lower[4]   = {0x00, 0x05, 0x00, 0x00};
    const uint8_t higher[4]  = {0x00, 0x0F, 0x00, 0x00};
    const uint8_t equal[4]   = {0x00, 0x0A, 0x00, 0x00};

    REQUIRE_FALSE(compound_wait_match(0x5, payload, 4, lower, 4)); // 10<=5
    REQUIRE(compound_wait_match(0x5, payload, 4, higher, 4));      // 10<=15
    REQUIRE(compound_wait_match(0x5, payload, 4, equal, 4));       // 10<=10
}

TEST_CASE("compound_wait_match leading-quadlet lo-word >= mode (evt[2:0] = 110b)", "[acf][REQ-ACF-030]") {
    const uint8_t payload[4] = {0xFF, 0xFF, 0x00, 0x0A}; // lo word = 10
    const uint8_t lower[4]   = {0xFF, 0xFF, 0x00, 0x05}; // lo word = 5
    const uint8_t higher[4]  = {0xFF, 0xFF, 0x00, 0x0F}; // lo word = 15

    REQUIRE(compound_wait_match(0x6, payload, 4, lower, 4));       // 10>=5
    REQUIRE_FALSE(compound_wait_match(0x6, payload, 4, higher, 4)); // 10>=15
}

TEST_CASE("compound_wait_match leading-quadlet lo-word <= mode (evt[2:0] = 111b)", "[acf][REQ-ACF-053]") {
    const uint8_t payload[4] = {0xFF, 0xFF, 0x00, 0x0A}; // lo word = 10
    const uint8_t lower[4]   = {0xFF, 0xFF, 0x00, 0x05};
    const uint8_t higher[4]  = {0xFF, 0xFF, 0x00, 0x0F};

    REQUIRE_FALSE(compound_wait_match(0x7, payload, 4, lower, 4)); // 10<=5
    REQUIRE(compound_wait_match(0x7, payload, 4, higher, 4));      // 10<=15
}

TEST_CASE("compound_wait_match ge/le modes reject a payload shorter than one quadlet", "[acf][REQ-ACF-029][REQ-ACF-030][REQ-ACF-052][REQ-ACF-053]") {
    const uint8_t payload[3] = {0x00, 0x0A, 0x00};
    const uint8_t status[3]  = {0x00, 0x00, 0x00};

    REQUIRE_FALSE(compound_wait_match(0x4, payload, 3, status, 3));
    REQUIRE_FALSE(compound_wait_match(0x5, payload, 3, status, 3));
    REQUIRE_FALSE(compound_wait_match(0x6, payload, 3, status, 3));
    REQUIRE_FALSE(compound_wait_match(0x7, payload, 3, status, 3));
}

TEST_CASE("compound_wait_match reserved mode (evt[2:0] = 011b) always returns false", "[acf][REQ-ACF-051]") {
    // Callers must gate on compound_wait_evt_valid() first; this pins the
    // function's own defined (always-false) behavior if they don't.
    const uint8_t payload[2] = {0x01, 0x02};
    REQUIRE_FALSE(compound_wait_match(0x3, payload, 2, payload, 2));
}

// ── reg_write_len (TC18 §13.7.1.2, RC5-corrected formula) ────────────────────

TEST_CASE("reg_write_len computes the EP0 register-write effective length", "[acf]") {
    // (acf_msg_length - 3) * 4 - pad - 2.
    REQUIRE(reg_write_len(5, 0) == (5 - 3) * 4 - 0 - 2);
    REQUIRE(reg_write_len(7, 1) == (7 - 3) * 4 - 1 - 2);
}

TEST_CASE("reg_write_len fails safe to 0 rather than underflowing", "[acf]") {
    REQUIRE(reg_write_len(0, 0) == 0);
    REQUIRE(reg_write_len(2, 0) == 0); // < 3 quadlets: no room for the fixed region at all
    REQUIRE(reg_write_len(3, 255) == 0); // pad + address overhead exceeds what's left
}

// ── peek_gbb_request_type (conditional-request modules' shared repurposed-
// timestamp-region accessor) ──────────────────────────────────────────────

TEST_CASE("peek_gbb_request_type reads frame[8] for a genuine GBB frame", "[acf][REQ-ACF-032]") {
    AcfMessageInfo info;
    auto frame = encode_acf_gbb(info, /*message_timestamp=*/0, {});
    // Repurpose the timestamp region's first octet, as the conditional-
    // request modules (compound/triggered/chained/timed) do when mtv=0.
    REQUIRE(frame.size() > kAcfCommonHeaderLen);
    frame[kAcfCommonHeaderLen] = 0x0F;

    uint8_t request_type = 0xFF;
    REQUIRE(peek_gbb_request_type(frame.data(), frame.size(), request_type));
    REQUIRE(request_type == 0x0F);
}

TEST_CASE("peek_gbb_request_type rejects an ACF_ABB frame outright", "[acf][REQ-ACF-032]") {
    AcfMessageInfo info;
    std::vector<uint8_t> frame = encode_acf_abb(info, {0x0F});

    uint8_t request_type = 0xFF;
    REQUIRE_FALSE(peek_gbb_request_type(frame.data(), frame.size(), request_type));
    REQUIRE(request_type == 0xFF); // left unchanged
}

TEST_CASE("peek_gbb_request_type rejects a GBB frame too short to hold the request_type octet", "[acf][REQ-ACF-032]") {
    AcfMessageInfo info;
    auto frame = encode_acf_gbb(info, 0, {});
    frame.resize(kAcfCommonHeaderLen); // exactly the 8-byte header, no request_type octet

    uint8_t request_type = 0xFF;
    REQUIRE_FALSE(peek_gbb_request_type(frame.data(), frame.size(), request_type));
    REQUIRE(request_type == 0xFF);
}

// ── Response builders (ported from c-RCP's rcp_acf_build_error_response()/
// _build_acknowledge_response()/_build_acknowledge_rejected_response()) ──────

TEST_CASE("build_error_response carries byte_bus_id, transaction_num, and the error code", "[acf][REQ-ACF-031][REQ-WIREERR-001]") {
    auto resp = build_error_response(7, 200, WireErrorCode::ReqStorageOverflow);

    AcfMessageInfo hdr;
    std::vector<uint8_t> payload;
    REQUIRE_FALSE(decode_acf_abb(resp.data(), resp.size(), hdr, payload));
    REQUIRE(response_kind_of(hdr) == ResponseKind::ErrorResponse);
    REQUIRE(hdr.err == true);
    REQUIRE(hdr.rsp == true);
    REQUIRE(hdr.byte_bus_id == 7);
    REQUIRE(hdr.transaction_num == 200);
    REQUIRE(payload.size() == 1);
    REQUIRE(payload[0] == static_cast<uint8_t>(WireErrorCode::ReqStorageOverflow));
}

TEST_CASE("build_error_response never classifies as Acknowledge", "[acf][REQ-ACF-031]") {
    auto resp = build_error_response(1, 1, WireErrorCode::UnsupportedCmd);

    AcfMessageInfo hdr;
    std::vector<uint8_t> payload;
    REQUIRE_FALSE(decode_acf_abb(resp.data(), resp.size(), hdr, payload));
    uint8_t evt = static_cast<uint8_t>((hdr.evt_ack ? 0x08 : 0) | (hdr.evt_op & 0x07));
    REQUIRE(evt != kEvtAcknowledge);
    REQUIRE(response_kind_of(hdr) == ResponseKind::ErrorResponse);
}

TEST_CASE("build_acknowledge_rejected_response carries byte_bus_id, transaction_num, and the error code",
          "[acf][REQ-ACF-033][REQ-WIREERR-001]") {
    auto resp = build_acknowledge_rejected_response(7, 200, WireErrorCode::ReqStorageOverflow);

    AcfMessageInfo hdr;
    std::vector<uint8_t> payload;
    REQUIRE_FALSE(decode_acf_abb(resp.data(), resp.size(), hdr, payload));
    REQUIRE(response_kind_of(hdr) == ResponseKind::Acknowledge);
    uint8_t evt = static_cast<uint8_t>((hdr.evt_ack ? 0x08 : 0) | (hdr.evt_op & 0x07));
    REQUIRE(evt == kEvtAcknowledge);
    REQUIRE(hdr.err == true);
    REQUIRE(hdr.rsp == true);
    REQUIRE(hdr.byte_bus_id == 7);
    REQUIRE(hdr.transaction_num == 200);
    REQUIRE(payload.size() == 1);
    REQUIRE(payload[0] == static_cast<uint8_t>(WireErrorCode::ReqStorageOverflow));
}

TEST_CASE("build_acknowledge_rejected_response differs from build_error_response only in evt",
          "[acf][REQ-ACF-033]") {
    // Pins the distinction directly: same transaction_num/error code, but
    // the two builders' own responses must classify differently
    // (Acknowledge vs Error), and only the rejected-acknowledge shape's own
    // evt is 0xF.
    auto ack_resp = build_acknowledge_rejected_response(3, 55, WireErrorCode::UnsupportedCmd);
    auto err_resp = build_error_response(3, 55, WireErrorCode::UnsupportedCmd);

    AcfMessageInfo ack_hdr, err_hdr;
    std::vector<uint8_t> ack_payload, err_payload;
    REQUIRE_FALSE(decode_acf_abb(ack_resp.data(), ack_resp.size(), ack_hdr, ack_payload));
    REQUIRE_FALSE(decode_acf_abb(err_resp.data(), err_resp.size(), err_hdr, err_payload));

    REQUIRE(response_kind_of(ack_hdr) == ResponseKind::Acknowledge);
    REQUIRE(response_kind_of(err_hdr) == ResponseKind::ErrorResponse);
    // Both carry err=1 and the same payload octet — only evt tells them apart.
    REQUIRE(ack_hdr.err == true);
    REQUIRE(err_hdr.err == true);
    REQUIRE(ack_payload[0] == err_payload[0]);
}

TEST_CASE("build_acknowledge_response builds a genuine Acknowledge with no payload", "[acf]") {
    auto resp = build_acknowledge_response(5, 9);

    AcfMessageInfo hdr;
    std::vector<uint8_t> payload;
    REQUIRE_FALSE(decode_acf_abb(resp.data(), resp.size(), hdr, payload));
    REQUIRE(response_kind_of(hdr) == ResponseKind::Acknowledge);
    REQUIRE(hdr.err == false);
    REQUIRE(hdr.rsp == true);
    REQUIRE(hdr.byte_bus_id == 5);
    REQUIRE(hdr.transaction_num == 9);
    REQUIRE(payload.empty());
}

// ── GBB worked-example pin, adapted to this codec's caller-owns-padding
// convention (GBB counterpart of the existing ABB
// "acf_msg_length/pad wire bits round-trip..." test above) ───────────────────

TEST_CASE("ACF_GBB acf_msg_length/pad wire bits round-trip the specification's own worked-example "
          "values",
          "[acf][REQ-WIRE-005]") {
    AcfMessageInfo info;
    info.acf_msg_length = 7; // Message Info+timestamp (4 quadlets) + padded payload (2) + CRC trailer (1)
    info.pad             = 1; // 7 payload bytes + 1 pad byte = 8 bytes = 2 quadlets
    info.mtv              = true;
    std::vector<uint8_t> payload(8, 0); // 7 "real" + 1 padding byte, caller-padded per convention

    auto frame = encode_acf_gbb(info, /*message_timestamp=*/0x1122334455667788ULL, payload);
    AcfMessageInfo out;
    uint64_t out_ts = 0;
    std::vector<uint8_t> out_payload;
    REQUIRE_FALSE(decode_acf_gbb(frame.data(), frame.size(), out, out_ts, out_payload));
    REQUIRE(out.acf_msg_length == 7);
    REQUIRE(out.pad == 1);
}

// ── acf_category() unique-message-per-errc (ported from c-RCP's
// rcp_acf_strerror()) ──────────────────────────────────────────────────────

TEST_CASE("acf_category() returns a unique, non-empty message per AcfErrc value", "[acf][REQ-ACF-001]") {
    const auto& cat = acf_category();
    const std::string bad_type   = cat.message(static_cast<int>(AcfErrc::bad_acf_msg_type));
    const std::string bad_length = cat.message(static_cast<int>(AcfErrc::bad_acf_msg_length));
    REQUIRE_FALSE(bad_type.empty());
    REQUIRE_FALSE(bad_length.empty());
    REQUIRE(bad_type != bad_length);
}

// ── encode_acf_abb/encode_acf_gbb force the correct acf_msg_type regardless
// of the caller-supplied header (ported from c-RCP's rcp_acf_encode_abb()/
// _encode_gbb()) ────────────────────────────────────────────────────────────

TEST_CASE("encode_acf_abb writes kAcfMsgTypeAbb even if the caller's header claims ACF_GBB",
          "[acf][REQ-ACF-004]") {
    AcfMessageInfo info;
    info.acf_msg_type = kAcfMsgTypeGbb; // deliberately wrong on entry
    auto frame = encode_acf_abb(info, {});
    REQUIRE(peek_acf_msg_type(frame.data()) == kAcfMsgTypeAbb);
}

TEST_CASE("encode_acf_gbb writes kAcfMsgTypeGbb even if the caller's header claims ACF_ABB",
          "[acf][REQ-ACF-038]") {
    AcfMessageInfo info;
    info.acf_msg_type = kAcfMsgTypeAbb; // deliberately wrong on entry
    auto frame = encode_acf_gbb(info, 0, {});
    REQUIRE(peek_acf_msg_type(frame.data()) == kAcfMsgTypeGbb);
}

// ── WireErrorCode numeric values pin TC18 Table 27/30 exactly (ported from
// c-RCP's rcp_wire_error_t) ─────────────────────────────────────────────────

TEST_CASE("WireErrorCode enumerators carry TC18 Table 27/30's own numeric values", "[acf][REQ-WIREERR-001]") {
    REQUIRE(static_cast<uint8_t>(WireErrorCode::UnsupportedCmd)        == 1);
    REQUIRE(static_cast<uint8_t>(WireErrorCode::SequencerNotKnown)     == 2);
    REQUIRE(static_cast<uint8_t>(WireErrorCode::UnauthorizedAccess)    == 3);
    REQUIRE(static_cast<uint8_t>(WireErrorCode::LockedMemAccess)       == 4);
    REQUIRE(static_cast<uint8_t>(WireErrorCode::RequestCanceled)       == 5);
    REQUIRE(static_cast<uint8_t>(WireErrorCode::RequestNotFound)       == 6);
    REQUIRE(static_cast<uint8_t>(WireErrorCode::EpError)               == 7);
    REQUIRE(static_cast<uint8_t>(WireErrorCode::EpNotFound)            == 8);
    REQUIRE(static_cast<uint8_t>(WireErrorCode::PwmInNoSignal)         == 9);
    REQUIRE(static_cast<uint8_t>(WireErrorCode::ReqStorageOverflow)    == 10);
    REQUIRE(static_cast<uint8_t>(WireErrorCode::RequestRejected)       == 11);
    REQUIRE(static_cast<uint8_t>(WireErrorCode::PociFailure)           == 12);
    REQUIRE(static_cast<uint8_t>(WireErrorCode::PresentationTimeTooFar) == 13);
    REQUIRE(static_cast<uint8_t>(WireErrorCode::GptpFail)              == 14);
    REQUIRE(static_cast<uint8_t>(WireErrorCode::InvalidParameter)      == 15);
    REQUIRE(static_cast<uint8_t>(WireErrorCode::ChainAborted)          == 16);
    REQUIRE(static_cast<uint8_t>(WireErrorCode::ChainError)            == 17);
}
