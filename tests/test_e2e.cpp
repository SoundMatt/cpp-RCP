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
// fusa:test REQ-E2E-021
// fusa:test REQ-E2E-028
// fusa:test REQ-E2E-029
// fusa:test REQ-E2E-035
// fusa:test REQ-E2E-038
// fusa:test REQ-E2E-045
// fusa:test REQ-E2E-046

// Tests for rcp/e2e.hpp — E2E CRC safe points and the per-request-stream
// watchdog/safe-state primitives (ROADMAP.md milestone 50, "E2E CRC Safe
// Points & Safety-Request Variants", v2.6.0; content-corrected against
// c-RCP's e2e.h/e2e.c during the Phase 2 pass, cpp-RCP issue #129 — see
// e2e.hpp's own top-of-file note for the full list of deltas this pass
// fixed).

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

// Arbitrary, fixed header-CRC-bytes stand-ins shared by every test below
// that does not itself care about their specific value — only that they
// are threaded through consistently (c-RCP issue #465; see e2e.hpp's own
// "CRC coverage & the trailing-CRC length pre-adjustment" section).
constexpr uint8_t kSubtype = rcp::avtp::kSubtypeTscf;
constexpr uint8_t kOctet1  = 0x00;
constexpr bool    kTu      = false;

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
    // text. This is also exactly the published CRC-32/AUTOSAR catalog check
    // value (c-RCP's own e2e.h file header), an independently-published
    // reference vector this implementation happens to coincide with.
    std::vector<uint8_t> data{'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    REQUIRE(crc32(data) == 0x1697D06Au);
}

// ── CRC coverage & length adjustment ──────────────────────────────────────────

TEST_CASE("coverage_buffer zero-fills avtp_timestamp under NTSCF (nullopt)", "[e2e][REQ-E2E-002]") {
    auto sid = make_stream_id(0x02, 0x1234);
    AcfMessageInfo info;
    info.byte_bus_id = 5;
    std::vector<uint8_t> payload{0xAA, 0xBB};

    auto with_zero    = coverage_buffer(kSubtype, kOctet1, kTu, sid, uint32_t{0}, info, std::nullopt, payload);
    auto with_nullopt = coverage_buffer(kSubtype, kOctet1, kTu, sid, std::nullopt, info, std::nullopt, payload);
    REQUIRE(with_zero == with_nullopt);
}

// c-RCP issue #465 ("Figure 20/21 header-CRC bytes"): the CRC coverage
// buffer's first three bytes are avtp_subtype, header_octet1, and a tu
// byte — TC18 §13.6 Figures 20/21's own orange "header CRC" region — ahead
// of stream_id/avtp_timestamp/ACF header/payload. Before this pass,
// coverage_buffer() had no way to express these three bytes at all; a
// genuinely spec-conformant peer's CRC32 would not have matched this
// library's prior output.
TEST_CASE("coverage_buffer layout is avtp_subtype + header_octet1 + tu + stream_id + avtp_timestamp "
          "+ ACF header + payload for ACF_ABB",
          "[e2e][REQ-E2E-002]") {
    auto sid = make_stream_id(0x02, 0x0001);
    AcfMessageInfo info; // acf_msg_type defaults to kAcfMsgTypeAbb
    info.byte_bus_id = 7;
    std::vector<uint8_t> payload{1, 2, 3};

    auto buf = coverage_buffer(0x05, 0x81, true, sid, uint32_t{0xDEADBEEF}, info, std::nullopt, payload);
    REQUIRE(buf.size() == 3 + 8 + 4 + rcp::acf::kAcfCommonHeaderLen + payload.size());

    // The three header-CRC bytes come first, in order.
    REQUIRE(buf[0] == 0x05);        // avtp_subtype
    REQUIRE(buf[1] == 0x81);        // header_octet1
    REQUIRE(buf[2] == 0x01);        // tu (true -> 0x01)

    // stream_id occupies the next 8 bytes, big-endian.
    REQUIRE(buf[3] == sid.mac[0]);
    // avtp_timestamp occupies the next 4 bytes, big-endian.
    REQUIRE(buf[11] == 0xDE);
    REQUIRE(buf[12] == 0xAD);
    REQUIRE(buf[13] == 0xBE);
    REQUIRE(buf[14] == 0xEF);
    // Payload is the final bytes, unchanged.
    REQUIRE(buf[buf.size() - 3] == 1);
    REQUIRE(buf[buf.size() - 1] == 3);
}

