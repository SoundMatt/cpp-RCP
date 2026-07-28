// fusa:req REQ-ENDPOINT-001
// fusa:req REQ-ENDPOINT-002
// fusa:req REQ-ENDPOINT-003
// fusa:req REQ-ENDPOINT-004
// fusa:req REQ-ENDPOINT-005
// fusa:req REQ-ENDPOINT-006

// Shared endpoint-registration and request-dispatch scaffolding — the pieces
// every concrete OPEN Alliance TC18 Remote Control Protocol Specification
// v0.5.1_RC endpoint type builds request handling on top of, rather than
// each reinventing its own copy (extraction §4.5, §5.x).
//
// ROADMAP.md milestone 47, "Basic Endpoint Types I — GPIO & SPI (v2.3.0)",
// and milestone 48, "Basic Endpoint Types II — I2C, UART, ADC, PWM_OUT,
// PWM_IN (v2.4.0)": GPIO (rcp/gpio.hpp) and SPI (rcp/spi.hpp) were
// deliberately sequenced first because their request/response payload
// shapes are the simplest fully specified ones, which made them the right
// place to establish this reusable pattern before the milestone 48 endpoint
// types (rcp/i2c.hpp, rcp/uart.hpp, rcp/adc.hpp, rcp/pwm.hpp) — and later,
// LIN, CAN, ISELED, MDIO, Wakeup at v2.7.0 — needed it too. This header
// holds exactly the parts of that pattern every endpoint type needs
// directly:
//   - endpoint-type id constants (ep_type), extended by each later milestone
//   - the 8-way evt[2:0] write-semantics decode shared by every endpoint
//     type whose functional payload is a combinable value (GPIO now,
//     PWM_OUT at v2.4.0)
//   - the arithmetic add/subtract saturation rule two of those eight
//     semantics require, generic over the caller's unsigned integer width
//   - a generic trigger-signal table: an enable/disable set plus a pending-
//     delivery queue, addressed by an opaque per-endpoint-type signal id, so
//     GPIO's per-pin change/rising/falling signals and SPI's
//     transfer-complete/per-CS assert/de-assert signals can both be built on
//     the same primitive without duplicating the bookkeeping
//
// This header has no dependency on rcp/wire.hpp's AcfMessageInfo directly —
// it operates on the already-decoded evt[2:0] value a caller extracts from
// AcfMessageInfo::evt_op, keeping this scaffolding usable independent of any
// particular wire encoding decision.
//
// Field names and behavior below implement TC18's *behavior* as described in
// an internal structured extraction of the specification named above; no
// text from that document is reproduced here. The concrete encodings chosen
// in this file are this implementation's own, same as the equivalent
// disclaimers in rcp/wire.hpp, rcp/lifecycle.hpp, rcp/regmap.hpp, and
// rcp/discovery.hpp.
#pragma once

#include <cstdint>
#include <limits>
#include <set>
#include <string>
#include <system_error>
#include <type_traits>
#include <vector>

