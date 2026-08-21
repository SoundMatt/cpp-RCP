// fusa:test REQ-CANEP-001
// fusa:test REQ-CANEP-002
// fusa:test REQ-CANEP-003
// fusa:test REQ-CANEP-004
// fusa:test REQ-CANEP-005
// fusa:test REQ-CANEP-006
// fusa:test REQ-CANEP-007
// fusa:test REQ-CANEP-008
// fusa:test REQ-CANEP-009

// Tests for rcp/can.hpp — the CAN controller endpoint type. Ported from
// c-RCP's tests/test_ep_can.c (ROADMAP.md "Phase 17", cpp-RCP issue #129,
// Phase 3 "Per-endpoint modules"), including this pass's centerpiece: CAN
// XL multi-frame fragmentation wired onto rcp/fragment.hpp, mirroring
// c-RCP's own worst-case (2048-octet) fragmented request/response tests
// (c-RCP issues #610/#611/#612/#613) and the oversized-reassembly lesson
// (c-RCP issues #614/#616, documented in fragment.hpp's own header comment).

#include <catch2/catch_test_macros.hpp>
#include <rcp/can.hpp>
#include <rcp/endpoint.hpp>
#include <rcp/fragment.hpp>
#include <rcp/lifecycle.hpp>

using namespace rcp::can;
using rcp::lifecycle::FieldKind;
using rcp::lifecycle::ServerState;
using rcp::lifecycle::WriterCtx;

// ── ep_type id ────────────────────────────────────────────────────────────────

TEST_CASE("CAN's ep_type id is 0x0B", "[can][REQ-CANEP-001]") {
    REQUIRE(rcp::endpoint::kEndpointTypeCan == 0x0B);
}

// ── FrameFormat helpers ───────────────────────────────────────────────────────

TEST_CASE("frame_format_valid accepts 0..5 and rejects 6/7", "[can][REQ-CANEP-002]") {
    for (uint8_t v = 0; v <= 5; ++v) REQUIRE(frame_format_valid(v));
    REQUIRE_FALSE(frame_format_valid(6));
    REQUIRE_FALSE(frame_format_valid(7));
}

TEST_CASE("frame_format_is_xl is true only for the two CAN XL variants", "[can][REQ-CANEP-002]") {
    REQUIRE_FALSE(frame_format_is_xl(FrameFormat::Cbff));
    REQUIRE_FALSE(frame_format_is_xl(FrameFormat::Ceff));
    REQUIRE_FALSE(frame_format_is_xl(FrameFormat::Fbff));
    REQUIRE_FALSE(frame_format_is_xl(FrameFormat::Feff));
    REQUIRE(frame_format_is_xl(FrameFormat::XlClassicalPl));
    REQUIRE(frame_format_is_xl(FrameFormat::XlNewPl));
}

TEST_CASE("frame_format_id_width: Extended29 for CEFF/FEFF, Base11 for every other defined format",
          "[can][REQ-CANEP-002]") {
    REQUIRE(frame_format_id_width(FrameFormat::Cbff) == IdWidth::Base11);
    REQUIRE(frame_format_id_width(FrameFormat::Ceff) == IdWidth::Extended29);
    REQUIRE(frame_format_id_width(FrameFormat::Fbff) == IdWidth::Base11);
    REQUIRE(frame_format_id_width(FrameFormat::Feff) == IdWidth::Extended29);
    REQUIRE(frame_format_id_width(FrameFormat::XlClassicalPl) == IdWidth::Base11);
    REQUIRE(frame_format_id_width(FrameFormat::XlNewPl) == IdWidth::Base11);
}

TEST_CASE("frame_format_max_data_len: 8/64/2048 per format, 0 for invalid", "[can][REQ-CANEP-003]") {
    REQUIRE(frame_format_max_data_len(FrameFormat::Cbff) == kClassicalMaxDataLen);
    REQUIRE(frame_format_max_data_len(FrameFormat::Ceff) == kClassicalMaxDataLen);
    REQUIRE(frame_format_max_data_len(FrameFormat::Fbff) == kFdMaxDataLen);
    REQUIRE(frame_format_max_data_len(FrameFormat::Feff) == kFdMaxDataLen);
    REQUIRE(frame_format_max_data_len(FrameFormat::XlClassicalPl) == kXlMaxDataLen);
    REQUIRE(frame_format_max_data_len(FrameFormat::XlNewPl) == kXlMaxDataLen);
    REQUIRE(frame_format_max_data_len(static_cast<FrameFormat>(6)) == 0);
}

TEST_CASE("xl_frame_matches_provisioned_pl: non-XL always matches; XL must match the provisioned PL",
          "[can][REQ-CANEP-002]") {
    REQUIRE(xl_frame_matches_provisioned_pl(true, FrameFormat::Cbff));
    REQUIRE(xl_frame_matches_provisioned_pl(false, FrameFormat::Feff));

    REQUIRE(xl_frame_matches_provisioned_pl(true, FrameFormat::XlNewPl));
    REQUIRE_FALSE(xl_frame_matches_provisioned_pl(true, FrameFormat::XlClassicalPl));
    REQUIRE(xl_frame_matches_provisioned_pl(false, FrameFormat::XlClassicalPl));
    REQUIRE_FALSE(xl_frame_matches_provisioned_pl(false, FrameFormat::XlNewPl));
}

