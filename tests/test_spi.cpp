// fusa:test REQ-SPI-001
// fusa:test REQ-SPI-002
// fusa:test REQ-SPI-003
// fusa:test REQ-SPI-004
// fusa:test REQ-SPI-005
// fusa:test REQ-SPI-006
// fusa:test REQ-SPI-007
// fusa:test REQ-SPI-008
// fusa:test REQ-SPI-009
// fusa:test REQ-SPI-010
// fusa:test REQ-SPI-011
// fusa:test REQ-SPI-012
// fusa:test REQ-SPI-013
// fusa:test REQ-SPI-014
// fusa:test REQ-SPI-015
// fusa:test REQ-SPI-016
// fusa:test REQ-SPI-017
// fusa:test REQ-SPI-018
// fusa:test REQ-SPI-019
// fusa:test REQ-SPI-020
// fusa:test REQ-SPI-021
// fusa:test REQ-SPI-022
// fusa:test REQ-SPI-023
// fusa:test REQ-SPI-024
// fusa:test REQ-SPI-025
// fusa:test REQ-SPI-026
// fusa:test REQ-SPI-027
// fusa:test REQ-SPI-028
// fusa:test REQ-SPI-029
// fusa:test REQ-SPI-030
// fusa:test REQ-SPI-033
// fusa:test REQ-SPI-034
// fusa:test REQ-SPI-035
// fusa:test REQ-SPI-036
// fusa:test REQ-SPI-038
// fusa:test REQ-SPI-039
// fusa:test REQ-SPI-040
// fusa:test REQ-SPI-041
// fusa:test REQ-SPI-042
// fusa:test REQ-SPI-043
// fusa:test REQ-SPI-044

// Tests for rcp/spi.hpp — the SPI endpoint type (ep_type 0x03), ported from
// c-RCP's tests/test_ep_spi.c (this project's RC5-spec-conformant reference)
// as part of Phase 3 of the ground-up rewrite (cpp-RCP issue #129,
// ROADMAP.md "Phase 17"), plus this codebase's own pre-existing
// SpiEndpoint-convenience-class coverage, re-verified rather than dropped.

#include <catch2/catch_test_macros.hpp>
#include <rcp/acf.hpp>
#include <rcp/lifecycle.hpp>
#include <rcp/spi.hpp>

using namespace rcp::spi;

// ── Channel addressing ────────────────────────────────────────────────────────

TEST_CASE("kMaxChannels is 6", "[spi][REQ-SPI-002]") {
    REQUIRE(kMaxChannels == 6);
}

TEST_CASE("channel_valid bounds channel indices to 0..5", "[spi][REQ-SPI-002]") {
    REQUIRE(channel_valid(0));
    REQUIRE(channel_valid(5));
    REQUIRE_FALSE(channel_valid(6));
    REQUIRE_FALSE(channel_valid(255));
}

TEST_CASE("channel_of accepts evt[2:0] values 0..5", "[spi][REQ-SPI-033]") {
    for (uint8_t v = 0; v < kMaxChannels; ++v) {
        uint8_t out = 0xFF;
        auto    ec  = channel_of(v, out);
        REQUIRE_FALSE(ec);
        REQUIRE(out == v);
    }
}

TEST_CASE("channel_of rejects evt[2:0] values 6 and 7", "[spi][REQ-SPI-033]") {
    uint8_t out = 0xFF;
    REQUIRE(channel_of(6, out) == make_error_code(SpiErrc::bad_channel));
    REQUIRE(channel_of(7, out) == make_error_code(SpiErrc::bad_channel));
}

TEST_CASE("channel_of masks its input down to 3 bits before range-checking", "[spi][REQ-SPI-033]") {
    uint8_t out = 0xFF;
    auto    ec  = channel_of(0xF9, out); // low 3 bits = 1
    REQUIRE_FALSE(ec);
    REQUIRE(out == 1);
}

// ── Clock mode ─────────────────────────────────────────────────────────────────

TEST_CASE("mode_valid accepts exactly 0..3", "[spi][REQ-SPI-003]") {
    for (uint8_t v = 0; v <= 3; ++v) REQUIRE(mode_valid(v));
    REQUIRE_FALSE(mode_valid(4));
    REQUIRE_FALSE(mode_valid(255));
}

TEST_CASE("mode_cpol derives CPOL correctly for all 4 modes", "[spi][REQ-SPI-004]") {
    REQUIRE_FALSE(mode_cpol(SpiMode::Mode0));
    REQUIRE_FALSE(mode_cpol(SpiMode::Mode1));
    REQUIRE(mode_cpol(SpiMode::Mode2));
    REQUIRE(mode_cpol(SpiMode::Mode3));
}

TEST_CASE("mode_cpha derives CPHA correctly for all 4 modes", "[spi][REQ-SPI-005]") {
    REQUIRE_FALSE(mode_cpha(SpiMode::Mode0));
    REQUIRE(mode_cpha(SpiMode::Mode1));
    REQUIRE_FALSE(mode_cpha(SpiMode::Mode2));
    REQUIRE(mode_cpha(SpiMode::Mode3));
}

// ── Per-channel trigger signals ────────────────────────────────────────────────

TEST_CASE("trigger_fires never fires for SpiTrigger::None", "[spi][REQ-SPI-006]") {
    REQUIRE_FALSE(trigger_fires(SpiTrigger::None, SpiEvent::TransferDone));
    REQUIRE_FALSE(trigger_fires(SpiTrigger::None, SpiEvent::CsAssert));
    REQUIRE_FALSE(trigger_fires(SpiTrigger::None, SpiEvent::CsDeassert));
}

TEST_CASE("trigger_fires implements TransferDone", "[spi][REQ-SPI-007]") {
    REQUIRE(trigger_fires(SpiTrigger::TransferDone, SpiEvent::TransferDone));
    REQUIRE_FALSE(trigger_fires(SpiTrigger::TransferDone, SpiEvent::CsAssert));
    REQUIRE_FALSE(trigger_fires(SpiTrigger::TransferDone, SpiEvent::CsDeassert));
}

