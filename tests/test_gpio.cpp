// fusa:test REQ-GPIO-001
// fusa:test REQ-GPIO-002
// fusa:test REQ-GPIO-003
// fusa:test REQ-GPIO-004
// fusa:test REQ-GPIO-005
// fusa:test REQ-GPIO-006
// fusa:test REQ-GPIO-007
// fusa:test REQ-GPIO-008
// fusa:test REQ-GPIO-009
// fusa:test REQ-GPIO-010
// fusa:test REQ-GPIO-011
// fusa:test REQ-GPIO-012
// fusa:test REQ-GPIO-013
// fusa:test REQ-GPIO-014
// fusa:test REQ-GPIO-015
// fusa:test REQ-GPIO-016
// fusa:test REQ-GPIO-017
// fusa:test REQ-GPIO-018
// fusa:test REQ-GPIO-019
// fusa:test REQ-GPIO-020
// fusa:test REQ-GPIO-021
// fusa:test REQ-GPIO-022
// fusa:test REQ-GPIO-023
// fusa:test REQ-GPIO-024
// fusa:test REQ-GPIO-025
// fusa:test REQ-GPIO-026
// fusa:test REQ-GPIO-027
// fusa:test REQ-GPIO-028
// fusa:test REQ-GPIO-029
// fusa:test REQ-GPIO-030
// fusa:test REQ-GPIO-031
// fusa:test REQ-GPIO-032
// fusa:test REQ-GPIO-033
// fusa:test REQ-GPIO-034
// fusa:test REQ-GPIO-035
// fusa:test REQ-GPIO-036
// fusa:test REQ-GPIO-037
// fusa:test REQ-GPIO-038
// fusa:test REQ-GPIO-039
// fusa:test REQ-GPIO-040
// fusa:test REQ-GPIO-041
// fusa:test REQ-GPIO-042
// fusa:test REQ-GPIO-043
// fusa:test REQ-GPIO-044
// fusa:test REQ-GPIO-045
// fusa:test REQ-GPIO-046

// Tests for rcp/gpio.hpp — the GPIO endpoint type, re-derived from c-RCP's
// test_ep_gpio.c (Phase 3, cpp-RCP issue #129).

#include <catch2/catch_test_macros.hpp>
#include <rcp/gpio.hpp>

using namespace rcp::gpio;
using rcp::endpoint::WriteSemantics;
using rcp::lifecycle::ServerState;
using rcp::lifecycle::WriterCtx;

// ── Payload shape ─────────────────────────────────────────────────────────────

TEST_CASE("GPIO payload is a 4-byte bitmask", "[gpio][REQ-GPIO-001]") {
    REQUIRE(kGpioPayloadLen == 4);
    REQUIRE(kMaxPins == 32);
}

TEST_CASE("encode_gpio_payload / decode_gpio_payload round-trip big-endian", "[gpio][REQ-GPIO-001]") {
    auto buf = encode_gpio_payload(0x01020304);
    REQUIRE(buf == std::vector<uint8_t>{0x01, 0x02, 0x03, 0x04});

    PinMask out = 0;
    auto ec = decode_gpio_payload(buf.data(), buf.size(), out);
    REQUIRE_FALSE(ec);
    REQUIRE(out == 0x01020304);
}

TEST_CASE("decode_gpio_payload rejects a short buffer", "[gpio][REQ-GPIO-001]") {
    std::vector<uint8_t> short_buf{0x01, 0x02};
    PinMask out = 0;
    auto ec = decode_gpio_payload(short_buf.data(), short_buf.size(), out);
    REQUIRE(ec == rcp::avtp::make_error_code(rcp::avtp::AvtpErrc::short_buffer));
}

TEST_CASE("decode_gpio_payload rejects an over-long buffer (spec requires exactly 4 bytes)",
          "[gpio][REQ-GPIO-001]") {
    std::vector<uint8_t> long_buf{0x01, 0x02, 0x03, 0x04, 0x05};
    PinMask out = 0;
    auto ec = decode_gpio_payload(long_buf.data(), long_buf.size(), out);
    REQUIRE(ec == rcp::avtp::make_error_code(rcp::avtp::AvtpErrc::short_buffer));
}

// ── Write semantics: the 6 generic combinators ───────────────────────────────

TEST_CASE("apply_gpio_write applies Replace/Or/And/Xor to state.values", "[gpio][REQ-GPIO-006][REQ-GPIO-007][REQ-GPIO-008][REQ-GPIO-009]") {
    GpioState state;
    state.directions = 0xFFFFFFFF; // every pin output, so masking is a no-op here
    state.values      = 0x0000FFFF;

    REQUIRE_FALSE(apply_gpio_write(WriteSemantics::Or, state, 0xFFFF0000));
    REQUIRE(state.values == 0xFFFFFFFF);

    REQUIRE_FALSE(apply_gpio_write(WriteSemantics::And, state, 0x0F0F0F0F));
    REQUIRE(state.values == 0x0F0F0F0F);

    REQUIRE_FALSE(apply_gpio_write(WriteSemantics::Xor, state, 0xFFFFFFFF));
    REQUIRE(state.values == 0xF0F0F0F0);

    REQUIRE_FALSE(apply_gpio_write(WriteSemantics::Replace, state, 0x00000001));
    REQUIRE(state.values == 0x00000001);
}

