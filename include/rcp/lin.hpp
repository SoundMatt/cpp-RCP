// fusa:req REQ-LINEP-001
// fusa:req REQ-LINEP-002
// fusa:req REQ-LINEP-003
// fusa:req REQ-LINEP-004
// fusa:req REQ-LINEP-005
// fusa:req REQ-LINEP-006
// fusa:req REQ-LINEP-007
// fusa:req REQ-LINEP-008
// fusa:req REQ-LINEP-009
// fusa:req REQ-LINEP-010
// fusa:req REQ-LINEP-011
// fusa:req REQ-LINEP-012
// fusa:req REQ-LINEP-013
// fusa:req REQ-LINEP-014
// fusa:req REQ-LINEP-015
// fusa:req REQ-LINEP-016
// fusa:req REQ-LINEP-017
// fusa:req REQ-LINEP-018
// fusa:req REQ-LINEP-019
// fusa:req REQ-LINEP-020
// fusa:req REQ-LINEP-021
// fusa:req REQ-LINEP-022
// fusa:req REQ-LINEP-024
// fusa:req REQ-LINEP-025
// fusa:req REQ-LINEP-027
// fusa:req REQ-LINEP-028
// fusa:req REQ-LINEP-029
// fusa:req REQ-LINEP-030
// fusa:req REQ-LINEP-031
// fusa:req REQ-LINEP-032
// fusa:req REQ-LINEP-033
// fusa:req REQ-LINEP-034
// fusa:req REQ-LINEP-035
// fusa:req REQ-LINEP-036
// fusa:req REQ-LINEP-037
// fusa:req REQ-LINEP-038
// fusa:req REQ-LINEP-039
// REQ-LINEP-023 and REQ-LINEP-026 are retired in c-RCP (near-duplicate ids
// consolidated into REQ-LINEP-030/REQ-LINEP-016 respectively) — not ported.