TEST_CASE("trigger_fires implements CsAssert", "[spi][REQ-SPI-008]") {
    REQUIRE(trigger_fires(SpiTrigger::CsAssert, SpiEvent::CsAssert));
    REQUIRE_FALSE(trigger_fires(SpiTrigger::CsAssert, SpiEvent::TransferDone));
    REQUIRE_FALSE(trigger_fires(SpiTrigger::CsAssert, SpiEvent::CsDeassert));
}

TEST_CASE("trigger_fires implements CsDeassert", "[spi][REQ-SPI-009]") {
    REQUIRE(trigger_fires(SpiTrigger::CsDeassert, SpiEvent::CsDeassert));
    REQUIRE_FALSE(trigger_fires(SpiTrigger::CsDeassert, SpiEvent::TransferDone));
    REQUIRE_FALSE(trigger_fires(SpiTrigger::CsDeassert, SpiEvent::CsAssert));
}

// ── Table 41 trigger-signal numbering ─────────────────────────────────────────

TEST_CASE("trigger_signal_number implements Table 41's 2+2n/3+2n CS numbering", "[spi][REQ-SPI-034]") {
    // CSn asserted -> signal 2+2n.
    REQUIRE(trigger_signal_number(0, SpiTrigger::CsAssert) == 2);
    REQUIRE(trigger_signal_number(3, SpiTrigger::CsAssert) == 8);
    REQUIRE(trigger_signal_number(5, SpiTrigger::CsAssert) == 12);
    // CSn de-asserted -> signal 3+2n.
    REQUIRE(trigger_signal_number(0, SpiTrigger::CsDeassert) == 3);
    REQUIRE(trigger_signal_number(3, SpiTrigger::CsDeassert) == 9);
    REQUIRE(trigger_signal_number(5, SpiTrigger::CsDeassert) == 13);
}

TEST_CASE("trigger_signal_number has no signal for TransferDone (signal 0 is whole-endpoint) or None",
          "[spi][REQ-SPI-034]") {
    REQUIRE_FALSE(trigger_signal_number(0, SpiTrigger::TransferDone).has_value());
    REQUIRE_FALSE(trigger_signal_number(0, SpiTrigger::None).has_value());
}

TEST_CASE("trigger_signal_number rejects a channel >= kMaxChannels", "[spi][REQ-SPI-034]") {
    REQUIRE_FALSE(trigger_signal_number(6, SpiTrigger::CsAssert).has_value());
    REQUIRE_FALSE(trigger_signal_number(255, SpiTrigger::CsDeassert).has_value());
}

// ── Functional config ─────────────────────────────────────────────────────────

TEST_CASE("functional_cfg_init zeroes every field", "[spi][REQ-SPI-010]") {
    SpiFunctionalCfg cfg;
    cfg.ep_enable     = true;
    cfg.ep_status     = 0xBEEF;
    cfg.channels[0].mode              = SpiMode::Mode3;
    cfg.channels[0].bit_order         = SpiBitOrder::LsbFirst;
    cfg.channels[0].cs_polarity       = SpiCsPolarity::ActiveHigh;
    cfg.channels[0].trigger           = SpiTrigger::CsAssert;
    cfg.channels[0].clock_divider     = 7;
    cfg.channels[0].use_common_cs     = true;
    cfg.channels[0].deassert_cs_pause = true;

    functional_cfg_init(cfg);

    REQUIRE_FALSE(cfg.ep_enable);
    REQUIRE_FALSE(cfg.ep_clear_req_storage);
    REQUIRE_FALSE(cfg.ep_req_crc_enable);
    REQUIRE_FALSE(cfg.ep_response_ts_enable);
    REQUIRE_FALSE(cfg.ep_suppress_response);
    REQUIRE(cfg.ep_status == 0);

    for (uint8_t i = 0; i < kMaxChannels; ++i) {
        const auto& ch = cfg.channels[i];
        REQUIRE(ch.mode == SpiMode::Mode0);
        REQUIRE(ch.bit_order == SpiBitOrder::MsbFirst);
        REQUIRE(ch.cs_polarity == SpiCsPolarity::ActiveLow);
        REQUIRE(ch.trigger == SpiTrigger::None);
        REQUIRE(ch.clock_divider == 0);
        REQUIRE(ch.inter_byte_delay_ns == 0);
        REQUIRE(ch.inter_transfer_delay_ns == 0);
        REQUIRE(ch.baud_rate_kbps == 0);
        REQUIRE_FALSE(ch.use_common_cs);
        REQUIRE(ch.cs_clk_leadtime == 0);
        REQUIRE(ch.clk_cs_trailtime == 0);
        REQUIRE(ch.bits_max == 0);
        REQUIRE(ch.pause_min == 0);
        REQUIRE_FALSE(ch.deassert_cs_pause);
    }
}

TEST_CASE("functional_cfg_writable is false in HwUnconfigured regardless of writer", "[spi][REQ-SPI-011]") {
    rcp::lifecycle::WriterCtx writer;
    writer.via_root_client_ep0 = true;
    writer.via_owning_stream   = true;
    REQUIRE_FALSE(functional_cfg_writable(rcp::lifecycle::ServerState::HwUnconfigured, writer));
}

TEST_CASE("functional_cfg_writable in HwConfigured requires EP0/owning-stream/discovery-stream",
          "[spi][REQ-SPI-012]") {
    rcp::lifecycle::WriterCtx none, via_ep0, via_stream, via_discovery;
    via_ep0.via_root_client_ep0        = true;
    via_stream.via_owning_stream       = true;
    via_discovery.via_discovery_stream = true;

    REQUIRE_FALSE(functional_cfg_writable(rcp::lifecycle::ServerState::HwConfigured, none));
    REQUIRE(functional_cfg_writable(rcp::lifecycle::ServerState::HwConfigured, via_ep0));
    REQUIRE(functional_cfg_writable(rcp::lifecycle::ServerState::HwConfigured, via_stream));
    REQUIRE(functional_cfg_writable(rcp::lifecycle::ServerState::HwConfigured, via_discovery));
}

