// fusa:req REQ-WAKEUP-001
// fusa:req REQ-WAKEUP-002
// fusa:req REQ-WAKEUP-003
// fusa:req REQ-WAKEUP-004
// fusa:req REQ-WAKEUP-005

// Wakeup control endpoint (ep_type 0x01) — the OPEN Alliance TC18 Remote
// Control Protocol Specification v0.5.1_RC's fixed SleepCMD request,
// wake-source pin monitoring, and the repeating WakeUp message handshake
// used during hot-start-from-Sleep (extraction §5.2, §3.3).
//
// ROADMAP.md milestone 51, "Remaining Endpoint Types — LIN, CAN (incl. CAN
// XL), ISELED, MDIO, Wakeup Control (v2.7.0)": the point the roadmap calls
// out explicitly for Wakeup control is that its SleepCMD request is a
// single FIXED opcode byte (kSleepCmd below), not one of the mtv=0
// RequestTypeOpcode kinds rcp/request.hpp's decode_request_type decodes
// (v2.5.0/v2.6.0) — the two are unrelated request-shape mechanisms that
// happen to both repurpose part of a message for a request-kind byte. This
// header has no dependency on rcp/request.hpp and does not call
// decode_request_type anywhere: decode_sleep_cmd below is Wakeup control's
// own, separate decode path, and a future reader must not route SleepCMD
// through the conditional-request taxonomy or vice versa.
//
// This is also the endpoint type ROADMAP.md milestone 53 ("Power Management
// Rebuild", v2.9.0, Phase 14) depends on directly — that milestone's
// power-state rebuild assumes WakeupEndpoint already exists as the place
// sleep/wake transitions and wake-source events are recorded.
//
// Field names and behavior below implement TC18's *behavior* as described in
// an internal structured extraction of the specification named above; no
// text from that document is reproduced here. The concrete wake-source pin
// count and handshake state machine chosen in this file are this
// implementation's own, same as the equivalent disclaimers in
// rcp/avtp.hpp, rcp/regmap.hpp, rcp/endpoint.hpp, and rcp/request.hpp.
#pragma once

#include <cstdint>
#include <string>
#include <system_error>

namespace rcp {
namespace wakeup {

// ── SleepCMD — a fixed opcode, not a RequestTypeOpcode (see header comment) ──

constexpr uint8_t kSleepCmd = 0xA5;

enum class WakeupErrc : int {
    not_sleep_cmd = 1, // decode_sleep_cmd was given a byte other than kSleepCmd
};

inline const std::error_category& wakeup_category() noexcept {
    struct Cat : std::error_category {
        const char* name() const noexcept override { return "rcp.wakeup"; }
        std::string message(int ev) const override {
            switch (static_cast<WakeupErrc>(ev)) {
            case WakeupErrc::not_sleep_cmd:
                return "rcp/wakeup: byte does not match the fixed SleepCMD opcode (0xA5)";
            default:
                return "rcp/wakeup: unknown error";
            }
        }
    };
    static Cat instance;
    return instance;
}

inline std::error_code make_error_code(WakeupErrc e) noexcept {
    return {static_cast<int>(e), wakeup_category()};
}

// decode_sleep_cmd is Wakeup control's entire request-decode surface: a
// single fixed-byte comparison, not a taxonomy lookup.
inline std::error_code decode_sleep_cmd(uint8_t byte) noexcept {
    if (byte != kSleepCmd) return make_error_code(WakeupErrc::not_sleep_cmd);
    return {};
}

// ── Wake-source pins ──────────────────────────────────────────────────────────

using WakeSourceMask = uint32_t;
constexpr uint8_t kMaxWakeSourcePins = 32;

// ── WakeupEndpoint ────────────────────────────────────────────────────────────
// Models three things together: the asleep/awake state SleepCMD and
// wake-source events drive, the accumulated wake-source pin mask, and the
// repeating WakeUp message handshake a hot-start-from-Sleep sequence uses
// (extraction §5.2, §3.3). This header has no clock or transport of its
// own — record_wake_source_event and acknowledge_wakeup are called by the
// embedding application's driver/transport layer, same disclaimer as every
// other endpoint header in this codebase.
class WakeupEndpoint {
public:
    // handle_sleep_cmd applies the fixed SleepCMD request. Returns
    // decode_sleep_cmd's error, unchanged, without altering endpoint state,
    // for any byte other than kSleepCmd.
    std::error_code handle_sleep_cmd(uint8_t request_byte) noexcept {
        auto ec = decode_sleep_cmd(request_byte);
        if (ec) return ec;
        asleep_ = true;
        wake_handshake_pending_ = false; // entering Sleep clears any handshake left over from a prior cycle
        return {};
    }

    bool is_asleep() const noexcept { return asleep_; }

    // record_wake_source_event models a wake-source pin transitioning
    // active: it wakes the endpoint (whether or not it was currently
    // asleep — a pin event outside Sleep is still recorded in the mask) and
    // arms the repeating WakeUp message handshake for a hot start.
    void record_wake_source_event(uint8_t pin) noexcept {
        if (pin < kMaxWakeSourcePins)
            wake_source_pins_ |= (WakeSourceMask{1} << pin);
        asleep_ = false;
        wake_handshake_pending_ = true;
    }

    WakeSourceMask wake_source_pins() const noexcept { return wake_source_pins_; }

    // clear_wake_source_pins resets the accumulated wake-source pin mask
    // (e.g. once the caller's driver layer has consumed it), independent of
    // the handshake-pending flag.
    void clear_wake_source_pins() noexcept { wake_source_pins_ = 0; }

    // wakeup_message_pending reports whether the repeating WakeUp message
    // handshake is still owed a repetition — the caller's transport layer
    // is expected to keep (re-)transmitting the WakeUp message while this
    // is true (extraction §3.3's hot-start-from-Sleep repetition), and stop
    // once acknowledge_wakeup() reports the handshake completed.
    bool wakeup_message_pending() const noexcept { return wake_handshake_pending_; }

    // acknowledge_wakeup completes the handshake once the far end has
    // confirmed receipt of the WakeUp message.
    void acknowledge_wakeup() noexcept { wake_handshake_pending_ = false; }

private:
    bool            asleep_                  = false;
    bool            wake_handshake_pending_  = false;
    WakeSourceMask  wake_source_pins_        = 0;
};

} // namespace wakeup
} // namespace rcp

// Enable std::error_code construction from rcp::wakeup::WakeupErrc.
namespace std {
template <>
struct is_error_code_enum<rcp::wakeup::WakeupErrc> : true_type {};
} // namespace std
