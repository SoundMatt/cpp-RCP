// fusa:test REQ-LINEP-001
// fusa:test REQ-LINEP-002
// fusa:test REQ-LINEP-003
// fusa:test REQ-LINEP-004
// fusa:test REQ-LINEP-005
// fusa:test REQ-LINEP-006
// fusa:test REQ-LINEP-007
// fusa:test REQ-LINEP-008
// fusa:test REQ-LINEP-009
// fusa:test REQ-LINEP-010
// fusa:test REQ-LINEP-011
// fusa:test REQ-LINEP-012
// fusa:test REQ-LINEP-013
// fusa:test REQ-LINEP-014
// fusa:test REQ-LINEP-015
// fusa:test REQ-LINEP-016
// fusa:test REQ-LINEP-017
// fusa:test REQ-LINEP-018
// fusa:test REQ-LINEP-019
// fusa:test REQ-LINEP-020
// fusa:test REQ-LINEP-021
// fusa:test REQ-LINEP-022
// fusa:test REQ-LINEP-024
// fusa:test REQ-LINEP-025
// fusa:test REQ-LINEP-027
// fusa:test REQ-LINEP-028
// fusa:test REQ-LINEP-029
// fusa:test REQ-LINEP-030
// fusa:test REQ-LINEP-031
// fusa:test REQ-LINEP-032
// fusa:test REQ-LINEP-033
// fusa:test REQ-LINEP-034
// fusa:test REQ-LINEP-035
// fusa:test REQ-LINEP-036
// fusa:test REQ-LINEP-037
// fusa:test REQ-LINEP-038
// fusa:test REQ-LINEP-039

// Tests for rcp/lin.hpp — the LIN commander endpoint type. Ported from
// c-RCP's tests/test_ep_lin.c (ROADMAP.md "Phase 17", cpp-RCP issue #129,
// Phase 3 "Per-endpoint modules"). The first block below (through the
// LinErrc category-sanity tests) is this file's pre-existing content,
// unchanged — LinEndpoint::transfer()/handle_request() kept their exact
// pre-rewrite signatures/behavior (see lin.hpp's own header comment). Every
// TEST_CASE after that is new, covering this pass's ported content: the
// evt[2:0] exact-match wrapper, the transmission-done trigger, functional
// config + lifecycle gating, the EP_func register block, and the real
// ACF-level wire codec.

#include <catch2/catch_test_macros.hpp>
#include <rcp/endpoint.hpp>
#include <rcp/lifecycle.hpp>
#include <rcp/lin.hpp>

using namespace rcp::lin;
using rcp::lifecycle::FieldKind;
using rcp::lifecycle::ServerState;
using rcp::lifecycle::WriterCtx;

// ── ep_type id ────────────────────────────────────────────────────────────────

TEST_CASE("LIN's ep_type id is 0x06", "[lin][REQ-LINEP-001]") {
    REQUIRE(rcp::endpoint::kEndpointTypeLin == 0x06);
}

// ── Raw-byte-pusher model: no frame-level concepts ───────────────────────────

TEST_CASE("LinEndpoint::transfer records raw bytes exactly, with no frame interpretation",
          "[lin][REQ-LINEP-002]") {
    LinEndpoint ep;
    // The caller's driver has already assembled break/sync/PID/data/checksum
    // into one opaque byte stream; this endpoint does not decompose it.
    std::vector<uint8_t> out_bytes{0x55, 0x21, 0xAA, 0xBB, 0x74};
    std::vector<uint8_t> in_bytes{0xAA, 0xBB, 0x74};

    auto ec = ep.transfer(out_bytes, in_bytes);
    REQUIRE_FALSE(ec);
    REQUIRE(ep.last_sent() == out_bytes);
    REQUIRE(ep.last_received() == in_bytes);
}

TEST_CASE("LinEndpoint::transfer treats an empty byte stream as valid (no minimum frame length)",
          "[lin][REQ-LINEP-002]") {
    LinEndpoint ep;
    auto ec = ep.transfer({}, {});
    REQUIRE_FALSE(ec);
    REQUIRE(ep.last_sent().empty());
    REQUIRE(ep.last_received().empty());
}

// ── No-response error path ───────────────────────────────────────────────────