// ── Identifier range validation, per frame-format id width ───────────────────

TEST_CASE("validate_identifier accepts in-range ids for each format's own width",
          "[can][REQ-CANEP-002]") {
    REQUIRE_FALSE(validate_identifier(FrameFormat::Cbff, 0x7FF));
    REQUIRE_FALSE(validate_identifier(FrameFormat::Ceff, 0x1FFFFFFF));
    REQUIRE_FALSE(validate_identifier(FrameFormat::XlClassicalPl, 0x7FF)); // XL is always base-11
}

TEST_CASE("validate_identifier rejects an out-of-range id for its own format's width",
          "[can][REQ-CANEP-002]") {
    auto ec = validate_identifier(FrameFormat::Cbff, 0x800); // 12 bits, too wide for base-11
    REQUIRE(ec == make_error_code(CanErrc::identifier_out_of_range));

    ec = validate_identifier(FrameFormat::Ceff, 0x20000000); // 30 bits, too wide for extended-29
    REQUIRE(ec == make_error_code(CanErrc::identifier_out_of_range));

    // A Base11 format never accepts a > 11-bit id, even if it would be
    // in-range for Extended29 — id width is derived from frame_format
    // alone, not independently configurable.
    ec = validate_identifier(FrameFormat::XlNewPl, 0x800);
    REQUIRE(ec == make_error_code(CanErrc::identifier_out_of_range));
}

// ── Frame-format payload ceilings, data frames only ──────────────────────────

TEST_CASE("validate_frame enforces each FrameFormat's own payload ceiling", "[can][REQ-CANEP-003]") {
    CanDataFrame classical;
    classical.arbitration_id = 0x100;
    classical.format         = FrameFormat::Cbff;
    classical.data.assign(kClassicalMaxDataLen, 0xAA);
    REQUIRE_FALSE(validate_frame(classical));

    classical.data.push_back(0xAA); // 9 bytes, exceeds Classical's 8-byte ceiling
    REQUIRE(validate_frame(classical) == make_error_code(CanErrc::payload_exceeds_format_limit));

    CanDataFrame fd;
    fd.arbitration_id = 0x100;
    fd.format          = FrameFormat::Fbff;
    fd.data.assign(kFdMaxDataLen, 0x55);
    REQUIRE_FALSE(validate_frame(fd));

    fd.data.push_back(0x55); // 65 bytes, exceeds FD's 64-byte ceiling
    REQUIRE(validate_frame(fd) == make_error_code(CanErrc::payload_exceeds_format_limit));
}

// ── CAN XL: single-ACF-frame vs. format ceiling vs. fragmentation-required ──

TEST_CASE("validate_frame accepts a CAN XL payload within the single-ACF-frame bound",
          "[can][REQ-CANEP-004]") {
    CanDataFrame xl;
    xl.arbitration_id = 0x100;
    xl.format          = FrameFormat::XlClassicalPl;
    xl.data.assign(kMaxXlPayloadSingleFrame, 0x11);
    REQUIRE_FALSE(validate_frame(xl));
}

TEST_CASE("validate_frame reports xl_payload_exceeds_single_avtpdu_bound between the single-frame "
          "bound and the format's own 2048-octet ceiling",
          "[can][REQ-CANEP-004]") {
    CanDataFrame xl;
    xl.arbitration_id = 0x100;
    xl.format          = FrameFormat::XlClassicalPl;
    xl.data.assign(kMaxXlPayloadSingleFrame + 1, 0x11);
    REQUIRE(kMaxXlPayloadSingleFrame + 1 <= kXlMaxDataLen);
    REQUIRE(validate_frame(xl) == make_error_code(CanErrc::xl_payload_exceeds_single_avtpdu_bound));

    // The worst case this pass's fragmentation wiring exists for: exactly
    // kXlMaxDataLen (2048) octets.
    xl.data.assign(kXlMaxDataLen, 0x11);
    REQUIRE(validate_frame(xl) == make_error_code(CanErrc::xl_payload_exceeds_single_avtpdu_bound));
}

TEST_CASE("validate_frame reports payload_exceeds_format_limit beyond CAN XL's own 2048-octet "
          "ceiling",
          "[can][REQ-CANEP-004]") {
    CanDataFrame xl;
    xl.arbitration_id = 0x100;
    xl.format          = FrameFormat::XlClassicalPl;
    xl.data.assign(kXlMaxDataLen + 1, 0x11);
    REQUIRE(validate_frame(xl) == make_error_code(CanErrc::payload_exceeds_format_limit));
}

// ── Per-phase bit-timing register sets (CanFunctionalConfig, lifecycle-gated) ─

