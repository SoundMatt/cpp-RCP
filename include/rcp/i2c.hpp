// fusa:req REQ-I2C-001
// fusa:req REQ-I2C-002
// fusa:req REQ-I2C-003
// fusa:req REQ-I2C-004
// fusa:req REQ-I2C-005
// fusa:req REQ-I2C-006
// fusa:req REQ-I2C-007
// fusa:req REQ-I2C-008
// fusa:req REQ-I2C-009
// fusa:req REQ-I2C-010
// fusa:req REQ-I2C-011
// fusa:req REQ-I2C-012
// fusa:req REQ-I2C-013
// fusa:req REQ-I2C-014
// fusa:req REQ-I2C-015
// fusa:req REQ-I2C-016
// fusa:req REQ-I2C-017
// fusa:req REQ-I2C-018
// fusa:req REQ-I2C-020
// fusa:req REQ-I2C-021
// fusa:req REQ-I2C-022
// fusa:req REQ-I2C-023
// fusa:req REQ-I2C-024
// fusa:req REQ-I2C-025
// fusa:req REQ-I2C-026

// I2C endpoint (ep_type 0x04) — controller-only, ACF-level raw byte-stream
// transfer (address bytes included, per the OPEN Alliance TC18 Remote
// Control Protocol Specification v0.5.1_RC5's own framing, §13.7.7.1/
// §13.7.7.3), its Table 49 functional-configuration register block
// (§13.7.7.2, reachable through the generic evt[2:0]=111b configuration-
// write escape hatch, §12.7.1), and the Table 33 Row 2 evt[2:0] plain/
// reserved/config-write classification every endpoint type in that row
// shares (§13.5).
//
// Phase 3 rewrite (cpp-RCP issue #129, ROADMAP.md "Phase 17"), ported from
// c-RCP's include/rcp/ep_i2c.h + src/ep_i2c.c — this project's RC5-spec-
// conformant reference for this module. Content correction, not a fresh
// design: this header's own pre-rewrite content (last touched at v2.20.0,
// commits ae5c69f/e488732) modeled `i2c_mode` as a 2-way Standard/HighSpeed
// decode of AcfMessageInfo::hs, and a local `compound_wait_matches_bits`
// helper — both invented, and both wrong against c-RCP's actual design:
// i2c_mode is a persistent 5-way bus-speed preset stored in this endpoint's
// own functional-config register block (Table 49, set via the generic
// evt[2:0]=111b reconfig path, never carried per-transfer on the ACF
// header's `hs` bit at all), and I2C carries no endpoint-specific compound-
// wait logic in c-RCP — rcp/acf.hpp's own compound_wait_evt_valid()/
// compound_wait_match() (added during this rewrite's Phase 1) are already
// the correct, endpoint-type-independent primitive for that TC18 §13.5.1
// mechanism, applicable identically to every endpoint type, not something
// I2C should keep its own local duplicate of. Both are removed here in
// favor of the real Table 49 register-block content c-RCP actually
// implements, ported below.
//
// Retained from the pre-rewrite pilot (post-v2.4.0, first endpoint type to
// wire Table 33 Row 2 evt[2:0] validation): I2cEndpoint's controller-only
// raw byte-stream transfer() call, its TransferComplete/Nack trigger pair,
// and handle_request()'s evt_row2_kind_of dispatch — genuinely good,
// cpp-RCP-only convenience-class content with no c-RCP equivalent (c-RCP is
// a pure free-function C library with no persistent per-transfer "endpoint
// object"), kept and re-verified rather than replaced, per this rewrite's
// "content correction, not API redesign" scope.
//
// ── Transfer direction: the ACF op bit vs. the address byte's R/W bit ──────
// An I2C transfer is directional, and that direction appears in two
// entirely independent places, which this module keeps strictly separate
// (§13.7.7.3, TC18.txt L5241-5243, "The byte msg payload is the I2C payload
// including the address. The I2C endpoint does not know whether there is a
// 7- or 10-bit address, since the endpoint is just transparent."):
//   - The *I2C-bus-level* R/W bit rides inside the payload's own address
//     byte(s); this module never inspects it — it is the caller's to set,
//     round-tripped byte for byte.
//   - The *RCP-level* direction is the ACF header's op bit: I2cDir::Read
//     (wire op=0) asks for a payload-bearing response; I2cDir::Write (wire
//     op=1) asks only for a payload-less success confirmation (§12.9.1,
//     §11.3.2/§11.3.3). Unlike a LIN command or an SPI transfer — both
//     unconditionally response-bearing, so a constant op is correct for
//     them — an I2C transfer is genuinely either-directional, so
//     encode_transfer_request()/encode_response() take I2cDir explicitly
//     rather than hard-coding either sense.
//
// Field names and behavior below implement TC18's *behavior* as described in
// an internal structured extraction of the specification named above; no
// text from that document is reproduced here. The concrete struct/enum
// shapes chosen in this file are this implementation's own, same as the
// equivalent disclaimers in rcp/acf.hpp, rcp/avtp.hpp, rcp/endpoint.hpp,
// rcp/lifecycle.hpp, and rcp/fragment.hpp.
#pragma once

