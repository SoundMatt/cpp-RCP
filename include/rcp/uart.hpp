// fusa:req REQ-UART-001
// fusa:req REQ-UART-002
// fusa:req REQ-UART-003
// fusa:req REQ-UART-004
// fusa:req REQ-UART-005
// fusa:req REQ-UART-006
// fusa:req REQ-UART-007
// fusa:req REQ-UART-008
// fusa:req REQ-UART-009

// UART endpoint (ep_type 0x05) — independent TX/RX request families sharing
// one functional-config block (baud rate, word format, flow control), its
// Table 51 functional-configuration register block (§13.7.8.2, reachable
// through the generic evt[2:0]=111b configuration-write escape hatch,
// §12.7.1), Table 52's two HW trigger signals (§13.7.8.4), the Table 33 Row
// 2 evt[2:0] plain/reserved/config-write classification every endpoint type
// in that row shares (§13.5), and §13.7.8.1's read-completion race
// (read_size satisfied / uart_timeout expired / fifo-full-so-fragment),
// wired to this project's Phase 20 fragmentation primitive (rcp/fragment.hpp).
//
// Phase 3 rewrite (cpp-RCP issue #129, ROADMAP.md "Phase 17"), content-
// corrected against c-RCP's include/rcp/ep_uart.h + src/ep_uart.c — this
// project's RC5-spec-conformant reference for this module. This pass is a
// content correction, not a fresh design: it keeps this header's own
// pre-rewrite UartEndpoint TX/RX-queue convenience class (enqueue_tx/
// drain_tx/rx_fill/handle_read/handle_pure_read/handle_request) unchanged —
// no c-RCP equivalent (c-RCP is a pure free-function codec with no
// persistent per-endpoint "queue" object), already wired into rcp/mock.hpp's
// real dispatch, and re-verified rather than replaced — while porting in,
// for the first time, the real ACF-level wire codec and functional-config
// content this header previously had none of at all:
//   - the functional-config block (UartFunctionalCfg: baud_rate, word format
//     [uart_nr_bits/parity/stop_bits], ep_rx_buffer_size, uart_timeout_ms,
//     plus Table 51's own rts_enable/cts_enable/half_duplex/trail/
//     baud_rate_kbps/wire_timeout_bit_times/ep_status fields) and its
//     setters (set_baud_rate/set_frame_format/set_rx_buffer_size/
//     set_timeout/set_trigger), each gated by functional_cfg_writable()
//   - Table 51's own EP_func register block (render_registers()/
//     apply_reconfig()/encode_reconfig_request()), the evt[2:0]==111b
//     configuration escape hatch this header never implemented at all before
//   - REQ-UART-037: rcp_ep_uart_stop_bits_t's real THIRD member. c-RCP's own
//     stop_bits enum shipped for a long time with exactly two values (ONE=0,
//     TWO=1) before a later, separately-numbered fix (tc18-gap post-backlog
//     audit, 2026-08-14, split 2026-08-18 into the now-independent
//     REQ-UART-049) added ONE_HALF as a genuine third, appended (not
//     inserted) member — Table 51's own uart_stop_bits register is in HALF
//     stop-bit units (2/3/4 for one/one-and-a-half/two stop bits), and this
//     port carries the exact same three-way enum and the exact same
//     stop_bits_to_half_units()/half_units_to_stop_bits() round-trip below,
//     including ONE_HALF's deliberately-appended numeric value (2, not 3) so
//     TWO's own pre-existing value (1) never moves — see StopBits below.
//   - Table 52's two HW trigger signals (UartTrigger/UartEvent/
//     trigger_fires()) — TX_FINALIZED/RX_FINALIZED, off-by-one against the
//     table's own signal numbers 0/1 because NONE occupies ordinal 0 here,
//     exactly as c-RCP's own header documents (this codec renders neither
//     onto the wire — Table 51 has no trigger-mode register of its own).
//   - the TX (write) and RX (read) request/response codec pairs
//     (encode_write_request/decode_write_request/encode_write_response/
//     decode_write_response, encode_read_request/decode_read_request/
//     encode_read_response/decode_read_response), all built on rcp/acf.hpp's
//     existing ACF_ABB/ACF_GBB codec rather than re-deriving frame layout —
//     evt is always 0 for a plain request; this endpoint type carries no
//     channel/port selector of its own, exactly as c-RCP's own file header
//     documents ("like ep_i2c.h, has no channel selector") — so there is no
//     max-channel-count constant to port here, unlike SPI's channel array or
//     GPIO's pin array.
//   - REQ-UART-033/037/048's read-completion arbitration
//     (read_completion_decision()) and wire-timeout conversion
//     (wire_timeout_us()) — pure, directly-testable functions over
//     caller-tracked counters; this module still owns no FIFO or clock of
//     its own, matching every other caller-driven primitive in this
//     codebase.
//   - REQ-UART-034/029/030/031: read_size is the ACF header's own full
//     12-bit width (kMaxReadSize is now 0x0FFF, not an artificial
//     single-AVTPDU ceiling — see below), and a genuinely oversized read
//     response is now fragmentable via read_response_fragment_count()/
//     encode_read_response_fragmented()/decode_read_response_fragment(),
//     rcp/fragment.hpp's Phase 20 primitive wired into this endpoint type
//     for the first time (fragment.hpp's own header comment lists
//     rcp/uart.hpp among its documented, previously-unwired callers — this
//     pass closes that gap). UartEndpoint's own handle_read()/handle_request()
//     convenience-class methods are intentionally left with their existing,
//     simpler two-way (read_size-reached vs. uart_timeout-elapsed) behavior
//     unchanged, for source and behavior stability with rcp/mock.hpp's
//     existing dispatch_uart() wiring and tests/test_mock.cpp's own UART
//     coverage — a caller that wants the real three-way arbitration or
//     fragmentation calls read_completion_decision()/
//     encode_read_response_fragmented() directly, the same division of
//     responsibility gpio.hpp/i2c.hpp already draw between their own
//     convenience classes and free-function codecs.
//   - sub-octet bit-width padding (nr_bits_valid()/bit_pad_mask()/
//     apply_bit_padding(), plus the pre-existing pack_frame_to_octet()/
//     unpack_frame_bits() convenience wrappers, now rebuilt on top of them):
//     WIDENED from this header's own pre-rewrite [5,8]-only accepted range
//     to c-RCP's real [1,8] (RCP_EP_UART_NR_BITS_MIN..MAX) — a genuinely
//     valid 1-4-bit UART word width was silently rejected before this pass;
//     c-RCP's own file header is explicit that 1..8 is exactly what this
//     one-byte-per-word wire representation can carry.
//
// ── Table 33 Row 2 evt[2:0] validation (handle_request) — re-verified ──────
// UartEndpoint::handle_request's own evt_row2_kind_of dispatch (added by an
// earlier pass, the sixth endpoint type after I2C/ADC/PWM_IN/LIN/CAN to wire
// this classification in) is re-verified against c-RCP's *current*
// ep_uart.c and is unchanged by this pass: decode_write_request()/
// decode_read_request() below independently apply the identical
// acf::evt_row2_is_plain() rule c-RCP's own rcp_ep_uart_decode_write_request()/
// _decode_read_request() do (RCP_EP_UART_ERR_BAD_EVT), and c-RCP's own evt/
// channel semantics have not changed since that earlier pass — no delta
// found here.
//
// Field names and behavior below implement TC18's *behavior* as described in
// an internal structured extraction of the specification named above; no
// text from that document is reproduced here. The concrete struct/enum
// shapes chosen in this file are this implementation's own, same as the
// equivalent disclaimers in rcp/acf.hpp, rcp/avtp.hpp, rcp/endpoint.hpp,
// rcp/lifecycle.hpp, rcp/fragment.hpp, rcp/gpio.hpp, and rcp/i2c.hpp.
#pragma once