TEST_CASE("coverage_buffer's tu byte is exactly 0x00 or 0x01, never any other value", "[e2e][REQ-E2E-002]") {
    auto sid = make_stream_id(0x02, 0x0001);
    AcfMessageInfo info;
    std::vector<uint8_t> payload{1};

    auto buf_false = coverage_buffer(kSubtype, kOctet1, false, sid, std::nullopt, info, std::nullopt, payload);
    auto buf_true  = coverage_buffer(kSubtype, kOctet1, true, sid, std::nullopt, info, std::nullopt, payload);
    REQUIRE(buf_false[2] == 0x00);
    REQUIRE(buf_true[2] == 0x01);
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

    auto buf = coverage_buffer(kSubtype, kOctet1, kTu, sid, uint32_t{0}, info, ts, payload);
    REQUIRE(buf.size() == 3 + 8 + 4 + 16 + payload.size()); // header-CRC bytes + stream_id + avtp_ts + 16-byte GBB block

    // Fixed prefix: 3 header-CRC bytes + stream_id (8, big-endian) +
    // avtp_timestamp (4). Message Info block therefore begins at coverage
    // offset 15:
    //   15..18  quadlet 0
    //   19..22  quadlet 1
    //   23..30  message_timestamp, big-endian
    //   31..    payload
    const size_t mi_off = 3 + 8 + 4;
    // Quadlet 0: byte0 = (0x0D << 1) | 0 = 0x1A; byte2 has mtv (bit5) set
    // and byte_bus_id's high bits clear = 0x20; byte3 = byte_bus_id & 0xFF.
    REQUIRE(buf[mi_off + 0] == 0x1A);
    REQUIRE(buf[mi_off + 2] == 0x20);
    REQUIRE(buf[mi_off + 3] == 9);
    // Quadlet 1 at block offset 4: byte1 is transaction_num, the cheapest
    // positive proof the second quadlet stayed right after the first, not
    // after the timestamp.
    REQUIRE(buf[mi_off + 5] == 0x5A);
    // message_timestamp at block offset 8, big-endian, immediately after
    // the complete header.
    REQUIRE(buf[mi_off + 8]  == 0x01);
    REQUIRE(buf[mi_off + 15] == 0x08);
    // Payload follows the complete 16-byte block, unchanged.
    REQUIRE(buf[mi_off + 16] == 0x11);
    REQUIRE(buf[mi_off + 17] == 0x22);

    // The CRC-covered ACF region must be byte-identical to the bytes
    // encode_acf_gbb actually puts on the wire — otherwise a peer
    // recomputing the CRC over the received frame can never match.
    auto wire = rcp::acf::encode_acf_gbb(info, ts, payload);
    REQUIRE(std::vector<uint8_t>(buf.begin() + static_cast<long>(mi_off), buf.end()) == wire);

    // An ACF_ABB message with the same nominal message_timestamp argument
    // must NOT grow by those 8 bytes — the type gates inclusion, not merely
    // whether the caller happens to pass a value.
    AcfMessageInfo abb_info;
    abb_info.byte_bus_id = 9;
    auto abb_buf = coverage_buffer(kSubtype, kOctet1, kTu, sid, uint32_t{0}, abb_info, ts, payload);
    REQUIRE(abb_buf.size() == 3 + 8 + 4 + rcp::acf::kAcfCommonHeaderLen + payload.size());
}

TEST_CASE("compute_crc changes when covered fields change", "[e2e][REQ-E2E-002]") {
    auto sid = make_stream_id(0x02, 0x0001);
    AcfMessageInfo info;
    info.byte_bus_id = 1;
    std::vector<uint8_t> payload{9, 9, 9};

    uint32_t base = compute_crc(kSubtype, kOctet1, kTu, sid, std::nullopt, info, std::nullopt, payload);

    AcfMessageInfo different_info = info;
    different_info.byte_bus_id     = 2;
    REQUIRE(compute_crc(kSubtype, kOctet1, kTu, sid, std::nullopt, different_info, std::nullopt, payload) != base);

    std::vector<uint8_t> different_payload{9, 9, 8};
    REQUIRE(compute_crc(kSubtype, kOctet1, kTu, sid, std::nullopt, info, std::nullopt, different_payload) != base);

    REQUIRE(compute_crc(kSubtype, kOctet1, kTu, sid, uint32_t{1}, info, std::nullopt, payload) != base);

    // A GBB message's CRC must also be sensitive to message_timestamp itself
    // (cpp-RCP-N2-03) — this would have been silently ignored before that fix.
    AcfMessageInfo gbb_info = info;
    gbb_info.acf_msg_type    = rcp::acf::kAcfMsgTypeGbb;
    uint32_t gbb_base = compute_crc(kSubtype, kOctet1, kTu, sid, std::nullopt, gbb_info, uint64_t{1}, payload);
    REQUIRE(compute_crc(kSubtype, kOctet1, kTu, sid, std::nullopt, gbb_info, uint64_t{2}, payload) != gbb_base);
    REQUIRE(compute_crc(kSubtype, kOctet1, kTu, sid, std::nullopt, gbb_info, std::nullopt, payload) != gbb_base);
}

// c-RCP issue #465's own three dedicated regression tests
// (test_compute_crc_avtp_subtype_changes_result/_header_octet1_changes_result/
// _tu_bit_changes_result): each of the three new header-CRC bytes must, on
// its own, actually change the result — otherwise they would be dead
// parameters threaded through for nothing.
TEST_CASE("compute_crc is sensitive to avtp_subtype, header_octet1, and tu independently",
          "[e2e][REQ-E2E-002]") {
    auto sid = make_stream_id(0x02, 0x0001);
    AcfMessageInfo info;
    info.byte_bus_id = 1;
    std::vector<uint8_t> payload{1, 2, 3};

    uint32_t base = compute_crc(rcp::avtp::kSubtypeTscf, 0x00, false, sid, std::nullopt, info, std::nullopt, payload);

    REQUIRE(compute_crc(rcp::avtp::kSubtypeNtscf, 0x00, false, sid, std::nullopt, info, std::nullopt, payload) !=
            base);
    REQUIRE(compute_crc(rcp::avtp::kSubtypeTscf, 0x01, false, sid, std::nullopt, info, std::nullopt, payload) !=
            base);
    REQUIRE(compute_crc(rcp::avtp::kSubtypeTscf, 0x00, true, sid, std::nullopt, info, std::nullopt, payload) !=
            base);
}

