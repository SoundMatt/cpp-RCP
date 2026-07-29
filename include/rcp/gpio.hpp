// fusa:req REQ-GPIO-001
// fusa:req REQ-GPIO-002
// fusa:req REQ-GPIO-003
// fusa:req REQ-GPIO-004
// fusa:req REQ-GPIO-005
// fusa:req REQ-GPIO-006
// fusa:req REQ-GPIO-007
// fusa:req REQ-GPIO-008

// GPIO endpoint (ep_type 0x02) — the OPEN Alliance TC18 Remote Control
// Protocol Specification v0.5.1_RC's simplest endpoint type: a 32-pin
// bitmask read/written with one of eight evt[2:0]-selected write semantics,
// plus per-pin change/rising/falling trigger signals (extraction §5.3,
// §4.5 Group C).
//
// ROADMAP.md milestone 47, "Basic Endpoint Types I — GPIO & SPI (v2.3.0)":
// GPIO is the first concrete endpoint type built on rcp/endpoint.hpp's
// shared write-semantics decode, saturating-arithmetic helpers, and
// trigger-signal table (v2.3.0), and is also the vehicle that exercises the
// add/subtract saturation rule PWM_OUT reuses at v2.4.0. It rides on
// rcp/regmap.hpp's generic/functional endpoint config split (v2.1.0) for
// its functional config block and on rcp/avtp.hpp's big-endian field codec
// helpers (v2.0.0) for its 4-byte payload, without changing either header.
//
// Field names and behavior below implement TC18's *behavior* as described in
// an internal structured extraction of the specification named above; no
// text from that document is reproduced here. The concrete pin-direction bit
// convention, functional-config layout, and trigger-signal id encoding
// chosen in this file are this implementation's own, same as the equivalent
// disclaimers in rcp/avtp.hpp, rcp/regmap.hpp, and rcp/endpoint.hpp.
#pragma once

#include <rcp/avtp.hpp>
#include <rcp/endpoint.hpp>
#include <rcp/regmap.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <system_error>
#include <vector>