TEST_CASE("functional_cfg_writable in RcpConfigured requires EP0/owning-stream, not discovery",
          "[spi][REQ-SPI-013]") {
    rcp::lifecycle::WriterCtx none, via_ep0, via_stream;
    via_ep0.via_root_client_ep0  = true;
    via_stream.via_owning_stream = true;

    REQUIRE_FALSE(functional_cfg_writable(rcp::lifecycle::ServerState::RcpConfigured, none));
    REQUIRE(functional_cfg_writable(rcp::lifecycle::ServerState::RcpConfigured, via_ep0));
    REQUIRE(functional_cfg_writable(rcp::lifecycle::ServerState::RcpConfigured, via_stream));
}

// ── Per-channel setters ────────────────────────────────────────────────────────

TEST_CASE("set_channel_mode rejects an invalid channel or an unauthorized write without mutating cfg",
          "[spi][REQ-SPI-014]") {
    SpiFunctionalCfg cfg;
    functional_cfg_init(cfg);
    rcp::lifecycle::WriterCtx authorized, none;
    authorized.via_root_client_ep0 = true;

    REQUIRE_FALSE(set_channel_mode(cfg, 6, SpiMode::Mode3, rcp::lifecycle::ServerState::HwConfigured, authorized));
    REQUIRE(cfg.channels[0].mode == SpiMode::Mode0);

    REQUIRE_FALSE(set_channel_mode(cfg, 0, SpiMode::Mode3, rcp::lifecycle::ServerState::RcpConfigured, none));
    REQUIRE(cfg.channels[0].mode == SpiMode::Mode0);
}

TEST_CASE("set_channel_mode applies the write when authorized", "[spi][REQ-SPI-015]") {
    SpiFunctionalCfg cfg;
    functional_cfg_init(cfg);
    rcp::lifecycle::WriterCtx writer;
    writer.via_owning_stream = true;

    REQUIRE(set_channel_mode(cfg, 3, SpiMode::Mode2, rcp::lifecycle::ServerState::HwConfigured, writer));
    REQUIRE(cfg.channels[3].mode == SpiMode::Mode2);
}

TEST_CASE("set_channel_bit_order rejects an invalid channel or an unauthorized write without mutating cfg",
          "[spi][REQ-SPI-016]") {
    SpiFunctionalCfg cfg;
    functional_cfg_init(cfg);
    rcp::lifecycle::WriterCtx none;

    REQUIRE_FALSE(set_channel_bit_order(cfg, 6, SpiBitOrder::LsbFirst, rcp::lifecycle::ServerState::HwConfigured,
                                         none));
    REQUIRE_FALSE(set_channel_bit_order(cfg, 0, SpiBitOrder::LsbFirst, rcp::lifecycle::ServerState::HwUnconfigured,
                                         none));
    REQUIRE(cfg.channels[0].bit_order == SpiBitOrder::MsbFirst);
}

TEST_CASE("set_channel_bit_order applies the write when authorized", "[spi][REQ-SPI-017]") {
    SpiFunctionalCfg cfg;
    functional_cfg_init(cfg);
    rcp::lifecycle::WriterCtx writer;
    writer.via_owning_stream = true;

    REQUIRE(set_channel_bit_order(cfg, 1, SpiBitOrder::LsbFirst, rcp::lifecycle::ServerState::HwConfigured, writer));
    REQUIRE(cfg.channels[1].bit_order == SpiBitOrder::LsbFirst);
}

TEST_CASE("set_channel_cs_polarity rejects an invalid channel or an unauthorized write without mutating cfg",
          "[spi][REQ-SPI-018]") {
    SpiFunctionalCfg cfg;
    functional_cfg_init(cfg);
    rcp::lifecycle::WriterCtx none;

    REQUIRE_FALSE(set_channel_cs_polarity(cfg, 6, SpiCsPolarity::ActiveHigh,
                                           rcp::lifecycle::ServerState::HwConfigured, none));
    REQUIRE_FALSE(set_channel_cs_polarity(cfg, 0, SpiCsPolarity::ActiveHigh,
                                           rcp::lifecycle::ServerState::HwUnconfigured, none));
    REQUIRE(cfg.channels[0].cs_polarity == SpiCsPolarity::ActiveLow);
}

TEST_CASE("set_channel_cs_polarity applies the write when authorized", "[spi][REQ-SPI-019]") {
    SpiFunctionalCfg cfg;
    functional_cfg_init(cfg);
    rcp::lifecycle::WriterCtx writer;
    writer.via_owning_stream = true;

    REQUIRE(set_channel_cs_polarity(cfg, 2, SpiCsPolarity::ActiveHigh, rcp::lifecycle::ServerState::HwConfigured,
                                     writer));
    REQUIRE(cfg.channels[2].cs_polarity == SpiCsPolarity::ActiveHigh);
}

TEST_CASE("set_channel_clock_divider rejects an invalid channel or an unauthorized write without mutating cfg",
          "[spi][REQ-SPI-020]") {
    SpiFunctionalCfg cfg;
    functional_cfg_init(cfg);
    rcp::lifecycle::WriterCtx none;

    REQUIRE_FALSE(set_channel_clock_divider(cfg, 6, 128, rcp::lifecycle::ServerState::HwConfigured, none));
    REQUIRE_FALSE(set_channel_clock_divider(cfg, 0, 128, rcp::lifecycle::ServerState::HwUnconfigured, none));
    REQUIRE(cfg.channels[0].clock_divider == 0);
}

TEST_CASE("set_channel_clock_divider applies the write when authorized", "[spi][REQ-SPI-021]") {
    SpiFunctionalCfg cfg;
    functional_cfg_init(cfg);
    rcp::lifecycle::WriterCtx writer;
    writer.via_owning_stream = true;

    REQUIRE(set_channel_clock_divider(cfg, 4, 256, rcp::lifecycle::ServerState::HwConfigured, writer));
    REQUIRE(cfg.channels[4].clock_divider == 256);
}