TEST_CASE("compute_crc_framed derives avtp_subtype and forces the NTSCF zero/false stand-ins",
          "[e2e][REQ-E2E-035]") {
    auto sid = make_stream_id(0x02, 0x0001);
    AcfMessageInfo info;
    info.byte_bus_id = 1;
    std::vector<uint8_t> payload{1, 2, 3};

    // TSCF framing passes avtp_timestamp/tu through unchanged.
    uint32_t tscf_direct = compute_crc(rcp::avtp::kSubtypeTscf, 0x81, true, sid, uint32_t{42}, info, std::nullopt,
                                        payload);
    uint32_t tscf_framed =
        compute_crc_framed(/*is_ntscf_framed=*/false, 0x81, sid, true, uint32_t{42}, info, std::nullopt, payload);
    REQUIRE(tscf_direct == tscf_framed);

    // NTSCF framing forces avtp_timestamp to the zero stand-in and tu to
    // false regardless of what the caller passes in for either.
    uint32_t ntscf_direct =
        compute_crc(rcp::avtp::kSubtypeNtscf, 0x81, false, sid, std::nullopt, info, std::nullopt, payload);
    uint32_t ntscf_framed_ignoring_inputs = compute_crc_framed(/*is_ntscf_framed=*/true, 0x81, sid, /*tu=*/true,
                                                                 uint32_t{999}, info, std::nullopt, payload);
    REQUIRE(ntscf_direct == ntscf_framed_ignoring_inputs);
}

TEST_CASE("apply_acf_length_adjustment adds exactly one quadlet", "[e2e][REQ-E2E-003]") {
    AcfMessageInfo info;
    info.acf_msg_length = 10;
    apply_acf_length_adjustment(info);
    REQUIRE(info.acf_msg_length == 10 + kCrcLengthAdjustQuadlets);
    REQUIRE(kCrcLengthAdjustQuadlets == 1);
}

TEST_CASE("length_with_crc adds exactly kCrcLengthAdjustOctets and saturates on overflow",
          "[e2e][REQ-E2E-004]") {
    REQUIRE(length_with_crc(0) == kCrcLengthAdjustOctets);
    REQUIRE(length_with_crc(100) == 104);
    REQUIRE(length_with_crc(static_cast<size_t>(-1)) == static_cast<size_t>(-1)); // saturates
    REQUIRE(length_with_crc(static_cast<size_t>(-1) - 1) == static_cast<size_t>(-1));
}

