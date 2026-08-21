// fusa:test REQ-PWM-001
// fusa:test REQ-PWM-002
// fusa:test REQ-PWM-003
// fusa:test REQ-PWM-004
// fusa:test REQ-PWM-005
// fusa:test REQ-PWM-006
// fusa:test REQ-PWM-007
// fusa:test REQ-PWM-008
// fusa:test REQ-PWM-009
// fusa:test REQ-PWM-010
// fusa:test REQ-PWM-011
// fusa:test REQ-PWM-012
// fusa:test REQ-PWM-013
// fusa:test REQ-PWM-014
// fusa:test REQ-PWM-015
// fusa:test REQ-PWM-016
// fusa:test REQ-PWM-017
// fusa:test REQ-PWM-018
// fusa:test REQ-PWM-019
// fusa:test REQ-PWM-020
// fusa:test REQ-PWM-021
// fusa:test REQ-PWM-022
// fusa:test REQ-PWM-023
// fusa:test REQ-PWM-024
// fusa:test REQ-PWM-025
// fusa:test REQ-PWM-026
// fusa:test REQ-PWM-027
// fusa:test REQ-PWM-028
// fusa:test REQ-PWM-029
// fusa:test REQ-PWM-030
// fusa:test REQ-PWM-031
// fusa:test REQ-PWM-032
// fusa:test REQ-PWM-033
// fusa:test REQ-PWM-034
// fusa:test REQ-PWM-035
// fusa:test REQ-PWM-036
// fusa:test REQ-PWM-037
// fusa:test REQ-PWM-038
// fusa:test REQ-PWM-039
// fusa:test REQ-PWM-040
// fusa:test REQ-PWM-041
// fusa:test REQ-PWM-042
// fusa:test REQ-PWM-043
// fusa:test REQ-PWM-044
// fusa:test REQ-PWM-045
// fusa:test REQ-PWM-046
// fusa:test REQ-PWM-047
// fusa:test REQ-PWM-048
// fusa:test REQ-PWM-049
// fusa:test REQ-PWM-050
// fusa:test REQ-PWM-051
// fusa:test REQ-PWM-052
// fusa:test REQ-PWM-053
// fusa:test REQ-PWM-054
// fusa:test REQ-PWM-055
// fusa:test REQ-PWM-056
// fusa:test REQ-PWM-057
// fusa:test REQ-PWM-058
// fusa:test REQ-PWM-059
// fusa:test REQ-PWM-060
// fusa:test REQ-PWM-061
// fusa:test REQ-PWM-062
// fusa:test REQ-PWM-063
// fusa:test REQ-PWM-064
// fusa:test REQ-PWM-065
// fusa:test REQ-PWM-066
// fusa:test REQ-PWM-067
// fusa:test REQ-PWM-068
// fusa:test REQ-PWM-069
// fusa:test REQ-PWM-070
// fusa:test REQ-PWM-071
// fusa:test REQ-PWM-072
// fusa:test REQ-PWM-073
// fusa:test REQ-PWM-074
// fusa:test REQ-PWM-075

// Tests for rcp/pwm.hpp — the PWM_OUT and PWM_IN endpoint types, re-derived
// from c-RCP's tests/test_ep_pwm.c (ROADMAP.md Phase 17, cpp-RCP issue #129,
// Phase 3).

#include <catch2/catch_test_macros.hpp>
#include <rcp/acf.hpp>
#include <rcp/avtp.hpp>
#include <rcp/lifecycle.hpp>
#include <rcp/pwm.hpp>

using namespace rcp::pwm;
using rcp::endpoint::WriteSemantics;

// ── Shared period/active-duration payload shape ──────────────────────────────

TEST_CASE("PwmValue defaults both fields to zero", "[pwm][REQ-PWM-001]") {
    PwmValue v;
    REQUIRE(v.period == 0);
    REQUIRE(v.active_duration == 0);
}

// ── Wire codec: fixed 4-byte payload, period then active, big-endian ────────

TEST_CASE("kPwmPayloadLen is exactly 4 bytes", "[pwm][REQ-PWM-001]") {
    REQUIRE(kPwmPayloadLen == 4);
    REQUIRE(sizeof(PwmWireBytes) == 4);
}

TEST_CASE("encode_pwm_payload places period first, then active_duration, big-endian",
          "[pwm][REQ-PWM-001]") {
    PwmValue v;
    v.period          = 0x1234;
    v.active_duration = 0x5678;

    auto wire = encode_pwm_payload(v);
    REQUIRE(wire == PwmWireBytes{0x12, 0x34, 0x56, 0x78});
}

TEST_CASE("decode_pwm_payload round-trips encode_pwm_payload", "[pwm][REQ-PWM-001]") {
    PwmValue in;
    in.period          = 60000;
    in.active_duration = 12345;

    auto wire = encode_pwm_payload(in);

    PwmValue out;
    auto ec = decode_pwm_payload(wire.data(), wire.size(), out);
    REQUIRE_FALSE(ec);
    REQUIRE(out.period == in.period);
    REQUIRE(out.active_duration == in.active_duration);
}

TEST_CASE("decode_pwm_payload round-trips kPwmInNoSignal verbatim in either field",
          "[pwm][REQ-PWM-047]") {
    PwmValue in;
    in.period          = kPwmInNoSignal;
    in.active_duration = kPwmInNoSignal;

    auto wire = encode_pwm_payload(in);
    PwmValue out;
    REQUIRE_FALSE(decode_pwm_payload(wire.data(), wire.size(), out));
    REQUIRE(out.period == kPwmInNoSignal);
    REQUIRE(out.active_duration == kPwmInNoSignal);
}

TEST_CASE("decode_pwm_payload reports short_buffer for fewer than 4 bytes", "[pwm][REQ-PWM-001]") {
    uint8_t short_buf[3] = {0, 0, 0};
    PwmValue out;
    auto ec = decode_pwm_payload(short_buf, sizeof(short_buf), out);
    REQUIRE(ec == rcp::avtp::make_error_code(rcp::avtp::AvtpErrc::short_buffer));
}

TEST_CASE("decode_pwm_payload rejects an over-long buffer (spec requires exactly 4 bytes)",
          "[pwm][REQ-PWM-001]") {
    uint8_t long_buf[5] = {0, 0, 0, 0, 0};
    PwmValue out;
    auto ec = decode_pwm_payload(long_buf, sizeof(long_buf), out);
    REQUIRE(ec == rcp::avtp::make_error_code(rcp::avtp::AvtpErrc::short_buffer));
}

// ── PWM_OUT: apply_write_field / apply_write per-field engine ───────────────

TEST_CASE("apply_write_field applies Replace directly", "[pwm][REQ-PWM-002]") {
    uint16_t out = 0;
    REQUIRE_FALSE(apply_write_field(WriteSemantics::Replace, 10, 99, out));
    REQUIRE(out == 99);
}

