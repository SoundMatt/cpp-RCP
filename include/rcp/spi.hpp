// fusa:req REQ-SPI-001
// fusa:req REQ-SPI-002
// fusa:req REQ-SPI-003
// fusa:req REQ-SPI-004
// fusa:req REQ-SPI-005
// fusa:req REQ-SPI-006
// fusa:req REQ-SPI-007
// fusa:req REQ-SPI-008
// fusa:req REQ-SPI-009
// fusa:req REQ-SPI-010
// fusa:req REQ-SPI-011
// fusa:req REQ-SPI-012
// fusa:req REQ-SPI-013
// fusa:req REQ-SPI-014
// fusa:req REQ-SPI-015
// fusa:req REQ-SPI-016
// fusa:req REQ-SPI-017
// fusa:req REQ-SPI-018
// fusa:req REQ-SPI-019
// fusa:req REQ-SPI-020
// fusa:req REQ-SPI-021
// fusa:req REQ-SPI-022
// fusa:req REQ-SPI-023
// fusa:req REQ-SPI-024
// fusa:req REQ-SPI-025
// fusa:req REQ-SPI-026
// fusa:req REQ-SPI-027
// fusa:req REQ-SPI-028
// fusa:req REQ-SPI-029
// fusa:req REQ-SPI-030
// fusa:req REQ-SPI-033
// fusa:req REQ-SPI-034
// fusa:req REQ-SPI-035
// fusa:req REQ-SPI-036
// fusa:req REQ-SPI-037
// fusa:req REQ-SPI-038
// fusa:req REQ-SPI-039
// fusa:req REQ-SPI-040
// fusa:req REQ-SPI-041
// fusa:req REQ-SPI-042
// fusa:req REQ-SPI-043
// fusa:req REQ-SPI-044

// SPI endpoint (ep_type 0x03) — a controller-only endpoint addressing up to
// kMaxChannels (6) independently pre-configured channels, selected via the
// ACF byte_message_info header's evt[2:0] field directly as a channel
// number (0..5; 6/7 select no defined channel and are rejected), a
// full-duplex byte-for-byte transfer request/response pair, per-channel
// functional configuration, and the EP_func addressed-configuration-write
// register block (evt[2:0] == 111b, §12.7.1) (extraction §5.4, §13.5
// Table 33, §13.7.3).
//
// Phase 3 rewrite (cpp-RCP issue #129, ROADMAP.md "Phase 17"), re-derived
// (not incrementally patched) from c-RCP's include/rcp/ep_spi.h +
// src/ep_spi.c — this project's RC5-spec-conformant reference for this
// module. This header's own pre-rewrite content was a suspiciously thin
// evt[2:0]-channel-decode-plus-a-wrong-compound-wait-rule pilot (kMaxChannels,
// channel_of, SpiEndpoint::transfer, and the trigger-signal scaffolding); the
// per-channel functional config, the EP_func register block, the real
// ACF-level wire codec (transfer request/response encode+decode), and
// Table 41's own trigger-signal numbering were entirely missing and are
// ported below for the first time.
//
// ── Channel selection is evt-bits only, NOT byte_bus_id/Table 26 ───────────
// c-RCP's ep_spi.h carries an extensive "INVESTIGATED 2026-08-11" file-header
// note (c-RCP-AUDIT-06, task #98) confirming that evt[2:0] (0-5 = channel,
// 6/7 = reserved/invalid) remains the current, conformant channel-selection
// mechanism: TC18 §13.5's own authoritative per-endpoint-type table's SPI row
// is unchanged ("selects channel 0...5"), and a competing BBID-based Table 26
// "Channel_selection[3:0]" proposal elsewhere in the 0.5.1_RC5 draft is
// explicitly conditional on that row changing, which as of RC5 has not
// happened. This module's channel_of()/decode_transfer_request()/
// decode_response() below all decode evt[2:0] directly as the channel
// number — there is no BBID-based channel-selection path in this codebase,
// and none should be added absent a future spec revision resolving that
// still-open proposal.
//
// ── spi_nr_cs is a 4-bit "(count-1)" register field, not a plain 8-bit
// count ─────────────────────────────────────────────────────────────────────
// c-RCP's ep_spi.h/.c carry a "FIXED 2026-08-11" note (spec rebaseline to
// TC18 0.5.1_RC5): the EP_func block's spi_nr_cs register (relative offset
// 0x0001) was originally read against the 0.5.1_RC baseline as a plain 8-bit
// channel count; RC4 narrowed it to a 4-bit "(count - 1)" field in bits
// [3:0], upper nibble reserved (reads 0000b). render_registers() below
// renders (kMaxChannels - 1) & 0x0F (0x05), not a plain 6, in the low
// nibble, and leaves the high nibble 0 — kRegNrCs is read-only, exactly as
// the specification's own R marker requires (a configuration write covering
// it is silently ignored, see apply_reconfig()/detail::reg_offset_read_only
// below).
//
// ── spi_deassert_cs_pauseN (RC5, ticket NXP_100) ────────────────────────────
// Bit 4 of each channel's own +0x02 cfg octet, new in TC18 spec revision
// 0.5.1_RC5 with no counterpart at all in the baseline this module was
// originally built against: "0b: no de-assertion during break / 1b:
// de-assertion during break" during the pause window
// spi_cs_clk_leadtimeN/spi_pause_minN/spi_clk_cs_trailtimeN define.
// SpiChannelCfg::deassert_cs_pause is the new field this bit round-trips
// through (render_registers()/detail::parse_registers()), added following
// the same "new field, every pre-existing cfg bit left untouched" rule
// c-RCP's own file header documents for this fix.
//
// ── Compound-wait against an SPI endpoint: the wrong SPI-specific
// 4-byte-truncation rule is REMOVED, not ported ─────────────────────────────
// c-RCP's ep_spi.h file header records that v0.111.0 removed
// rcp_ep_spi_compound_wait_status_equal()/RCP_EP_SPI_COMPOUND_WAIT_COMPARE_LEN:
// both modeled the compound-wait comparison length as an SPI-specific,
// hardcoded 4-byte truncation. That was wrong — TC18 §13.5.1's own length
// rule (status is capped to byte_msg_payload's own length, whatever that
// request happens to carry) is universal across every endpoint type, and the
// specification's own worked example (checking only the first four of 20
// received bytes when byte_msg_payload has only four bytes) illustrates that
// general rule *using* SPI, rather than stating an SPI-specific rule of its
// own. This header's own pre-rewrite content had exactly that wrong,
// SPI-specific hardcoded-4 rule (compound_wait_matches()/
// kCompoundWaitCompareLen) — both are deleted here, not merely deprecated.
// This codebase already has the correct, endpoint-type-independent
// primitive (rcp/acf.hpp's compound_wait_evt_valid()/compound_wait_match(),
// added during this rewrite's Phase 1, and already the mechanism
// rcp/i2c.hpp's own file header points callers at for the identical
// reason) — a caller evaluating a compound-wait request against this
// endpoint calls those two functions directly, exactly like every other
// endpoint type; there is nothing SPI-specific left for this header to
// provide here. kMaxStatusBytes below is unrelated (it bounds this endpoint
// type's own transfer-done status-report width, not compound-wait's
// comparison length).
//
// ── REQ-SPI-037: genuinely not implemented ──────────────────────────────────
// c-RCP's own .fusa-reqs.json records REQ-SPI-037 ("SPI error state resets
// the EP enable bit; a clamped pin flags every response") as
// NOT-IMPLEMENTED, blocked by real specification silence: TC18 §13.7.3.3
// gestures at "cs/hs bits" indicating stopped execution without ever
// defining what that means for SPI, and the two plausible readings (the ACF
// header's own cs/hs bits, vs. the SPI bus's own physical CS/HS hardware
// signal lines) lead to entirely different implementations. This is a
// genuine spec defect (c-RCP: TC18_spec_defects_report.md item 56), not a
// local implementation gap — nothing is ported for it here, matching c-RCP's
// own disposition, rather than guessing at a resolution.
//
// Field names and behavior below implement TC18's *behavior* as described in
// an internal structured extraction of the specification named above; no
// text from that document is reproduced here. The concrete
// SpiChannelCfg/SpiFunctionalCfg layout, SpiTrigger's collapsed 4-value
// simplification of Table 41's 14 fixed hardware signals, and bit_order
// (which — like `trigger` — has no TC18 register counterpart at all and is
// never rendered onto the wire) are this implementation's own, same as the
// equivalent disclaimers in rcp/avtp.hpp, rcp/endpoint.hpp, rcp/gpio.hpp,
// and rcp/i2c.hpp.
#pragma once