#include <rcp/acf.hpp>
#include <rcp/avtp.hpp>
#include <rcp/endpoint.hpp>
#include <rcp/fragment.hpp>
#include <rcp/lifecycle.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace rcp {
namespace uart {

// ── Single-AVTPDU-vs-real-wire-width bound ────────────────────────────────────
// FIXED (REQ-UART-034, matching i2c.hpp's identical kMaxReadSize correction):
// kMaxReadSize is now the ACF header's own real 12-bit read_size_or_segment_num
// width (0-4095), not an artificial, conservative single-AVTPDU ceiling — a
// caller asking for more than kRxFifoCapacity/kTxQueueCapacity octets is no
// longer inexpressible; it is served, per TC18 §13.7.8.1's own third
// read-completion trigger, via read_response_fragment_count()/
// encode_read_response_fragmented() below. kRxFifoCapacity/kTxQueueCapacity
// remain this convenience class's own, deliberately separate, implementation-
// chosen queue-capacity bound (unrelated to the wire's own read_size limit,
// same as c-RCP's ep_rx_buffer_size functional-config field is a distinct
// concept from a request's own read_size) — decoupled from kMaxReadSize by
// this pass rather than left aliased to it.

constexpr uint16_t kMaxReadSize     = 0x0FFFu; // ACF's 12-bit read_size_or_segment_num max
constexpr size_t   kRxFifoCapacity  = 512;     // this convenience class's own queue-capacity bound
constexpr size_t   kTxQueueCapacity = 512;

// ── Errors ────────────────────────────────────────────────────────────────────
// Kept as one shared error category/enum for both UartEndpoint's own
// pre-existing convenience-class errors (1-5, unchanged in value — relied on
// by rcp/mock.hpp's dispatch_uart()) and the newly-ported free-function wire
// codec's errors (6-11, additive), the same single-enum consolidation
// i2c.hpp's own I2cErrc already uses for its convenience class + codec.

enum class UartErrc : int {
    read_size_exceeds_bound    = 1, // requested read_size > kMaxReadSize
    rx_fifo_overflow           = 2, // rx_fill would push the RX FIFO past kRxFifoCapacity
    tx_queue_overflow          = 3, // enqueue_tx would push the TX queue past kTxQueueCapacity
    bits_per_frame_out_of_range = 4, // nr_bits/bits_per_frame outside [kNrBitsMin, kNrBitsMax]
    // evt_row2_kind_of classified the request as ConfigWrite (evt[2:0] ==
    // 111b, §12.7.1) via UartEndpoint::handle_request's own convenience
    // dispatch — that call's own out_data/out_bytes shape cannot carry a
    // reconfig payload; the real mechanism is apply_reconfig()/
    // render_registers()/encode_reconfig_request() below, called directly.
    config_write_not_supported = 5,
    // ── Ported from c-RCP's rcp_ep_uart_errc_t (RCP_EP_UART_ERR_*) ──────────
    short_frame  = 6,
    bad_msg_type = 7,
    wrong_bus    = 8,
    wrong_op     = 9,
    // A read request carried a payload — nothing meaningful a UART read
    // request's payload could carry (read_size already rides the ACF
    // header's own field); this endpoint type treats one arriving anyway as
    // an unrecognized command, a deliberate asymmetry against GPIO's write
    // requests / PWM_OUT, which do accept a payload on some request types.
    unknown_cmd  = 10,
    // evt[2:0] is not 0b000, TC18 §13.5 Table 33's only legal value for a
    // plain (non-configuration) request in UART's endpoint-type row —
    // caller shall respond with error code UNSUPPORTED_CMD.
    bad_evt      = 11,
};

inline const std::error_category& uart_category() noexcept {
    struct Cat : std::error_category {
        const char* name() const noexcept override { return "rcp.uart"; }
        std::string message(int ev) const override {
            switch (static_cast<UartErrc>(ev)) {
            case UartErrc::read_size_exceeds_bound:
                return "rcp/uart: read_size exceeds kMaxReadSize (the ACF header's own 12-bit width)";
            case UartErrc::rx_fifo_overflow:
                return "rcp/uart: RX FIFO overflow";
            case UartErrc::tx_queue_overflow:
                return "rcp/uart: TX queue overflow";
            case UartErrc::bits_per_frame_out_of_range:
                return "rcp/uart: nr_bits/bits_per_frame out of accepted range";
            case UartErrc::config_write_not_supported:
                return "rcp/uart: evt[2:0]=111b configuration-write requests are not routable through "
                       "UartEndpoint::handle_request — call apply_reconfig() directly";
            case UartErrc::short_frame:  return "rcp/uart: frame too short";
            case UartErrc::bad_msg_type: return "rcp/uart: unexpected ACF message type";
            case UartErrc::wrong_bus:    return "rcp/uart: wrong byte_bus_id";
            case UartErrc::wrong_op:     return "rcp/uart: wrong ACF op";
            case UartErrc::unknown_cmd:  return "rcp/uart: unrecognized command (payload-bearing read request)";
            case UartErrc::bad_evt:      return "rcp/uart: evt[2:0] is not 0b000";
            default:
                return "rcp/uart: unknown error";
            }
        }
    };
    static Cat instance;
    return instance;
}

inline std::error_code make_error_code(UartErrc e) noexcept {
    return {static_cast<int>(e), uart_category()};
}

// wire_error maps e to its numbered wire error code (acf::WireErrorCode), for
// a caller building an Error Response frame once a request has failed to
// decode — std::nullopt for every UartErrc value with no numbered
// counterpart. Matches rcp/gpio.hpp's identical wire_error() convenience.
inline std::optional<acf::WireErrorCode> wire_error(UartErrc e) noexcept {
    switch (e) {
    case UartErrc::bad_evt:
    case UartErrc::unknown_cmd:
        return acf::WireErrorCode::UnsupportedCmd;
    default:
        return std::nullopt;
    }
}

// ── Word format: data bits, parity, stop bits ──────────────────────────────────
// UART frames narrower than a full octet are still carried one-per-byte on
// the wire; the frame's data occupies the low `nr_bits` bits of that byte,
// with the remaining high bits zero-padded (this implementation's own
// explicit packing convention, ported from c-RCP's rcp_ep_uart_apply_bit_
// padding()/rcp_ep_uart_bit_pad_mask()).

constexpr uint8_t kNrBitsMin = 1; // RCP_EP_UART_NR_BITS_MIN
constexpr uint8_t kNrBitsMax = 8; // RCP_EP_UART_NR_BITS_MAX

// nr_bits_valid: true iff nr_bits is in [kNrBitsMin, kNrBitsMax] (1..8)
// inclusive — WIDENED from this header's own pre-rewrite [5,8]-only range;
// see the file header.
constexpr bool nr_bits_valid(uint8_t nr_bits) noexcept {
    return nr_bits >= kNrBitsMin && nr_bits <= kNrBitsMax;
}

// bit_pad_mask: the mask apply_bit_padding() applies to every payload byte
// for a given nr_bits — (1u << nr_bits) - 1 for nr_bits_valid(nr_bits), e.g.
// 0x7F for nr_bits == 7 and 0xFF for nr_bits == 8. Returns 0 (fail-safe —
// clears every bit) for an nr_bits outside 1..8.
constexpr uint8_t bit_pad_mask(uint8_t nr_bits) noexcept {
    if (!nr_bits_valid(nr_bits)) return 0;
    if (nr_bits == kNrBitsMax) return 0xFFu; // (1u << 8) would overflow uint8_t's own range
    return static_cast<uint8_t>((1u << nr_bits) - 1u);
}

// Applies bit_pad_mask(nr_bits) to every byte of buf[0..len) in place. buf
// may be nullptr iff len == 0. A no-op when nr_bits == 8; every byte is
// zeroed when nr_bits is outside 1..8 (same fail-safe mask as bit_pad_mask()).
inline void apply_bit_padding(uint8_t* buf, size_t len, uint8_t nr_bits) noexcept {
    const uint8_t mask = bit_pad_mask(nr_bits);
    for (size_t i = 0; i < len; ++i) buf[i] = static_cast<uint8_t>(buf[i] & mask);
}

inline void apply_bit_padding(std::vector<uint8_t>& buf, uint8_t nr_bits) noexcept {
    apply_bit_padding(buf.data(), buf.size(), nr_bits);
}

// pack_frame_to_octet / unpack_frame_bits: this header's own pre-rewrite
// single-octet convenience wrappers, kept and rebuilt on bit_pad_mask() above
// (their accepted range is now [kNrBitsMin, kNrBitsMax] == [1,8], widened
// from the pre-rewrite [5,8] — see the file header).
inline std::error_code pack_frame_to_octet(uint8_t value, uint8_t bits_per_frame, uint8_t& out) noexcept {
    if (!nr_bits_valid(bits_per_frame)) return make_error_code(UartErrc::bits_per_frame_out_of_range);
    out = static_cast<uint8_t>(value & bit_pad_mask(bits_per_frame));
    return {};
}

inline std::error_code unpack_frame_bits(uint8_t octet, uint8_t bits_per_frame, uint8_t& out) noexcept {
    if (!nr_bits_valid(bits_per_frame)) return make_error_code(UartErrc::bits_per_frame_out_of_range);
    out = static_cast<uint8_t>(octet & bit_pad_mask(bits_per_frame));
    return {};
}

enum class Parity : uint8_t { None = 0, Odd = 1, Even = 2 };

// StopBits — REQ-UART-037/049: the real, three-value enum. ONE_HALF is
// APPENDED (value 2), not inserted between ONE and TWO, so TWO's own
// pre-existing numeric value (1) never moves for any code that stored it as
// a raw integer before this fix — ported verbatim from c-RCP's
// rcp_ep_uart_stop_bits_t (RCP_EP_UART_STOP_BITS_ONE=0/_TWO=1/_ONE_HALF=2).
enum class StopBits : uint8_t {
    One     = 0,
    Two     = 1,
    OneHalf = 2, // 1.5 stop bits — Table 51's own uart_stop_bits register value 3
};

// ── HW trigger signals (§13.7.8.4 Table 52) ─────────────────────────────────
// UartTrigger names Table 52's two output signals ("0: Transmit request
// finalized", "1: Read request finalized") plus a NONE member for "no
// trigger selected" at ordinal 0 (this codebase's own enum convention, e.g.
// gpio::GpioTrigger::None). Because NONE occupies slot 0, the enum's own
// ordinals do NOT directly equal Table 52's signal numbers:
// TxFinalized == 1 (Table 52 signal 0), RxFinalized == 2 (Table 52 signal
// 1) — an off-by-one ported verbatim from c-RCP's own
// rcp_ep_uart_trigger_t, whose header comment documents the identical
// correction (c-RCP-AUDIT-28, issue #449). Nothing below renders this
// field's ordinal onto the wire — cfg.trigger, like SPI's/PWM's own
// per-channel trigger fields, has no Table 51 register of its own; a future
// wire-rendering would need an explicit ordinal -> signal-number mapping
// function, matching the pattern this codebase's own SPI trigger encoding
// already establishes.
enum class UartTrigger : uint8_t {
    None        = 0,
    TxFinalized = 1, // Table 52 signal 0: "Transmit request finalized"
    RxFinalized = 2, // Table 52 signal 1: "Read request finalized"
};

// The two asynchronous events a UART endpoint's trigger mode may be
// evaluated against — see trigger_fires() below.
enum class UartEvent : uint8_t {
    TxRequestFinalized   = 0,
    ReadRequestFinalized = 1,
};

// trigger_fires: true iff event satisfies trigger — never for None; for
// TxFinalized iff event == TxRequestFinalized; for RxFinalized iff event ==
// ReadRequestFinalized — TC18 §13.7.8.4 Table 52's own two HW trigger
// signals, verbatim.
constexpr bool trigger_fires(UartTrigger trigger, UartEvent event) noexcept {
    switch (trigger) {
    case UartTrigger::TxFinalized: return event == UartEvent::TxRequestFinalized;
    case UartTrigger::RxFinalized: return event == UartEvent::ReadRequestFinalized;
    case UartTrigger::None:
    default:                       return false;
    }
}

// ── Functional config (§13.7.8.2 Table 51) ────────────────────────────────────
// The common EP_func prefix every endpoint type's own Table shares is
// modeled directly as members here, same as i2c.hpp's I2cFunctionalCfg and
// gpio.hpp's GpioFunctionalConfig (rcp/regmap.hpp's EndpointFunctionalConfig
// is still an opaque byte blob pending its own structural port).
//
// Three fields render/parse through a genuinely different representation
// than this endpoint's own pre-existing, differently-scoped fields of a
// similar name — kept as new, separate members rather than reinterpreting
// existing ones, the same "don't silently redefine an existing public
// field's meaning" caution SPI's own baud_rate_kbps-vs-clock_divider split
// already established (REQ-UART-048):
//   - baud_rate_kbps (16 bit, kbit/s) is the wire's own uart_baud_rate
//     register; baud_rate (uint32_t, unit unspecified) is a separate,
//     pre-existing field, not derived from or written to by the register
//     block.
//   - wire_timeout_bit_times (8 bit) is the wire's own uart_timeout register
//     — a receiver idle-timeout in UART bit periods, distinct from
//     uart_timeout_ms (a wall-clock read-completion race timeout at a
//     different layer entirely — see UartEndpoint::handle_read() below).
//   - stop_bits round-trips through uart_stop_bits's half-stop-bit units
//     exactly (REQ-UART-049): render emits 2/3/4 for One/OneHalf/Two; parse
//     maps register value 2 to One, 3 to OneHalf, anything >= 4 (or < 2) to
//     Two/One respectively — see detail::stop_bits_to_half_units()/
//     detail::half_units_to_stop_bits() below.

struct UartFunctionalCfg {
    bool ep_enable             = false;
    bool ep_clear_req_storage  = false;
    bool ep_req_crc_enable     = false;
    bool ep_response_ts_enable = false;
    bool ep_suppress_response  = false;

