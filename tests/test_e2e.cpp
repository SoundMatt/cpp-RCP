// fusa:test REQ-E2E-001
// fusa:test REQ-E2E-002
// fusa:test REQ-E2E-003
// fusa:test REQ-E2E-004
// fusa:test REQ-E2E-005
// fusa:test REQ-E2E-006
// fusa:test REQ-E2E-007
// fusa:test REQ-E2E-008
// fusa:test REQ-E2E-009
// fusa:test REQ-E2E-010
// fusa:test REQ-E2E-011
// fusa:test REQ-E2E-012
// fusa:test REQ-E2E-013
// fusa:test REQ-E2E-014

// Tests for rcp/e2e.hpp — E2E CRC safe points and the per-request-stream
// watchdog/safe-state primitives (ROADMAP.md milestone 50, "E2E CRC Safe
// Points & Safety-Request Variants", v2.6.0).

#include <catch2/catch_test_macros.hpp>
#include <rcp/e2e.hpp>

using namespace rcp::e2e;
using rcp::regmap::EndpointGenericConfig;
using rcp::regmap::RequestStreamConfig;
using rcp::regmap::RxSafetyMeasure;
using rcp::request::RequestLedger;
using rcp::request::RequestRecord;
using rcp::request::RequestTypeOpcode;
using rcp::request::SequencerTable;
using rcp::request::request_record_for;
using rcp::acf::AcfMessageInfo;
using rcp::avtp::StreamId;

namespace {

StreamId make_stream_id(uint8_t mac_seed, uint16_t suffix) {
    StreamId id;
    for (auto& b : id.mac) b = mac_seed++;
    id.suffix = suffix;
    return id;
}

} // namespace

// ── CRC32 primitive ───────────────────────────────────────────────────────────

TEST_CASE("crc32 of empty input is the all-ones init XORed with all-ones xorout", "[e2e][REQ-E2E-001]") {
    // With init=0xFFFFFFFF and no bytes processed, the update loop never
    // runs, so the result is init ^ xorout == 0xFFFFFFFF ^ 0xFFFFFFFF == 0.
    REQUIRE(crc32(nullptr, 0) == 0u);
}

TEST_CASE("crc32 is deterministic and sensitive to every input byte", "[e2e][REQ-E2E-001]") {
    std::vector<uint8_t> a{0x01, 0x02, 0x03, 0x04};
    std::vector<uint8_t> b{0x01, 0x02, 0x03, 0x05};
    REQUIRE(crc32(a) == crc32(a));
    REQUIRE(crc32(a) != crc32(b));
}