// LIN commander endpoint (ep_type 0x06) — the OPEN Alliance TC18 Remote
// Control Protocol Specification v0.5.1_RC's raw-byte-pusher model for LIN:
// a request carries the exact bytes to place on the bus and a response
// carries the exact bytes observed back, with no frame-level concept
// modeled by the endpoint itself at all — no checksum selection, no PID
// generation, no schedule tables (extraction §5.10, §6 "significant
// behavioral-scope question").
//
// ROADMAP.md "Phase 17" (cpp-RCP issue #129), Phase 3 ("Per-endpoint
// modules"): ported from c-RCP's include/rcp/ep_lin.h + src/ep_lin.c, this
// project's RC5-spec-conformant reference implementation for this endpoint
// type. No spec prose, bit layout, or numeric constant is reproduced here.
//
// This pass is additive on top of what this file already had (the
// LinEndpoint object model — transfer()/handle_request()/triggers() — from
// the pre-rewrite "Table 30/33 Row 2 evt[2:0] validation" milestone, KEPT
// with its exact existing signatures because rcp/mock.hpp's dispatch_lin
// already calls `lin_.handle_request(req.evt_op, payload, lin_response_,
// lin_responded_)` and this pass does not touch rcp/mock.hpp at all — see
// ROADMAP.md Phase 17 item 4, "Server/dispatch", which is where mock.hpp's
// own dispatch-loop rewiring belongs, not here):
//
//   - response_matches()/LinTrigger/trigger_fires(): ported from
//     rcp_ep_lin_response_matches()/rcp_ep_lin_trigger_t/
//     rcp_ep_lin_trigger_fires(). response_matches() is a thin wrapper over
//     rcp/acf.hpp's own compound_wait_match(evt=0, ...) (§13.5.1 mode 000b,
//     exact match) — LIN's own analogue of I2C's compound_wait_matches_bits,
//     not a fresh comparison routine.
//   - LinFunctionalConfig + functional_cfg_writable()/set_clk_divider()/
//     set_trigger(): ported from rcp_ep_lin_functional_cfg_t and its
//     setters, gated the same way every c-RCP endpoint type's functional
//     config is: rcp::lifecycle::field_writable() with FieldKind::FunctionalW
//     (c-RCP's RCP_LIFECYCLE_FIELD_FUNCTIONAL_W). The five ep_enable/
//     ep_clear_req_storage/ep_req_crc_enable/ep_response_ts_enable/
//     ep_suppress_response flags c-RCP composes via its own
//     rcp_regmap_ep_functional_cfg_t "common" prefix are kept as plain
//     members on LinFunctionalConfig directly instead — cpp-RCP's own
//     rcp/regmap.hpp EndpointFunctionalConfig is still the pre-rewrite
//     opaque-byte-blob shape as of this pass (regmap.hpp's real content
//     port is ROADMAP.md Phase 17 item 4, not this one) and has no such
//     five-flag "common" struct yet to compose against — see the
//     TODO(phase3-followup) below.
//   - The EP_func register block (TC18 §13.7.10.2 Table 55, evt[2:0]==111b
//     target): render_registers()/apply_reconfig()/encode_reconfig_request(),
//     ported from rcp_ep_lin_render_registers()/_apply_reconfig()/
//     _encode_reconfig_request(). kEpFuncLen (0x09) and every register
//     offset below match Table 55 exactly, per c-RCP's own already-verified
//     (no address-collision defect, unlike CAN's/GPIO's/I2C's own source
//     tables) rendering of it.
//   - The real ACF-level wire codec — encode_command_request()/
//     decode_command_request()/encode_response()/decode_response() — ported
//     from rcp_ep_lin_encode_command_request()/_decode_command_request()/
//     _encode_response()/_decode_response(). LIN's payload never approaches
//     ACF's own per-message ceiling (unlike CAN XL — see rcp/can.hpp), so
//     unlike that module this one needs no rcp/fragment.hpp wiring at all.
//
// TODO(phase3-followup): once rcp/regmap.hpp's own functional-config split
// is re-derived from c-RCP (ROADMAP.md Phase 17 item 4), recompose
// LinFunctionalConfig's five ep_enable/ep_clear_req_storage/ep_req_crc_enable/
// ep_response_ts_enable/ep_suppress_response flags on top of that shared
// struct (matching rcp_ep_lin_functional_cfg_t's own `common` composition)
// instead of carrying local duplicates of them here.
//
// Field names and behavior below implement TC18's *behavior* as described in
// an internal structured extraction of the specification named above; no
// text from that document is reproduced here. The concrete transfer-shape
// and trigger-signal id encoding chosen in this file are this
// implementation's own, same as the equivalent disclaimers in rcp/avtp.hpp,
// rcp/acf.hpp, rcp/endpoint.hpp, rcp/i2c.hpp, rcp/adc.hpp, and rcp/pwm.hpp.
#pragma once

