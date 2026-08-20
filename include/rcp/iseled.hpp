// fusa:req REQ-ISELED-001
// fusa:req REQ-ISELED-002
// fusa:req REQ-ISELED-003
// fusa:req REQ-ISELED-004
// fusa:req REQ-ISELED-005
// fusa:req REQ-ISELED-006
// fusa:req REQ-ISELED-007

// ISELED endpoint (ep_type 0x0C) — the OPEN Alliance TC18 Remote Control
// Protocol Specification v0.5.1_RC's native ISELED daisy-chain framing: a
// 4-bit Instruction / 12-bit Address / variable-length Data request shape
// and a 12-bit Address / 12-bit Data response shape (extraction §5.12).
//
// ROADMAP.md milestone 51, "Remaining Endpoint Types — LIN, CAN (incl. CAN
// XL), ISELED, MDIO, Wakeup Control (v2.7.0)".
//
// Wire-format fix (issue #71, cpp-RCP-02): this header previously modeled
// IseledResponse::address as a full uint16_t, IseledResponse::data as an
// open-ended std::vector<uint8_t>, and carried an invented 8-bit CRC
// (polynomial 0x07) the header's own prior comment admitted was not from
// the ISELED standard. Verified against the OPEN Alliance TC18 Remote
// Control Protocol Specification's "iseled request format" (Figure 40) and
// "iseled response format" (Figure 41) figures, §13.7.12.3: the address
// field is 12 bits wide (both in the request and, per Figure 41, again in
// the response), and a response's data is a single 12-bit value
// ("Data[11:0]"), not an open byte vector. kIseledFieldMask below is that
// shared 12-bit width, applied to IseledRequest::address,
// IseledResponse::address, and IseledResponse::data via validate_request/
// validate_response. IseledRequest::data is left as std::vector<uint8_t>:
// unlike the response's single Data[11:0] value, the request's data is
// realistic-payload-length-dependent (Figure 40 shows Data1/Data2/Data3/...
// plus padding), so a byte vector remains the right shape there — only the
// response's fixed-width fields were wrong.
//
// This header's invented CRC-8 (polynomial 0x07) is removed outright rather
// than replaced with a differently-sized invented polynomial: the real
// ISELED-standard CRC algorithm and its optional-4-bit-trailer width (per
// Figure 41's "CRC (optional)" field) are not sourced anywhere in this
// codebase or in the TC18 spec extract this header is built from — the
// spec explicitly defers the ISELED CRC's own definition to the separate
// ISELED standard, which this repository does not have a verified copy of
// — so inventing a differently-sized polynomial here would repeat the same
// mistake this fix is for. IseledErrc::field_out_of_range takes over the
// error category in its place: it is a direct consequence of the same
// width fix (rejecting an instruction/address/data value that does not fit
// its documented field width), not a new invented protocol behavior.
// IseledSignal::NativeCrcError is removed along with it, leaving
// IseledSignal::TransferComplete as the endpoint's only trigger signal.
//
// Table 30/33 Row 2 evt[2:0] validation (post-v2.7.0, seventh endpoint type
// after I2C, ADC, PWM_IN, LIN, CAN, and UART): IseledEndpoint::handle_request
// is this header's own wiring of rcp::endpoint::evt_row2_kind_of — the
// shared 3-way evt[2:0] classifier for Table 33's {ADC, PWM_IN, I2C, LIN,
// CAN, UART, ISELED, MDIO} row — into ISELED's request decode, following the
// exact shape rcp/i2c.hpp's I2cEndpoint::handle_request, rcp/adc.hpp's
// AdcEndpoint::handle_request, rcp/pwm.hpp's PwmInEndpoint::handle_request,
// rcp/lin.hpp's LinEndpoint::handle_request, rcp/can.hpp's CanEndpoint::
// handle_request, and rcp/uart.hpp's UartEndpoint::handle_request
// established. This is a completely separate, TC18-Table-33-derived
// concern from the Figure 40/41 wire-format fix directly above — it does
// not touch, reinterpret, or extend IseledRequest's/IseledResponse's field
// shapes, or the encode_iseled_request/decode_iseled_request/
// encode_iseled_response/decode_iseled_response codec, in any way. See
// handle_request's own doc comment below for why a Reserved or ConfigWrite
// evt must never reach transact().
//
// Field names and behavior below implement TC18's *behavior* as described in
// an internal structured extraction of the specification named above; no
// text from that document is reproduced here. The concrete request `data`
// vector representation and trigger-signal id encoding chosen in this file
// are this implementation's own, same as the equivalent disclaimers in
// rcp/avtp.hpp, rcp/regmap.hpp, rcp/endpoint.hpp, rcp/e2e.hpp, rcp/i2c.hpp,
// rcp/adc.hpp, rcp/pwm.hpp, rcp/lin.hpp, rcp/can.hpp, and rcp/uart.hpp.
#pragma once