TEST_CASE("set_channel_timing rejects an invalid channel or an unauthorized write without mutating cfg",
          "[spi][REQ-SPI-022]") {
    SpiFunctionalCfg cfg;
    functional_cfg_init(cfg);
    rcp::lifecycle::WriterCtx none;

    REQUIRE_FALSE(set_channel_timing(cfg, 6, 100, 200, rcp::lifecycle::ServerState::HwConfigured, none));
    REQUIRE_FALSE(set_channel_timing(cfg, 0, 100, 200, rcp::lifecycle::ServerState::HwUnconfigured, none));
    REQUIRE(cfg.channels[0].inter_byte_delay_ns == 0);
    REQUIRE(cfg.channels[0].inter_transfer_delay_ns == 0);
}

TEST_CASE("set_channel_timing applies the write when authorized", "[spi][REQ-SPI-023]") {
    SpiFunctionalCfg cfg;
    functional_cfg_init(cfg);
    rcp::lifecycle::WriterCtx writer;
    writer.via_owning_stream = true;

    REQUIRE(set_channel_timing(cfg, 5, 50, 500, rcp::lifecycle::ServerState::HwConfigured, writer));
    REQUIRE(cfg.channels[5].inter_byte_delay_ns == 50);
    REQUIRE(cfg.channels[5].inter_transfer_delay_ns == 500);
}

TEST_CASE("set_channel_trigger rejects an invalid channel or an unauthorized write without mutating cfg",
          "[spi][REQ-SPI-024]") {
    SpiFunctionalCfg cfg;
    functional_cfg_init(cfg);
    rcp::lifecycle::WriterCtx none;

    REQUIRE_FALSE(set_channel_trigger(cfg, 6, SpiTrigger::TransferDone, rcp::lifecycle::ServerState::HwConfigured,
                                       none));
    REQUIRE_FALSE(set_channel_trigger(cfg, 0, SpiTrigger::TransferDone, rcp::lifecycle::ServerState::HwUnconfigured,
                                       none));
    REQUIRE(cfg.channels[0].trigger == SpiTrigger::None);
}

TEST_CASE("set_channel_trigger applies the write when authorized", "[spi][REQ-SPI-025]") {
    SpiFunctionalCfg cfg;
    functional_cfg_init(cfg);
    rcp::lifecycle::WriterCtx writer;
    writer.via_owning_stream = true;

    REQUIRE(set_channel_trigger(cfg, 0, SpiTrigger::CsAssert, rcp::lifecycle::ServerState::HwConfigured, writer));
    REQUIRE(cfg.channels[0].trigger == SpiTrigger::CsAssert);
}

// ── The EP_func register block ────────────────────────────────────────────────

TEST_CASE("render_registers matches Table 42 offsets, including the nr_cs 4-bit (count-1) encoding",
          "[spi][REQ-SPI-035][REQ-SPI-038][REQ-SPI-040]") {
    SpiFunctionalCfg cfg;
    functional_cfg_init(cfg);
    cfg.ep_enable                     = true;
    cfg.ep_status                     = 0x1234;
    cfg.channels[0].mode              = SpiMode::Mode3; // cpol=1, cpha=1
    cfg.channels[0].cs_polarity       = SpiCsPolarity::ActiveHigh;
    cfg.channels[0].use_common_cs     = true;
    cfg.channels[0].baud_rate_kbps    = 0x5566;
    cfg.channels[0].cs_clk_leadtime   = 3;
    cfg.channels[0].clk_cs_trailtime  = 4;
    cfg.channels[0].bits_max          = 5;
    cfg.channels[0].pause_min         = 6;
    cfg.channels[0].deassert_cs_pause = true;

    SpiRegisterBlock out{};
    render_registers(cfg, out);

    REQUIRE(out[kRegEpLen] == static_cast<uint8_t>(kEpFuncLen));

    // TC18 0.5.1_RC5: spi_nr_cs is a 4-bit "(count - 1)" field, upper
    // nibble reserved — kMaxChannels (6) renders as 0x05, not a plain 6.
    REQUIRE(out[kRegNrCs] == static_cast<uint8_t>(kMaxChannels - 1u));
    REQUIRE((out[kRegNrCs] & 0xF0u) == 0u); // reserved nibble

    REQUIRE((out[kRegEpEnableClr] & 0x01u) != 0u);
    REQUIRE(out[kRegEpStatus] == 0x12u);
    REQUIRE(out[kRegEpStatus + 1] == 0x34u);

    // Channel 0's own block starts at 0x0006.
    REQUIRE(out[0x0006] == 0x55u);
    REQUIRE(out[0x0007] == 0x66u);
    REQUIRE(out[0x0008] == (kCfgBitClkPolarity | kCfgBitClkPhase | kCfgBitCsPolarity | kCfgBitUseCs |
                             kCfgBitDeassertCsPause));
    REQUIRE(out[0x0009] == 3);
    REQUIRE(out[0x000A] == 4);
    REQUIRE(out[0x000B] == 5);
    REQUIRE(out[0x000C] == 6);
    REQUIRE(out[0x000D] == 0); // channel 0's reserved octet

    // Channel 1's own block starts at 0x0006 + 8 = 0x000E.
    REQUIRE(static_cast<uint16_t>(kRegChannelBase + 1u * kRegChannelSpan) == 0x000Eu);
    REQUIRE(kEpFuncLen == 0x0036u);
}

TEST_CASE("apply_reconfig writes baud rate", "[spi][REQ-SPI-039]") {
    SpiFunctionalCfg cfg;
    functional_cfg_init(cfg);

    uint8_t payload[4] = {0x00, 0x0E, 0x12, 0x34}; // channel 1's baud_rate

    REQUIRE_FALSE(apply_reconfig(cfg, payload, sizeof(payload)));
    REQUIRE(cfg.channels[1].baud_rate_kbps == 0x1234);
    REQUIRE(cfg.channels[0].baud_rate_kbps == 0); // untouched
}