TEST_CASE("LinEndpoint::transfer reports no_response and fires NoResponse when responded=false",
          "[lin][REQ-LINEP-003]") {
    LinEndpoint ep;
    ep.triggers().enable(lin_signal_id(LinSignal::TransferComplete));
    ep.triggers().enable(lin_signal_id(LinSignal::NoResponse));

    auto ec = ep.transfer({0x55, 0x21}, {}, /*responded=*/false);
    REQUIRE(ec == make_error_code(LinErrc::no_response));

    auto drained = ep.triggers().drain();
    REQUIRE(drained.size() == 1);
    REQUIRE(drained[0] == lin_signal_id(LinSignal::NoResponse));
}

TEST_CASE("LinEndpoint::transfer fires only TransferComplete on a normal responded transfer",
          "[lin][REQ-LINEP-003]") {
    LinEndpoint ep;
    ep.triggers().enable(lin_signal_id(LinSignal::TransferComplete));
    ep.triggers().enable(lin_signal_id(LinSignal::NoResponse));

    REQUIRE_FALSE(ep.transfer({0x55}, {0xAA}));

    auto drained = ep.triggers().drain();
    REQUIRE(drained.size() == 1);
    REQUIRE(drained[0] == lin_signal_id(LinSignal::TransferComplete));
}

// ── Table 33 Row 2 evt[2:0] validation (handle_request) ─────────────────────

TEST_CASE("LinEndpoint::handle_request delegates a Plain (evt[2:0]==000b) request to transfer()",
          "[lin][REQ-LINEP-005]") {
    LinEndpoint ep;
    ep.triggers().enable(lin_signal_id(LinSignal::TransferComplete));

    auto ec = ep.handle_request(/*evt_op=*/0, {0x55, 0x21}, {0xAA, 0xBB});
    REQUIRE_FALSE(ec);
    REQUIRE(ep.last_sent() == std::vector<uint8_t>{0x55, 0x21});
    REQUIRE(ep.last_received() == std::vector<uint8_t>{0xAA, 0xBB});
    REQUIRE(ep.triggers().drain() == std::vector<rcp::endpoint::TriggerRegistry::SignalId>{
                                          lin_signal_id(LinSignal::TransferComplete)});
}

TEST_CASE("LinEndpoint::handle_request rejects every reserved evt[2:0] value (001b-110b)",
          "[lin][REQ-LINEP-005]") {
    for (uint8_t evt_op = 1; evt_op <= 6; ++evt_op) {
        LinEndpoint ep;
        auto ec = ep.handle_request(evt_op, {0x55}, {0xAA});
        REQUIRE(ec == rcp::endpoint::make_error_code(rcp::endpoint::EndpointErrc::reserved_evt_row2));
        // A rejected reserved evt must not record anything as sent/received.
        REQUIRE(ep.last_sent().empty());
        REQUIRE(ep.last_received().empty());
    }
}

TEST_CASE("LinEndpoint::handle_request reports config_write_not_supported for evt[2:0]==111b "
          "without crashing or touching transfer state",
          "[lin][REQ-LINEP-006]") {
    LinEndpoint ep;
    auto ec = ep.handle_request(/*evt_op=*/7, {0x00, 0xAB}, {});
    REQUIRE(ec == make_error_code(LinErrc::config_write_not_supported));
    REQUIRE(ep.last_sent().empty());
    REQUIRE(ep.last_received().empty());
}

TEST_CASE("LinEndpoint::handle_request masks evt_op down to 3 bits before classifying",
          "[lin][REQ-LINEP-005]") {
    LinEndpoint ep;
    REQUIRE_FALSE(ep.handle_request(/*evt_op=*/0xF8, {0x55}, {0xAA})); // low 3 bits 000 -> Plain
    auto ec = ep.handle_request(/*evt_op=*/0xF9, {0x55}, {0xAA});      // low 3 bits 001 -> Reserved
    REQUIRE(ec == rcp::endpoint::make_error_code(rcp::endpoint::EndpointErrc::reserved_evt_row2));
}

// ── LinErrc category sanity ───────────────────────────────────────────────────