    uint32_t baud_rate     = 0;           // unit unspecified — this module's own pre-existing field
    uint8_t  uart_nr_bits  = kNrBitsMax;  // 1..8; the only sane power-on default (0 is not nr_bits_valid())
    uint8_t  parity        = static_cast<uint8_t>(Parity::None);
    uint8_t  stop_bits     = static_cast<uint8_t>(StopBits::One);
    uint16_t ep_rx_buffer_size = 0; // RX FIFO size, octets
    uint32_t uart_timeout_ms   = 0; // read-completion race timeout — see UartEndpoint::handle_read()

    uint16_t ep_status              = 0;     // uart_ep_status, Table 51
    uint16_t baud_rate_kbps          = 0;     // uart_baud_rate, Table 51 — kbit/s
    bool     rts_enable              = false; // uart_rts_enable, Table 51
    bool     cts_enable              = false; // uart_cts_enable, Table 51
    bool     half_duplex             = false; // uart_half_duplex, Table 51
    uint8_t  wire_timeout_bit_times = 0;     // uart_timeout, Table 51 — bit times
    uint8_t  trail                   = 0;     // uart_trail, Table 51 — bit times

    UartTrigger trigger = UartTrigger::None; // this module's own field, not part of the EP_func block
};

inline void functional_cfg_init(UartFunctionalCfg& cfg) noexcept { cfg = UartFunctionalCfg{}; }

// functional_cfg_writable is a thin, named wrapper over rcp/lifecycle.hpp's
// field_writable() (FieldKind::FunctionalW) — reuses, never duplicates, that
// function's authorization logic.
inline bool functional_cfg_writable(lifecycle::ServerState state, lifecycle::WriterCtx writer) noexcept {
    return lifecycle::field_writable(state, lifecycle::FieldKind::FunctionalW, writer);
}

inline bool set_baud_rate(UartFunctionalCfg& cfg, uint32_t baud_rate, lifecycle::ServerState state,
                            lifecycle::WriterCtx writer) noexcept {
    if (!functional_cfg_writable(state, writer)) return false;
    cfg.baud_rate = baud_rate;
    return true;
}

// set_frame_format sets cfg.uart_nr_bits/parity/stop_bits together (one
// setter for all three, since they are always reconfigured as a pack on the
// wire) iff nr_bits is nr_bits_valid() and functional_cfg_writable()
// authorizes the write. cfg is left entirely unchanged when it returns false.
inline bool set_frame_format(UartFunctionalCfg& cfg, uint8_t nr_bits, Parity parity, StopBits stop_bits,
                               lifecycle::ServerState state, lifecycle::WriterCtx writer) noexcept {
    if (!nr_bits_valid(nr_bits)) return false;
    if (!functional_cfg_writable(state, writer)) return false;
    cfg.uart_nr_bits = nr_bits;
    cfg.parity        = static_cast<uint8_t>(parity);
    cfg.stop_bits      = static_cast<uint8_t>(stop_bits);
    return true;
}

inline bool set_rx_buffer_size(UartFunctionalCfg& cfg, uint16_t rx_buffer_size, lifecycle::ServerState state,
                                 lifecycle::WriterCtx writer) noexcept {
    if (!functional_cfg_writable(state, writer)) return false;
    cfg.ep_rx_buffer_size = rx_buffer_size;
    return true;
}

inline bool set_timeout(UartFunctionalCfg& cfg, uint32_t timeout_ms, lifecycle::ServerState state,
                          lifecycle::WriterCtx writer) noexcept {
    if (!functional_cfg_writable(state, writer)) return false;
    cfg.uart_timeout_ms = timeout_ms;
    return true;
}

// Same authorization rule as set_baud_rate(), for cfg.trigger — see the
// "HW trigger signals" section above. Never touches the EP_func register
// block (this field has no wire counterpart).
inline bool set_trigger(UartFunctionalCfg& cfg, UartTrigger trigger, lifecycle::ServerState state,
                          lifecycle::WriterCtx writer) noexcept {
    if (!functional_cfg_writable(state, writer)) return false;
    cfg.trigger = trigger;
    return true;
}

// ── The EP_func register block (evt[2:0] == 111b, §13.7.8.2 Table 51) ────────
// Table 51's own layout (unlike GPIO's/I2C's own source tables, this one has
// no address-collision editorial defect — its printed addresses are
// internally consistent throughout, per c-RCP's own file header).

constexpr uint16_t kRegEpLen       = 0x0000; //  8 bit, R
constexpr uint16_t kRegReserved01  = 0x0001; //  8 bit, R
constexpr uint16_t kRegEpEnableClr = 0x0002; //  8 bit, R/W
constexpr uint16_t kRegEpOptions   = 0x0003; //  8 bit, R/W
constexpr uint16_t kRegEpStatus    = 0x0004; // 16 bit, R/W
constexpr uint16_t kRegBaudRate    = 0x0006; // 16 bit, R/W — kbit/s
constexpr uint16_t kRegNrBits      = 0x0008; //  8 bit, R/W — number of data bits
constexpr uint16_t kRegFlags       = 0x0009; //  8 bit, R/W — parity_enable(0)/parity_pol(1)/
                                              //  rts_enable(2)/cts_enable(3)/half_duplex(4)
constexpr uint16_t kRegStopBits    = 0x000A; //  8 bit, R/W — half stop bits
constexpr uint16_t kRegTimeout     = 0x000B; //  8 bit, R/W — receiver timeout, bit times
constexpr uint16_t kRegTrail       = 0x000C; //  8 bit, R/W — inter-transmission trail time, bit times

// Bit masks within kRegFlags.
constexpr uint8_t kFlagParityEnable = 1u << 0;
constexpr uint8_t kFlagParityPol    = 1u << 1;
constexpr uint8_t kFlagRtsEnable    = 1u << 2;
constexpr uint8_t kFlagCtsEnable    = 1u << 3;
constexpr uint8_t kFlagHalfDuplex   = 1u << 4;

// The block's own length in octets — one past the last assigned offset, i.e.
// the value reported at kRegEpLen and the bound the "write beyond EP_LEN is
// ignored" rule (§12.7.1) is applied against.
constexpr size_t kEpFuncLen = 0x000D;

using EpFuncBlock = std::array<uint8_t, kEpFuncLen>;

constexpr size_t kReconfigAddrLen = 2;

namespace detail {
constexpr uint8_t kEnableClrBitEnable = 1u << 0;
constexpr uint8_t kEnableClrBitClear  = 1u << 4;
constexpr uint8_t kOptionsBitReqCrc   = 1u << 0;
constexpr uint8_t kOptionsBitRespTs   = 1u << 3;
constexpr uint8_t kOptionsBitSuppress = 1u << 7;

// uart_stop_bits's own half-stop-bit units <-> StopBits — REQ-UART-049; see
// the "Functional config" section above for the exact three-way mapping.
constexpr uint8_t stop_bits_to_half_units(uint8_t stop_bits) noexcept {
    if (stop_bits == static_cast<uint8_t>(StopBits::Two)) return 4u;
    if (stop_bits == static_cast<uint8_t>(StopBits::OneHalf)) return 3u;
    return 2u; // StopBits::One, and any other/invalid value
}

constexpr uint8_t half_units_to_stop_bits(uint8_t half_units) noexcept {
    if (half_units == 3u) return static_cast<uint8_t>(StopBits::OneHalf);
    return half_units >= 4u ? static_cast<uint8_t>(StopBits::Two) : static_cast<uint8_t>(StopBits::One);
}

constexpr bool reg_offset_read_only(uint16_t addr) noexcept {
    return addr == kRegEpLen || addr == kRegReserved01;
}
} // namespace detail

// render_registers serializes cfg's EP_func registers into out exactly as a
// configuration *read* of the whole block would report them — the inverse of
// apply_reconfig()'s own parse step. parity_enable/parity_pol are derived
// from cfg.parity; uart_stop_bits is derived from cfg.stop_bits via the
// half-stop-bit mapping documented above.
inline void render_registers(const UartFunctionalCfg& cfg, EpFuncBlock& out) noexcept {
    uint8_t enable_clr = 0;
    uint8_t options    = 0;
    uint8_t flags      = 0;

    if (cfg.ep_enable) enable_clr |= detail::kEnableClrBitEnable;
    if (cfg.ep_clear_req_storage) enable_clr |= detail::kEnableClrBitClear;
    if (cfg.ep_req_crc_enable) options |= detail::kOptionsBitReqCrc;
    if (cfg.ep_response_ts_enable) options |= detail::kOptionsBitRespTs;
    if (cfg.ep_suppress_response) options |= detail::kOptionsBitSuppress;

    if (cfg.parity != static_cast<uint8_t>(Parity::None)) flags |= kFlagParityEnable;
    if (cfg.parity == static_cast<uint8_t>(Parity::Even)) flags |= kFlagParityPol;
    if (cfg.rts_enable) flags |= kFlagRtsEnable;
    if (cfg.cts_enable) flags |= kFlagCtsEnable;
    if (cfg.half_duplex) flags |= kFlagHalfDuplex;

    out[kRegEpLen]       = static_cast<uint8_t>(kEpFuncLen);
    out[kRegReserved01]  = 0;
    out[kRegEpEnableClr] = enable_clr;
    out[kRegEpOptions]   = options;
    avtp::detail::put_u16(&out[kRegEpStatus], cfg.ep_status);
    avtp::detail::put_u16(&out[kRegBaudRate], cfg.baud_rate_kbps);
    out[kRegNrBits]   = cfg.uart_nr_bits;
    out[kRegFlags]    = flags;
    out[kRegStopBits] = detail::stop_bits_to_half_units(cfg.stop_bits);
    out[kRegTimeout]  = cfg.wire_timeout_bit_times;
    out[kRegTrail]    = cfg.trail;
}

namespace detail {
inline void parse_registers(UartFunctionalCfg& cfg, const EpFuncBlock& in) noexcept {
    const uint8_t flags         = in[kRegFlags];
    const bool    parity_enable = (flags & kFlagParityEnable) != 0;
    const bool    parity_pol    = (flags & kFlagParityPol) != 0;
    const uint8_t enable_clr    = in[kRegEpEnableClr];
    const uint8_t options       = in[kRegEpOptions];

    cfg.ep_enable             = (enable_clr & kEnableClrBitEnable) != 0;
    cfg.ep_clear_req_storage  = (enable_clr & kEnableClrBitClear) != 0;
    cfg.ep_req_crc_enable     = (options & kOptionsBitReqCrc) != 0;
    cfg.ep_response_ts_enable = (options & kOptionsBitRespTs) != 0;
    cfg.ep_suppress_response  = (options & kOptionsBitSuppress) != 0;

    cfg.ep_status      = avtp::detail::get_u16(&in[kRegEpStatus]);
    cfg.baud_rate_kbps = avtp::detail::get_u16(&in[kRegBaudRate]);
    cfg.uart_nr_bits    = in[kRegNrBits];

    if (!parity_enable) {
        cfg.parity = static_cast<uint8_t>(Parity::None);
    } else {
        cfg.parity = parity_pol ? static_cast<uint8_t>(Parity::Even) : static_cast<uint8_t>(Parity::Odd);
    }
    cfg.rts_enable  = (flags & kFlagRtsEnable) != 0;
    cfg.cts_enable  = (flags & kFlagCtsEnable) != 0;
    cfg.half_duplex = (flags & kFlagHalfDuplex) != 0;

    cfg.stop_bits              = half_units_to_stop_bits(in[kRegStopBits]);
    cfg.wire_timeout_bit_times = in[kRegTimeout];
    cfg.trail                  = in[kRegTrail];
}
} // namespace detail

enum class UartReconfigErrc : int {
    short_payload = 1, // payload carries no address prefix, or one with no data octet after it
    out_of_range  = 2, // start_address + data length exceeds kEpFuncLen — the whole write is ignored
};

inline const std::error_category& uart_reconfig_category() noexcept {
    struct Cat : std::error_category {
        const char* name() const noexcept override { return "rcp.uart.reconfig"; }
        std::string message(int ev) const override {
            switch (static_cast<UartReconfigErrc>(ev)) {
            case UartReconfigErrc::short_payload:
                return "rcp/uart: configuration write has no address and data";
            case UartReconfigErrc::out_of_range:
                return "rcp/uart: configuration write extends past the EP_func block";
            default: return "rcp/uart: unknown configuration-write error";
            }
        }
    };
    static Cat instance;
    return instance;
}

inline std::error_code make_error_code(UartReconfigErrc e) noexcept {
    return {static_cast<int>(e), uart_reconfig_category()};
}

// apply_reconfig applies the configuration escape hatch (evt[2:0] == 111b):
// payload is a 16-bit big-endian relative start address followed by the
// configuration data octets to write from that address onward (§12.7.1).
// Patches the block's current image at octet granularity, then adopts it
// wholesale; octets landing on a read-only register (EP_LEN, the reserved
// octet) are silently skipped while the rest of the span is still applied. A
// write whose start_address+length exceeds kEpFuncLen is rejected wholesale
// and cfg left entirely unchanged, per §12.7.1's own "such a payload is to
// be ignored" rule.
inline std::error_code apply_reconfig(UartFunctionalCfg& cfg, const uint8_t* payload,
                                        size_t payload_len) {
    if (payload_len <= kReconfigAddrLen) return make_error_code(UartReconfigErrc::short_payload);

    const uint16_t start_address = avtp::detail::get_u16(payload);
    const size_t   data_len      = payload_len - kReconfigAddrLen;

    if (static_cast<size_t>(start_address) + data_len > kEpFuncLen)
        return make_error_code(UartReconfigErrc::out_of_range);

    EpFuncBlock block{};
    render_registers(cfg, block);
    for (size_t i = 0; i < data_len; ++i) {
        const auto addr = static_cast<uint16_t>(start_address + i);
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
    avtp::detail::put_u16(payload.data(), start_address);
    std::copy(data.begin(), data.end(), payload.begin() + static_cast<long>(kReconfigAddrLen));

    acf::AcfMessageInfo hdr;
    hdr.byte_bus_id     = byte_bus_id;
    hdr.op               = true; // write — §12.7.1: the write request's payload is written into EP_func
    hdr.evt_op            = 0x7; // evt[2:0] = 111b, the reconfiguration escape hatch
    hdr.transaction_num  = transaction_num;
    return acf::encode_acf_abb(hdr, payload);
}

// ── TX: write request/response ────────────────────────────────────────────── ──
// evt is always 0 for every request/response this module produces or
// consumes — this endpoint type, like I2C, has no channel selector.

// encode_write_request encodes an ACF_ABB write (TX) request addressed to
// byte_bus_id: payload is exactly tx_data, the raw bytes to transmit
// (already bit-padded by the caller if applicable — see apply_bit_padding()).
inline std::vector<uint8_t> encode_write_request(avtp::ByteBusId byte_bus_id,
                                                    const std::vector<uint8_t>& tx_data,
                                                    uint8_t transaction_num) {
    acf::AcfMessageInfo hdr;
    hdr.byte_bus_id     = byte_bus_id;
    hdr.op               = true; // write
    hdr.evt_op            = 0;    // no channel selector — see the file header
    hdr.transaction_num  = transaction_num;
    return acf::encode_acf_abb(hdr, tx_data);
}

// decode_write_request decodes and validates an ACF-level UART write
// request. Fails with UartErrc::short_frame/bad_msg_type/wrong_bus/wrong_op;
// UartErrc::bad_evt if evt[2:0] is not 0b000 (acf::evt_row2_is_plain(), TC18
// §13.5 Table 33 — caller shall respond with error code UNSUPPORTED_CMD). On
// success, out_tx_data/out_transaction_num are populated.
inline std::error_code decode_write_request(const uint8_t* b, size_t len, avtp::ByteBusId expected_bus_id,
                                              std::vector<uint8_t>& out_tx_data,
                                              uint8_t& out_transaction_num) {
    acf::AcfMessageInfo  hdr;
    std::vector<uint8_t> payload;
    const auto ec = acf::decode_acf_abb(b, len, hdr, payload);
    if (ec == avtp::make_error_code(avtp::AvtpErrc::short_buffer)) return make_error_code(UartErrc::short_frame);
    if (ec) return make_error_code(UartErrc::bad_msg_type);

    if (hdr.byte_bus_id != expected_bus_id) return make_error_code(UartErrc::wrong_bus);
    if (!hdr.op) return make_error_code(UartErrc::wrong_op); // op=false means read
    if (!acf::evt_row2_is_plain(hdr.evt_op)) return make_error_code(UartErrc::bad_evt);

    out_tx_data          = std::move(payload);
    out_transaction_num  = hdr.transaction_num;
    return {};
}

// encode_write_response encodes a write (TX) response carrying accepted_data
// (the prefix of the original request's tx bytes this endpoint actually
// accepted into its TX path; all of them, in the ordinary case) as its
// payload, echoing transaction_num. Encoded as ACF_ABB when timed is false;
// as ACF_GBB (mtv valid, message_timestamp = timestamp) when timed is true.
inline std::vector<uint8_t> encode_write_response(avtp::ByteBusId byte_bus_id,
                                                     const std::vector<uint8_t>& accepted_data,
                                                     uint8_t transaction_num, bool timed,
                                                     uint64_t timestamp) {
    acf::AcfMessageInfo hdr;
    hdr.byte_bus_id     = byte_bus_id;
    hdr.op               = true; // write
    hdr.rsp               = true;
    hdr.evt_op            = 0;
    hdr.transaction_num  = transaction_num;

    if (timed) {
        hdr.mtv = true;
        return acf::encode_acf_gbb(hdr, timestamp, accepted_data);
    }
    return acf::encode_acf_abb(hdr, accepted_data);
}

// decode_write_response decodes a write (TX) response from either an
// ACF_ABB or ACF_GBB message (peeks the ACF message type, since a
// response's encoding depends on the responding endpoint's own
// timed/untimed choice). Fails with UartErrc::short_frame/bad_msg_type/
// wrong_bus.
inline std::error_code decode_write_response(const uint8_t* b, size_t len, avtp::ByteBusId expected_bus_id,
                                               std::vector<uint8_t>& out_accepted_data, bool& out_timed,
                                               uint64_t& out_timestamp, uint8_t& out_transaction_num) {
    uint8_t msg_type = 0;
    if (acf::peek_msg_type(b, len, msg_type)) return make_error_code(UartErrc::short_frame);

    acf::AcfMessageInfo  hdr;
    std::vector<uint8_t> payload;
    avtp::ByteBusId       bus_id = 0;
    uint8_t                txn    = 0;
    bool                    timed  = false;
    uint64_t                ts     = 0;

    if (msg_type == acf::kAcfMsgTypeGbb) {
        const auto ec = acf::decode_acf_gbb(b, len, hdr, ts, payload);
        if (ec == avtp::make_error_code(avtp::AvtpErrc::short_buffer)) return make_error_code(UartErrc::short_frame);
        if (ec) return make_error_code(UartErrc::bad_msg_type);
        bus_id = hdr.byte_bus_id;
        txn    = hdr.transaction_num;
        timed  = hdr.mtv;
    } else {
        const auto ec = acf::decode_acf_abb(b, len, hdr, payload);
        if (ec == avtp::make_error_code(avtp::AvtpErrc::short_buffer)) return make_error_code(UartErrc::short_frame);
        if (ec) return make_error_code(UartErrc::bad_msg_type);
        bus_id = hdr.byte_bus_id;
        txn    = hdr.transaction_num;
    }

    if (bus_id != expected_bus_id) return make_error_code(UartErrc::wrong_bus);

    out_accepted_data     = std::move(payload);
    out_timed              = timed;
    out_timestamp           = timed ? ts : 0;
    out_transaction_num    = txn;
    return {};
}

// ── RX: read request/response ─────────────────────────────────────────────── ──

// encode_read_request encodes an ACF_ABB read (RX) request addressed to
// byte_bus_id, with no payload: read_size rides the ACF header's own
// read_size_or_segment_num field. REQ-UART-034: read_size is the header's
// full 12-bit width (0-4095); this function does not itself validate the
// range (matching every other endpoint's own read_size parameter).
inline std::vector<uint8_t> encode_read_request(avtp::ByteBusId byte_bus_id, uint16_t read_size,
                                                   uint8_t transaction_num) {
    acf::AcfMessageInfo hdr;
    hdr.byte_bus_id               = byte_bus_id;
    hdr.op                        = false; // read
    hdr.evt_op                     = 0;     // no channel selector — see the file header
    hdr.transaction_num           = transaction_num;
    hdr.read_size_or_segment_num  = read_size;
    return acf::encode_acf_abb(hdr, {});
}

// decode_read_request decodes and validates an ACF-level UART read request.
// Fails with UartErrc::short_frame/bad_msg_type/wrong_bus/wrong_op;
// UartErrc::bad_evt if evt[2:0] is not 0b000; UartErrc::unknown_cmd if it
// carries any payload at all — the deliberate asymmetry documented in the
// file header. On success, out_read_size/out_transaction_num are populated.
inline std::error_code decode_read_request(const uint8_t* b, size_t len, avtp::ByteBusId expected_bus_id,
                                             uint16_t& out_read_size, uint8_t& out_transaction_num) {
    acf::AcfMessageInfo  hdr;
    std::vector<uint8_t> payload;
    const auto ec = acf::decode_acf_abb(b, len, hdr, payload);
    if (ec == avtp::make_error_code(avtp::AvtpErrc::short_buffer)) return make_error_code(UartErrc::short_frame);
    if (ec) return make_error_code(UartErrc::bad_msg_type);

    if (hdr.byte_bus_id != expected_bus_id) return make_error_code(UartErrc::wrong_bus);
    if (hdr.op) return make_error_code(UartErrc::wrong_op); // op=true means write
    if (!acf::evt_row2_is_plain(hdr.evt_op)) return make_error_code(UartErrc::bad_evt);
    if (!payload.empty()) return make_error_code(UartErrc::unknown_cmd);

    out_read_size        = hdr.read_size_or_segment_num;
    out_transaction_num  = hdr.transaction_num;
    return {};
}

// encode_read_response encodes a read (RX) response carrying rx_data (the
// bytes actually received — possibly fewer than the requesting read
// request's read_size, i.e. a short read) as its payload, echoing
// transaction_num. Encoded as ACF_ABB when timed is false; as ACF_GBB (mtv
// valid, message_timestamp = timestamp) when timed is true.
inline std::vector<uint8_t> encode_read_response(avtp::ByteBusId byte_bus_id,
                                                    const std::vector<uint8_t>& rx_data,
                                                    uint8_t transaction_num, bool timed, uint64_t timestamp) {
    acf::AcfMessageInfo hdr;
    hdr.byte_bus_id     = byte_bus_id;
    hdr.op               = false; // read
    hdr.rsp               = true;
    hdr.evt_op            = 0;
    hdr.transaction_num  = transaction_num;

    if (timed) {
        hdr.mtv = true;
        return acf::encode_acf_gbb(hdr, timestamp, rx_data);
    }
    return acf::encode_acf_abb(hdr, rx_data);
}

// decode_read_response decodes a read (RX) response from either an ACF_ABB
// or ACF_GBB message (peeked, same reasoning as decode_write_response()).
// Fails with UartErrc::short_frame/bad_msg_type/wrong_bus. payload_len may
// legitimately be shorter than the originating read request's read_size (a
// short read, raced against uart_timeout_ms) — this is treated exactly like
// any other payload length, with no segment_num-based reassembly (this
// codec's own read_size width rarely needs
// decode_read_response_fragment()'s reassembly in practice — see the
// "Fragmented read response" section below).
inline std::error_code decode_read_response(const uint8_t* b, size_t len, avtp::ByteBusId expected_bus_id,
                                              std::vector<uint8_t>& out_rx_data, bool& out_timed,
                                              uint64_t& out_timestamp, uint8_t& out_transaction_num) {
    uint8_t msg_type = 0;
    if (acf::peek_msg_type(b, len, msg_type)) return make_error_code(UartErrc::short_frame);

    acf::AcfMessageInfo  hdr;
    std::vector<uint8_t> payload;
    avtp::ByteBusId       bus_id = 0;
    uint8_t                txn    = 0;
    bool                    timed  = false;
    uint64_t                ts     = 0;

    if (msg_type == acf::kAcfMsgTypeGbb) {
        const auto ec = acf::decode_acf_gbb(b, len, hdr, ts, payload);
        if (ec == avtp::make_error_code(avtp::AvtpErrc::short_buffer)) return make_error_code(UartErrc::short_frame);
        if (ec) return make_error_code(UartErrc::bad_msg_type);
        bus_id = hdr.byte_bus_id;
        txn    = hdr.transaction_num;
        timed  = hdr.mtv;
    } else {
        const auto ec = acf::decode_acf_abb(b, len, hdr, payload);
        if (ec == avtp::make_error_code(avtp::AvtpErrc::short_buffer)) return make_error_code(UartErrc::short_frame);
        if (ec) return make_error_code(UartErrc::bad_msg_type);
        bus_id = hdr.byte_bus_id;
        txn    = hdr.transaction_num;
    }

    if (bus_id != expected_bus_id) return make_error_code(UartErrc::wrong_bus);

    out_rx_data           = std::move(payload);
    out_timed              = timed;
    out_timestamp           = timed ? ts : 0;
    out_transaction_num    = txn;
    return {};
}

// ── Read-completion arbitration (REQ-UART-033) ──────────────────────────────
// TC18 §13.7.8.1's own three read-completion triggers, verbatim: a read
// request completes as soon as the fifo-rx-buffer holds read_size bytes, OR
// when uart_timeout has expired, OR — when read_size is larger than the
// rx_fifo_size — once the fifo is full, in which case the response is
// fragmented. This is a pure, directly-testable function over caller-tracked
// counters; this module still owns no real FIFO or clock.

enum class UartReadCompletion : uint8_t {
    NotYetComplete   = 0, // keep waiting
    RespondNormal    = 1, // emit a normal (possibly short) response now
    RespondFragmented = 2, // emit via encode_read_response_fragmented() now
};

// wire_timeout_us (REQ-UART-037/048) converts Table 51's own uart_timeout
// register (a raw UART bit-time count measured from the last received stop
// bit) into a wall-clock microsecond duration: one bit period is
// 1000/baud_rate_kbps microseconds, so wire_timeout_bit_times bit periods is
// wire_timeout_bit_times*1000/baud_rate_kbps microseconds, rounded UP
// (ceiling) so a caller never underestimates the configured timeout. Fails
// open (returns 0) when baud_rate_kbps == 0 — this library never invents a
// clock rate it has no way to know.
constexpr uint32_t wire_timeout_us(uint16_t baud_rate_kbps, uint8_t wire_timeout_bit_times) noexcept {
    return baud_rate_kbps == 0
               ? 0u
               : (static_cast<uint32_t>(wire_timeout_bit_times) * 1000u + baud_rate_kbps - 1u) / baud_rate_kbps;
}

// read_completion_decision decides which of the three triggers, if any, has
// fired for a read request in progress: bytes_available is the caller-
// tracked count currently held in the fifo-rx-buffer; read_size is the
// request's own requested byte count; elapsed_ms is wall-clock time since
// the request began; uart_timeout_ms/rx_fifo_size are cfg.uart_timeout_ms/
// cfg.ep_rx_buffer_size (passed explicitly, since neither is mutated and a
// caller may be tracking several in-flight reads against one shared cfg).
// The fragmentation trigger is checked first (the more specific condition —
// ordering only matters for documentation clarity, not correctness, since
// the read_size-satisfied trigger can never itself fire when read_size >
// rx_fifo_size). elapsed_ms >= uart_timeout_ms with uart_timeout_ms == 0
// completes immediately (no waiting).
constexpr UartReadCompletion read_completion_decision(uint16_t bytes_available, uint16_t read_size,
                                                         uint32_t elapsed_ms, uint32_t uart_timeout_ms,
                                                         uint16_t rx_fifo_size) noexcept {
    // THIRD trigger: read_size larger than the fifo's own capacity, and the
    // fifo has filled to that capacity — fragmentation is required because a
    // single response can never carry the whole request's worth of data.
    if (read_size > rx_fifo_size && bytes_available >= rx_fifo_size) return UartReadCompletion::RespondFragmented;
    // FIRST trigger: the fifo already holds everything the request asked for.
    if (bytes_available >= read_size) return UartReadCompletion::RespondNormal;
    // SECOND trigger: uart_timeout has expired — whatever is in the fifo
    // right now (possibly nothing) goes out as a short read.
    if (elapsed_ms >= uart_timeout_ms) return UartReadCompletion::RespondNormal;
    return UartReadCompletion::NotYetComplete;
}

// ── Fragmented read response (Phase 20, rcp/fragment.hpp) ────────────────────
// REQ-UART-029/030/031: wires rcp/fragment.hpp's generic ms/segment_num
// primitive into this endpoint type for the first time — fragment.hpp's own
// header comment lists rcp/uart.hpp among its documented, previously-unwired
// callers; this pass closes that gap, matching rcp/can.hpp's own
// frame_response_fragment_count()/encode_frame_response_fragmented() shape.

// The number of ACF frames encode_read_response_fragmented() would produce
// for rx_len octets of read-response payload split into fragments of at most
// max_fragment_payload octets each.
inline size_t read_response_fragment_count(size_t rx_len, size_t max_fragment_payload) noexcept {
    return fragment::plan_count(rx_len, max_fragment_payload);
}

// Encodes a UART read (RX) response as one or more ACF frames, fragmenting
// via rcp/fragment.hpp's ms/segment_num mechanism whenever rx_len exceeds
// max_fragment_payload octets. Every fragment shares byte_bus_id/op(read)/
// transaction_num/timed/timestamp with encode_read_response(); only the ms
// flag, read_size_or_segment_num (meaningful only on an ms=true fragment),
// and each fragment's own payload slice differ. When rx_len already fits in
// one fragment, this produces exactly one frame identical to what
// encode_read_response() itself would have. Returns an empty vector under
// the same conditions read_response_fragment_count() returns 0 for.
inline std::vector<std::vector<uint8_t>>
encode_read_response_fragmented(avtp::ByteBusId byte_bus_id, const std::vector<uint8_t>& rx_data,
                                  uint8_t transaction_num, bool timed, uint64_t timestamp,
                                  size_t max_fragment_payload) {
    const size_t count = read_response_fragment_count(rx_data.size(), max_fragment_payload);
    if (count == 0) return {};

    std::vector<fragment::Segment> segs(count);
    if (fragment::plan(rx_data.size(), max_fragment_payload, segs.data(), count)) return {};

    std::vector<std::vector<uint8_t>> out_frames;
    out_frames.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        std::vector<uint8_t> slice(rx_data.begin() + static_cast<long>(segs[i].offset),
                                    rx_data.begin() + static_cast<long>(segs[i].offset + segs[i].len));

        acf::AcfMessageInfo hdr;
        hdr.byte_bus_id              = byte_bus_id;
        hdr.op                        = false; // read
        hdr.rsp                        = true;
        hdr.evt_op                     = 0;
        hdr.transaction_num           = transaction_num;
        hdr.ms                         = segs[i].ms;
        hdr.read_size_or_segment_num  = segs[i].ms ? segs[i].segment_num : uint16_t{0};

        if (timed) {
            hdr.mtv = true;
            out_frames.push_back(acf::encode_acf_gbb(hdr, timestamp, slice));
        } else {
            out_frames.push_back(acf::encode_acf_abb(hdr, slice));
        }
    }
    return out_frames;
}

// Decodes one fragment of a (possibly multi-fragment) UART read response
// from b[0..len) — the same peek-message-type/byte_bus_id validation
// decode_read_response() applies, but surfaces the fragment's own ms bit and
// read_size_or_segment_num (as *out_segment_num, meaningful only when
// *out_ms) alongside the raw payload slice, for a caller to feed straight
// into a rcp::fragment::Reassembler. Once reassembly reports
// ReasmResult::kComplete, the Reassembler's own data()/size() *is* the fully
// reassembled rx_data directly — unlike CAN's fragmented response, this
// endpoint's payload has no further internal structure of its own to parse.
// Fails with the same UartErrc::short_frame/bad_msg_type/wrong_bus
// conditions decode_read_response() does.
inline std::error_code decode_read_response_fragment(const uint8_t* b, size_t len,
                                                        avtp::ByteBusId expected_bus_id, bool& out_ms,
                                                        uint16_t& out_segment_num,
                                                        std::vector<uint8_t>& out_payload, bool& out_timed,
                                                        uint64_t& out_timestamp,
                                                        uint8_t& out_transaction_num) {
    uint8_t msg_type = 0;
    if (acf::peek_msg_type(b, len, msg_type)) return make_error_code(UartErrc::short_frame);

    acf::AcfMessageInfo  hdr;
    std::vector<uint8_t> payload;
    avtp::ByteBusId       bus_id = 0;
    uint8_t                txn    = 0;
    bool                    timed  = false;
    uint64_t                ts     = 0;

    if (msg_type == acf::kAcfMsgTypeGbb) {
        const auto ec = acf::decode_acf_gbb(b, len, hdr, ts, payload);
        if (ec == avtp::make_error_code(avtp::AvtpErrc::short_buffer)) return make_error_code(UartErrc::short_frame);
        if (ec) return make_error_code(UartErrc::bad_msg_type);
        bus_id = hdr.byte_bus_id;
        txn    = hdr.transaction_num;
        timed  = hdr.mtv;
    } else {
        const auto ec = acf::decode_acf_abb(b, len, hdr, payload);
        if (ec == avtp::make_error_code(avtp::AvtpErrc::short_buffer)) return make_error_code(UartErrc::short_frame);
        if (ec) return make_error_code(UartErrc::bad_msg_type);
        bus_id = hdr.byte_bus_id;
        txn    = hdr.transaction_num;
    }

    if (bus_id != expected_bus_id) return make_error_code(UartErrc::wrong_bus);

    out_ms                = hdr.ms;
    out_segment_num        = hdr.read_size_or_segment_num;
    out_payload            = std::move(payload);
    out_timed               = timed;
    out_timestamp            = timed ? ts : 0;
    out_transaction_num     = txn;
    return {};
}

// ── UartEndpoint ──────────────────────────────────────────────────────────────
// This header's own pre-rewrite TX/RX-queue convenience class — no c-RCP
// equivalent (c-RCP is a pure free-function codec with no persistent
// per-endpoint "queue" object) — kept and re-verified unchanged, per the file
// header. TX and RX are modeled as independent queues: writes enqueue onto
// the TX queue via enqueue_tx and are later drained by the caller's own
// transport/driver layer via drain_tx; bytes arriving from the wire are
// pushed into the RX FIFO via rx_fill (also driven by that same external
// layer — this class does not itself own a UART transceiver) and consumed by
// read requests via handle_read/handle_pure_read.
class UartEndpoint {
public:
    // enqueue_tx appends to the independent TX queue, bounded by
    // kTxQueueCapacity.
    std::error_code enqueue_tx(const std::vector<uint8_t>& bytes) {
        if (tx_queue_.size() + bytes.size() > kTxQueueCapacity)
            return make_error_code(UartErrc::tx_queue_overflow);
        tx_queue_.insert(tx_queue_.end(), bytes.begin(), bytes.end());
        return {};
    }