// TC18 0.5.1_RC5, ticket NXP_100: spi_deassert_cs_pauseN is bit 4 of a
// channel's own +0x02 cfg octet — proves it round-trips through the parse
// path (apply_reconfig(), not just render), and that the other three cfg
// bits are unaffected by setting or clearing it.
TEST_CASE("apply_reconfig writes the deassert_cs_pause bit without disturbing sibling cfg bits",
          "[spi][REQ-SPI-039][REQ-SPI-040]") {
    SpiFunctionalCfg cfg;
    functional_cfg_init(cfg);

    uint8_t payload[3] = {0x00, 0x08, static_cast<uint8_t>(kCfgBitDeassertCsPause | kCfgBitClkPhase)};

    REQUIRE_FALSE(apply_reconfig(cfg, payload, sizeof(payload)));
    REQUIRE(cfg.channels[0].deassert_cs_pause);
    REQUIRE(cfg.channels[0].mode == SpiMode::Mode1); // cpha only
    REQUIRE_FALSE(cfg.channels[0].use_common_cs);
    REQUIRE_FALSE(cfg.channels[1].deassert_cs_pause); // untouched

    payload[2] = 0x00;
    REQUIRE_FALSE(apply_reconfig(cfg, payload, sizeof(payload)));
    REQUIRE_FALSE(cfg.channels[0].deassert_cs_pause);
}

TEST_CASE("apply_reconfig derives Mode2 and Mode3 from the cpol/cpha bits", "[spi][REQ-SPI-039]") {
    SpiFunctionalCfg cfg;
    functional_cfg_init(cfg);

    uint8_t payload[3] = {0x00, 0x08, kCfgBitClkPolarity}; // cpol=1, cpha=0 -> Mode2
    REQUIRE_FALSE(apply_reconfig(cfg, payload, sizeof(payload)));
    REQUIRE(cfg.channels[0].mode == SpiMode::Mode2);

    payload[2] = static_cast<uint8_t>(kCfgBitClkPolarity | kCfgBitClkPhase); // cpol=1, cpha=1 -> Mode3
    REQUIRE_FALSE(apply_reconfig(cfg, payload, sizeof(payload)));
    REQUIRE(cfg.channels[0].mode == SpiMode::Mode3);
}

TEST_CASE("apply_reconfig writes a span covering multiple channels", "[spi][REQ-SPI-039]") {
    SpiFunctionalCfg cfg;
    functional_cfg_init(cfg);

    uint8_t payload[2 + 16] = {
        0x00, 0x06,                         // address = channel 0's block base
        0xAA, 0xBB,                         // baud_rate0
        0x00,                               // cfg byte -- all bits clear
        1, 2, 3, 4, 0xFF,                   // leadtime/trailtime/bits_max/pause_min/reserved(ignored)
        0xCC, 0xDD,                         // baud_rate1
        0x00,
        5, 6, 7, 8, 0xFF,
    };

    REQUIRE_FALSE(apply_reconfig(cfg, payload, sizeof(payload)));
    REQUIRE(cfg.channels[0].baud_rate_kbps == 0xAABB);
    REQUIRE(cfg.channels[0].cs_clk_leadtime == 1);
    REQUIRE(cfg.channels[0].clk_cs_trailtime == 2);
    REQUIRE(cfg.channels[0].bits_max == 3);
    REQUIRE(cfg.channels[0].pause_min == 4);
    REQUIRE(cfg.channels[1].baud_rate_kbps == 0xCCDD);
    REQUIRE(cfg.channels[1].cs_clk_leadtime == 5);
    REQUIRE(cfg.channels[1].clk_cs_trailtime == 6);
    REQUIRE(cfg.channels[1].bits_max == 7);
    REQUIRE(cfg.channels[1].pause_min == 8);
}

TEST_CASE("apply_reconfig ignores the read-only EP_LEN/NR_CS registers", "[spi][REQ-SPI-039]") {
    SpiFunctionalCfg cfg;
    functional_cfg_init(cfg);

    uint8_t payload[2 + 2] = {0x00, 0x00, 0xFF, 0xFF}; // covers EP_LEN(0x00) and NR_CS(0x01)

    REQUIRE_FALSE(apply_reconfig(cfg, payload, sizeof(payload)));

    SpiRegisterBlock out{};
    render_registers(cfg, out);
    REQUIRE(out[kRegEpLen] == static_cast<uint8_t>(kEpFuncLen));
    REQUIRE(out[kRegNrCs] == static_cast<uint8_t>(kMaxChannels - 1u));
}

TEST_CASE("apply_reconfig ignores a channel's own reserved octet", "[spi][REQ-SPI-039]") {
    SpiFunctionalCfg cfg;
    functional_cfg_init(cfg);

    uint8_t payload[3] = {0x00, 0x0D, 0xFF}; // channel 0's own reserved octet

    REQUIRE_FALSE(apply_reconfig(cfg, payload, sizeof(payload)));

    SpiRegisterBlock out{};
    render_registers(cfg, out);
    REQUIRE(out[0x000D] == 0);
}

TEST_CASE("apply_reconfig rejects a write past EP_LEN", "[spi][REQ-SPI-039]") {
    SpiFunctionalCfg cfg;
    functional_cfg_init(cfg);

    uint8_t payload[3] = {0x00, 0x36, 0xFF}; // == kEpFuncLen, one past the last valid offset

    REQUIRE(apply_reconfig(cfg, payload, sizeof(payload)) == make_error_code(SpiReconfigErrc::out_of_range));
    REQUIRE(cfg.channels[5].baud_rate_kbps == 0);
}

