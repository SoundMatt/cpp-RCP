// fusa:req REQ-MDIO-001
// fusa:req REQ-MDIO-002
// fusa:req REQ-MDIO-003
// fusa:req REQ-MDIO-004
// fusa:req REQ-MDIO-005
// fusa:req REQ-MDIO-006
// fusa:req REQ-MDIO-007

// MDIO endpoint (ep_type 0x0D) — the OPEN Alliance TC18 Remote Control
// Protocol Specification v0.5.1_RC's mdio_mode-selected register access
// (extraction §5.13).
//
// ROADMAP.md milestone 51, "Remaining Endpoint Types — LIN, CAN (incl. CAN
// XL), ISELED, MDIO, Wakeup Control (v2.7.0)": this endpoint type is fully
// specified in the source document but is not named in that document's own
// informative "ten interfaces" scope-summary list (extraction §1.2) — this
// implementation treats that as an editorial omission in the summary list,
// not as evidence MDIO is actually out of scope, and builds it anyway.
//
// Addressing-model fix (issue #72, cpp-RCP-03): this header previously
// modeled MDIO addressing as an invented IEEE 802.3 Clause 22/Clause
// 45-style PHY-address scheme (a 5-bit PHY address, a Clause22 register /
// Clause45 device-address field, and an MdioClause selector) that has no
// basis in the TC18 spec's own MDIO section. Verified against the OPEN
// Alliance TC18 Remote Control Protocol Specification's "mdio request
// format" figure and Table 57 (§13.7.13.3): the real addressing model is a
// 2-bit mdio_mode selector, an mdio_address field ("as per IEEE & OA SPI
// spec" — i.e. opaque to this header, not decomposed into a PHY/device/
// register split of this header's own invention), and an mdio_payload whose
// width mdio_mode (and, for one mode, which MMS device is addressed)
// determines. The Clause 22/Clause 45 abstraction (MdioClause,
// phy_address, device_or_reg, register_address, kMaxPhyAddress,
// kMaxDeviceOrRegField) is removed entirely and rebuilt around
// mdio_mode/mdio_address/mdio_payload below.
//
// mdio_mode's four values, per Table 57:
//   00b: MMD, single word access   (16-bit payload)
//   01b: MMD, multiple byte access (16-bit payload)
//   10b: MMS, single word access   (16-bit payload)
//   11b: MMS, multiple (double) word access (32-bit payload, but only when
//        the addressed MMS device is MMS0 or MMS1 — any other MMS device
//        number still uses a 16-bit payload even in this mode)
// Table 57 itself prints "01b" for both the first two rows (MMD single word
// and MMD multiple byte access), which cannot both be correct in a 2-bit
// field with four otherwise-unambiguous rows (10b and 11b are printed once
// each, for the two MMS rows) — this is treated as a transcription defect
// in the v0.5.1_RC table, not as evidence of a 3-value field. By
// elimination (00b is the only 2-bit value the table's other three rows
// leave unclaimed), MMD single word access is 00b and MMD multiple byte
// access is 01b, below.
//
// mdio_payload's width, per Table 57 ("for MMD, data fields are 16 bits.
// for MMS0 & 1: data fields are 32 bits. For other MMS, data fields are 16
// bits"): always 16 bits for MMD (either sub-mode) and for MMS single word
// access; 32 bits for MMS multiple (double) word access specifically when
// the addressed MMS device is MMS0 or MMS1, 16 bits otherwise. Which MMS
// device number mdio_address addresses is part of the external IEEE 802.3 /
// OPEN Alliance SPI addressing scheme Table 57 itself defers to (mdio
// address: "As per IEEE & OA SPI spec") and is not decoded by this header;
// MdioRequest::mms_is_0_or_1 below is the caller-supplied fact
// payload_width_bits needs for the MmsMultiWord case.
//
// Table 30/33 Row 2 evt[2:0] validation (post-v2.7.0, EIGHTH and last
// endpoint type in this row, after I2C, ADC, PWM_IN, LIN, CAN, UART, and
// ISELED): MdioEndpoint::handle_request is this header's own wiring of
// rcp::endpoint::evt_row2_kind_of — the shared 3-way evt[2:0] classifier for
// Table 33's {ADC, PWM_IN, I2C, LIN, CAN, UART, ISELED, MDIO} row — into
// MDIO's request handling, following the exact shape rcp/i2c.hpp's
// I2cEndpoint::handle_request, rcp/adc.hpp's AdcEndpoint::handle_request,
// rcp/pwm.hpp's PwmInEndpoint::handle_request, rcp/lin.hpp's
// LinEndpoint::handle_request, rcp/can.hpp's CanEndpoint::handle_request,
// rcp/uart.hpp's UartEndpoint::handle_request, and rcp/iseled.hpp's
// IseledEndpoint::handle_request established.
//
// NAMING: this header's own single-transaction method — the read/write
// register access above, predating this milestone entirely — was previously
// itself named `handle_request(MdioRequest, MdioResponse&)`. That name is
// now reused for the NEW evt[2:0]-classifying entry point below, so the old
// method is renamed to `transact`, matching the name rcp/iseled.hpp's
// IseledEndpoint::transact already established for the identical "existing
// single-call transaction method, now wrapped by a classifying
// handle_request" role. Every call site (tests/test_mdio.cpp) is updated to
// call transact() directly where it exercises the register-access behavior
// itself, unrelated to evt[2:0] classification.
//
// This is a completely separate, TC18-Table-33-derived concern from this
// header's own addressing-model fix directly above — it does not touch,
// reinterpret, or extend MdioRequest's/MdioResponse's field shapes, or the
// mdio_mode/mdio_address/mdio_payload/register_key model, in any way. See
// handle_request's own doc comment below for why a Reserved or ConfigWrite
// evt must never reach transact().
//
// Field names and behavior below implement TC18's *behavior* as described in
// an internal structured extraction of the specification named above; no
// text from that document is reproduced here. The concrete register-key
// composition chosen in this file is this implementation's own, same as the
// equivalent disclaimers in rcp/avtp.hpp, rcp/regmap.hpp, rcp/endpoint.hpp,
// rcp/i2c.hpp, rcp/adc.hpp, rcp/pwm.hpp, rcp/lin.hpp, rcp/can.hpp,
// rcp/uart.hpp, and rcp/iseled.hpp.
#pragma once