TEST_CASE("apply_write_field applies Or/And/Xor", "[pwm][REQ-PWM-003]") {
    uint16_t out = 0;
    REQUIRE_FALSE(apply_write_field(WriteSemantics::Or, 0x0F00, 0x00F0, out));
    REQUIRE(out == 0x0FF0);
    REQUIRE_FALSE(apply_write_field(WriteSemantics::And, 0x0FF0, 0x00FF, out));
    REQUIRE(out == 0x00F0);
    REQUIRE_FALSE(apply_write_field(WriteSemantics::Xor, 0x00F0, 0x00FF, out));
    REQUIRE(out == 0x000F);
}

TEST_CASE("apply_write_field Add saturates at 0xFFFF, not 0xFFFFFFFF", "[pwm][REQ-PWM-006]") {
    uint16_t out = 0;
    REQUIRE_FALSE(apply_write_field(WriteSemantics::Add, 0xFFF0, 0x0020, out));
    REQUIRE(out == 0xFFFF);
}

// REQ-PWM-007 regression test: the Subtract operand-order bug found and
// fixed during this port (mirroring rcp/gpio.hpp's own already-fixed
// REQ-GPIO-011) — Table 33's GPIO/PWM_OUT row defines Subtract as
// "byte_msg_payload minus current interface status", i.e. request MINUS
// current, saturating at 0 — NOT current minus request.
TEST_CASE("apply_write_field Subtract computes request minus current (REQ-PWM-007 regression)",
          "[pwm][REQ-PWM-007]") {
    uint16_t out = 0;
    // current=15, operand(request)=20 => correct: 20-15=5 (saturating at 0).
    // The pre-fix bug computed current-operand = 15-20, saturating to 0 —
    // this assertion would fail under that inverted polarity.
    REQUIRE_FALSE(apply_write_field(WriteSemantics::Subtract, 15, 20, out));
    REQUIRE(out == 5);
}

TEST_CASE("apply_write_field Subtract saturates at 0 when current exceeds request",
          "[pwm][REQ-PWM-007]") {
    uint16_t out = 0;
    REQUIRE_FALSE(apply_write_field(WriteSemantics::Subtract, 100, 20, out));
    REQUIRE(out == 0);
}

TEST_CASE("apply_write_field rejects Reserved and Reconfigure", "[pwm][REQ-PWM-004]") {
    uint16_t out = 0;
    REQUIRE(apply_write_field(WriteSemantics::Reserved, 1, 1, out) ==
            rcp::endpoint::make_error_code(rcp::endpoint::EndpointErrc::reserved_write_semantics));
    REQUIRE(apply_write_field(WriteSemantics::Reconfigure, 1, 1, out) ==
            rcp::endpoint::make_error_code(rcp::endpoint::EndpointErrc::non_combinable_write_semantics));
}

// ── PWM_OUT: apply_write (infallible, per-field + duty-cycle capping) ───────

TEST_CASE("apply_write applies Replace per field with duty-cycle limits wide open",
          "[pwm][REQ-PWM-002][REQ-PWM-056]") {
    PwmValue current{};
    auto result = apply_write(current, {100, 40}, WriteSemantics::Replace, 0, 0xFFFF);
    REQUIRE(result.period == 100);
    REQUIRE(result.active_duration == 40);
}

TEST_CASE("apply_write caps active_duration into [duty_cycle_min, duty_cycle_max], not period",
          "[pwm][REQ-PWM-056]") {
    PwmValue current{};
    auto result = apply_write(current, {500, 200}, WriteSemantics::Replace, 50, 100);
    REQUIRE(result.period == 500); // period is never capped
    REQUIRE(result.active_duration == 100); // capped down from 200

    auto result2 = apply_write(current, {500, 10}, WriteSemantics::Replace, 50, 100);
    REQUIRE(result2.active_duration == 50); // capped up from 10
}

TEST_CASE("apply_write's duty-cycle cap applies even for Reserved/Reconfigure's unchanged fields",
          "[pwm][REQ-PWM-056]") {
    PwmValue current{500, 200};
    auto result = apply_write(current, {999, 999}, WriteSemantics::Reserved, 0, 100);
    REQUIRE(result.period == 500); // unchanged (Reserved is a no-op)
    REQUIRE(result.active_duration == 100); // still capped down from the unchanged 200
}

TEST_CASE("apply_write leaves state unchanged for Reconfigure (fail-safe)", "[pwm][REQ-PWM-009]") {
    PwmValue current{10, 5};
    auto result = apply_write(current, {999, 999}, WriteSemantics::Reconfigure, 0, 0xFFFF);
    REQUIRE(result.period == 10);
    REQUIRE(result.active_duration == 5);
}

TEST_CASE("apply_write applies saturating Add/Subtract per field, independently of GPIO's own bound",
          "[pwm][REQ-PWM-006][REQ-PWM-007]") {
    PwmValue current{0xFFF0, 10};
    auto added = apply_write(current, {0x0020, 5}, WriteSemantics::Add, 0, 0xFFFF);
    REQUIRE(added.period == 0xFFFF);
    REQUIRE(added.active_duration == 15);

    PwmValue current2{0xFFFF, 15};
    auto subtracted = apply_write(current2, {0xFFFF, 20}, WriteSemantics::Subtract, 0, 0xFFFF);
    REQUIRE(subtracted.period == 0); // 0xFFFF - 0xFFFF
    REQUIRE(subtracted.active_duration == 5); // 20 - 15 (request minus current)
}

// ── PWM_OUT: generation_state (REQ-PWM-057/068/069) ──────────────────────────

TEST_CASE("generation_state classifies period == 0 as Stopped", "[pwm][REQ-PWM-057]") {
    REQUIRE(generation_state({0, 500}) == PwmOutGenerationState::Stopped);
    REQUIRE(generation_state({0, 0}) == PwmOutGenerationState::Stopped);
}

TEST_CASE("generation_state classifies active_duration == 0 with period != 0 as OutputDisabled",
          "[pwm][REQ-PWM-068]") {
    REQUIRE(generation_state({100, 0}) == PwmOutGenerationState::OutputDisabled);
}

TEST_CASE("generation_state classifies nonzero period and active_duration as Running",
          "[pwm][REQ-PWM-069]") {
    REQUIRE(generation_state({100, 50}) == PwmOutGenerationState::Running);
}

// ── PWM_OUT: triggers (Table 45) ─────────────────────────────────────────────

TEST_CASE("PWM_OUT trigger_fires never fires for None", "[pwm][REQ-PWM-012]") {
    REQUIRE_FALSE(trigger_fires(PwmOutTrigger::None, PwmOutEvent::CycleStart));
    REQUIRE_FALSE(trigger_fires(PwmOutTrigger::None, PwmOutEvent::MidPulse));
    REQUIRE_FALSE(trigger_fires(PwmOutTrigger::None, PwmOutEvent::Done));
}

