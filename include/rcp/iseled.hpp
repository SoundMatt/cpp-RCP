// fusa:req REQ-ISELED-001
// fusa:req REQ-ISELED-002
// fusa:req REQ-ISELED-003
// fusa:req REQ-ISELED-004
// fusa:req REQ-ISELED-005
// fusa:req REQ-ISELED-006
// fusa:req REQ-ISELED-007
// fusa:req REQ-ISELED-008
// fusa:req REQ-ISELED-009
// fusa:req REQ-ISELED-010
// fusa:req REQ-ISELED-011
// fusa:req REQ-ISELED-012
// fusa:req REQ-ISELED-013
// fusa:req REQ-ISELED-014
// fusa:req REQ-ISELED-015
// fusa:req REQ-ISELED-016
// fusa:req REQ-ISELED-017
// fusa:req REQ-ISELED-018
// fusa:req REQ-ISELED-019
// fusa:req REQ-ISELED-020
// fusa:req REQ-ISELED-021
// fusa:req REQ-ISELED-022
// fusa:req REQ-ISELED-023
// fusa:req REQ-ISELED-024
// fusa:req REQ-ISELED-025
// fusa:req REQ-ISELED-026
// fusa:req REQ-ISELED-027
// fusa:req REQ-ISELED-029
// fusa:req REQ-ISELED-030
// fusa:req REQ-ISELED-031
// fusa:req REQ-ISELED-032
// fusa:req REQ-ISELED-033
// fusa:req REQ-ISELED-034
// fusa:req REQ-ISELED-035
// fusa:req REQ-ISELED-036
// fusa:req REQ-ISELED-037
// fusa:req REQ-ISELED-038
// fusa:req REQ-ISELED-039
// fusa:req REQ-ISELED-040
// fusa:req REQ-ISELED-041
// fusa:req REQ-ISELED-042
// REQ-ISELED-028 is retired (stale duplicate of REQ-ISELED-007) — see
// c-RCP's .fusa-reqs.json; not ported here.

// ISELED endpoint (ep_type 0x0C) — the OPEN Alliance TC18 Remote Control
// Protocol Specification v0.5.1_RC5's ACF-level raw byte-stream command/
// read/response codec for the ISELED daisy-chain (§13.7.12.1/§13.7.12.3),
// its native 4-bit/5-bit even-parity bit framing plus an independent,
// optional CRC-8 integrity layer for actually driving the physical ISP_P/
// ISP_N pair, its Table 58 functional-configuration register block
// (§13.7.12.2), and read-direction response fragmentation bounded by
// read_size (§13.7.12.1, rcp/fragment.hpp).
//
// Phase 3 rewrite (cpp-RCP issue #129, ROADMAP.md "Phase 17"), ported from
// c-RCP's include/rcp/ep_iseled.h + src/ep_iseled.c — this project's
// RC5-spec-conformant reference for this module, itself the product of a
// real fix history (issue #270's requires_isp_n polarity correction, issue
// #256 Group I's Table 58 register-block + evt[2:0]=111b reconfig work,
// issue #471's read-direction request, and the REQ-ISELED-025 response-
// fragmentation work) — this port reflects that current, post-fix state.
//
// This is a wholesale content replacement of this header's own pre-rewrite
// design (last touched at v2.20.0/#120), not an incremental patch: the
// prior IseledRequest/IseledResponse structs modeled a 4-bit Instruction/
// 12-bit Address/12-bit Data shape as ACF *payload structure* — but c-RCP's
// actual ACF-level codec (encode_command_request/encode_read_request/
// encode_response below) treats the byte_msg_payload as an opaque raw byte
// stream, exactly like every other raw-byte-stream endpoint type (I2C,
// LIN, CAN) — it does not itself decode Instruction/Address/Data at the ACF
// layer at all. That structured decoding is a *bus-physical-layer* concern
// this module keeps separate: the real Instruction/Address/Data framing
// (and the invented, non-spec-sourced 12-bit field widths and CRC-8 the
// prior design used to represent it) belongs to the ISELED standard itself
// — a separate, independently-documented industry protocol this repository
// has no verified copy of — never to the RCP wire codec. The native 4-bit/
// 5-bit symbol framing and CRC-8 ported below are c-RCP's own original,
// clearly-labeled design (poly 0x07, init 0x00, no reflection — a standard,
// publicly documented small CRC, not derived from the confidential TC18
// extraction or from ISELED's own specification) for that separate,
// physical-layer job, matching the "separate jobs, never conflated" split
// c-RCP's own file header draws.
//
// ── Response fragmentation and rcp/fragment.hpp ─────────────────────────────
// c-RCP's ep_iseled.c genuinely `#include`s "rcp/fragment.h" and calls its
// rcp_fragment_plan_count()/rcp_fragment_plan() directly from
// rcp_ep_iseled_response_fragment_count()/_encode_response_fragmented() —
// unlike an endpoint-specific multi-response dispatch mechanism, ISELED's
// own response fragmentation genuinely is the generic fragment module
// applied to one endpoint type's read-direction response, so this port
// wires rcp/iseled.hpp to rcp/fragment.hpp (fragment::plan_count()/
// fragment::plan()) the same way, rather than inventing ISELED-specific
// fragmentation logic.
//
// Field names and behavior below implement TC18's *behavior* as described in
// an internal structured extraction of the specification named above; no
// text from that document is reproduced here. The concrete struct/enum
// shapes chosen in this file are this implementation's own, same as the
// equivalent disclaimers in rcp/acf.hpp, rcp/avtp.hpp, rcp/endpoint.hpp,
// rcp/lifecycle.hpp, rcp/fragment.hpp, and rcp/i2c.hpp.
#pragma once