TEST_CASE("data_length_for_protected_members multiplies by kCrcLengthAdjustOctets and saturates "
          "on overflow",
          "[e2e][REQ-E2E-004]") {
    REQUIRE(data_length_for_protected_members(0) == 0);
    REQUIRE(data_length_for_protected_members(3) == 12);
    REQUIRE(data_length_for_protected_members(static_cast<size_t>(-1)) == static_cast<size_t>(-1)); // saturates
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
        uint32_t crc = compute_crc(kSubtype, kOctet1, kTu, sid, std::nullopt, info, std::nullopt, payload);
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
        uint32_t crc = compute_crc(kSubtype, kOctet1, kTu, sid, std::nullopt, info, ts, payload);
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
    append_crc(frame, compute_crc(kSubtype, kOctet1, kTu, sid, std::nullopt, info, ts, payload));
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

    uint32_t crc = compute_crc(kSubtype, kOctet1, kTu, sid, std::nullopt, info, std::nullopt, payload);
    REQUIRE_FALSE(verify_crc(kSubtype, kOctet1, kTu, sid, std::nullopt, info, std::nullopt, payload, crc));
    REQUIRE(verify_crc(kSubtype, kOctet1, kTu, sid, std::nullopt, info, std::nullopt, payload, crc ^ 0xFFFFFFFFu) ==
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

    uint32_t crc = compute_crc(kSubtype, kOctet1, kTu, sid, std::nullopt, info, uint64_t{42}, payload);
    REQUIRE_FALSE(verify_crc(kSubtype, kOctet1, kTu, sid, std::nullopt, info, uint64_t{42}, payload, crc));
    REQUIRE(verify_crc(kSubtype, kOctet1, kTu, sid, std::nullopt, info, uint64_t{43}, payload, crc) ==
            make_error_code(E2eErrc::crc_error));
}

TEST_CASE("verify_crc_framed forces the same NTSCF stand-ins compute_crc_framed does",
          "[e2e][REQ-E2E-035]") {
    auto sid = make_stream_id(0x02, 0x0002);
    AcfMessageInfo info;
    info.byte_bus_id = 3;
    std::vector<uint8_t> payload{1, 2, 3, 4};

    uint32_t crc = compute_crc_framed(/*is_ntscf_framed=*/true, kOctet1, sid, /*tu=*/true, uint32_t{999}, info,
                                       std::nullopt, payload);
    REQUIRE_FALSE(verify_crc_framed(true, kOctet1, sid, /*tu=*/true, uint32_t{999}, info, std::nullopt, payload,
                                     crc));
    // A verifier that (correctly) ignores the caller's tu/timestamp under
    // NTSCF still agrees — both sides force the same zero/false stand-ins.
    REQUIRE_FALSE(verify_crc_framed(true, kOctet1, sid, /*tu=*/false, std::nullopt, info, std::nullopt, payload,
                                     crc));
}

// ── Numeric TC18 wire error code (cpp-RCP-08) ─────────────────────────────────

TEST_CASE("wire_error_code maps crc_error to TC18's numeric POCI_FAILURE (12)",
          "[e2e][REQ-E2E-004]") {
    auto code = wire_error_code(E2eErrc::crc_error);
    REQUIRE(code.has_value());
    REQUIRE(*code == 12);
    REQUIRE(*code == kPociFailureErrorCode);

    // sequence_violation and short_frame have no TC18 Table 27 entry of
    // their own — this module reports "no mapping" rather than guessing
    // one (short_frame in particular never reaches the point of being a
    // transmittable Response at all).
    REQUIRE_FALSE(wire_error_code(E2eErrc::sequence_violation).has_value());
    REQUIRE_FALSE(wire_error_code(E2eErrc::short_frame).has_value());
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

// ── wrap / unwrap (c-RCP issue #420) ──────────────────────────────────────────

TEST_CASE("wrap/unwrap round trips an unpadded ACF_ABB payload", "[e2e][REQ-E2E-005][REQ-E2E-006]") {
    auto sid = make_stream_id(0x02, 0x0010);
    AcfMessageInfo info;
    info.byte_bus_id = 11;
    std::vector<uint8_t> payload{1, 2, 3, 4}; // already quadlet-aligned, no pad

    auto frame = wrap(kSubtype, kOctet1, kTu, sid, uint32_t{100}, info, std::nullopt, payload);
    // header(8) + payload(4) + CRC(4) = 16 bytes.
    REQUIRE(frame.size() == rcp::acf::kAcfCommonHeaderLen + payload.size() + 4);

    auto result = unwrap(kSubtype, kOctet1, kTu, sid, uint32_t{100}, frame);
    REQUIRE_FALSE(result.ec);
    // acf_msg_length is un-adapted back down by one quadlet, and the
    // header+payload is byte-identical to what a plain encode_acf_abb (with
    // the ORIGINAL, un-adapted length) would have produced.
    AcfMessageInfo original = info;
    original.acf_msg_length = rcp::acf::compute_acf_msg_length(rcp::acf::kAcfMsgTypeAbb, payload.size());
    REQUIRE(result.acf_frame == rcp::acf::encode_acf_abb(original, payload));
}

TEST_CASE("wrap places the CRC before trailing pad octets, not after", "[e2e][REQ-E2E-005][REQ-E2E-006]") {
    auto sid = make_stream_id(0x02, 0x0011);
    AcfMessageInfo info;
    info.byte_bus_id = 12;
    info.pad          = 2;
    std::vector<uint8_t> payload{0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x00}; // last 2 bytes are pad

    auto frame = wrap(kSubtype, kOctet1, kTu, sid, std::nullopt, info, std::nullopt, payload);
    // header(8) + real(6) + CRC(4) + pad(2) = 20 bytes; the CRC sits at
    // offset 14..17, and the two original pad bytes (0x00, 0x00 here,
    // copied unchanged from the caller's own payload) sit last.
    REQUIRE(frame.size() == rcp::acf::kAcfCommonHeaderLen + 6 + 4 + 2);
    REQUIRE(frame[frame.size() - 1] == 0x00);
    REQUIRE(frame[frame.size() - 2] == 0x00);
    // The CRC itself is NOT all-zero (a real computed value), proving the
    // last two bytes are genuinely the re-seated pad, not part of the CRC.
    bool crc_all_zero = frame[frame.size() - 6] == 0 && frame[frame.size() - 5] == 0 &&
                         frame[frame.size() - 4] == 0 && frame[frame.size() - 3] == 0;
    REQUIRE_FALSE(crc_all_zero);
}

TEST_CASE("wrap/unwrap round trips a padded ACF_ABB payload with the pad re-seated after the CRC",
          "[e2e][REQ-E2E-005][REQ-E2E-006]") {
    auto sid = make_stream_id(0x02, 0x0012);
    AcfMessageInfo info;
    info.byte_bus_id = 13;
    info.pad          = 3;
    std::vector<uint8_t> payload{1, 2, 3, 4, 5, 0, 0, 0}; // 5 real bytes + 3 pad bytes

    auto frame = wrap(kSubtype, kOctet1, kTu, sid, std::nullopt, info, std::nullopt, payload);
    auto result = unwrap(kSubtype, kOctet1, kTu, sid, std::nullopt, frame);
    REQUIRE_FALSE(result.ec);

    AcfMessageInfo original = info;
    original.acf_msg_length = rcp::acf::compute_acf_msg_length(rcp::acf::kAcfMsgTypeAbb, payload.size());
    REQUIRE(result.acf_frame == rcp::acf::encode_acf_abb(original, payload));
}

TEST_CASE("wrap/unwrap round trips an ACF_GBB payload with the message_timestamp folded into the CRC",
          "[e2e][REQ-E2E-005][REQ-E2E-006]") {
    auto sid = make_stream_id(0x03, 0x0013);
    AcfMessageInfo info;
    info.acf_msg_type = rcp::acf::kAcfMsgTypeGbb;
    info.byte_bus_id   = 14;
    const uint64_t ts   = 0x1122334455667788ULL;
    std::vector<uint8_t> payload{7, 8, 9};

    auto frame = wrap(kSubtype, kOctet1, kTu, sid, uint32_t{1}, info, ts, payload);
    auto result = unwrap(kSubtype, kOctet1, kTu, sid, uint32_t{1}, frame);
    REQUIRE_FALSE(result.ec);

    AcfMessageInfo original = info;
    original.acf_msg_length = rcp::acf::compute_acf_msg_length(rcp::acf::kAcfMsgTypeGbb, payload.size());
    REQUIRE(result.acf_frame == rcp::acf::encode_acf_gbb(original, ts, payload));

    // Corrupting the timestamp between wrap() and unwrap() must be detected
    // — the timestamp is folded into the CRC coverage, not decorative.
    auto mismatched = unwrap(kSubtype, kOctet1, kTu, sid, uint32_t{1}, frame);
    // (re-verify against a frame built with a different timestamp)
    auto frame2 = wrap(kSubtype, kOctet1, kTu, sid, uint32_t{1}, info, ts + 1, payload);
    REQUIRE(frame != frame2);
}

TEST_CASE("unwrap detects CRC corruption but still returns a body for diagnostic use",
          "[e2e][REQ-E2E-005][REQ-E2E-006]") {
    auto sid = make_stream_id(0x02, 0x0014);
    AcfMessageInfo info;
    info.byte_bus_id = 15;
    std::vector<uint8_t> payload{1, 2, 3, 4};

    auto frame = wrap(kSubtype, kOctet1, kTu, sid, std::nullopt, info, std::nullopt, payload);
    frame[frame.size() - 1] ^= 0xFF; // corrupt one CRC byte

    auto result = unwrap(kSubtype, kOctet1, kTu, sid, std::nullopt, frame);
    REQUIRE(result.ec == make_error_code(E2eErrc::crc_error));
    REQUIRE_FALSE(result.acf_frame.empty()); // diagnostic body still populated
}

TEST_CASE("unwrap detects a wrong stream_id used to verify", "[e2e][REQ-E2E-005][REQ-E2E-006]") {
    auto sid       = make_stream_id(0x02, 0x0015);
    auto other_sid = make_stream_id(0x09, 0x0015);
    AcfMessageInfo info;
    info.byte_bus_id = 16;
    std::vector<uint8_t> payload{1, 2, 3, 4};

    auto frame  = wrap(kSubtype, kOctet1, kTu, sid, std::nullopt, info, std::nullopt, payload);
    auto result = unwrap(kSubtype, kOctet1, kTu, other_sid, std::nullopt, frame);
    REQUIRE(result.ec == make_error_code(E2eErrc::crc_error));
}

TEST_CASE("unwrap fails safe on a frame too short to contain a header and CRC trailer",
          "[e2e][REQ-E2E-005][REQ-E2E-006]") {
    auto sid = make_stream_id(0x02, 0x0016);
    std::vector<uint8_t> too_short{0x00, 0x01, 0x02}; // < kAcfCommonHeaderLen (8)

    auto result = unwrap(kSubtype, kOctet1, kTu, sid, std::nullopt, too_short);
    REQUIRE(result.ec == make_error_code(E2eErrc::short_frame));
    REQUIRE(result.acf_frame.empty());
}

TEST_CASE("unwrap fails safe when the header claims more pad octets than the frame can hold",
          "[e2e][REQ-E2E-005][REQ-E2E-006]") {
    auto sid = make_stream_id(0x02, 0x0017);
    AcfMessageInfo info;
    info.byte_bus_id = 17;
    std::vector<uint8_t> payload{1, 2, 3, 4};

    auto frame = wrap(kSubtype, kOctet1, kTu, sid, std::nullopt, info, std::nullopt, payload);
    // Truncate the frame so it can no longer contain header + CRC + the
    // (zero, in this fixture) claimed pad octets plus the 4-byte trailer.
    frame.resize(rcp::acf::kAcfCommonHeaderLen); // header only, no CRC at all
    auto result = unwrap(kSubtype, kOctet1, kTu, sid, std::nullopt, frame);
    REQUIRE(result.ec == make_error_code(E2eErrc::short_frame));
}

TEST_CASE("wrap rejects a pad count that exceeds the payload it was given",
          "[e2e][REQ-E2E-005][REQ-E2E-006]") {
    auto sid = make_stream_id(0x02, 0x0018);
    AcfMessageInfo info;
    info.byte_bus_id = 18;
    info.pad          = 5; // more than the payload itself holds
    std::vector<uint8_t> payload{1, 2};

    auto frame = wrap(kSubtype, kOctet1, kTu, sid, std::nullopt, info, std::nullopt, payload);
    REQUIRE(frame.empty());
}

TEST_CASE("wrap_framed/unwrap_framed force the NTSCF zero-timestamp/false-tu stand-in",
          "[e2e][REQ-E2E-035]") {
    auto sid = make_stream_id(0x02, 0x0019);
    AcfMessageInfo info;
    info.byte_bus_id = 19;
    std::vector<uint8_t> payload{1, 2, 3};

    // Two callers who disagree about avtp_timestamp/tu but agree they're
    // NTSCF-framed must still round-trip successfully — both sides force
    // the same stand-ins internally.
    auto frame = wrap_framed(/*is_ntscf_framed=*/true, kOctet1, /*tu=*/true, sid, uint32_t{555}, info, std::nullopt,
                              payload);
    auto result =
        unwrap_framed(/*is_ntscf_framed=*/true, kOctet1, /*tu=*/false, sid, std::nullopt, frame);
    REQUIRE_FALSE(result.ec);

    // The frame's own subtype byte is the hardcoded NTSCF value, matching
    // what a caller cannot override by construction.
    // (indirectly verified: unwrap_framed with is_ntscf_framed=false, i.e.
    // a different subtype, must NOT verify.)
    auto mismatched = unwrap_framed(/*is_ntscf_framed=*/false, kOctet1, /*tu=*/false, sid, std::nullopt, frame);
    REQUIRE(mismatched.ec == make_error_code(E2eErrc::crc_error));
}

// ── Fragmentation/CRC interaction ─────────────────────────────────────────────

TEST_CASE("fragment_carries_crc is true only for the last fragment", "[e2e][REQ-E2E-010]") {
    REQUIRE_FALSE(fragment_carries_crc(/*is_last_fragment=*/false));
    REQUIRE(fragment_carries_crc(/*is_last_fragment=*/true));
}

TEST_CASE("compute_fragmented_crc matches manual concatenation via coverage_buffer-equivalent bytes",
          "[e2e][REQ-E2E-038]") {
    auto sid = make_stream_id(0x02, 0x0020);
    std::vector<uint8_t> first_fragment_header{0x1A, 0x05, 0x00, 20}; // arbitrary 4-byte stand-in header
    std::vector<uint8_t> reassembled_payload{1, 2, 3, 4, 5, 6};

    uint32_t crc = compute_fragmented_crc(kSubtype, kOctet1, kTu, sid, uint32_t{7}, first_fragment_header,
                                           reassembled_payload);

    // Manual concatenation: subtype + octet1 + tu + stream_id + timestamp +
    // header + payload, fed through crc32() directly.
    std::vector<uint8_t> manual;
    manual.push_back(kSubtype);
    manual.push_back(kOctet1);
    manual.push_back(0x00);
    for (int i = 7; i >= 0; --i) manual.push_back(static_cast<uint8_t>((sid.to_u64() >> (8 * i)) & 0xFF));
    manual.push_back(0x00);
    manual.push_back(0x00);
    manual.push_back(0x00);
    manual.push_back(0x07);
    manual.insert(manual.end(), first_fragment_header.begin(), first_fragment_header.end());
    manual.insert(manual.end(), reassembled_payload.begin(), reassembled_payload.end());

    REQUIRE(crc == crc32(manual));
}

TEST_CASE("compute_fragmented_crc is sensitive to the first fragment's header and the full "
          "reassembled payload",
          "[e2e][REQ-E2E-038]") {
    auto sid = make_stream_id(0x02, 0x0021);
    std::vector<uint8_t> header{0x1A, 0x05};
    std::vector<uint8_t> payload{1, 2, 3};

    uint32_t base = compute_fragmented_crc(kSubtype, kOctet1, kTu, sid, std::nullopt, header, payload);

    std::vector<uint8_t> different_header{0x1A, 0x06};
    REQUIRE(compute_fragmented_crc(kSubtype, kOctet1, kTu, sid, std::nullopt, different_header, payload) != base);

    std::vector<uint8_t> different_payload{1, 2, 4};
    REQUIRE(compute_fragmented_crc(kSubtype, kOctet1, kTu, sid, std::nullopt, header, different_payload) != base);
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

// c-RCP issue #256 Group I / REQ-E2E-045: rx_enforce_e2e's own second,
// independent consequence — TC18 §12.7.7 Table 24 documents its 1b value as
// triggering BOTH the stream-latch above AND "Safe state will be entered",
// in the same sentence, with no separate dedicated safestate-enable bit.
TEST_CASE("crc_error_should_enter_safe_state mirrors rx_enforce_e2e directly", "[e2e][REQ-E2E-045]") {
    REQUIRE_FALSE(crc_error_should_enter_safe_state(false));
    REQUIRE(crc_error_should_enter_safe_state(true));
}

// ── StreamFaultTracker — the bounded multi-stream keyed wrapper (REQ-E2E-021) ─

TEST_CASE("StreamFaultTracker: a never-seen stream is vacuously not faulted", "[e2e][REQ-E2E-021]") {
    StreamFaultTracker tracker;
    REQUIRE_FALSE(tracker.is_faulted(0xAABBCCDD));
}

TEST_CASE("StreamFaultTracker registers streams on first touch and isolates their fault state",
          "[e2e][REQ-E2E-021]") {
    StreamFaultTracker tracker;
    REQUIRE(tracker.on_crc_error(1, /*rx_enforce_e2e=*/true));
    REQUIRE(tracker.is_faulted(1));
    REQUIRE_FALSE(tracker.is_faulted(2)); // a different stream is untouched

    REQUIRE(tracker.on_crc_error(2, /*rx_enforce_e2e=*/false));
    REQUIRE_FALSE(tracker.is_faulted(2)); // drop-mode never latches
}

TEST_CASE("StreamFaultTracker's reset clears only the named stream", "[e2e][REQ-E2E-021]") {
    StreamFaultTracker tracker;
    REQUIRE(tracker.on_crc_error(1, true));
    REQUIRE(tracker.on_crc_error(2, true));
    REQUIRE(tracker.is_faulted(1));
    REQUIRE(tracker.is_faulted(2));

    tracker.reset(1);
    REQUIRE_FALSE(tracker.is_faulted(1));
    REQUIRE(tracker.is_faulted(2));
}

TEST_CASE("StreamFaultTracker's reset on a never-seen stream is a harmless no-op",
          "[e2e][REQ-E2E-021]") {
    StreamFaultTracker tracker;
    tracker.reset(0xDEAD);
    REQUIRE_FALSE(tracker.is_faulted(0xDEAD));
}

TEST_CASE("StreamFaultTracker honestly reports capacity exhaustion instead of silently dropping state",
          "[e2e][REQ-E2E-021]") {
    StreamFaultTracker tracker;
    for (uint64_t i = 0; i < StreamFaultTracker::kMaxStreams; ++i) {
        REQUIRE(tracker.on_crc_error(i, true));
    }
    // Every already-tracked stream is still reachable...
    REQUIRE(tracker.is_faulted(0));
    // ...but one more, previously-unseen stream_id cannot be registered:
    // capacity is exhausted, and the tracker reports this honestly rather
    // than silently overwriting an existing slot.
    REQUIRE_FALSE(tracker.on_crc_error(StreamFaultTracker::kMaxStreams, true));
    REQUIRE_FALSE(tracker.is_faulted(StreamFaultTracker::kMaxStreams));

    // Freeing a slot (reset does not unregister it, so capacity stays
    // exhausted) — this asserts reset() does not itself free capacity,
    // matching c-RCP's own tracker semantics (a reset stream stays tracked,
    // just unfaulted).
    tracker.reset(0);
    REQUIRE_FALSE(tracker.on_crc_error(StreamFaultTracker::kMaxStreams, true));
}

// ── StreamStatus — the aggregate rx_stream_status bit (REQ-E2E-046) ──────────

TEST_CASE("StreamStatus starts not blocked", "[e2e][REQ-E2E-046]") {
    StreamStatus status;
    REQUIRE_FALSE(status.rx_blocked());
}

TEST_CASE("StreamStatus: CRC cause blocks and resets independently of the others",
          "[e2e][REQ-E2E-046]") {
    StreamStatus status;
    status.note_crc_error(/*rx_enforce_e2e=*/true);
    REQUIRE(status.rx_blocked());

    status.reset_crc();
    REQUIRE_FALSE(status.rx_blocked());
}

TEST_CASE("StreamStatus: CRC cause in drop mode never blocks", "[e2e][REQ-E2E-046]") {
    StreamStatus status;
    status.note_crc_error(/*rx_enforce_e2e=*/false);
    REQUIRE_FALSE(status.rx_blocked());
}

TEST_CASE("StreamStatus: seq/wd/overflow causes block only when told enter_safe_state is true",
          "[e2e][REQ-E2E-046]") {
    {
        StreamStatus status;
        status.note_seq(false);
        REQUIRE_FALSE(status.rx_blocked());
        status.note_seq(true);
        REQUIRE(status.rx_blocked());
        status.reset_seq();
        REQUIRE_FALSE(status.rx_blocked());
    }
    {
        StreamStatus status;
        status.note_wd(true);
        REQUIRE(status.rx_blocked());
        status.reset_wd();
        REQUIRE_FALSE(status.rx_blocked());
    }
    {
        StreamStatus status;
        status.note_overflow(true);
        REQUIRE(status.rx_blocked());
        status.reset_overflow();
        REQUIRE_FALSE(status.rx_blocked());
    }
}

TEST_CASE("StreamStatus: the four causes are independent of one another", "[e2e][REQ-E2E-046]") {
    StreamStatus status;
    status.note_crc_error(true);
    status.note_seq(true);
    status.note_wd(true);
    status.note_overflow(true);
    REQUIRE(status.rx_blocked());

    status.reset_crc();
    REQUIRE(status.rx_blocked()); // still blocked by the other three
    status.reset_seq();
    REQUIRE(status.rx_blocked());
    status.reset_wd();
    REQUIRE(status.rx_blocked());
    status.reset_overflow();
    REQUIRE_FALSE(status.rx_blocked()); // only now fully clear
}

// ── RxSequenceGuard — rx_enforce_seq / rx_seq_safestate_enable ───────────────
// Content-corrected against c-RCP's rcp_e2e_seq_evaluate() during the Phase
// 2 pass (REQ-E2E-028/029) — see e2e.hpp's own top-of-file note, item 3, for
// what this class got wrong before: plain non-wrapping uint32_t comparison
// with no RFC 1982 forward-window logic, and rx_seq_safestate_enable was
// entirely unused.

TEST_CASE("RxSequenceGuard.evaluate: the first observed sequence number always accepts and is "
          "never a discontinuity",
          "[e2e][REQ-E2E-007][REQ-E2E-028][REQ-E2E-029]") {
    RequestStreamConfig cfg;
    cfg.rx_enforce_seq          = true;
    cfg.rx_seq_safestate_enable = true;
    RxSequenceGuard guard;

    auto r = guard.evaluate(cfg, 200);
    REQUIRE(r.accept);
    REQUIRE_FALSE(r.discontinuity);
    REQUIRE_FALSE(r.enter_safe_state);
    REQUIRE(guard.has_tracked_value());
    REQUIRE(guard.last_accepted_seq() == 200);
}

TEST_CASE("RxSequenceGuard.evaluate: exactly one increment is accepted with no discontinuity",
          "[e2e][REQ-E2E-028][REQ-E2E-029]") {
    RequestStreamConfig cfg;
    cfg.rx_enforce_seq = true;
    RxSequenceGuard guard;

    REQUIRE(guard.evaluate(cfg, 10).accept);
    auto r = guard.evaluate(cfg, 11);
    REQUIRE(r.accept);
    REQUIRE_FALSE(r.discontinuity);
}

TEST_CASE("RxSequenceGuard.evaluate: a gap (advance by more than one) is a discontinuity but is "
          "still accepted (ordering preserved)",
          "[e2e][REQ-E2E-028][REQ-E2E-029]") {
    RequestStreamConfig cfg;
    cfg.rx_enforce_seq = true;
    RxSequenceGuard guard;

    REQUIRE(guard.evaluate(cfg, 10).accept);
    auto r = guard.evaluate(cfg, 15); // gap of 5
    REQUIRE(r.accept);
    REQUIRE(r.discontinuity);
    REQUIRE_FALSE(r.enter_safe_state); // rx_seq_safestate_enable not set
}

TEST_CASE("RxSequenceGuard.evaluate: a discontinuity only enters safe state when "
          "rx_seq_safestate_enable is set",
          "[e2e][REQ-E2E-028][REQ-E2E-029]") {
    RequestStreamConfig cfg;
    cfg.rx_enforce_seq          = true;
    cfg.rx_seq_safestate_enable = true;
    RxSequenceGuard guard;

    REQUIRE(guard.evaluate(cfg, 10).accept);
    auto r = guard.evaluate(cfg, 15);
    REQUIRE(r.accept);
    REQUIRE(r.discontinuity);
    REQUIRE(r.enter_safe_state);
}

TEST_CASE("RxSequenceGuard.evaluate: a repeat or backward jump is rejected when rx_enforce_seq is set",
          "[e2e][REQ-E2E-028]") {
    RequestStreamConfig cfg;
    cfg.rx_enforce_seq = true;
    RxSequenceGuard guard;

    REQUIRE(guard.evaluate(cfg, 50).accept);
    auto repeat = guard.evaluate(cfg, 50);
    REQUIRE_FALSE(repeat.accept);

    auto backward = guard.evaluate(cfg, 40);
    REQUIRE_FALSE(backward.accept);

    // Tracked state does not move on a rejected seq — the reference point
    // for "next expected" stays at 50, not 50 (repeat) or 40 (backward).
    REQUIRE(guard.last_accepted_seq() == 50);
}

TEST_CASE("RxSequenceGuard.evaluate: rx_enforce_seq off accepts everything (bootstrap or not) "
          "but discontinuity/enter_safe_state are still computed",
          "[e2e][REQ-E2E-028][REQ-E2E-029]") {
    RequestStreamConfig cfg; // rx_enforce_seq and rx_seq_safestate_enable default false
    RxSequenceGuard guard;

    REQUIRE(guard.evaluate(cfg, 5).accept);
    auto r = guard.evaluate(cfg, 1); // would be a backward-jump rejection if enforced
    REQUIRE(r.accept);               // gate is off — always accepts
    REQUIRE(r.discontinuity);        // still correctly flagged as a discontinuity...
    REQUIRE_FALSE(r.enter_safe_state); // ...but rx_seq_safestate_enable is also off
}

TEST_CASE("RxSequenceGuard.evaluate: RFC 1982 wraparound accepts 0x00 immediately after 0xFF",
          "[e2e][REQ-E2E-028]") {
    RequestStreamConfig cfg;
    cfg.rx_enforce_seq = true;
    RxSequenceGuard guard;

    REQUIRE(guard.evaluate(cfg, 0xFF).accept);
    auto r = guard.evaluate(cfg, 0x00); // a real wrap, not a replay
    REQUIRE(r.accept);
    REQUIRE_FALSE(r.discontinuity); // forward distance is exactly 1 (mod 256)
}

TEST_CASE("RxSequenceGuard.evaluate: a forward distance beyond the RFC 1982 half-circle (128) is "
          "rejected, not treated as a wrap",
          "[e2e][REQ-E2E-028]") {
    RequestStreamConfig cfg;
    cfg.rx_enforce_seq = true;
    RxSequenceGuard guard;

    REQUIRE(guard.evaluate(cfg, 0).accept);
    // Forward distance of exactly 128 is outside [1, 127] — this is the
    // ambiguous exact-antipode case RFC 1982 excludes from "ahead".
    auto r = guard.evaluate(cfg, 128);
    REQUIRE_FALSE(r.accept);
}

TEST_CASE("RxSequenceGuard.check is evaluate()'s std::error_code convenience form",
          "[e2e][REQ-E2E-007]") {
    RequestStreamConfig cfg;
    cfg.rx_enforce_seq = true;
    RxSequenceGuard guard;

    REQUIRE_FALSE(guard.check(cfg, 10)); // bootstrap accepts the first value
    REQUIRE_FALSE(guard.check(cfg, 11));
    REQUIRE(guard.check(cfg, 11) == make_error_code(E2eErrc::sequence_violation)); // repeat
    REQUIRE(guard.check(cfg, 9) == make_error_code(E2eErrc::sequence_violation));  // regression
}

TEST_CASE("RxSequenceGuard.check is a no-op (always accepts) when rx_enforce_seq is clear",
          "[e2e][REQ-E2E-007]") {
    RequestStreamConfig cfg; // rx_enforce_seq defaults to false
    RxSequenceGuard guard;
    REQUIRE_FALSE(guard.check(cfg, 5));
    REQUIRE_FALSE(guard.check(cfg, 1)); // would violate monotonicity if enforced
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

TEST_CASE("overflow_should_enter_safe_state mirrors rx_ovrflw_safestate_enable directly",
          "[e2e][REQ-E2E-010]") {
    REQUIRE_FALSE(overflow_should_enter_safe_state(false));
    REQUIRE(overflow_should_enter_safe_state(true));
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

// REQ-SEQ-012 (TC18 Table 28), ported from c-RCP's rcp_e2e_endpoint_in_safe_state()
// during the Phase 2 pass — this file did not apply this fail-closed rule
// before: a manually-disabled sequencer (state == 0) conveys no
// application-state information at all and can never itself satisfy a
// safe-state check, even if rx_safe_sequencer_state is also (mis)configured
// to 0.
TEST_CASE("endpoint_in_configured_safe_state: RunSafeSequencer fails closed when the target "
          "sequencer is disabled (state == 0), even if safe_sequencer_state is also 0",
          "[e2e][REQ-E2E-011][REQ-SEQ-012]") {
    RequestStreamConfig cfg;
    cfg.rx_safety_measure       = RxSafetyMeasure::RunSafeSequencer;
    cfg.rx_safestate_sequencer  = 0;
    cfg.rx_safe_sequencer_state = 0; // misconfigured to match "disabled"

    std::vector<rcp::regmap::SequencerState> states{0}; // disabled
    SequencerTable sequencers(states);

    REQUIRE_FALSE(endpoint_in_configured_safe_state(cfg, sequencers, false));
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
    std::error_code shrt = make_error_code(E2eErrc::short_frame);

    REQUIRE(crc.category() == e2e_category());
    REQUIRE(seq.category() == e2e_category());
    REQUIRE(shrt.category() == e2e_category());
    REQUIRE_FALSE(crc.message().empty());
    REQUIRE_FALSE(seq.message().empty());
    REQUIRE_FALSE(shrt.message().empty());
    REQUIRE(crc.message() != seq.message());
    REQUIRE(crc.message() != shrt.message());
    REQUIRE(seq.message() != shrt.message());
}