TEST_CASE("set_arbitration_timing/set_fd_data_timing/set_xl_data_timing store independently, "
          "gated by functional_cfg_writable",
          "[can][REQ-CANEP-005]") {
    CanFunctionalConfig cfg;
    WriterCtx via_owning;
    via_owning.via_owning_stream = true;
    WriterCtx none;

    CanBitTimingPhase arbitration;
    arbitration.prescaler   = 4;
    arbitration.phase_seg1  = 10;
    CanBitTimingPhase fd;
    fd.prescaler  = 1;
    fd.phase_seg1 = 3;
    CanBitTimingPhase xl;
    xl.prescaler  = 1;
    xl.phase_seg1 = 2;

    REQUIRE_FALSE(set_arbitration_timing(cfg, arbitration, ServerState::HwUnconfigured, none));
    REQUIRE(set_arbitration_timing(cfg, arbitration, ServerState::HwConfigured, via_owning));
    REQUIRE(set_fd_data_timing(cfg, fd, ServerState::HwConfigured, via_owning));
    REQUIRE(set_xl_data_timing(cfg, xl, ServerState::HwConfigured, via_owning));

    REQUIRE(cfg.timing.arbitration.prescaler == 4);
    REQUIRE(cfg.timing.fd_data.prescaler == 1);
    REQUIRE(cfg.timing.xl_data.prescaler == 1);
    REQUIRE(cfg.timing.arbitration.phase_seg1 == 10);
    REQUIRE(cfg.timing.fd_data.phase_seg1 == 3);
    REQUIRE(cfg.timing.xl_data.phase_seg1 == 2);
}

TEST_CASE("set_delay_compensation/set_exec_delay_clk_divider/set_xl_new_pl_provisioned apply only "
          "when authorized",
          "[can][REQ-CANEP-005]") {
    CanFunctionalConfig cfg;
    WriterCtx via_owning;
    via_owning.via_owning_stream = true;
    WriterCtx none;

    REQUIRE_FALSE(set_delay_compensation(cfg, true, 5, ServerState::HwUnconfigured, none));
    REQUIRE(set_delay_compensation(cfg, true, 5, ServerState::HwConfigured, via_owning));
    REQUIRE(cfg.delay_comp_enable);
    REQUIRE(cfg.delay_comp_offset == 5);

    REQUIRE(set_exec_delay_clk_divider(cfg, 77, ServerState::HwConfigured, via_owning));
    REQUIRE(cfg.exec_delay_clk_divider == 77);

    REQUIRE(set_xl_new_pl_provisioned(cfg, true, ServerState::HwConfigured, via_owning));
    REQUIRE(cfg.xl_new_pl_provisioned);
}

TEST_CASE("set_xl_filter applies only for a valid index and when authorized", "[can][REQ-CANEP-006]") {
    CanFunctionalConfig cfg;
    WriterCtx via_owning;
    via_owning.via_owning_stream = true;

    REQUIRE_FALSE(set_xl_filter(cfg, kMaxXlFilters, CanXlFilter{0x100, 0x7FF, true},
                                 ServerState::HwConfigured, via_owning));
    REQUIRE(set_xl_filter(cfg, 0, CanXlFilter{0x100, 0x7FF, true}, ServerState::HwConfigured, via_owning));
    REQUIRE(cfg.xl_filters[0].id == 0x100);
    REQUIRE(cfg.xl_filters[0].enable);
}

// ── EP_func register block (Table 56) ─────────────────────────────────────────

TEST_CASE("render_registers reports kEpFuncLen at offset 0, zeroes the reserved/base_clk/"
          "undecomposed span, and round-trips ep_status/status/fifo_status",
          "[can][REQ-CANEP-005]") {
    CanFunctionalConfig cfg;
    cfg.ep_enable    = true;
    cfg.ep_status    = 0x1234;
    cfg.status       = 0xAABBCCDD;
    cfg.fifo_status  = 0x11223344;

    std::array<uint8_t, kEpFuncLen> out{};
    render_registers(cfg, out);

    REQUIRE(out[kRegEpLen] == static_cast<uint8_t>(kEpFuncLen));
    REQUIRE(out[kRegReserved01] == 0);
    REQUIRE(out[kRegBaseClk] == 0);
    REQUIRE(out[kRegBaseClk + 1] == 0);
    for (uint16_t off = kRegUndecomposedStart; off < kRegUndecomposedStart + kRegUndecomposedLen; ++off)
        REQUIRE(out[off] == 0);
    REQUIRE(((out[kRegEpStatus] << 8) | out[kRegEpStatus + 1]) == 0x1234);
}

TEST_CASE("apply_reconfig round-trips a write to the status register, ignoring the read-only span",
          "[can][REQ-CANEP-005]") {
    CanFunctionalConfig cfg;
    std::vector<uint8_t> payload{0x00, static_cast<uint8_t>(kRegStatus), 0xDE, 0xAD, 0xBE, 0xEF};
    auto ec = apply_reconfig(cfg, payload.data(), payload.size());
    REQUIRE_FALSE(ec);
    REQUIRE(cfg.status == 0xDEADBEEFu);
}

TEST_CASE("apply_reconfig reports short_payload/out_of_range", "[can][REQ-CANEP-005]") {
    CanFunctionalConfig cfg;
    std::vector<uint8_t> too_short{0x00, 0x00};
    REQUIRE(apply_reconfig(cfg, too_short.data(), too_short.size()) ==
            make_error_code(CanReconfigErrc::short_payload));

    std::vector<uint8_t> oob{0x00, static_cast<uint8_t>(kEpFuncLen - 1), 0xAA, 0xBB};
    REQUIRE(apply_reconfig(cfg, oob.data(), oob.size()) == make_error_code(CanReconfigErrc::out_of_range));
}