#include <rcp/acf.hpp>
#include <rcp/avtp.hpp>
#include <rcp/endpoint.hpp>
#include <rcp/fragment.hpp>
#include <rcp/lifecycle.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <system_error>
#include <vector>

namespace rcp {
namespace iseled {

// ── Native 4-bit/5-bit bit framing (§13.7.12.1) ───────────────────────────────
// Each 4-bit data nibble is framed onto the ISP_P/ISP_N pair as a 5-bit
// symbol: the low 4 bits carry the nibble unchanged, and bit 4 carries that
// nibble's own even-parity bit. A full content buffer is framed two symbols
// per octet, high nibble first, one symbol value (0-31) stored per output
// octet.

namespace detail {
constexpr uint8_t popcount4(uint8_t n) noexcept {
    uint8_t c = 0;
    uint8_t v = static_cast<uint8_t>(n & 0x0F);
    while (v != 0) {
        c = static_cast<uint8_t>(c + (v & 0x01));
        v = static_cast<uint8_t>(v >> 1);
    }
    return c;
}
} // namespace detail

// symbol_encode: encodes nibble's low 4 bits into a 5-bit even-parity
// symbol (bit 4 = even parity of bits [3:0]). Any bits of nibble above bit
// 3 are ignored. Return value is always in 0..31.
constexpr uint8_t symbol_encode(uint8_t nibble) noexcept {
    const uint8_t n      = static_cast<uint8_t>(nibble & 0x0F);
    const uint8_t parity = static_cast<uint8_t>(detail::popcount4(n) & 0x01);
    return static_cast<uint8_t>(static_cast<uint8_t>(parity << 4) | n);
}

// symbol_decode: decodes symbol (only bits [4:0] are inspected) back into a
// 4-bit nibble. Returns true and sets out_nibble to bits [3:0] of symbol
// iff bit 4 equals the even parity of bits [3:0]; returns false (leaving
// out_nibble untouched) for any symbol whose parity bit does not match.
inline bool symbol_decode(uint8_t symbol, uint8_t& out_nibble) noexcept {
    const uint8_t s           = static_cast<uint8_t>(symbol & 0x1F);
    const uint8_t n            = static_cast<uint8_t>(s & 0x0F);
    const uint8_t parity_bit   = static_cast<uint8_t>((s >> 4) & 0x01);
    const uint8_t want_parity = static_cast<uint8_t>(detail::popcount4(n) & 0x01);
    if (parity_bit != want_parity) return false;
    out_nibble = n;
    return true;
}

// bitframe_encoded_len: the number of framed octets encode_bitframe()
// produces for data_len octets of plain content, optionally with the
// one-octet CRC-8 trailer appended before framing: 2 * (data_len +
// (append_crc ? 1 : 0)).
constexpr size_t bitframe_encoded_len(size_t data_len, bool append_crc) noexcept {
    return (data_len + (append_crc ? size_t{1} : size_t{0})) * 2;
}

// ── ISELED-level CRC (distinct from rcp/e2e.hpp; see the file header) ────────
// A standard CRC-8 (poly 0x07, init 0x00, no input/output reflection) over
// data — a second, independent integrity layer from rcp/e2e.hpp's own
// CRC-16, gated by IseledFunctionalCfg::crc_enable.
inline uint8_t crc8(const uint8_t* data, size_t len) noexcept {
    constexpr uint8_t poly = 0x07;
    uint8_t crc = 0x00;
    for (size_t i = 0; i < len; ++i) {
        crc = static_cast<uint8_t>(crc ^ data[i]);
        for (int b = 0; b < 8; ++b)
            crc = (crc & 0x80) ? static_cast<uint8_t>(static_cast<uint8_t>(crc << 1) ^ poly)
                                : static_cast<uint8_t>(crc << 1);
    }
    return crc;
}
inline uint8_t crc8(const std::vector<uint8_t>& data) noexcept {
    return crc8(data.empty() ? nullptr : data.data(), data.size());
}

// ── Errors ────────────────────────────────────────────────────────────────────

enum class IseledErrc : int {
    short_frame       = 1,
    bad_msg_type      = 2,
    wrong_bus         = 3,
    wrong_op          = 4,
    bad_symbol        = 5,
    crc_mismatch      = 6,
    odd_symbol_count  = 7,
    // evt[2:0] is not 0b000, TC18 §13.5 Table 33's only legal value for a
    // plain (non-configuration) request in ISELED's endpoint-type row —
    // caller shall respond with error code UNSUPPORTED_CMD.
    bad_evt            = 8,
    // handle_request()'s own convenience-wrapper limitation — see that
    // method's own comment and I2cErrc::config_write_not_supported's
    // identical rationale (rcp/i2c.hpp).
    config_write_not_supported = 9,
};

inline const std::error_category& iseled_category() noexcept {
    struct Cat : std::error_category {
        const char* name() const noexcept override { return "rcp.iseled"; }
        std::string message(int ev) const override {
            switch (static_cast<IseledErrc>(ev)) {
            case IseledErrc::short_frame:      return "rcp/iseled: frame too short";
            case IseledErrc::bad_msg_type:     return "rcp/iseled: unexpected ACF message type";
            case IseledErrc::wrong_bus:        return "rcp/iseled: wrong byte_bus_id";
            case IseledErrc::wrong_op:         return "rcp/iseled: wrong ACF op";
            case IseledErrc::bad_symbol:       return "rcp/iseled: invalid bit-framing symbol";
            case IseledErrc::crc_mismatch:     return "rcp/iseled: native ISELED CRC-8 mismatch";
            case IseledErrc::odd_symbol_count:
                return "rcp/iseled: odd symbol count — every octet frames to two symbols";
            case IseledErrc::bad_evt:          return "rcp/iseled: evt[2:0] is not 0b000";
            case IseledErrc::config_write_not_supported:
                return "rcp/iseled: evt[2:0]=111b configuration-write requests are not supported "
                       "by this convenience wrapper — call apply_reconfig() directly";
            default: return "rcp/iseled: unknown error";
            }
        }
    };
    static Cat instance;
    return instance;
}

inline std::error_code make_error_code(IseledErrc e) noexcept {
    return {static_cast<int>(e), iseled_category()};
}

// encode_bitframe: bit-frames data into a newly built symbol vector of
// bitframe_encoded_len(data.size(), append_crc) octets, each holding one
// symbol_encode() result (0-31) in its low 5 bits. When append_crc is true,
// crc8(data) is framed as one extra trailing content octet before the two-
// symbols-per-octet expansion. High nibble framed before low nibble for
// every content octet, including the trailing CRC octet. Returns an empty
// vector iff bitframe_encoded_len(data.size(), append_crc) would be 0.
inline std::vector<uint8_t> encode_bitframe(const std::vector<uint8_t>& data, bool append_crc) {
    const size_t content_len = data.size() + (append_crc ? size_t{1} : size_t{0});
    const size_t n           = content_len * 2;
    if (n == 0) return {};

    const uint8_t trailer = append_crc ? crc8(data) : 0;
    std::vector<uint8_t> out(n);
    for (size_t i = 0; i < content_len; ++i) {
        const uint8_t octet = (i < data.size()) ? data[i] : trailer;
        out[2 * i]     = symbol_encode(static_cast<uint8_t>(octet >> 4));
        out[2 * i + 1] = symbol_encode(static_cast<uint8_t>(octet & 0x0F));
    }
    return out;
}

// decode_bitframe reverses encode_bitframe(): decodes symbols[0..symbol_count)
// back into plain content. Fails with odd_symbol_count if symbol_count is
// odd; short_frame if expect_crc is true and symbol_count yields fewer than
// one content octet (no room for the CRC trailer itself); bad_symbol if any
// symbol fails symbol_decode(); crc_mismatch if expect_crc is true and the
// trailing octet does not equal crc8() of the preceding content. On
// success, out_data holds the decoded plain content — the trailing CRC
// octet is verified but not included in out_data when expect_crc is true.
inline std::error_code decode_bitframe(const uint8_t* symbols, size_t symbol_count, bool expect_crc,
                                        std::vector<uint8_t>& out_data) {
    out_data.clear();
    if ((symbol_count & size_t{1}) != 0) return make_error_code(IseledErrc::odd_symbol_count);

    const size_t byte_count = symbol_count / 2;
    if (expect_crc && byte_count == 0) return make_error_code(IseledErrc::short_frame);

    std::vector<uint8_t> bytes(byte_count);
    for (size_t i = 0; i < byte_count; ++i) {
        uint8_t hi = 0, lo = 0;
        if (!symbol_decode(symbols[2 * i], hi) || !symbol_decode(symbols[2 * i + 1], lo))
            return make_error_code(IseledErrc::bad_symbol);
        bytes[i] = static_cast<uint8_t>(static_cast<uint8_t>(hi << 4) | lo);
    }

    if (expect_crc) {
        const size_t  data_len = byte_count - 1;
        const uint8_t want     = crc8(bytes.data(), data_len);
        if (bytes[byte_count - 1] != want) return make_error_code(IseledErrc::crc_mismatch);
        out_data.assign(bytes.begin(), bytes.begin() + static_cast<long>(data_len));
    } else {
        out_data = std::move(bytes);
    }
    return {};
}

// ── Recovered-clock mode (§13.7.12.2 Table 58) ────────────────────────────────
// requires_isp_n: true iff the ISP_N pin must be wired/mapped for this
// endpoint to operate — true iff use_rcv_clk is true (device-provided
// clock, which arrives on ISP_N), false iff the Freq_Sync pattern is used
// instead.
constexpr bool requires_isp_n(bool use_rcv_clk) noexcept { return use_rcv_clk; }

// ── Transmission-complete trigger (§13.7.12.1) ────────────────────────────────

enum class IseledTrigger : uint8_t { None = 0, TxComplete = 1 };

// trigger_fires: true iff tx_complete_event satisfies trigger — never for
// None; for TxComplete iff tx_complete_event is true.
constexpr bool trigger_fires(IseledTrigger trigger, bool tx_complete_event) noexcept {
    return trigger == IseledTrigger::TxComplete && tx_complete_event;
}

// ── Functional config (§13.7.12.2 Table 58) ───────────────────────────────────
// The common EP_func prefix is modeled directly as members here, same as
// rcp/i2c.hpp's I2cFunctionalCfg — see that header's own identical note on
// why (rcp/regmap.hpp's own EndpointFunctionalConfig is still an opaque
// byte blob pending Phase 4).
struct IseledFunctionalCfg {
    bool ep_enable             = false;
    bool ep_clear_req_storage  = false;
    bool ep_req_crc_enable     = false;
    bool ep_response_ts_enable = false;
    bool ep_suppress_response  = false;