TEST_CASE("crc32 known-vector regression check", "[e2e][REQ-E2E-001]") {
    // Cross-checked against an independent reference implementation of the
    // standard reflected-CRC construction (RefIn=true, RefOut=true, init
    // 0xFFFFFFFF, xorout 0xFFFFFFFF) using this file's own polynomial
    // (0xF4ACFB13) — not a vector taken from the confidential specification
    // text, purely a regression guard against this implementation
    // silently changing behavior.
    std::vector<uint8_t> data{'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    REQUIRE(crc32(data) == 0x1697D06Au);
}

// ── CRC coverage & length adjustment ──────────────────────────────────────────

TEST_CASE("coverage_buffer zero-fills avtp_timestamp under NTSCF (nullopt)", "[e2e][REQ-E2E-002]") {
    auto sid = make_stream_id(0x02, 0x1234);
    AcfMessageInfo info;
    info.byte_bus_id = 5;
    std::vector<uint8_t> payload{0xAA, 0xBB};

    auto with_zero    = coverage_buffer(sid, uint32_t{0}, info, std::nullopt, payload);
    auto with_nullopt = coverage_buffer(sid, std::nullopt, info, std::nullopt, payload);
    REQUIRE(with_zero == with_nullopt);
}

TEST_CASE("coverage_buffer layout is stream_id + avtp_timestamp + ACF header + payload for ACF_ABB",
          "[e2e][REQ-E2E-002]") {
    auto sid = make_stream_id(0x02, 0x0001);
    AcfMessageInfo info; // acf_msg_type defaults to kAcfMsgTypeAbb
    info.byte_bus_id = 7;
    std::vector<uint8_t> payload{1, 2, 3};

    auto buf = coverage_buffer(sid, uint32_t{0xDEADBEEF}, info, std::nullopt, payload);
    REQUIRE(buf.size() == 8 + 4 + rcp::acf::kAcfCommonHeaderLen + payload.size());

    // stream_id occupies the first 8 bytes, big-endian.
    REQUIRE(buf[0] == sid.mac[0]);
    // avtp_timestamp occupies the next 4 bytes, big-endian.
    REQUIRE(buf[8]  == 0xDE);
    REQUIRE(buf[9]  == 0xAD);
    REQUIRE(buf[10] == 0xBE);
    REQUIRE(buf[11] == 0xEF);
    // Payload is the final bytes, unchanged.
    REQUIRE(buf[buf.size() - 3] == 1);
    REQUIRE(buf[buf.size() - 1] == 3);
}

// cpp-RCP-N2-03 / cpp-RCP-GBB-TS: for ACF_GBB the wire carries an 8-byte
// message_timestamp *inside* the Message Info block, immediately after the
// complete 8-byte header (contiguous, not spliced between the header's two
// quadlets) — the specification's single-ACF_GBB CRC-coverage figure draws
// one "Byte Message Info" group whose rows are, in order, the
// acf_msg_type/acf_msg_length/pad/mtv/rsv/byte_bus_id quadlet, then the
// evt/rsv/hs/cs/transaction_num/op/rsp/err/ms/read_size quadlet, then
// message_time_stamp as a double-height 64-bit block. The CRC coverage
// buffer must reproduce that byte order exactly, because the CRC has to
// cover the bytes actually transmitted. A prior pass (v2.22.0) spliced the
// timestamp between the two quadlets instead; that was a regression against
// c-RCP's confirmed-RC5-conformant `rcp_acf_encode_gbb()`, reverted during
// the Phase 17 rewrite (cpp-RCP issue #129) — the offsets below are derived
// from acf.hpp's actual `kAcfGbbTimestampOffset` (contiguous, after the full
// header), not from a hand-derived figure.
TEST_CASE("coverage_buffer places the 8-byte message_timestamp contiguously after the ACF "
          "header's two quadlets for ACF_GBB",
          "[e2e][REQ-E2E-002]") {
    auto sid = make_stream_id(0x02, 0x0003);
    AcfMessageInfo info;
    info.acf_msg_type = rcp::acf::kAcfMsgTypeGbb;
    info.byte_bus_id   = 9;
    info.mtv            = true;
    info.transaction_num = 0x5A;
    std::vector<uint8_t> payload{0x11, 0x22};
    const uint64_t ts = 0x0102030405060708ULL;
    // Set explicitly so coverage_buffer (which never auto-fills) and
    // encode_acf_gbb (which auto-fills only a 0) serialize the same header.
    info.acf_msg_length = rcp::acf::compute_acf_msg_length(rcp::acf::kAcfMsgTypeGbb, payload.size());

    auto buf = coverage_buffer(sid, uint32_t{0}, info, ts, payload);
    REQUIRE(buf.size() == 8 + 4 + 16 + payload.size()); // stream_id + avtp_ts + 16-byte GBB block

    // Fixed prefix: stream_id (8, big-endian) + avtp_timestamp (4).
    // Message Info block therefore begins at coverage offset 12:
    //   12..15  quadlet 0
    //   16..19  quadlet 1
    //   20..27  message_timestamp, big-endian
    //   28..    payload
    const size_t mi_off = 8 + 4;
    // Quadlet 0: byte0 = (0x0D << 1) | 0 = 0x1A; byte2 has mtv (bit5) set
    // and byte_bus_id's high bits clear = 0x20; byte3 = byte_bus_id & 0xFF.
    REQUIRE(buf[mi_off + 0] == 0x1A);
    REQUIRE(buf[mi_off + 2] == 0x20);
    REQUIRE(buf[mi_off + 3] == 9);
    // Quadlet 1 at block offset 4 (coverage offset 16): byte1 is
    // transaction_num, which is the cheapest positive proof the second
    // quadlet stayed right after the first, not after the timestamp.
    REQUIRE(buf[mi_off + 5] == 0x5A);
    // message_timestamp at block offset 8 (coverage offset 20), big-endian,
    // immediately after the complete header.
    REQUIRE(buf[mi_off + 8]  == 0x01);
    REQUIRE(buf[mi_off + 15] == 0x08);
    // Payload follows the complete 16-byte block, unchanged.
    REQUIRE(buf[mi_off + 16] == 0x11);
    REQUIRE(buf[mi_off + 17] == 0x22);

    // The CRC-covered bytes must be byte-identical to the bytes
    // encode_acf_gbb actually puts on the wire — otherwise a peer
    // recomputing the CRC over the received frame can never match.
    auto wire = rcp::acf::encode_acf_gbb(info, ts, payload);
    REQUIRE(std::vector<uint8_t>(buf.begin() + static_cast<long>(mi_off), buf.end()) == wire);

    // An ACF_ABB message with the same nominal message_timestamp argument
    // must NOT grow by those 8 bytes — the type gates inclusion, not merely
    // whether the caller happens to pass a value.
    AcfMessageInfo abb_info;
    abb_info.byte_bus_id = 9;
    auto abb_buf = coverage_buffer(sid, uint32_t{0}, abb_info, ts, payload);
    REQUIRE(abb_buf.size() == 8 + 4 + rcp::acf::kAcfCommonHeaderLen + payload.size());
}

TEST_CASE("compute_crc changes when covered fields change", "[e2e][REQ-E2E-002]") {
    auto sid = make_stream_id(0x02, 0x0001);
    AcfMessageInfo info;
    info.byte_bus_id = 1;
    std::vector<uint8_t> payload{9, 9, 9};

    uint32_t base = compute_crc(sid, std::nullopt, info, std::nullopt, payload);

    AcfMessageInfo different_info = info;
    different_info.byte_bus_id     = 2;
    REQUIRE(compute_crc(sid, std::nullopt, different_info, std::nullopt, payload) != base);

    std::vector<uint8_t> different_payload{9, 9, 8};
    REQUIRE(compute_crc(sid, std::nullopt, info, std::nullopt, different_payload) != base);

    REQUIRE(compute_crc(sid, uint32_t{1}, info, std::nullopt, payload) != base);

    // A GBB message's CRC must also be sensitive to message_timestamp itself
    // (cpp-RCP-N2-03) — this would have been silently ignored before the fix.
    AcfMessageInfo gbb_info = info;
    gbb_info.acf_msg_type    = rcp::acf::kAcfMsgTypeGbb;
    uint32_t gbb_base = compute_crc(sid, std::nullopt, gbb_info, uint64_t{1}, payload);
    REQUIRE(compute_crc(sid, std::nullopt, gbb_info, uint64_t{2}, payload) != gbb_base);
    REQUIRE(compute_crc(sid, std::nullopt, gbb_info, std::nullopt, payload) != gbb_base);
}

TEST_CASE("apply_acf_length_adjustment adds exactly one quadlet", "[e2e][REQ-E2E-003]") {
    AcfMessageInfo info;
    info.acf_msg_length = 10;
    apply_acf_length_adjustment(info);
    REQUIRE(info.acf_msg_length == 10 + kCrcLengthAdjustQuadlets);
    REQUIRE(kCrcLengthAdjustQuadlets == 1);
}

TEST_CASE("apply_frame_length_adjustment adds exactly four octets to NTSCF and TSCF headers",
          "[e2e][REQ-E2E-003]") {
    rcp::avtp::NtscfHeader ntscf;
    ntscf.control_data_length = 20;
    apply_frame_length_adjustment(ntscf);
    REQUIRE(ntscf.control_data_length == 20 + kCrcLengthAdjustOctets);

    rcp::avtp::TscfHeader tscf;
    tscf.control_data_length = 30;
    apply_frame_length_adjustment(tscf);
    REQUIRE(tscf.control_data_length == 30 + kCrcLengthAdjustOctets);

    REQUIRE(kCrcLengthAdjustOctets == 4);
}

// ── acf_msg_length worked-example pins (issue cpp-RCP-01) ────────────────────
// These two cases reproduce the specification's own two fully worked
// numeric examples end to end (compute_acf_msg_length ->
// apply_acf_length_adjustment -> encode_acf_abb/encode_acf_gbb ->
// append_crc), confirming the final wire acf_msg_length equals the
// specification's own stated value in each case, not just that some
// nonzero value comes out.
TEST_CASE("acf_msg_length end-to-end matches the specification's own worked examples "
          "(ACF_ABB -> 0x05, ACF_GBB -> 0x07)",
          "[e2e][acf][REQ-E2E-003][REQ-WIRE-004][REQ-WIRE-005]") {
    using rcp::acf::compute_acf_msg_length;
    using rcp::acf::encode_acf_abb;
    using rcp::acf::encode_acf_gbb;
    using rcp::acf::kAcfMsgTypeAbb;
    using rcp::acf::kAcfMsgTypeGbb;
    using rcp::acf::kAcfCommonHeaderLen;
    using rcp::acf::kAcfGbbTimestampLen;

    // ACF_ABB: 6 real payload bytes + 2 pad bytes (caller-padded, per this
    // codec's own AcfMessageInfo::pad convention — see acf.hpp's "ACF
    // shared header" section) = 8 payload bytes, plus a trailing 4-byte
    // CRC32 trailer. header(2 quadlets) + payload(2 quadlets) + CRC(1
    // quadlet) = 5 quadlets = 0x05, header + payload = 20 bytes total.
    {
        AcfMessageInfo info;
        info.pad = 2;
        std::vector<uint8_t> payload(8, 0); // 6 "real" bytes + 2 pad bytes

        info.acf_msg_length = compute_acf_msg_length(kAcfMsgTypeAbb, payload.size());
        REQUIRE(info.acf_msg_length == 4); // base, before the CRC trailer is counted
        apply_acf_length_adjustment(info);
        REQUIRE(info.acf_msg_length == 5); // 0x05

        auto frame = encode_acf_abb(info, payload);
        REQUIRE(frame.size() == kAcfCommonHeaderLen + payload.size()); // 8 + 8 = 16 bytes

        auto sid = make_stream_id(0x02, 1);
        uint32_t crc = compute_crc(sid, std::nullopt, info, std::nullopt, payload);
        append_crc(frame, crc);
        REQUIRE(frame.size() == 20); // 8 header + 8 payload + 4 CRC = 20 bytes = 5 quadlets

        // The wire header actually carries 5: byte0 bit0 is acf_msg_length's
        // MSB (0, since 5 < 256) and byte1 is its low 8 bits.
        REQUIRE((frame[0] & 0x01) == 0);
        REQUIRE(frame[1] == 5);
    }

    // ACF_GBB: 7 real payload bytes + 1 pad byte = 8 payload bytes, plus the
    // mandatory 8-byte message_timestamp and a trailing 4-byte CRC32.
    // header(2) + timestamp(2) + payload(2) + CRC(1) = 7 quadlets = 0x07,
    // 28 bytes total.
    {
        AcfMessageInfo info;
        info.pad = 1;
        info.mtv = true;
        std::vector<uint8_t> payload(8, 0); // 7 "real" bytes + 1 pad byte
        const uint64_t ts = 0x1122334455667788ULL;

        info.acf_msg_length = compute_acf_msg_length(kAcfMsgTypeGbb, payload.size());
        REQUIRE(info.acf_msg_length == 6); // base, before the CRC trailer is counted
        apply_acf_length_adjustment(info);
        REQUIRE(info.acf_msg_length == 7); // 0x07

        auto frame = encode_acf_gbb(info, ts, payload);
        // 16-byte Message Info block (quadlet0 + quadlet1 + 8-byte
        // message_timestamp, contiguous) + 8-byte payload = 24.
        REQUIRE(frame.size() == rcp::acf::kAcfGbbMessageInfoLen + payload.size()); // 16+8=24

        auto sid = make_stream_id(0x03, 1);
        uint32_t crc = compute_crc(sid, std::nullopt, info, ts, payload);
        append_crc(frame, crc);
        REQUIRE(frame.size() == 28); // 24 + 4 CRC = 28 bytes = 7 quadlets

        REQUIRE((frame[0] & 0x01) == 0);
        REQUIRE(frame[1] == 7);

        // Quadlet 1 occupies octets 4..7, immediately after quadlet 0; with
        // every field left at its default in this fixture, all four of its
        // bytes are 0.
        REQUIRE(frame[4] == 0x00);
        REQUIRE(frame[7] == 0x00);
        // The timestamp occupies octets 8..15 — contiguous, right after the
        // complete 8-byte header, not spliced between the two quadlets.
        REQUIRE(frame[8]  == 0x11); // message_timestamp MSB
        REQUIRE(frame[15] == 0x88); // message_timestamp LSB
    }
}

// Full-message conformance vector reproducing the specification's own
// single-ACF_GBB CRC-coverage figure field for field: acf_msg_type = 0x0D,
// acf_msg_length = 0x07, pad = 1, a 7-real-byte payload padded to 8, and a
// trailing CRC32 — 28 octets total. Every octet position below comes from
// that figure's row structure (quadlet 0 || quadlet 1 || 64-bit
// message_time_stamp || byte_msg_payload || CRC32) — the timestamp is
// contiguous right after the complete 8-byte header, not spliced between
// the two quadlets — cross-checked against c-RCP's confirmed-RC5-conformant
// `rcp_acf_encode_gbb()`, not derived from this codec's own output.
TEST_CASE("ACF_GBB full-message layout matches the specification's CRC-coverage figure "
          "octet for octet",
          "[e2e][acf][REQ-WIRE-005][REQ-E2E-002]") {
    using rcp::acf::AcfMessageInfo;

    AcfMessageInfo info;
    info.acf_msg_type   = rcp::acf::kAcfMsgTypeGbb;
    info.acf_msg_length = 0x07; // the figure's own stated value
    info.pad             = 1;    // the figure's own stated value
    info.mtv             = true;
    info.byte_bus_id     = 0x123;
    info.transaction_num = 0x42;
    info.op               = true;
    info.read_size_or_segment_num = 0x0AB;

    const uint64_t ts = 0xDEADBEEFCAFEF00DULL;
    // 7 real payload octets + 1 pad octet, matching the figure's PL_Byte1..7
    // plus one 0x00 (padding) cell.
    const std::vector<uint8_t> payload{0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0x00};

    // Octet-by-octet expectation, hand-derived:
    //   [0]  = (0x0D << 1) | (0x07 >> 8)          = 0x1A
    //   [1]  = 0x07 & 0xFF                        = 0x07
    //   [2]  = (1 << 6) | (mtv << 5) | (0x123>>8) = 0x40|0x20|0x01 = 0x61
    //   [3]  = 0x123 & 0xFF                       = 0x23
    //   [4]  = (evt=0 << 4) | (hs=0 << 1) | cs=0  = 0x00
    //   [5]  = transaction_num                    = 0x42
    //   [6]  = (op << 7) | (0x0AB >> 8)           = 0x80|0x00 = 0x80
    //   [7]  = 0x0AB & 0xFF                       = 0xAB
    //   [8..15] = message_timestamp, big-endian, immediately after the
    //             complete 8-byte header
    //   [16..23] = byte_msg_payload (7 real + 1 pad)
    const std::vector<uint8_t> expected{
        0x1A, 0x07, 0x61, 0x23,
        0x00, 0x42, 0x80, 0xAB,
        0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xF0, 0x0D,
        0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0x00,
    };

    auto frame = rcp::acf::encode_acf_gbb(info, ts, payload);
    REQUIRE(frame == expected);

    // ...and with the CRC32 trailer the figure also shows, the frame is
    // exactly acf_msg_length * 4 = 28 octets.
    auto sid = make_stream_id(0x03, 0x0007);
    append_crc(frame, compute_crc(sid, std::nullopt, info, ts, payload));
    REQUIRE(frame.size() == static_cast<size_t>(info.acf_msg_length) * 4);
    REQUIRE(frame.size() == 28);

    // Round trip from the hand-written literal back to fields.
    AcfMessageInfo decoded;
    uint64_t decoded_ts = 0;
    std::vector<uint8_t> decoded_payload;
    REQUIRE_FALSE(rcp::acf::decode_acf_gbb(expected.data(), expected.size(), decoded, decoded_ts,
                                            decoded_payload));
    REQUIRE(decoded.acf_msg_length == 0x07);
    REQUIRE(decoded.pad == 1);
    REQUIRE(decoded.mtv == true);
    REQUIRE(decoded.byte_bus_id == 0x123);
    REQUIRE(decoded.transaction_num == 0x42);
    REQUIRE(decoded.op == true);
    REQUIRE(decoded.read_size_or_segment_num == 0x0AB);
    REQUIRE(decoded_ts == ts);
    REQUIRE(decoded_payload == payload);
}

// ── verify_crc / append_crc ────────────────────────────────────────────────────

TEST_CASE("verify_crc accepts a matching CRC and rejects a corrupted one", "[e2e][REQ-E2E-004]") {
    auto sid = make_stream_id(0x02, 0x0002);
    AcfMessageInfo info;
    info.byte_bus_id = 3;
    std::vector<uint8_t> payload{1, 2, 3, 4};

    uint32_t crc = compute_crc(sid, std::nullopt, info, std::nullopt, payload);
    REQUIRE_FALSE(verify_crc(sid, std::nullopt, info, std::nullopt, payload, crc));
    REQUIRE(verify_crc(sid, std::nullopt, info, std::nullopt, payload, crc ^ 0xFFFFFFFFu) ==
            make_error_code(E2eErrc::crc_error));
}

TEST_CASE("verify_crc for ACF_GBB fails if the message_timestamp used to verify differs from "
          "the one used to compute",
          "[e2e][REQ-E2E-004]") {
    auto sid = make_stream_id(0x02, 0x0005);
    AcfMessageInfo info;
    info.acf_msg_type = rcp::acf::kAcfMsgTypeGbb;
    info.byte_bus_id   = 4;
    std::vector<uint8_t> payload{5, 6, 7};

    uint32_t crc = compute_crc(sid, std::nullopt, info, uint64_t{42}, payload);
    REQUIRE_FALSE(verify_crc(sid, std::nullopt, info, uint64_t{42}, payload, crc));
    REQUIRE(verify_crc(sid, std::nullopt, info, uint64_t{43}, payload, crc) ==
            make_error_code(E2eErrc::crc_error));
}

// ── Numeric TC18 wire error code (cpp-RCP-08) ─────────────────────────────────

TEST_CASE("wire_error_code maps crc_error to TC18's numeric POCI_FAILURE (12)",
          "[e2e][REQ-E2E-004]") {
    auto code = wire_error_code(E2eErrc::crc_error);
    REQUIRE(code.has_value());
    REQUIRE(*code == 12);
    REQUIRE(*code == kPociFailureErrorCode);

    // sequence_violation has no TC18 Table 27 entry of its own — this
    // module reports "no mapping" rather than guessing one.
    REQUIRE_FALSE(wire_error_code(E2eErrc::sequence_violation).has_value());
}

TEST_CASE("append_crc appends exactly 4 big-endian octets", "[e2e][REQ-E2E-004]") {
    std::vector<uint8_t> frame{0x11, 0x22};
    append_crc(frame, 0x01020304);
    REQUIRE(frame.size() == 6);
    REQUIRE(frame[2] == 0x01);
    REQUIRE(frame[3] == 0x02);
    REQUIRE(frame[4] == 0x03);
    REQUIRE(frame[5] == 0x04);
}

// ── Per-endpoint opt-in safe mode ─────────────────────────────────────────────

TEST_CASE("crc_required reflects each independently-toggled endpoint config field",
          "[e2e][REQ-E2E-005]") {
    EndpointGenericConfig cfg;
    REQUIRE_FALSE(crc_required(cfg, MessageRole::Request));
    REQUIRE_FALSE(crc_required(cfg, MessageRole::Acknowledge));
    REQUIRE_FALSE(crc_required(cfg, MessageRole::Response));

    cfg.ep_req_crc_enable = true;
    REQUIRE(crc_required(cfg, MessageRole::Request));
    REQUIRE_FALSE(crc_required(cfg, MessageRole::Acknowledge));
    REQUIRE_FALSE(crc_required(cfg, MessageRole::Response));

    cfg.ep_ack_crc_enable      = true;
    cfg.ep_response_crc_enable = true;
    REQUIRE(crc_required(cfg, MessageRole::Acknowledge));
    REQUIRE(crc_required(cfg, MessageRole::Response));
}

TEST_CASE("implemented_options_bit reports kOptSafetyRequests only when actually implemented",
          "[e2e][REQ-E2E-005]") {
    REQUIRE(implemented_options_bit(false) == 0u);
    REQUIRE(implemented_options_bit(true) == rcp::regmap::kOptSafetyRequests);
}

// ── RxStreamGuard — rx_enforce_e2e ────────────────────────────────────────────

TEST_CASE("RxStreamGuard drops only the failing request when rx_enforce_e2e is clear",
          "[e2e][REQ-E2E-006]") {
    RequestStreamConfig cfg; // rx_enforce_e2e defaults to false
    RxStreamGuard guard;

    REQUIRE(guard.record_crc_result(cfg, /*ok=*/false) == make_error_code(E2eErrc::crc_error));
    REQUIRE_FALSE(guard.latched());
    REQUIRE_FALSE(guard.record_crc_result(cfg, /*ok=*/true)); // next request unaffected
}

TEST_CASE("RxStreamGuard latches the whole stream when rx_enforce_e2e is set", "[e2e][REQ-E2E-006]") {
    RequestStreamConfig cfg;
    cfg.rx_enforce_e2e = true;
    RxStreamGuard guard;

    REQUIRE(guard.record_crc_result(cfg, /*ok=*/false) == make_error_code(E2eErrc::crc_error));
    REQUIRE(guard.latched());
    // Every subsequent request fails too, even one whose own CRC was fine.
    REQUIRE(guard.record_crc_result(cfg, /*ok=*/true) == make_error_code(E2eErrc::crc_error));

    guard.reset_latch();
    REQUIRE_FALSE(guard.latched());
    REQUIRE_FALSE(guard.record_crc_result(cfg, /*ok=*/true));
}

// ── RxSequenceGuard — rx_enforce_seq ───────────────────────────────────────────

TEST_CASE("RxSequenceGuard is a no-op when rx_enforce_seq is clear", "[e2e][REQ-E2E-007]") {
    RequestStreamConfig cfg; // rx_enforce_seq defaults to false
    RxSequenceGuard guard;
    REQUIRE_FALSE(guard.check(cfg, 5));
    REQUIRE_FALSE(guard.check(cfg, 1)); // would violate monotonicity if enforced
}

TEST_CASE("RxSequenceGuard rejects a non-increasing sequence number when enforced",
          "[e2e][REQ-E2E-007]") {
    RequestStreamConfig cfg;
    cfg.rx_enforce_seq = true;
    RxSequenceGuard guard;

    REQUIRE_FALSE(guard.check(cfg, 10)); // bootstrap accepts the first value
    REQUIRE_FALSE(guard.check(cfg, 11));
    REQUIRE(guard.check(cfg, 11) == make_error_code(E2eErrc::sequence_violation)); // repeat
    REQUIRE(guard.check(cfg, 9) == make_error_code(E2eErrc::sequence_violation));  // regression
}

// ── RxWatchdog — rx_wd_enable / rx_wd_timeout_interval ────────────────────────

TEST_CASE("RxWatchdog never overflows while disabled or before any kick", "[e2e][REQ-E2E-008]") {
    RequestStreamConfig cfg;
    cfg.rx_wd_timeout_interval = 100;
    RxWatchdog wd;
    REQUIRE_FALSE(wd.overflowed(cfg, /*now_ms=*/10'000)); // rx_wd_enable defaults to false

    cfg.rx_wd_enable = true;
    REQUIRE_FALSE(wd.overflowed(cfg, /*now_ms=*/10'000)); // never kicked
}

TEST_CASE("RxWatchdog overflows once the timeout interval elapses since the last kick",
          "[e2e][REQ-E2E-008]") {
    RequestStreamConfig cfg;
    cfg.rx_wd_enable           = true;
    cfg.rx_wd_timeout_interval = 100;
    RxWatchdog wd;

    wd.kick(1'000);
    REQUIRE_FALSE(wd.overflowed(cfg, 1'050));
    REQUIRE(wd.overflowed(cfg, 1'101));

    wd.kick(1'101);
    REQUIRE_FALSE(wd.overflowed(cfg, 1'150));
}

// ── Watchdog/queue overflow purge-normal/retain-safety ────────────────────────

TEST_CASE("apply_watchdog_overflow purges normal requests but retains safety-tagged ones",
          "[e2e][REQ-E2E-009]") {
    RequestStreamConfig cfg;
    cfg.rx_wd_safestate_enable = true;

    RequestLedger ledger;
    REQUIRE_FALSE(ledger.submit(request_record_for(1, RequestTypeOpcode::Compound, /*cs=*/false)));
    REQUIRE_FALSE(ledger.submit(request_record_for(2, RequestTypeOpcode::CompoundSafety, /*cs=*/false)));

    RxWatchdog wd;
    size_t purged = apply_watchdog_overflow(cfg, wd, ledger);

    REQUIRE(purged == 1);
    REQUIRE(wd.in_safe_state());
    REQUIRE(ledger.find(1)->state == rcp::request::RequestState::Canceled);
    REQUIRE(ledger.find(2)->state == rcp::request::RequestState::Pending);
}

TEST_CASE("apply_watchdog_overflow purges nothing and does not enter safe state when disabled",
          "[e2e][REQ-E2E-009]") {
    RequestStreamConfig cfg; // rx_wd_safestate_enable defaults to false
    RequestLedger ledger;
    REQUIRE_FALSE(ledger.submit(request_record_for(1, RequestTypeOpcode::Compound, /*cs=*/false)));

    RxWatchdog wd;
    REQUIRE(apply_watchdog_overflow(cfg, wd, ledger) == 0);
    REQUIRE_FALSE(wd.in_safe_state());
    REQUIRE(ledger.find(1)->state == rcp::request::RequestState::Pending);
}

TEST_CASE("apply_queue_overflow implements the same purge-normal/retain-safety rule via a "
          "distinct trigger",
          "[e2e][REQ-E2E-010]") {
    RequestStreamConfig cfg;
    cfg.rx_ovrflw_safestate_enable = true;

    RequestLedger ledger;
    REQUIRE_FALSE(ledger.submit(request_record_for(1, RequestTypeOpcode::Triggered, /*cs=*/false)));
    REQUIRE_FALSE(ledger.submit(request_record_for(2, RequestTypeOpcode::TriggeredSafety, /*cs=*/false)));

    RxWatchdog wd;
    REQUIRE(apply_queue_overflow(cfg, wd, ledger) == 1);
    REQUIRE(wd.in_safe_state());
    REQUIRE(ledger.find(1)->state == rcp::request::RequestState::Canceled);
    REQUIRE(ledger.find(2)->state == rcp::request::RequestState::Pending);
}

// ── Safe-state gating ─────────────────────────────────────────────────────────

TEST_CASE("endpoint_in_configured_safe_state: ForceHighImpedance defers to the caller-supplied flag",
          "[e2e][REQ-E2E-011]") {
    RequestStreamConfig cfg; // rx_safety_measure defaults to ForceHighImpedance
    std::vector<rcp::regmap::SequencerState> states;
    SequencerTable sequencers(states);

    REQUIRE_FALSE(endpoint_in_configured_safe_state(cfg, sequencers, /*force_high_impedance_asserted=*/false));
    REQUIRE(endpoint_in_configured_safe_state(cfg, sequencers, /*force_high_impedance_asserted=*/true));
}

TEST_CASE("endpoint_in_configured_safe_state: RunSafeSequencer checks the target sequencer's value",
          "[e2e][REQ-E2E-011]") {
    RequestStreamConfig cfg;
    cfg.rx_safety_measure       = RxSafetyMeasure::RunSafeSequencer;
    cfg.rx_safestate_sequencer  = 0;
    cfg.rx_safe_sequencer_state = 3;

    std::vector<rcp::regmap::SequencerState> states;
    SequencerTable sequencers(states);
    sequencers.ensure_size(1); // starts at SequencerTable::kDefaultState (1), not 3

    REQUIRE_FALSE(endpoint_in_configured_safe_state(cfg, sequencers, /*force_high_impedance_asserted=*/true));

    states[0] = 3;
    REQUIRE(endpoint_in_configured_safe_state(cfg, sequencers, /*force_high_impedance_asserted=*/false));
}

TEST_CASE("endpoint_in_configured_safe_state: RunSafeSequencer with an out-of-range index is never safe",
          "[e2e][REQ-E2E-011]") {
    RequestStreamConfig cfg;
    cfg.rx_safety_measure      = RxSafetyMeasure::RunSafeSequencer;
    cfg.rx_safestate_sequencer = 5; // no sequencer at this index

    std::vector<rcp::regmap::SequencerState> states;
    SequencerTable sequencers(states);
    REQUIRE_FALSE(endpoint_in_configured_safe_state(cfg, sequencers, true));
}

TEST_CASE("may_execute_now: normal requests are always eligible; safety requests need safe state",
          "[e2e][REQ-E2E-012]") {
    RequestRecord normal = request_record_for(1, RequestTypeOpcode::Compound, false);
    RequestRecord safety = request_record_for(2, RequestTypeOpcode::CompoundSafety, false);

    REQUIRE(may_execute_now(normal, /*endpoint_in_safe_state=*/false));
    REQUIRE(may_execute_now(normal, /*endpoint_in_safe_state=*/true));
    REQUIRE_FALSE(may_execute_now(safety, /*endpoint_in_safe_state=*/false));
    REQUIRE(may_execute_now(safety, /*endpoint_in_safe_state=*/true));
}

// ── Watchdog info notification ────────────────────────────────────────────────

TEST_CASE("RxWatchdog emits the info notification only while latched with rx_wd_info_enable set",
          "[e2e][REQ-E2E-013]") {
    RequestStreamConfig cfg;
    RxWatchdog wd;

    REQUIRE_FALSE(wd.should_emit_info_notification(cfg)); // not latched yet

    wd.enter_safe_state();
    REQUIRE_FALSE(wd.should_emit_info_notification(cfg)); // latched, but feature disabled

    cfg.rx_wd_info_enable = true;
    REQUIRE(wd.should_emit_info_notification(cfg));

    wd.clear_safe_state();
    REQUIRE_FALSE(wd.should_emit_info_notification(cfg));
}

// ── Error category ────────────────────────────────────────────────────────────

TEST_CASE("E2eErrc is a distinct error category with non-empty, distinct messages",
          "[e2e][REQ-E2E-014]") {
    std::error_code crc  = make_error_code(E2eErrc::crc_error);
    std::error_code seq  = make_error_code(E2eErrc::sequence_violation);

    REQUIRE(crc.category() == e2e_category());
    REQUIRE(seq.category() == e2e_category());
    REQUIRE_FALSE(crc.message().empty());
    REQUIRE_FALSE(seq.message().empty());
    REQUIRE(crc.message() != seq.message());
}