#include <rcp/acf.hpp>
#include <rcp/avtp.hpp>
#include <rcp/endpoint.hpp>
#include <rcp/lifecycle.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <system_error>
#include <vector>

namespace rcp {
namespace i2c {

// ── i2c_mode: bus-speed presets (§13.7.7.2 Table 49) ─────────────────────────
// The specification extraction available to this implementation carries two
// internally inconsistent numberings for where its highest-speed preset
// sits relative to "Fast mode plus" immediately below it — an apparent
// drafting inconsistency in the source material. c-RCP deliberately
// implements the *lower*-numbered of the two candidate positions
// (HighSpeed = 3, immediately following FastPlus with no reserved value
// skipped) as the more conservative reading, flagged here as pending
// resolution by spec errata rather than guessed at — ported verbatim,
// unchanged, from c-RCP's own identical flag (RCP_EP_I2C_MODE_HIGH_SPEED).
enum class I2cMode : uint8_t {
    Standard  = 0, // ~100 kHz-class preset
    Fast      = 1, // ~400 kHz-class preset
    FastPlus  = 2, // ~1 MHz-class preset
    HighSpeed = 3, // conservative, lower-numbered reading; pending spec errata
    UltraFast = 4, // ~5 MHz-class preset — Table 49's own fifth row, unambiguous
};

// i2c_mode_valid: true iff v is one of the five defined presets (v <= 4).
constexpr bool i2c_mode_valid(uint8_t v) noexcept {
    return v <= static_cast<uint8_t>(I2cMode::UltraFast);
}

// ── Errors ────────────────────────────────────────────────────────────────────

enum class I2cErrc : int {
    short_frame  = 1,
    bad_msg_type = 2,
    wrong_bus    = 3,
    // Retained for source stability; no longer produced by any decoder in
    // this module — both op senses are valid on an I2C transfer/response
    // (see the file header), so there is no longer a "wrong" one to reject.
    wrong_op     = 4,
    // evt[2:0] is not 0b000, TC18 §13.5 Table 33's only legal value for a
    // plain (non-configuration) request in I2C's endpoint-type row —
    // caller shall respond with error code UNSUPPORTED_CMD.
    bad_evt      = 5,
    // The addressed device did not acknowledge (I2cEndpoint::transfer's own
    // controller-only convenience-class concept — c-RCP's ep_i2c.c has no
    // equivalent; TC18 does not model bus-level ack/nack for this endpoint
    // type at all, so this is additive, not a port).
    nack = 6,
    // handle_request()'s own convenience-wrapper limitation: a Table 33
    // ConfigWrite (evt[2:0]==111b) request's payload is a relative EP_func
    // start-address + configuration-data shape, entirely different from
    // handle_request()'s own out_bytes/in_bytes bus-transfer parameters, so
    // it cannot be routed through this call — see handle_request's own
    // comment. The real configuration-write mechanism this milestone's
    // pilot deliberately punted on is fully implemented below
    // (apply_reconfig()/render_registers()/encode_reconfig_request(),
    // REQ-I2C-021/022/025) — a caller integrating real EP0/regmap dispatch
    // calls those directly with the request's actual raw payload, rather
    // than through this endpoint's own transfer-shaped convenience call.
    config_write_not_supported = 7,
};

inline const std::error_category& i2c_category() noexcept {
    struct Cat : std::error_category {
        const char* name() const noexcept override { return "rcp.i2c"; }
        std::string message(int ev) const override {
            switch (static_cast<I2cErrc>(ev)) {
            case I2cErrc::short_frame:  return "rcp/i2c: frame too short";
            case I2cErrc::bad_msg_type: return "rcp/i2c: unexpected ACF message type";
            case I2cErrc::wrong_bus:    return "rcp/i2c: wrong byte_bus_id";
            case I2cErrc::wrong_op:     return "rcp/i2c: wrong ACF op";
            case I2cErrc::bad_evt:      return "rcp/i2c: evt[2:0] is not 0b000";
            case I2cErrc::nack:         return "rcp/i2c: addressed device did not acknowledge (NACK)";
            case I2cErrc::config_write_not_supported:
                return "rcp/i2c: evt[2:0]=111b configuration-write requests are not supported by "
                       "this convenience wrapper — call apply_reconfig() directly";
            default: return "rcp/i2c: unknown error";
            }
        }
    };
    static Cat instance;
    return instance;
}

inline std::error_code make_error_code(I2cErrc e) noexcept {
    return {static_cast<int>(e), i2c_category()};
}

// ── Functional config (§13.7.7.2 Table 49) ────────────────────────────────────
// The common EP_func prefix (ep_enable/ep_clear_req_storage/
// ep_req_crc_enable/ep_response_ts_enable/ep_suppress_response) every
// endpoint type's own Table shares (c-RCP's rcp_regmap_ep_functional_cfg_t,
// composed as `common` there) is modeled directly as members here rather
// than through rcp/regmap.hpp's own EndpointFunctionalConfig — that struct
// is still an opaque byte blob pending its own structural port (Phase 4,
// ROADMAP.md "Phase 17" phase 4's "regmap: c-RCP's is ~7x more complete").
struct I2cFunctionalCfg {
    bool ep_enable             = false;
    bool ep_clear_req_storage  = false;
    bool ep_req_crc_enable     = false;
    bool ep_response_ts_enable = false;
    bool ep_suppress_response  = false;

