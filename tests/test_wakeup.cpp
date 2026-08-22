// fusa:test REQ-WAKEUP-001
// fusa:test REQ-WAKEUP-002
// fusa:test REQ-WAKEUP-003
// fusa:test REQ-WAKEUP-004
// fusa:test REQ-WAKEUP-005
// fusa:test REQ-WAKEUP-006
// fusa:test REQ-WAKEUP-007
// fusa:test REQ-WAKEUP-008
// fusa:test REQ-WAKEUP-009
// fusa:test REQ-WAKEUP-010
// fusa:test REQ-WAKEUP-011
// fusa:test REQ-WAKEUP-012
// fusa:test REQ-WAKEUP-013
// fusa:test REQ-WAKEUP-014
// fusa:test REQ-WAKEUP-015
// fusa:test REQ-WAKEUP-016
// fusa:test REQ-WAKEUP-017
// fusa:test REQ-WAKEUP-018
// fusa:test REQ-WAKEUP-019
// fusa:test REQ-WAKEUP-021
// fusa:test REQ-WAKEUP-022
// fusa:test REQ-WAKEUP-023
// fusa:test REQ-WAKEUP-024
// fusa:test REQ-WAKEUP-025
// fusa:test REQ-WAKEUP-027
// fusa:test REQ-WAKEUP-028
// fusa:test REQ-WAKEUP-029
// fusa:test REQ-WAKEUP-030
// fusa:test REQ-WAKEUP-031
// fusa:test REQ-WAKEUP-032
// fusa:test REQ-WAKEUP-033
// fusa:test REQ-WAKEUP-034
// fusa:test REQ-WAKEUP-035
// fusa:test REQ-WAKEUP-036
// fusa:test REQ-PWRMODE-023

// Tests for rcp/wakeup.hpp — the WakeUp endpoint type, re-derived from
// c-RCP's tests/test_ep_wakeup.c (ROADMAP.md Phase 17, cpp-RCP issue #129,
// Phase 3).

#include <catch2/catch_test_macros.hpp>
#include <rcp/acf.hpp>
#include <rcp/lifecycle.hpp>
#include <rcp/wakeup.hpp>

using namespace rcp::wakeup;

// ── Fixed opcodes ──────────────────────────────────────────────────────────────

TEST_CASE("SleepCMD and WakeUp opcodes are the fixed, distinct values", "[wakeup][REQ-WAKEUP-010]") {
    REQUIRE(kSleepCmdOpcode == 0xA5);
    REQUIRE(kWakeupOpcode == 0x5A);
    REQUIRE(kSleepCmdOpcode != kWakeupOpcode);
}

// ── Wake-source pin monitoring: LEVEL mode (REQ-WAKEUP-003/004) ──────────────

TEST_CASE("source_asserted applies one source's own polarity", "[wakeup][REQ-WAKEUP-003]") {
    WakeSourceCfg cfg;
    cfg.enabled     = true;
    cfg.active_high = true;
    REQUIRE(source_asserted(cfg, true));
    REQUIRE_FALSE(source_asserted(cfg, false));

    cfg.active_high = false;
    REQUIRE(source_asserted(cfg, false));
    REQUIRE_FALSE(source_asserted(cfg, true));
}

TEST_CASE("source_asserted is always false when the source is disabled", "[wakeup][REQ-WAKEUP-003]") {
    WakeSourceCfg cfg;
    cfg.enabled     = false;
    cfg.active_high = true;
    REQUIRE_FALSE(source_asserted(cfg, true));
}

TEST_CASE("any_source_asserted reports whether any configured source is asserted", "[wakeup][REQ-WAKEUP-004]") {
    std::array<WakeSourceCfg, kMaxWakeSources> sources{};
    sources[2].enabled     = true;
    sources[2].active_high = true;

    std::vector<bool> levels(kMaxWakeSources, false);
    REQUIRE_FALSE(any_source_asserted(sources, levels));

    levels[2] = true;
    REQUIRE(any_source_asserted(sources, levels));
}

