// fusa:req REQ-LINEP-001
// fusa:req REQ-LINEP-002
// fusa:req REQ-LINEP-003
// fusa:req REQ-LINEP-004

// LIN commander endpoint (ep_type 0x06) — the OPEN Alliance TC18 Remote
// Control Protocol Specification v0.5.1_RC's raw-byte-pusher model for LIN:
// a request carries the exact bytes to place on the bus and a response
// carries the exact bytes observed back, with no frame-level concept
// modeled by the endpoint itself at all — no checksum selection, no PID
// generation, no schedule tables (extraction §5.10, §6 "significant
// behavioral-scope question").
//
// ROADMAP.md milestone 51, "Remaining Endpoint Types — LIN, CAN (incl. CAN
// XL), ISELED, MDIO, Wakeup Control (v2.7.0)": LIN is deliberately the
// simplest of this milestone's five endpoint types by design, not by
// omission — the specification puts every bit of LIN frame construction
// (break/sync generation, PID computation, checksum selection between
// classic and enhanced, schedule-table sequencing) on the client-side
// driver, not on the RC-Server endpoint. This is an explicit deviation from
// the pre-replacement `rcp/linbr.hpp` bridge's shape: that stub (now
// DEPRECATE per the Satellite Package Disposition table, left untouched by
// this milestone) was built against the old rcp::Controller/Zone/Command
// model bridging *from* a Zone, which is the inverse direction and had no
// occasion to assume the endpoint understood frame structure one way or the
// other — this header exists to make explicit, in code, that a native
// LIN-commander endpoint carries none of that structure either, so a future
// reader does not carry over any frame-aware assumption from `linbr.hpp`'s
// era. `LinEndpoint::transfer` below mirrors `rcp::i2c::I2cEndpoint::
// transfer`'s raw byte-stream shape for exactly this reason: both endpoint
// types push/pull opaque bytes and leave interpretation to the caller.
//
// Field names and behavior below implement TC18's *behavior* as described in
// an internal structured extraction of the specification named above; no
// text from that document is reproduced here. The concrete transfer-shape
// and trigger-signal id encoding chosen in this file are this
// implementation's own, same as the equivalent disclaimers in rcp/avtp.hpp,
// rcp/regmap.hpp, rcp/endpoint.hpp, and rcp/i2c.hpp.
#pragma once

#include <rcp/endpoint.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <system_error>
#include <vector>

namespace rcp {
namespace lin {

// ── Errors ────────────────────────────────────────────────────────────────────
// no_response is this header's only defined failure: the commander pushed
// bytes onto the bus and nothing came back within whatever window the
// caller's driver layer enforces (this header has no clock of its own, same
// disclaimer as every other bus endpoint type in this codebase).

enum class LinErrc : int {
    no_response = 1,
};

inline const std::error_category& lin_category() noexcept {
    struct Cat : std::error_category {
        const char* name() const noexcept override { return "rcp.lin"; }
        std::string message(int ev) const override {
            switch (static_cast<LinErrc>(ev)) {
            case LinErrc::no_response: return "rcp/lin: no response observed on the bus";
            default:                   return "rcp/lin: unknown error";
            }
        }
    };
    static Cat instance;
    return instance;
}

inline std::error_code make_error_code(LinErrc e) noexcept {
    return {static_cast<int>(e), lin_category()};
}

// ── Trigger signals ───────────────────────────────────────────────────────────
// One TransferComplete/NoResponse pair per LinEndpoint instance — LIN has no
// pre-configured channel concept in this milestone's scope (a single
// LinEndpoint instance models one commander-mode LIN bus), matching
// rcp/i2c.hpp's own no-channel precedent. Built on rcp/endpoint.hpp's
// generic TriggerRegistry.

enum class LinSignal : uint8_t { TransferComplete = 0, NoResponse = 1 };

constexpr endpoint::TriggerRegistry::SignalId lin_signal_id(LinSignal sig) noexcept {
    return static_cast<endpoint::TriggerRegistry::SignalId>(sig);
}

// ── LinEndpoint ───────────────────────────────────────────────────────────────
// Mirrors rcp::i2c::I2cEndpoint's shape deliberately: one request-dispatch
// entry point per incoming LIN transfer, recording the raw bytes pushed and
// the raw bytes observed back, with no interpretation of either as a LIN
// frame (extraction §5.10). `out_bytes` is whatever byte sequence the
// caller's driver constructed (break/sync/PID/data/checksum all already
// assembled by that driver, not by this header); `in_bytes` is this
// implementation's record of whatever came back over the same exchange
// (supplied by the caller — this header models the request/response and
// trigger-signal shape of a LIN commander transfer, not an actual bus).
class LinEndpoint {
public:
    std::error_code transfer(std::vector<uint8_t> out_bytes, std::vector<uint8_t> in_bytes,
                              bool responded = true) {
        last_out_ = std::move(out_bytes);
        last_in_  = std::move(in_bytes);
        if (!responded) {
            triggers_.notify(lin_signal_id(LinSignal::NoResponse));
            return make_error_code(LinErrc::no_response);
        }
        triggers_.notify(lin_signal_id(LinSignal::TransferComplete));
        return {};
    }

    const std::vector<uint8_t>& last_sent() const noexcept { return last_out_; }
    const std::vector<uint8_t>& last_received() const noexcept { return last_in_; }
    endpoint::TriggerRegistry& triggers() noexcept { return triggers_; }

private:
    endpoint::TriggerRegistry triggers_;
    std::vector<uint8_t>      last_out_;
    std::vector<uint8_t>      last_in_;
};

} // namespace lin
} // namespace rcp

// Enable std::error_code construction from rcp::lin::LinErrc.
namespace std {
template <>
struct is_error_code_enum<rcp::lin::LinErrc> : true_type {};
} // namespace std
