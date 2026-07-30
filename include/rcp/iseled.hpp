// fusa:req REQ-ISELED-001
// fusa:req REQ-ISELED-002
// fusa:req REQ-ISELED-003
// fusa:req REQ-ISELED-004
// fusa:req REQ-ISELED-005

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
// Field names and behavior below implement TC18's *behavior* as described in
// an internal structured extraction of the specification named above; no
// text from that document is reproduced here. The concrete request `data`
// vector representation and trigger-signal id encoding chosen in this file
// are this implementation's own, same as the equivalent disclaimers in
// rcp/avtp.hpp, rcp/regmap.hpp, rcp/endpoint.hpp, and rcp/e2e.hpp.
#pragma once

#include <rcp/endpoint.hpp>

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
};

inline const std::error_category& iseled_category() noexcept {
    struct Cat : std::error_category {
        const char* name() const noexcept override { return "rcp.iseled"; }
        std::string message(int ev) const override {
            switch (static_cast<IseledErrc>(ev)) {
            case IseledErrc::field_out_of_range:
                return "rcp/iseled: instruction/address/data exceeds its wire field width";
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