TEST_CASE("any_source_asserted only consults the first min(levels, kMaxWakeSources) entries",
          "[wakeup][REQ-WAKEUP-004]") {
    std::array<WakeSourceCfg, kMaxWakeSources> sources{};
    sources[0].enabled     = true;
    sources[0].active_high = true;

    std::vector<bool> levels; // empty
    REQUIRE_FALSE(any_source_asserted(sources, levels));
}

// ── Edge-triggered wake-source detection (REQ-WAKEUP-022/032/033/034) ───────

TEST_CASE("source_edge_asserted delegates to source_asserted in LEVEL mode, state untouched",
          "[wakeup][REQ-WAKEUP-033]") {
    WakeSourceCfg cfg;
    cfg.enabled     = true;
    cfg.active_high = true;
    SourceEdgeState state;

    REQUIRE(source_edge_asserted(cfg, state, true));
    REQUIRE_FALSE(state.has_previous); // LEVEL mode never touches state
}

TEST_CASE("source_edge_asserted's first observation only seeds state, never fires",
          "[wakeup][REQ-WAKEUP-032][REQ-WAKEUP-033]") {
    WakeSourceCfg cfg;
    cfg.enabled                  = true;
    cfg.trigger_on_rising_edge   = true;
    SourceEdgeState state;

    REQUIRE_FALSE(source_edge_asserted(cfg, state, false));
    REQUIRE(state.has_previous);
    REQUIRE_FALSE(state.previous_level);
}

TEST_CASE("source_edge_asserted fires on a rising edge when trigger_on_rising_edge is set",
          "[wakeup][REQ-WAKEUP-033]") {
    WakeSourceCfg cfg;
    cfg.enabled                = true;
    cfg.trigger_on_rising_edge = true;
    SourceEdgeState state;

    REQUIRE_FALSE(source_edge_asserted(cfg, state, false)); // seed
    REQUIRE(source_edge_asserted(cfg, state, true));         // rising edge -> fires
    REQUIRE_FALSE(source_edge_asserted(cfg, state, true));   // level held -> no edge
    REQUIRE_FALSE(source_edge_asserted(cfg, state, false));  // falling edge, not configured -> no fire
}

TEST_CASE("source_edge_asserted fires on both edges when both trigger flags are set",
          "[wakeup][REQ-WAKEUP-033]") {
    WakeSourceCfg cfg;
    cfg.enabled                  = true;
    cfg.trigger_on_rising_edge   = true;
    cfg.trigger_on_falling_edge  = true;
    SourceEdgeState state;

    REQUIRE_FALSE(source_edge_asserted(cfg, state, false));
    REQUIRE(source_edge_asserted(cfg, state, true));  // rising
    REQUIRE(source_edge_asserted(cfg, state, false)); // falling
}

TEST_CASE("source_edge_asserted never fires while disabled, but still updates state",
          "[wakeup][REQ-WAKEUP-033]") {
    WakeSourceCfg cfg;
    cfg.enabled                = false;
    cfg.trigger_on_rising_edge = true;
    SourceEdgeState state;

    REQUIRE_FALSE(source_edge_asserted(cfg, state, false));
    REQUIRE_FALSE(source_edge_asserted(cfg, state, true));
    REQUIRE(state.previous_level); // still updated
}

TEST_CASE("any_source_edge_asserted updates every in-range source's state, never short-circuiting",
          "[wakeup][REQ-WAKEUP-034]") {
    std::array<WakeSourceCfg, kMaxWakeSources> sources{};
    sources[0].enabled = true; sources[0].trigger_on_rising_edge = true;
    sources[1].enabled = true; sources[1].trigger_on_rising_edge = true;

    std::array<SourceEdgeState, kMaxWakeSources> states{};
    std::vector<bool> levels(kMaxWakeSources, false);

    // Seed both.
    REQUIRE_FALSE(any_source_edge_asserted(sources, states, levels));

    // Source 0 rises now; source 1 stays low — both states must still be
    // updated (source 1 stays seeded at false, no missed transition later).
    levels[0] = true;
    REQUIRE(any_source_edge_asserted(sources, states, levels));
    REQUIRE(states[0].previous_level);
    REQUIRE_FALSE(states[1].previous_level);

    // Now source 1 rises too — it must still fire, proving it wasn't
    // silently skipped by short-circuiting on source 0's own earlier hit.
    levels[1] = true;
    REQUIRE(any_source_edge_asserted(sources, states, levels));
}