TEST_CASE("apply_reconfig rejects a payload with no data octet", "[spi][REQ-SPI-039]") {
    SpiFunctionalCfg cfg;
    functional_cfg_init(cfg);

    uint8_t addr_only[2] = {0x00, 0x06};

    REQUIRE(apply_reconfig(cfg, addr_only, sizeof(addr_only)) ==
            make_error_code(SpiReconfigErrc::short_payload));
    REQUIRE(apply_reconfig(cfg, nullptr, 0) == make_error_code(SpiReconfigErrc::short_payload));
}

TEST_CASE("reconfig category reports a non-empty message for every known and an unknown code",
          "[spi][REQ-SPI-043]") {
    for (int code : {1, 2}) {
        auto ec = std::error_code(code, spi_reconfig_category());
        REQUIRE_FALSE(ec.message().empty());
    }
    auto unknown = std::error_code(99, spi_reconfig_category());
    REQUIRE_FALSE(unknown.message().empty());
}

TEST_CASE("encode_reconfig_request round-trips through acf::decode_acf_abb", "[spi][REQ-SPI-042]") {
    std::vector<uint8_t> data{0xAB, 0xCD};
    auto                 frame = encode_reconfig_request(0x03, 0x0006, data, 7);
    REQUIRE_FALSE(frame.empty());

    rcp::acf::AcfMessageInfo hdr;
    std::vector<uint8_t>     payload;
    REQUIRE_FALSE(rcp::acf::decode_acf_abb(frame.data(), frame.size(), hdr, payload));
    REQUIRE(hdr.byte_bus_id == 0x03);
    REQUIRE(hdr.op); // write
    REQUIRE(hdr.evt_op == 0x7u);
    REQUIRE(hdr.transaction_num == 7);
    REQUIRE(payload.size() == 4);
    REQUIRE(payload[0] == 0x00);
    REQUIRE(payload[1] == 0x06);
    REQUIRE(payload[2] == 0xAB);
    REQUIRE(payload[3] == 0xCD);
}

TEST_CASE("encode_reconfig_request rejects empty data", "[spi][REQ-SPI-042]") {
    auto frame = encode_reconfig_request(0x00, 0, {}, 0);
    REQUIRE(frame.empty());
}

// ── strerror-equivalent category coverage ─────────────────────────────────────

TEST_CASE("SpiErrc category reports a non-empty, distinct message for every known code", "[spi][REQ-SPI-001]") {
    const SpiErrc codes[] = {SpiErrc::short_frame, SpiErrc::bad_msg_type, SpiErrc::wrong_bus, SpiErrc::wrong_op,
                              SpiErrc::bad_channel};
    for (size_t i = 0; i < std::size(codes); ++i) {
        auto ec = make_error_code(codes[i]);
        REQUIRE_FALSE(ec.message().empty());
        for (size_t j = 0; j < i; ++j) {
            REQUIRE(ec.message() != make_error_code(codes[j]).message());
        }
    }
    auto unknown = std::error_code(999, spi_category());
    REQUIRE_FALSE(unknown.message().empty());
}

// ── Transfer request round trip ───────────────────────────────────────────────

// TC18's own worked SPI transfer example ("write N bytes, get a response
// with M") carries op=0 (the read direction) — an SPI transfer request
// sends PICO bytes and expects POCI bytes back.
TEST_CASE("encode_transfer_request uses the read-direction op", "[spi][REQ-SPI-026]") {
    std::vector<uint8_t> tx{0x55};
    auto                 frame = encode_transfer_request(4, 3, tx, 0, 3);

    rcp::acf::AcfMessageInfo hdr;
    std::vector<uint8_t>     payload;
    REQUIRE_FALSE(rcp::acf::decode_acf_abb(frame.data(), frame.size(), hdr, payload));
    REQUIRE_FALSE(hdr.op); // read
    REQUIRE((hdr.evt_op & 0x7u) == 3u);
}

TEST_CASE("transfer request round-trips channel, PICO-out payload, read_size, and transaction_num",
          "[spi][REQ-SPI-027][REQ-SPI-041][REQ-SPI-044]") {
    std::vector<uint8_t> tx{0x01, 0x02, 0x03};
    auto                 frame = encode_transfer_request(4, 2, tx, 0x0Au, 9);

    uint8_t               channel = 0xFF;
    std::vector<uint8_t>  out_tx;
    uint16_t              out_read_size = 0;
    uint8_t               txn           = 0;

    REQUIRE_FALSE(decode_transfer_request(frame.data(), frame.size(), 4, channel, out_tx, out_read_size, txn));
    REQUIRE(channel == 2);
    REQUIRE(out_tx == tx);
    REQUIRE(out_read_size == 0x0Au);
    REQUIRE(txn == 9);
}

TEST_CASE("transfer request round-trips an empty PICO-out payload", "[spi][REQ-SPI-027]") {
    auto frame = encode_transfer_request(4, 0, {}, 0, 1);

    uint8_t              channel = 0xFF;
    std::vector<uint8_t> out_tx;
    uint16_t             out_read_size = 0;
    uint8_t              txn           = 0;

    REQUIRE_FALSE(decode_transfer_request(frame.data(), frame.size(), 4, channel, out_tx, out_read_size, txn));
    REQUIRE(channel == 0);
    REQUIRE(out_tx.empty());
}

TEST_CASE("decode_transfer_request rejects the wrong byte_bus_id", "[spi][REQ-SPI-027]") {
    std::vector<uint8_t> tx{0xAB};
    auto                 frame = encode_transfer_request(4, 1, tx, 0, 0);

    uint8_t               channel;
    std::vector<uint8_t>  out_tx;
    uint16_t              out_read_size;
    uint8_t                txn;
    REQUIRE(decode_transfer_request(frame.data(), frame.size(), 5, channel, out_tx, out_read_size, txn) ==
            make_error_code(SpiErrc::wrong_bus));
}