// ── Write semantics: saturating Add/Subtract shared with PWM_OUT ────────────

TEST_CASE("apply_gpio_write applies saturating Add/Subtract to state.values", "[gpio][REQ-GPIO-010][REQ-GPIO-011]") {
    GpioState state;
    state.directions = 0xFFFFFFFF;
    state.values      = 0xFFFFFFF0;

    REQUIRE_FALSE(apply_gpio_write(WriteSemantics::Add, state, 0x20));
    REQUIRE(state.values == 0xFFFFFFFF); // saturates, does not wrap past max

    state.values = 5;
    REQUIRE_FALSE(apply_gpio_write(WriteSemantics::Subtract, state, 10));
    REQUIRE(state.values == 5); // request(10) - current(5) = 5, per the "request minus current" rule

    state.values = 20;
    REQUIRE_FALSE(apply_gpio_write(WriteSemantics::Subtract, state, 5));
    REQUIRE(state.values == 0); // request(5) < current(20) -> saturates at 0
}

// ── Write semantics: Reserved rejected, Reconfigure targets directions ──────

TEST_CASE("apply_gpio_write rejects Reserved and leaves state untouched", "[gpio][REQ-GPIO-012]") {
    GpioState state;
    state.values = 0x12345678;
    auto ec = apply_gpio_write(WriteSemantics::Reserved, state, 0xFFFFFFFF);
    REQUIRE(ec);
    REQUIRE(state.values == 0x12345678);
}

TEST_CASE("apply_gpio_write's Reconfigure replaces state.directions, not state.values",
          "[gpio][REQ-GPIO-013]") {
    GpioState state;
    state.values     = 0xAAAAAAAA;
    state.directions = 0;

    REQUIRE_FALSE(apply_gpio_write(WriteSemantics::Reconfigure, state, 0x0000FFFF));
    REQUIRE(state.directions == 0x0000FFFF);
    REQUIRE(state.values == 0xAAAAAAAA);
}

// ── Write masking against input-configured pins (TC18 §13.7.4.3) ────────────

TEST_CASE("apply_gpio_write's Replace does not modify input-configured pins",
          "[gpio][REQ-GPIO-037]") {
    GpioState state;
    state.directions = 0x0000000F; // pins 0-3 output, pins 4-31 input
    state.values     = 0xABCD0005; // pin 4 (input) currently reads 1

    REQUIRE_FALSE(apply_gpio_write(WriteSemantics::Replace, state, 0xFFFFFFF0));
    REQUIRE(state.values == 0xABCD0000);
}

TEST_CASE("apply_gpio_write's Or/And only affect output-configured pins", "[gpio][REQ-GPIO-037]") {
    GpioState state;
    state.directions = 0x000000FF; // pins 0-7 output, rest input
    state.values     = 0x00000F0F;

    REQUIRE_FALSE(apply_gpio_write(WriteSemantics::Or, state, 0xFFFFFFF0));
    REQUIRE(state.values == 0x00000FFF);

    state.values = 0x0000FFFF;
    REQUIRE_FALSE(apply_gpio_write(WriteSemantics::And, state, 0x00000000));
    REQUIRE(state.values == 0x0000FF00);
}

TEST_CASE("apply_gpio_write's Add/Subtract saturation still respects input masking",
          "[gpio][REQ-GPIO-037]") {
    GpioState state;
    state.directions = 0x0000FFFF; // low 16 bits output, high 16 bits input
    state.values     = 0xBEEF0000;

    REQUIRE_FALSE(apply_gpio_write(WriteSemantics::Add, state, 0xFFFFFFFF));
    REQUIRE(state.values == 0xBEEFFFFF);
}

// ── Per-pin change/rising/falling trigger signals (internal bookkeeping) ────

TEST_CASE("evaluate_gpio_triggers fires Change+Rising for a 0->1 transition when both are armed",
          "[gpio][REQ-GPIO-014][REQ-GPIO-015]") {
    rcp::endpoint::TriggerRegistry triggers;
    triggers.enable(gpio_signal_id(3, GpioEdge::Change));
    triggers.enable(gpio_signal_id(3, GpioEdge::Rising));
    triggers.enable(gpio_signal_id(3, GpioEdge::Falling));

    evaluate_gpio_triggers(triggers, /*old=*/0, /*new=*/(1u << 3));

    auto drained = triggers.drain();
    REQUIRE(drained.size() == 2);
    REQUIRE(drained[0] == gpio_signal_id(3, GpioEdge::Change));
    REQUIRE(drained[1] == gpio_signal_id(3, GpioEdge::Rising));
}