#include <rcp/avtp.hpp>
#include <rcp/endpoint.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <system_error>
#include <vector>

namespace rcp {
namespace iseled {

// ── Field widths ──────────────────────────────────────────────────────────────
// 12-bit address/data field width shared by the request's Address field and
// the response's Address/Data[11:0] fields (Figures 40/41, §13.7.12.3); 4-bit
// instruction field width (Figure 40).

constexpr uint16_t kIseledFieldMask       = 0x0FFFu; // 12 bits: address, and response data
constexpr uint8_t  kIseledInstructionMask = 0x0Fu;   // 4 bits: request instruction

// ── Request / response shapes ────────────────────────────────────────────────

struct IseledRequest {
    uint8_t               instruction = 0; // 4-bit instruction (kIseledInstructionMask), per Figure 40
    uint16_t               address     = 0; // 12-bit address (kIseledFieldMask), per Figure 40
    std::vector<uint8_t>   data; // variable-length data bytes (Data1/Data2/Data3/... + padding), per Figure 40
};

struct IseledResponse {
    uint16_t address = 0; // 12-bit address (kIseledFieldMask), per Figure 41
    uint16_t data     = 0; // 12-bit data value ("Data[11:0]"), per Figure 41 — a single field, not a byte vector
};

// ── Errors ────────────────────────────────────────────────────────────────────

enum class IseledErrc : int {
    field_out_of_range = 1, // instruction/address/data exceeds its documented wire field width
    // evt_row2_kind_of classified the request as ConfigWrite (evt[2:0] ==
    // 111b, §12.7.1). This milestone's follow-up deliberately does not
    // implement the configuration-write shape (relative EP_functional-
    // config start address + configuration data) — see handle_request's
    // own comment. Reported explicitly rather than silently accepted as a
    // plain transaction or silently ignored, same as I2C's, ADC's,
    // PWM_IN's, LIN's, CAN's, and UART's own config_write_not_supported
    // variants.
    config_write_not_supported = 2,
};

inline const std::error_category& iseled_category() noexcept {
    struct Cat : std::error_category {
        const char* name() const noexcept override { return "rcp.iseled"; }
        std::string message(int ev) const override {
            switch (static_cast<IseledErrc>(ev)) {
            case IseledErrc::field_out_of_range:
                return "rcp/iseled: instruction/address/data exceeds its wire field width";
            case IseledErrc::config_write_not_supported:
                return "rcp/iseled: evt[2:0]=111b configuration-write requests are not yet implemented";
            default:
                return "rcp/iseled: unknown error";
            }
        }
    };
    static Cat instance;
    return instance;
}

inline std::error_code make_error_code(IseledErrc e) noexcept {
    return {static_cast<int>(e), iseled_category()};
}

// validate_request rejects an instruction wider than 4 bits or an address
// wider than 12 bits (Figure 40).
inline std::error_code validate_request(const IseledRequest& req) noexcept {
    if (req.instruction > kIseledInstructionMask) return make_error_code(IseledErrc::field_out_of_range);
    if (req.address > kIseledFieldMask) return make_error_code(IseledErrc::field_out_of_range);
    return {};
}

// validate_response rejects an address or data value wider than 12 bits
// (Figure 41).
inline std::error_code validate_response(const IseledResponse& resp) noexcept {
    if (resp.address > kIseledFieldMask) return make_error_code(IseledErrc::field_out_of_range);
    if (resp.data > kIseledFieldMask) return make_error_code(IseledErrc::field_out_of_range);
    return {};
}

// ── ACF byte_msg_payload codec (Figure 40/41, §13.7.12.3; issue cpp-RCP-A4-iseled) ──
// Before this pass, IseledRequest/IseledResponse and IseledEndpoint::transact
// operated purely on in-memory structs with range validation — nothing in
// this file packed/unpacked the 12-bit address + 12-bit data fields into
// actual ACF wire payload bytes, so no real ISELED request/response could be
// built or parsed for the wire despite ep_type 0x0C being otherwise wired
// up. encode_iseled_request/decode_iseled_request and
// encode_iseled_response/decode_iseled_response below are that missing
// byte-level codec.

// Figure 40: "Instruction | Address | Data1 | Data2 | Data3 | padding" — the
// 4-bit Instruction and 12-bit Address share the first two payload bytes
// (instruction in byte0's top nibble, address's top 4 bits in byte0's low
// nibble, address's low 8 bits in byte1 — the same nibble-then-byte split
// this specification uses throughout, e.g. rcp/acf.hpp's own
// evt/byte_bus_id fields), followed by `data` verbatim. Any trailing
// padding octets Figure 40 shows are this codec's caller's concern to add,
// same as every other endpoint payload in this codebase (see e.g.
// rcp/acf.hpp's own AcfMessageInfo::pad convention) — encode_iseled_request
// does not itself round `data` up to any particular length.
constexpr size_t kIseledRequestFixedLen = 2; // Instruction(4 bits) + Address(12 bits), before variable-length Data

inline std::vector<uint8_t> encode_iseled_request(const IseledRequest& req) {
    std::vector<uint8_t> buf(kIseledRequestFixedLen + req.data.size());
    buf[0] = static_cast<uint8_t>(((req.instruction & kIseledInstructionMask) << 4) |
                                   ((req.address >> 8) & 0x0F));
    buf[1] = static_cast<uint8_t>(req.address & 0xFF);
    std::copy(req.data.begin(), req.data.end(), buf.begin() + static_cast<long>(kIseledRequestFixedLen));
    return buf;
}

inline std::error_code decode_iseled_request(const uint8_t* buf, size_t len, IseledRequest& out) noexcept {
    if (len < kIseledRequestFixedLen) return avtp::make_error_code(avtp::AvtpErrc::short_buffer);
    out.instruction = static_cast<uint8_t>((buf[0] >> 4) & kIseledInstructionMask);
    out.address      = static_cast<uint16_t>(((buf[0] & 0x0F) << 8) | buf[1]);
    out.data.assign(buf + kIseledRequestFixedLen, buf + len);
    return {};
}

// Figure 41: "Address | Data[11:0] | CRC (optional) | rsvd" — Address (12
// bits) and Data[11:0] (12 bits) pack into 3 bytes (address's 8 high bits
// in byte0, address's low 4 bits + data's top 4 bits sharing byte1's two
// nibbles, data's low 8 bits in byte2). The optional 4-bit CRC + rsvd bits
// Figure 41 also shows are NOT encoded here: this repository has no
// verified copy of the ISELED standard's own CRC algorithm (see this
// header's own top-of-file comment on the removed invented CRC-8), so —
// same as the rest of this file — this codec only represents the
// Address/Data fields it can source confidently, not the CRC/rsvd trailer.
constexpr size_t kIseledResponseLen = 3; // Address(12 bits) + Data[11:0](12 bits); no CRC/rsvd

inline std::vector<uint8_t> encode_iseled_response(const IseledResponse& resp) {
    std::vector<uint8_t> buf(kIseledResponseLen);
    const uint16_t addr = resp.address & kIseledFieldMask;
    const uint16_t data = resp.data & kIseledFieldMask;
    buf[0] = static_cast<uint8_t>(addr >> 4);
    buf[1] = static_cast<uint8_t>(((addr & 0x0F) << 4) | ((data >> 8) & 0x0F));
    buf[2] = static_cast<uint8_t>(data & 0xFF);
    return buf;
}

inline std::error_code decode_iseled_response(const uint8_t* buf, size_t len, IseledResponse& out) noexcept {
    if (len != kIseledResponseLen) return avtp::make_error_code(avtp::AvtpErrc::short_buffer);
    out.address = static_cast<uint16_t>((static_cast<uint16_t>(buf[0]) << 4) | (buf[1] >> 4));
    out.data     = static_cast<uint16_t>(((buf[1] & 0x0F) << 8) | buf[2]);
    return {};
}

// ── Trigger signals ───────────────────────────────────────────────────────────
// One TransferComplete signal per IseledEndpoint instance, built on
// rcp/endpoint.hpp's generic TriggerRegistry, same primitive
// rcp/i2c.hpp's and rcp/spi.hpp's transfer-complete signals use.

enum class IseledSignal : uint8_t { TransferComplete = 0 };

constexpr endpoint::TriggerRegistry::SignalId iseled_signal_id(IseledSignal sig) noexcept {
    return static_cast<endpoint::TriggerRegistry::SignalId>(sig);
}

// ── IseledEndpoint ────────────────────────────────────────────────────────────
// Mirrors rcp::spi::SpiEndpoint's/rcp::i2c::I2cEndpoint's shape: one
// request-dispatch entry point per daisy-chain transaction, recording the
// exact Instruction/Address/Data request and Address/Data response
// (supplied together by the caller — this header models the request/
// response and trigger-signal shape of one ISELED transaction, not an
// actual daisy-chain driver or its per-device propagation timing). A
// request or response whose fields do not fit their documented wire widths
// is rejected without being recorded.
class IseledEndpoint {
public:
    std::error_code transact(IseledRequest request, IseledResponse response) {
        if (auto ec = validate_request(request)) return ec;
        if (auto ec = validate_response(response)) return ec;

        last_request_  = std::move(request);
        last_response_ = std::move(response);
        triggers_.notify(iseled_signal_id(IseledSignal::TransferComplete));
        return {};
    }