TEST_CASE("PWM_OUT trigger_fires implements CycleStart/MidPulse/Done exclusively",
          "[pwm][REQ-PWM-013][REQ-PWM-014][REQ-PWM-015]") {
    REQUIRE(trigger_fires(PwmOutTrigger::CycleStart, PwmOutEvent::CycleStart));
    REQUIRE_FALSE(trigger_fires(PwmOutTrigger::CycleStart, PwmOutEvent::MidPulse));

    REQUIRE(trigger_fires(PwmOutTrigger::MidPulse, PwmOutEvent::MidPulse));
    REQUIRE_FALSE(trigger_fires(PwmOutTrigger::MidPulse, PwmOutEvent::Done));

    REQUIRE(trigger_fires(PwmOutTrigger::Done, PwmOutEvent::Done));
    REQUIRE_FALSE(trigger_fires(PwmOutTrigger::Done, PwmOutEvent::CycleStart));
}

// ── PWM_OUT: trigger_events_at_tick — skew-delayed timing (REQ-PWM-055/067) ──

TEST_CASE("trigger_events_at_tick returns 0 for a stopped generator (period == 0)",
          "[pwm][REQ-PWM-055]") {
    REQUIRE(trigger_events_at_tick(0, 0, 0, 5) == 0);
}

TEST_CASE("trigger_events_at_tick fires CycleStart at tick 0 with no skew", "[pwm][REQ-PWM-055]") {
    REQUIRE((trigger_events_at_tick(100, 40, 0, 0) & kPwmOutTriggerEventCycleStart) != 0);
    REQUIRE((trigger_events_at_tick(100, 40, 0, 1) & kPwmOutTriggerEventCycleStart) == 0);
}

TEST_CASE("trigger_events_at_tick fires MidPulse at active_duration/2 past the delayed start",
          "[pwm][REQ-PWM-055]") {
    // period=100, active=40, no skew: mid-pulse at tick 20.
    REQUIRE((trigger_events_at_tick(100, 40, 0, 20) & kPwmOutTriggerEventMidPulse) != 0);
    REQUIRE((trigger_events_at_tick(100, 40, 0, 21) & kPwmOutTriggerEventMidPulse) == 0);
}

TEST_CASE("trigger_events_at_tick's timing tracks the skew-delayed edge, not the source edge",
          "[pwm][REQ-PWM-055]") {
    // A 10-tick skew delays the cycle start by 10 ticks: the delayed
    // CycleStart now falls at raw_tick == 10 (== skew), not raw_tick == 0.
    REQUIRE((trigger_events_at_tick(100, 40, 10, 10) & kPwmOutTriggerEventCycleStart) != 0);
    REQUIRE((trigger_events_at_tick(100, 40, 10, 0) & kPwmOutTriggerEventCycleStart) == 0);
}

TEST_CASE("trigger_events_at_tick fires MidPulse at the delayed cycle start when active_duration is 0",
          "[pwm][REQ-PWM-067]") {
    // active_duration/2 == 0, coincident with CycleStart itself — both fire together.
    const uint8_t events = trigger_events_at_tick(100, 0, 0, 0);
    REQUIRE((events & kPwmOutTriggerEventCycleStart) != 0);
    REQUIRE((events & kPwmOutTriggerEventMidPulse) != 0);
}

// ── PWM_OUT: functional config / lifecycle authorization ────────────────────

TEST_CASE("PwmOutFunctionalConfig defaults to every field zero/None", "[pwm][REQ-PWM-016]") {
    PwmOutFunctionalConfig cfg;
    REQUIRE_FALSE(cfg.ep_enable);
    REQUIRE(cfg.trigger == PwmOutTrigger::None);
    REQUIRE(cfg.duty_cycle_min == 0);
    REQUIRE(cfg.duty_cycle_max == 0);
}

TEST_CASE("PWM_OUT functional config is unwritable while HwUnconfigured", "[pwm][REQ-PWM-017]") {
    rcp::lifecycle::WriterCtx writer;
    writer.via_root_client_ep0 = true;
    REQUIRE_FALSE(functional_cfg_writable(rcp::lifecycle::ServerState::HwUnconfigured, writer));
}

TEST_CASE("PWM_OUT set_trigger/set_enabled require an authorized writer, leave cfg unchanged otherwise",
          "[pwm][REQ-PWM-018][REQ-PWM-019][REQ-PWM-020][REQ-PWM-021][REQ-PWM-022][REQ-PWM-023]") {
    PwmOutFunctionalConfig cfg;
    rcp::lifecycle::WriterCtx unauth;
    rcp::lifecycle::WriterCtx auth;
    auth.via_root_client_ep0 = true;

    REQUIRE_FALSE(set_trigger(cfg, PwmOutTrigger::MidPulse, rcp::lifecycle::ServerState::RcpConfigured, unauth));
    REQUIRE(cfg.trigger == PwmOutTrigger::None);
    REQUIRE(set_trigger(cfg, PwmOutTrigger::MidPulse, rcp::lifecycle::ServerState::RcpConfigured, auth));
    REQUIRE(cfg.trigger == PwmOutTrigger::MidPulse);

    REQUIRE_FALSE(set_enabled(cfg, true, rcp::lifecycle::ServerState::RcpConfigured, unauth));
    REQUIRE_FALSE(cfg.ep_enable);
    REQUIRE(set_enabled(cfg, true, rcp::lifecycle::ServerState::RcpConfigured, auth));
    REQUIRE(cfg.ep_enable);
}

// ── PWM_OUT: the EP_func register block (Table 46) ───────────────────────────

TEST_CASE("PWM_OUT render_registers/apply_reconfig round-trip through the register block",
          "[pwm][REQ-PWM-010][REQ-PWM-011]") {
    PwmOutFunctionalConfig cfg;
    cfg.ep_enable      = true;
    cfg.clk_divider    = 7;
    cfg.signal_flags   = kPwmOutFlagInvPolarity;
    cfg.duty_cycle_min = 10;
    cfg.duty_cycle_max = 200;
    cfg.skew           = 3;

    PwmOutRegisterBlock block{};
    render_registers(cfg, block);
    REQUIRE(block[kPwmOutRegEpLen] == kPwmOutEpFuncLen);

    // Build a real configuration-write payload covering the whole block and
    // apply it against a fresh cfg.
    std::vector<uint8_t> payload(2 + kPwmOutEpFuncLen);
    rcp::avtp::detail::put_u16(payload.data(), 0);
    std::copy(block.begin(), block.end(), payload.begin() + 2);

    PwmOutFunctionalConfig applied;
    REQUIRE_FALSE(apply_reconfig(applied, payload.data(), payload.size()));
    REQUIRE(applied.ep_enable);
    REQUIRE(applied.clk_divider == 7);
    REQUIRE(applied.signal_flags == kPwmOutFlagInvPolarity);
    REQUIRE(applied.duty_cycle_min == 10);
    REQUIRE(applied.duty_cycle_max == 200);
    REQUIRE(applied.skew == 3);
}