// ── wup_status latch (REQ-WAKEUP-005..008/021/027/028) ───────────────────────

TEST_CASE("WupStatus starts clear", "[wakeup][REQ-WAKEUP-005]") {
    WupStatus s;
    REQUIRE(s.is_clear());
    REQUIRE(s.mask() == 0);
}

TEST_CASE("WupStatus latch_source sets exactly one source's own bit", "[wakeup][REQ-WAKEUP-006]") {
    WupStatus s;
    s.latch_source(3);
    REQUIRE_FALSE(s.is_clear());
    REQUIRE(s.source_is_latched(3));
    REQUIRE_FALSE(s.source_is_latched(0));
    REQUIRE(s.mask() == (uint16_t{1} << 3));
}

TEST_CASE("WupStatus latch_source is a no-op for an out-of-range index", "[wakeup][REQ-WAKEUP-006]") {
    WupStatus s;
    s.latch_source(kMaxWakeSources); // one past the last valid index
    REQUIRE(s.is_clear());
}

TEST_CASE("WupStatus clear clears every latched bit at once", "[wakeup][REQ-WAKEUP-007]") {
    WupStatus s;
    s.latch_source(0);
    s.latch_source(5);
    REQUIRE_FALSE(s.is_clear());
    s.clear();
    REQUIRE(s.is_clear());
}

TEST_CASE("WupStatus clear_source clears one bit, leaving every other source's latch untouched",
          "[wakeup][REQ-WAKEUP-027]") {
    WupStatus s;
    s.latch_source(1);
    s.latch_source(2);
    s.clear_source(1);
    REQUIRE_FALSE(s.source_is_latched(1));
    REQUIRE(s.source_is_latched(2));
}

TEST_CASE("WupStatus source_is_latched returns false for an out-of-range index",
          "[wakeup][REQ-WAKEUP-028]") {
    WupStatus s;
    s.latch_source(0);
    REQUIRE_FALSE(s.source_is_latched(kMaxWakeSources));
}

// ── SleepCMD request/response (0xA5) ─────────────────────────────────────────

TEST_CASE("encode_sleepcmd_request/decode_sleepcmd_request round-trip", "[wakeup][REQ-WAKEUP-010][REQ-WAKEUP-011]") {
    auto frame = encode_sleepcmd_request(4, 7);
    uint8_t txn = 0;
    REQUIRE_FALSE(decode_sleepcmd_request(frame.data(), frame.size(), 4, txn));
    REQUIRE(txn == 7);
}

TEST_CASE("decode_sleepcmd_request rejects a wrong fixed opcode", "[wakeup][REQ-WAKEUP-011]") {
    rcp::acf::AcfMessageInfo info;
    info.byte_bus_id = 4;
    auto frame = rcp::acf::encode_acf_abb(info, {0x00});
    uint8_t txn = 0;
    REQUIRE(decode_sleepcmd_request(frame.data(), frame.size(), 4, txn) == make_error_code(WakeupErrc::bad_opcode));
}

TEST_CASE("decode_sleepcmd_request rejects a misaddressed frame", "[wakeup][REQ-WAKEUP-011]") {
    auto frame = encode_sleepcmd_request(4, 1);
    uint8_t txn = 0;
    REQUIRE(decode_sleepcmd_request(frame.data(), frame.size(), 9, txn) == make_error_code(WakeupErrc::wrong_bus));
}

TEST_CASE("encode_sleepcmd_response/decode_sleepcmd_response round-trip Ok", "[wakeup][REQ-WAKEUP-012][REQ-WAKEUP-013]") {
    auto frame = encode_sleepcmd_response(4, SleepCmdResult::Ok, 9);
    SleepCmdResult result{};
    uint8_t txn = 0;
    REQUIRE_FALSE(decode_sleepcmd_response(frame.data(), frame.size(), 4, result, txn));
    REQUIRE(result == SleepCmdResult::Ok);
    REQUIRE(txn == 9);
}