    uint32_t bit_clk_divider = 0;     // this endpoint's own outbound bit-time clock divider
    bool     use_rcv_clk     = false; // 0x0009.4, R/W — recovered-clock mode; see requires_isp_n()
    // Gates the native ISELED CRC-8 trailer (encode_bitframe's append_crc)
    // — NOT part of the Table 58 register block below (Table 58 defines no
    // register for it; never rendered onto or parsed from the wire).
    bool     crc_enable      = false;
    uint8_t  trigger          = static_cast<uint8_t>(IseledTrigger::None); // IseledTrigger

    uint16_t base_clk        = 0; // 0x0004, R
    uint16_t ep_status       = 0; // 0x0006, R/W
    uint8_t  wire_clk_divider = 0; // 0x0008, R/W — the real wire register, distinct from bit_clk_divider
    bool     collect_resp     = false; // 0x0009.3, R/W
    uint16_t nr_leds          = 0; // 0x000A, R/W
    uint16_t rcv_timeout      = 0; // 0x000C, R/W
};

inline void iseled_functional_cfg_init(IseledFunctionalCfg& cfg) noexcept { cfg = IseledFunctionalCfg{}; }

// iseled_functional_cfg_writable is a thin, named wrapper over
// rcp/lifecycle.hpp's field_writable() (FieldKind::FunctionalW) — reuses,
// never duplicates, that function's authorization logic.
inline bool iseled_functional_cfg_writable(lifecycle::ServerState state,
                                            lifecycle::WriterCtx writer) noexcept {
    return lifecycle::field_writable(state, lifecycle::FieldKind::FunctionalW, writer);
}

inline bool set_bit_clk_divider(IseledFunctionalCfg& cfg, uint32_t divider, lifecycle::ServerState state,
                                 lifecycle::WriterCtx writer) noexcept {
    if (!iseled_functional_cfg_writable(state, writer)) return false;
    cfg.bit_clk_divider = divider;
    return true;
}

inline bool set_use_rcv_clk(IseledFunctionalCfg& cfg, bool use_rcv_clk, lifecycle::ServerState state,
                             lifecycle::WriterCtx writer) noexcept {
    if (!iseled_functional_cfg_writable(state, writer)) return false;
    cfg.use_rcv_clk = use_rcv_clk;
    return true;
}

inline bool set_crc_enable(IseledFunctionalCfg& cfg, bool enable, lifecycle::ServerState state,
                            lifecycle::WriterCtx writer) noexcept {
    if (!iseled_functional_cfg_writable(state, writer)) return false;
    cfg.crc_enable = enable;
    return true;
}

inline bool set_trigger(IseledFunctionalCfg& cfg, IseledTrigger trigger, lifecycle::ServerState state,
                         lifecycle::WriterCtx writer) noexcept {
    if (!iseled_functional_cfg_writable(state, writer)) return false;
    cfg.trigger = static_cast<uint8_t>(trigger);
    return true;
}

// ── The EP_func register block (evt[2:0] == 111b, §13.7.12.2 Table 58) ───────
// Table 58's own printed relative-address column has the same class of
// address-collision editorial defect as I2C's Table 49 (rcp/i2c.hpp):
// iseled_base_clk (16 bit, R) is printed at relative address 0x0001, one
// octet after iseled_ep_len, with no reserved octet at 0x0001 — colliding
// with iseled_ep_enable&clr, separately printed at 0x0002. Resolved via the
// same cross-table structural precedent: iseled_base_clk moves to
// 0x0004-0x0005, pushing iseled_ep_status to 0x0006-0x0007,
// iseled_clk_divider to 0x0008, the flags octet (iseled_collect_resp bit 3,
// iseled_use_rcv_clk bit 4) to 0x0009, iseled_nr_leds to 0x000A-0x000B, and
// iseled_rcv_timeout to 0x000C-0x000D (kEpFuncLen = 0x000E).
constexpr uint16_t kRegEpLen       = 0x0000; //  8 bit, R
constexpr uint16_t kRegReserved01  = 0x0001; //  8 bit, R
constexpr uint16_t kRegEpEnableClr = 0x0002; //  8 bit, R/W
constexpr uint16_t kRegEpOptions   = 0x0003; //  8 bit, R/W
constexpr uint16_t kRegBaseClk     = 0x0004; // 16 bit, R
constexpr uint16_t kRegEpStatus    = 0x0006; // 16 bit, R/W
constexpr uint16_t kRegClkDivider  = 0x0008; //  8 bit, R/W
constexpr uint16_t kRegFlags       = 0x0009; //  8 bit, R/W
constexpr uint16_t kRegNrLeds      = 0x000A; // 16 bit, R/W
constexpr uint16_t kRegRcvTimeout  = 0x000C; // 16 bit, R/W

// The block's own length in octets — one past the last assigned offset.
constexpr size_t kEpFuncLen = 0x000E;

using EpFuncBlock = std::array<uint8_t, kEpFuncLen>;

// Bit masks within the kRegFlags octet — Table 58's own two named
// single-bit parameters, at their corrected relative bit positions
// (0x0009.3/0x0009.4); the remaining bits are reserved and always read 0.
constexpr uint8_t kFlagCollectResp = 1u << 3;
constexpr uint8_t kFlagUseRcvClk   = 1u << 4;

namespace detail {
constexpr uint8_t kEnableClrBitEnable = 1u << 0;
constexpr uint8_t kEnableClrBitClear  = 1u << 4;
constexpr uint8_t kOptionsBitReqCrc   = 1u << 0;
constexpr uint8_t kOptionsBitRespTs   = 1u << 3;
constexpr uint8_t kOptionsBitSuppress = 1u << 7;
} // namespace detail

// render_registers serializes cfg's whole EP_func register block into the
// corrected offsets above — the inverse of apply_reconfig()'s own parse
// step. iseled_crc_enable is NOT part of this block (see the file header)
// and is never touched here.
inline EpFuncBlock render_registers(const IseledFunctionalCfg& cfg) noexcept {
    EpFuncBlock out{};
    uint8_t enable_clr = 0;
    uint8_t options    = 0;
    uint8_t flags      = 0;
    if (cfg.ep_enable) enable_clr |= detail::kEnableClrBitEnable;
    if (cfg.ep_clear_req_storage) enable_clr |= detail::kEnableClrBitClear;
    if (cfg.ep_req_crc_enable) options |= detail::kOptionsBitReqCrc;
    if (cfg.ep_response_ts_enable) options |= detail::kOptionsBitRespTs;
    if (cfg.ep_suppress_response) options |= detail::kOptionsBitSuppress;
    if (cfg.collect_resp) flags |= kFlagCollectResp;
    if (cfg.use_rcv_clk) flags |= kFlagUseRcvClk;

    out[kRegEpLen]         = static_cast<uint8_t>(kEpFuncLen);
    out[kRegReserved01]    = 0;
    out[kRegEpEnableClr]   = enable_clr;
    out[kRegEpOptions]     = options;
    out[kRegBaseClk]       = static_cast<uint8_t>(cfg.base_clk >> 8);
    out[kRegBaseClk + 1]   = static_cast<uint8_t>(cfg.base_clk & 0xFF);
    out[kRegEpStatus]      = static_cast<uint8_t>(cfg.ep_status >> 8);
    out[kRegEpStatus + 1]  = static_cast<uint8_t>(cfg.ep_status & 0xFF);
    out[kRegClkDivider]    = cfg.wire_clk_divider;
    out[kRegFlags]         = flags;
    out[kRegNrLeds]        = static_cast<uint8_t>(cfg.nr_leds >> 8);
    out[kRegNrLeds + 1]    = static_cast<uint8_t>(cfg.nr_leds & 0xFF);
    out[kRegRcvTimeout]    = static_cast<uint8_t>(cfg.rcv_timeout >> 8);
    out[kRegRcvTimeout + 1] = static_cast<uint8_t>(cfg.rcv_timeout & 0xFF);
    return out;
}

namespace detail {
inline void parse_registers(IseledFunctionalCfg& cfg, const EpFuncBlock& in) noexcept {
    const uint8_t enable_clr = in[kRegEpEnableClr];
    const uint8_t options    = in[kRegEpOptions];
    const uint8_t flags      = in[kRegFlags];

    cfg.ep_enable             = (enable_clr & kEnableClrBitEnable) != 0;
    cfg.ep_clear_req_storage  = (enable_clr & kEnableClrBitClear) != 0;
    cfg.ep_req_crc_enable     = (options & kOptionsBitReqCrc) != 0;
    cfg.ep_response_ts_enable = (options & kOptionsBitRespTs) != 0;
    cfg.ep_suppress_response  = (options & kOptionsBitSuppress) != 0;
    cfg.collect_resp          = (flags & kFlagCollectResp) != 0;
    cfg.use_rcv_clk           = (flags & kFlagUseRcvClk) != 0;

    // base_clk (read-only) is deliberately NOT read back here — see
    // reg_offset_read_only() below and apply_reconfig()'s own doc comment;
    // re-rendering from cfg before patching means a write covering it is a
    // no-op, matching c-RCP's identical parse_iseled_registers() design.
    cfg.ep_status      = static_cast<uint16_t>((static_cast<uint16_t>(in[kRegEpStatus]) << 8) | in[kRegEpStatus + 1]);
    cfg.wire_clk_divider = in[kRegClkDivider];
    cfg.nr_leds           = static_cast<uint16_t>((static_cast<uint16_t>(in[kRegNrLeds]) << 8) | in[kRegNrLeds + 1]);
    cfg.rcv_timeout        = static_cast<uint16_t>((static_cast<uint16_t>(in[kRegRcvTimeout]) << 8) | in[kRegRcvTimeout + 1]);
}

// True iff the octet at relative offset addr belongs to a read-only
// register of the block — EP_LEN, the reserved octet, and both octets of
// base_clk.
constexpr bool reg_offset_read_only(uint16_t addr) noexcept {
    return addr == kRegEpLen || addr == kRegReserved01 || addr == kRegBaseClk ||
           addr == static_cast<uint16_t>(kRegBaseClk + 1);
}
} // namespace detail

// The fixed width (octets) of the relative-start-address prefix every
// configuration request's payload begins with.
constexpr size_t kReconfigAddrLen = 2;

enum class IseledReconfigErrc : int {
    short_payload = 1, // payload carries no address prefix, or no data octet after it
    out_of_range  = 2, // start_address + data length exceeds kEpFuncLen — the whole write is ignored
};

inline const std::error_category& iseled_reconfig_category() noexcept {
    struct Cat : std::error_category {
        const char* name() const noexcept override { return "rcp.iseled.reconfig"; }
        std::string message(int ev) const override {
            switch (static_cast<IseledReconfigErrc>(ev)) {
            case IseledReconfigErrc::short_payload:
                return "rcp/iseled: configuration write has no address and data";
            case IseledReconfigErrc::out_of_range:
                return "rcp/iseled: configuration write extends past the EP_func block";
            default: return "rcp/iseled: unknown configuration-write error";
            }
        }
    };
    static Cat instance;
    return instance;
}

inline std::error_code make_error_code(IseledReconfigErrc e) noexcept {
    return {static_cast<int>(e), iseled_reconfig_category()};
}

// apply_reconfig applies the configuration escape hatch (evt[2:0] == 111b):
// payload is a 16-bit big-endian relative start address followed by the
// configuration data octets to write from that address onward (§12.7.1).
// Same octet-granularity patch, read-only-offset-skip, and
// out-of-range-ignores-the-whole-write rules as rcp/i2c.hpp's own
// apply_reconfig().
inline std::error_code apply_reconfig(IseledFunctionalCfg& cfg, const uint8_t* payload,
                                       size_t payload_len) noexcept {
    if (payload_len <= kReconfigAddrLen) return make_error_code(IseledReconfigErrc::short_payload);

    const uint16_t start_address =
        static_cast<uint16_t>((static_cast<uint16_t>(payload[0]) << 8) | payload[1]);
    const size_t data_len = payload_len - kReconfigAddrLen;

    if (static_cast<size_t>(start_address) + data_len > kEpFuncLen)
        return make_error_code(IseledReconfigErrc::out_of_range);

    EpFuncBlock block = render_registers(cfg);
    for (size_t i = 0; i < data_len; ++i) {
        const uint16_t addr = static_cast<uint16_t>(start_address + i);
        if (detail::reg_offset_read_only(addr)) continue;
        block[addr] = payload[kReconfigAddrLen + i];
    }
    detail::parse_registers(cfg, block);
    return {};
}

// encode_reconfig_request encodes an ACF_ABB configuration request
// (evt[2:0] == 111b) addressed to byte_bus_id: payload is start_address
// (16-bit big-endian) followed by data. Returns an empty vector if data is
// empty, or if the encoded payload would exceed acf::kAcfAbbMaxPayload.
inline std::vector<uint8_t> encode_reconfig_request(avtp::ByteBusId byte_bus_id, uint16_t start_address,
                                                      const std::vector<uint8_t>& data,
                                                      uint8_t transaction_num) {
    if (data.empty()) return {};
    if (kReconfigAddrLen + data.size() > acf::kAcfAbbMaxPayload) return {};

    std::vector<uint8_t> payload(kReconfigAddrLen + data.size());
    payload[0] = static_cast<uint8_t>(start_address >> 8);
    payload[1] = static_cast<uint8_t>(start_address & 0xFF);
    std::copy(data.begin(), data.end(), payload.begin() + static_cast<long>(kReconfigAddrLen));

    acf::AcfMessageInfo hdr;
    hdr.byte_bus_id     = byte_bus_id;
    hdr.op              = true; // write
    hdr.evt_op           = 0x7; // evt[2:0] = 111b
    hdr.transaction_num = transaction_num;
    return acf::encode_acf_abb(hdr, payload);
}

// ── Command request (write direction, §13.7.12.3 Figure 41) ──────────────────
// The payload is the raw plain Instruction/Address/Data content — never
// pre-encoded ISELED symbols; this module never inspects, strips, or
// reformats a byte of it at the ACF layer (see the file header).

// encode_command_request encodes an ACF_ABB command request addressed to
// byte_bus_id: payload is exactly tx_data.
inline std::vector<uint8_t> encode_command_request(avtp::ByteBusId byte_bus_id,
                                                     const std::vector<uint8_t>& tx_data,
                                                     uint8_t transaction_num) {
    acf::AcfMessageInfo hdr;
    hdr.byte_bus_id     = byte_bus_id;
    hdr.op              = true; // write
    hdr.evt_op           = 0;
    hdr.transaction_num = transaction_num;
    return acf::encode_acf_abb(hdr, tx_data);
}

// decode_command_request decodes and validates an ACF-level ISELED command
// request. Rejects an op other than write with wrong_op, and evt[2:0] !=
// 0b000 with bad_evt (acf::evt_row2_is_plain(), TC18 §13.5 Table 33).
inline std::error_code decode_command_request(const uint8_t* b, size_t len, avtp::ByteBusId expected_bus_id,
                                               std::vector<uint8_t>& out_tx_data,
                                               uint8_t& out_transaction_num) {
    acf::AcfMessageInfo  hdr;
    std::vector<uint8_t> payload;
    const auto ec = acf::decode_acf_abb(b, len, hdr, payload);
    if (ec == avtp::make_error_code(avtp::AvtpErrc::short_buffer)) return make_error_code(IseledErrc::short_frame);
    if (ec) return make_error_code(IseledErrc::bad_msg_type);
    if (hdr.byte_bus_id != expected_bus_id) return make_error_code(IseledErrc::wrong_bus);
    if (!hdr.op) return make_error_code(IseledErrc::wrong_op);
    if (!acf::evt_row2_is_plain(hdr.evt_op)) return make_error_code(IseledErrc::bad_evt);

    out_tx_data         = std::move(payload);
    out_transaction_num = hdr.transaction_num;
    return {};
}

// ── Read request (read direction, issue #471, REQ-ISELED-030/031) ────────────
// TC18 §13.7.12.1: "Upon read requests the responses are collected 5/4bit
// decoded and aggregated into one or multiple ACF [messages] up to the
// requested read_size." A read request may still carry a payload — the
// plain Instruction/Address content selecting what to read back — exactly
// as an I2C read carries the target register address (rcp/i2c.hpp's own
// analogous decode_transfer_request()); only the Data octets are
// meaningless on a read. The write-direction encode_command_request()/
// decode_command_request() pair above is completely unchanged by this:
// it continues to model op=write only, and continues to reject a read-op
// frame with wrong_op.

// Largest value the ACF header's 12-bit read_size_or_segment_num field can
// carry.
constexpr uint16_t kMaxReadSize = 0x0FFFu;

// encode_read_request encodes an ACF_ABB read request addressed to
// byte_bus_id: payload is exactly tx_data (the plain Instruction/Address
// content selecting what to read back; no Data octets), and the ACF
// header's own read_size_or_segment_num field carries read_size (0-4095).
// Returns an empty vector if read_size exceeds kMaxReadSize or tx_data
// exceeds acf::kAcfAbbMaxPayload.
inline std::vector<uint8_t> encode_read_request(avtp::ByteBusId byte_bus_id,
                                                 const std::vector<uint8_t>& tx_data, uint16_t read_size,
                                                 uint8_t transaction_num) {
    if (read_size > kMaxReadSize) return {};
    if (tx_data.size() > acf::kAcfAbbMaxPayload) return {};

    acf::AcfMessageInfo hdr;
    hdr.byte_bus_id              = byte_bus_id;
    hdr.op                       = false; // read
    hdr.evt_op                    = 0;
    hdr.transaction_num          = transaction_num;
    hdr.read_size_or_segment_num = read_size;
    return acf::encode_acf_abb(hdr, tx_data);
}

// decode_read_request decodes and validates an ACF-level ISELED read
// request. Rejects an op other than read with wrong_op (the mirror image
// of decode_command_request()'s own check), and evt[2:0] != 0b000 with
// bad_evt.
inline std::error_code decode_read_request(const uint8_t* b, size_t len, avtp::ByteBusId expected_bus_id,
                                            std::vector<uint8_t>& out_tx_data, uint16_t& out_read_size,
                                            uint8_t& out_transaction_num) {
    acf::AcfMessageInfo  hdr;
    std::vector<uint8_t> payload;
    const auto ec = acf::decode_acf_abb(b, len, hdr, payload);
    if (ec == avtp::make_error_code(avtp::AvtpErrc::short_buffer)) return make_error_code(IseledErrc::short_frame);
    if (ec) return make_error_code(IseledErrc::bad_msg_type);
    if (hdr.byte_bus_id != expected_bus_id) return make_error_code(IseledErrc::wrong_bus);
    if (hdr.op) return make_error_code(IseledErrc::wrong_op);
    if (!acf::evt_row2_is_plain(hdr.evt_op)) return make_error_code(IseledErrc::bad_evt);

    out_tx_data         = std::move(payload);
    out_read_size        = hdr.read_size_or_segment_num;
    out_transaction_num = hdr.transaction_num;
    return {};
}

// ── Response (§13.7.12.3 Figure 42) ───────────────────────────────────────────

// encode_response encodes an ISELED response carrying rx_data as its
// payload, echoing transaction_num. Encoded as ACF_ABB when timed is
// false; ACF_GBB (message_timestamp = timestamp, mtv valid) when timed is
// true.
inline std::vector<uint8_t> encode_response(avtp::ByteBusId byte_bus_id, const std::vector<uint8_t>& rx_data,
                                             uint8_t transaction_num, bool timed, uint64_t timestamp) {
    acf::AcfMessageInfo hdr;
    hdr.byte_bus_id     = byte_bus_id;
    hdr.op              = false; // read
    hdr.rsp             = true;
    hdr.evt_op           = 0;
    hdr.transaction_num = transaction_num;

    if (timed) {
        hdr.mtv = true;
        return acf::encode_acf_gbb(hdr, timestamp, rx_data);
    }
    return acf::encode_acf_abb(hdr, rx_data);
}

// decode_response decodes an ISELED response from either an ACF_ABB or
// ACF_GBB message (peeks the message type itself, since a response's
// encoding depends on the responding endpoint's own timed/untimed choice).
inline std::error_code decode_response(const uint8_t* b, size_t len, avtp::ByteBusId expected_bus_id,
                                        std::vector<uint8_t>& out_rx_data, bool& out_timed,
                                        uint64_t& out_timestamp, uint8_t& out_transaction_num) {
    uint8_t msg_type = 0;
    if (acf::peek_msg_type(b, len, msg_type)) return make_error_code(IseledErrc::short_frame);

    acf::AcfMessageInfo  hdr;
    std::vector<uint8_t> payload;
    bool                 timed     = false;
    uint64_t             timestamp = 0;

    if (msg_type == acf::kAcfMsgTypeGbb) {
        uint64_t ts = 0;
        const auto ec = acf::decode_acf_gbb(b, len, hdr, ts, payload);
        if (ec == avtp::make_error_code(avtp::AvtpErrc::short_buffer)) return make_error_code(IseledErrc::short_frame);
        if (ec) return make_error_code(IseledErrc::bad_msg_type);
        timed     = hdr.mtv;
        timestamp = timed ? ts : 0;
    } else {
        const auto ec = acf::decode_acf_abb(b, len, hdr, payload);
        if (ec == avtp::make_error_code(avtp::AvtpErrc::short_buffer)) return make_error_code(IseledErrc::short_frame);
        if (ec) return make_error_code(IseledErrc::bad_msg_type);
    }

    if (hdr.byte_bus_id != expected_bus_id) return make_error_code(IseledErrc::wrong_bus);

    out_rx_data          = std::move(payload);
    out_timed             = timed;
    out_timestamp         = timestamp;
    out_transaction_num  = hdr.transaction_num;
    return {};
}

// ── Fragmented response (REQ-ISELED-025/040, rcp/fragment.hpp) ───────────────
// TC18 §13.7.12.1: "Upon read requests the responses are collected 5/4bit
// decoded and aggregated into one or multiple ACF [messages] up to the
// requested read_size." Wired directly to rcp/fragment.hpp's own
// fragment::plan_count()/fragment::plan() — c-RCP's ep_iseled.c genuinely
// `#include`s "rcp/fragment.h" and calls its equivalents directly rather
// than implementing its own ISELED-specific multi-response splitting logic
// (see the file header).

// response_fragment_count: the number of ACF messages
// encode_response_fragmented() would produce for available_len octets of
// already-decoded ISELED data, first capped to at most read_size octets,
// then split into fragments of at most max_fragment_payload octets each.
inline size_t response_fragment_count(size_t available_len, uint16_t read_size,
                                       size_t max_fragment_payload) noexcept {
    const size_t capped_len = std::min(available_len, static_cast<size_t>(read_size));
    return fragment::plan_count(capped_len, max_fragment_payload);
}

// encode_response_fragmented encodes an ISELED response as one or more ACF
// messages, first capping rx_data to at most read_size octets (TC18
// §13.7.12.1's own response-aggregation ceiling), then fragmenting via
// rcp/fragment.hpp's ms/segment_num mechanism whenever the capped data
// exceeds max_fragment_payload octets. Every fragment shares byte_bus_id/
// op(read)/transaction_num/timed/timestamp with encode_response(); only the
// ms flag, the read_size_or_segment_num field, and each fragment's own
// payload slice differ. Returns an empty vector under the same conditions
// fragment::plan_count() returns 0 for (fragmentation disabled or too many
// segments needed).
inline std::vector<std::vector<uint8_t>> encode_response_fragmented(
    avtp::ByteBusId byte_bus_id, const std::vector<uint8_t>& rx_data, uint16_t read_size,
    uint8_t transaction_num, bool timed, uint64_t timestamp, size_t max_fragment_payload) {
    const size_t capped_len = std::min(rx_data.size(), static_cast<size_t>(read_size));
    const size_t count      = fragment::plan_count(capped_len, max_fragment_payload);
    if (count == 0) return {};

    std::vector<fragment::Segment> segs(count);
    if (fragment::plan(capped_len, max_fragment_payload, segs.data(), count)) return {};

    std::vector<std::vector<uint8_t>> out;
    out.reserve(count);
    for (const auto& seg : segs) {
        std::vector<uint8_t> slice(rx_data.begin() + static_cast<long>(seg.offset),
                                    rx_data.begin() + static_cast<long>(seg.offset + seg.len));

        acf::AcfMessageInfo hdr;
        hdr.byte_bus_id              = byte_bus_id;
        hdr.op                       = false; // read
        hdr.rsp                      = true;
        hdr.evt_op                    = 0;
        hdr.transaction_num          = transaction_num;
        hdr.ms                       = seg.ms;
        hdr.read_size_or_segment_num = seg.ms ? seg.segment_num : static_cast<uint16_t>(0);

        if (timed) {
            hdr.mtv = true;
            out.push_back(acf::encode_acf_gbb(hdr, timestamp, slice));
        } else {
            out.push_back(acf::encode_acf_abb(hdr, slice));
        }
    }
    return out;
}

// ── Trigger signals ───────────────────────────────────────────────────────────
// One TransferComplete signal per IseledEndpoint instance — the endpoint's
// one asynchronous-event trigger mode, matching IseledTrigger above.

enum class IseledSignal : uint8_t { TransferComplete = 0 };

constexpr endpoint::TriggerRegistry::SignalId iseled_signal_id(IseledSignal sig) noexcept {
    return static_cast<endpoint::TriggerRegistry::SignalId>(sig);
}

// ── IseledEndpoint ────────────────────────────────────────────────────────────
// A per-transaction convenience wrapper (no c-RCP equivalent — see
// rcp/i2c.hpp's I2cEndpoint for the identical rationale): records raw
// tx/rx byte streams and fires TransferComplete.
class IseledEndpoint {
public:
    // Records a plain-mode ISELED command's raw tx bytes as sent (mirroring
    // encode_command_request/decode_command_request's own raw-byte-stream
    // model) and fires TransferComplete.
    void send(std::vector<uint8_t> tx_data) {
        last_sent_ = std::move(tx_data);
        triggers_.notify(iseled_signal_id(IseledSignal::TransferComplete));
    }