    // handle_request is ISELED's request-decode entry point — the piece
    // this header previously had none of, mirroring rcp::i2c::
    // I2cEndpoint::handle_request's shape (this repo's seventh Table 33
    // Row 2 endpoint type after I2C, ADC, PWM_IN, LIN, CAN, and UART). It
    // classifies the incoming request's evt[2:0] field via rcp::endpoint::
    // evt_row2_kind_of before doing anything else, so a Reserved value can
    // never reach transact() and be misread as an ordinary transaction,
    // and a ConfigWrite value can never be silently accepted or silently
    // dropped:
    //   - Plain (evt[2:0] == 000b): delegates straight to transact(request,
    //     response) with both arguments unchanged — ISELED's existing
    //     request/response transaction model (extraction §13.7.12.3)
    //     already IS this row's correct "plain request" behavior; evt[2:0]
    //     carries no combinable value or channel selector for this row.
    //   - Reserved (evt[2:0] in 001b-110b): returns
    //     endpoint::EndpointErrc::reserved_evt_row2 without recording
    //     anything (last_request_/last_response_ are left exactly as they
    //     were, and TransferComplete does not fire) — TC18 requires this be
    //     rejected with error code UNSUPPORTED_CMD.
    //   - ConfigWrite (evt[2:0] == 111b): §12.7.1's configuration-write
    //     shape targets the ISELED EP's own functional-config block, not a
    //     daisy-chain transaction at all. Full handling is deliberately out
    //     of scope for this milestone (nontrivial — it needs
    //     EP_functional-config wiring this header does not yet have, the
    //     same gap I2C's, ADC's, PWM_IN's, LIN's, CAN's, and UART's own
    //     handle_request comments defer for the identical reason); this
    //     returns IseledErrc::config_write_not_supported rather than
    //     crashing, silently accepting the request as a transaction, or
    //     silently doing nothing.
    //
    // NOT to be confused with this header's own Figure 40/41 wire-format
    // fix directly above (the 12-bit address/data field widths, and the
    // removed invented CRC-8): that fix concerns what IseledRequest's and
    // IseledResponse's *fields* mean and how wide they are; Table 33's
    // evt[2:0] classification is an entirely separate, orthogonal concern
    // about which *kind* of request evt[2:0] itself selects. Reading
    // evt[2:0] as if it also selected or combined with instruction/address/
    // data, or inventing any encoding of those fields beyond what
    // validate_request/validate_response and encode_iseled_request/
    // decode_iseled_request/encode_iseled_response/decode_iseled_response
    // already define, would be exactly the kind of invented, non-spec-
    // derived encoding this header's own top-of-file comment documents
    // having to remove once already. handle_request below calls the same
    // shared evt_row2_kind_of every other Row 2 endpoint type uses and
    // invents nothing of its own.
    std::error_code handle_request(uint8_t evt_op, IseledRequest request, IseledResponse response) {
        switch (endpoint::evt_row2_kind_of(evt_op)) {
        case endpoint::EvtRow2Kind::Plain:
            return transact(std::move(request), std::move(response));
        case endpoint::EvtRow2Kind::Reserved:
            return endpoint::make_error_code(endpoint::EndpointErrc::reserved_evt_row2);
        case endpoint::EvtRow2Kind::ConfigWrite:
            return make_error_code(IseledErrc::config_write_not_supported);
        }
        return endpoint::make_error_code(endpoint::EndpointErrc::reserved_evt_row2); // unreachable
    }

    const IseledRequest&  last_request() const noexcept { return last_request_; }
    const IseledResponse& last_response() const noexcept { return last_response_; }
    endpoint::TriggerRegistry& triggers() noexcept { return triggers_; }

private:
    endpoint::TriggerRegistry triggers_;
    IseledRequest              last_request_;
    IseledResponse             last_response_;
};

} // namespace iseled
} // namespace rcp

// Enable std::error_code construction from rcp::iseled::IseledErrc.
namespace std {
template <>
struct is_error_code_enum<rcp::iseled::IseledErrc> : true_type {};
} // namespace std