// The mirror of the read-direction test above: a frame carrying the write
// direction is not an SPI transfer request.
TEST_CASE("decode_transfer_request rejects the wrong op", "[spi][REQ-SPI-027]") {
    rcp::acf::AcfMessageInfo hdr;
    hdr.byte_bus_id = 4;
    hdr.op           = true; // write -- not a transfer request
    auto frame       = rcp::acf::encode_acf_abb(hdr, {});

    uint8_t              channel;
    std::vector<uint8_t> out_tx;
    uint16_t             out_read_size;
    uint8_t              txn;
    REQUIRE(decode_transfer_request(frame.data(), frame.size(), 4, channel, out_tx, out_read_size, txn) ==
            make_error_code(SpiErrc::wrong_op));
}

// TC18 Table 33's SPI row: evt[2:0] 000b-101b selects channel 0..5, 110b is
// reserved, 111b is the configuration escape hatch -- neither 6 nor 7 is a
// channel selector.
TEST_CASE("decode_transfer_request rejects a bad channel", "[spi][REQ-SPI-027]") {
    rcp::acf::AcfMessageInfo hdr;
    hdr.byte_bus_id = 4;
    hdr.op           = false; // read
    hdr.evt_op        = 7;     // not a valid channel selector
    auto frame        = rcp::acf::encode_acf_abb(hdr, {});

    uint8_t              channel;
    std::vector<uint8_t> out_tx;
    uint16_t             out_read_size;
    uint8_t              txn;
    REQUIRE(decode_transfer_request(frame.data(), frame.size(), 4, channel, out_tx, out_read_size, txn) ==
            make_error_code(SpiErrc::bad_channel));
}

TEST_CASE("decode_transfer_request rejects a non-ACF_ABB frame", "[spi][REQ-SPI-027]") {
    rcp::acf::AcfMessageInfo hdr;
    hdr.byte_bus_id = 4;
    hdr.op           = true;
    auto frame        = rcp::acf::encode_acf_gbb(hdr, 0, {});

    uint8_t              channel;
    std::vector<uint8_t> out_tx;
    uint16_t             out_read_size;
    uint8_t              txn;
    REQUIRE(decode_transfer_request(frame.data(), frame.size(), 4, channel, out_tx, out_read_size, txn) ==
            make_error_code(SpiErrc::bad_msg_type));
}

TEST_CASE("decode_transfer_request rejects a short frame", "[spi][REQ-SPI-027]") {
    // byte0's top 7 bits must carry acf_msg_type == kAcfMsgTypeAbb (0x0E) --
    // otherwise decode_acf_abb reports bad_msg_type before it even gets to
    // check the buffer's length against the fixed header size.
    uint8_t               too_short[3] = {0x1C, 0x00, 0x00};
    uint8_t               channel;
    std::vector<uint8_t>  out_tx;
    uint16_t              out_read_size;
    uint8_t               txn;
    REQUIRE(decode_transfer_request(too_short, sizeof(too_short), 4, channel, out_tx, out_read_size, txn) ==
            make_error_code(SpiErrc::short_frame));
}

// TC18 §13.7.3.3's own zero-fill rule: verified directly against
// transfer_length(), one case per direction plus the exactly-equal boundary.
TEST_CASE("transfer_length zero-fills when read_size exceeds the payload", "[spi][REQ-SPI-036]") {
    REQUIRE(transfer_length(3u, 10u) == 10u);
}

TEST_CASE("transfer_length presents the full payload when read_size is smaller", "[spi][REQ-SPI-036]") {
    REQUIRE(transfer_length(10u, 3u) == 10u);
}

TEST_CASE("transfer_length handles the exactly-equal boundary", "[spi][REQ-SPI-036]") {
    REQUIRE(transfer_length(5u, 5u) == 5u);
}

// ── Response round trip ───────────────────────────────────────────────────────

TEST_CASE("response round-trips untimed", "[spi][REQ-SPI-028]") {
    std::vector<uint8_t> rx{0xDE, 0xAD, 0xBE, 0xEF};
    auto                 frame = encode_response(2, 5, rx, 11, false, 0);

    uint8_t               channel = 0xFF;
    std::vector<uint8_t>  out_rx;
    bool                   timed     = true;
    uint64_t                ts        = 1;
    uint8_t                  txn       = 0;

    REQUIRE_FALSE(decode_response(frame.data(), frame.size(), 2, channel, out_rx, timed, ts, txn));
    REQUIRE(channel == 5);
    REQUIRE(out_rx == rx);
    REQUIRE_FALSE(timed);
    REQUIRE(ts == 0);
    REQUIRE(txn == 11);
}

TEST_CASE("response round-trips timed", "[spi][REQ-SPI-029]") {
    std::vector<uint8_t> rx{0x11, 0x22};
    auto                 frame = encode_response(2, 3, rx, 200, true, 0x0102030405060708ull);

    uint8_t               channel = 0xFF;
    std::vector<uint8_t>  out_rx;
    bool                   timed     = false;
    uint64_t                ts        = 0;
    uint8_t                  txn       = 0;

    REQUIRE_FALSE(decode_response(frame.data(), frame.size(), 2, channel, out_rx, timed, ts, txn));
    REQUIRE(channel == 3);
    REQUIRE(out_rx == rx);
    REQUIRE(timed);
    REQUIRE(ts == 0x0102030405060708ull);
    REQUIRE(txn == 200);
}

TEST_CASE("decode_response rejects the wrong byte_bus_id", "[spi][REQ-SPI-030]") {
    auto frame = encode_response(2, 0, {}, 0, false, 0);

    uint8_t              channel;
    std::vector<uint8_t> out_rx;
    bool                 timed;
    uint64_t             ts;
    uint8_t              txn;
    REQUIRE(decode_response(frame.data(), frame.size(), 3, channel, out_rx, timed, ts, txn) ==
            make_error_code(SpiErrc::wrong_bus));
}

TEST_CASE("decode_response rejects a bad channel", "[spi][REQ-SPI-030]") {
    rcp::acf::AcfMessageInfo hdr;
    hdr.byte_bus_id = 2;
    hdr.op           = false;
    hdr.evt_op        = 6; // not a valid channel selector
    auto frame        = rcp::acf::encode_acf_abb(hdr, {});

    uint8_t              channel;
    std::vector<uint8_t> out_rx;
    bool                 timed;
    uint64_t             ts;
    uint8_t              txn;
    REQUIRE(decode_response(frame.data(), frame.size(), 2, channel, out_rx, timed, ts, txn) ==
            make_error_code(SpiErrc::bad_channel));
}