TEST_CASE("LinErrc reports a non-empty message in its own category", "[lin][REQ-LINEP-004]") {
    auto ec = make_error_code(LinErrc::no_response);
    REQUIRE(ec.category() == lin_category());
    REQUIRE_FALSE(ec.message().empty());
}

TEST_CASE("LinErrc::config_write_not_supported reports a non-empty message in its own category",
          "[lin][REQ-LINEP-006]") {
    auto ec = make_error_code(LinErrc::config_write_not_supported);
    REQUIRE(ec.category() == lin_category());
    REQUIRE_FALSE(ec.message().empty());
}

// ── evt[2:0] exact-match wrapper (response_matches) ──────────────────────────

TEST_CASE("response_matches is exact-match, length-capped to the shorter of the two buffers",
          "[lin][REQ-LINEP-005]") {
    std::vector<uint8_t> tx{0xAA, 0xBB};
    std::vector<uint8_t> rx{0xAA, 0xBB, 0xCC};
    REQUIRE(response_matches(tx, rx)); // rx's extra trailing byte is ignored

    std::vector<uint8_t> rx_short{0xAA};
    REQUIRE_FALSE(response_matches(tx, rx_short)); // rx shorter than tx never matches

    std::vector<uint8_t> rx_mismatch{0xAA, 0xCC};
    REQUIRE_FALSE(response_matches(tx, rx_mismatch));

    REQUIRE(response_matches({}, {})); // both empty -> trivially matches
}

// ── Transmission-done trigger ─────────────────────────────────────────────────

TEST_CASE("trigger_fires: TxDone requires both tx_done_event and trailing_time_expired",
          "[lin][REQ-LINEP-003]") {
    REQUIRE(trigger_fires(LinTrigger::TxDone, true, true));
    REQUIRE_FALSE(trigger_fires(LinTrigger::TxDone, true, false));
    REQUIRE_FALSE(trigger_fires(LinTrigger::TxDone, false, true));
    REQUIRE_FALSE(trigger_fires(LinTrigger::TxDone, false, false));
}

TEST_CASE("trigger_fires: None never fires", "[lin][REQ-LINEP-003]") {
    REQUIRE_FALSE(trigger_fires(LinTrigger::None, true, true));
}

// ── Functional config + lifecycle gating ─────────────────────────────────────

TEST_CASE("functional_cfg_writable follows FieldKind::FunctionalW's own gating rule",
          "[lin][REQ-LINEP-006]") {
    WriterCtx via_owning;
    via_owning.via_owning_stream = true;
    WriterCtx none;

    REQUIRE_FALSE(functional_cfg_writable(ServerState::HwUnconfigured, via_owning));
    REQUIRE(functional_cfg_writable(ServerState::HwConfigured, via_owning));
    REQUIRE_FALSE(functional_cfg_writable(ServerState::HwConfigured, none));
    REQUIRE(functional_cfg_writable(ServerState::RcpConfigured, via_owning));
}

TEST_CASE("set_clk_divider/set_trigger apply only when authorized, leaving cfg untouched otherwise",
          "[lin][REQ-LINEP-006]") {
    LinFunctionalConfig cfg;
    WriterCtx none;
    WriterCtx via_owning;
    via_owning.via_owning_stream = true;

    REQUIRE_FALSE(set_clk_divider(cfg, 42, ServerState::HwUnconfigured, none));
    REQUIRE(cfg.lin_clk_divider == 0);

    REQUIRE(set_clk_divider(cfg, 42, ServerState::HwConfigured, via_owning));
    REQUIRE(cfg.lin_clk_divider == 42);

    REQUIRE_FALSE(set_trigger(cfg, LinTrigger::TxDone, ServerState::HwUnconfigured, none));
    REQUIRE(cfg.trigger == LinTrigger::None);

    REQUIRE(set_trigger(cfg, LinTrigger::TxDone, ServerState::HwConfigured, via_owning));
    REQUIRE(cfg.trigger == LinTrigger::TxDone);
}

// ── EP_func register block (Table 55) ─────────────────────────────────────────