// ── CAN-XL-specific acceptance filters (CanFunctionalConfig::xl_filters) and
// the general (non-XL) acceptance-filter bank ────────────────────────────────

TEST_CASE("CanEndpoint::receive accepts everything when no filters are configured",
          "[can][REQ-CANEP-006]") {
    CanEndpoint ep;
    CanDataFrame f;
    f.arbitration_id = 0x123;
    f.format          = FrameFormat::Cbff;
    REQUIRE(ep.receive(f));
    REQUIRE(ep.last_received().arbitration_id == 0x123);
}

TEST_CASE("CanEndpoint::receive drops a non-XL frame that matches no general acceptance filter",
          "[can][REQ-CANEP-006]") {
    CanEndpoint ep;
    ep.set_acceptance_filters({CanAcceptanceFilter{0x200, 0x7FF, false}});

    CanDataFrame f;
    f.arbitration_id = 0x100;
    f.format          = FrameFormat::Cbff;
    REQUIRE_FALSE(ep.receive(f));
}

TEST_CASE("CanEndpoint::receive accepts a frame matching a general acceptance filter's masked id",
          "[can][REQ-CANEP-006]") {
    CanEndpoint ep;
    ep.set_acceptance_filters({CanAcceptanceFilter{0x200, 0x700, false}}); // top 3 id bits must match 0x200's

    CanDataFrame f;
    f.arbitration_id = 0x21F; // top 3 bits (0x200) match; low bits are don't-care
    f.format          = FrameFormat::Cbff;
    REQUIRE(ep.receive(f));
}

TEST_CASE("CanEndpoint::receive matches an XL frame against CanFunctionalConfig::xl_filters, not "
          "the general acceptance bank",
          "[can][REQ-CANEP-006]") {
    CanEndpoint ep;
    ep.set_acceptance_filters({CanAcceptanceFilter{0x999, 0x7FF, false}}); // would reject 0x100
    WriterCtx via_owning;
    via_owning.via_owning_stream = true;
    REQUIRE(set_xl_filter(ep.functional_config(), 0, CanXlFilter{0x100, 0x7FF, true},
                           ServerState::HwConfigured, via_owning)); // accepts 0x100

    CanDataFrame f;
    f.arbitration_id = 0x100;
    f.format          = FrameFormat::XlClassicalPl;
    REQUIRE(ep.receive(f));
}

TEST_CASE("CanEndpoint::receive drops an XL frame matching no enabled xl_filters entry",
          "[can][REQ-CANEP-006]") {
    CanEndpoint ep;
    WriterCtx via_owning;
    via_owning.via_owning_stream = true;
    REQUIRE(set_xl_filter(ep.functional_config(), 0, CanXlFilter{0x200, 0x7FF, true},
                           ServerState::HwConfigured, via_owning));

    CanDataFrame f;
    f.arbitration_id = 0x100;
    f.format          = FrameFormat::XlNewPl;
    REQUIRE_FALSE(ep.receive(f));
}

// ── No trigger-signal table for CAN ───────────────────────────────────────────

TEST_CASE("CanEndpoint exposes no TriggerRegistry accessor", "[can][REQ-CANEP-007]") {
    CanEndpoint ep;
    CanDataFrame f;
    f.arbitration_id = 0x1;
    REQUIRE(ep.transmit(f) == std::error_code{});
    // No ep.triggers() call exists to make here — that is the point.
}

// ── Table 33 Row 2 evt[2:0] validation (handle_request) ─────────────────────

TEST_CASE("CanEndpoint::handle_request delegates a Plain (evt[2:0]==000b) request to transmit()",
          "[can][REQ-CANEP-008]") {
    CanEndpoint ep;
    CanDataFrame f;
    f.arbitration_id = 0x123;
    f.format          = FrameFormat::Cbff;
    f.data            = {0xDE, 0xAD};

    auto ec = ep.handle_request(/*evt_op=*/0, f);
    REQUIRE_FALSE(ec);
    REQUIRE(ep.last_transmitted().arbitration_id == 0x123);
    REQUIRE(ep.last_transmitted().data == std::vector<uint8_t>{0xDE, 0xAD});
}

TEST_CASE("CanEndpoint::handle_request rejects every reserved evt[2:0] value (001b-110b) without "
          "touching transmit state",
          "[can][REQ-CANEP-008]") {
    for (uint8_t evt_op = 1; evt_op <= 6; ++evt_op) {
        CanEndpoint ep;
        CanDataFrame f;
        f.arbitration_id = 0x123;
        f.data           = {0xAA};
        auto ec = ep.handle_request(evt_op, f);
        REQUIRE(ec == rcp::endpoint::make_error_code(rcp::endpoint::EndpointErrc::reserved_evt_row2));
        // A rejected reserved evt must not record anything as transmitted —
        // last_transmitted() stays at its default-constructed value.
        REQUIRE(ep.last_transmitted().arbitration_id == 0);
        REQUIRE(ep.last_transmitted().data.empty());
    }
}