// REQ-WAKEUP-019: a refused entry is encoded as a genuine ACF Error Response
// carrying REQUEST_CANCELED, not this module's own positive-form payload.
TEST_CASE("encode_sleepcmd_response encodes Refused as a genuine REQUEST_CANCELED error response",
          "[wakeup][REQ-WAKEUP-019]") {
    auto frame = encode_sleepcmd_response(4, SleepCmdResult::Refused, 2);

    rcp::acf::AcfMessageInfo info;
    std::vector<uint8_t> payload;
    REQUIRE_FALSE(rcp::acf::decode_acf_abb(frame.data(), frame.size(), info, payload));
    REQUIRE(info.err);
    REQUIRE(info.rsp);
    REQUIRE(payload.size() == 1);
    REQUIRE(payload[0] == static_cast<uint8_t>(rcp::acf::WireErrorCode::RequestCanceled));
}

TEST_CASE("decode_sleepcmd_response recognizes the REQUEST_CANCELED error response as Refused",
          "[wakeup][REQ-WAKEUP-023][REQ-WAKEUP-025]") {
    auto frame = encode_sleepcmd_response(4, SleepCmdResult::Refused, 2);
    SleepCmdResult result{};
    uint8_t txn = 0;
    REQUIRE_FALSE(decode_sleepcmd_response(frame.data(), frame.size(), 4, result, txn));
    REQUIRE(result == SleepCmdResult::Refused);
    REQUIRE(txn == 2);
}

TEST_CASE("decode_sleepcmd_response rejects an error response carrying a different error code",
          "[wakeup][REQ-WAKEUP-025]") {
    auto frame = rcp::acf::build_error_response(4, 1, rcp::acf::WireErrorCode::UnsupportedCmd);
    SleepCmdResult result{};
    uint8_t txn = 0;
    REQUIRE(decode_sleepcmd_response(frame.data(), frame.size(), 4, result, txn) ==
            make_error_code(WakeupErrc::bad_opcode));
}

// ── WakeUp-message emission (REQ-WAKEUP-014/015/016) ─────────────────────────

TEST_CASE("encode_wakeup_message/decode_wakeup_message round-trip", "[wakeup][REQ-WAKEUP-014][REQ-WAKEUP-015]") {
    auto frame = encode_wakeup_message(4, 3);
    uint8_t txn = 0;
    REQUIRE_FALSE(decode_wakeup_message(frame.data(), frame.size(), 4, txn));
    REQUIRE(txn == 3);
}

TEST_CASE("decode_wakeup_message rejects a wrong fixed opcode", "[wakeup][REQ-WAKEUP-015]") {
    rcp::acf::AcfMessageInfo info;
    info.byte_bus_id = 4;
    auto frame = rcp::acf::encode_acf_abb(info, {0x00});
    uint8_t txn = 0;
    REQUIRE(decode_wakeup_message(frame.data(), frame.size(), 4, txn) == make_error_code(WakeupErrc::bad_opcode));
}

TEST_CASE("is_wakeup_echo recognizes a matching WakeUp echo", "[wakeup][REQ-WAKEUP-016]") {
    auto frame = encode_wakeup_message(4, 55);
    REQUIRE(is_wakeup_echo(frame.data(), frame.size(), 4, 55));
    REQUIRE_FALSE(is_wakeup_echo(frame.data(), frame.size(), 4, 56)); // wrong transaction number
    REQUIRE_FALSE(is_wakeup_echo(frame.data(), frame.size(), 9, 55)); // wrong bus
}

TEST_CASE("decode_wakeup_message tolerates the longer with-source payload shape",
          "[wakeup][REQ-WAKEUP-015][REQ-WAKEUP-017]") {
    auto frame = encode_wakeup_message_with_source(4, 1, WakeupSource::Io, 2);
    uint8_t txn = 0;
    REQUIRE_FALSE(decode_wakeup_message(frame.data(), frame.size(), 4, txn));
}

// ── WakeUp-with-source (REQ-WAKEUP-017/024) ───────────────────────────────────