TEST_CASE("decode_response rejects a short frame", "[spi][REQ-SPI-030]") {
    uint8_t              too_short[2] = {rcp::acf::kAcfMsgTypeAbb << 1, 0};
    uint8_t              channel;
    std::vector<uint8_t> out_rx;
    bool                 timed;
    uint64_t             ts;
    uint8_t              txn;
    REQUIRE(decode_response(too_short, sizeof(too_short), 2, channel, out_rx, timed, ts, txn) ==
            make_error_code(SpiErrc::short_frame));
}

// ── Compound-wait: the wrong SPI-specific 4-byte truncation rule is gone ─────
// c-RCP's v0.111.0 removed rcp_ep_spi_compound_wait_status_equal() and its
// hardcoded 4-byte comparison length as an SPI-specific rule that was simply
// wrong — TC18 §13.5.1's own length rule (status capped to
// byte_msg_payload's own length) is universal across every endpoint type.
// This header carries no compound-wait logic of its own any more; a caller
// evaluating a compound-wait request against this endpoint goes through
// rcp/acf.hpp's own endpoint-type-independent compound_wait_evt_valid()/
// compound_wait_match() directly, exercised here against the
// specification's own SPI-flavored worked example (checking only the first
// four of 20 received status bytes) to demonstrate the replacement covers
// exactly the case the old, wrong SPI-specific helper existed for.
TEST_CASE("compound-wait against an SPI status transfer goes through acf::compound_wait_match, "
          "not an SPI-specific truncation rule",
          "[spi]") {
    std::vector<uint8_t> payload{0x01, 0x02, 0x03, 0x04}; // the request's own 4-byte byte_msg_payload
    std::vector<uint8_t> status(kMaxStatusBytes, 0x00);   // a full 20-byte status transfer
    status[0] = 0x01;
    status[1] = 0x02;
    status[2] = 0x03;
    status[3] = 0x04;
    status[19] = 0xAA; // differs, but past payload's own 4-byte length -- must not affect the verdict

    REQUIRE(rcp::acf::compound_wait_evt_valid(0x0)); // exact-match mode
    REQUIRE(rcp::acf::compound_wait_match(0x0, payload.data(), payload.size(), status.data(), status.size()));

    status[3] = 0x05; // now differs within the first 4 bytes
    REQUIRE_FALSE(rcp::acf::compound_wait_match(0x0, payload.data(), payload.size(), status.data(), status.size()));
}

// ── SpiEndpoint convenience class ─────────────────────────────────────────────

TEST_CASE("SpiEndpoint::transfer records sent and received bytes per channel", "[spi]") {
    SpiEndpoint ep;
    auto        ec = ep.transfer(/*channel=*/2, {0xDE, 0xAD}, {0xBE, 0xEF});
    REQUIRE_FALSE(ec);
    REQUIRE(ep.last_sent(2) == std::vector<uint8_t>{0xDE, 0xAD});
    REQUIRE(ep.last_received(2) == std::vector<uint8_t>{0xBE, 0xEF});
}

TEST_CASE("SpiEndpoint::transfer rejects a channel >= kMaxChannels", "[spi]") {
    SpiEndpoint ep;
    auto        ec = ep.transfer(/*channel=*/6, {0x01}, {0x02});
    REQUIRE(ec == make_error_code(SpiErrc::bad_channel));
}

TEST_CASE("SpiEndpoint tracks each channel's last transfer independently", "[spi]") {
    SpiEndpoint ep;
    REQUIRE_FALSE(ep.transfer(0, {0x01}, {0x11}));
    REQUIRE_FALSE(ep.transfer(1, {0x02}, {0x22}));
    REQUIRE(ep.last_received(0) == std::vector<uint8_t>{0x11});
    REQUIRE(ep.last_received(1) == std::vector<uint8_t>{0x22});
}

TEST_CASE("SpiEndpoint::transfer fires CsAssert, TransferComplete, CsDeassert in order when armed", "[spi]") {
    SpiEndpoint ep;
    ep.triggers().enable(spi_signal_id(3, SpiSignal::CsAssert));
    ep.triggers().enable(spi_signal_id(3, SpiSignal::TransferComplete));
    ep.triggers().enable(spi_signal_id(3, SpiSignal::CsDeassert));

    REQUIRE_FALSE(ep.transfer(3, {0x01}, {0x02}));

    auto drained = ep.triggers().drain();
    REQUIRE(drained.size() == 3);
    REQUIRE(drained[0] == spi_signal_id(3, SpiSignal::CsAssert));
    REQUIRE(drained[1] == spi_signal_id(3, SpiSignal::TransferComplete));
    REQUIRE(drained[2] == spi_signal_id(3, SpiSignal::CsDeassert));
}

TEST_CASE("SpiEndpoint's trigger signals are scoped per channel", "[spi]") {
    SpiEndpoint ep;
    ep.triggers().enable(spi_signal_id(0, SpiSignal::TransferComplete));
    // Channel 1's TransferComplete signal is deliberately left disabled.

    REQUIRE_FALSE(ep.transfer(1, {0xAA}, {0xBB}));
    REQUIRE_FALSE(ep.triggers().has_pending());

    REQUIRE_FALSE(ep.transfer(0, {0xAA}, {0xBB}));
    REQUIRE(ep.triggers().has_pending());
}

TEST_CASE("spi_signal_id gives every (channel, signal) pair a distinct id", "[spi]") {
    REQUIRE(spi_signal_id(0, SpiSignal::TransferComplete) != spi_signal_id(0, SpiSignal::CsAssert));
    REQUIRE(spi_signal_id(0, SpiSignal::TransferComplete) != spi_signal_id(1, SpiSignal::TransferComplete));
}