#include <rcp/acf.hpp>
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
namespace lin {

// ── Errors ────────────────────────────────────────────────────────────────────
// no_response/config_write_not_supported (values 1/2) are unchanged from
// this header's pre-existing content — rcp/mock.hpp's wire_error_code_for()
// compares against both by name. short_frame..bad_evt (3..6) are new,
// ported from rcp_ep_lin_errc_t (RCP_EP_LIN_ERR_*), for the new wire-codec
// functions below.

enum class LinErrc : int {
    no_response = 1,
    // evt_row2_kind_of classified the request as ConfigWrite (evt[2:0] ==
    // 111b, §12.7.1). This milestone's follow-up deliberately does not
    // implement the configuration-write shape (relative EP_functional-
    // config start address + configuration data) — see handle_request's
    // own comment. Reported explicitly rather than silently accepted as a
    // plain transfer or silently ignored, same as I2C's, ADC's, and
    // PWM_IN's own config_write_not_supported variants.
    config_write_not_supported = 2,
    short_frame                = 3, // ported from RCP_EP_LIN_ERR_SHORT_FRAME
    bad_msg_type                = 4, // ported from RCP_EP_LIN_ERR_BAD_MSG_TYPE
    wrong_bus                   = 5, // ported from RCP_EP_LIN_ERR_WRONG_BUS
    wrong_op                    = 6, // ported from RCP_EP_LIN_ERR_WRONG_OP
    bad_evt                     = 7, // ported from RCP_EP_LIN_ERR_BAD_EVT
};

inline const std::error_category& lin_category() noexcept {
    struct Cat : std::error_category {
        const char* name() const noexcept override { return "rcp.lin"; }
        std::string message(int ev) const override {
            switch (static_cast<LinErrc>(ev)) {
            case LinErrc::no_response: return "rcp/lin: no response observed on the bus";
            case LinErrc::config_write_not_supported:
                return "rcp/lin: evt[2:0]=111b configuration-write requests are not yet implemented";
            case LinErrc::short_frame:   return "rcp/lin: frame too short";
            case LinErrc::bad_msg_type:  return "rcp/lin: unexpected ACF message type";
            case LinErrc::wrong_bus:     return "rcp/lin: wrong byte_bus_id";
            case LinErrc::wrong_op:      return "rcp/lin: wrong ACF op";
            case LinErrc::bad_evt:       return "rcp/lin: evt[2:0] is not plain (000b)";
            default: return "rcp/lin: unknown error";
            }
        }
    };
    static Cat instance;
    return instance;
}

inline std::error_code make_error_code(LinErrc e) noexcept {
    return {static_cast<int>(e), lin_category()};
}

// ── Trigger signals ───────────────────────────────────────────────────────────
// One TransferComplete/NoResponse pair per LinEndpoint instance — LIN has no
// pre-configured channel concept in this milestone's scope (a single
// LinEndpoint instance models one commander-mode LIN bus), matching
// rcp/i2c.hpp's own no-channel precedent. Built on rcp/endpoint.hpp's
// generic TriggerRegistry.

enum class LinSignal : uint8_t { TransferComplete = 0, NoResponse = 1 };

constexpr endpoint::TriggerRegistry::SignalId lin_signal_id(LinSignal sig) noexcept {
    return static_cast<endpoint::TriggerRegistry::SignalId>(sig);
}

// ── evt[2:0]: exact-match, per Table 33's plain-request row ─────────────────
// response_matches is a thin, named wrapper over rcp/acf.hpp's
// compound_wait_match(evt=0, ...) — TC18 §13.5.1 mode 000b, exact match,
// length-capped — the only comparison a plain LIN command request's
// evt[2:0] can ever select (Table 33 constrains it to 000b). Ported from
// rcp_ep_lin_response_matches(). tx_data/rx_data may be empty.
inline bool response_matches(const uint8_t* tx_data, size_t tx_len, const uint8_t* rx_data,
                              size_t rx_len) noexcept {
    return acf::compound_wait_match(0x0u, tx_data, tx_len, rx_data, rx_len);
}
inline bool response_matches(const std::vector<uint8_t>& tx, const std::vector<uint8_t>& rx) noexcept {
    return response_matches(tx.data(), tx.size(), rx.data(), rx.size());
}

// ── Transmission-done trigger (entirely this module's own design — TC18
// defines no "lin trigger outputs" table at all, per c-RCP's own
// c-RCP-AUDIT-06/issue #256 Group C finding) ─────────────────────────────

enum class LinTrigger : uint8_t { None = 0, TxDone = 1 };

// True iff tx_done_event and trailing_time_expired together satisfy
// trigger: never for None; for TxDone iff BOTH are true. Ported from
// rcp_ep_lin_trigger_fires().
inline bool trigger_fires(LinTrigger trigger, bool tx_done_event,
                           bool trailing_time_expired) noexcept {
    switch (trigger) {
    case LinTrigger::TxDone: return tx_done_event && trailing_time_expired;
    case LinTrigger::None:
    default:                 return false;
    }
}

// ── Functional config ─────────────────────────────────────────────────────────
// Ported from rcp_ep_lin_functional_cfg_t — see this file's own header
// comment for why the five Table 35 "common" flags are local members here
// rather than composed from rcp/regmap.hpp's EndpointFunctionalConfig.

struct LinFunctionalConfig {
    bool ep_enable             = false;
    bool ep_clear_req_storage  = false;
    bool ep_req_crc_enable     = false;
    bool ep_response_ts_enable = false;
    bool ep_suppress_response  = false;