TEST_CASE("encode_wakeup_message_with_source/decode round-trip for every source classification",
          "[wakeup][REQ-WAKEUP-017][REQ-WAKEUP-024]") {
    for (auto source : {WakeupSource::Unknown, WakeupSource::Io, WakeupSource::Wakepin, WakeupSource::Network}) {
        auto frame = encode_wakeup_message_with_source(4, 5, source, source == WakeupSource::Io ? 3 : kWakeupSourceIndexNa);
        uint8_t txn = 0;
        WakeupSource out_source{};
        uint8_t out_index = 0;
        REQUIRE_FALSE(decode_wakeup_message_with_source(frame.data(), frame.size(), 4, txn, out_source, out_index));
        REQUIRE(txn == 5);
        REQUIRE(out_source == source);
        REQUIRE(out_index == (source == WakeupSource::Io ? 3 : kWakeupSourceIndexNa));
    }
}

TEST_CASE("decode_wakeup_message_with_source rejects the plain 1-byte shape as short_frame",
          "[wakeup][REQ-WAKEUP-024]") {
    auto frame = encode_wakeup_message(4, 1);
    uint8_t txn = 0;
    WakeupSource out_source{};
    uint8_t out_index = 0;
    REQUIRE(decode_wakeup_message_with_source(frame.data(), frame.size(), 4, txn, out_source, out_index) ==
            make_error_code(WakeupErrc::short_frame));
}

TEST_CASE("decode_wakeup_message_with_source rejects an unrecognized source byte", "[wakeup][REQ-WAKEUP-024]") {
    rcp::acf::AcfMessageInfo info;
    info.byte_bus_id = 4;
    auto frame = rcp::acf::encode_acf_abb(info, {kWakeupOpcode, 0xEE, 0});
    uint8_t txn = 0;
    WakeupSource out_source{};
    uint8_t out_index = 0;
    REQUIRE(decode_wakeup_message_with_source(frame.data(), frame.size(), 4, txn, out_source, out_index) ==
            make_error_code(WakeupErrc::bad_opcode));
}

// ── The EP_func register block (Table 39/40) ──────────────────────────────────

TEST_CASE("render_registers/apply_reconfig round-trip through the register block",
          "[wakeup][REQ-WAKEUP-021][REQ-WAKEUP-022]") {
    WakeupFunctionalConfig cfg;
    cfg.ep_status = 0xBEEF;
    cfg.wup_status.latch_source(0);
    cfg.wup_status.latch_source(3);
    cfg.sources[0].enabled     = true;
    cfg.sources[0].active_high = true;
    cfg.sources[0].pin_number  = 12;
    cfg.sources[1].enabled                = true;
    cfg.sources[1].trigger_on_rising_edge = true;
    cfg.sources[1].pin_number             = 5;

    WakeupRegisterBlock block{};
    render_registers(cfg, block);
    REQUIRE(block[kWakeupRegEpLen] == kWakeupEpFuncLen);
    REQUIRE(block[kWakeupRegNrIoPinsMax] == kMaxWakeSources);

    std::vector<uint8_t> payload(2 + kWakeupEpFuncLen);
    rcp::avtp::detail::put_u16(payload.data(), 0);
    std::copy(block.begin(), block.end(), payload.begin() + 2);

    WakeupFunctionalConfig applied;
    REQUIRE_FALSE(apply_reconfig(applied, payload.data(), payload.size()));
    REQUIRE(applied.ep_status == 0xBEEF);
    REQUIRE(applied.sources[0].enabled);
    REQUIRE(applied.sources[0].active_high);
    REQUIRE(applied.sources[0].pin_number == 12);
    REQUIRE(applied.sources[1].enabled);
    REQUIRE(applied.sources[1].trigger_on_rising_edge);
    REQUIRE(applied.sources[1].pin_number == 5);
}

TEST_CASE("apply_reconfig applies wup_status's write-1-to-clear semantics bit-by-bit",
          "[wakeup][REQ-WAKEUP-029]") {
    WakeupFunctionalConfig cfg;
    cfg.wup_status.latch_source(0);
    cfg.wup_status.latch_source(1);
    cfg.wup_status.latch_source(2);

    // Write only bit 1 set -> clears source 1 only, leaves 0 and 2 latched.
    std::vector<uint8_t> payload(2 + 2);
    rcp::avtp::detail::put_u16(payload.data(), kWakeupRegWupStatus);
    rcp::avtp::detail::put_u16(&payload[2], uint16_t{1} << 1);

    REQUIRE_FALSE(apply_reconfig(cfg, payload.data(), payload.size()));
    REQUIRE(cfg.wup_status.source_is_latched(0));
    REQUIRE_FALSE(cfg.wup_status.source_is_latched(1));
    REQUIRE(cfg.wup_status.source_is_latched(2));
}

