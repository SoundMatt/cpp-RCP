// fusa:req REQ-E2E-001
// fusa:req REQ-E2E-002
// fusa:req REQ-E2E-003
// fusa:req REQ-E2E-004
// fusa:req REQ-E2E-005
// fusa:req REQ-E2E-006
// fusa:req REQ-E2E-007
// fusa:req REQ-E2E-008

// End-to-end communication protection (ISO 26262 Part 7 E2E profile).
//
// Three layers of defence:
//   1. Sequence counter — per-Controller monotonically incrementing uint32.
//   2. CRC-16/CCITT-FALSE checksum — computed over seq + original payload.
//   3. Replay guard — sliding window rejects previously seen sequence numbers.
//
// Sender: wrap any Controller with e2e::Controller.
// Receiver: call e2e::unwrap() then ReplayGuard::check().
#pragma once

#include "rcp.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <system_error>

namespace rcp {
namespace e2e {

// HeaderLen is the byte overhead prepended by wrap().
// Layout: [0:4] SeqNum uint32 big-endian, [4:6] CRC-16 uint16 big-endian.
constexpr size_t HeaderLen = 6;

// Error codes
enum class E2EErrc : int {
    crc_mismatch = 1,
    short_frame  = 2,
    replay       = 3,
};

inline const std::error_category& e2e_category() noexcept {
    struct Cat : std::error_category {
        const char* name() const noexcept override { return "rcp.e2e"; }
        std::string message(int ev) const override {
            switch (static_cast<E2EErrc>(ev)) {
            case E2EErrc::crc_mismatch: return "e2e: CRC-16 mismatch — payload corrupted";
            case E2EErrc::short_frame:  return "e2e: frame too short for E2E header";
            case E2EErrc::replay:       return "e2e: replayed sequence number detected";
            default:                    return "e2e: unknown error";
            }
        }
    };
    static Cat instance;
    return instance;
}

inline std::error_code make_error_code(E2EErrc e) noexcept {
    return {static_cast<int>(e), e2e_category()};
}

// ── CRC-16/CCITT-FALSE ────────────────────────────────────────────────────────

namespace detail {

inline uint16_t crc16_update(uint16_t crc, uint8_t b) noexcept {
    constexpr uint16_t poly = 0x1021;
    crc ^= static_cast<uint16_t>(b) << 8;
    for (int i = 0; i < 8; ++i) {
        crc = (crc & 0x8000u) ? static_cast<uint16_t>((crc << 1) ^ poly) : static_cast<uint16_t>(crc << 1);
    }
    return crc;
}

inline uint16_t crc16(const uint8_t* prefix, size_t prefix_len,
                       const uint8_t* data,   size_t data_len) noexcept {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < prefix_len; ++i) crc = crc16_update(crc, prefix[i]);
    for (size_t i = 0; i < data_len;   ++i) crc = crc16_update(crc, data[i]);
    return crc;
}

inline void put_u16(uint8_t* p, uint16_t v) noexcept {
    p[0] = static_cast<uint8_t>(v >> 8);
    p[1] = static_cast<uint8_t>(v & 0xFF);
}
inline void put_u32(uint8_t* p, uint32_t v) noexcept {
    p[0] = static_cast<uint8_t>(v >> 24);
    p[1] = static_cast<uint8_t>((v >> 16) & 0xFF);
    p[2] = static_cast<uint8_t>((v >>  8) & 0xFF);
    p[3] = static_cast<uint8_t>(v & 0xFF);
}
inline uint32_t get_u32(const uint8_t* p) noexcept {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
           (uint32_t(p[2]) <<  8) |  uint32_t(p[3]);
}
inline uint16_t get_u16(const uint8_t* p) noexcept {
    return static_cast<uint16_t>((uint16_t(p[0]) << 8) | p[1]);
}

} // namespace detail

// ── wrap / unwrap ─────────────────────────────────────────────────────────────

// wrap prepends a 6-byte E2E header (seqNum + CRC-16) to payload.
inline std::vector<uint8_t> wrap(uint32_t seq_num, const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> frame(HeaderLen + payload.size());
    detail::put_u32(&frame[0], seq_num);
    std::copy(payload.begin(), payload.end(), frame.begin() + HeaderLen);
    uint16_t crc = detail::crc16(&frame[0], 4, payload.data(), payload.size());
    detail::put_u16(&frame[4], crc);
    return frame;
}

// unwrap validates the CRC and extracts seq_num + original payload.
inline std::error_code unwrap(const std::vector<uint8_t>& frame,
                               uint32_t& seq_num_out,
                               std::vector<uint8_t>& payload_out) {
    if (frame.size() < HeaderLen)
        return make_error_code(E2EErrc::short_frame);
    seq_num_out = detail::get_u32(frame.data());
    uint16_t got_crc  = detail::get_u16(&frame[4]);
    const uint8_t* pl = frame.data() + HeaderLen;
    size_t   pl_len   = frame.size() - HeaderLen;
    uint16_t want_crc = detail::crc16(frame.data(), 4, pl, pl_len);
    if (got_crc != want_crc)
        return make_error_code(E2EErrc::crc_mismatch);
    payload_out.assign(pl, pl + pl_len);
    return {};
}

// ── ReplayGuard ───────────────────────────────────────────────────────────────

constexpr size_t ReplayWindowSize = 32;

// ReplayGuard implements a bitmap sliding window to detect duplicate or
// replayed sequence numbers (ISO 26262 Part 7 E2E counter protection).
//
// The window covers [high_water - ReplayWindowSize + 1, high_water].
// Sequence numbers older than the window floor are unconditionally rejected.
// Thread-safe.
class ReplayGuard {
public:
    // check returns {} if seq_num is fresh and within the valid window,
    // or E2EErrc::replay if it is a duplicate or too old.
    std::error_code check(uint32_t seq_num) {
        std::lock_guard<std::mutex> lk(mu_);

        if (!initialized_) {
            // Bootstrap — accept the very first sequence number.
            initialized_  = true;
            high_water_   = seq_num;
            bitmap_[seq_num % ReplayWindowSize] = true;
            return {};
        }

        if (seq_num > high_water_) {
            // Advance window: clear slots that are no longer in range.
            for (uint32_t i = high_water_ + 1; i != seq_num + 1; ++i) {
                bitmap_[i % ReplayWindowSize] = false;
            }
            high_water_ = seq_num;
        } else {
            // seq_num <= high_water_
            uint32_t age = high_water_ - seq_num;
            if (age >= static_cast<uint32_t>(ReplayWindowSize)) {
                // Too old — reject.
                return make_error_code(E2EErrc::replay);
            }
        }

        size_t slot = seq_num % ReplayWindowSize;
        if (bitmap_[slot]) {
            return make_error_code(E2EErrc::replay); // duplicate within window
        }
        bitmap_[slot] = true;
        return {};
    }

private:
    std::mutex mu_;
    std::array<bool, ReplayWindowSize> bitmap_{};
    uint32_t   high_water_  = 0;
    bool       initialized_ = false;
};

// ── Controller wrapper ────────────────────────────────────────────────────────

// Controller wraps any rcp::Controller and automatically applies E2E protection
// (sequence counter + CRC-16) to every command payload on send().
class Controller final : public rcp::Controller {
public:
    explicit Controller(std::shared_ptr<rcp::Controller> inner)
        : inner_(std::move(inner)) {}

    Zone zone() const noexcept override { return inner_->zone(); }

    std::error_code send(const rcp::Context& ctx,
                          const Command&      cmd,
                          Response&           out) override {
        uint32_t seq = ++seq_;
        Command protected_cmd = cmd;
        protected_cmd.payload = wrap(seq, cmd.payload);
        return inner_->send(ctx, protected_cmd, out);
    }

    std::error_code subscribe(const rcp::Context&             ctx,
                               std::shared_ptr<StatusChannel>& out) override {
        return inner_->subscribe(ctx, out);
    }

    std::error_code close() override { return inner_->close(); }

private:
    std::shared_ptr<rcp::Controller> inner_;
    std::atomic<uint32_t> seq_{0};
};

// new_controller is a convenience factory.
inline std::shared_ptr<Controller> new_controller(std::shared_ptr<rcp::Controller> inner) {
    return std::make_shared<Controller>(std::move(inner));
}

} // namespace e2e
} // namespace rcp