namespace rcp {
namespace endpoint {

// ── Endpoint-type ids (ep_type) ──────────────────────────────────────────────
// Assigned starting at 1, per rcp/regmap.hpp's own header comment reserving
// this numbering for endpoint milestones. Ids are added here as each
// milestone's endpoint types are implemented, so the numbering stays
// visibly centralized in one place rather than getting re-derived ad hoc by
// each new header. 0x06 has no assignment in either v2.3.0 or v2.4.0's
// scope (the extraction does not name an endpoint type for it) and is left
// unallocated rather than guessed at.

using EndpointTypeId = uint8_t;

constexpr EndpointTypeId kEndpointTypeGpio   = 0x02; // v2.3.0
constexpr EndpointTypeId kEndpointTypeSpi    = 0x03; // v2.3.0
constexpr EndpointTypeId kEndpointTypeI2c    = 0x04; // this milestone (v2.4.0)
constexpr EndpointTypeId kEndpointTypeUart   = 0x05; // this milestone (v2.4.0)
constexpr EndpointTypeId kEndpointTypePwmOut = 0x07; // this milestone (v2.4.0)
constexpr EndpointTypeId kEndpointTypePwmIn  = 0x08; // this milestone (v2.4.0)
constexpr EndpointTypeId kEndpointTypeAdc    = 0x09; // this milestone (v2.4.0)
// LIN, CAN (incl. CAN XL), ISELED, MDIO, Wakeup control — v2.7.0 (ids TBD there)

// ── evt[2:0] write semantics ──────────────────────────────────────────────────
// The 8-way write-semantics selector every combinable-payload endpoint type
// reads out of AcfMessageInfo::evt_op on a write request (extraction §4.5).
// SPI does *not* use this enum — it repurposes the same 3-bit field as a
// channel selector instead (see rcp/spi.hpp) — this enum is only meaningful
// for endpoint types whose functional payload is a value to be combined with
// the endpoint's current state.

enum class WriteSemantics : uint8_t {
    Replace     = 0,
    Or          = 1,
    And         = 2,
    Xor         = 3,
    Reserved    = 4,
    Add         = 5,
    Subtract    = 6,
    Reconfigure = 7,
};

// write_semantics_of masks its argument down to the low 3 bits before
// converting, so callers may pass the raw evt[2:0] field (AcfMessageInfo's
// evt_op is already only ever 3 bits wide, but this keeps the conversion
// total regardless of caller discipline).
constexpr WriteSemantics write_semantics_of(uint8_t evt_op) noexcept {
    return static_cast<WriteSemantics>(evt_op & 0x07);
}

// ── Errors ────────────────────────────────────────────────────────────────────

enum class EndpointErrc : int {
    // evt[2:0] == 4 (Reserved) was used in a write request; this value has
    // no defined behavior and must never be silently treated as a no-op.
    reserved_write_semantics = 1,
    // A write-semantics value that apply_bitmask_write cannot itself carry
    // out generically (currently only Reconfigure, whose target is
    // endpoint-type-specific — see e.g. gpio::apply_gpio_write) was passed
    // to it directly.
    non_combinable_write_semantics = 2,
};

inline const std::error_category& endpoint_category() noexcept {
    struct Cat : std::error_category {
        const char* name() const noexcept override { return "rcp.endpoint"; }
        std::string message(int ev) const override {
            switch (static_cast<EndpointErrc>(ev)) {
            case EndpointErrc::reserved_write_semantics:
                return "rcp/endpoint: evt[2:0]=4 (Reserved) is not a valid write semantics";
            case EndpointErrc::non_combinable_write_semantics:
                return "rcp/endpoint: write semantics does not combine via apply_bitmask_write";
            default:
                return "rcp/endpoint: unknown error";
            }
        }
    };
    static Cat instance;
    return instance;
}

inline std::error_code make_error_code(EndpointErrc e) noexcept {
    return {static_cast<int>(e), endpoint_category()};
}

// ── Saturating arithmetic ─────────────────────────────────────────────────────
// The add/subtract saturation rule shared by GPIO (this milestone) and
// PWM_OUT (v2.4.0, per the roadmap's own note that PWM_OUT reuses it):
// evt[2:0] values Add/Subtract combine the request operand with the
// endpoint's current value by clamping at the representable range's edges
// rather than wrapping (extraction §4.5). Templated on the caller's unsigned
// integer width so both GPIO's 32-bit pin mask and PWM_OUT's narrower
// period/duration fields can share one implementation.

template <typename UInt>
constexpr UInt saturating_add(UInt a, UInt b) noexcept {
    static_assert(std::is_unsigned<UInt>::value, "saturating_add requires an unsigned integer type");
    const UInt sum = static_cast<UInt>(a + b);
    return (sum < a) ? std::numeric_limits<UInt>::max() : sum; // wrapped past max -> saturate
}

template <typename UInt>
constexpr UInt saturating_subtract(UInt a, UInt b) noexcept {
    static_assert(std::is_unsigned<UInt>::value, "saturating_subtract requires an unsigned integer type");
    return (b > a) ? UInt{0} : static_cast<UInt>(a - b); // would go below 0 -> saturate at 0
}

// ── Generic bitmask/value write combinator ────────────────────────────────────
// apply_bitmask_write folds `operand` into `current` according to `op`,
// covering the six of the eight write semantics that are meaningful as a
// pure value combination (Replace/Or/And/Xor/Add/Subtract). Reserved is
// rejected as an error. Reconfigure is deliberately *not* handled here — its
// target (e.g. GPIO pin direction, see gpio::apply_gpio_write) is
// endpoint-type-specific, not a value to combine with `current` — callers
// that receive WriteSemantics::Reconfigure must special-case it themselves
// before reaching this function; passing it here is reported as
// non_combinable_write_semantics rather than silently doing nothing.
inline std::error_code apply_bitmask_write(WriteSemantics op, uint32_t current, uint32_t operand,
                                            uint32_t& out) noexcept {
    switch (op) {
    case WriteSemantics::Replace:  out = operand;                                   return {};
    case WriteSemantics::Or:       out = current | operand;                         return {};
    case WriteSemantics::And:      out = current & operand;                         return {};
    case WriteSemantics::Xor:      out = current ^ operand;                         return {};
    case WriteSemantics::Add:      out = saturating_add<uint32_t>(current, operand);      return {};
    case WriteSemantics::Subtract: out = saturating_subtract<uint32_t>(current, operand); return {};
    case WriteSemantics::Reserved:
        return make_error_code(EndpointErrc::reserved_write_semantics);
    case WriteSemantics::Reconfigure:
        return make_error_code(EndpointErrc::non_combinable_write_semantics);
    default:
        return make_error_code(EndpointErrc::non_combinable_write_semantics);
    }
}

// ── Generic trigger-signal table ──────────────────────────────────────────────
// TriggerRegistry tracks which of an endpoint's trigger signals a client has
// armed and queues occurrences of those signals for delivery, without
// knowing anything about what a "signal" means for a given endpoint type —
// GPIO encodes (pin, edge) pairs into a SignalId (see gpio::gpio_signal_id),
// SPI encodes (channel, transfer-complete-or-CS-edge) pairs into the same
// space (see spi::spi_signal_id), and later endpoint types are expected to
// do the same rather than each building their own enable/pending
// bookkeeping (extraction §4.5 Group A/C).
//
// Signals are level-armed, not edge-latched: notify() on a signal that is
// not currently enabled is simply dropped (returns false) rather than queued
// for later delivery once the client re-arms it — a client must be armed
// *at the time* a signal occurs to see it, matching the request/response
// (not fragmented, not buffered indefinitely) model this milestone targets.
class TriggerRegistry {
public:
    using SignalId = uint16_t;

    void enable(SignalId id) { enabled_.insert(id); }
    void disable(SignalId id) { enabled_.erase(id); }
    bool is_enabled(SignalId id) const { return enabled_.count(id) != 0; }

    // notify records that `id` occurred. Returns true and queues it for
    // drain() iff the signal was enabled; returns false (and does not
    // queue) otherwise.
    bool notify(SignalId id) {
        if (!is_enabled(id)) return false;
        pending_.push_back(id);
        return true;
    }

    bool has_pending() const noexcept { return !pending_.empty(); }

    // drain returns every signal id queued since the last drain() call, in
    // occurrence order, and clears the queue.
    std::vector<SignalId> drain() {
        auto out = std::move(pending_);
        pending_.clear();
        return out;
    }

private:
    std::set<SignalId>    enabled_;
    std::vector<SignalId> pending_;
};

} // namespace endpoint
} // namespace rcp

// Enable std::error_code construction from rcp::endpoint::EndpointErrc.
namespace std {
template <>
struct is_error_code_enum<rcp::endpoint::EndpointErrc> : true_type {};
} // namespace std