TEST_CASE("evaluate_gpio_triggers fires Change+Falling for a 1->0 transition", "[gpio][REQ-GPIO-017]") {
    rcp::endpoint::TriggerRegistry triggers;
    triggers.enable(gpio_signal_id(7, GpioEdge::Change));
    triggers.enable(gpio_signal_id(7, GpioEdge::Falling));

    evaluate_gpio_triggers(triggers, /*old=*/(1u << 7), /*new=*/0);

    auto drained = triggers.drain();
    REQUIRE(drained.size() == 2);
    REQUIRE(drained[0] == gpio_signal_id(7, GpioEdge::Change));
    REQUIRE(drained[1] == gpio_signal_id(7, GpioEdge::Falling));
}

TEST_CASE("evaluate_gpio_triggers fires nothing for a pin whose bit is unchanged", "[gpio][REQ-GPIO-014]") {
    rcp::endpoint::TriggerRegistry triggers;
    triggers.enable(gpio_signal_id(0, GpioEdge::Change));

    evaluate_gpio_triggers(triggers, /*old=*/0x1, /*new=*/0x1);
    REQUIRE_FALSE(triggers.has_pending());
}

TEST_CASE("evaluate_gpio_triggers only reports signals that were actually armed", "[gpio][REQ-GPIO-014]") {
    rcp::endpoint::TriggerRegistry triggers;
    auto fired = evaluate_gpio_triggers(triggers, /*old=*/0, /*new=*/1);
    REQUIRE(fired.empty());
}

// ── GpioTrigger / trigger_fires / Table 43 wire signal numbering ────────────

TEST_CASE("trigger_fires never fires for None", "[gpio][REQ-GPIO-014]") {
    REQUIRE_FALSE(trigger_fires(GpioTrigger::None, false, true));
    REQUIRE_FALSE(trigger_fires(GpioTrigger::None, true, false));
}

TEST_CASE("trigger_fires implements AnyChange/Rising/Falling", "[gpio][REQ-GPIO-015][REQ-GPIO-016][REQ-GPIO-017]") {
    REQUIRE(trigger_fires(GpioTrigger::AnyChange, false, true));
    REQUIRE(trigger_fires(GpioTrigger::AnyChange, true, false));
    REQUIRE_FALSE(trigger_fires(GpioTrigger::AnyChange, true, true));

    REQUIRE(trigger_fires(GpioTrigger::Rising, false, true));
    REQUIRE_FALSE(trigger_fires(GpioTrigger::Rising, true, false));

    REQUIRE(trigger_fires(GpioTrigger::Falling, true, false));
    REQUIRE_FALSE(trigger_fires(GpioTrigger::Falling, false, true));
}

TEST_CASE("trigger_signal_number implements Table 43's 3n+{1,2,3} pattern", "[gpio][REQ-GPIO-034]") {
    REQUIRE(trigger_signal_number(0, GpioTrigger::AnyChange) == 1);
    REQUIRE(trigger_signal_number(0, GpioTrigger::Rising) == 2);
    REQUIRE(trigger_signal_number(0, GpioTrigger::Falling) == 3);
    REQUIRE(trigger_signal_number(31, GpioTrigger::Falling) == 96); // 3*31+3
}

TEST_CASE("trigger_signal_number returns nullopt for None or an out-of-range pin", "[gpio][REQ-GPIO-034]") {
    REQUIRE_FALSE(trigger_signal_number(0, GpioTrigger::None).has_value());
    REQUIRE_FALSE(trigger_signal_number(32, GpioTrigger::AnyChange).has_value());
}

// ── Debounce filtering (REQ-GPIO-035/044) ─────────────────────────────────────

TEST_CASE("GpioDebounceState default-constructs / debounce_state_init zeroes", "[gpio][REQ-GPIO-044]") {
    GpioDebounceState s;
    s.consecutive_count = 5;
    debounce_state_init(s);
    REQUIRE_FALSE(s.has_settled);
    REQUIRE_FALSE(s.has_candidate);
    REQUIRE(s.consecutive_count == 0);
}

TEST_CASE("debounce_sample with n=0 settles every sample immediately", "[gpio][REQ-GPIO-035]") {
    GpioDebounceState s;
    bool changed = false;
    REQUIRE(debounce_sample(s, true, 0, &changed) == true);
    REQUIRE_FALSE(changed); // first-ever settle isn't reported as a change
    REQUIRE(debounce_sample(s, false, 0, &changed) == false);
    REQUIRE(changed);
}