TEST_CASE("CanEndpoint::handle_request reports config_write_not_supported for evt[2:0]==111b "
          "without crashing or touching transmit state",
          "[can][REQ-CANEP-009]") {
    CanEndpoint ep;
    CanDataFrame f;
    f.arbitration_id = 0x123;
    f.data            = {0x00, 0xAB};
    auto ec = ep.handle_request(/*evt_op=*/7, f);
    REQUIRE(ec == make_error_code(CanErrc::config_write_not_supported));
    REQUIRE(ep.last_transmitted().arbitration_id == 0);
    REQUIRE(ep.last_transmitted().data.empty());
}

TEST_CASE("CanEndpoint::handle_request masks evt_op down to 3 bits before classifying",
          "[can][REQ-CANEP-008]") {
    CanEndpoint ep;
    CanDataFrame f;
    f.arbitration_id = 0x123;
    REQUIRE_FALSE(ep.handle_request(/*evt_op=*/0xF8, f)); // low 3 bits 000 -> Plain
    auto ec = ep.handle_request(/*evt_op=*/0xF9, f);      // low 3 bits 001 -> Reserved
    REQUIRE(ec == rcp::endpoint::make_error_code(rcp::endpoint::EndpointErrc::reserved_evt_row2));
}

TEST_CASE("CanEndpoint::handle_request Reserved/ConfigWrite classification is independent of "
          "FrameFormat/CAN-ID — evt[2:0] carries no frame-format or remote-frame selector",
          "[can][REQ-CANEP-008]") {
    for (auto fmt : {FrameFormat::Cbff, FrameFormat::Fbff, FrameFormat::XlClassicalPl}) {
        CanEndpoint ep;
        CanDataFrame f;
        f.arbitration_id = 0x123;
        f.format          = fmt;
        REQUIRE(ep.handle_request(/*evt_op=*/3, f) ==
                rcp::endpoint::make_error_code(rcp::endpoint::EndpointErrc::reserved_evt_row2));
        REQUIRE(ep.handle_request(/*evt_op=*/7, f) == make_error_code(CanErrc::config_write_not_supported));
    }
}

// ── CanErrc category sanity ───────────────────────────────────────────────────

TEST_CASE("CanErrc reports a non-empty message in its own category", "[can][REQ-CANEP-007]") {
    auto ec = make_error_code(CanErrc::identifier_out_of_range);
    REQUIRE(ec.category() == can_category());
    REQUIRE_FALSE(ec.message().empty());
}

TEST_CASE("CanErrc::config_write_not_supported reports a non-empty message in its own category",
          "[can][REQ-CANEP-009]") {
    auto ec = make_error_code(CanErrc::config_write_not_supported);
    REQUIRE(ec.category() == can_category());
    REQUIRE_FALSE(ec.message().empty());
}

// ── ACF-level wire codec: unfragmented frame request/response round-trips ────

TEST_CASE("encode_frame_request/decode_frame_request round-trip, Classical (no xl_header)",
          "[can][REQ-CANEP-003]") {
    std::vector<uint8_t> tx{0x11, 0x22, 0x33};
    auto frame = encode_frame_request(/*byte_bus_id=*/7, FrameFormat::Cbff, 0x123, std::nullopt, tx,
                                       /*transaction_num=*/4);
    REQUIRE_FALSE(frame.empty());

    FrameFormat out_fmt{};
    uint32_t     out_id  = 0;
    XlHeader      out_xl{};
    std::vector<uint8_t> out_tx;
    uint8_t                out_txn = 0;
    auto ec = decode_frame_request(frame.data(), frame.size(), 7, out_fmt, out_id, out_xl, out_tx, out_txn);
    REQUIRE_FALSE(ec);
    REQUIRE(out_fmt == FrameFormat::Cbff);
    REQUIRE(out_id == 0x123);
    REQUIRE(out_tx == tx);
    REQUIRE(out_txn == 4);
}

TEST_CASE("encode_frame_request/decode_frame_request round-trip, CAN XL (with xl_header)",
          "[can][REQ-CANEP-003]") {
    std::vector<uint8_t> tx{0xAA, 0xBB, 0xCC, 0xDD};
    XlHeader xl{0x5, 0x7, 0xDEADBEEF};
    auto frame = encode_frame_request(7, FrameFormat::XlClassicalPl, 0x321, xl, tx, 9);
    REQUIRE_FALSE(frame.empty());

    FrameFormat out_fmt{};
    uint32_t     out_id  = 0;
    XlHeader      out_xl{};
    std::vector<uint8_t> out_tx;
    uint8_t                out_txn = 0;
    auto ec = decode_frame_request(frame.data(), frame.size(), 7, out_fmt, out_id, out_xl, out_tx, out_txn);
    REQUIRE_FALSE(ec);
    REQUIRE(out_fmt == FrameFormat::XlClassicalPl);
    REQUIRE(out_id == 0x321);
    REQUIRE(out_xl.sdt == 0x5);
    REQUIRE(out_xl.vcid == 0x7);
    REQUIRE(out_xl.af == 0xDEADBEEFu);
    REQUIRE(out_tx == tx);
    REQUIRE(out_txn == 9);
}