    // Records a response's raw rx bytes as received (see encode_response/
    // decode_response above). Does not itself fire a trigger — ISELED has
    // only the one TransferComplete signal, already fired by send() for the
    // paired command.
    void receive(std::vector<uint8_t> rx_data) { last_received_ = std::move(rx_data); }

    // handle_request classifies the incoming request's evt[2:0] field via
    // rcp::endpoint::evt_row2_kind_of before doing anything else:
    //   - Plain (evt[2:0] == 000b): delegates to send() unchanged.
    //   - Reserved (001b-110b): returns
    //     endpoint::EndpointErrc::reserved_evt_row2 without recording
    //     anything.
    //   - ConfigWrite (111b): returns IseledErrc::config_write_not_supported
    //     — see that enumerator's own comment (and I2cErrc's identical
    //     one) for why this convenience call's single-tx_data shape cannot
    //     carry a reconfig payload, and where the real, now-implemented
    //     mechanism lives (apply_reconfig()/render_registers() above).
    std::error_code handle_request(uint8_t evt_op, std::vector<uint8_t> tx_data) {
        switch (endpoint::evt_row2_kind_of(evt_op)) {
        case endpoint::EvtRow2Kind::Plain:
            send(std::move(tx_data));
            return {};
        case endpoint::EvtRow2Kind::Reserved:
            return endpoint::make_error_code(endpoint::EndpointErrc::reserved_evt_row2);
        case endpoint::EvtRow2Kind::ConfigWrite:
            return make_error_code(IseledErrc::config_write_not_supported);
        }
        return endpoint::make_error_code(endpoint::EndpointErrc::reserved_evt_row2); // unreachable
    }

    const std::vector<uint8_t>& last_sent() const noexcept { return last_sent_; }
    const std::vector<uint8_t>& last_received() const noexcept { return last_received_; }
    endpoint::TriggerRegistry& triggers() noexcept { return triggers_; }

private:
    endpoint::TriggerRegistry triggers_;
    std::vector<uint8_t>      last_sent_;
    std::vector<uint8_t>      last_received_;
};

} // namespace iseled
} // namespace rcp

// Enable std::error_code construction from rcp::iseled::IseledErrc /
// IseledReconfigErrc.
namespace std {
template <>
struct is_error_code_enum<rcp::iseled::IseledErrc> : true_type {};
template <>
struct is_error_code_enum<rcp::iseled::IseledReconfigErrc> : true_type {};
} // namespace std
