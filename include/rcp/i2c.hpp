// fusa:req REQ-I2C-001
// fusa:req REQ-I2C-002
// fusa:req REQ-I2C-003
// fusa:req REQ-I2C-004
// fusa:req REQ-I2C-005
// fusa:req REQ-I2C-006
// fusa:req REQ-I2C-007

// I2C endpoint (ep_type 0x04) — controller-only raw byte-stream transfer
// (including address bytes, per the OPEN Alliance TC18 Remote Control
// Protocol Specification v0.5.1_RC's own framing) and the compound-wait
// arbitrary-bit-sequence match rule (extraction §5.7, §7).
//
// ROADMAP.md milestone 48, "Basic Endpoint Types II — I2C, UART, ADC,
// PWM_OUT, PWM_IN (v2.4.0)": I2C is built on rcp/endpoint.hpp's shared
// TriggerRegistry, following the request-dispatch shape rcp/gpio.hpp and
// rcp/spi.hpp establish (I2cEndpoint mirrors SpiEndpoint's shape: a raw
// byte-exchange transfer() call plus per-endpoint trigger signals). Unlike
// SPI, I2C has no pre-configured channel concept in this milestone's scope
// — a single I2cEndpoint instance models one controller-mode I2C bus, and
// the target device address travels inside the raw byte stream itself
// rather than as a separate selector field.
//
// OPEN ITEM, called out explicitly per the roadmap rather than guessed at:
// the extraction leaves the mapping of I2C's `i2c_mode` field (device
// speed grade — Standard/Fast/Fast-mode-Plus/High-Speed) onto wire values
// ambiguous beyond the coarse `AcfMessageInfo::hs` "high-speed requested"
// flag rcp/acf.hpp already reserves for this purpose. This header
// therefore only decodes that coarse bit (see i2c_mode_of below) and does
// not attempt to further distinguish the standard-speed grades from each
// other; extending i2c_mode_of to do so must wait for a spec errata pass
// that resolves the ambiguity, not a guess made here.
//
// Field names and behavior below implement TC18's *behavior* as described in
// an internal structured extraction of the specification named above; no
// text from that document is reproduced here. The concrete transfer-shape
// and trigger-signal id encoding chosen in this file are this
// implementation's own, same as the equivalent disclaimers in rcp/avtp.hpp,
// rcp/regmap.hpp, rcp/endpoint.hpp, rcp/gpio.hpp, and rcp/spi.hpp.
#pragma once

#include <rcp/endpoint.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <system_error>
#include <vector>