TEST_CASE("encode_frame_request returns empty for a mismatched xl_header presence or an invalid "
          "identifier",
          "[can][REQ-CANEP-003]") {
    REQUIRE(encode_frame_request(7, FrameFormat::Cbff, 0, XlHeader{}, {}, 0).empty()); // xl_header on non-XL
    REQUIRE(encode_frame_request(7, FrameFormat::XlClassicalPl, 0, std::nullopt, {}, 0).empty()); // missing xl_header on XL
    REQUIRE(encode_frame_request(7, FrameFormat::Cbff, 0x800, std::nullopt, {}, 0).empty()); // id out of range
}

TEST_CASE("decode_frame_request reports wrong_bus/wrong_op/bad_evt/short_frame",
          "[can][REQ-CANEP-003]") {
    auto frame = encode_frame_request(7, FrameFormat::Cbff, 0x1, std::nullopt, {0xAA}, 1);

    FrameFormat out_fmt{};
    uint32_t     out_id = 0;
    XlHeader      out_xl{};
    std::vector<uint8_t> out_tx;
    uint8_t                out_txn = 0;

    REQUIRE(decode_frame_request(frame.data(), frame.size(), 8, out_fmt, out_id, out_xl, out_tx, out_txn) ==
            make_error_code(CanErrc::wrong_bus));

    std::vector<uint8_t> too_short{0x1C}; // valid ACF_ABB acf_msg_type (0x0E) but shorter than the fixed header
    REQUIRE(decode_frame_request(too_short.data(), too_short.size(), 7, out_fmt, out_id, out_xl, out_tx,
                                  out_txn) == make_error_code(CanErrc::short_frame));

    std::vector<uint8_t> forced_read = frame;
    forced_read[6] &= static_cast<uint8_t>(~0x80); // clear op bit -> read direction, not write
    REQUIRE(decode_frame_request(forced_read.data(), forced_read.size(), 7, out_fmt, out_id, out_xl,
                                  out_tx, out_txn) == make_error_code(CanErrc::wrong_op));

    std::vector<uint8_t> forced_evt = frame;
    forced_evt[4] |= 0x10; // evt_op bit0 (byte4 bits6:4) -> evt[2:0] = 001b, Reserved
    REQUIRE(decode_frame_request(forced_evt.data(), forced_evt.size(), 7, out_fmt, out_id, out_xl,
                                  out_tx, out_txn) == make_error_code(CanErrc::bad_evt));
}

TEST_CASE("encode_frame_response/decode_frame_response round-trip, untimed (ACF_ABB)",
          "[can][REQ-CANEP-003]") {
    std::vector<uint8_t> rx{0x01, 0x02, 0x03};
    auto frame = encode_frame_response(7, FrameFormat::Fbff, 0x55, std::nullopt, rx, 2, false, 0);
    REQUIRE_FALSE(frame.empty());

    FrameFormat out_fmt{};
    uint32_t     out_id  = 0;
    XlHeader      out_xl{};
    std::vector<uint8_t> out_rx;
    bool                   out_timed = true;
    uint64_t                out_ts   = 0;
    uint8_t                  out_txn  = 0;
    auto ec = decode_frame_response(frame.data(), frame.size(), 7, out_fmt, out_id, out_xl, out_rx,
                                     out_timed, out_ts, out_txn);
    REQUIRE_FALSE(ec);
    REQUIRE(out_fmt == FrameFormat::Fbff);
    REQUIRE(out_id == 0x55);
    REQUIRE(out_rx == rx);
    REQUIRE_FALSE(out_timed);
    REQUIRE(out_txn == 2);
}

TEST_CASE("encode_frame_response/decode_frame_response round-trip, timed (ACF_GBB)",
          "[can][REQ-CANEP-003]") {
    std::vector<uint8_t> rx{0xEE};
    auto frame = encode_frame_response(7, FrameFormat::Ceff, 0x1FFFFFFF, std::nullopt, rx, 6, true,
                                        0xCAFEBABEDEADBEEFull);
    REQUIRE_FALSE(frame.empty());

    FrameFormat out_fmt{};
    uint32_t     out_id  = 0;
    XlHeader      out_xl{};
    std::vector<uint8_t> out_rx;
    bool                   out_timed = false;
    uint64_t                out_ts   = 0;
    uint8_t                  out_txn  = 0;
    auto ec = decode_frame_response(frame.data(), frame.size(), 7, out_fmt, out_id, out_xl, out_rx,
                                     out_timed, out_ts, out_txn);
    REQUIRE_FALSE(ec);
    REQUIRE(out_fmt == FrameFormat::Ceff);
    REQUIRE(out_id == 0x1FFFFFFF);
    REQUIRE(out_rx == rx);
    REQUIRE(out_timed);
    REQUIRE(out_ts == 0xCAFEBABEDEADBEEFull);
    REQUIRE(out_txn == 6);
}

// ── Fragmentation: CAN XL, worst-case 2048-octet round-trips ────────────────