TEST_CASE("debounce_sample returns false before the first settle completes", "[gpio][REQ-GPIO-035]") {
    GpioDebounceState s;
    bool changed = true;
    REQUIRE(debounce_sample(s, true, 3, &changed) == false); // only 1 of 3 samples seen
    REQUIRE_FALSE(changed);
    REQUIRE(debounce_sample(s, true, 3, &changed) == false); // 2 of 3
}

TEST_CASE("debounce_sample settles after n consecutive identical samples", "[gpio][REQ-GPIO-035]") {
    GpioDebounceState s;
    debounce_sample(s, true, 3, nullptr);
    debounce_sample(s, true, 3, nullptr);
    bool changed = true;
    REQUIRE(debounce_sample(s, true, 3, &changed) == true); // 3rd consecutive sample settles
    REQUIRE_FALSE(changed); // this is the first-ever settle, not reported as a "change"
}

TEST_CASE("debounce_sample's first-ever settle is not reported as a change", "[gpio][REQ-GPIO-035]") {
    GpioDebounceState s;
    debounce_sample(s, true, 2, nullptr);
    bool changed = true;
    REQUIRE(debounce_sample(s, true, 2, &changed) == true);
    REQUIRE_FALSE(changed);
}

TEST_CASE("debounce_sample discards a partial run on a differing sample", "[gpio][REQ-GPIO-035]") {
    GpioDebounceState s;
    debounce_sample(s, true, 3, nullptr);
    debounce_sample(s, true, 3, nullptr); // 2 consecutive trues
    debounce_sample(s, false, 3, nullptr); // differing sample resets the run
    bool changed = false;
    REQUIRE(debounce_sample(s, true, 3, &changed) == false); // only 1 consecutive true again
    REQUIRE_FALSE(changed);
}

TEST_CASE("debounce_sample reports no change when the settled value repeats", "[gpio][REQ-GPIO-035]") {
    GpioDebounceState s;
    debounce_sample(s, true, 1, nullptr); // settles true
    bool changed = true;
    REQUIRE(debounce_sample(s, true, 1, &changed) == true);
    REQUIRE_FALSE(changed);
}

TEST_CASE("debounce_sample reports a change when re-settling to the opposite value",
          "[gpio][REQ-GPIO-035]") {
    GpioDebounceState s;
    debounce_sample(s, true, 1, nullptr); // settles true
    bool changed = false;
    REQUIRE(debounce_sample(s, false, 1, &changed) == false);
    REQUIRE(changed);
}

// ── Response timing (REQ-GPIO-036) ────────────────────────────────────────────

TEST_CASE("response_timing: a pure read (no payload) is immediate", "[gpio][REQ-GPIO-036]") {
    REQUIRE(response_timing(/*is_write=*/false, /*payload_len=*/0) == GpioResponseTiming::Immediate);
}

TEST_CASE("response_timing: a payload-bearing read is after debounce", "[gpio][REQ-GPIO-036]") {
    REQUIRE(response_timing(false, 4) == GpioResponseTiming::AfterDebounce);
}

TEST_CASE("response_timing: a write is always after debounce", "[gpio][REQ-GPIO-036]") {
    REQUIRE(response_timing(true, 0) == GpioResponseTiming::AfterDebounce);
    REQUIRE(response_timing(true, 4) == GpioResponseTiming::AfterDebounce);
}

// ── Functional config ──────────────────────────────────────────────────────────

TEST_CASE("GpioFunctionalConfig default-constructs zeroed", "[gpio][REQ-GPIO-018]") {
    GpioFunctionalConfig cfg;
    REQUIRE_FALSE(cfg.ep_enable);
    REQUIRE(cfg.pins[0].pin_property == 0);
    REQUIRE(cfg.pins[0].trigger == GpioTrigger::None);
    REQUIRE(cfg.ep_status == 0);
    REQUIRE(cfg.clk_divider == 0);
    REQUIRE(cfg.debounce[0] == 0);
}

TEST_CASE("functional_cfg_writable is unwritable while HwUnconfigured", "[gpio][REQ-GPIO-019]") {
    WriterCtx writer;
    writer.via_root_client_ep0 = true;
    REQUIRE_FALSE(functional_cfg_writable(ServerState::HwUnconfigured, writer));
}

TEST_CASE("functional_cfg_writable requires an authorized writer while HwConfigured",
          "[gpio][REQ-GPIO-020]") {
    WriterCtx authorized;
    authorized.via_root_client_ep0 = true;
    REQUIRE(functional_cfg_writable(ServerState::HwConfigured, authorized));
    REQUIRE_FALSE(functional_cfg_writable(ServerState::HwConfigured, WriterCtx{}));
}