#include <rcp/endpoint.hpp>

#include <cstdint>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>

namespace rcp {
namespace mdio {

// ── mdio_mode ─────────────────────────────────────────────────────────────────
// See header comment for the 00b/01b elimination reasoning.

enum class MdioMode : uint8_t {
    MmdSingleWord = 0b00,
    MmdMultiWord  = 0b01,
    MmsSingleWord = 0b10,
    MmsMultiWord  = 0b11,
};

// payload_width_bits returns the mdio_payload width Table 57 assigns for
// `mode`, given whether the addressed MMS device (relevant only for
// MmsMultiWord) is MMS0 or MMS1.
constexpr uint8_t payload_width_bits(MdioMode mode, bool mms_is_0_or_1) noexcept {
    if (mode == MdioMode::MmsMultiWord && mms_is_0_or_1) return 32;
    return 16;
}

// ── Request / response shapes ────────────────────────────────────────────────

struct MdioRequest {
    MdioMode mode          = MdioMode::MmdSingleWord;
    uint16_t mdio_address  = 0; // opaque per IEEE 802.3 / OA SPI addressing (Table 57); not decoded here
    bool     mms_is_0_or_1 = false; // only meaningful when mode == MmsMultiWord; see payload_width_bits
    bool     is_write      = false;
    uint32_t mdio_payload  = 0; // width per payload_width_bits(mode, mms_is_0_or_1)
};

struct MdioResponse {
    uint32_t mdio_payload = 0;
};

// ── Errors ────────────────────────────────────────────────────────────────────

enum class MdioErrc : int {
    payload_exceeds_mode_width = 1, // mdio_payload does not fit payload_width_bits(mode, mms_is_0_or_1)
    // evt_row2_kind_of classified the request as ConfigWrite (evt[2:0] ==
    // 111b, §12.7.1). This milestone's follow-up deliberately does not
    // implement the configuration-write shape (relative EP_functional-
    // config start address + configuration data) — see handle_request's
    // own comment. Reported explicitly rather than silently accepted as a
    // plain transaction or silently ignored, same as I2C's, ADC's,
    // PWM_IN's, LIN's, CAN's, UART's, and ISELED's own
    // config_write_not_supported variants.
    config_write_not_supported = 2,
};

inline const std::error_category& mdio_category() noexcept {
    struct Cat : std::error_category {
        const char* name() const noexcept override { return "rcp.mdio"; }
        std::string message(int ev) const override {
            switch (static_cast<MdioErrc>(ev)) {
            case MdioErrc::payload_exceeds_mode_width:
                return "rcp/mdio: mdio_payload exceeds the width mdio_mode assigns it";
            case MdioErrc::config_write_not_supported:
                return "rcp/mdio: evt[2:0]=111b configuration-write requests are not yet implemented";
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
    const uint8_t width = payload_width_bits(req.mode, req.mms_is_0_or_1);
    const uint64_t max_value = (width == 32) ? 0xFFFFFFFFull : 0xFFFFull;
    if (req.mdio_payload > max_value) return make_error_code(MdioErrc::payload_exceeds_mode_width);
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
// register_key folds (mode, mdio_address) into one lookup key, so the four
// mdio_mode values never collide with each other even for an identical
// mdio_address bit pattern (mirroring how the register spaces of, e.g.,
// MMD vs. MMS access are logically distinct on real MDIO-manageable
// devices, per the spec's own MMD/MMS split in Table 57).
class MdioEndpoint {
public:
    // transact is MDIO's own pre-existing single-call register access — a
    // write stores req.mdio_payload under (mode, mdio_address) and echoes it
    // back via `out`; a read returns whatever was last stored there, or 0 if
    // never written. Renamed from this method's pre-milestone name
    // `handle_request` (see this header's own top comment) so that name is
    // free for the evt[2:0]-classifying entry point below, matching
    // rcp/iseled.hpp's IseledEndpoint::transact naming for the identical
    // role.
    std::error_code transact(MdioRequest req, MdioResponse& out) {
        auto ec = validate_request(req);
        if (ec) return ec;

        last_request_ = req;
        const uint64_t key = register_key(req);
        if (req.is_write) {
            registers_[key] = req.mdio_payload;
            out.mdio_payload = req.mdio_payload;
        } else {
            const auto it = registers_.find(key);
            out.mdio_payload = (it != registers_.end()) ? it->second : uint32_t{0};
        }
        triggers_.notify(mdio_signal_id(MdioSignal::TransferComplete));
        return {};
    }

    // handle_request is MDIO's request-decode entry point — the piece this
    // header previously had none of, mirroring rcp::i2c::
    // I2cEndpoint::handle_request's shape (this repo's eighth and LAST
    // Table 33 Row 2 endpoint type, after I2C, ADC, PWM_IN, LIN, CAN, UART,
    // and ISELED). It classifies the incoming request's evt[2:0] field via
    // rcp::endpoint::evt_row2_kind_of before doing anything else, so a
    // Reserved value can never reach transact() and be misread as an
    // ordinary transaction, and a ConfigWrite value can never be silently
    // accepted or silently dropped:
    //   - Plain (evt[2:0] == 000b): delegates straight to transact(request,
    //     out) with both arguments unchanged — MDIO's existing
    //     mdio_mode-keyed request/response transaction model (extraction
    //     §13.7.13.3) already IS this row's correct "plain request"
    //     behavior; evt[2:0] carries no combinable value or channel
    //     selector for this row.
    //   - Reserved (evt[2:0] in 001b-110b): returns
    //     endpoint::EndpointErrc::reserved_evt_row2 without recording
    //     anything (last_request_ is left exactly as it was, no register is
    //     written or read, and TransferComplete does not fire) — TC18
    //     requires this be rejected with error code UNSUPPORTED_CMD.
    //   - ConfigWrite (evt[2:0] == 111b): §12.7.1's configuration-write
    //     shape targets the MDIO EP's own functional-config block, not an
    //     mdio_mode-selected register access at all. Full handling is
    //     deliberately out of scope for this milestone (nontrivial — it
    //     needs EP_functional-config wiring this header does not yet have,
    //     the same gap I2C's, ADC's, PWM_IN's, LIN's, CAN's, UART's, and
    //     ISELED's own handle_request comments defer for the identical
    //     reason); this returns MdioErrc::config_write_not_supported rather
    //     than crashing, silently accepting the request as a transaction,
    //     or silently doing nothing.
    //
    // NOT to be confused with this header's own addressing-model fix above
    // (issue #72's mdio_mode/mdio_address/mdio_payload rebuild replacing the
    // invented Clause 22/Clause 45 scheme): that fix concerns what
    // MdioRequest's and MdioResponse's *fields* mean and how wide they are;
    // Table 33's evt[2:0] classification is an entirely separate, orthogonal
    // concern about which *kind* of request evt[2:0] itself selects. Reading
    // evt[2:0] as if it also selected or combined with mode/address/payload,
    // or inventing any encoding of those fields beyond what
    // validate_request/register_key already define, would be exactly the
    // kind of invented, non-spec-derived encoding this header's own
    // addressing-model-fix comment documents having to remove once already.
    // handle_request below calls the same shared evt_row2_kind_of every
    // other Row 2 endpoint type uses and invents nothing of its own.
    std::error_code handle_request(uint8_t evt_op, MdioRequest req, MdioResponse& out) {
        switch (endpoint::evt_row2_kind_of(evt_op)) {
        case endpoint::EvtRow2Kind::Plain:
            return transact(std::move(req), out);
        case endpoint::EvtRow2Kind::Reserved:
            return endpoint::make_error_code(endpoint::EndpointErrc::reserved_evt_row2);
        case endpoint::EvtRow2Kind::ConfigWrite:
            return make_error_code(MdioErrc::config_write_not_supported);
        }
        return endpoint::make_error_code(endpoint::EndpointErrc::reserved_evt_row2); // unreachable
    }

    const MdioRequest& last_request() const noexcept { return last_request_; }
    endpoint::TriggerRegistry& triggers() noexcept { return triggers_; }

private:
    static uint64_t register_key(const MdioRequest& req) noexcept {
        return (static_cast<uint64_t>(req.mode) << 16) | req.mdio_address;
    }

    endpoint::TriggerRegistry              triggers_;
    MdioRequest                            last_request_;
    std::unordered_map<uint64_t, uint32_t> registers_;
};

} // namespace mdio
} // namespace rcp

// Enable std::error_code construction from rcp::mdio::MdioErrc.
namespace std {
template <>
struct is_error_code_enum<rcp::mdio::MdioErrc> : true_type {};
} // namespace std