    // drain_tx returns and clears everything queued for transmission.
    std::vector<uint8_t> drain_tx() {
        std::vector<uint8_t> out(tx_queue_.begin(), tx_queue_.end());
        tx_queue_.clear();
        return out;
    }

    // rx_fill pushes bytes that arrived from the wire into the RX FIFO,
    // bounded by kRxFifoCapacity.
    std::error_code rx_fill(const std::vector<uint8_t>& bytes) {
        if (rx_fifo_.size() + bytes.size() > kRxFifoCapacity)
            return make_error_code(UartErrc::rx_fifo_overflow);
        rx_fifo_.insert(rx_fifo_.end(), bytes.begin(), bytes.end());
        return {};
    }

    size_t rx_available() const noexcept { return rx_fifo_.size(); }

    // handle_read is this convenience class's own simplified read-completion
    // rule: it drains up to `read_size` bytes currently available in the RX
    // FIFO, then reports whether `read_size` was fully reached or whether
    // `elapsed_ms >= uart_timeout_ms` — the caller supplies elapsed/timeout
    // explicitly since this class has no clock of its own. Deliberately
    // unchanged by this pass (source/behavior stability with rcp/mock.hpp's
    // existing dispatch_uart() wiring) — a caller wanting the real, full
    // three-way §13.7.8.1 arbitration (fragmentation included) calls
    // read_completion_decision()/encode_read_response_fragmented() directly.
    std::error_code handle_read(uint16_t read_size, uint32_t elapsed_ms, uint32_t uart_timeout_ms,
                                 std::vector<uint8_t>& out_data, bool& out_timed_out) noexcept {
        if (read_size > kMaxReadSize) return make_error_code(UartErrc::read_size_exceeds_bound);
        out_data.clear();
        while (out_data.size() < read_size && !rx_fifo_.empty()) {
            out_data.push_back(rx_fifo_.front());
            rx_fifo_.pop_front();
        }
        const bool reached = out_data.size() >= read_size && read_size > 0;
        out_timed_out = !reached && elapsed_ms >= uart_timeout_ms;
        return {};
    }