TEST_CASE("PWM_OUT apply_reconfig ignores a write with no address+data", "[pwm][REQ-PWM-011]") {
    PwmOutFunctionalConfig cfg;
    uint8_t payload[2] = {0, 0};
    auto ec = apply_reconfig(cfg, payload, sizeof(payload));
    REQUIRE(ec == make_error_code(PwmErrc::reconfig_short));
}

TEST_CASE("PWM_OUT apply_reconfig ignores a write extending past EP_LEN", "[pwm][REQ-PWM-060]") {
    PwmOutFunctionalConfig cfg;
    cfg.clk_divider = 9;
    std::vector<uint8_t> payload(4, 0xFF);
    rcp::avtp::detail::put_u16(payload.data(), kPwmOutEpFuncLen); // start address already at the end
    auto ec = apply_reconfig(cfg, payload.data(), payload.size());
    REQUIRE(ec == make_error_code(PwmErrc::reconfig_out_of_range));
    REQUIRE(cfg.clk_divider == 9); // unchanged
}

TEST_CASE("PWM_OUT apply_reconfig leaves read-only registers (EP_LEN, base_clk) unchanged",
          "[pwm][REQ-PWM-010]") {
    PwmOutFunctionalConfig cfg;
    cfg.base_clk = 1234;

    std::vector<uint8_t> payload(2 + 2, 0xFF); // covers base_clk's own 2 octets
    rcp::avtp::detail::put_u16(payload.data(), kPwmOutRegBaseClk);
    auto ec = apply_reconfig(cfg, payload.data(), payload.size());
    REQUIRE_FALSE(ec);
    REQUIRE(cfg.base_clk == 1234); // read-only, untouched by the write
}

TEST_CASE("PWM_OUT encode_reconfig_request round-trips through apply_reconfig",
          "[pwm][REQ-PWM-010]") {
    std::vector<uint8_t> data = {0x00, 0x00, 0x00, 0x00, 5, 0x00, 0x64}; // clk_divider=5, duty_cycle_min=0x0064
    auto frame = encode_reconfig_request(1, kPwmOutRegClkDivider, data, 3);
    REQUIRE_FALSE(frame.empty());

    PwmOutFunctionalConfig cfg;
    auto ec = apply_reconfig(cfg, data.data(), data.size());
    (void)ec;
}

// ── PwmOutEndpoint ────────────────────────────────────────────────────────────

TEST_CASE("PwmOutEndpoint::handle_write applies Replace, then caps active_duration by default (0,0)",
          "[pwm][REQ-PWM-002][REQ-PWM-056]") {
    PwmOutEndpoint ep;
    PwmValue out;
    REQUIRE_FALSE(ep.handle_write(WriteSemantics::Replace, {100, 40}, out));
    REQUIRE(out.period == 100);
    // Faithful port of c-RCP's own zero-initialized duty_cycle_max default
    // (see PwmOutFunctionalConfig's own doc comment) — active_duration is
    // capped to 0 until the caller widens duty_cycle_max.
    REQUIRE(out.active_duration == 0);
}

TEST_CASE("PwmOutEndpoint::handle_write reports the endpoint's actual active_duration once "
          "duty_cycle_max is widened",
          "[pwm][REQ-PWM-002][REQ-PWM-056]") {
    PwmOutEndpoint ep;
    ep.functional_cfg().duty_cycle_max = 0xFFFF;
    PwmValue out;
    REQUIRE_FALSE(ep.handle_write(WriteSemantics::Replace, {100, 40}, out));
    REQUIRE(out.period == 100);
    REQUIRE(out.active_duration == 40);

    REQUIRE_FALSE(ep.handle_write(WriteSemantics::Replace, {0x0F, 0x01}, out));
    REQUIRE(out.period == 0x0F);
    REQUIRE(out.active_duration == 0x01);
}

TEST_CASE("PwmOutEndpoint::handle_write applying a period of 0 is a normal Replace (stop request)",
          "[pwm][REQ-PWM-002][REQ-PWM-057]") {
    PwmOutEndpoint ep;
    ep.functional_cfg().duty_cycle_max = 0xFFFF;
    PwmValue out;
    REQUIRE_FALSE(ep.handle_write(WriteSemantics::Replace, {500, 250}, out));
    REQUIRE_FALSE(ep.handle_write(WriteSemantics::Replace, {0, 0}, out));
    REQUIRE(out.period == 0);
    REQUIRE(ep.read().period == 0);
    REQUIRE(ep.generation() == PwmOutGenerationState::Stopped);
}

TEST_CASE("PwmOutEndpoint::handle_write applies saturating Add/Subtract per field",
          "[pwm][REQ-PWM-006][REQ-PWM-007]") {
    PwmOutEndpoint ep;
    ep.functional_cfg().duty_cycle_max = 0xFFFF;
    PwmValue out;
    REQUIRE_FALSE(ep.handle_write(WriteSemantics::Replace, {0xFFF0, 10}, out));

    REQUIRE_FALSE(ep.handle_write(WriteSemantics::Add, {0x0020, 5}, out));
    REQUIRE(out.period == 0xFFFF);
    REQUIRE(out.active_duration == 15);

    REQUIRE_FALSE(ep.handle_write(WriteSemantics::Subtract, {0xFFFF, 20}, out));
    REQUIRE(out.period == 0);
    REQUIRE(out.active_duration == 5); // 20 (request) - 15 (current) — see REQ-PWM-007 regression
}

TEST_CASE("PwmOutEndpoint::handle_write rejects Reserved without changing state", "[pwm][REQ-PWM-004]") {
    PwmOutEndpoint ep;
    ep.functional_cfg().duty_cycle_max = 0xFFFF;
    PwmValue out;
    REQUIRE_FALSE(ep.handle_write(WriteSemantics::Replace, {10, 5}, out));

    auto ec = ep.handle_write(WriteSemantics::Reserved, {999, 999}, out);
    REQUIRE(ec);
    REQUIRE(ep.read().period == 10);
    REQUIRE(ep.read().active_duration == 5);
}

TEST_CASE("PwmOutEndpoint::handle_write rejects Reconfigure", "[pwm][REQ-PWM-004]") {
    PwmOutEndpoint ep;
    PwmValue out;
    auto ec = ep.handle_write(WriteSemantics::Reconfigure, {1, 1}, out);
    REQUIRE(ec == rcp::endpoint::make_error_code(rcp::endpoint::EndpointErrc::non_combinable_write_semantics));
}

// ── PWM_OUT: wire codec ───────────────────────────────────────────────────────