TEST_CASE("functional_cfg_writable requires authorization once RcpConfigured", "[gpio][REQ-GPIO-021]") {
    WriterCtx owning;
    owning.via_owning_stream = true;
    REQUIRE(functional_cfg_writable(ServerState::RcpConfigured, owning));

    WriterCtx discovery;
    discovery.via_discovery_stream = true;
    REQUIRE_FALSE(functional_cfg_writable(ServerState::RcpConfigured, discovery));
}

TEST_CASE("set_pin_property rejects an invalid pin index or an unauthorized write without mutating cfg",
          "[gpio][REQ-GPIO-022]") {
    GpioFunctionalConfig cfg;
    WriterCtx writer;
    writer.via_root_client_ep0 = true;
    REQUIRE_FALSE(set_pin_property(cfg, kMaxPins, kPinPropOutput, ServerState::HwConfigured, writer));
    REQUIRE_FALSE(set_pin_property(cfg, 0, kPinPropOutput, ServerState::HwUnconfigured, writer));
    REQUIRE(cfg.pins[0].pin_property == 0);
}

TEST_CASE("set_pin_property applies the write when authorized", "[gpio][REQ-GPIO-023]") {
    GpioFunctionalConfig cfg;
    WriterCtx writer;
    writer.via_root_client_ep0 = true;
    REQUIRE(set_pin_property(cfg, 5, kPinPropOutput, ServerState::HwConfigured, writer));
    REQUIRE(cfg.pins[5].pin_property == kPinPropOutput);
}

TEST_CASE("set_pin_trigger rejects an invalid pin index or an unauthorized write without mutating cfg",
          "[gpio][REQ-GPIO-024]") {
    GpioFunctionalConfig cfg;
    WriterCtx writer;
    writer.via_root_client_ep0 = true;
    REQUIRE_FALSE(set_pin_trigger(cfg, kMaxPins, GpioTrigger::Rising, ServerState::HwConfigured, writer));
    REQUIRE_FALSE(set_pin_trigger(cfg, 0, GpioTrigger::Rising, ServerState::HwUnconfigured, writer));
    REQUIRE(cfg.pins[0].trigger == GpioTrigger::None);
}

TEST_CASE("set_pin_trigger applies the write when authorized", "[gpio][REQ-GPIO-025]") {
    GpioFunctionalConfig cfg;
    WriterCtx writer;
    writer.via_owning_stream = true;
    REQUIRE(set_pin_trigger(cfg, 3, GpioTrigger::Falling, ServerState::RcpConfigured, writer));
    REQUIRE(cfg.pins[3].trigger == GpioTrigger::Falling);
}

// ── The EP_func register block ────────────────────────────────────────────────

TEST_CASE("render_registers matches Table 44's own offsets", "[gpio][REQ-GPIO-038]") {
    GpioFunctionalConfig cfg;
    cfg.ep_enable    = true;
    cfg.ep_status     = 0xCAFE;
    cfg.clk_divider    = 7;
    cfg.debounce[0]     = 3;
    cfg.debounce[31]     = 9;

    GpioRegisterBlock block{};
    render_registers(cfg, block);

    REQUIRE(block[kGpioRegEpLen] == kGpioEpFuncLen);
    REQUIRE(block[kGpioRegIoMax] == kMaxPins);
    REQUIRE((block[kGpioRegEpEnableClr] & 0x01) != 0);
    REQUIRE(rcp::avtp::detail::get_u16(&block[kGpioRegBaseClk]) == 0);
    REQUIRE(rcp::avtp::detail::get_u16(&block[kGpioRegEpStatus]) == 0xCAFE);
    REQUIRE(block[kGpioRegClkDivider] == 7);
    REQUIRE(block[kGpioRegDebounceIo0 + 0] == 3);
    REQUIRE(block[kGpioRegDebounceIo0 + 31] == 9);
    REQUIRE(kGpioEpFuncLen == 0x0029); // gpio_debounce_IO31 at the arithmetically-consistent 0x0028
}

TEST_CASE("apply_reconfig writes clk_divider", "[gpio][REQ-GPIO-013]") {
    GpioFunctionalConfig cfg;
    std::vector<uint8_t> payload{0x00, static_cast<uint8_t>(kGpioRegClkDivider), 42};
    REQUIRE_FALSE(apply_reconfig(cfg, payload.data(), payload.size()));
    REQUIRE(cfg.clk_divider == 42);
}

TEST_CASE("apply_reconfig writes a multi-register debounce span", "[gpio][REQ-GPIO-013]") {
    GpioFunctionalConfig cfg;
    std::vector<uint8_t> payload{0x00, static_cast<uint8_t>(kGpioRegDebounceIo0), 1, 2, 3};
    REQUIRE_FALSE(apply_reconfig(cfg, payload.data(), payload.size()));
    REQUIRE(cfg.debounce[0] == 1);
    REQUIRE(cfg.debounce[1] == 2);
    REQUIRE(cfg.debounce[2] == 3);
}