    // handle_pure_read is UART's payload-less "pure" read request: no
    // read_size is carried at all, so there is nothing to wait on — it
    // drains and returns whatever is currently buffered in the RX FIFO,
    // non-blocking.
    std::vector<uint8_t> handle_pure_read() {
        std::vector<uint8_t> out(rx_fifo_.begin(), rx_fifo_.end());
        rx_fifo_.clear();
        return out;
    }

    // handle_request is UART's single request-decode entry point, mirroring
    // rcp::i2c::I2cEndpoint::handle_request's shape with one necessary
    // adaptation: every other Row 2 endpoint type funnels its Plain request
    // into a single unified transfer()/transmit()/request_reading() call,
    // but UART's TX and RX are genuinely independent operations reached via
    // two different existing entry points (enqueue_tx, handle_read).
    // Classifies evt[2:0] via rcp::endpoint::evt_row2_kind_of exactly once
    // and routes on `is_write`:
    //   - Plain (evt[2:0] == 000b) + is_write: delegates to
    //     enqueue_tx(tx_bytes). out_data/out_timed_out are left exactly as
    //     the caller passed them: a write request produces no read data.
    //   - Plain (evt[2:0] == 000b) + !is_write: delegates to
    //     handle_read(read_size, elapsed_ms, uart_timeout_ms, out_data,
    //     out_timed_out).
    //   - Reserved (evt[2:0] in 001b-110b): returns
    //     endpoint::EndpointErrc::reserved_evt_row2 without touching either
    //     queue.
    //   - ConfigWrite (evt[2:0] == 111b): §12.7.1's configuration-write
    //     shape targets the UART EP's own functional-config block (Table
    //     51), not a TX/RX operation at all — this convenience call's
    //     out_data/tx_bytes shape cannot carry that payload; returns
    //     UartErrc::config_write_not_supported. The real mechanism
    //     (apply_reconfig()/render_registers()/encode_reconfig_request()
    //     above) is fully implemented; a caller integrating real EP0/regmap
    //     dispatch calls those directly with the request's actual raw
    //     payload.
    std::error_code handle_request(uint8_t evt_op, bool is_write, const std::vector<uint8_t>& tx_bytes,
                                    uint16_t read_size, uint32_t elapsed_ms, uint32_t uart_timeout_ms,
                                    std::vector<uint8_t>& out_data, bool& out_timed_out) {
        switch (endpoint::evt_row2_kind_of(evt_op)) {
        case endpoint::EvtRow2Kind::Plain:
            if (is_write) return enqueue_tx(tx_bytes);
            return handle_read(read_size, elapsed_ms, uart_timeout_ms, out_data, out_timed_out);
        case endpoint::EvtRow2Kind::Reserved:
            return endpoint::make_error_code(endpoint::EndpointErrc::reserved_evt_row2);
        case endpoint::EvtRow2Kind::ConfigWrite:
            return make_error_code(UartErrc::config_write_not_supported);
        }
        return endpoint::make_error_code(endpoint::EndpointErrc::reserved_evt_row2); // unreachable
    }

private:
    std::deque<uint8_t> tx_queue_;
    std::deque<uint8_t> rx_fifo_;
};

} // namespace uart
} // namespace rcp

// Enable std::error_code construction from rcp::uart::UartErrc / UartReconfigErrc.
namespace std {
template <>
struct is_error_code_enum<rcp::uart::UartErrc> : true_type {};
template <>
struct is_error_code_enum<rcp::uart::UartReconfigErrc> : true_type {};
} // namespace std