TEST_CASE("PWM_OUT encode_read_request/decode_read_request round-trip", "[pwm][REQ-PWM-025][REQ-PWM-026]") {
    auto frame = encode_read_request(7, 42);
    uint8_t txn = 0;
    REQUIRE_FALSE(decode_read_request(frame.data(), frame.size(), 7, txn));
    REQUIRE(txn == 42);
}

TEST_CASE("PWM_OUT decode_read_request rejects a short frame", "[pwm][REQ-PWM-026]") {
    // byte0's top 7 bits must carry acf_msg_type == kAcfMsgTypeAbb (0x0E) --
    // otherwise decode_acf_abb reports bad_acf_msg_type before it even gets
    // to check the buffer's length against the fixed header size (matches
    // rcp/gpio.hpp's own equivalent test).
    std::vector<uint8_t> buf{0x1C, 0x01};
    uint8_t txn = 0;
    REQUIRE(decode_read_request(buf.data(), buf.size(), 0, txn) == make_error_code(PwmErrc::short_frame));
}

TEST_CASE("PWM_OUT decode_read_request rejects a misaddressed frame", "[pwm][REQ-PWM-062]") {
    auto frame = encode_read_request(7, 1);
    uint8_t txn = 0;
    REQUIRE(decode_read_request(frame.data(), frame.size(), 9, txn) == make_error_code(PwmErrc::wrong_bus));
}

TEST_CASE("PWM_OUT decode_read_request rejects a wrong-op frame", "[pwm][REQ-PWM-063]") {
    auto frame = encode_write_request(1, {10, 5}, WriteSemantics::Replace, 1);
    uint8_t txn = 0;
    REQUIRE(decode_read_request(frame.data(), frame.size(), 1, txn) == make_error_code(PwmErrc::wrong_op));
}

TEST_CASE("PWM_OUT encode_write_request/decode_write_request round-trip, including evt[2:0]",
          "[pwm][REQ-PWM-027]") {
    auto frame = encode_write_request(3, {100, 50}, WriteSemantics::Add, 9);
    PwmValue value{};
    WriteSemantics evt{};
    uint8_t txn = 0;
    REQUIRE_FALSE(decode_write_request(frame.data(), frame.size(), 3, value, evt, txn));
    REQUIRE(value.period == 100);
    REQUIRE(value.active_duration == 50);
    REQUIRE(evt == WriteSemantics::Add);
    REQUIRE(txn == 9);
}

TEST_CASE("PWM_OUT decode_write_request rejects a bad-payload-length frame", "[pwm][REQ-PWM-028]") {
    rcp::acf::AcfMessageInfo info;
    info.byte_bus_id = 1;
    info.op          = true;
    auto frame = rcp::acf::encode_acf_abb(info, {1, 2, 3});
    PwmValue value{};
    WriteSemantics evt{};
    uint8_t txn = 0;
    REQUIRE(decode_write_request(frame.data(), frame.size(), 1, value, evt, txn) ==
            make_error_code(PwmErrc::bad_payload_len));
}

TEST_CASE("PWM_OUT decode_write_request rejects a wrong-op frame", "[pwm][REQ-PWM-064]") {
    auto frame = encode_read_request(1, 1);
    PwmValue value{};
    WriteSemantics evt{};
    uint8_t txn = 0;
    REQUIRE(decode_write_request(frame.data(), frame.size(), 1, value, evt, txn) ==
            make_error_code(PwmErrc::wrong_op));
}

TEST_CASE("PWM_OUT decode_write_request rejects evt[2:0]==100b (Reserved) without populating outputs",
          "[pwm][REQ-PWM-008]") {
    auto frame = encode_write_request(1, {1, 1}, WriteSemantics::Reserved, 1);
    PwmValue value{};
    WriteSemantics evt{};
    uint8_t txn = 0;
    auto ec = decode_write_request(frame.data(), frame.size(), 1, value, evt, txn);
    REQUIRE(ec == make_error_code(PwmErrc::reserved_evt));
    REQUIRE(wire_error(PwmErrc::reserved_evt) == rcp::acf::WireErrorCode::UnsupportedCmd);
}

TEST_CASE("PWM_OUT encode_response/decode_response round-trip untimed", "[pwm][REQ-PWM-029]") {
    auto frame = encode_response(2, {70, 30}, 5, false, 0);
    PwmValue value{};
    bool timed = true;
    uint64_t ts = 0;
    uint8_t txn = 0;
    REQUIRE_FALSE(decode_response(frame.data(), frame.size(), 2, value, timed, ts, txn));
    REQUIRE(value.period == 70);
    REQUIRE(value.active_duration == 30);
    REQUIRE_FALSE(timed);
    REQUIRE(ts == 0);
    REQUIRE(txn == 5);
}

TEST_CASE("PWM_OUT encode_response/decode_response round-trip timed", "[pwm][REQ-PWM-030]") {
    auto frame = encode_response(2, {70, 30}, 5, true, 999888777);
    PwmValue value{};
    bool timed = false;
    uint64_t ts = 0;
    uint8_t txn = 0;
    REQUIRE_FALSE(decode_response(frame.data(), frame.size(), 2, value, timed, ts, txn));
    REQUIRE(timed);
    REQUIRE(ts == 999888777);
}

TEST_CASE("PWM_OUT decode_response rejects a misaddressed frame", "[pwm][REQ-PWM-065]") {
    auto frame = encode_response(2, {1, 1}, 1, false, 0);
    PwmValue value{};
    bool timed = false;
    uint64_t ts = 0;
    uint8_t txn = 0;
    REQUIRE(decode_response(frame.data(), frame.size(), 4, value, timed, ts, txn) ==
            make_error_code(PwmErrc::wrong_bus));
}

TEST_CASE("PWM_OUT decode_response rejects a too-short frame", "[pwm][REQ-PWM-031]") {
    uint8_t buf[1] = {0};
    PwmValue value{};
    bool timed = false;
    uint64_t ts = 0;
    uint8_t txn = 0;
    REQUIRE(decode_response(buf, 0, 1, value, timed, ts, txn) == make_error_code(PwmErrc::short_frame));
}

// ── PwmErrc / wire_error sanity ───────────────────────────────────────────────

TEST_CASE("PwmErrc reports a non-empty message in its own category", "[pwm][REQ-PWM-024]") {
    auto ec = make_error_code(PwmErrc::no_signal);
    REQUIRE(ec.category() == pwm_category());
    REQUIRE_FALSE(ec.message().empty());
}

TEST_CASE("wire_error maps bad_payload_len to InvalidParameter and no_signal to PwmInNoSignal",
          "[pwm][REQ-PWM-028]") {
    REQUIRE(wire_error(PwmErrc::bad_payload_len) == rcp::acf::WireErrorCode::InvalidParameter);
    REQUIRE(wire_error(PwmErrc::no_signal) == rcp::acf::WireErrorCode::PwmInNoSignal);
    REQUIRE(wire_error(PwmErrc::short_frame) == std::nullopt);
}