TEST_CASE("render_registers reports kEpFuncLen at offset 0 and zero at the reserved/base_clk "
          "offsets",
          "[lin][REQ-LINEP-006]") {
    LinFunctionalConfig cfg;
    cfg.ep_enable    = true;
    cfg.ep_status    = 0x1234;
    cfg.wire_clk_divider = 7;

    std::array<uint8_t, kEpFuncLen> out{};
    render_registers(cfg, out);

    REQUIRE(out[kRegEpLen] == static_cast<uint8_t>(kEpFuncLen));
    REQUIRE(out[kRegReserved01] == 0);
    REQUIRE(out[kRegBaseClk] == 0);
    REQUIRE(out[kRegBaseClk + 1] == 0);
    REQUIRE((out[kRegEpEnableClr] & 0x01) != 0); // ep_enable bit
    REQUIRE(((out[kRegEpStatus] << 8) | out[kRegEpStatus + 1]) == 0x1234);
    REQUIRE(out[kRegClkDivider] == 7);
}

TEST_CASE("apply_reconfig round-trips a write through render_registers", "[lin][REQ-LINEP-006]") {
    LinFunctionalConfig cfg;
    std::vector<uint8_t> payload{0x00, kRegClkDivider, 99}; // start_address=kRegClkDivider, data=[99]

    auto ec = apply_reconfig(cfg, payload.data(), payload.size());
    REQUIRE_FALSE(ec);
    REQUIRE(cfg.wire_clk_divider == 99);
}

TEST_CASE("apply_reconfig leaves cfg untouched and reports short_payload for a too-short payload",
          "[lin][REQ-LINEP-006]") {
    LinFunctionalConfig cfg;
    std::vector<uint8_t> payload{0x00, 0x00}; // address only, no data octet
    auto ec = apply_reconfig(cfg, payload.data(), payload.size());
    REQUIRE(ec == make_error_code(LinReconfigErrc::short_payload));
    REQUIRE(cfg.wire_clk_divider == 0);
}

TEST_CASE("apply_reconfig reports out_of_range when the addressed span exceeds kEpFuncLen",
          "[lin][REQ-LINEP-006]") {
    LinFunctionalConfig cfg;
    std::vector<uint8_t> payload{0x00, static_cast<uint8_t>(kEpFuncLen - 1), 0xAA, 0xBB}; // 2 data octets starting 1 before the end
    auto ec = apply_reconfig(cfg, payload.data(), payload.size());
    REQUIRE(ec == make_error_code(LinReconfigErrc::out_of_range));
}

TEST_CASE("apply_reconfig ignores writes to read-only offsets while still applying the rest of "
          "the span",
          "[lin][REQ-LINEP-006]") {
    LinFunctionalConfig cfg;
    // Address 0 (lin_ep_len, read-only) through address 2 (lin_ep_enable&clr, R/W).
    std::vector<uint8_t> payload{0x00, 0x00, 0xFF, 0xFF, 0x01};
    auto ec = apply_reconfig(cfg, payload.data(), payload.size());
    REQUIRE_FALSE(ec);
    REQUIRE(cfg.ep_enable); // bit 0 of enable&clr (offset 2) was applied
}

// ── ACF-level wire codec: command request ─────────────────────────────────────

TEST_CASE("encode_command_request/decode_command_request round-trip", "[lin][REQ-LINEP-002]") {
    std::vector<uint8_t> tx{0x55, 0x21, 0xAA};
    auto frame = encode_command_request(/*byte_bus_id=*/6, tx, /*transaction_num=*/9);
    REQUIRE_FALSE(frame.empty());

    std::vector<uint8_t> out_tx;
    uint8_t               out_txn = 0;
    auto ec = decode_command_request(frame.data(), frame.size(), /*expected_bus_id=*/6, out_tx, out_txn);
    REQUIRE_FALSE(ec);
    REQUIRE(out_tx == tx);
    REQUIRE(out_txn == 9);
}

