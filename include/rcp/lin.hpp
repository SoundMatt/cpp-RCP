// fusa:req REQ-LINEP-001
// fusa:req REQ-LINEP-002
// fusa:req REQ-LINEP-003
// fusa:req REQ-LINEP-004
// fusa:req REQ-LINEP-005
// fusa:req REQ-LINEP-006

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
// Table 30/33 Row 2 evt[2:0] validation (post-v2.7.0, fourth endpoint type
// after I2C, ADC, and PWM_IN): LinEndpoint::handle_request is this header's
// own wiring of rcp::endpoint::evt_row2_kind_of — the shared 3-way evt[2:0]
// classifier for Table 33's {ADC, PWM_IN, I2C, LIN, CAN, UART, ISELED,
// MDIO} row — into LIN's request decode, following the exact shape
// rcp/i2c.hpp's I2cEndpoint::handle_request, rcp/adc.hpp's
// AdcEndpoint::handle_request, and rcp/pwm.hpp's PwmInEndpoint::
// handle_request established. Plain (evt[2:0]==000b) delegates straight to
// the existing transfer() above, unchanged — LIN's raw-byte-pusher model
// already IS this row's correct "plain request" behavior, the same
// reasoning I2C's own handle_request gave for its identical shape. Reserved
// (001b-110b) is rejected with endpoint::EndpointErrc::reserved_evt_row2
// without touching out_bytes/in_bytes or any transfer state. ConfigWrite
// (evt[2:0]==111b, §12.7.1) is reported as the new
// LinErrc::config_write_not_supported rather than crashing or silently
// accepted as a plain transfer — LIN has no EP_functional-config wiring in
// this codebase yet (same gap I2C's, ADC's, and PWM_IN's own handle_request
// comments call out for their own endpoint types), so full §12.7.1 handling
// is out of scope here too.
//
// NOT to be confused with §13.7.10.1's separate "conditions given by
// evt[2:0]" text describing pending-read-request byte-sequence matching
// against bus traffic (LIN's own analog of I2C's compound_wait_matches_bits
// — a different mechanism, out of this milestone's scope, not modeled by
// this header at all): that text is about compound-wait match conditions,
// not about Table 33's top-level Plain/Reserved/ConfigWrite request
// classification handle_request implements below. Reading the two as one
// "evt[2:0] selects a comparison mode" scheme would be exactly the kind of
// invented, non-spec-derived encoding this codebase has had to remove
// elsewhere once discovered (e.g. rcp/iseled.hpp's and rcp/mdio.hpp's own
// header comments on previously invented, non-spec-derived field encodings
// later corrected) — handle_request below calls the same shared
// evt_row2_kind_of every other Row 2 endpoint type uses and invents nothing
// of its own; §13.7.10.1's pending-read match semantics remain unimplemented
// here, called out rather than silently guessed at.
//
// Field names and behavior below implement TC18's *behavior* as described in
// an internal structured extraction of the specification named above; no
// text from that document is reproduced here. The concrete transfer-shape
// and trigger-signal id encoding chosen in this file are this
// implementation's own, same as the equivalent disclaimers in rcp/avtp.hpp,
// rcp/regmap.hpp, rcp/endpoint.hpp, rcp/i2c.hpp, rcp/adc.hpp, and
// rcp/pwm.hpp.
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
    // evt_row2_kind_of classified the request as ConfigWrite (evt[2:0] ==
    // 111b, §12.7.1). This milestone's follow-up deliberately does not
    // implement the configuration-write shape (relative EP_functional-
    // config start address + configuration data) — see handle_request's
    // own comment. Reported explicitly rather than silently accepted as a
    // plain transfer or silently ignored, same as I2C's, ADC's, and
    // PWM_IN's own config_write_not_supported variants.
    config_write_not_supported = 2,
};

inline const std::error_category& lin_category() noexcept {
    struct Cat : std::error_category {
        const char* name() const noexcept override { return "rcp.lin"; }
        std::string message(int ev) const override {
            switch (static_cast<LinErrc>(ev)) {
            case LinErrc::no_response: return "rcp/lin: no response observed on the bus";
            case LinErrc::config_write_not_supported:
                return "rcp/lin: evt[2:0]=111b configuration-write requests are not yet implemented";
            default: return "rcp/lin: unknown error";
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

    // handle_request is LIN's request-decode entry point — the piece this
    // header previously had none of, mirroring rcp::i2c::I2cEndpoint::
    // handle_request's shape exactly (this repo's fourth Table 33 Row 2
    // endpoint type after I2C, ADC, and PWM_IN). It classifies the incoming
    // request's evt[2:0] field via rcp::endpoint::evt_row2_kind_of before
    // doing anything else, so a Reserved value can never reach transfer()
    // and be misread as an ordinary transfer, and a ConfigWrite value can
    // never be silently accepted or silently dropped:
    //   - Plain (evt[2:0] == 000b): delegates straight to transfer() with
    //     `out_bytes`/`in_bytes`/`responded` unchanged — LIN's existing
    //     raw-byte-pusher transfer model (extraction §13.7.10.3) already IS
    //     this row's correct "plain request" behavior; evt[2:0] carries no
    //     combinable value or channel selector for this row the way it
    //     does for GPIO/PWM_OUT or SPI.
    //   - Reserved (evt[2:0] in 001b-110b): returns
    //     endpoint::EndpointErrc::reserved_evt_row2 without touching any
    //     endpoint state or recording anything as sent/received — TC18
    //     requires this be rejected with error code UNSUPPORTED_CMD.
    //   - ConfigWrite (evt[2:0] == 111b): §12.7.1's configuration-write
    //     shape targets the LIN EP's own functional-config block (relative
    //     start address + configuration data), not a bus transfer at all.
    //     Full handling is deliberately out of scope for this milestone
    //     (nontrivial — it needs EP_functional-config wiring this header
    //     does not yet have, the same gap I2C's, ADC's, and PWM_IN's own
    //     handle_request comments defer for the identical reason); this
    //     returns LinErrc::config_write_not_supported rather than
    //     crashing, silently accepting the request as a transfer, or
    //     silently doing nothing.
    std::error_code handle_request(uint8_t evt_op, std::vector<uint8_t> out_bytes,
                                    std::vector<uint8_t> in_bytes, bool responded = true) {
        switch (endpoint::evt_row2_kind_of(evt_op)) {
        case endpoint::EvtRow2Kind::Plain:
            return transfer(std::move(out_bytes), std::move(in_bytes), responded);
        case endpoint::EvtRow2Kind::Reserved:
            return endpoint::make_error_code(endpoint::EndpointErrc::reserved_evt_row2);
        case endpoint::EvtRow2Kind::ConfigWrite:
            return make_error_code(LinErrc::config_write_not_supported);
        }
        return endpoint::make_error_code(endpoint::EndpointErrc::reserved_evt_row2); // unreachable
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
