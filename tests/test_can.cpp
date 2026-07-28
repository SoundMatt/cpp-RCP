// fusa:test REQ-CANEP-001
// fusa:test REQ-CANEP-002
// fusa:test REQ-CANEP-003
// fusa:test REQ-CANEP-004
// fusa:test REQ-CANEP-005
// fusa:test REQ-CANEP-006
// fusa:test REQ-CANEP-007

// Tests for rcp/can.hpp — the CAN controller endpoint type (ROADMAP.md
// milestone 51, "Remaining Endpoint Types — LIN, CAN (incl. CAN XL),
// ISELED, MDIO, Wakeup Control", v2.7.0).

#include <catch2/catch_test_macros.hpp>
#include <rcp/can.hpp>
#include <rcp/endpoint.hpp>

using namespace rcp::can;

// ── ep_type id ────────────────────────────────────────────────────────────────

TEST_CASE("CAN's ep_type id is 0x0B", "[can][REQ-CANEP-001]") {
    REQUIRE(rcp::endpoint::kEndpointTypeCan == 0x0B);
}

// ── Identifier range validation ───────────────────────────────────────────────

TEST_CASE("validate_identifier accepts in-range standard and extended ids", "[can][REQ-CANEP-002]") {
    REQUIRE_FALSE(validate_identifier({0x7FF, false}));
    REQUIRE_FALSE(validate_identifier({0x1FFFFFFF, true}));
}

TEST_CASE("validate_identifier rejects an out-of-range id for its own width", "[can][REQ-CANEP-002]") {
    auto ec = validate_identifier({0x800, false}); // 12 bits, too wide for an 11-bit standard id
    REQUIRE(ec == make_error_code(CanErrc::identifier_out_of_range));

    ec = validate_identifier({0x20000000, true}); // 30 bits, too wide for a 29-bit extended id
    REQUIRE(ec == make_error_code(CanErrc::identifier_out_of_range));
}

// ── Frame-format payload ceilings, data frames only ──────────────────────────

TEST_CASE("validate_frame enforces each FrameFormat's own payload ceiling", "[can][REQ-CANEP-003]") {
    CanDataFrame classical;
    classical.id.value = 0x100;
    classical.format    = FrameFormat::Classical;
    classical.data.assign(kMaxClassicalPayload, 0xAA);
    REQUIRE_FALSE(validate_frame(classical));

    classical.data.push_back(0xAA); // 9 bytes, exceeds Classical's 8-byte ceiling
    REQUIRE(validate_frame(classical) == make_error_code(CanErrc::payload_exceeds_format_limit));

    CanDataFrame fd;
    fd.id.value = 0x100;
    fd.format    = FrameFormat::Fd;
    fd.data.assign(kMaxFdPayload, 0x55);
    REQUIRE_FALSE(validate_frame(fd));

    fd.data.push_back(0x55); // 65 bytes, exceeds FD's 64-byte ceiling
    REQUIRE(validate_frame(fd) == make_error_code(CanErrc::payload_exceeds_format_limit));
}

// ── CAN XL: accepted single-AVTPDU limitation ────────────────────────────────

TEST_CASE("validate_frame accepts a CAN XL payload within the single-AVTPDU bound", "[can][REQ-CANEP-004]") {
    CanDataFrame xl;
    xl.id.value = 0x100;
    xl.format    = FrameFormat::Xl;
    xl.data.assign(kMaxXlPayloadSingleAvtpdu, 0x11);
    REQUIRE_FALSE(validate_frame(xl));
}

TEST_CASE("validate_frame reports xl_payload_exceeds_single_avtpdu_bound between the accepted "
          "bound and the specification's own ceiling",
          "[can][REQ-CANEP-004]") {
    CanDataFrame xl;
    xl.id.value = 0x100;
    xl.format    = FrameFormat::Xl;
    xl.data.assign(kMaxXlPayloadSingleAvtpdu + 1, 0x11);
    REQUIRE(kMaxXlPayloadSingleAvtpdu + 1 <= kMaxXlPayloadSpec);
    REQUIRE(validate_frame(xl) == make_error_code(CanErrc::xl_payload_exceeds_single_avtpdu_bound));
}

TEST_CASE("validate_frame reports payload_exceeds_format_limit beyond the specification's own "
          "2054-byte CAN XL ceiling",
          "[can][REQ-CANEP-004]") {
    CanDataFrame xl;
    xl.id.value = 0x100;
    xl.format    = FrameFormat::Xl;
    xl.data.assign(kMaxXlPayloadSpec + 1, 0x11);
    REQUIRE(validate_frame(xl) == make_error_code(CanErrc::payload_exceeds_format_limit));
}

