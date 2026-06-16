// fusa:req REQ-UDP-001
// fusa:req REQ-UDP-002
// fusa:req REQ-UDP-003
// fusa:req REQ-UDP-004
// fusa:req REQ-UDP-005
// fusa:req REQ-UDP-006
// fusa:req REQ-UDP-007
// fusa:req REQ-UDP-008
// fusa:req REQ-UDP-009
// fusa:req REQ-UDP-010
// fusa:req REQ-UDP-011
// fusa:req REQ-UDP-012

// Binary frame codec shared by the UDP and TLS transports.
//
// Frame layout (all multi-byte fields big-endian):
//   [0]    Magic 'R'
//   [1]    Magic 'C'
//   [2]    Protocol version (0x01)
//   [3]    Message type (TypeCommand / TypeResponse / TypeStatus / …)
//   [4]    Zone (uint8)
//   [5:7]  CommandType or 0x0000 (uint16)
//   [7]    Priority, ResponseStatus, or Healthy flag (uint8)
//   [8:12] Command/response ID or Status seq (uint32)
//   [12:16] Payload length (uint32)
//   [16:]  Payload (variable)
#pragma once

#include "rcp.hpp"

#include <cstdint>
#include <system_error>

namespace rcp {
namespace wire {

constexpr uint8_t MagicByte0 = 0x52; // 'R'
constexpr uint8_t MagicByte1 = 0x43; // 'C'
constexpr uint8_t ProtoVer   = 0x01;

constexpr uint8_t TypeCommand     = 0x01;
constexpr uint8_t TypeResponse    = 0x02;
constexpr uint8_t TypeStatus      = 0x03;
constexpr uint8_t TypeSubscribe   = 0x04;
constexpr uint8_t TypeUnsubscribe = 0x05;

constexpr size_t HeaderLen  = 16;
constexpr size_t MaxPayload = 65507 - HeaderLen; // fits in one UDP datagram

// ── Codec errors ──────────────────────────────────────────────────────────────

enum class WireErrc : int {
    short_frame   = 1,
    bad_magic     = 2,
    bad_version   = 3,
};

inline const std::error_category& wire_category() noexcept {
    struct Cat : std::error_category {
        const char* name() const noexcept override { return "rcp.wire"; }
        std::string message(int ev) const override {
            switch (static_cast<WireErrc>(ev)) {
            case WireErrc::short_frame: return "rcp/wire: frame too short";
            case WireErrc::bad_magic:   return "rcp/wire: bad magic bytes";
            case WireErrc::bad_version: return "rcp/wire: unsupported protocol version";
            default:                    return "rcp/wire: unknown error";
            }
        }
    };
    static Cat instance;
    return instance;
}

inline std::error_code make_error_code(WireErrc e) noexcept {
    return {static_cast<int>(e), wire_category()};
}

// ── Internal helpers ──────────────────────────────────────────────────────────

namespace detail {

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
inline uint16_t get_u16(const uint8_t* p) noexcept {
    return static_cast<uint16_t>((uint16_t(p[0]) << 8) | p[1]);
}
inline uint32_t get_u32(const uint8_t* p) noexcept {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
           (uint32_t(p[2]) <<  8) |  uint32_t(p[3]);
}

} // namespace detail

// ── Header validation ─────────────────────────────────────────────────────────

inline std::error_code validate_header(const uint8_t* b, size_t len) noexcept {
    if (len < HeaderLen)
        return make_error_code(WireErrc::short_frame);
    if (b[0] != MagicByte0 || b[1] != MagicByte1)
        return make_error_code(WireErrc::bad_magic);
    if (b[2] != ProtoVer)
        return make_error_code(WireErrc::bad_version);
    return {};
}

// ── Command encode / decode ───────────────────────────────────────────────────

inline std::vector<uint8_t> encode_command(const Command& cmd) {
    const size_t n = HeaderLen + cmd.payload.size();
    std::vector<uint8_t> buf(n);
    buf[0] = MagicByte0;
    buf[1] = MagicByte1;
    buf[2] = ProtoVer;
    buf[3] = TypeCommand;
    buf[4] = static_cast<uint8_t>(cmd.zone);
    detail::put_u16(&buf[5], static_cast<uint16_t>(cmd.type));
    buf[7] = static_cast<uint8_t>(cmd.priority);
    detail::put_u32(&buf[8], cmd.id);
    detail::put_u32(&buf[12], static_cast<uint32_t>(cmd.payload.size()));
    std::copy(cmd.payload.begin(), cmd.payload.end(), buf.begin() + HeaderLen);
    return buf;
}

inline std::error_code decode_command(const uint8_t* b, size_t len, Command& out) {
    if (auto ec = validate_header(b, len)) return ec;
    uint32_t body_len = detail::get_u32(&b[12]);
    if (len < HeaderLen + body_len)
        return make_error_code(WireErrc::short_frame);
    out.zone     = static_cast<Zone>(b[4]);
    out.type     = static_cast<CommandType>(detail::get_u16(&b[5]));
    out.priority = static_cast<Priority>(b[7]);
    out.id       = detail::get_u32(&b[8]);
    out.payload.assign(b + HeaderLen, b + HeaderLen + body_len);
    return {};
}

// ── Response encode / decode ──────────────────────────────────────────────────

inline std::vector<uint8_t> encode_response(const Response& resp) {
    const size_t n = HeaderLen + resp.payload.size();
    std::vector<uint8_t> buf(n);
    buf[0] = MagicByte0;
    buf[1] = MagicByte1;
    buf[2] = ProtoVer;
    buf[3] = TypeResponse;
    buf[4] = static_cast<uint8_t>(resp.zone);
    detail::put_u16(&buf[5], 0);
    buf[7] = static_cast<uint8_t>(resp.status);
    detail::put_u32(&buf[8], resp.command_id);
    detail::put_u32(&buf[12], static_cast<uint32_t>(resp.payload.size()));
    std::copy(resp.payload.begin(), resp.payload.end(), buf.begin() + HeaderLen);
    return buf;
}

inline std::error_code decode_response(const uint8_t* b, size_t len, Response& out) {
    if (auto ec = validate_header(b, len)) return ec;
    uint32_t body_len = detail::get_u32(&b[12]);
    if (len < HeaderLen + body_len)
        return make_error_code(WireErrc::short_frame);
    out.zone       = static_cast<Zone>(b[4]);
    out.status     = static_cast<ResponseStatus>(b[7]);
    out.command_id = detail::get_u32(&b[8]);
    out.payload.assign(b + HeaderLen, b + HeaderLen + body_len);
    return {};
}

// ── Status encode / decode ────────────────────────────────────────────────────

inline std::vector<uint8_t> encode_status(const Status& st) {
    const size_t n = HeaderLen + st.payload.size();
    std::vector<uint8_t> buf(n);
    buf[0] = MagicByte0;
    buf[1] = MagicByte1;
    buf[2] = ProtoVer;
    buf[3] = TypeStatus;
    buf[4] = static_cast<uint8_t>(st.zone);
    detail::put_u16(&buf[5], 0);
    buf[7] = st.healthy ? 1u : 0u;
    detail::put_u32(&buf[8], st.seq);
    detail::put_u32(&buf[12], static_cast<uint32_t>(st.payload.size()));
    std::copy(st.payload.begin(), st.payload.end(), buf.begin() + HeaderLen);
    return buf;
}

inline std::error_code decode_status(const uint8_t* b, size_t len, Status& out) {
    if (auto ec = validate_header(b, len)) return ec;
    uint32_t body_len = detail::get_u32(&b[12]);
    if (len < HeaderLen + body_len)
        return make_error_code(WireErrc::short_frame);
    out.zone    = static_cast<Zone>(b[4]);
    out.healthy = (b[7] == 1);
    out.seq     = detail::get_u32(&b[8]);
    out.payload.assign(b + HeaderLen, b + HeaderLen + body_len);
    return {};
}

// ── Control frame (subscribe / unsubscribe) ───────────────────────────────────

inline std::vector<uint8_t> encode_control(uint8_t msg_type, Zone zone) {
    std::vector<uint8_t> buf(HeaderLen, 0);
    buf[0] = MagicByte0;
    buf[1] = MagicByte1;
    buf[2] = ProtoVer;
    buf[3] = msg_type;
    buf[4] = static_cast<uint8_t>(zone);
    return buf;
}

} // namespace wire
} // namespace rcp