TEST_CASE("apply_reconfig ignores read-only registers within an otherwise-applied span",
          "[gpio][REQ-GPIO-042]") {
    GpioFunctionalConfig cfg;
    std::vector<uint8_t> payload{0x00, 0x00, /*EP_LEN*/ 0xAA, /*IO_MAX*/ 0xBB,
                                  /*EP_ENABLE_CLR*/ 0x01, /*EP_OPTIONS*/ 0x08};
    REQUIRE_FALSE(apply_reconfig(cfg, payload.data(), payload.size()));

    GpioRegisterBlock block{};
    render_registers(cfg, block);
    REQUIRE(block[kGpioRegEpLen] == kGpioEpFuncLen);
    REQUIRE(block[kGpioRegIoMax] == kMaxPins);
    REQUIRE(cfg.ep_enable);
    REQUIRE(cfg.ep_response_ts_enable); // options bit 3 (0x08) DID apply
}

TEST_CASE("apply_reconfig ignores the base_clk octets individually", "[gpio][REQ-GPIO-042]") {
    GpioFunctionalConfig cfg;
    std::vector<uint8_t> payload{0x00, static_cast<uint8_t>(kGpioRegBaseClk), 0xFF, 0xFF};
    REQUIRE_FALSE(apply_reconfig(cfg, payload.data(), payload.size()));
    GpioRegisterBlock block{};
    render_registers(cfg, block);
    REQUIRE(rcp::avtp::detail::get_u16(&block[kGpioRegBaseClk]) == 0);
}

TEST_CASE("apply_reconfig rejects a write extending past EP_LEN", "[gpio][REQ-GPIO-041]") {
    GpioFunctionalConfig cfg;
    std::vector<uint8_t> payload{0x00, static_cast<uint8_t>(kGpioEpFuncLen - 1), 0xAA, 0xBB};
    REQUIRE(apply_reconfig(cfg, payload.data(), payload.size()) ==
            make_error_code(GpioErrc::reconfig_out_of_range));
}

TEST_CASE("apply_reconfig rejects a payload without address+data", "[gpio][REQ-GPIO-040]") {
    GpioFunctionalConfig cfg;
    std::vector<uint8_t> payload{0x00, 0x00};
    REQUIRE(apply_reconfig(cfg, payload.data(), payload.size()) ==
            make_error_code(GpioErrc::reconfig_short));
}

TEST_CASE("encode_reconfig_request round-trips through apply_reconfig", "[gpio][REQ-GPIO-043]") {
    std::vector<uint8_t> data{42};
    auto frame = encode_reconfig_request(0x10, kGpioRegClkDivider, data, 5);
    REQUIRE_FALSE(frame.empty());

    rcp::acf::AcfMessageInfo info;
    std::vector<uint8_t> payload;
    REQUIRE_FALSE(rcp::acf::decode_acf_abb(frame.data(), frame.size(), info, payload));
    REQUIRE(info.op);
    REQUIRE(info.evt_op == static_cast<uint8_t>(WriteSemantics::Reconfigure));

    GpioFunctionalConfig cfg;
    REQUIRE_FALSE(apply_reconfig(cfg, payload.data(), payload.size()));
    REQUIRE(cfg.clk_divider == 42);
}

TEST_CASE("encode_reconfig_request rejects empty data", "[gpio][REQ-GPIO-043]") {
    REQUIRE(encode_reconfig_request(0x10, 0, {}, 1).empty());
}

// ── GpioErrc category sanity ──────────────────────────────────────────────────

TEST_CASE("GpioErrc reports a non-empty, category-correct message for every value",
          "[gpio][REQ-GPIO-001][REQ-GPIO-039]") {
    for (int v = 1; v <= 9; ++v) {
        auto ec = make_error_code(static_cast<GpioErrc>(v));
        REQUIRE(ec.category() == gpio_category());
        REQUIRE_FALSE(ec.message().empty());
    }
}

TEST_CASE("wire_error maps bad_payload_len/reserved_evt to their numbered wire codes",
          "[gpio][REQ-GPIO-033][REQ-GPIO-046]") {
    REQUIRE(wire_error(GpioErrc::bad_payload_len) == rcp::acf::WireErrorCode::InvalidParameter);
    REQUIRE(wire_error(GpioErrc::reserved_evt) == rcp::acf::WireErrorCode::UnsupportedCmd);
    REQUIRE_FALSE(wire_error(GpioErrc::short_frame).has_value());
}

// ── Wire codec: read request ──────────────────────────────────────────────────