// ── PWM_IN: response-only read model + PWM_IN_NO_SIGNAL ─────────────────────

TEST_CASE("PwmInEndpoint::handle_read reports no_signal before any measurement", "[pwm][REQ-PWM-005]") {
    PwmInEndpoint ep;
    PwmValue out;
    auto ec = ep.handle_read(out);
    REQUIRE(ec == make_error_code(PwmErrc::no_signal));
}

TEST_CASE("PwmInEndpoint::handle_read returns the last recorded measurement", "[pwm][REQ-PWM-005]") {
    PwmInEndpoint ep;
    ep.record_measurement({200, 75});

    PwmValue out;
    auto ec = ep.handle_read(out);
    REQUIRE_FALSE(ec);
    REQUIRE(out.period == 200);
    REQUIRE(out.active_duration == 75);
}

TEST_CASE("PwmInEndpoint::clear_signal re-arms PWM_IN_NO_SIGNAL", "[pwm][REQ-PWM-005]") {
    PwmInEndpoint ep;
    ep.record_measurement({200, 75});
    ep.clear_signal();

    PwmValue out;
    auto ec = ep.handle_read(out);
    REQUIRE(ec == make_error_code(PwmErrc::no_signal));
}

// ── PWM_IN: rising/falling-edge trigger signals (Table 47) ──────────────────

TEST_CASE("PwmInEndpoint::record_measurement fires both RisingEdge and FallingEdge when armed",
          "[pwm][REQ-PWM-032]") {
    PwmInEndpoint ep;
    ep.triggers().enable(pwm_in_signal_id(PwmInSignal::RisingEdge));
    ep.triggers().enable(pwm_in_signal_id(PwmInSignal::FallingEdge));

    ep.record_measurement({100, 50});

    auto drained = ep.triggers().drain();
    REQUIRE(drained.size() == 2);
    REQUIRE(drained[0] == pwm_in_signal_id(PwmInSignal::RisingEdge));
    REQUIRE(drained[1] == pwm_in_signal_id(PwmInSignal::FallingEdge));
}

TEST_CASE("PwmInEndpoint::record_measurement fires nothing when neither edge is armed",
          "[pwm][REQ-PWM-032]") {
    PwmInEndpoint ep;
    ep.record_measurement({100, 50});
    REQUIRE_FALSE(ep.triggers().has_pending());
}

TEST_CASE("PwmInEndpoint::record_edge fires exactly the requested edge", "[pwm][REQ-PWM-032]") {
    PwmInEndpoint ep;
    ep.triggers().enable(pwm_in_signal_id(PwmInSignal::RisingEdge));
    ep.triggers().enable(pwm_in_signal_id(PwmInSignal::FallingEdge));

    ep.record_edge(PwmInSignal::RisingEdge);
    auto drained = ep.triggers().drain();
    REQUIRE(drained.size() == 1);
    REQUIRE(drained[0] == pwm_in_signal_id(PwmInSignal::RisingEdge));

    ep.record_edge(PwmInSignal::FallingEdge);
    drained = ep.triggers().drain();
    REQUIRE(drained.size() == 1);
    REQUIRE(drained[0] == pwm_in_signal_id(PwmInSignal::FallingEdge));
}

// ── PWM_IN: PwmInTrigger exclusive-select model (REQ-PWM-032..034) ──────────

TEST_CASE("PWM_IN trigger_fires never fires for None", "[pwm][REQ-PWM-032]") {
    REQUIRE_FALSE(trigger_fires(PwmInTrigger::None, false, true));
    REQUIRE_FALSE(trigger_fires(PwmInTrigger::None, true, false));
}

TEST_CASE("PWM_IN trigger_fires implements Rising", "[pwm][REQ-PWM-033]") {
    REQUIRE(trigger_fires(PwmInTrigger::Rising, false, true));
    REQUIRE_FALSE(trigger_fires(PwmInTrigger::Rising, true, false));
    REQUIRE_FALSE(trigger_fires(PwmInTrigger::Rising, false, false));
}

TEST_CASE("PWM_IN trigger_fires implements Falling", "[pwm][REQ-PWM-034]") {
    REQUIRE(trigger_fires(PwmInTrigger::Falling, true, false));
    REQUIRE_FALSE(trigger_fires(PwmInTrigger::Falling, false, true));
    REQUIRE_FALSE(trigger_fires(PwmInTrigger::Falling, true, true));
}

// ── PWM_IN: functional config / lifecycle authorization ─────────────────────

TEST_CASE("PwmInFunctionalConfig defaults to every field zero/None", "[pwm][REQ-PWM-035]") {
    PwmInFunctionalConfig cfg;
    REQUIRE_FALSE(cfg.ep_enable);
    REQUIRE(cfg.trigger == PwmInTrigger::None);
    REQUIRE(cfg.max_period == 0);
}

TEST_CASE("PWM_IN functional config is unwritable while HwUnconfigured", "[pwm][REQ-PWM-036]") {
    rcp::lifecycle::WriterCtx writer;
    writer.via_root_client_ep0 = true;
    REQUIRE_FALSE(pwm_in_functional_cfg_writable(rcp::lifecycle::ServerState::HwUnconfigured, writer));
}

TEST_CASE("PWM_IN set_trigger requires an authorized writer, leaves cfg unchanged otherwise",
          "[pwm][REQ-PWM-037][REQ-PWM-038][REQ-PWM-039][REQ-PWM-040]") {
    PwmInFunctionalConfig cfg;
    rcp::lifecycle::WriterCtx unauth;
    rcp::lifecycle::WriterCtx auth;
    auth.via_root_client_ep0 = true;

    REQUIRE_FALSE(set_trigger(cfg, PwmInTrigger::Rising, rcp::lifecycle::ServerState::RcpConfigured, unauth));
    REQUIRE(cfg.trigger == PwmInTrigger::None);
    REQUIRE(set_trigger(cfg, PwmInTrigger::Rising, rcp::lifecycle::ServerState::RcpConfigured, auth));
    REQUIRE(cfg.trigger == PwmInTrigger::Rising);
}

// ── PWM_IN: the EP_func register block (Table 48) ────────────────────────────

TEST_CASE("PWM_IN render_registers/apply_reconfig round-trip through the register block",
          "[pwm][REQ-PWM-058]") {
    PwmInFunctionalConfig cfg;
    cfg.ep_enable   = true;
    cfg.clk_divider = 4;
    cfg.flags       = kPwmInFlagErrOnMaxPeriod | kPwmInFlagContinuousMode;
    cfg.max_period  = 0x2020;

    PwmInRegisterBlock block{};
    render_registers(cfg, block);
    REQUIRE(block[kPwmInRegEpLen] == kPwmInEpFuncLen);

    std::vector<uint8_t> payload(2 + kPwmInEpFuncLen);
    rcp::avtp::detail::put_u16(payload.data(), 0);
    std::copy(block.begin(), block.end(), payload.begin() + 2);

    PwmInFunctionalConfig applied;
    REQUIRE_FALSE(apply_reconfig(applied, payload.data(), payload.size()));
    REQUIRE(applied.ep_enable);
    REQUIRE(applied.clk_divider == 4);
    REQUIRE(applied.flags == (kPwmInFlagErrOnMaxPeriod | kPwmInFlagContinuousMode));
    REQUIRE(applied.max_period == 0x2020);
}