namespace rcp {
namespace gpio {

// ── Pin mask ──────────────────────────────────────────────────────────────────
// GPIO's request/response payload is a single 4-byte bitmask, one bit per
// pin, up to 32 pins (extraction §5.3).

using PinMask = uint32_t;
constexpr uint8_t kMaxPins       = 32;
constexpr size_t  kGpioPayloadLen = sizeof(PinMask);

// ── GpioState ─────────────────────────────────────────────────────────────────
// `values` is the live per-pin logic-level bitmask every write semantics
// below (other than Reconfigure) reads and updates. `directions` is this
// implementation's own choice of bit convention for which pins are
// currently configured as outputs (1) vs. inputs (0); Reconfigure is the
// only write semantics that targets it rather than `values`.

struct GpioState {
    PinMask values     = 0;
    PinMask directions = 0;
};

// ── Errors ────────────────────────────────────────────────────────────────────

enum class GpioErrc : int {
    pin_index_out_of_range = 1, // a pin index >= kMaxPins was referenced
};

inline const std::error_category& gpio_category() noexcept {
    struct Cat : std::error_category {
        const char* name() const noexcept override { return "rcp.gpio"; }
        std::string message(int ev) const override {
            switch (static_cast<GpioErrc>(ev)) {
            case GpioErrc::pin_index_out_of_range: return "rcp/gpio: pin index out of range";
            default:                               return "rcp/gpio: unknown error";
            }
        }
    };
    static Cat instance;
    return instance;
}

inline std::error_code make_error_code(GpioErrc e) noexcept {
    return {static_cast<int>(e), gpio_category()};
}

// ── Write-semantics application ──────────────────────────────────────────────
// apply_gpio_write is GPIO's endpoint-specific completion of
// endpoint::apply_bitmask_write: it handles the six generic combinators by
// delegating to it against `state.values`, and separately handles
// Reconfigure by treating `operand` as the *new pin-direction mask*
// (replacing, not combining, `state.directions`) rather than a value
// combined with the pin-level state (extraction §5.3, §4.5 Group C).
inline std::error_code apply_gpio_write(endpoint::WriteSemantics op, GpioState& state,
                                         PinMask operand) noexcept {
    if (op == endpoint::WriteSemantics::Reconfigure) {
        state.directions = operand;
        return {};
    }
    uint32_t out = 0;
    auto ec = endpoint::apply_bitmask_write(op, state.values, operand, out);
    if (ec) return ec;
    state.values = out;
    return {};
}

// ── Payload codec ─────────────────────────────────────────────────────────────
// Big-endian 4-byte encoding, matching rcp/avtp.hpp's own field convention
// (reusing its internal put_u32/get_u32 rather than re-deriving byte order
// here).

inline std::vector<uint8_t> encode_gpio_payload(PinMask mask) {
    std::vector<uint8_t> buf(kGpioPayloadLen);
    avtp::detail::put_u32(buf.data(), mask);
    return buf;
}

inline std::error_code decode_gpio_payload(const uint8_t* buf, size_t len, PinMask& out) noexcept {
    if (len < kGpioPayloadLen) return avtp::make_error_code(avtp::AvtpErrc::short_buffer);
    out = avtp::detail::get_u32(buf);
    return {};
}

// ── Trigger signals ───────────────────────────────────────────────────────────
// Per-pin change/rising/falling signals (extraction §5.3, §4.5 Group C),
// built on rcp/endpoint.hpp's generic TriggerRegistry. gpio_signal_id packs
// (pin, edge) into one TriggerRegistry::SignalId so all three of a pin's
// signals — and every other pin's — share one registry instance per GPIO
// endpoint.

enum class GpioEdge : uint8_t { Change = 0, Rising = 1, Falling = 2 };

constexpr endpoint::TriggerRegistry::SignalId gpio_signal_id(uint8_t pin, GpioEdge edge) noexcept {
    return static_cast<endpoint::TriggerRegistry::SignalId>((uint16_t(pin) << 2) | uint16_t(edge));
}

// evaluate_gpio_triggers compares `old_values` against `new_values` bit by
// bit and, for every pin whose bit changed, notifies TriggerRegistry of that
// pin's Change signal, plus Rising or Falling depending on the direction of
// the transition. Returns every signal id notify() reported as armed (i.e.
// worth delivering to a client), in pin-ascending, Change-before-edge order.
inline std::vector<endpoint::TriggerRegistry::SignalId>
evaluate_gpio_triggers(endpoint::TriggerRegistry& triggers, PinMask old_values, PinMask new_values) {
    std::vector<endpoint::TriggerRegistry::SignalId> fired;
    for (uint8_t pin = 0; pin < kMaxPins; ++pin) {
        const bool was_set = ((old_values >> pin) & 1u) != 0;
        const bool is_set  = ((new_values >> pin) & 1u) != 0;
        if (was_set == is_set) continue;

        if (triggers.notify(gpio_signal_id(pin, GpioEdge::Change)))
            fired.push_back(gpio_signal_id(pin, GpioEdge::Change));

        const GpioEdge edge = is_set ? GpioEdge::Rising : GpioEdge::Falling;
        if (triggers.notify(gpio_signal_id(pin, edge)))
            fired.push_back(gpio_signal_id(pin, edge));
    }
    return fired;
}

// ── Functional config block wiring ────────────────────────────────────────────
// Interprets regmap::EndpointFunctionalConfig::data (left as an opaque byte
// blob by v2.1.0's generic/functional split) for a GPIO endpoint: the
// pin-direction mask followed by one enabled-edge bitmask byte per pin
// (bits 0-2 corresponding to GpioEdge::Change/Rising/Falling). This is the
// pattern later endpoint types are expected to follow for their own
// functional config content (extraction §6 item 5).

constexpr size_t kGpioFunctionalConfigLen = kGpioPayloadLen + kMaxPins;

inline regmap::EndpointFunctionalConfig
encode_gpio_functional_config(PinMask directions, const std::array<uint8_t, kMaxPins>& enabled_edge_masks) {
    regmap::EndpointFunctionalConfig cfg;
    cfg.data.resize(kGpioFunctionalConfigLen);
    avtp::detail::put_u32(cfg.data.data(), directions);
    std::copy(enabled_edge_masks.begin(), enabled_edge_masks.end(),
              cfg.data.begin() + static_cast<long>(kGpioPayloadLen));
    return cfg;
}

inline std::error_code decode_gpio_functional_config(const regmap::EndpointFunctionalConfig& cfg,
                                                       PinMask& out_directions,
                                                       std::array<uint8_t, kMaxPins>& out_enabled_edge_masks) noexcept {
    if (cfg.data.size() < kGpioFunctionalConfigLen)
        return avtp::make_error_code(avtp::AvtpErrc::short_buffer);
    out_directions = avtp::detail::get_u32(cfg.data.data());
    std::copy(cfg.data.begin() + static_cast<long>(kGpioPayloadLen),
              cfg.data.begin() + static_cast<long>(kGpioFunctionalConfigLen),
              out_enabled_edge_masks.begin());
    return {};
}

// ── GpioEndpoint ──────────────────────────────────────────────────────────────
// Ties GpioState, apply_gpio_write, and the trigger-signal evaluation above
// into the single request-dispatch entry point a caller (e.g. an RC Server's
// request loop) would invoke per incoming GPIO write — the reusable
// endpoint-registration/dispatch object this milestone establishes the shape
// of for SPI (rcp::spi::SpiEndpoint) and later endpoint types to follow.
class GpioEndpoint {
public:
    // handle_write applies one decoded write request (`op` from
    // endpoint::write_semantics_of(evt_op), `operand` from the request's
    // decoded 4-byte payload) to this endpoint's live state, evaluates the
    // resulting transition against the trigger-signal table, and reports
    // the endpoint's new pin-value bitmask for the caller to encode into a
    // write response.
    std::error_code handle_write(endpoint::WriteSemantics op, PinMask operand, PinMask& out_value) noexcept {
        const PinMask before = state_.values;
        auto ec = apply_gpio_write(op, state_, operand);
        if (ec) return ec;
        evaluate_gpio_triggers(triggers_, before, state_.values);
        out_value = state_.values;
        return {};
    }

    PinMask read() const noexcept { return state_.values; }
    const GpioState& state() const noexcept { return state_; }
    endpoint::TriggerRegistry& triggers() noexcept { return triggers_; }

private:
    GpioState                 state_;
    endpoint::TriggerRegistry triggers_;
};

} // namespace gpio
} // namespace rcp

// Enable std::error_code construction from rcp::gpio::GpioErrc.
namespace std {
template <>
struct is_error_code_enum<rcp::gpio::GpioErrc> : true_type {};
} // namespace std