    uint32_t   lin_clk_divider = 0;              // bit-time clock divider; see the file header
    LinTrigger trigger         = LinTrigger::None;
    uint16_t   ep_status       = 0;               // lin_ep_status, Table 55
    uint8_t    wire_clk_divider = 0;              // lin_clk_divider, Table 55 — see the file header
};

// functional_cfg_writable is a thin, named wrapper over
// rcp::lifecycle::field_writable() with FieldKind::FunctionalW — ported
// from rcp_ep_lin_functional_cfg_writable(). Reuses, and never duplicates,
// that function's authorization logic.
inline bool functional_cfg_writable(lifecycle::ServerState state,
                                     lifecycle::WriterCtx writer) noexcept {
    return lifecycle::field_writable(state, lifecycle::FieldKind::FunctionalW, writer);
}

// Sets cfg.lin_clk_divider iff functional_cfg_writable() authorizes the
// write; returns whether the write was applied. cfg is left entirely
// unchanged when it returns false. Ported from rcp_ep_lin_set_clk_divider().
inline bool set_clk_divider(LinFunctionalConfig& cfg, uint32_t lin_clk_divider,
                             lifecycle::ServerState state, lifecycle::WriterCtx writer) noexcept {
    if (!functional_cfg_writable(state, writer)) return false;
    cfg.lin_clk_divider = lin_clk_divider;
    return true;
}

// Same authorization rule, for cfg.trigger. Ported from
// rcp_ep_lin_set_trigger().
inline bool set_trigger(LinFunctionalConfig& cfg, LinTrigger trigger, lifecycle::ServerState state,
                         lifecycle::WriterCtx writer) noexcept {
    if (!functional_cfg_writable(state, writer)) return false;
    cfg.trigger = trigger;
    return true;
}

// ── The EP_func register block (evt[2:0] == 111b target), TC18 §13.7.10.2
// Table 55 ──────────────────────────────────────────────────────────────────
//
//   0x0000  lin_ep_len        8 bit  R    kEpFuncLen (0x09)
//   0x0001  Reserved          8 bit  R    reads 0x00
//   0x0002  lin_ep_enable&clr 8 bit  R/W  Table 35 common entries
//   0x0003  lin_ep_options    8 bit  R/W* Table 35 common entries
//   0x0004  lin_base_clk     16 bit  R    LIN system clock (always renders 0
//                                          — no real clock source modelled,
//                                          same honesty every other endpoint
//                                          type's own base_clk field commits
//                                          to)
//   0x0006  lin_ep_status    16 bit  R/W
//   0x0008  lin_clk_divider   8 bit  R/W  generates the LIN bit time
//
// Ported from ep_lin.c's RCP_EP_LIN_REG_*/rcp_ep_lin_render_registers()/
// rcp_ep_lin_apply_reconfig().

constexpr uint16_t kRegEpLen        = 0x0000;
constexpr uint16_t kRegReserved01   = 0x0001;
constexpr uint16_t kRegEpEnableClr  = 0x0002;
constexpr uint16_t kRegEpOptions    = 0x0003;
constexpr uint16_t kRegBaseClk      = 0x0004;
constexpr uint16_t kRegEpStatus     = 0x0006;
constexpr uint16_t kRegClkDivider   = 0x0008;

constexpr size_t kEpFuncLen        = 0x0009;
constexpr size_t kReconfigAddrLen  = 2;

namespace detail {
constexpr uint8_t kEnableClrBitEnable = (1u << 0);
constexpr uint8_t kEnableClrBitClear  = (1u << 4);
constexpr uint8_t kOptionsBitReqCrc   = (1u << 0);
constexpr uint8_t kOptionsBitRespTs   = (1u << 3);
constexpr uint8_t kOptionsBitSuppress = (1u << 7);

inline void put_u16(uint8_t* p, uint16_t v) noexcept {
    p[0] = static_cast<uint8_t>((v >> 8) & 0xFFu);
    p[1] = static_cast<uint8_t>(v & 0xFFu);
}
inline uint16_t get_u16(const uint8_t* p) noexcept {
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}
} // namespace detail

// Serializes cfg's EP_func registers into out[0..kEpFuncLen) exactly as a
// configuration *read* of the whole block would report them — the inverse
// of apply_reconfig()'s own parse step. Ported from
// rcp_ep_lin_render_registers().
inline void render_registers(const LinFunctionalConfig& cfg,
                              std::array<uint8_t, kEpFuncLen>& out) noexcept {
    uint8_t enable_clr = 0;
    uint8_t options    = 0;
    if (cfg.ep_enable) enable_clr |= detail::kEnableClrBitEnable;
    if (cfg.ep_clear_req_storage) enable_clr |= detail::kEnableClrBitClear;
    if (cfg.ep_req_crc_enable) options |= detail::kOptionsBitReqCrc;
    if (cfg.ep_response_ts_enable) options |= detail::kOptionsBitRespTs;
    if (cfg.ep_suppress_response) options |= detail::kOptionsBitSuppress;

    out[kRegEpLen]       = static_cast<uint8_t>(kEpFuncLen);
    out[kRegReserved01]  = 0;
    out[kRegEpEnableClr] = enable_clr;
    out[kRegEpOptions]   = options;
    detail::put_u16(&out[kRegBaseClk], 0); // no real clock source modelled
    detail::put_u16(&out[kRegEpStatus], cfg.ep_status);
    out[kRegClkDivider] = cfg.wire_clk_divider;
}

namespace detail {
inline bool reg_offset_read_only(uint16_t addr) noexcept {
    return addr == kRegEpLen || addr == kRegReserved01 || addr == kRegBaseClk ||
           addr == static_cast<uint16_t>(kRegBaseClk + 1);
}
} // namespace detail

enum class LinReconfigErrc : int {
    short_payload = 1, // payload carries no address prefix, or an address prefix with no data octet after it
    out_of_range  = 2, // start_address + data length exceeds kEpFuncLen — the whole write is ignored
};

inline const std::error_category& lin_reconfig_category() noexcept {
    struct Cat : std::error_category {
        const char* name() const noexcept override { return "rcp.lin.reconfig"; }
        std::string message(int ev) const override {
            switch (static_cast<LinReconfigErrc>(ev)) {
            case LinReconfigErrc::short_payload: return "rcp/lin: LIN configuration write has no address and data";
            case LinReconfigErrc::out_of_range:  return "rcp/lin: LIN configuration write extends past the EP_func block";
            default: return "rcp/lin: LIN unknown configuration-write error";
            }
        }
    };
    static Cat instance;
    return instance;
}

inline std::error_code make_error_code(LinReconfigErrc e) noexcept {
    return {static_cast<int>(e), lin_reconfig_category()};
}

// Applies the configuration escape hatch (evt[2:0] == 111b): payload is NOT
// presented at the interface but interpreted as an addressed write into
// this endpoint's own EP_func block — a 16-bit big-endian relative start
// address followed by the configuration data octets to write from that
// address onward (§12.7.1). Ported from rcp_ep_lin_apply_reconfig(). cfg is
// left entirely unchanged on error, per the specification's own "such a
// payload is to be ignored" rule. Octets of the addressed span that land on
// a read-only register (EP_LEN, the reserved octet, base_clk) are left at
// their current values while the rest of the span is still applied.
inline std::error_code apply_reconfig(LinFunctionalConfig& cfg, const uint8_t* payload,
                                       size_t payload_len) {
    if (payload_len <= kReconfigAddrLen) return make_error_code(LinReconfigErrc::short_payload);

    const uint16_t start_address = detail::get_u16(payload);
    const size_t   data_len      = payload_len - kReconfigAddrLen;

    if (static_cast<size_t>(start_address) + data_len > kEpFuncLen)
        return make_error_code(LinReconfigErrc::out_of_range);

    std::array<uint8_t, kEpFuncLen> block{};
    render_registers(cfg, block);
    for (size_t i = 0; i < data_len; ++i) {
        const auto addr = static_cast<uint16_t>(start_address + i);
        if (detail::reg_offset_read_only(addr)) continue; // write ignored
        block[addr] = payload[kReconfigAddrLen + i];
    }

    cfg.ep_enable             = (block[kRegEpEnableClr] & detail::kEnableClrBitEnable) != 0;
    cfg.ep_clear_req_storage  = (block[kRegEpEnableClr] & detail::kEnableClrBitClear) != 0;
    cfg.ep_req_crc_enable     = (block[kRegEpOptions] & detail::kOptionsBitReqCrc) != 0;
    cfg.ep_response_ts_enable = (block[kRegEpOptions] & detail::kOptionsBitRespTs) != 0;
    cfg.ep_suppress_response  = (block[kRegEpOptions] & detail::kOptionsBitSuppress) != 0;
    cfg.ep_status             = detail::get_u16(&block[kRegEpStatus]);
    cfg.wire_clk_divider      = block[kRegClkDivider];

    return {};
}

// Encodes an ACF_ABB configuration request (evt[2:0] == 111b) addressed to
// byte_bus_id: payload is start_address (16-bit big-endian) followed by
// data. Returns an empty vector if data is empty. Ported from
// rcp_ep_lin_encode_reconfig_request().
inline std::vector<uint8_t> encode_reconfig_request(avtp::ByteBusId byte_bus_id,
                                                      uint16_t start_address,
                                                      const std::vector<uint8_t>& data,
                                                      uint8_t transaction_num) {
    if (data.empty()) return {};

    std::vector<uint8_t> payload(kReconfigAddrLen + data.size());
    detail::put_u16(payload.data(), start_address);
    std::copy(data.begin(), data.end(), payload.begin() + static_cast<long>(kReconfigAddrLen));

    acf::AcfMessageInfo hdr;
    hdr.byte_bus_id     = byte_bus_id;
    hdr.op              = true; // write — §12.7.1 Figure 18
    hdr.evt_op          = 0x7u; // evt[2:0] = 111b
    hdr.transaction_num = transaction_num;
    return acf::encode_acf_abb(hdr, payload);
}

// ── ACF-level wire codec: command request / response ─────────────────────────
// Ported from rcp_ep_lin_encode_command_request()/_decode_command_request()/
// _encode_response()/_decode_response(). LIN's payload never approaches
// ACF's own per-message ceiling, so this module needs no fragmentation.

// Encodes an ACF_ABB command request addressed to byte_bus_id: the payload
// is exactly tx_data, the raw bytes driven directly onto the bus for this
// transaction — every LIN-frame semantic already constructed into these
// bytes by the caller (see the file header). Encoded as a read-direction
// request (op=false): a LIN command request expects the endpoint to reply
// with the bytes received on the bus, so encoding it as a write would tell
// a conforming peer the opposite of what the request means. evt is always 0.
inline std::vector<uint8_t> encode_command_request(avtp::ByteBusId byte_bus_id,
                                                     const std::vector<uint8_t>& tx_data,
                                                     uint8_t transaction_num) {
    acf::AcfMessageInfo hdr;
    hdr.byte_bus_id     = byte_bus_id;
    hdr.op              = false; // read direction — see this function's own comment
    hdr.transaction_num = transaction_num;
    return acf::encode_acf_abb(hdr, tx_data);
}

// Decodes and validates an ACF-level LIN command request from b[0..len).
// Fails with LinErrc::short_frame/bad_msg_type/wrong_bus/wrong_op/bad_evt —
// see ep_lin.c's own doc comment for the exact condition each maps to. On
// success, *out_tx_data/*out_transaction_num are populated; payload is
// round-tripped verbatim, byte for byte, with no protocol-level LIN-frame
// parsing of any kind.
inline std::error_code decode_command_request(const uint8_t* b, size_t len,
                                               avtp::ByteBusId expected_bus_id,
                                               std::vector<uint8_t>& out_tx_data,
                                               uint8_t& out_transaction_num) {
    acf::AcfMessageInfo   hdr;
    std::vector<uint8_t> payload;
    auto ec = acf::decode_acf_abb(b, len, hdr, payload);
    if (ec == avtp::make_error_code(avtp::AvtpErrc::short_buffer)) return make_error_code(LinErrc::short_frame);
    if (ec) return make_error_code(LinErrc::bad_msg_type);

    if (hdr.byte_bus_id != expected_bus_id) return make_error_code(LinErrc::wrong_bus);
    if (hdr.op) return make_error_code(LinErrc::wrong_op); // read direction — see encode_command_request()
    if (!acf::evt_row2_is_plain(hdr.evt_op)) return make_error_code(LinErrc::bad_evt);

    out_tx_data         = std::move(payload);
    out_transaction_num = hdr.transaction_num;
    return {};
}

// Encodes a LIN response carrying rx_data (the raw bytes captured back from
// the bus) as its payload, echoing transaction_num. Encoded as ACF_ABB when
// timed is false; as ACF_GBB (message_timestamp = timestamp, mtv = true)
// when timed is true.
inline std::vector<uint8_t> encode_response(avtp::ByteBusId byte_bus_id,
                                             const std::vector<uint8_t>& rx_data,
                                             uint8_t transaction_num, bool timed,
                                             uint64_t timestamp) {
    if (timed) {
        acf::AcfMessageInfo hdr;
        hdr.byte_bus_id     = byte_bus_id;
        hdr.op              = false;
        hdr.rsp             = true;
        hdr.mtv             = true;
        hdr.transaction_num = transaction_num;
        return acf::encode_acf_gbb(hdr, timestamp, rx_data);
    }
    acf::AcfMessageInfo hdr;
    hdr.byte_bus_id     = byte_bus_id;
    hdr.op              = false;
    hdr.rsp             = true;
    hdr.transaction_num = transaction_num;
    return acf::encode_acf_abb(hdr, rx_data);
}

// Decodes a LIN response from either an ACF_ABB or ACF_GBB message (peeks
// the ACF message type itself, unlike decode_command_request(), since a
// response's encoding depends on the responding endpoint's own
// timed/untimed choice). Fails with LinErrc::short_frame/bad_msg_type/
// wrong_bus. On success, every output parameter is populated; *out_timed
// and *out_timestamp report whether the message was ACF_GBB with a valid
// timestamp, and that timestamp's value (0 when !*out_timed).
inline std::error_code decode_response(const uint8_t* b, size_t len, avtp::ByteBusId expected_bus_id,
                                        std::vector<uint8_t>& out_rx_data, bool& out_timed,
                                        uint64_t& out_timestamp, uint8_t& out_transaction_num) {
    uint8_t msg_type;
    auto peek_ec = acf::peek_msg_type(b, len, msg_type);
    if (peek_ec) return make_error_code(LinErrc::short_frame);

    acf::AcfMessageInfo   hdr;
    std::vector<uint8_t> payload;
    avtp::ByteBusId        bus_id;
    bool                    timed;
    uint64_t                timestamp;

    if (msg_type == acf::kAcfMsgTypeGbb) {
        uint64_t ts = 0;
        auto ec = acf::decode_acf_gbb(b, len, hdr, ts, payload);
        if (ec == avtp::make_error_code(avtp::AvtpErrc::short_buffer)) return make_error_code(LinErrc::short_frame);
        if (ec) return make_error_code(LinErrc::bad_msg_type);
        bus_id    = hdr.byte_bus_id;
        timed     = hdr.mtv;
        timestamp = timed ? ts : 0;
    } else {
        auto ec = acf::decode_acf_abb(b, len, hdr, payload);
        if (ec == avtp::make_error_code(avtp::AvtpErrc::short_buffer)) return make_error_code(LinErrc::short_frame);
        if (ec) return make_error_code(LinErrc::bad_msg_type);
        bus_id    = hdr.byte_bus_id;
        timed     = false;
        timestamp = 0;
    }

    if (bus_id != expected_bus_id) return make_error_code(LinErrc::wrong_bus);

    out_rx_data         = std::move(payload);
    out_timed            = timed;
    out_timestamp         = timestamp;
    out_transaction_num   = hdr.transaction_num;
    return {};
}

// ── LinEndpoint ───────────────────────────────────────────────────────────────
// Mirrors rcp::i2c::I2cEndpoint's shape deliberately: one request-dispatch
// entry point per incoming LIN transfer, recording the raw bytes pushed and
// the raw bytes observed back, with no interpretation of either as a LIN
// frame (extraction §5.10). `out_bytes` is whatever byte sequence the
// caller's driver constructed (break/sync/PID/data/checksum all already
// assembled by that driver, not by this header); `in_bytes` is this
// implementation's record of whatever came back over the same exchange
// (supplied by the caller — this header models the request/response and
// trigger-signal shape of a LIN commander transfer, not an actual bus).
//
// transfer()/handle_request() below are UNCHANGED from this header's
// pre-existing content (signature and behavior) — rcp/mock.hpp's
// dispatch_lin() already calls handle_request() with this exact signature
// and this pass does not touch rcp/mock.hpp.
class LinEndpoint {
public:
    std::error_code transfer(std::vector<uint8_t> out_bytes, std::vector<uint8_t> in_bytes,
                              bool responded = true) {
        last_out_ = std::move(out_bytes);
        last_in_  = std::move(in_bytes);
        if (!responded) {
            triggers_.notify(lin_signal_id(LinSignal::NoResponse));
            return make_error_code(LinErrc::no_response);
        }
        triggers_.notify(lin_signal_id(LinSignal::TransferComplete));
        return {};
    }