namespace rcp {
namespace i2c {

// ── i2c_mode (OPEN ITEM — see header comment above) ──────────────────────────
// Only the coarse high-speed-requested bit is unambiguous from the
// extraction available to this implementation; finer speed grades are
// deliberately not modeled here.
enum class I2cMode : uint8_t {
    Standard  = 0, // hs not set — this implementation's catch-all for every non-high-speed grade
    HighSpeed = 1, // hs set — sub-mode (if any) is the unresolved part of this open item
};

// i2c_mode_of decodes AcfMessageInfo::hs into the two-way distinction this
// implementation can make without guessing (see OPEN ITEM above).
constexpr I2cMode i2c_mode_of(bool hs) noexcept {
    return hs ? I2cMode::HighSpeed : I2cMode::Standard;
}

// ── Errors ────────────────────────────────────────────────────────────────────

enum class I2cErrc : int {
    nack = 1, // the addressed device did not acknowledge (controller-only view)
};

inline const std::error_category& i2c_category() noexcept {
    struct Cat : std::error_category {
        const char* name() const noexcept override { return "rcp.i2c"; }
        std::string message(int ev) const override {
            switch (static_cast<I2cErrc>(ev)) {
            case I2cErrc::nack: return "rcp/i2c: addressed device did not acknowledge (NACK)";
            default:            return "rcp/i2c: unknown error";
            }
        }
    };
    static Cat instance;
    return instance;
}

inline std::error_code make_error_code(I2cErrc e) noexcept {
    return {static_cast<int>(e), i2c_category()};
}

// ── Compound-wait arbitrary-bit-sequence match ────────────────────────────────
// I2C's compound-wait condition is matched against an arbitrary-length bit
// sequence rather than SPI's fixed 4-of-20-byte truncation (rcp/spi.hpp's
// compound_wait_matches) — extraction §5.7. `bit_len` is the number of
// leading bits of `received` to compare against `expected`, MSB-first
// within each byte; a `bit_len` that is not a multiple of 8 compares only
// the high `bit_len % 8` bits of the final partial byte, leaving the low
// bits of that byte unconstrained. This function takes no position on the
// rest of compound-wait's semantics (sequencer state, the `cs` field),
// which are v2.5.0 scope, same disclaimer as rcp/spi.hpp's own
// compound_wait_matches.
inline bool compound_wait_matches_bits(const std::vector<uint8_t>& received,
                                        const std::vector<uint8_t>& expected,
                                        size_t bit_len) noexcept {
    if (bit_len == 0) return false;
    const size_t full_bytes  = bit_len / 8;
    const size_t rem_bits    = bit_len % 8;
    const size_t needed_bytes = full_bytes + (rem_bits != 0 ? 1 : 0);
    if (received.size() < needed_bytes || expected.size() < needed_bytes) return false;

    if (!std::equal(received.begin(), received.begin() + static_cast<long>(full_bytes), expected.begin()))
        return false;

    if (rem_bits != 0) {
        const uint8_t mask = static_cast<uint8_t>(0xFFu << (8u - rem_bits)); // top rem_bits of the byte
        if ((received[full_bytes] & mask) != (expected[full_bytes] & mask)) return false;
    }
    return true;
}

// ── Trigger signals ───────────────────────────────────────────────────────────
// One TransferComplete/Nack pair per I2cEndpoint instance (no per-channel
// scoping — see the header-comment note on I2C having no channel concept in
// this milestone's scope), built on rcp/endpoint.hpp's generic
// TriggerRegistry, same primitive rcp/gpio.hpp and rcp/spi.hpp use.

enum class I2cSignal : uint8_t { TransferComplete = 0, Nack = 1 };

constexpr endpoint::TriggerRegistry::SignalId i2c_signal_id(I2cSignal sig) noexcept {
    return static_cast<endpoint::TriggerRegistry::SignalId>(sig);
}

// ── I2cEndpoint ───────────────────────────────────────────────────────────────
// Mirrors rcp::spi::SpiEndpoint's shape: one request-dispatch entry point
// per incoming I2C transfer. `out_bytes` is the raw stream sent to the bus,
// address byte(s) included per this milestone's "raw byte stream including
// address bytes" scope; `in_bytes` is this implementation's record of the
// bytes received over the same transfer (supplied by the caller — this
// header models the request/response and trigger-signal shape of an I2C
// transfer, not an actual bus controller). Controller-only: this header has
// no target/peripheral-mode behavior at all.
class I2cEndpoint {
public:
    std::error_code transfer(std::vector<uint8_t> out_bytes, std::vector<uint8_t> in_bytes,
                              bool acked = true) {
        last_out_ = std::move(out_bytes);
        last_in_  = std::move(in_bytes);
        triggers_.notify(i2c_signal_id(I2cSignal::TransferComplete));
        if (!acked) {
            triggers_.notify(i2c_signal_id(I2cSignal::Nack));
            return make_error_code(I2cErrc::nack);
        }
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


// ── TC18 conformance gaps (not implemented) ──────────────────────────────────
// Normative surface of the OPEN Alliance TC18 Remote Control Protocol
// Specification this header does NOT implement. Each item is carried as a
// requirement entry in .fusa-reqs.json marked [NOT IMPLEMENTED], so the
// requirements corpus stays an honest map of the specification rather than
// only of what is built. Do not delete an item without either implementing
// the behavior or updating the matching requirement entry.
//
//   REQ-I2C-007: TC18 §13.7.7.2 Table 46's I2C functional configuration
//     (clock divider, full i2c_mode ladder, i2c_trail) is not modeled.

} // namespace i2c
} // namespace rcp

// Enable std::error_code construction from rcp::i2c::I2cErrc.
namespace std {
template <>
struct is_error_code_enum<rcp::i2c::I2cErrc> : true_type {};
} // namespace std