TEST_CASE("decode_command_request reports wrong_bus/wrong_op/bad_evt/short_frame", "[lin][REQ-LINEP-002]") {
    std::vector<uint8_t> tx{0xAA};
    auto frame = encode_command_request(6, tx, 1);

    std::vector<uint8_t> out_tx;
    uint8_t               out_txn = 0;

    REQUIRE(decode_command_request(frame.data(), frame.size(), /*expected_bus_id=*/7, out_tx, out_txn) ==
            make_error_code(LinErrc::wrong_bus));

    std::vector<uint8_t> too_short{0x1C, 0x00}; // valid ACF_ABB acf_msg_type (0x0E) but shorter than the fixed header
    REQUIRE(decode_command_request(too_short.data(), too_short.size(), 6, out_tx, out_txn) ==
            make_error_code(LinErrc::short_frame));

    // A command request is always encoded read-direction (op=false, see
    // encode_command_request's own comment) — force op=true (write
    // direction, what encode_response's own frames never carry either) by
    // hand-editing byte 6's top bit, to exercise wrong_op explicitly.
    std::vector<uint8_t> forced_write = frame;
    forced_write[6] |= 0x80; // op bit
    REQUIRE(decode_command_request(forced_write.data(), forced_write.size(), 6, out_tx, out_txn) ==
            make_error_code(LinErrc::wrong_op));
}

// ── ACF-level wire codec: response ─────────────────────────────────────────────

TEST_CASE("encode_response/decode_response round-trip, untimed (ACF_ABB)", "[lin][REQ-LINEP-002]") {
    std::vector<uint8_t> rx{0xDE, 0xAD, 0xBE, 0xEF};
    auto frame = encode_response(/*byte_bus_id=*/6, rx, /*transaction_num=*/3, /*timed=*/false, 0);
    REQUIRE_FALSE(frame.empty());

    std::vector<uint8_t> out_rx;
    bool                  out_timed = true;
    uint64_t               out_ts   = 0;
    uint8_t                 out_txn  = 0;
    auto ec = decode_response(frame.data(), frame.size(), 6, out_rx, out_timed, out_ts, out_txn);
    REQUIRE_FALSE(ec);
    REQUIRE(out_rx == rx);
    REQUIRE_FALSE(out_timed);
    REQUIRE(out_ts == 0);
    REQUIRE(out_txn == 3);
}

TEST_CASE("encode_response/decode_response round-trip, timed (ACF_GBB)", "[lin][REQ-LINEP-002]") {
    std::vector<uint8_t> rx{0x01, 0x02};
    auto frame = encode_response(6, rx, 5, /*timed=*/true, /*timestamp=*/0x1122334455667788ull);

    std::vector<uint8_t> out_rx;
    bool                  out_timed = false;
    uint64_t               out_ts   = 0;
    uint8_t                 out_txn  = 0;
    auto ec = decode_response(frame.data(), frame.size(), 6, out_rx, out_timed, out_ts, out_txn);
    REQUIRE_FALSE(ec);
    REQUIRE(out_rx == rx);
    REQUIRE(out_timed);
    REQUIRE(out_ts == 0x1122334455667788ull);
    REQUIRE(out_txn == 5);
}

TEST_CASE("decode_response reports wrong_bus/short_frame", "[lin][REQ-LINEP-002]") {
    auto frame = encode_response(6, {0xAA}, 1, false, 0);

    std::vector<uint8_t> out_rx;
    bool                  out_timed = false;
    uint64_t               out_ts   = 0;
    uint8_t                 out_txn  = 0;
    REQUIRE(decode_response(frame.data(), frame.size(), 7, out_rx, out_timed, out_ts, out_txn) ==
            make_error_code(LinErrc::wrong_bus));

    std::vector<uint8_t> empty;
    REQUIRE(decode_response(empty.data(), empty.size(), 6, out_rx, out_timed, out_ts, out_txn) ==
            make_error_code(LinErrc::short_frame));
}

// ── Reconfig request encode ───────────────────────────────────────────────────

TEST_CASE("encode_reconfig_request builds a WRITE frame with evt[2:0]=111b", "[lin][REQ-LINEP-006]") {
    std::vector<uint8_t> data{0x2A};
    auto frame = encode_reconfig_request(/*byte_bus_id=*/6, /*start_address=*/kRegClkDivider, data,
                                          /*transaction_num=*/1);
    REQUIRE_FALSE(frame.empty());
    REQUIRE((frame[6] & 0x80) != 0); // op=write

    std::vector<uint8_t> out_tx;
    uint8_t               out_txn = 0;
    auto ec = decode_command_request(frame.data(), frame.size(), 6, out_tx, out_txn);
    // decode_command_request only accepts a read-direction (op=false)
    // request — a config-write frame (op=true, evt[2:0]=111b) is correctly
    // rejected as wrong_op before its evt[2:0] is even inspected, since the
    // op check runs first. This documents the two decoders' deliberate
    // separation of concerns: a config-write frame is never a valid
    // "plain command request" on any axis.
    REQUIRE(ec == make_error_code(LinErrc::wrong_op));
}

