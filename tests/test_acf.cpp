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

TEST_CASE("ACF_ABB round-trips the shared header and payload with no timestamp field", "[acf][REQ-WIRE-004][REQ-WIRE-006]") {
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

TEST_CASE("ACF_GBB round-trips a 64-bit message_timestamp alongside the shared header", "[acf][REQ-WIRE-005][REQ-WIRE-006]") {
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
// Every byte below is computed by hand from the field values and this file's
// own derived bit layout (see acf.hpp's "ACF shared header" comment) — not
// copied from anywhere. Byte-by-byte derivation (MSB-first bit numbering,
// bit0 = MSB of byte0, matching the specification's own diagrams):
//
//   byte0 = (acf_msg_type[6:0] << 1) | acf_msg_length[8]
//   byte1 = acf_msg_length[7:0]
//   byte2 = (pad[1:0] << 6) | (mtv << 5) | (rsv=00 << 3) | byte_bus_id[10:8]
//   byte3 = byte_bus_id[7:0]
//   byte4 = (evt[3:0] << 4) | (rsv=00 << 2) | (hs << 1) | cs
//     where evt[3:0] = (evt_ack << 3) | evt_op[2:0]
//   byte5 = transaction_num
//   byte6 = (op << 7) | (rsp << 6) | (err << 5) | (ms << 4) | read_size[11:8]
//   byte7 = read_size[7:0]
//   (ACF_GBB only) bytes8-15 = message_timestamp, big-endian
//   remaining bytes = payload, unchanged

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

TEST_CASE("ACF_GBB hand-computed expected byte sequence", "[acf][REQ-WIRE-005][REQ-WIRE-006]") {
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

    // byte0 = (0x0D << 1) | (7 >> 8 & 1) = 0x1A | 0 = 0x1A
    // byte1 = 7 & 0xFF = 0x07
    // byte2 = (1 << 6) | (1 << 5) | (300 >> 8 & 7) = 0x40 | 0x20 | 0x01 = 0x61
    // byte3 = 300 & 0xFF = 0x2C
    // byte4 = (0x3 << 4) | (0 << 1) | 1 = 0x30 | 0x01 = 0x31
    // byte5 = 200 = 0xC8
    // byte6 = 0 | (1<<6) | (1<<5) | (1<<4) | (4095 >> 8 & 0xF) = 0x40|0x20|0x10|0x0F = 0x7F
    // byte7 = 4095 & 0xFF = 0xFF
    const std::vector<uint8_t> expected_header = {0x1A, 0x07, 0x61, 0x2C, 0x31, 0xC8, 0x7F, 0xFF};
    const uint64_t ts = 0x1122334455667788ULL;
    const std::vector<uint8_t> expected_ts = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
    const std::vector<uint8_t> payload = {0xAA, 0xBB, 0xCC, 0xDD};

    auto frame = encode_acf_gbb(info, ts, payload);
    REQUIRE(frame.size() == expected_header.size() + expected_ts.size() + payload.size());
    std::vector<uint8_t> header(frame.begin(), frame.begin() + static_cast<long>(kAcfCommonHeaderLen));
    REQUIRE(header == expected_header);
    std::vector<uint8_t> ts_bytes(frame.begin() + static_cast<long>(kAcfCommonHeaderLen),
                                   frame.begin() + static_cast<long>(kAcfCommonHeaderLen + kAcfGbbTimestampLen));
    REQUIRE(ts_bytes == expected_ts);
    REQUIRE(std::vector<uint8_t>(frame.begin() + static_cast<long>(kAcfCommonHeaderLen + kAcfGbbTimestampLen),
                                  frame.end()) == payload);

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

TEST_CASE("effective_timestamp prefers a valid TSCF avtp_timestamp", "[acf][REQ-WIRE-012]") {
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

TEST_CASE("effective_timestamp falls back to a valid ACF_GBB message_timestamp", "[acf][REQ-WIRE-012]") {
    AcfMessageInfo gbb_info;
    gbb_info.acf_msg_type = kAcfMsgTypeGbb;
    gbb_info.mtv           = true;

    auto ts = effective_timestamp(nullptr, &gbb_info, /*message_timestamp=*/333);
    REQUIRE(ts.has_value());
    REQUIRE(*ts == 333);
}

TEST_CASE("effective_timestamp is nullopt, not zero, when neither source is valid", "[acf][REQ-WIRE-012]") {
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
