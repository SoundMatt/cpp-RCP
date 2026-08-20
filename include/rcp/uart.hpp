// fusa:req REQ-UART-001
// fusa:req REQ-UART-002
// fusa:req REQ-UART-003
// fusa:req REQ-UART-004
// fusa:req REQ-UART-005
// fusa:req REQ-UART-006
// fusa:req REQ-UART-007
// fusa:req REQ-UART-008
// fusa:req REQ-UART-009

// UART endpoint (ep_type 0x05) — independent TX/RX queues, RX FIFO
// fill/drain semantics, read completion on either a configured `read_size`
// or `uart_timeout` elapsing, payload-less "pure" read requests, and
// sub-octet bit-width padding (extraction §5.8).
//
// ROADMAP.md milestone 48, "Basic Endpoint Types II — I2C, UART, ADC,
// PWM_OUT, PWM_IN (v2.4.0)": UART follows the request-dispatch shape
// rcp/gpio.hpp and rcp/spi.hpp establish, but — unlike either — has no
// evt[2:0]/channel *selector* of its own the way SPI's evt[2:0] picks a
// channel; TX and RX are independent queues addressed by separate calls
// (enqueue_tx/handle_read below) rather than by a shared selector field.
//
// CORRECTION (Table 30/33 Row 2 evt[2:0] validation, sixth endpoint type
// after I2C, ADC, PWM_IN, LIN, and CAN): the "no evt[2:0] decode of its
// own" claim above described only the *absence of a channel/value
// selector*; it did not mean UART is exempt from Table 33's shared
// Plain/Reserved/ConfigWrite classification. Table 33's own text lists
// UART explicitly, by name, in its second row alongside ADC, PWM_IN, I2C,
// LIN, CAN, ISELED, and MDIO (extraction §13.5, TC18.txt L4085-4092) — the
// exact same row every other endpoint type in that list has already had
// this classification wired in for. handle_request below is that wiring
// for UART, following the exact shape rcp/i2c.hpp's I2cEndpoint::
// handle_request, rcp/adc.hpp's AdcEndpoint::handle_request, rcp/pwm.hpp's
// PwmInEndpoint::handle_request, rcp/lin.hpp's LinEndpoint::handle_request,
// and rcp/can.hpp's CanEndpoint::handle_request established, adapted for
// UART's own two-entry-point (TX/RX) shape — see handle_request's own
// comment for why that adaptation is needed and how it stays exactly one
// choke point rather than two independent, unvalidated ones.
//
// ACCEPTED LIMITATION, documented explicitly per the roadmap rather than
// left implicit: ROADMAP.md milestone 52 ("Fragmentation — Go/No-Go
// Decision", v2.8.0) has already made its go/no-go call — fragmentation is
// no-go for this development cycle. Because of that already-final decision,
// this header bounds both the RX FIFO's capacity and the largest
// `read_size` it will accept (kMaxReadSize below) so that a UART read
// response can always be carried in a single, unfragmented AVTPDU. This is
// a conservative implementation-chosen ceiling, not a value derived from
// the wire format's own read_size field width (which is a full uint16_t,
// see rcp/acf.hpp's AcfMessageInfo::read_size_or_segment_num) — a real
// single-AVTPDU budget depends on MTU and other-header overhead this header
// does not model, so kMaxReadSize is deliberately conservative rather than
// computed exactly.
//
// Field names and behavior below implement TC18's *behavior* as described in
// an internal structured extraction of the specification named above; no
// text from that document is reproduced here. The concrete queue-capacity
// values and sub-octet padding convention chosen in this file are this
// implementation's own, same as the equivalent disclaimers in rcp/avtp.hpp,
// rcp/regmap.hpp, rcp/endpoint.hpp, rcp/gpio.hpp, rcp/spi.hpp, rcp/i2c.hpp,
// rcp/adc.hpp, rcp/pwm.hpp, rcp/lin.hpp, and rcp/can.hpp.
#pragma once

#include <rcp/endpoint.hpp>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <system_error>
#include <vector>