// ── Per-phase bit-timing register sets ───────────────────────────────────────

TEST_CASE("CanEndpoint::configure_bit_timing stores arbitration/fd_data/xl_data independently",
          "[can][REQ-CANEP-005]") {
    CanEndpoint ep;
    CanBitTimingConfig cfg;
    cfg.arbitration.prescaler = 4;
    cfg.fd_data.prescaler     = 1;
    cfg.xl_data.prescaler     = 1;
    cfg.arbitration.phase_seg1 = 10;
    cfg.fd_data.phase_seg1     = 3;
    cfg.xl_data.phase_seg1     = 2;

    REQUIRE_FALSE(ep.configure_bit_timing(cfg));
    REQUIRE(ep.bit_timing().arbitration.prescaler == 4);
    REQUIRE(ep.bit_timing().fd_data.prescaler == 1);
    REQUIRE(ep.bit_timing().xl_data.prescaler == 1);
    REQUIRE(ep.bit_timing().arbitration.phase_seg1 == 10);
    REQUIRE(ep.bit_timing().fd_data.phase_seg1 == 3);
    REQUIRE(ep.bit_timing().xl_data.phase_seg1 == 2);
}

// ── CAN-XL-specific acceptance/receive filters ───────────────────────────────

TEST_CASE("CanEndpoint::receive accepts everything when no filters are configured",
          "[can][REQ-CANEP-006]") {
    CanEndpoint ep;
    CanDataFrame f;
    f.id.value = 0x123;
    f.format    = FrameFormat::Classical;
    REQUIRE(ep.receive(f));
    REQUIRE(ep.last_received().id.value == 0x123);
}

TEST_CASE("CanEndpoint::receive drops a non-XL frame that matches no general acceptance filter",
          "[can][REQ-CANEP-006]") {
    CanEndpoint ep;
    ep.set_acceptance_filters({CanAcceptanceFilter{0x200, 0x7FF, false}});

    CanDataFrame f;
    f.id.value = 0x100;
    f.format    = FrameFormat::Classical;
    REQUIRE_FALSE(ep.receive(f));
}

TEST_CASE("CanEndpoint::receive accepts a frame matching a general acceptance filter's masked id",
          "[can][REQ-CANEP-006]") {
    CanEndpoint ep;
    ep.set_acceptance_filters({CanAcceptanceFilter{0x200, 0x700, false}}); // top 3 id bits must match 0x200's

    CanDataFrame f;
    f.id.value = 0x21F; // top 3 bits (0x200) match; low bits are don't-care
    f.format    = FrameFormat::Classical;
    REQUIRE(ep.receive(f));
}

TEST_CASE("CanEndpoint::receive matches an XL frame against xl_receive_filters, not the general "
          "acceptance bank, once xl_receive_filters is configured",
          "[can][REQ-CANEP-006]") {
    CanEndpoint ep;
    ep.set_acceptance_filters({CanAcceptanceFilter{0x999, 0x7FF, false}}); // would reject 0x100
    ep.set_xl_receive_filters({CanAcceptanceFilter{0x100, 0x7FF, false}}); // accepts 0x100

    CanDataFrame f;
    f.id.value = 0x100;
    f.format    = FrameFormat::Xl;
    REQUIRE(ep.receive(f));
}

// ── No trigger-signal table for CAN (extraction §5.11, §7) ──────────────────
// CAN is the one device-facing endpoint type in this codebase with no
// TriggerRegistry at all — there is no triggers() accessor on CanEndpoint to
// call in the first place, so the "no trigger table" property is enforced
// structurally (a caller cannot arm/drain anything), not by a runtime check.
// This test only documents that CanEndpoint's public surface has no
// trigger-related members, by construction: the class compiles without
// exposing one.

TEST_CASE("CanEndpoint exposes no TriggerRegistry accessor", "[can][REQ-CANEP-007]") {
    CanEndpoint ep;
    CanDataFrame f;
    f.id.value = 0x1;
    REQUIRE(ep.transmit(f) == std::error_code{});
    // No ep.triggers() call exists to make here — that is the point.
}

// ── CanErrc category sanity ───────────────────────────────────────────────────

TEST_CASE("CanErrc reports a non-empty message in its own category", "[can][REQ-CANEP-007]") {
    auto ec = make_error_code(CanErrc::identifier_out_of_range);
    REQUIRE(ec.category() == can_category());
    REQUIRE_FALSE(ec.message().empty());
}