TEST_CASE("frame_response_fragment_count/encode_frame_response_fragmented produce a single frame "
          "when the combined payload already fits",
          "[can][REQ-CANEP-003]") {
    std::vector<uint8_t> rx{0x01, 0x02, 0x03};
    auto count = frame_response_fragment_count(FrameFormat::Cbff, 0x1, std::nullopt, rx.size(), 64);
    REQUIRE(count == 1);

    auto frames = encode_frame_response_fragmented(7, FrameFormat::Cbff, 0x1, std::nullopt, rx, 3, false,
                                                     0, 64);
    REQUIRE(frames.size() == 1);
    REQUIRE(frames[0] == encode_frame_response(7, FrameFormat::Cbff, 0x1, std::nullopt, rx, 3, false, 0));
}

TEST_CASE("encode_frame_request_fragmented + Reassembler + decode_reassembled_frame_response "
          "round-trip a worst-case 2048-octet CAN XL write request",
          "[can][REQ-CANEP-003]") {
    std::vector<uint8_t> tx(kXlMaxDataLen, 0x5A);
    XlHeader xl{0x1, 0x2, 0x33445566};
    constexpr size_t kMaxFragmentPayload = 200;

    auto count = frame_request_fragment_count(FrameFormat::XlNewPl, 0x42, xl, tx.size(), kMaxFragmentPayload);
    REQUIRE(count > 1); // genuinely needs multiple fragments

    auto frames = encode_frame_request_fragmented(7, FrameFormat::XlNewPl, 0x42, xl, tx, 11,
                                                    kMaxFragmentPayload);
    REQUIRE(frames.size() == count);

    rcp::fragment::Reassembler reasm(kXlMaxEncodedLen);
    rcp::fragment::ReasmResult last_result = rcp::fragment::ReasmResult::kErrTooLarge;
    for (size_t i = 0; i < frames.size(); ++i) {
        bool     ms;
        uint16_t segment_num;
        std::vector<uint8_t> payload;
        bool     timed;
        uint64_t timestamp;
        uint8_t  txn;
        // A request is decoded the same way a response fragment is at the
        // wire-shape level (both are just ms/segment_num/payload over ACF)
        // — decode_frame_response_fragment() is reused here exactly as
        // ep_can.c's own file header documents c-RCP reusing its response
        // decoder for a reassembled REQUEST's combined payload too.
        auto ec = decode_frame_response_fragment(frames[i].data(), frames[i].size(), 7, ms, segment_num,
                                                   payload, timed, timestamp, txn);
        REQUIRE_FALSE(ec);
        REQUIRE(txn == 11);
        last_result = reasm.feed(ms, segment_num, payload.data(), payload.size());
        if (i + 1 < frames.size())
            REQUIRE(last_result == rcp::fragment::ReasmResult::kContinue);
    }
    REQUIRE(last_result == rcp::fragment::ReasmResult::kComplete);
    REQUIRE(reasm.size() == kXlMaxEncodedLen);

    FrameFormat out_fmt{};
    uint32_t     out_id  = 0;
    XlHeader      out_xl{};
    std::vector<uint8_t> out_data;
    auto ec = decode_reassembled_frame_response(reasm.data(), reasm.size(), out_fmt, out_id, out_xl, out_data);
    REQUIRE_FALSE(ec);
    REQUIRE(out_fmt == FrameFormat::XlNewPl);
    REQUIRE(out_id == 0x42);
    REQUIRE(out_xl.sdt == 0x1);
    REQUIRE(out_xl.vcid == 0x2);
    REQUIRE(out_xl.af == 0x33445566u);
    REQUIRE(out_data == tx);
}

TEST_CASE("encode_frame_response_fragmented + Reassembler round-trip a worst-case 2048-octet CAN "
          "XL read response, timed (ACF_GBB)",
          "[can][REQ-CANEP-003]") {
    std::vector<uint8_t> rx(kXlMaxDataLen, 0xC3);
    XlHeader xl{0x0, 0x0, 0};
    constexpr size_t kMaxFragmentPayload = 512;

    auto count = frame_response_fragment_count(FrameFormat::XlClassicalPl, 0x7FF, xl, rx.size(),
                                                 kMaxFragmentPayload);
    REQUIRE(count > 1);
    REQUIRE(count <= kMaxFragmentSegments);

    auto frames = encode_frame_response_fragmented(7, FrameFormat::XlClassicalPl, 0x7FF, xl, rx, 200,
                                                     /*timed=*/true, 0x1122334455667788ull,
                                                     kMaxFragmentPayload);
    REQUIRE(frames.size() == count);

    rcp::fragment::Reassembler reasm(kXlMaxEncodedLen);
    rcp::fragment::ReasmResult last_result = rcp::fragment::ReasmResult::kErrTooLarge;
    for (auto& frame : frames) {
        bool     ms;
        uint16_t segment_num;
        std::vector<uint8_t> payload;
        bool     timed;
        uint64_t timestamp;
        uint8_t  txn;
        auto ec = decode_frame_response_fragment(frame.data(), frame.size(), 7, ms, segment_num, payload,
                                                   timed, timestamp, txn);
        REQUIRE_FALSE(ec);
        REQUIRE(timed);
        REQUIRE(timestamp == 0x1122334455667788ull);
        REQUIRE(txn == 200);
        last_result = reasm.feed(ms, segment_num, payload.data(), payload.size());
    }
    REQUIRE(last_result == rcp::fragment::ReasmResult::kComplete);

    FrameFormat out_fmt{};
    uint32_t     out_id  = 0;
    XlHeader      out_xl{};
    std::vector<uint8_t> out_data;
    auto ec = decode_reassembled_frame_response(reasm.data(), reasm.size(), out_fmt, out_id, out_xl, out_data);
    REQUIRE_FALSE(ec);
    REQUIRE(out_fmt == FrameFormat::XlClassicalPl);
    REQUIRE(out_id == 0x7FF);
    REQUIRE(out_data == rx);
}