TEST_CASE("apply_reconfig ignores a write with no address+data", "[wakeup][REQ-WAKEUP-036]") {
    WakeupFunctionalConfig cfg;
    uint8_t payload[2] = {0, 0};
    REQUIRE(apply_reconfig(cfg, payload, sizeof(payload)) == make_error_code(WakeupErrc::reconfig_short));
}

TEST_CASE("apply_reconfig ignores a write extending past EP_LEN", "[wakeup][REQ-WAKEUP-036]") {
    WakeupFunctionalConfig cfg;
    cfg.ep_status = 0x1111;
    std::vector<uint8_t> payload(4, 0xFF);
    rcp::avtp::detail::put_u16(payload.data(), kWakeupEpFuncLen);
    REQUIRE(apply_reconfig(cfg, payload.data(), payload.size()) ==
            make_error_code(WakeupErrc::reconfig_out_of_range));
    REQUIRE(cfg.ep_status == 0x1111);
}

TEST_CASE("apply_reconfig leaves read-only registers (EP_LEN, NR_IO_PINS_MAX) unchanged",
          "[wakeup][REQ-WAKEUP-036]") {
    WakeupFunctionalConfig cfg;
    std::vector<uint8_t> payload(2 + 2, 0x00);
    rcp::avtp::detail::put_u16(payload.data(), kWakeupRegEpLen);
    REQUIRE_FALSE(apply_reconfig(cfg, payload.data(), payload.size()));

    WakeupRegisterBlock block{};
    render_registers(cfg, block);
    REQUIRE(block[kWakeupRegEpLen] == kWakeupEpFuncLen); // still the real length, not 0x00
}

TEST_CASE("apply_reconfig leaves an unrepresentable (reserved) IO_SRC value's flags unchanged, "
          "but still updates pin_number",
          "[wakeup][REQ-WAKEUP-035]") {
    WakeupFunctionalConfig cfg;
    cfg.sources[0].enabled     = true;
    cfg.sources[0].active_high = true;

    std::vector<uint8_t> payload(2 + 2);
    rcp::avtp::detail::put_u16(payload.data(), kWakeupRegSourceBase);
    const uint16_t reserved_reg = static_cast<uint16_t>((0x10u << 11) | 99u); // reserved IO_SRC=0x10
    rcp::avtp::detail::put_u16(&payload[2], reserved_reg);

    REQUIRE_FALSE(apply_reconfig(cfg, payload.data(), payload.size()));
    REQUIRE(cfg.sources[0].enabled);     // unchanged
    REQUIRE(cfg.sources[0].active_high); // unchanged
    REQUIRE(cfg.sources[0].pin_number == 99); // still updated
}

TEST_CASE("encode_reconfig_request encodes a well-formed configuration write", "[wakeup][REQ-WAKEUP-031]") {
    std::vector<uint8_t> data = {0xAB, 0xCD};
    auto frame = encode_reconfig_request(4, kWakeupRegEpStatus, data, 1);
    REQUIRE_FALSE(frame.empty());

    rcp::acf::AcfMessageInfo info;
    std::vector<uint8_t> payload;
    REQUIRE_FALSE(rcp::acf::decode_acf_abb(frame.data(), frame.size(), info, payload));
    REQUIRE(info.evt_op == 0x7);
    REQUIRE(info.op);
}

TEST_CASE("encode_reconfig_request returns empty for empty data", "[wakeup][REQ-WAKEUP-031]") {
    REQUIRE(encode_reconfig_request(4, 0, {}, 1).empty());
}

// ── Functional config / lifecycle authorization ───────────────────────────────