#include <rcp/acf.hpp>
#include <rcp/avtp.hpp>
#include <rcp/endpoint.hpp>
#include <rcp/lifecycle.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace rcp {
namespace spi {

// ── Channel addressing ────────────────────────────────────────────────────────
// The largest number of pre-configured SPI channels this endpoint type
// addresses via evt[2:0] (extraction §5.4; TC18 §13.5 Table 33's own SPI
// row, "selects channel 0...5" — see the file header's own channel-selection
// note).

constexpr uint8_t kMaxChannels = 6;

// True iff channel is a valid channel index (0..kMaxChannels-1) — one of the
// 6 evt[2:0] values this endpoint type actually assigns a channel to (values
// 6 and 7 select no defined channel).
constexpr bool channel_valid(uint8_t channel) noexcept { return channel < kMaxChannels; }

// ── Clock mode: the 4 standard CPOL/CPHA combinations ─────────────────────────

enum class SpiMode : uint8_t {
    Mode0 = 0, // CPOL=0, CPHA=0
    Mode1 = 1, // CPOL=0, CPHA=1
    Mode2 = 2, // CPOL=1, CPHA=0
    Mode3 = 3, // CPOL=1, CPHA=1
};

// True iff v (a raw clock-mode value, e.g. as decoded from a register) is
// one of the four defined modes.
constexpr bool mode_valid(uint8_t v) noexcept { return v <= static_cast<uint8_t>(SpiMode::Mode3); }

// The clock-polarity (CPOL) bit implied by mode: false for Mode0/Mode1, true
// for Mode2/Mode3. An invalid mode value is treated as CPOL false (fail-safe
// default — never fabricate a "true" safety-relevant bit for undefined
// input).
constexpr bool mode_cpol(SpiMode mode) noexcept {
    switch (mode) {
    case SpiMode::Mode2:
    case SpiMode::Mode3: return true;
    case SpiMode::Mode0:
    case SpiMode::Mode1:
    default:             return false;
    }
}

// The clock-phase (CPHA) bit implied by mode: false for Mode0/Mode2, true
// for Mode1/Mode3. Same fail-safe treatment of an invalid mode value as
// mode_cpol().
constexpr bool mode_cpha(SpiMode mode) noexcept {
    switch (mode) {
    case SpiMode::Mode1:
    case SpiMode::Mode3: return true;
    case SpiMode::Mode0:
    case SpiMode::Mode2:
    default:             return false;
    }
}

// ── Bit order and chip-select active-polarity ─────────────────────────────────
// bit_order has no counterpart in TC18 §13.7.3.2's per-channel register block
// at all — this module's own original addition, never rendered onto the
// wire (see render_registers() below).

enum class SpiBitOrder : uint8_t {
    MsbFirst = 0,
    LsbFirst = 1,
};

enum class SpiCsPolarity : uint8_t {
    ActiveLow  = 0,
    ActiveHigh = 1,
};

// ── Per-channel trigger signals ────────────────────────────────────────────────
// SpiTrigger names the three asynchronous-event trigger modes a channel's
// functional config may select (transfer-done, CS-assert-edge,
// CS-deassert-edge), plus None. This is this module's own deliberate
// collapse of TC18 §13.7.3.1 Table 41's 14 fixed, always-on, per-CS-channel
// hardware trigger signals (execution-done, plus an assert/de-assert pair
// for each of CS0 through CS5) into 4 generic values with no per-channel
// distinction — Table 42 (the per-channel functional-config register block)
// defines no register field that selects among them, so `trigger` is never
// rendered onto the wire, exactly like bit_order above.

enum class SpiTrigger : uint8_t {
    None          = 0,
    TransferDone  = 1,
    CsAssert      = 2,
    CsDeassert    = 3,
};

// The three asynchronous events a channel's trigger mode may be evaluated
// against — see trigger_fires().
enum class SpiEvent : uint8_t {
    TransferDone = 0,
    CsAssert     = 1,
    CsDeassert   = 2,
};

// True iff event satisfies trigger: never for None; for TransferDone iff
// event == SpiEvent::TransferDone; for CsAssert iff event ==
// SpiEvent::CsAssert; for CsDeassert iff event == SpiEvent::CsDeassert.
constexpr bool trigger_fires(SpiTrigger trigger, SpiEvent event) noexcept {
    switch (trigger) {
    case SpiTrigger::TransferDone: return event == SpiEvent::TransferDone;
    case SpiTrigger::CsAssert:     return event == SpiEvent::CsAssert;
    case SpiTrigger::CsDeassert:   return event == SpiEvent::CsDeassert;
    case SpiTrigger::None:
    default:                       return false;
    }
}

// REQ-SPI-034: TC18 §13.7.3.1's own Table 41 "spi trigger outputs" — signal 0
// is "SPI execution done" (a whole-endpoint trigger, not modeled by this
// per-channel function), and for chip select CSn: signal 2+2n is CSn
// asserted, signal 3+2n is CSn de-asserted (0 <= n < 16 in Table 41's own
// wording, narrowed here to this module's own kMaxChannels (6), the same way
// rcp/gpio.hpp's trigger_signal_number() narrows Table 43's IOn range to
// kMaxPins).
//
// This is a pure numbering computation, entirely independent of SpiTrigger's
// own deliberately-collapsed, non-wire-rendered per-channel trigger mode
// above — it only lets a caller resolve a Table 41 signal number for a
// (channel, CS-edge) pair that names one.
//
// Returns the signal number iff channel < kMaxChannels and trigger is
// CsAssert or CsDeassert (never TransferDone, which is signal 0's
// whole-endpoint concept and has no per-channel Table 41 entry, nor None,
// which names no trigger event and therefore no signal number); std::nullopt
// otherwise.
inline std::optional<uint8_t> trigger_signal_number(uint8_t channel, SpiTrigger trigger) noexcept {
    if (channel >= kMaxChannels) return std::nullopt;
    switch (trigger) {
    case SpiTrigger::CsAssert:   return static_cast<uint8_t>(2u + 2u * channel);
    case SpiTrigger::CsDeassert: return static_cast<uint8_t>(3u + 2u * channel);
    case SpiTrigger::TransferDone: // signal 0 is whole-endpoint, not per-channel
    case SpiTrigger::None:
    default:
        return std::nullopt;
    }
}

// ── Functional config ─────────────────────────────────────────────────────────
// One channel's runtime-adjustable functional configuration. Timing delays
// (inter_byte_delay_ns/inter_transfer_delay_ns) are this module's own
// nanosecond-denominated original addition, distinct from the wire's
// spi_clk-cycle-denominated cs_clk_leadtime/clk_cs_trailtime/pause_min
// fields (Table 42) below.

struct SpiChannelCfg {
    SpiMode       mode         = SpiMode::Mode0;
    SpiBitOrder   bit_order    = SpiBitOrder::MsbFirst;
    SpiCsPolarity cs_polarity  = SpiCsPolarity::ActiveLow;
    SpiTrigger    trigger      = SpiTrigger::None;
    uint32_t      clock_divider           = 0;
    uint32_t      inter_byte_delay_ns     = 0;
    uint32_t      inter_transfer_delay_ns = 0;
    uint16_t      baud_rate_kbps = 0; // spi_baud_rateN, Table 42
    bool          use_common_cs  = false; // spi_use_csN: false = this channel's own
                                            // CSN (the wire's 0b default), true = the
                                            // common CS0 is used instead
    uint8_t       cs_clk_leadtime  = 0; // spi_cs_clk_leadtimeN, spi_clk cycles
    uint8_t       clk_cs_trailtime = 0; // spi_clk_cs_trailtimeN, spi_clk cycles
    uint8_t       bits_max         = 0; // spi_bits_maxN
    uint8_t       pause_min        = 0; // spi_pause_minN, spi_clk cycles
    bool          deassert_cs_pause = false; // spi_deassert_cs_pauseN (TC18
                                               // 0.5.1_RC5, ticket NXP_100 — see
                                               // the file header): false = no
                                               // de-assertion during the pause
                                               // (the wire's 0b default), true =
                                               // CS is de-asserted during the
                                               // pause window
};

struct SpiFunctionalCfg {
    bool ep_enable             = false;
    bool ep_clear_req_storage  = false;
    bool ep_req_crc_enable     = false;
    bool ep_response_ts_enable = false;
    bool ep_suppress_response  = false;