    // handle_request is LIN's request-decode entry point, mirroring
    // rcp::i2c::I2cEndpoint::handle_request's shape (this repo's fourth
    // Table 33 Row 2 endpoint type after I2C, ADC, and PWM_IN). It
    // classifies the incoming request's evt[2:0] field via
    // rcp::endpoint::evt_row2_kind_of before doing anything else, so a
    // Reserved value can never reach transfer() and be misread as an
    // ordinary transfer, and a ConfigWrite value can never be silently
    // accepted or silently dropped:
    //   - Plain (evt[2:0] == 000b): delegates straight to transfer() with
    //     `out_bytes`/`in_bytes`/`responded` unchanged.
    //   - Reserved (evt[2:0] in 001b-110b): returns
    //     endpoint::EndpointErrc::reserved_evt_row2 without touching any
    //     endpoint state or recording anything as sent/received.
    //   - ConfigWrite (evt[2:0] == 111b): returns
    //     LinErrc::config_write_not_supported — this handle_request/
    //     transfer() object-model path is deliberately independent of the
    //     new apply_reconfig()/LinFunctionalConfig surface added by this
    //     pass (wiring the two together, like wiring fragmentation into
    //     mock.hpp's own dispatch loop, is Phase 4's "Server/dispatch"
    //     scope, not this pass's).
    std::error_code handle_request(uint8_t evt_op, std::vector<uint8_t> out_bytes,
                                    std::vector<uint8_t> in_bytes, bool responded = true) {
        switch (endpoint::evt_row2_kind_of(evt_op)) {
        case endpoint::EvtRow2Kind::Plain:
            return transfer(std::move(out_bytes), std::move(in_bytes), responded);
        case endpoint::EvtRow2Kind::Reserved:
            return endpoint::make_error_code(endpoint::EndpointErrc::reserved_evt_row2);
        case endpoint::EvtRow2Kind::ConfigWrite:
            return make_error_code(LinErrc::config_write_not_supported);
        }
        return endpoint::make_error_code(endpoint::EndpointErrc::reserved_evt_row2); // unreachable
    }

    const std::vector<uint8_t>& last_sent() const noexcept { return last_out_; }
    const std::vector<uint8_t>& last_received() const noexcept { return last_in_; }
    endpoint::TriggerRegistry& triggers() noexcept { return triggers_; }

    LinFunctionalConfig&       functional_config() noexcept { return cfg_; }
    const LinFunctionalConfig& functional_config() const noexcept { return cfg_; }

private:
    endpoint::TriggerRegistry triggers_;
    std::vector<uint8_t>      last_out_;
    std::vector<uint8_t>      last_in_;
    LinFunctionalConfig       cfg_;
};

} // namespace lin
} // namespace rcp

// Enable std::error_code construction from rcp::lin::LinErrc/LinReconfigErrc.
namespace std {
template <>
struct is_error_code_enum<rcp::lin::LinErrc> : true_type {};
template <>
struct is_error_code_enum<rcp::lin::LinReconfigErrc> : true_type {};
} // namespace std