    uint8_t  i2c_mode      = static_cast<uint8_t>(I2cMode::Standard); // 0x0009, R/W
    uint16_t ep_status     = 0; // 0x0006, R/W
    uint8_t  clock_divider = 0; // 0x0008, R/W
    uint8_t  trail         = 0; // 0x000A, R/W
};

inline void i2c_functional_cfg_init(I2cFunctionalCfg& cfg) noexcept { cfg = I2cFunctionalCfg{}; }

// i2c_functional_cfg_writable is a thin, named wrapper over
// rcp/lifecycle.hpp's field_writable() (FieldKind::FunctionalW) — reuses,
// never duplicates, that function's authorization logic.
inline bool i2c_functional_cfg_writable(lifecycle::ServerState state, lifecycle::WriterCtx writer) noexcept {
    return lifecycle::field_writable(state, lifecycle::FieldKind::FunctionalW, writer);
}

// set_mode: applies mode to cfg iff mode is i2c_mode_valid() AND
// i2c_functional_cfg_writable() authorizes the write for state/writer;
// returns whether the write was applied. cfg is left entirely unchanged
// when it returns false.
inline bool set_mode(I2cFunctionalCfg& cfg, I2cMode mode, lifecycle::ServerState state,
                      lifecycle::WriterCtx writer) noexcept {
    if (!i2c_mode_valid(static_cast<uint8_t>(mode))) return false;
    if (!i2c_functional_cfg_writable(state, writer)) return false;
    cfg.i2c_mode = static_cast<uint8_t>(mode);
    return true;
}

// ── The EP_func register block (evt[2:0] == 111b, §13.7.7.2 Table 49) ────────
// TC18 §13.7.7.2 Table 49's own printed relative-address column collides
// two entries at 0x0002 (i2c_ep_enable&clr, 8 bit, and i2c_base_clk, 16
// bit) and two more at 0x0004 (i2c_base_clk's own second octet and
// i2c_ep_status, 16 bit) — a genuine editorial defect, confirmed by direct
// visual inspection of the source PDF, the same class of defect PWM_OUT's/
// GPIO's/SPI's own Tables carry. Every endpoint type's common EP_func
// prefix places EP_LEN/reserved/enable&clr/options/a 16-bit base_clk at the
// identical address sequence 0x0000/0x0001/0x0002/0x0003/0x0004-0x0005 —
// that cross-table pattern is authoritative here too, so i2c_base_clk moves
// to 0x0004-0x0005, pushing i2c_ep_status to 0x0006-0x0007,
// i2c_clock_divider to 0x0008, i2c_mode to 0x0009, and i2c_trail to
// 0x000A (kEpFuncLen = 0x000B, 11 octets total).
constexpr uint16_t kRegEpLen        = 0x0000; //  8 bit, R
constexpr uint16_t kRegReserved01   = 0x0001; //  8 bit, R
constexpr uint16_t kRegEpEnableClr  = 0x0002; //  8 bit, R/W
constexpr uint16_t kRegEpOptions    = 0x0003; //  8 bit, R/W
constexpr uint16_t kRegBaseClk      = 0x0004; // 16 bit, R
constexpr uint16_t kRegEpStatus     = 0x0006; // 16 bit, R/W
constexpr uint16_t kRegClockDivider = 0x0008; //  8 bit, R/W
constexpr uint16_t kRegMode         = 0x0009; //  8 bit, R/W
constexpr uint16_t kRegTrail        = 0x000A; //  8 bit, R/W

// The block's own length in octets — one past the last assigned offset,
// i.e. the value reported at kRegEpLen and the bound the "write beyond
// EP_LEN is ignored" rule (§12.7.1) is applied against.
constexpr size_t kEpFuncLen = 0x000B;

using EpFuncBlock = std::array<uint8_t, kEpFuncLen>;

namespace detail {
constexpr uint8_t kEnableClrBitEnable = 1u << 0;
constexpr uint8_t kEnableClrBitClear  = 1u << 4;
constexpr uint8_t kOptionsBitReqCrc   = 1u << 0;
constexpr uint8_t kOptionsBitRespTs   = 1u << 3;
constexpr uint8_t kOptionsBitSuppress = 1u << 7;
} // namespace detail

// render_registers serializes cfg's whole EP_func register block into the
// corrected (not the table's own colliding-printed) offsets above — the
// inverse of apply_reconfig()'s own parse step. i2c_base_clk (read-only)
// always renders 0 — no real clock source is modelled.
inline EpFuncBlock render_registers(const I2cFunctionalCfg& cfg) noexcept {
    EpFuncBlock out{};
    uint8_t enable_clr = 0;
    uint8_t options    = 0;
    if (cfg.ep_enable) enable_clr |= detail::kEnableClrBitEnable;
    if (cfg.ep_clear_req_storage) enable_clr |= detail::kEnableClrBitClear;
    if (cfg.ep_req_crc_enable) options |= detail::kOptionsBitReqCrc;
    if (cfg.ep_response_ts_enable) options |= detail::kOptionsBitRespTs;
    if (cfg.ep_suppress_response) options |= detail::kOptionsBitSuppress;

    out[kRegEpLen]              = static_cast<uint8_t>(kEpFuncLen);
    out[kRegReserved01]         = 0;
    out[kRegEpEnableClr]        = enable_clr;
    out[kRegEpOptions]          = options;
    out[kRegBaseClk]            = 0; // no real clock source modelled
    out[kRegBaseClk + 1]        = 0;
    out[kRegEpStatus]           = static_cast<uint8_t>(cfg.ep_status >> 8);
    out[kRegEpStatus + 1]       = static_cast<uint8_t>(cfg.ep_status & 0xFF);
    out[kRegClockDivider]       = cfg.clock_divider;
    out[kRegMode]                = cfg.i2c_mode;
    out[kRegTrail]               = cfg.trail;
    return out;
}

namespace detail {
inline void parse_registers(I2cFunctionalCfg& cfg, const EpFuncBlock& in) noexcept {
    const uint8_t enable_clr = in[kRegEpEnableClr];
    const uint8_t options    = in[kRegEpOptions];

    cfg.ep_enable             = (enable_clr & kEnableClrBitEnable) != 0;
    cfg.ep_clear_req_storage  = (enable_clr & kEnableClrBitClear) != 0;
    cfg.ep_req_crc_enable     = (options & kOptionsBitReqCrc) != 0;
    cfg.ep_response_ts_enable = (options & kOptionsBitRespTs) != 0;
    cfg.ep_suppress_response  = (options & kOptionsBitSuppress) != 0;

    cfg.ep_status     = static_cast<uint16_t>((static_cast<uint16_t>(in[kRegEpStatus]) << 8) | in[kRegEpStatus + 1]);
    cfg.clock_divider = in[kRegClockDivider];
    cfg.i2c_mode        = in[kRegMode];
    cfg.trail            = in[kRegTrail];
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
// configuration request's payload begins with — a 16-bit big-endian field,
// followed by the configuration data octets to write from that address
// onward (§12.7.1).
constexpr size_t kReconfigAddrLen = 2;

// apply_reconfig applies the configuration escape hatch (evt[2:0] == 111b):
// payload is a 16-bit big-endian relative start address followed by the
// configuration data octets to write from that address onward (§12.7.1).
// Patches the block's current image at octet granularity, then adopts it
// wholesale — a write covering only part of a multi-octet register updates
// exactly the octets it addresses and leaves that register's other octets
// alone; octets landing on a read-only register (EP_LEN, the reserved
// octet, base_clk) are silently skipped while the rest of the span is still
// applied. A write whose start_address+length exceeds kEpFuncLen is
// rejected wholesale and cfg left entirely unchanged, per §12.7.1's own
// "such a payload is to be ignored" rule.
enum class I2cReconfigErrc : int {
    // payload carries no address prefix, or an address prefix with no data
    // octet after it.
    short_payload = 1,
    // start_address + data length exceeds kEpFuncLen — the whole write is
    // ignored.
    out_of_range = 2,
};

inline const std::error_category& i2c_reconfig_category() noexcept {
    struct Cat : std::error_category {
        const char* name() const noexcept override { return "rcp.i2c.reconfig"; }
        std::string message(int ev) const override {
            switch (static_cast<I2cReconfigErrc>(ev)) {
            case I2cReconfigErrc::short_payload:
                return "rcp/i2c: configuration write has no address and data";
            case I2cReconfigErrc::out_of_range:
                return "rcp/i2c: configuration write extends past the EP_func block";
            default: return "rcp/i2c: unknown configuration-write error";
            }
        }
    };
    static Cat instance;
    return instance;
}

inline std::error_code make_error_code(I2cReconfigErrc e) noexcept {
    return {static_cast<int>(e), i2c_reconfig_category()};
}

inline std::error_code apply_reconfig(I2cFunctionalCfg& cfg, const uint8_t* payload,
                                       size_t payload_len) noexcept {
    if (payload_len <= kReconfigAddrLen) return make_error_code(I2cReconfigErrc::short_payload);

    const uint16_t start_address =
        static_cast<uint16_t>((static_cast<uint16_t>(payload[0]) << 8) | payload[1]);
    const size_t data_len = payload_len - kReconfigAddrLen;

    if (static_cast<size_t>(start_address) + data_len > kEpFuncLen)
        return make_error_code(I2cReconfigErrc::out_of_range);

    EpFuncBlock block = render_registers(cfg);
    for (size_t i = 0; i < data_len; ++i) {
        const uint16_t addr = static_cast<uint16_t>(start_address + i);
        if (detail::reg_offset_read_only(addr)) continue; // write ignored
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
    hdr.op              = true; // write — §12.7.1: the write request's payload is written into EP_func
    hdr.evt_op           = 0x7; // evt[2:0] = 111b, the reconfiguration escape hatch
    hdr.transaction_num = transaction_num;
    return acf::encode_acf_abb(hdr, payload);
}

// ── Transfer direction ────────────────────────────────────────────────────────
// The RCP-level direction of a transfer/response — NOT the I2C-bus-level R/W
// bit inside the payload's address byte(s), which this module never
// inspects. See the file header.
enum class I2cDir : uint8_t {
    Write = 0, // op=1: octets go out, no data comes back
    Read  = 1, // op=0: read_size octets are clocked back in the response
};

constexpr bool i2c_dir_valid(I2cDir d) noexcept { return d == I2cDir::Write || d == I2cDir::Read; }

namespace detail {
constexpr bool dir_to_write_op(I2cDir d) noexcept { return d == I2cDir::Write; }
constexpr I2cDir op_to_dir(bool op) noexcept { return op ? I2cDir::Write : I2cDir::Read; }
} // namespace detail

// ── Transfer request ──────────────────────────────────────────────────────────

// Largest value the ACF header's 12-bit read_size_or_segment_num field can
// carry.
constexpr uint16_t kMaxReadSize = 0x0FFFu;

// encode_transfer_request encodes an ACF_ABB transfer request addressed to
// byte_bus_id: payload is exactly tx_data, the raw bytes to place on the
// bus — target-device address byte(s) included, never parsed by this
// module (§13.7.7.3, §13.5 Table 33 — no channel selector, evt is always
// 0). read_size applies to I2cDir::Read only; for I2cDir::Write it must be
// 0, since that header slot carries a segment_num, not a read_size, in the
// write sense. Returns an empty vector if direction is not i2c_dir_valid(),
// if read_size exceeds kMaxReadSize, if read_size != 0 with
// I2cDir::Write, or if tx_data exceeds acf::kAcfAbbMaxPayload.
inline std::vector<uint8_t> encode_transfer_request(avtp::ByteBusId byte_bus_id, I2cDir direction,
                                                       const std::vector<uint8_t>& tx_data,
                                                       uint16_t read_size, uint8_t transaction_num) {
    if (!i2c_dir_valid(direction)) return {};
    if (read_size > kMaxReadSize) return {};
    if (direction == I2cDir::Write && read_size != 0) return {};
    if (tx_data.size() > acf::kAcfAbbMaxPayload) return {};

    acf::AcfMessageInfo hdr;
    hdr.byte_bus_id     = byte_bus_id;
    hdr.op              = detail::dir_to_write_op(direction);
    hdr.evt_op           = 0; // no channel selector — see the file header
    hdr.transaction_num = transaction_num;
    hdr.read_size_or_segment_num = (direction == I2cDir::Read) ? read_size : static_cast<uint16_t>(0);
    return acf::encode_acf_abb(hdr, tx_data);
}

// decode_transfer_request decodes and validates an ACF-level I2C transfer
// request. Both op senses are valid — reported via out_direction, never
// rejected (§13.7.7.1, §13.5 Table 33's own row carries no per-op-sense
// restriction). Rejects evt[2:0] != 0b000 with I2cErrc::bad_evt
// (acf::evt_row2_is_plain(), TC18 §13.5 Table 33 — caller shall respond
// with error code UNSUPPORTED_CMD). out_tx_data (address byte(s) plus data,
// round-tripped verbatim byte for byte, per §13.7.7.3) is set to a copy of
// the request's byte_msg_payload.
inline std::error_code decode_transfer_request(const uint8_t* b, size_t len, avtp::ByteBusId expected_bus_id,
                                                 I2cDir& out_direction, std::vector<uint8_t>& out_tx_data,
                                                 uint16_t& out_read_size, uint8_t& out_transaction_num) {
    acf::AcfMessageInfo   hdr;
    std::vector<uint8_t>  payload;
    const auto            ec = acf::decode_acf_abb(b, len, hdr, payload);
    if (ec == avtp::make_error_code(avtp::AvtpErrc::short_buffer)) return make_error_code(I2cErrc::short_frame);
    if (ec) return make_error_code(I2cErrc::bad_msg_type);
    if (hdr.byte_bus_id != expected_bus_id) return make_error_code(I2cErrc::wrong_bus);
    if (!acf::evt_row2_is_plain(hdr.evt_op)) return make_error_code(I2cErrc::bad_evt);

    const I2cDir direction = detail::op_to_dir(hdr.op);
    out_direction           = direction;
    out_tx_data              = std::move(payload);
    // Only meaningful in the read sense; in the write sense that slot is a
    // segment_num this module does not interpret.
    out_read_size            = (direction == I2cDir::Read) ? hdr.read_size_or_segment_num : static_cast<uint16_t>(0);
    out_transaction_num      = hdr.transaction_num;
    return {};
}

// ── Response ───────────────────────────────────────────────────────────────────

// encode_response encodes the response to a transfer request of the same
// direction: a read response has a byte_msg_payload (rx_data), a write
// response does not (§11.3.2/§11.3.3). Encoded as ACF_ABB when timed is
// false; ACF_GBB (message_timestamp = timestamp, mtv valid) when timed is
// true. Returns an empty vector if direction is not i2c_dir_valid(), or if
// rx_data is non-empty with direction I2cDir::Write.
inline std::vector<uint8_t> encode_response(avtp::ByteBusId byte_bus_id, I2cDir direction,
                                              const std::vector<uint8_t>& rx_data, uint8_t transaction_num,
                                              bool timed, uint64_t timestamp) {
    if (!i2c_dir_valid(direction)) return {};
    if (direction == I2cDir::Write && !rx_data.empty()) return {};

    acf::AcfMessageInfo hdr;
    hdr.byte_bus_id     = byte_bus_id;
    hdr.op              = detail::dir_to_write_op(direction);
    hdr.rsp             = true;
    hdr.evt_op           = 0;
    hdr.transaction_num = transaction_num;

    if (timed) {
        hdr.mtv = true;
        return acf::encode_acf_gbb(hdr, timestamp, rx_data);
    }
    return acf::encode_acf_abb(hdr, rx_data);
}

// decode_response decodes an I2C response from either an ACF_ABB or
// ACF_GBB message (peeks the message type itself, since a response's
// encoding depends on the responding endpoint's own timed/untimed choice).
inline std::error_code decode_response(const uint8_t* b, size_t len, avtp::ByteBusId expected_bus_id,
                                         I2cDir& out_direction, std::vector<uint8_t>& out_rx_data,
                                         bool& out_timed, uint64_t& out_timestamp,
                                         uint8_t& out_transaction_num) {
    uint8_t msg_type = 0;
    if (acf::peek_msg_type(b, len, msg_type)) return make_error_code(I2cErrc::short_frame);

    acf::AcfMessageInfo   hdr;
    std::vector<uint8_t>  payload;
    bool                  timed     = false;
    uint64_t              timestamp = 0;

    if (msg_type == acf::kAcfMsgTypeGbb) {
        uint64_t ts = 0;
        const auto ec = acf::decode_acf_gbb(b, len, hdr, ts, payload);
        if (ec == avtp::make_error_code(avtp::AvtpErrc::short_buffer)) return make_error_code(I2cErrc::short_frame);
        if (ec) return make_error_code(I2cErrc::bad_msg_type);
        timed     = hdr.mtv;
        timestamp = timed ? ts : 0;
    } else {
        const auto ec = acf::decode_acf_abb(b, len, hdr, payload);
        if (ec == avtp::make_error_code(avtp::AvtpErrc::short_buffer)) return make_error_code(I2cErrc::short_frame);
        if (ec) return make_error_code(I2cErrc::bad_msg_type);
    }

    if (hdr.byte_bus_id != expected_bus_id) return make_error_code(I2cErrc::wrong_bus);

    out_direction       = detail::op_to_dir(hdr.op);
    out_rx_data          = std::move(payload);
    out_timed            = timed;
    out_timestamp        = timestamp;
    out_transaction_num = hdr.transaction_num;
    return {};
}

// ── Trigger signals ───────────────────────────────────────────────────────────
// One TransferComplete/Nack pair per I2cEndpoint instance (I2C has no
// channel concept — a single I2cEndpoint instance models one controller-
// mode I2C bus), built on rcp/endpoint.hpp's generic TriggerRegistry.

enum class I2cSignal : uint8_t { TransferComplete = 0, Nack = 1 };

constexpr endpoint::TriggerRegistry::SignalId i2c_signal_id(I2cSignal sig) noexcept {
    return static_cast<endpoint::TriggerRegistry::SignalId>(sig);
}

// ── I2cEndpoint ───────────────────────────────────────────────────────────────
// A per-transfer convenience wrapper (no c-RCP equivalent — c-RCP is a pure
// free-function codec, see the free functions above for the real ACF-level
// wire content this class does not itself encode/decode): records the raw
// out/in byte streams of one transfer and fires trigger signals. Nack
// modeling is this class's own addition (TC18 does not itself model I2C
// bus-level ack/nack); everything else below is unchanged from the
// pre-rewrite pilot.
class I2cEndpoint {
public:
    std::error_code transfer(std::vector<uint8_t> out_bytes, std::vector<uint8_t> in_bytes,
                              bool acked = true) {
        last_out_ = std::move(out_bytes);
        last_in_  = std::move(in_bytes);
        triggers_.notify(i2c_signal_id(I2cSignal::TransferComplete));
        if (!acked) {
            triggers_.notify(i2c_signal_id(I2cSignal::Nack));
            return make_error_code(I2cErrc::nack);
        }
        return {};
    }

    // handle_request classifies the incoming request's evt[2:0] field via
    // rcp::endpoint::evt_row2_kind_of (Table 33's shared ADC/PWM_IN/I2C/LIN/
    // CAN/UART/ISELED/MDIO rule) before doing anything else:
    //   - Plain (evt[2:0] == 000b): delegates to transfer() unchanged.
    //   - Reserved (001b-110b): returns
    //     endpoint::EndpointErrc::reserved_evt_row2 without touching any
    //     endpoint state.
    //   - ConfigWrite (111b): returns I2cErrc::config_write_not_supported —
    //     see that enumerator's own comment for why this convenience call's
    //     out_bytes/in_bytes shape cannot carry a reconfig payload, and
    //     where the real, now-implemented mechanism lives.
    std::error_code handle_request(uint8_t evt_op, std::vector<uint8_t> out_bytes,
                                    std::vector<uint8_t> in_bytes, bool acked = true) {
        switch (endpoint::evt_row2_kind_of(evt_op)) {
        case endpoint::EvtRow2Kind::Plain:
            return transfer(std::move(out_bytes), std::move(in_bytes), acked);
        case endpoint::EvtRow2Kind::Reserved:
            return endpoint::make_error_code(endpoint::EndpointErrc::reserved_evt_row2);
        case endpoint::EvtRow2Kind::ConfigWrite:
            return make_error_code(I2cErrc::config_write_not_supported);
        }
        return endpoint::make_error_code(endpoint::EndpointErrc::reserved_evt_row2); // unreachable
    }

    const std::vector<uint8_t>& last_sent() const noexcept { return last_out_; }
    const std::vector<uint8_t>& last_received() const noexcept { return last_in_; }
    endpoint::TriggerRegistry& triggers() noexcept { return triggers_; }

private:
    endpoint::TriggerRegistry triggers_;
    std::vector<uint8_t>      last_out_;
    std::vector<uint8_t>      last_in_;
};

} // namespace i2c
} // namespace rcp

// Enable std::error_code construction from rcp::i2c::I2cErrc / I2cReconfigErrc.
namespace std {
template <>
struct is_error_code_enum<rcp::i2c::I2cErrc> : true_type {};
template <>
struct is_error_code_enum<rcp::i2c::I2cReconfigErrc> : true_type {};
} // namespace std