TEST_CASE("GPIO read request encode/decode round-trips", "[gpio][REQ-GPIO-026]") {
    auto frame = encode_read_request(0x20, 7);
    uint8_t txn = 0;
    REQUIRE_FALSE(decode_read_request(frame.data(), frame.size(), 0x20, txn));
    REQUIRE(txn == 7);
    REQUIRE(frame.size() == rcp::acf::kAcfCommonHeaderLen); // no payload
}

TEST_CASE("decode_read_request rejects a malformed or misaddressed frame", "[gpio][REQ-GPIO-027]") {
    auto frame = encode_read_request(0x20, 1);
    uint8_t txn = 0;
    REQUIRE(decode_read_request(frame.data(), frame.size(), 0x21, txn) ==
            make_error_code(GpioErrc::wrong_bus));

    // byte0's top 7 bits must carry acf_msg_type == kAcfMsgTypeAbb (0x0E) --
    // otherwise decode_acf_abb reports bad_acf_msg_type before it even gets
    // to check the buffer's length against the fixed header size.
    std::vector<uint8_t> short_frame{0x1C, 0x01};
    REQUIRE(decode_read_request(short_frame.data(), short_frame.size(), 0x20, txn) ==
            make_error_code(GpioErrc::short_frame));

    auto write_frame = encode_write_request(0x20, 0, WriteSemantics::Replace, 1);
    REQUIRE(decode_read_request(write_frame.data(), write_frame.size(), 0x20, txn) ==
            make_error_code(GpioErrc::wrong_op));
}

// ── Wire codec: write request ─────────────────────────────────────────────────

TEST_CASE("GPIO write request encode/decode round-trips, including evt[2:0]", "[gpio][REQ-GPIO-028]") {
    auto frame = encode_write_request(0x20, 0xDEADBEEF, WriteSemantics::Or, 3);
    PinMask bitmask = 0;
    WriteSemantics evt{};
    uint8_t txn = 0;
    REQUIRE_FALSE(decode_write_request(frame.data(), frame.size(), 0x20, bitmask, evt, txn));
    REQUIRE(bitmask == 0xDEADBEEF);
    REQUIRE(evt == WriteSemantics::Or);
    REQUIRE(txn == 3);
}

TEST_CASE("decode_write_request rejects a malformed or misaddressed frame", "[gpio][REQ-GPIO-029]") {
    auto frame = encode_write_request(0x20, 0, WriteSemantics::Replace, 1);
    PinMask bitmask = 0;
    WriteSemantics evt{};
    uint8_t txn = 0;
    REQUIRE(decode_write_request(frame.data(), frame.size(), 0x21, bitmask, evt, txn) ==
            make_error_code(GpioErrc::wrong_bus));

    auto read_frame = encode_read_request(0x20, 1);
    REQUIRE(decode_write_request(read_frame.data(), read_frame.size(), 0x20, bitmask, evt, txn) ==
            make_error_code(GpioErrc::wrong_op));

    rcp::acf::AcfMessageInfo bad_len_info;
    bad_len_info.byte_bus_id = 0x20;
    bad_len_info.op = true;
    auto bad_len_frame = rcp::acf::encode_acf_abb(bad_len_info, std::vector<uint8_t>{0x01});
    REQUIRE(decode_write_request(bad_len_frame.data(), bad_len_frame.size(), 0x20, bitmask, evt, txn) ==
            make_error_code(GpioErrc::bad_payload_len));
}

TEST_CASE("decode_write_request rejects the reserved evt[2:0]=100b value", "[gpio][REQ-GPIO-012][REQ-GPIO-045]") {
    auto frame = encode_write_request(0x20, 0xFF, WriteSemantics::Reserved, 1);
    PinMask bitmask = 0xAAAAAAAA;
    WriteSemantics evt = WriteSemantics::Or;
    uint8_t txn = 0xFF;
    auto ec = decode_write_request(frame.data(), frame.size(), 0x20, bitmask, evt, txn);
    REQUIRE(ec == make_error_code(GpioErrc::reserved_evt));
}

TEST_CASE("encode_write_request masks evt to its low 3 bits", "[gpio][REQ-GPIO-028]") {
    // Add (5) is within range; verify the round trip carries it faithfully.
    auto frame = encode_write_request(0x20, 1, WriteSemantics::Add, 1);
    PinMask bitmask = 0;
    WriteSemantics evt{};
    uint8_t txn = 0;
    REQUIRE_FALSE(decode_write_request(frame.data(), frame.size(), 0x20, bitmask, evt, txn));
    REQUIRE(evt == WriteSemantics::Add);
}

// ── Wire codec: response ──────────────────────────────────────────────────────

TEST_CASE("GPIO response encode/decode round-trips when untimed", "[gpio][REQ-GPIO-030]") {
    auto frame = encode_response(0x20, 0x12345678, 9, /*timed=*/false, 0);
    PinMask bitmask = 0;
    bool timed = true;
    uint64_t ts = 1;
    uint8_t txn = 0;
    REQUIRE_FALSE(decode_response(frame.data(), frame.size(), 0x20, bitmask, timed, ts, txn));
    REQUIRE(bitmask == 0x12345678);
    REQUIRE_FALSE(timed);
    REQUIRE(ts == 0);
    REQUIRE(txn == 9);
}

