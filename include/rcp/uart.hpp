// fusa:req REQ-UART-001
// fusa:req REQ-UART-002
// fusa:req REQ-UART-003
// fusa:req REQ-UART-004
// fusa:req REQ-UART-005
// fusa:req REQ-UART-006
// fusa:req REQ-UART-007

// UART endpoint (ep_type 0x05) — independent TX/RX queues, RX FIFO
// fill/drain semantics, read completion on either a configured `read_size`
// or `uart_timeout` elapsing, payload-less "pure" read requests, and
// sub-octet bit-width padding (extraction §5.8).
//
// ROADMAP.md milestone 48, "Basic Endpoint Types II — I2C, UART, ADC,
// PWM_OUT, PWM_IN (v2.4.0)": UART follows the request-dispatch shape
// rcp/gpio.hpp and rcp/spi.hpp establish, but — unlike either — has no
// evt[2:0]/channel decode of its own; TX and RX are independent queues
// addressed by separate calls (enqueue_tx/handle_read below) rather than by
// a shared selector field.
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
// see rcp/wire.hpp's AcfMessageInfo::read_size_or_segment_num) — a real
// single-AVTPDU budget depends on MTU and other-header overhead this header
// does not model, so kMaxReadSize is deliberately conservative rather than
// computed exactly.
//
// Field names and behavior below implement TC18's *behavior* as described in
// an internal structured extraction of the specification named above; no
// text from that document is reproduced here. The concrete queue-capacity
// values and sub-octet padding convention chosen in this file are this
// implementation's own, same as the equivalent disclaimers in rcp/wire.hpp,
// rcp/regmap.hpp, rcp/endpoint.hpp, rcp/gpio.hpp, and rcp/spi.hpp.
#pragma once

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