TEST_CASE("encode_reconfig_request returns empty for empty data", "[lin][REQ-LINEP-006]") {
    REQUIRE(encode_reconfig_request(6, 0, {}, 1).empty());
}

// ── Phase 6 batch 7: closing real test-coverage gaps found while re-deriving
// REQ-LINEP-* from c-RCP (id-collision audit, c-RCP-18-tracker issue #533's
// per-endpoint-type successor). Every function below was already genuinely
// implemented; only the specific branch/edge case exercised here was
// previously untested.

// ── REQ-LINEP-015: strerror-equivalent is non-empty and distinct per code,
// exhaustively over every defined LinErrc value ──────────────────────────────

TEST_CASE("LinErrc reports a distinct, non-empty message for every defined code, and a non-null "
          "message for an undefined one",
          "[lin][REQ-LINEP-015]") {
    const LinErrc codes[] = {
        LinErrc::no_response, LinErrc::config_write_not_supported, LinErrc::short_frame,
        LinErrc::bad_msg_type, LinErrc::wrong_bus, LinErrc::wrong_op, LinErrc::bad_evt,
    };
    std::vector<std::string> seen;
    for (auto c : codes) {
        auto ec = make_error_code(c);
        REQUIRE(ec.category() == lin_category());
        REQUIRE_FALSE(ec.message().empty());
        for (const auto& s : seen) REQUIRE(s != ec.message());
        seen.push_back(ec.message());
    }
    REQUIRE_FALSE(make_error_code(static_cast<LinErrc>(999)).message().empty());
}

// ── REQ-LINEP-027/031: decode_command_request rejects evt[2:0] != 000b and
// a non-ACF_ABB message — previously untested despite being named in the
// pre-existing test's own title ──────────────────────────────────────────────

TEST_CASE("decode_command_request rejects a nonzero evt[2:0] with bad_evt", "[lin][REQ-LINEP-027]") {
    rcp::acf::AcfMessageInfo hdr;
    hdr.byte_bus_id = 6;
    hdr.op          = false; // read direction, matching encode_command_request()
    hdr.evt_op      = 0x2;
    auto frame = rcp::acf::encode_acf_abb(hdr, {0xAA});

    std::vector<uint8_t> out_tx;
    uint8_t               out_txn = 0;
    REQUIRE(decode_command_request(frame.data(), frame.size(), 6, out_tx, out_txn) ==
            make_error_code(LinErrc::bad_evt));
}

TEST_CASE("decode_command_request rejects a non-ACF_ABB frame with bad_msg_type",
          "[lin][REQ-LINEP-031]") {
    rcp::acf::AcfMessageInfo hdr;
    hdr.byte_bus_id = 6;
    hdr.op          = false;
    auto frame = rcp::acf::encode_acf_gbb(hdr, 0, {0xAA});

    std::vector<uint8_t> out_tx;
    uint8_t               out_txn = 0;
    REQUIRE(decode_command_request(frame.data(), frame.size(), 6, out_tx, out_txn) ==
            make_error_code(LinErrc::bad_msg_type));
}

// ── REQ-LINEP-039: reconfig_strerror-equivalent never returns an empty
// message, including for an unrecognized code ────────────────────────────────

TEST_CASE("LinReconfigErrc reports a distinct, non-empty message per code", "[lin][REQ-LINEP-039]") {
    auto short_ec = make_error_code(LinReconfigErrc::short_payload);
    auto range_ec = make_error_code(LinReconfigErrc::out_of_range);
    REQUIRE(short_ec.category() == lin_reconfig_category());
    REQUIRE_FALSE(short_ec.message().empty());
    REQUIRE_FALSE(range_ec.message().empty());
    REQUIRE(short_ec.message() != range_ec.message());
    REQUIRE_FALSE(make_error_code(static_cast<LinReconfigErrc>(999)).message().empty());
}