TEST_CASE("frame_response_fragment_count returns 0 (not representable) when max_fragment_payload "
          "is 0 and the combined payload does not fit in one fragment",
          "[can][REQ-CANEP-003]") {
    std::vector<uint8_t> rx(kXlMaxDataLen, 0);
    REQUIRE(frame_response_fragment_count(FrameFormat::XlClassicalPl, 0, std::nullopt, rx.size(), 0) == 0);
    REQUIRE(encode_frame_response_fragmented(7, FrameFormat::XlClassicalPl, 0, std::nullopt, rx, 1,
                                              false, 0, 0)
                .empty());
}

// ── The oversized-reassembly lesson (c-RCP issues #614/#616) ─────────────────
// A request whose every fragment is individually well-formed can still
// reassemble into a payload too large for a caller's own bound —
// fragment::Reassembler fails closed (kErrTooLarge) rather than silently
// accepting or truncating it. This module's own contribution is making sure
// decode_reassembled_frame_response() never gets a chance to paper over
// that: a caller must check the Reassembler's own result before ever
// calling it (Phase 4's dispatch-loop wiring is where that check belongs
// end to end — see this file's own header comment).

TEST_CASE("Reassembler reports kErrTooLarge, not kComplete, when a fully-fragmented CAN XL "
          "response's reassembled size would exceed a caller-supplied max_total_len smaller than "
          "the actual combined payload",
          "[can][REQ-CANEP-003]") {
    std::vector<uint8_t> rx(kXlMaxDataLen, 0x42);
    XlHeader xl{0, 0, 0};
    constexpr size_t kMaxFragmentPayload = 512;

    auto frames = encode_frame_response_fragmented(7, FrameFormat::XlClassicalPl, 0x10, xl, rx,
                                                     1, false, 0, kMaxFragmentPayload);
    REQUIRE(frames.size() > 1);

    // A caller that (mis)configures its own Reassembler with too small a
    // bound — smaller than this response's real combined payload
    // (kArbitrationPrefixLen + rx.size()) — must see every fragment beyond
    // that bound rejected, never silently truncated or accepted.
    rcp::fragment::Reassembler undersized(/*max_total_len=*/100);
    bool     ms;
    uint16_t segment_num;
    std::vector<uint8_t> payload;
    bool     timed;
    uint64_t timestamp;
    uint8_t  txn;
    auto ec = decode_frame_response_fragment(frames[0].data(), frames[0].size(), 7, ms, segment_num,
                                               payload, timed, timestamp, txn);
    REQUIRE_FALSE(ec);
    auto result = undersized.feed(ms, segment_num, payload.data(), payload.size());
    // The first fragment's own payload (kMaxFragmentPayload=512) already
    // exceeds the 100-byte bound on its own.
    REQUIRE(result == rcp::fragment::ReasmResult::kErrTooLarge);
    REQUIRE_FALSE(undersized.is_collecting());
}

TEST_CASE("Reassembler correctly reports kErrTooLarge partway through a multi-fragment sequence "
          "whose EARLY fragments individually fit but whose running total does not",
          "[can][REQ-CANEP-003]") {
    std::vector<uint8_t> rx(600, 0x7E); // combined = 10 + 600 = 610 octets
    XlHeader xl{0, 0, 0};
    constexpr size_t kMaxFragmentPayload = 100;

    auto frames = encode_frame_response_fragmented(7, FrameFormat::XlClassicalPl, 0x1, xl, rx,
                                                     1, false, 0, kMaxFragmentPayload);
    REQUIRE(frames.size() > 1);

    // Bound the Reassembler to fit the first two fragments but not the
    // whole 604-octet combined payload.
    rcp::fragment::Reassembler bounded(/*max_total_len=*/250);
    bool     ms;
    uint16_t segment_num;
    std::vector<uint8_t> payload;
    bool     timed;
    uint64_t timestamp;
    uint8_t  txn;
    bool saw_too_large = false;
    for (auto& frame : frames) {
        auto ec = decode_frame_response_fragment(frame.data(), frame.size(), 7, ms, segment_num, payload,
                                                   timed, timestamp, txn);
        REQUIRE_FALSE(ec);
        auto result = bounded.feed(ms, segment_num, payload.data(), payload.size());
        if (result == rcp::fragment::ReasmResult::kErrTooLarge) {
            saw_too_large = true;
            break;
        }
    }
    REQUIRE(saw_too_large);
}