    std::array<SpiChannelCfg, kMaxChannels> channels{};
    uint16_t                                ep_status = 0; // spi_ep_status, Table 42
};

// Resets cfg to its zero/default state (every common flag false; every
// channel's mode Mode0, bit_order MsbFirst, cs_polarity ActiveLow, trigger
// None, every numeric timing/rate field 0, use_common_cs/deassert_cs_pause
// false; ep_status 0).
inline void functional_cfg_init(SpiFunctionalCfg& cfg) noexcept { cfg = SpiFunctionalCfg{}; }

// functional_cfg_writable is a thin, named wrapper over
// rcp/lifecycle.hpp's field_writable() (FieldKind::FunctionalW) — reuses,
// never duplicates, that function's authorization logic.
inline bool functional_cfg_writable(lifecycle::ServerState state, lifecycle::WriterCtx writer) noexcept {
    return lifecycle::field_writable(state, lifecycle::FieldKind::FunctionalW, writer);
}

// Sets cfg.channels[channel].mode to mode iff channel is channel_valid() and
// functional_cfg_writable() authorizes the write for state/writer; returns
// whether the write was applied. cfg is left entirely unchanged when it
// returns false.
inline bool set_channel_mode(SpiFunctionalCfg& cfg, uint8_t channel, SpiMode mode,
                              lifecycle::ServerState state, lifecycle::WriterCtx writer) noexcept {
    if (!channel_valid(channel)) return false;
    if (!functional_cfg_writable(state, writer)) return false;
    cfg.channels[channel].mode = mode;
    return true;
}

// Same authorization/validity rule as set_channel_mode(), for
// cfg.channels[channel].bit_order.
inline bool set_channel_bit_order(SpiFunctionalCfg& cfg, uint8_t channel, SpiBitOrder bit_order,
                                   lifecycle::ServerState state, lifecycle::WriterCtx writer) noexcept {
    if (!channel_valid(channel)) return false;
    if (!functional_cfg_writable(state, writer)) return false;
    cfg.channels[channel].bit_order = bit_order;
    return true;
}

// Same authorization/validity rule, for cfg.channels[channel].cs_polarity.
inline bool set_channel_cs_polarity(SpiFunctionalCfg& cfg, uint8_t channel, SpiCsPolarity cs_polarity,
                                     lifecycle::ServerState state, lifecycle::WriterCtx writer) noexcept {
    if (!channel_valid(channel)) return false;
    if (!functional_cfg_writable(state, writer)) return false;
    cfg.channels[channel].cs_polarity = cs_polarity;
    return true;
}

// Same authorization/validity rule, for cfg.channels[channel].clock_divider.
inline bool set_channel_clock_divider(SpiFunctionalCfg& cfg, uint8_t channel, uint32_t clock_divider,
                                       lifecycle::ServerState state, lifecycle::WriterCtx writer) noexcept {
    if (!channel_valid(channel)) return false;
    if (!functional_cfg_writable(state, writer)) return false;
    cfg.channels[channel].clock_divider = clock_divider;
    return true;
}

// Same authorization/validity rule, for cfg.channels[channel]'s
// inter_byte_delay_ns and inter_transfer_delay_ns together (one setter for
// both timing fields, since they are always reconfigured as a pair on the
// wire).
inline bool set_channel_timing(SpiFunctionalCfg& cfg, uint8_t channel, uint32_t inter_byte_delay_ns,
                                uint32_t inter_transfer_delay_ns, lifecycle::ServerState state,
                                lifecycle::WriterCtx writer) noexcept {
    if (!channel_valid(channel)) return false;
    if (!functional_cfg_writable(state, writer)) return false;
    cfg.channels[channel].inter_byte_delay_ns     = inter_byte_delay_ns;
    cfg.channels[channel].inter_transfer_delay_ns = inter_transfer_delay_ns;
    return true;
}

// Same authorization/validity rule, for cfg.channels[channel].trigger.
inline bool set_channel_trigger(SpiFunctionalCfg& cfg, uint8_t channel, SpiTrigger trigger,
                                 lifecycle::ServerState state, lifecycle::WriterCtx writer) noexcept {
    if (!channel_valid(channel)) return false;
    if (!functional_cfg_writable(state, writer)) return false;
    cfg.channels[channel].trigger = trigger;
    return true;
}

// ── The EP_func register block (evt[2:0] == 111b) ────────────────────────────
// Relative octet offsets of the registers making up the common (non-
// per-channel) prefix of an SPI endpoint's own EP_func block. Every
// multi-octet register is big-endian. Offsets marked R are read-only: a
// configuration write covering them leaves them unchanged (see
// apply_reconfig() below).

constexpr uint16_t kRegEpLen        = 0x0000; //  8 bit, R
constexpr uint16_t kRegNrCs         = 0x0001; //  8 bit, R -- only bits [3:0]
                                                //  carry spi_nr_cs
                                                //  (count - 1); [7:4]
                                                //  reserved (TC18 0.5.1_RC5 —
                                                //  see the file header)
constexpr uint16_t kRegEpEnableClr  = 0x0002; //  8 bit, R/W
constexpr uint16_t kRegEpOptions    = 0x0003; //  8 bit, R/W
constexpr uint16_t kRegEpStatus     = 0x0004; // 16 bit, R/W

// Channel c's own 8-octet block starts at kRegChannelBase + c *
// kRegChannelSpan; the kChReg* offsets below are relative to that channel's
// own base.
constexpr uint16_t kRegChannelBase = 0x0006;
constexpr uint16_t kRegChannelSpan = 0x0008;

constexpr uint16_t kChRegBaudRate     = 0x00; // 16 bit, R/W
constexpr uint16_t kChRegCfg          = 0x02; //  8 bit, R/W -- clk_polarity(0)/
                                                //  clk_phase(1)/cs_polarity(2)/
                                                //  use_cs(3)/deassert_cs_pause(4),
                                                //  bits 5-7 reserved
constexpr uint16_t kChRegCsLeadtime   = 0x03; //  8 bit, R/W
constexpr uint16_t kChRegCsTrailtime  = 0x04; //  8 bit, R/W
constexpr uint16_t kChRegBitsMax      = 0x05; //  8 bit, R/W
constexpr uint16_t kChRegPauseMin     = 0x06; //  8 bit, R/W
constexpr uint16_t kChRegReserved     = 0x07; //  8 bit, R

// Bit masks within a channel's kChRegCfg octet.
constexpr uint8_t kCfgBitClkPolarity      = 1u << 0;
constexpr uint8_t kCfgBitClkPhase         = 1u << 1;
constexpr uint8_t kCfgBitCsPolarity       = 1u << 2;
constexpr uint8_t kCfgBitUseCs            = 1u << 3;
// Added TC18 spec revision 0.5.1_RC5, ticket NXP_100 — see the file header.
constexpr uint8_t kCfgBitDeassertCsPause  = 1u << 4;

// The block's own length in octets — one past the last assigned offset: the
// 6-octet common prefix plus kMaxChannels 8-octet per-channel blocks (0x36 =
// 54 octets).
constexpr uint16_t kEpFuncLen =
    static_cast<uint16_t>(kRegChannelBase + static_cast<uint16_t>(kMaxChannels) * kRegChannelSpan);

// The fixed width (octets) of the relative-start-address prefix every
// configuration request's payload begins with — a 16-bit big-endian field,
// followed by the configuration data octets to write from that address
// onward (§12.7.1).
constexpr size_t kReconfigAddrLen = 2;

using SpiRegisterBlock = std::array<uint8_t, kEpFuncLen>;

namespace detail {
constexpr uint8_t kEnableClrBitEnable = 1u << 0;
constexpr uint8_t kEnableClrBitClear  = 1u << 4;
constexpr uint8_t kOptionsBitReqCrc   = 1u << 0;
constexpr uint8_t kOptionsBitRespTs   = 1u << 3;
constexpr uint8_t kOptionsBitSuppress = 1u << 7;

// The inverse of mode_cpol()/mode_cpha(): recovers the mode implied by a
// (cpol, cpha) bit pair. The mapping is bijective (each of the four modes
// yields a distinct pair), so this is a lossless round trip.
constexpr SpiMode mode_from_bits(bool cpol, bool cpha) noexcept {
    if (!cpol && !cpha) return SpiMode::Mode0;
    if (!cpol && cpha) return SpiMode::Mode1;
    if (cpol && !cpha) return SpiMode::Mode2;
    return SpiMode::Mode3;
}

// True iff the octet at relative offset addr belongs to a read-only register
// of the block — EP_LEN, NR_CS, or any channel's own reserved octet
// (computed via the channel span's own modulus, so it applies uniformly to
// all kMaxChannels channels).
inline bool reg_offset_read_only(uint16_t addr) noexcept {
    if (addr == kRegEpLen || addr == kRegNrCs) return true;
    if (addr >= kRegChannelBase) {
        const uint16_t rel = static_cast<uint16_t>((addr - kRegChannelBase) % kRegChannelSpan);
        return rel == kChRegReserved;
    }
    return false;
}
} // namespace detail

// render_registers serializes cfg's EP_func registers into out exactly as a
// configuration *read* of the whole block would report them — the inverse
// of apply_reconfig()'s own parse step. mode's CPOL/CPHA bits are rendered
// via mode_cpol()/mode_cpha(); bit_order and trigger have no wire
// counterpart (see the file header) and are not rendered.
inline void render_registers(const SpiFunctionalCfg& cfg, SpiRegisterBlock& out) noexcept {
    uint8_t enable_clr = 0;
    uint8_t options    = 0;
    if (cfg.ep_enable) enable_clr |= detail::kEnableClrBitEnable;
    if (cfg.ep_clear_req_storage) enable_clr |= detail::kEnableClrBitClear;
    if (cfg.ep_req_crc_enable) options |= detail::kOptionsBitReqCrc;
    if (cfg.ep_response_ts_enable) options |= detail::kOptionsBitRespTs;
    if (cfg.ep_suppress_response) options |= detail::kOptionsBitSuppress;

    out[kRegEpLen] = static_cast<uint8_t>(kEpFuncLen);
    // TC18 0.5.1_RC5: spi_nr_cs is a 4-bit "(count - 1)" field in bits
    // [3:0], upper nibble reserved — see the file header. kMaxChannels (6)
    // renders as 0x05, not a plain 6.
    out[kRegNrCs]         = static_cast<uint8_t>((kMaxChannels - 1u) & 0x0Fu);
    out[kRegEpEnableClr]  = enable_clr;
    out[kRegEpOptions]    = options;
    avtp::detail::put_u16(&out[kRegEpStatus], cfg.ep_status);

    for (uint8_t c = 0; c < kMaxChannels; ++c) {
        const SpiChannelCfg& ch   = cfg.channels[c];
        const uint16_t       base = static_cast<uint16_t>(kRegChannelBase +
                                                            static_cast<uint16_t>(c) * kRegChannelSpan);
        uint8_t cfg_byte = 0;
        if (mode_cpol(ch.mode)) cfg_byte |= kCfgBitClkPolarity;
        if (mode_cpha(ch.mode)) cfg_byte |= kCfgBitClkPhase;
        if (ch.cs_polarity == SpiCsPolarity::ActiveHigh) cfg_byte |= kCfgBitCsPolarity;
        if (ch.use_common_cs) cfg_byte |= kCfgBitUseCs;
        if (ch.deassert_cs_pause) cfg_byte |= kCfgBitDeassertCsPause;

        avtp::detail::put_u16(&out[base + kChRegBaudRate], ch.baud_rate_kbps);
        out[base + kChRegCfg]         = cfg_byte;
        out[base + kChRegCsLeadtime]  = ch.cs_clk_leadtime;
        out[base + kChRegCsTrailtime] = ch.clk_cs_trailtime;
        out[base + kChRegBitsMax]     = ch.bits_max;
        out[base + kChRegPauseMin]    = ch.pause_min;
        out[base + kChRegReserved]    = 0;
    }
}

namespace detail {
// The inverse of render: adopts every R/W register from an already-patched
// block image. The read-only offsets (EP_LEN, NR_CS, and each channel's own
// reserved octet) are deliberately not read back — apply_reconfig()
// re-renders them from cfg before patching, so a write covering them is a
// no-op. bit_order and trigger have no wire counterpart and are left
// untouched here, exactly as render leaves them unrendered.
inline void parse_registers(SpiFunctionalCfg& cfg, const SpiRegisterBlock& in) noexcept {
    const uint8_t enable_clr = in[kRegEpEnableClr];
    const uint8_t options    = in[kRegEpOptions];

    cfg.ep_enable             = (enable_clr & kEnableClrBitEnable) != 0;
    cfg.ep_clear_req_storage  = (enable_clr & kEnableClrBitClear) != 0;
    cfg.ep_req_crc_enable     = (options & kOptionsBitReqCrc) != 0;
    cfg.ep_response_ts_enable = (options & kOptionsBitRespTs) != 0;
    cfg.ep_suppress_response  = (options & kOptionsBitSuppress) != 0;

    cfg.ep_status = avtp::detail::get_u16(&in[kRegEpStatus]);

    for (uint8_t c = 0; c < kMaxChannels; ++c) {
        SpiChannelCfg& ch   = cfg.channels[c];
        const uint16_t base = static_cast<uint16_t>(kRegChannelBase +
                                                      static_cast<uint16_t>(c) * kRegChannelSpan);
        const uint8_t cfg_byte = in[base + kChRegCfg];
        const bool    cpol     = (cfg_byte & kCfgBitClkPolarity) != 0;
        const bool    cpha     = (cfg_byte & kCfgBitClkPhase) != 0;

        ch.mode              = mode_from_bits(cpol, cpha);
        ch.cs_polarity       = (cfg_byte & kCfgBitCsPolarity) != 0 ? SpiCsPolarity::ActiveHigh
                                                                    : SpiCsPolarity::ActiveLow;
        ch.use_common_cs     = (cfg_byte & kCfgBitUseCs) != 0;
        ch.deassert_cs_pause = (cfg_byte & kCfgBitDeassertCsPause) != 0;

        ch.baud_rate_kbps  = avtp::detail::get_u16(&in[base + kChRegBaudRate]);
        ch.cs_clk_leadtime = in[base + kChRegCsLeadtime];
        ch.clk_cs_trailtime = in[base + kChRegCsTrailtime];
        ch.bits_max        = in[base + kChRegBitsMax];
        ch.pause_min       = in[base + kChRegPauseMin];
    }
}
} // namespace detail

// ── Reconfig errors ────────────────────────────────────────────────────────────

enum class SpiReconfigErrc : int {
    // payload carries no address prefix, or an address prefix with no data
    // octet after it.
    short_payload = 1,
    // start_address + data length exceeds kEpFuncLen — the whole write is
    // ignored, per the specification's own rule.
    out_of_range = 2,
};

inline const std::error_category& spi_reconfig_category() noexcept {
    struct Cat : std::error_category {
        const char* name() const noexcept override { return "rcp.spi.reconfig"; }
        std::string message(int ev) const override {
            switch (static_cast<SpiReconfigErrc>(ev)) {
            case SpiReconfigErrc::short_payload:
                return "rcp/spi: SPI configuration write has no address and data";
            case SpiReconfigErrc::out_of_range:
                return "rcp/spi: SPI configuration write extends past the EP_func block";
            default:
                return "rcp/spi: SPI unknown configuration-write error";
            }
        }
    };
    static Cat instance;
    return instance;
}

inline std::error_code make_error_code(SpiReconfigErrc e) noexcept {
    return {static_cast<int>(e), spi_reconfig_category()};
}

// apply_reconfig applies the configuration escape hatch (evt[2:0] == 111b):
// payload is NOT presented at the interface but interpreted as an addressed
// write into this endpoint's own EP_func block — a 16-bit big-endian
// relative start address followed by the configuration data octets to write
// from that address onward (§12.7.1). Returns SpiReconfigErrc::short_payload
// when payload_len is not at least kReconfigAddrLen + 1, and
// SpiReconfigErrc::out_of_range when the addressed span would extend past
// kEpFuncLen; in both cases cfg is left entirely unchanged, per the
// specification's own "such a payload is to be ignored" rule. Octets of the
// addressed span that land on a read-only register (EP_LEN, NR_CS, or a
// channel's own reserved octet) are left at their current values while the
// rest of the span is still applied — the write is applied at octet
// granularity over the block's rendered image, so a partially-covered
// multi-octet register is handled correctly.
inline std::error_code apply_reconfig(SpiFunctionalCfg& cfg, const uint8_t* payload,
                                       size_t payload_len) noexcept {
    if (payload_len <= kReconfigAddrLen) return make_error_code(SpiReconfigErrc::short_payload);

    const uint16_t start_address = avtp::detail::get_u16(payload);
    const size_t   data_len      = payload_len - kReconfigAddrLen;

    if (static_cast<size_t>(start_address) + data_len > static_cast<size_t>(kEpFuncLen))
        return make_error_code(SpiReconfigErrc::out_of_range);

    SpiRegisterBlock block{};
    render_registers(cfg, block);
    for (size_t i = 0; i < data_len; ++i) {
        const auto addr = static_cast<uint16_t>(start_address + i);
        if (detail::reg_offset_read_only(addr)) continue; // write ignored
        block[addr] = payload[kReconfigAddrLen + i];
    }
    detail::parse_registers(cfg, block);
    return {};
}

// encode_reconfig_request encodes an ACF_ABB configuration request (evt[2:0]
// == 111b) addressed to byte_bus_id: payload is start_address (16-bit
// big-endian) followed by data. Returns an empty vector if data is empty, or
// if the encoded payload would exceed acf::kAcfAbbMaxPayload.
inline std::vector<uint8_t> encode_reconfig_request(avtp::ByteBusId byte_bus_id, uint16_t start_address,
                                                      const std::vector<uint8_t>& data,
                                                      uint8_t transaction_num) {
    if (data.empty()) return {};
    if (kReconfigAddrLen + data.size() > acf::kAcfAbbMaxPayload) return {};

    std::vector<uint8_t> payload(kReconfigAddrLen + data.size());
    avtp::detail::put_u16(payload.data(), start_address);
    std::copy(data.begin(), data.end(), payload.begin() + static_cast<long>(kReconfigAddrLen));

    acf::AcfMessageInfo hdr;
    hdr.byte_bus_id     = byte_bus_id;
    hdr.op              = true; // write -- §12.7.1: the write request's payload is written into EP_func
    hdr.evt_op          = 0x7u; // evt[2:0] = 111b, TC18 Table 33 SPI row
    hdr.transaction_num = transaction_num;
    return acf::encode_acf_abb(hdr, payload);
}

// ── Errors ────────────────────────────────────────────────────────────────────

enum class SpiErrc : int {
    short_frame  = 1,
    bad_msg_type = 2,
    wrong_bus    = 3,
    wrong_op     = 4,
    bad_channel  = 5,
};

inline const std::error_category& spi_category() noexcept {
    struct Cat : std::error_category {
        const char* name() const noexcept override { return "rcp.spi"; }
        std::string message(int ev) const override {
            switch (static_cast<SpiErrc>(ev)) {
            case SpiErrc::short_frame:  return "rcp/spi: frame too short";
            case SpiErrc::bad_msg_type: return "rcp/spi: unexpected ACF message type";
            case SpiErrc::wrong_bus:    return "rcp/spi: wrong byte_bus_id";
            case SpiErrc::wrong_op:     return "rcp/spi: wrong ACF op";
            case SpiErrc::bad_channel:  return "rcp/spi: invalid channel selector (evt[2:0] not in 0..5)";
            default:                    return "rcp/spi: unknown error";
            }
        }
    };
    static Cat instance;
    return instance;
}

inline std::error_code make_error_code(SpiErrc e) noexcept {
    return {static_cast<int>(e), spi_category()};
}

// channel_of decodes a request's evt[2:0] field as an SPI channel selector
// (extraction §5.4; TC18 §13.5 Table 33's own SPI row — see the file
// header's channel-selection note). This is SPI's own decode, distinct from
// rcp::endpoint::write_semantics_of and rcp::endpoint::evt_row2_kind_of —
// the same 3-bit wire field means something different for SPI than for
// GPIO/PWM_OUT or the Table 33 Row 2 endpoint types.
inline std::error_code channel_of(uint8_t evt_op, uint8_t& out_channel) noexcept {
    const uint8_t channel = static_cast<uint8_t>(evt_op & 0x07);
    if (!channel_valid(channel)) return make_error_code(SpiErrc::bad_channel);
    out_channel = channel;
    return {};
}

// ── Transfer request ──────────────────────────────────────────────────────────
// A transfer request's payload is the PICO-out (controller-to-peripheral)
// bytes to shift out; the matching response's payload is the same-length
// POCI-in (peripheral-to-controller) bytes captured during that same
// transfer. Both halves are encoded as the read direction: a transfer
// request carries the PICO-out bytes *and* asks for the POCI-in bytes back
// (the specification's own worked SPI example — write N bytes, get a
// response with M — carries op=0 with a non-zero read_size), and the
// response carries the POCI-in bytes.

// encode_transfer_request encodes an ACF_ABB transfer request addressed to
// byte_bus_id: evt's low three bits carry channel (0..kMaxChannels-1; any
// other bits are left 0), the payload is exactly tx_data (the PICO-out bytes
// to shift out), and read_size carries the ACF header's own
// read_size_or_segment_num field — see transfer_length() for what it means
// for the actual bus transfer length. Returns an empty vector if tx_data
// exceeds acf::kAcfAbbMaxPayload.
inline std::vector<uint8_t> encode_transfer_request(avtp::ByteBusId byte_bus_id, uint8_t channel,
                                                      const std::vector<uint8_t>& tx_data,
                                                      uint16_t read_size, uint8_t transaction_num) {
    if (tx_data.size() > acf::kAcfAbbMaxPayload) return {};

    acf::AcfMessageInfo hdr;
    hdr.byte_bus_id              = byte_bus_id;
    hdr.op                        = false; // read direction -- see this section's own header comment
    hdr.evt_op                    = static_cast<uint8_t>(channel & 0x7u);
    hdr.read_size_or_segment_num  = read_size;
    hdr.transaction_num           = transaction_num;
    return acf::encode_acf_abb(hdr, tx_data);
}

// decode_transfer_request decodes and validates an ACF-level SPI transfer
// request. Fails with SpiErrc::short_frame (frame shorter than the ACF_ABB
// fixed header or its declared payload length), SpiErrc::bad_msg_type (not
// an ACF_ABB message), SpiErrc::wrong_bus (byte_bus_id != expected_bus_id),
// SpiErrc::wrong_op (op is not the read direction), or SpiErrc::bad_channel
// (evt[2:0] is not channel_valid()). On success, out_channel, out_tx_data,
// out_read_size, and out_transaction_num are populated. See
// transfer_length() for combining out_tx_data.size() and out_read_size into
// the actual bus transfer length TC18 §13.7.3.3 requires.
inline std::error_code decode_transfer_request(const uint8_t* b, size_t len, avtp::ByteBusId expected_bus_id,
                                                uint8_t& out_channel, std::vector<uint8_t>& out_tx_data,
                                                uint16_t& out_read_size, uint8_t& out_transaction_num) {
    acf::AcfMessageInfo  hdr;
    std::vector<uint8_t> payload;
    const auto           ec = acf::decode_acf_abb(b, len, hdr, payload);
    if (ec == avtp::make_error_code(avtp::AvtpErrc::short_buffer)) return make_error_code(SpiErrc::short_frame);
    if (ec) return make_error_code(SpiErrc::bad_msg_type);

    if (hdr.byte_bus_id != expected_bus_id) return make_error_code(SpiErrc::wrong_bus);
    // Read direction -- see this section's own header comment.
    if (hdr.op) return make_error_code(SpiErrc::wrong_op);

    const uint8_t channel = static_cast<uint8_t>(hdr.evt_op & 0x7u);
    if (!channel_valid(channel)) return make_error_code(SpiErrc::bad_channel);

    out_channel         = channel;
    out_tx_data          = std::move(payload);
    out_read_size         = hdr.read_size_or_segment_num;
    out_transaction_num   = hdr.transaction_num;
    return {};
}

// REQ-SPI-036: computes the actual SPI bus transfer length in octets for a
// transfer request carrying tx_len bytes of PICO-out payload and a
// read_size of read_size, per TC18 §13.7.3.3's own zero-fill rule: a caller
// driving real SPI hardware clocks tx_data[0..tx_len) verbatim, followed by
// (return value - tx_len) zero octets when read_size > tx_len; POCI is
// captured for the same length; the byte_msg_payload is always presented on
// PICO in full, even when read_size is less than tx_len. Equivalently,
// max(tx_len, read_size) — expressed as its own named primitive both for
// readability and because tx_len is a size_t while read_size is the ACF
// header's own 12-bit-wide uint16_t, two different-width types a bare max()
// would silently promote past their own domains' intent.
inline size_t transfer_length(size_t tx_len, uint16_t read_size) noexcept {
    return (read_size > tx_len) ? static_cast<size_t>(read_size) : tx_len;
}

// ── Response ───────────────────────────────────────────────────────────────────

// encode_response encodes an SPI response carrying rx_data (the POCI-in
// bytes captured during the transfer) as its payload, with evt's low three
// bits carrying channel, echoing transaction_num. Encoded as ACF_ABB when
// timed is false; as ACF_GBB (message_timestamp = timestamp, mtv valid)
// when timed is true.
inline std::vector<uint8_t> encode_response(avtp::ByteBusId byte_bus_id, uint8_t channel,
                                             const std::vector<uint8_t>& rx_data, uint8_t transaction_num,
                                             bool timed, uint64_t timestamp) {
    acf::AcfMessageInfo hdr;
    hdr.byte_bus_id     = byte_bus_id;
    hdr.op               = false; // read
    hdr.rsp               = true;
    hdr.evt_op             = static_cast<uint8_t>(channel & 0x7u);
    hdr.transaction_num   = transaction_num;

    if (timed) {
        hdr.mtv = true;
        return acf::encode_acf_gbb(hdr, timestamp, rx_data);
    }
    return acf::encode_acf_abb(hdr, rx_data);
}

// decode_response decodes an SPI response from either an ACF_ABB or ACF_GBB
// message (peeks the ACF message type itself, since a response's encoding
// depends on the responding endpoint's own timed/untimed choice). Fails with
// SpiErrc::short_frame, SpiErrc::wrong_bus, or SpiErrc::bad_channel
// (evt[2:0] is not channel_valid()). On success, out_channel and
// out_transaction_num are populated; out_rx_data carries the POCI-in
// payload; out_timed/out_timestamp report whether the message was ACF_GBB
// with a valid timestamp, and that timestamp's value (0 when !out_timed).
inline std::error_code decode_response(const uint8_t* b, size_t len, avtp::ByteBusId expected_bus_id,
                                        uint8_t& out_channel, std::vector<uint8_t>& out_rx_data,
                                        bool& out_timed, uint64_t& out_timestamp,
                                        uint8_t& out_transaction_num) {
    uint8_t msg_type = 0;
    if (acf::peek_msg_type(b, len, msg_type)) return make_error_code(SpiErrc::short_frame);

    acf::AcfMessageInfo  hdr;
    std::vector<uint8_t> payload;
    avtp::ByteBusId       bus_id = 0;
    uint8_t                evt    = 0;
    uint8_t                txn    = 0;
    bool                    timed     = false;
    uint64_t                timestamp = 0;

    if (msg_type == acf::kAcfMsgTypeGbb) {
        uint64_t   ts = 0;
        const auto ec = acf::decode_acf_gbb(b, len, hdr, ts, payload);
        if (ec == avtp::make_error_code(avtp::AvtpErrc::short_buffer)) return make_error_code(SpiErrc::short_frame);
        if (ec) return make_error_code(SpiErrc::bad_msg_type);
        bus_id    = hdr.byte_bus_id;
        evt       = hdr.evt_op;
        txn       = hdr.transaction_num;
        timed     = hdr.mtv;
        timestamp = timed ? ts : 0;
    } else {
        const auto ec = acf::decode_acf_abb(b, len, hdr, payload);
        if (ec == avtp::make_error_code(avtp::AvtpErrc::short_buffer)) return make_error_code(SpiErrc::short_frame);
        if (ec) return make_error_code(SpiErrc::bad_msg_type);
        bus_id    = hdr.byte_bus_id;
        evt       = hdr.evt_op;
        txn       = hdr.transaction_num;
        timed     = false;
        timestamp = 0;
    }

    if (bus_id != expected_bus_id) return make_error_code(SpiErrc::wrong_bus);

    const uint8_t channel = static_cast<uint8_t>(evt & 0x7u);
    if (!channel_valid(channel)) return make_error_code(SpiErrc::bad_channel);

    out_channel         = channel;
    out_rx_data           = std::move(payload);
    out_timed              = timed;
    out_timestamp           = timestamp;
    out_transaction_num     = txn;
    return {};
}

// ── SPI status-report width ───────────────────────────────────────────────────
// The largest status-report width this endpoint type's transfer-done status
// may carry (extraction §4.6's "up to 20 status bytes" ceiling). Not itself
// enforced by any encode/decode function above (no status-report codec is in
// this milestone's scope). Unrelated to compound-wait's own comparison
// length — see the file header's own note on why this module carries no
// compound-wait logic of its own.

constexpr size_t kMaxStatusBytes = 20;

// ── Trigger-signal bookkeeping ids ────────────────────────────────────────────
// One instance of each of transfer-complete and per-CS assert/de-assert per
// channel, built on rcp/endpoint.hpp's generic TriggerRegistry — this
// module's own internal SignalId bookkeeping scheme (kept deliberately
// distinct from trigger_signal_number()'s Table 41 wire-visible numbering
// above, the same split rcp/gpio.hpp's gpio_signal_id/trigger_signal_number
// pair establishes).

enum class SpiSignal : uint8_t { TransferComplete = 0, CsAssert = 1, CsDeassert = 2 };

constexpr endpoint::TriggerRegistry::SignalId spi_signal_id(uint8_t channel, SpiSignal sig) noexcept {
    return static_cast<endpoint::TriggerRegistry::SignalId>((uint16_t(channel) << 2) | uint16_t(sig));
}

// ── SpiEndpoint ───────────────────────────────────────────────────────────────
// A per-transfer convenience wrapper — no c-RCP equivalent (c-RCP is a pure
// free-function codec; see the free functions above for the real ACF-level
// wire content this class does not itself encode/decode): records the
// exchanged bytes of one transfer and fires that channel's trigger signals
// in the order a client would observe them.
class SpiEndpoint {
public:
    // transfer performs one full-duplex byte exchange on `channel`: CS is
    // asserted, `pico_out` is the bytes sent out, `poci_in` is this
    // implementation's record of the bytes received over the same exchange
    // (supplied by the caller — this class models the request/response and
    // trigger-signal shape of an SPI transfer, not an actual bus), and CS is
    // de-asserted again. Fragmentation is out of scope (v2.8.0 no-go), so
    // this models one transfer per request rather than a CS line held
    // across several.
    std::error_code transfer(uint8_t channel, std::vector<uint8_t> pico_out,
                              std::vector<uint8_t> poci_in) {
        if (!channel_valid(channel)) return make_error_code(SpiErrc::bad_channel);

        triggers_.notify(spi_signal_id(channel, SpiSignal::CsAssert));
        last_pico_out_[channel] = std::move(pico_out);
        last_poci_in_[channel]  = std::move(poci_in);
        triggers_.notify(spi_signal_id(channel, SpiSignal::TransferComplete));
        triggers_.notify(spi_signal_id(channel, SpiSignal::CsDeassert));
        return {};
    }

    const std::vector<uint8_t>& last_sent(uint8_t channel) const { return last_pico_out_.at(channel); }
    const std::vector<uint8_t>& last_received(uint8_t channel) const { return last_poci_in_.at(channel); }
    endpoint::TriggerRegistry& triggers() noexcept { return triggers_; }

private:
    endpoint::TriggerRegistry                      triggers_;
    std::array<std::vector<uint8_t>, kMaxChannels> last_pico_out_;
    std::array<std::vector<uint8_t>, kMaxChannels> last_poci_in_;
};

} // namespace spi
} // namespace rcp

// Enable std::error_code construction from rcp::spi::SpiErrc / SpiReconfigErrc.
namespace std {
template <>
struct is_error_code_enum<rcp::spi::SpiErrc> : true_type {};
template <>
struct is_error_code_enum<rcp::spi::SpiReconfigErrc> : true_type {};
} // namespace std
