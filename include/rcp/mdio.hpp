// fusa:req REQ-MDIO-001
// fusa:req REQ-MDIO-002
// fusa:req REQ-MDIO-003
// fusa:req REQ-MDIO-004
// fusa:req REQ-MDIO-005

// MDIO endpoint (ep_type 0x0D) — the OPEN Alliance TC18 Remote Control
// Protocol Specification v0.5.1_RC's Clause-22/Clause-45-style mode-selected
// PHY register access (extraction §5.13).
//
// ROADMAP.md milestone 51, "Remaining Endpoint Types — LIN, CAN (incl. CAN
// XL), ISELED, MDIO, Wakeup Control (v2.7.0)": two points the roadmap calls
// out explicitly for MDIO. First, this endpoint type is fully specified in
// the source document but is not named in that document's own informative
// "ten interfaces" scope-summary list (extraction §1.2) — this
// implementation treats that as an editorial omission in the summary list,
// not as evidence MDIO is actually out of scope, and builds it anyway.
// Second, MDIO carries essentially no type-specific functional
// configuration beyond rcp/regmap.hpp's already-existing generic/functional
// endpoint config split (v2.1.0) — unlike rcp/gpio.hpp (which layers a
// pin-direction-plus-edge-mask block on top of that split), this header
// defines no MDIO-specific functional-config encode/decode pair at all,
// because the extraction does not call for one; MdioRequest/MdioResponse
// below are the entire type-specific surface.
//
// Field names and behavior below implement TC18's *behavior* as described in
// an internal structured extraction of the specification named above; no
// text from that document is reproduced here. The concrete request/response
// field widths and register-key composition chosen in this file are this
// implementation's own, same as the equivalent disclaimers in rcp/wire.hpp,
// rcp/regmap.hpp, and rcp/endpoint.hpp.
#pragma once

#include <rcp/endpoint.hpp>

#include <cstdint>
#include <string>
#include <system_error>
#include <unordered_map>

namespace rcp {
namespace mdio {

// ── Clause selection ─────────────────────────────────────────────────────────
// Clause 22's addressing is a flat 5-bit register address within a PHY;
// Clause 45 adds a device (MMD) address plus a wider 16-bit register address
// within that device (extraction §5.13).

enum class MdioClause : uint8_t { Clause22 = 0, Clause45 = 1 };

constexpr uint8_t  kMaxPhyAddress        = 0x1Fu; // 5-bit field
constexpr uint8_t  kMaxDeviceOrRegField  = 0x1Fu; // 5-bit field: Clause22 register addr, or Clause45 MMD device addr

struct MdioRequest {
    MdioClause clause           = MdioClause::Clause22;
    uint8_t    phy_address      = 0; // 5-bit PHY address
    uint8_t    device_or_reg    = 0; // Clause22: register address; Clause45: MMD device address
    uint16_t   register_address = 0; // Clause45 only: 16-bit register within the MMD; unused for Clause22
    bool       is_write         = false;
    uint16_t   write_value      = 0; // meaningful only when is_write is true
};

struct MdioResponse {
    uint16_t value = 0;
};

// ── Errors ────────────────────────────────────────────────────────────────────

enum class MdioErrc : int {
    phy_address_out_of_range     = 1,
    device_or_reg_out_of_range   = 2,
};

inline const std::error_category& mdio_category() noexcept {
    struct Cat : std::error_category {
        const char* name() const noexcept override { return "rcp.mdio"; }
        std::string message(int ev) const override {
            switch (static_cast<MdioErrc>(ev)) {
            case MdioErrc::phy_address_out_of_range:
                return "rcp/mdio: PHY address exceeds its 5-bit range";
            case MdioErrc::device_or_reg_out_of_range:
                return "rcp/mdio: device/register address exceeds its 5-bit range";
            default:
                return "rcp/mdio: unknown error";
            }
        }
    };
    static Cat instance;
    return instance;
}

inline std::error_code make_error_code(MdioErrc e) noexcept {
    return {static_cast<int>(e), mdio_category()};
}

inline std::error_code validate_request(const MdioRequest& req) noexcept {
    if (req.phy_address > kMaxPhyAddress) return make_error_code(MdioErrc::phy_address_out_of_range);
    if (req.device_or_reg > kMaxDeviceOrRegField)
        return make_error_code(MdioErrc::device_or_reg_out_of_range);
    return {};
}

// ── Trigger signals ───────────────────────────────────────────────────────────
// One TransferComplete signal per MdioEndpoint instance, built on
// rcp/endpoint.hpp's generic TriggerRegistry, same primitive every other
// bus-style endpoint type in this codebase uses.

enum class MdioSignal : uint8_t { TransferComplete = 0 };

constexpr endpoint::TriggerRegistry::SignalId mdio_signal_id(MdioSignal sig) noexcept {
    return static_cast<endpoint::TriggerRegistry::SignalId>(sig);
}

// ── MdioEndpoint ──────────────────────────────────────────────────────────────
// register_key folds (clause, phy_address, device_or_reg[, register_address
// for Clause45 only]) into one lookup key so Clause22's flat address space
// and Clause45's device+register address space never collide with each
// other, even for identical phy_address/device_or_reg bit patterns.
class MdioEndpoint {
public:
    std::error_code handle_request(MdioRequest req, MdioResponse& out) {
        auto ec = validate_request(req);
        if (ec) return ec;

        last_request_ = req;
        const uint64_t key = register_key(req);
        if (req.is_write) {
            registers_[key] = req.write_value;
            out.value = req.write_value;
        } else {
            const auto it = registers_.find(key);
            out.value = (it != registers_.end()) ? it->second : uint16_t{0};
        }
        triggers_.notify(mdio_signal_id(MdioSignal::TransferComplete));
        return {};
    }

    const MdioRequest& last_request() const noexcept { return last_request_; }
    endpoint::TriggerRegistry& triggers() noexcept { return triggers_; }

private:
    static uint64_t register_key(const MdioRequest& req) noexcept {
        uint64_t key = (static_cast<uint64_t>(req.clause) << 40) |
                       (static_cast<uint64_t>(req.phy_address) << 32) |
                       (static_cast<uint64_t>(req.device_or_reg) << 16);
        if (req.clause == MdioClause::Clause45) key |= req.register_address;
        return key;
    }

    endpoint::TriggerRegistry            triggers_;
    MdioRequest                          last_request_;
    std::unordered_map<uint64_t, uint16_t> registers_;
};

} // namespace mdio
} // namespace rcp

// Enable std::error_code construction from rcp::mdio::MdioErrc.
namespace std {
template <>
struct is_error_code_enum<rcp::mdio::MdioErrc> : true_type {};
} // namespace std