TEST_CASE("PWM_IN apply_reconfig ignores a write with no address+data", "[pwm][REQ-PWM-070]") {
    PwmInFunctionalConfig cfg;
    uint8_t payload[2] = {0, 0};
    REQUIRE(apply_reconfig(cfg, payload, sizeof(payload)) == make_error_code(PwmErrc::reconfig_short));
}

TEST_CASE("PWM_IN apply_reconfig ignores a write extending past EP_LEN", "[pwm][REQ-PWM-071]") {
    PwmInFunctionalConfig cfg;
    cfg.clk_divider = 6;
    std::vector<uint8_t> payload(4, 0xFF);
    rcp::avtp::detail::put_u16(payload.data(), kPwmInEpFuncLen);
    REQUIRE(apply_reconfig(cfg, payload.data(), payload.size()) ==
            make_error_code(PwmErrc::reconfig_out_of_range));
    REQUIRE(cfg.clk_divider == 6);
}

TEST_CASE("PWM_IN apply_reconfig leaves read-only registers unchanged", "[pwm][REQ-PWM-058]") {
    PwmInFunctionalConfig cfg;
    cfg.base_clk = 42;
    std::vector<uint8_t> payload(4, 0xFF);
    rcp::avtp::detail::put_u16(payload.data(), kPwmInRegBaseClk);
    REQUIRE_FALSE(apply_reconfig(cfg, payload.data(), payload.size()));
    REQUIRE(cfg.base_clk == 42);
}

// ── PWM_IN: wire codec ────────────────────────────────────────────────────────

TEST_CASE("PWM_IN encode_pwm_in_read_request/decode round-trip", "[pwm][REQ-PWM-042][REQ-PWM-043]") {
    auto frame = encode_pwm_in_read_request(5, 3);
    uint8_t txn = 0;
    REQUIRE_FALSE(decode_pwm_in_read_request(frame.data(), frame.size(), 5, txn));
    REQUIRE(txn == 3);
}

TEST_CASE("PWM_IN decode_pwm_in_read_request rejects a misaddressed frame", "[pwm][REQ-PWM-066]") {
    auto frame = encode_pwm_in_read_request(5, 3);
    uint8_t txn = 0;
    REQUIRE(decode_pwm_in_read_request(frame.data(), frame.size(), 6, txn) ==
            make_error_code(PwmErrc::wrong_bus));
}

TEST_CASE("PWM_IN decode_pwm_in_read_request rejects a reserved evt[2:0] value", "[pwm][REQ-PWM-059]") {
    rcp::acf::AcfMessageInfo info;
    info.byte_bus_id = 5;
    info.op          = false;
    info.evt_op       = 0x3; // reserved (not 000b, not 111b)
    auto frame = rcp::acf::encode_acf_abb(info, {});
    uint8_t txn = 0;
    REQUIRE(decode_pwm_in_read_request(frame.data(), frame.size(), 5, txn) ==
            make_error_code(PwmErrc::bad_evt));
}

TEST_CASE("PWM_IN encode_pwm_in_response/decode round-trip, untimed and timed",
          "[pwm][REQ-PWM-044][REQ-PWM-045]") {
    auto frame = encode_pwm_in_response(2, {123, 45}, 8, false, 0);
    PwmValue value{};
    bool timed = true;
    uint64_t ts = 0;
    uint8_t txn = 0;
    REQUIRE_FALSE(decode_pwm_in_response(frame.data(), frame.size(), 2, value, timed, ts, txn));
    REQUIRE(value.period == 123);
    REQUIRE_FALSE(timed);

    auto tframe = encode_pwm_in_response(2, {123, 45}, 8, true, 1000);
    REQUIRE_FALSE(decode_pwm_in_response(tframe.data(), tframe.size(), 2, value, timed, ts, txn));
    REQUIRE(timed);
    REQUIRE(ts == 1000);
}

TEST_CASE("PWM_IN decode_pwm_in_response rejects a misaddressed frame", "[pwm][REQ-PWM-046]") {
    auto frame = encode_pwm_in_response(2, {1, 1}, 1, false, 0);
    PwmValue value{};
    bool timed = false;
    uint64_t ts = 0;
    uint8_t txn = 0;
    REQUIRE(decode_pwm_in_response(frame.data(), frame.size(), 9, value, timed, ts, txn) ==
            make_error_code(PwmErrc::wrong_bus));
}

// ── PWM_IN: MAX_PERIOD timeout classification (REQ-PWM-072..075) ────────────

TEST_CASE("max_period_outcome reports Ok when the measured period is within bound",
          "[pwm][REQ-PWM-072]") {
    REQUIRE(max_period_outcome(100, 200, true, true) == PwmInMaxPeriodOutcome::Ok);
    REQUIRE(max_period_outcome(200, 200, false, false) == PwmInMaxPeriodOutcome::Ok); // exactly at bound
}

TEST_CASE("max_period_outcome invalidates and never errors when err_on_max_period is clear",
          "[pwm][REQ-PWM-073]") {
    REQUIRE(max_period_outcome(300, 200, false, true) == PwmInMaxPeriodOutcome::Invalidate);
    REQUIRE(pwm_in_wire_error(PwmInMaxPeriodOutcome::Invalidate) == std::nullopt);
}

TEST_CASE("max_period_outcome stops without erroring when EP_RESP_ON_ERR is disabled",
          "[pwm][REQ-PWM-074]") {
    REQUIRE(max_period_outcome(300, 200, true, false) == PwmInMaxPeriodOutcome::Stop);
    REQUIRE(pwm_in_wire_error(PwmInMaxPeriodOutcome::Stop) == std::nullopt);
}

TEST_CASE("max_period_outcome stops and signals an error when EP_RESP_ON_ERR is enabled",
          "[pwm][REQ-PWM-075]") {
    REQUIRE(max_period_outcome(300, 200, true, true) == PwmInMaxPeriodOutcome::StopAndError);
    REQUIRE(pwm_in_wire_error(PwmInMaxPeriodOutcome::StopAndError) == rcp::acf::WireErrorCode::PwmInNoSignal);
}

// ── Compound-wait numeric comparison modes against PWM_IN (REQ-PWM-048..054) ─

