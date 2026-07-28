// fusa:req REQ-ISELED-001
// fusa:req REQ-ISELED-002
// fusa:req REQ-ISELED-003
// fusa:req REQ-ISELED-004
// fusa:req REQ-ISELED-005

// ISELED endpoint (ep_type 0x0C) — the OPEN Alliance TC18 Remote Control
// Protocol Specification v0.5.1_RC's native ISELED daisy-chain framing: an
// Instruction/Address/Data request shape and an Address/Data/optional-
// native-CRC response shape (extraction §5.12).
//
// ROADMAP.md milestone 51, "Remaining Endpoint Types — LIN, CAN (incl. CAN
// XL), ISELED, MDIO, Wakeup Control (v2.7.0)": the point the roadmap calls
// out explicitly for ISELED is that its own native CRC (the optional
// trailing field in the response shape below) is ADDITIONAL to, not a
// replacement for, the general RCP-level end-to-end CRC rcp/e2e.hpp already
// implements (v2.6.0). This header has no dependency on rcp/e2e.hpp and does
// not call into it: rcp/e2e.hpp's CRC covers the whole ACF message
// (stream_id + avtp_timestamp + ACF header + payload) regardless of
// endpoint type, and continues to do so unmodified around an ISELED
// request/response exactly as it does around every other endpoint type's
// payload — ISELED's native CRC below is a second, independent, narrower
// check scoped only to the Address/Data content of one daisy-chain
// response, layered on top of that outer coverage rather than folded into
// or substituted for it.
//
// The ISELED daisy-chain protocol itself (its addressing scheme, its actual
// native CRC polynomial/width, its per-device propagation timing) is
// defined by the ISELED standard, not by TC18 RCP, and no such external
// specification is reproduced or matched exactly here — `compute_native_crc8`
// below computes a CRC-8 of this implementation's own choosing, present so
// this header's encode/decode/verify round-trip has a concrete algorithm to
// exercise, not a claim of bit-exact conformance with any particular ISELED
// silicon's real CRC.
//
// Field names and behavior below implement TC18's *behavior* as described in
// an internal structured extraction of the specification named above; no
// text from that document is reproduced here. The concrete request/response
// field widths, the native-CRC algorithm, and the trigger-signal id
// encoding chosen in this file are this implementation's own, same as the
// equivalent disclaimers in rcp/wire.hpp, rcp/regmap.hpp, rcp/endpoint.hpp,
// and rcp/e2e.hpp.
#pragma once

#include <rcp/endpoint.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace rcp {
namespace iseled {

// ── Request / response shapes ────────────────────────────────────────────────

struct IseledRequest {
    uint8_t               instruction = 0;
    uint16_t               address     = 0; // this implementation's own width for a chain-position + register address
    std::vector<uint8_t>   data;
};

struct IseledResponse {
    uint16_t                address    = 0;
    std::vector<uint8_t>    data;
    std::optional<uint8_t>  native_crc; // optional, per the response shape's own "optional-native-CRC" naming
};

// ── Native CRC (this implementation's own algorithm — see header comment) ───

constexpr uint8_t kNativeCrcPoly = 0x07; // CRC-8/CCITT-shaped choice; not a claim of matching real ISELED silicon

inline uint8_t compute_native_crc8(const std::vector<uint8_t>& bytes) noexcept {
    uint8_t crc = 0;
    for (uint8_t b : bytes) {
        crc = static_cast<uint8_t>(crc ^ b);
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x80u) ? static_cast<uint8_t>((crc << 1) ^ kNativeCrcPoly)
                                 : static_cast<uint8_t>(crc << 1);
        }
    }
    return crc;
}

// crc_coverage_bytes assembles exactly the bytes compute_native_crc8 covers
// for a response: the 2-byte big-endian address followed by the data field,
// matching how verify_native_crc below reconstructs the same buffer to
// check a response's optional native_crc.
inline std::vector<uint8_t> crc_coverage_bytes(const IseledResponse& resp) {
    std::vector<uint8_t> buf;
    buf.reserve(2 + resp.data.size());
    buf.push_back(static_cast<uint8_t>(resp.address >> 8));
    buf.push_back(static_cast<uint8_t>(resp.address & 0xFFu));
    buf.insert(buf.end(), resp.data.begin(), resp.data.end());
    return buf;
}

// ── Errors ────────────────────────────────────────────────────────────────────

enum class IseledErrc : int {
    native_crc_mismatch = 1, // response carried a native_crc that does not match its own address+data
};

inline const std::error_category& iseled_category() noexcept {
    struct Cat : std::error_category {
        const char* name() const noexcept override { return "rcp.iseled"; }
        std::string message(int ev) const override {
            switch (static_cast<IseledErrc>(ev)) {
            case IseledErrc::native_crc_mismatch:
                return "rcp/iseled: response native CRC does not match its address+data";
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

// verify_native_crc is a no-op success when response.native_crc is not
// present (the field is optional per the response shape) and otherwise
// recomputes compute_native_crc8 over crc_coverage_bytes(response) and
// compares. This check is independent of, and has no effect on, the
// separate general RCP-level E2E CRC that rcp/e2e.hpp applies around the
// whole ACF message — see header comment.
inline std::error_code verify_native_crc(const IseledResponse& response) noexcept {
    if (!response.native_crc.has_value()) return {};
    const uint8_t expected = compute_native_crc8(crc_coverage_bytes(response));
    if (expected != *response.native_crc) return make_error_code(IseledErrc::native_crc_mismatch);
    return {};
}

// ── Trigger signals ───────────────────────────────────────────────────────────
// One TransferComplete/NativeCrcError pair per IseledEndpoint instance, built
// on rcp/endpoint.hpp's generic TriggerRegistry, same primitive
// rcp/i2c.hpp's and rcp/spi.hpp's transfer-complete signals use.

enum class IseledSignal : uint8_t { TransferComplete = 0, NativeCrcError = 1 };

constexpr endpoint::TriggerRegistry::SignalId iseled_signal_id(IseledSignal sig) noexcept {
    return static_cast<endpoint::TriggerRegistry::SignalId>(sig);
}

// ── IseledEndpoint ────────────────────────────────────────────────────────────
// Mirrors rcp::spi::SpiEndpoint's/rcp::i2c::I2cEndpoint's shape: one
// request-dispatch entry point per daisy-chain transaction, recording the
// exact Instruction/Address/Data request and Address/Data/optional-CRC
// response (supplied together by the caller — this header models the
// request/response and trigger-signal shape of one ISELED transaction, not
// an actual daisy-chain driver or its per-device propagation timing).
class IseledEndpoint {
public:
    std::error_code transact(IseledRequest request, IseledResponse response) {
        last_request_ = std::move(request);
        auto ec = verify_native_crc(response);
        last_response_ = std::move(response);
        if (ec) {
            triggers_.notify(iseled_signal_id(IseledSignal::NativeCrcError));
            return ec;
        }
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