TEST_CASE("WakeupFunctionalConfig defaults to a fully cleared configuration", "[wakeup][REQ-WAKEUP-001]") {
    WakeupFunctionalConfig cfg;
    REQUIRE_FALSE(cfg.ep_enable);
    REQUIRE(cfg.wup_status.is_clear());
    REQUIRE(cfg.ep_status == 0);
    REQUIRE(cfg.repetition_time_us == 0);
    for (const auto& src : cfg.sources) {
        REQUIRE_FALSE(src.enabled);
    }
}

TEST_CASE("functional_cfg_writable delegates to the shared lifecycle field-authorization logic",
          "[wakeup][REQ-WAKEUP-002]") {
    rcp::lifecycle::WriterCtx writer;
    writer.via_root_client_ep0 = true;
    REQUIRE_FALSE(functional_cfg_writable(rcp::lifecycle::ServerState::HwUnconfigured, writer));
    REQUIRE(functional_cfg_writable(rcp::lifecycle::ServerState::RcpConfigured, writer));
}

TEST_CASE("sleepcmd_writable requires the root client via EP0", "[wakeup][REQ-WAKEUP-019]") {
    rcp::lifecycle::WriterCtx unauth;
    unauth.via_discovery_stream = true;
    REQUIRE_FALSE(sleepcmd_writable(unauth));

    rcp::lifecycle::WriterCtx auth;
    auth.via_root_client_ep0 = true;
    REQUIRE(sleepcmd_writable(auth));
}

// ── WakeupEndpoint ─────────────────────────────────────────────────────────────

TEST_CASE("WakeupEndpoint::handle_sleep_cmd transitions to asleep on the fixed opcode",
          "[wakeup][REQ-WAKEUP-010]") {
    WakeupEndpoint ep;
    REQUIRE_FALSE(ep.is_asleep());
    REQUIRE_FALSE(ep.handle_sleep_cmd(kSleepCmdOpcode));
    REQUIRE(ep.is_asleep());
}

TEST_CASE("WakeupEndpoint::handle_sleep_cmd rejects a non-SleepCMD byte without changing state",
          "[wakeup][REQ-WAKEUP-011]") {
    WakeupEndpoint ep;
    auto ec = ep.handle_sleep_cmd(0x5A); // the WakeUp opcode, not SleepCMD's
    REQUIRE(ec == make_error_code(WakeupErrc::bad_opcode));
    REQUIRE_FALSE(ep.is_asleep());
}

TEST_CASE("WakeupEndpoint::record_wake_source_event latches the source and wakes the endpoint",
          "[wakeup][REQ-WAKEUP-006]") {
    WakeupEndpoint ep;
    REQUIRE_FALSE(ep.handle_sleep_cmd(kSleepCmdOpcode));
    REQUIRE(ep.is_asleep());

    ep.record_wake_source_event(3);
    REQUIRE_FALSE(ep.is_asleep());
    REQUIRE(ep.functional_cfg().wup_status.source_is_latched(3));
}

TEST_CASE("A wake-source event arms the repeating WakeUp handshake until acknowledged",
          "[wakeup][REQ-WAKEUP-018]") {
    WakeupEndpoint ep;
    REQUIRE_FALSE(ep.wakeup_message_pending());

    ep.record_wake_source_event(5);
    REQUIRE(ep.wakeup_message_pending());
    REQUIRE(ep.wakeup_message_pending()); // repeated calls keep reporting pending

    ep.acknowledge_wakeup();
    REQUIRE_FALSE(ep.wakeup_message_pending());
}

TEST_CASE("Entering Sleep clears any handshake left pending from a prior wake cycle",
          "[wakeup][REQ-WAKEUP-018]") {
    WakeupEndpoint ep;
    ep.record_wake_source_event(2);
    REQUIRE(ep.wakeup_message_pending());

    REQUIRE_FALSE(ep.handle_sleep_cmd(kSleepCmdOpcode));
    REQUIRE_FALSE(ep.wakeup_message_pending());
}

// ── WakeupErrc category sanity ────────────────────────────────────────────────

TEST_CASE("WakeupErrc reports a non-empty message in its own category", "[wakeup][REQ-WAKEUP-009]") {
    auto ec = make_error_code(WakeupErrc::bad_opcode);
    REQUIRE(ec.category() == wakeup_category());
    REQUIRE_FALSE(ec.message().empty());
}