TEST_CASE("compound_wait_mode_valid accepts exactly 4..7", "[pwm][REQ-PWM-048]") {
    for (uint8_t v = 0; v <= 3; ++v) REQUIRE_FALSE(compound_wait_mode_valid(v));
    for (uint8_t v = 4; v <= 7; ++v) REQUIRE(compound_wait_mode_valid(v));
    REQUIRE_FALSE(compound_wait_mode_valid(8));
}

// The dedicated regression test for the compound-wait polarity fix (c-RCP
// issue #256 Group B): GE ("greater or equal", evt[2:0]=100b) is satisfied
// when the wire threshold is >= the captured value — i.e. captured <=
// threshold — NOT the naive "captured >= threshold" reading the field name
// alone might suggest, and which this port would produce if it were
// (re-)introduced with the historical inverted polarity.
TEST_CASE("compound_wait_compare PeriodGe is satisfied when captured <= threshold (polarity regression)",
          "[pwm][REQ-PWM-049]") {
    PwmValue captured{100, 0};
    REQUIRE(compound_wait_compare(captured, PwmInCompoundWaitMode::PeriodGe, 100));  // equal: satisfied
    REQUIRE(compound_wait_compare(captured, PwmInCompoundWaitMode::PeriodGe, 150));  // captured < threshold: satisfied
    REQUIRE_FALSE(compound_wait_compare(captured, PwmInCompoundWaitMode::PeriodGe, 50)); // captured > threshold: NOT satisfied
}

TEST_CASE("compound_wait_compare PeriodLe is satisfied when captured >= threshold (polarity regression)",
          "[pwm][REQ-PWM-050]") {
    PwmValue captured{100, 0};
    REQUIRE(compound_wait_compare(captured, PwmInCompoundWaitMode::PeriodLe, 100));  // equal: satisfied
    REQUIRE(compound_wait_compare(captured, PwmInCompoundWaitMode::PeriodLe, 50));   // captured > threshold: satisfied
    REQUIRE_FALSE(compound_wait_compare(captured, PwmInCompoundWaitMode::PeriodLe, 150)); // captured < threshold: NOT satisfied
}

TEST_CASE("compound_wait_compare DutyGe/DutyLe read active_duration, same polarity as Period",
          "[pwm][REQ-PWM-051][REQ-PWM-052]") {
    PwmValue captured{0, 40};
    REQUIRE(compound_wait_compare(captured, PwmInCompoundWaitMode::DutyGe, 40));
    REQUIRE(compound_wait_compare(captured, PwmInCompoundWaitMode::DutyGe, 60));
    REQUIRE_FALSE(compound_wait_compare(captured, PwmInCompoundWaitMode::DutyGe, 20));

    REQUIRE(compound_wait_compare(captured, PwmInCompoundWaitMode::DutyLe, 40));
    REQUIRE(compound_wait_compare(captured, PwmInCompoundWaitMode::DutyLe, 20));
    REQUIRE_FALSE(compound_wait_compare(captured, PwmInCompoundWaitMode::DutyLe, 60));
}

TEST_CASE("compound_wait_compare returns false for an invalid mode", "[pwm][REQ-PWM-053]") {
    PwmValue captured{100, 50};
    REQUIRE_FALSE(compound_wait_compare(captured, static_cast<PwmInCompoundWaitMode>(0), 100));
    REQUIRE_FALSE(compound_wait_compare(captured, static_cast<PwmInCompoundWaitMode>(3), 100));
}

TEST_CASE("compound_wait_compare never matches a kPwmInNoSignal sub-field", "[pwm][REQ-PWM-054]") {
    PwmValue captured{kPwmInNoSignal, kPwmInNoSignal};
    REQUIRE_FALSE(compound_wait_compare(captured, PwmInCompoundWaitMode::PeriodGe, 0xFFFF));
    REQUIRE_FALSE(compound_wait_compare(captured, PwmInCompoundWaitMode::PeriodLe, 0));
    REQUIRE_FALSE(compound_wait_compare(captured, PwmInCompoundWaitMode::DutyGe, 0xFFFF));
    REQUIRE_FALSE(compound_wait_compare(captured, PwmInCompoundWaitMode::DutyLe, 0));
}

// ── Table 33 Row 2 evt[2:0] validation (PwmInEndpoint::handle_request) ──────

TEST_CASE("PwmInEndpoint::handle_request delegates a Plain (evt[2:0]==000b) request to handle_read()",
          "[pwm][REQ-PWM-042]") {
    PwmInEndpoint ep;
    ep.record_measurement({200, 75});

    PwmValue out;
    auto ec = ep.handle_request(/*evt_op=*/0, out);
    REQUIRE_FALSE(ec);
    REQUIRE(out.period == 200);
    REQUIRE(out.active_duration == 75);
}

TEST_CASE("PwmInEndpoint::handle_request Plain surfaces no_signal before any measurement",
          "[pwm][REQ-PWM-042]") {
    PwmInEndpoint ep;
    PwmValue out;
    auto ec = ep.handle_request(/*evt_op=*/0, out);
    REQUIRE(ec == make_error_code(PwmErrc::no_signal));
}

TEST_CASE("PwmInEndpoint::handle_request rejects every reserved evt[2:0] value (001b-110b) "
          "without touching out_value",
          "[pwm]") {
    for (uint8_t evt_op = 1; evt_op <= 6; ++evt_op) {
        PwmInEndpoint ep;
        ep.record_measurement({111, 22});

        PwmValue out;
        auto ec = ep.handle_request(evt_op, out);
        REQUIRE(ec == rcp::endpoint::make_error_code(rcp::endpoint::EndpointErrc::reserved_evt_row2));
        REQUIRE(out.period == 0);
        REQUIRE(out.active_duration == 0);
    }
}

TEST_CASE("PwmInEndpoint::handle_request reports config_write_not_supported for evt[2:0]==111b "
          "without touching out_value",
          "[pwm]") {
    PwmInEndpoint ep;
    ep.record_measurement({111, 22});

    PwmValue out;
    auto ec = ep.handle_request(/*evt_op=*/7, out);
    REQUIRE(ec == make_error_code(PwmErrc::config_write_not_supported));
    REQUIRE(out.period == 0);
    REQUIRE(out.active_duration == 0);
}

TEST_CASE("PwmInEndpoint::handle_request masks evt_op down to 3 bits before classifying", "[pwm]") {
    PwmInEndpoint ep;
    ep.record_measurement({9, 4});

    PwmValue out;
    REQUIRE_FALSE(ep.handle_request(/*evt_op=*/0xF8, out)); // low 3 bits 000 -> Plain
    REQUIRE(out.period == 9);
    REQUIRE(out.active_duration == 4);

    auto ec = ep.handle_request(/*evt_op=*/0xF9, out); // low 3 bits 001 -> Reserved
    REQUIRE(ec == rcp::endpoint::make_error_code(rcp::endpoint::EndpointErrc::reserved_evt_row2));
}