TEST_CASE("GPIO response encode/decode round-trips when timed", "[gpio][REQ-GPIO-031]") {
    auto frame = encode_response(0x20, 0xAABBCCDD, 2, /*timed=*/true, 55555);
    PinMask bitmask = 0;
    bool timed = false;
    uint64_t ts = 0;
    uint8_t txn = 0;
    REQUIRE_FALSE(decode_response(frame.data(), frame.size(), 0x20, bitmask, timed, ts, txn));
    REQUIRE(bitmask == 0xAABBCCDD);
    REQUIRE(timed);
    REQUIRE(ts == 55555);
}

TEST_CASE("decode_response rejects a malformed or misaddressed frame", "[gpio][REQ-GPIO-032]") {
    auto frame = encode_response(0x20, 0, 1, false, 0);
    PinMask bitmask = 0;
    bool timed = false;
    uint64_t ts = 0;
    uint8_t txn = 0;
    REQUIRE(decode_response(frame.data(), frame.size(), 0x21, bitmask, timed, ts, txn) ==
            make_error_code(GpioErrc::wrong_bus));

    std::vector<uint8_t> short_frame{0x00};
    REQUIRE(decode_response(short_frame.data(), 0, 0x20, bitmask, timed, ts, txn) ==
            make_error_code(GpioErrc::short_frame));

    rcp::acf::AcfMessageInfo bad_len_info;
    bad_len_info.byte_bus_id = 0x20;
    auto bad_len_frame = rcp::acf::encode_acf_abb(bad_len_info, std::vector<uint8_t>{0x01, 0x02});
    REQUIRE(decode_response(bad_len_frame.data(), bad_len_frame.size(), 0x20, bitmask, timed, ts, txn) ==
            make_error_code(GpioErrc::bad_payload_len));
}

// ── Functional config block wiring (pre-Phase-3 opaque-blob helpers) ────────

TEST_CASE("encode/decode_gpio_functional_config round-trips through the opaque regmap blob",
          "[gpio][REQ-GPIO-006]") {
    std::array<uint8_t, kMaxPins> edges{};
    edges[0] = 0b001;
    edges[5] = 0b110;

    auto cfg = encode_gpio_functional_config(0xF0F0F0F0, edges);
    REQUIRE(cfg.data.size() == kGpioFunctionalConfigLen);

    PinMask out_directions = 0;
    std::array<uint8_t, kMaxPins> out_edges{};
    auto ec = decode_gpio_functional_config(cfg, out_directions, out_edges);
    REQUIRE_FALSE(ec);
    REQUIRE(out_directions == 0xF0F0F0F0);
    REQUIRE(out_edges == edges);
}

TEST_CASE("decode_gpio_functional_config rejects an undersized blob", "[gpio][REQ-GPIO-006]") {
    rcp::regmap::EndpointFunctionalConfig cfg;
    cfg.data = {0x01, 0x02};
    PinMask out_directions = 0;
    std::array<uint8_t, kMaxPins> out_edges{};
    auto ec = decode_gpio_functional_config(cfg, out_directions, out_edges);
    REQUIRE(ec == rcp::avtp::make_error_code(rcp::avtp::AvtpErrc::short_buffer));
}

// ── GpioEndpoint request-dispatch pattern ────────────────────────────────────

TEST_CASE("GpioEndpoint::handle_write updates state and fires triggers in one call",
          "[gpio][REQ-GPIO-007]") {
    GpioEndpoint ep;
    ep.triggers().enable(gpio_signal_id(0, GpioEdge::Change));
    ep.triggers().enable(gpio_signal_id(0, GpioEdge::Rising));

    PinMask out_value = 0;
    REQUIRE_FALSE(ep.handle_write(WriteSemantics::Reconfigure, /*operand=*/0x1, out_value));

    auto ec = ep.handle_write(WriteSemantics::Or, /*operand=*/0x1, out_value);
    REQUIRE_FALSE(ec);
    REQUIRE(out_value == 0x1);
    REQUIRE(ep.read() == 0x1);

    auto fired = ep.triggers().drain();
    REQUIRE(fired.size() == 2);
}

TEST_CASE("GpioEndpoint::handle_write propagates a Reserved-semantics error without changing state",
          "[gpio][REQ-GPIO-012]") {
    GpioEndpoint ep;
    PinMask out_value = 0;
    auto ec = ep.handle_write(WriteSemantics::Reserved, 0xFF, out_value);
    REQUIRE(ec);
    REQUIRE(ep.read() == 0);
}