namespace rcp {
namespace uart {

// ── Single-AVTPDU response bound (accepted limitation — see header comment) ──

constexpr size_t kMaxReadSize    = 512; // bytes; conservative single-AVTPDU bound
constexpr size_t kRxFifoCapacity = kMaxReadSize;
constexpr size_t kTxQueueCapacity = kMaxReadSize;

// ── Sub-octet bit-width padding ───────────────────────────────────────────────
// UART frames narrower than a full octet (5-8 data bits per frame is this
// implementation's own accepted range) are still carried one-per-byte on
// the wire; the frame's data occupies the low `bits_per_frame` bits of that
// byte, left-justified from bit 0, with the remaining high bits zero-padded
// (this implementation's own explicit packing convention — the extraction
// does not itself dictate which end the padding goes on, so it is called
// out here rather than left to reader inference, matching this repo's
// convention of flagging implementation-chosen encodings elsewhere).

constexpr uint8_t kMinBitsPerFrame = 5;
constexpr uint8_t kMaxBitsPerFrame = 8;

// ── Errors ────────────────────────────────────────────────────────────────────

enum class UartErrc : int {
    read_size_exceeds_bound = 1, // requested read_size > kMaxReadSize
    rx_fifo_overflow        = 2, // rx_fill would push the RX FIFO past kRxFifoCapacity
    tx_queue_overflow        = 3, // enqueue_tx would push the TX queue past kTxQueueCapacity
    bits_per_frame_out_of_range = 4, // bits_per_frame outside [kMinBitsPerFrame, kMaxBitsPerFrame]
    // evt_row2_kind_of classified the request as ConfigWrite (evt[2:0] ==
    // 111b, §12.7.1). This milestone deliberately does not implement the
    // configuration-write shape (relative EP_functional-config start
    // address + configuration data) — see UartEndpoint::handle_request's
    // own comment. Reported explicitly rather than silently accepted as a
    // plain TX/RX operation or silently ignored, same as I2C's, ADC's,
    // PWM_IN's, LIN's, and CAN's own config_write_not_supported variants.
    config_write_not_supported = 5,
};

inline const std::error_category& uart_category() noexcept {
    struct Cat : std::error_category {
        const char* name() const noexcept override { return "rcp.uart"; }
        std::string message(int ev) const override {
            switch (static_cast<UartErrc>(ev)) {
            case UartErrc::read_size_exceeds_bound:
                return "rcp/uart: read_size exceeds the single-AVTPDU accepted bound";
            case UartErrc::rx_fifo_overflow:
                return "rcp/uart: RX FIFO overflow";
            case UartErrc::tx_queue_overflow:
                return "rcp/uart: TX queue overflow";
            case UartErrc::bits_per_frame_out_of_range:
                return "rcp/uart: bits_per_frame out of accepted range";
            case UartErrc::config_write_not_supported:
                return "rcp/uart: evt[2:0]=111b configuration-write requests are not yet implemented";
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

// pack_frame_to_octet / unpack_frame_bits implement the sub-octet padding
// convention documented above.
inline std::error_code pack_frame_to_octet(uint8_t value, uint8_t bits_per_frame, uint8_t& out) noexcept {
    if (bits_per_frame < kMinBitsPerFrame || bits_per_frame > kMaxBitsPerFrame)
        return make_error_code(UartErrc::bits_per_frame_out_of_range);
    const uint8_t mask = static_cast<uint8_t>((1u << bits_per_frame) - 1u);
    out = static_cast<uint8_t>(value & mask);
    return {};
}

inline std::error_code unpack_frame_bits(uint8_t octet, uint8_t bits_per_frame, uint8_t& out) noexcept {
    if (bits_per_frame < kMinBitsPerFrame || bits_per_frame > kMaxBitsPerFrame)
        return make_error_code(UartErrc::bits_per_frame_out_of_range);
    const uint8_t mask = static_cast<uint8_t>((1u << bits_per_frame) - 1u);
    out = static_cast<uint8_t>(octet & mask);
    return {};
}

// ── UartEndpoint ──────────────────────────────────────────────────────────────
// TX and RX are modeled as independent queues (extraction §5.8): writes
// enqueue onto the TX queue via enqueue_tx and are later drained by the
// caller's transport/driver layer via drain_tx; bytes arriving from the
// wire are pushed into the RX FIFO via rx_fill (also driven by that same
// external layer — this header does not itself own a UART transceiver) and
// consumed by read requests via handle_read/handle_pure_read.
class UartEndpoint {
public:
    // enqueue_tx appends to the independent TX queue, bounded by
    // kTxQueueCapacity per the single-AVTPDU accepted limitation.
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
    // bounded by kRxFifoCapacity per the single-AVTPDU accepted limitation
    // (extraction §5.8's RX FIFO fill semantics).
    std::error_code rx_fill(const std::vector<uint8_t>& bytes) {
        if (rx_fifo_.size() + bytes.size() > kRxFifoCapacity)
            return make_error_code(UartErrc::rx_fifo_overflow);
        rx_fifo_.insert(rx_fifo_.end(), bytes.begin(), bytes.end());
        return {};
    }

    size_t rx_available() const noexcept { return rx_fifo_.size(); }

    // handle_read is UART's read-completion rule: it drains up to
    // `read_size` bytes currently available in the RX FIFO (RX FIFO drain
    // semantics), then reports whether `read_size` was fully reached or
    // whether `elapsed_ms >= uart_timeout_ms` — the two conditions the
    // extraction names as completing a read (extraction §5.8). The caller
    // supplies elapsed/timeout explicitly since this header has no clock of
    // its own, matching every other endpoint type in this milestone.
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
    // non-blocking (extraction §5.8).
    std::vector<uint8_t> handle_pure_read() {
        std::vector<uint8_t> out(rx_fifo_.begin(), rx_fifo_.end());
        rx_fifo_.clear();
        return out;
    }

    // handle_request is UART's single request-decode entry point — the
    // piece this header previously had none of, mirroring rcp::i2c::
    // I2cEndpoint::handle_request's shape (this repo's sixth Table 33 Row 2
    // endpoint type after I2C, ADC, PWM_IN, LIN, and CAN) with one
    // necessary adaptation: every other Row 2 endpoint type funnels its
    // Plain request into a single unified transfer()/transmit()/
    // request_reading() call, but UART's TX and RX are genuinely
    // independent operations reached via two different existing entry
    // points (enqueue_tx, handle_read — extraction §13.7.8.1's "these two
    // processes are independent from each other"). Rather than leave two
    // separate, individually-unvalidated request-decode entry points (which
    // would let a Reserved or ConfigWrite evt reach either one directly),
    // handle_request classifies evt[2:0] via rcp::endpoint::
    // evt_row2_kind_of exactly once and then routes on `is_write` (the
    // caller's own req.op — this header has no AcfMessageInfo of its own to
    // read it from, same reason every out-parameter below is passed
    // explicitly rather than pulled from a wire type):
    //   - Plain (evt[2:0] == 000b) + is_write: delegates straight to
    //     enqueue_tx(tx_bytes), unchanged — UART's existing TX-queue model
    //     already IS this row's correct "plain write request" behavior.
    //     out_data/out_timed_out are left exactly as the caller passed
    //     them: a write request produces no read data.
    //   - Plain (evt[2:0] == 000b) + !is_write: delegates straight to
    //     handle_read(read_size, elapsed_ms, uart_timeout_ms, out_data,
    //     out_timed_out), unchanged — UART's existing read-completion rule
    //     already IS this row's correct "plain read request" behavior.
    //   - Reserved (evt[2:0] in 001b-110b): returns
    //     endpoint::EndpointErrc::reserved_evt_row2 without touching either
    //     queue (neither enqueue_tx nor handle_read/rx_fifo_ is invoked) —
    //     TC18 requires this be rejected with error code UNSUPPORTED_CMD.
    //   - ConfigWrite (evt[2:0] == 111b): §12.7.1's configuration-write
    //     shape targets the UART EP's own functional-config block (Table
    //     51 — baud rate, parity, stop bits, uart_timeout itself, ...), not
    //     a TX/RX operation at all. Full handling is deliberately out of
    //     scope for this milestone (nontrivial — it needs
    //     EP_functional-config wiring this header does not yet have, the
    //     same gap I2C's, ADC's, PWM_IN's, LIN's, and CAN's own
    //     handle_request comments defer for the identical reason); this
    //     returns UartErrc::config_write_not_supported rather than
    //     crashing, silently accepting the request as a TX/RX operation,
    //     or silently doing nothing.
    //
    // NOT to be confused with §13.7.8.3's own, entirely separate rules —
    // "A read request having a byte_msg_payload will be rejected with
    // error code = UNKNOWN_CMD" (the payload-less-read-only rule) and the
    // read_size-reached-vs-uart_timeout-elapsed race handle_read already
    // implements. Both operate only once a request has already been
    // classified Plain by the switch above; neither is folded into
    // evt[2:0] decoding here, and reading evt[2:0] as if it also gated or
    // combined with either would be exactly the kind of invented,
    // non-spec-derived encoding this codebase has had to remove elsewhere
    // once discovered (e.g. rcp/iseled.hpp's and rcp/mdio.hpp's own header
    // comments, and rcp/lin.hpp's/rcp/can.hpp's own handle_request comments
    // on the identical class of mistake for their own endpoint types). The
    // payload-less-read-only rule itself remains unimplemented by this
    // header — called out explicitly here rather than silently conflated
    // with Table 33 classification or silently assumed.
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

// Enable std::error_code construction from rcp::uart::UartErrc.
namespace std {
template <>
struct is_error_code_enum<rcp::uart::UartErrc> : true_type {};
} // namespace std
