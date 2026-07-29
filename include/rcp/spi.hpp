// fusa:req REQ-SPI-001
// fusa:req REQ-SPI-002
// fusa:req REQ-SPI-003
// fusa:req REQ-SPI-004
// fusa:req REQ-SPI-005

// SPI endpoint (ep_type 0x03) — up to 6 pre-configured channels selected by
// evt[2:0], raw full-duplex PICO-out/POCI-in byte transfer, the
// compound-wait status-byte truncation rule, and transfer-complete/per-CS
// assert/de-assert trigger signals (extraction §5.4, §4.5 Group A).
//
// ROADMAP.md milestone 47, "Basic Endpoint Types I — GPIO & SPI (v2.3.0)":
// SPI is the second endpoint type built on rcp/endpoint.hpp's shared
// trigger-signal table, following the request-dispatch pattern
// rcp/gpio.hpp establishes (SpiEndpoint mirrors GpioEndpoint's shape).
// Unlike GPIO, SPI does *not* use endpoint::WriteSemantics at all — it
// repurposes the same evt[2:0] field as a 0-5 channel selector instead
// (extraction §5.4). The compound-wait truncation rule implemented here
// (compound_wait_matches) is forward-looking scaffolding: the surrounding
// compound-wait request kind itself (sequencer gating, the `cs` field's
// conditional-start meaning) is v2.5.0 scope — this milestone only
// implements the byte-comparison rule the roadmap calls out as belonging
// here.
//
// Field names and behavior below implement TC18's *behavior* as described in
// an internal structured extraction of the specification named above; no
// text from that document is reproduced here. The concrete channel-config
// encoding and trigger-signal id scheme chosen in this file are this
// implementation's own, same as the equivalent disclaimers in
// rcp/avtp.hpp, rcp/regmap.hpp, rcp/endpoint.hpp, and rcp/gpio.hpp.
#pragma once

#include <rcp/endpoint.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <system_error>
#include <vector>

namespace rcp {
namespace spi {

// ── Channels ──────────────────────────────────────────────────────────────────
// SPI exposes up to 6 pre-configured channels, selected by the request's
// evt[2:0] field taking the value of the channel index directly — 0 through
// 5 are valid channels; 6 and 7 have no defined meaning (extraction §5.4).

constexpr uint8_t kMaxChannels = 6;

// ── Errors ────────────────────────────────────────────────────────────────────

enum class SpiErrc : int {
    channel_out_of_range = 1, // evt[2:0] selected a channel >= kMaxChannels
};

inline const std::error_category& spi_category() noexcept {
    struct Cat : std::error_category {
        const char* name() const noexcept override { return "rcp.spi"; }
        std::string message(int ev) const override {
            switch (static_cast<SpiErrc>(ev)) {
            case SpiErrc::channel_out_of_range: return "rcp/spi: channel index out of range (evt[2:0] not in 0..5)";
            default:                            return "rcp/spi: unknown error";
            }
        }
    };
    static Cat instance;
    return instance;
}

inline std::error_code make_error_code(SpiErrc e) noexcept {
    return {static_cast<int>(e), spi_category()};
}

// channel_of decodes a request's evt[2:0] field as an SPI channel selector.
// This is SPI's own decode, distinct from endpoint::write_semantics_of —
// the same 3-bit wire field means something different for SPI than for
// GPIO/PWM_OUT (extraction §5.4).
inline std::error_code channel_of(uint8_t evt_op, uint8_t& out_channel) noexcept {
    const uint8_t channel = static_cast<uint8_t>(evt_op & 0x07);
    if (channel >= kMaxChannels) return make_error_code(SpiErrc::channel_out_of_range);
    out_channel = channel;
    return {};
}

// ── Compound-wait status-byte truncation rule ────────────────────────────────
// A compound-wait request's condition is evaluated only against the first
// kCompoundWaitCompareLen bytes of an SPI status transfer, regardless of how
// many of the up to kMaxStatusBytes status bytes that transfer actually
// carried (extraction §5.4). compound_wait_matches implements exactly that
// truncated comparison; it takes no position on the rest of compound-wait's
// semantics (sequencer state, sc-field meaning), which are v2.5.0 scope.

constexpr size_t kMaxStatusBytes        = 20;
constexpr size_t kCompoundWaitCompareLen = 4;

inline bool compound_wait_matches(const std::vector<uint8_t>& status,
                                   const std::vector<uint8_t>& expected) noexcept {
    if (status.size() > kMaxStatusBytes) return false; // exceeds this endpoint's own status-byte ceiling
    const size_t n = std::min({status.size(), expected.size(), kCompoundWaitCompareLen});
    if (n == 0) return false; // nothing in the truncated window to compare
    return std::equal(status.begin(), status.begin() + static_cast<long>(n), expected.begin());
}

// ── Trigger signals ───────────────────────────────────────────────────────────
// Transfer-complete and per-CS assert/de-assert signals, one instance of
// each per channel, built on rcp/endpoint.hpp's generic TriggerRegistry —
// the same primitive rcp/gpio.hpp's per-pin signals use (extraction §5.4,
// §4.5 Group A).

enum class SpiSignal : uint8_t { TransferComplete = 0, CsAssert = 1, CsDeassert = 2 };

constexpr endpoint::TriggerRegistry::SignalId spi_signal_id(uint8_t channel, SpiSignal sig) noexcept {
    return static_cast<endpoint::TriggerRegistry::SignalId>((uint16_t(channel) << 2) | uint16_t(sig));
}

// ── SpiEndpoint ───────────────────────────────────────────────────────────────
// Mirrors rcp::gpio::GpioEndpoint's shape: one request-dispatch entry point
// per incoming SPI transfer, recording the exchanged bytes and firing the
// channel's trigger signals in the order a client would observe them.
class SpiEndpoint {
public:
    // transfer performs one full-duplex byte exchange on `channel`: CS is
    // asserted, `pico_out` is the bytes sent out, `poci_in` is this
    // implementation's record of the bytes received over the same exchange
    // (supplied by the caller — this header models the request/response and
    // trigger-signal shape of an SPI transfer, not an actual bus), and CS is
    // de-asserted again. Fragmentation is deferred (v2.8.0), so this models
    // one transfer per request rather than a CS line held across several.
    std::error_code transfer(uint8_t channel, std::vector<uint8_t> pico_out,
                              std::vector<uint8_t> poci_in) {
        if (channel >= kMaxChannels) return make_error_code(SpiErrc::channel_out_of_range);

        triggers_.notify(spi_signal_id(channel, SpiSignal::CsAssert));
        last_pico_out_[channel] = std::move(pico_out);
        last_poci_in_[channel]  = std::move(poci_in);
        triggers_.notify(spi_signal_id(channel, SpiSignal::TransferComplete));
        triggers_.notify(spi_signal_id(channel, SpiSignal::CsDeassert));
        return {};
    }

    const std::vector<uint8_t>& last_sent(uint8_t channel) const { return last_pico_out_.at(channel); }
    const std::vector<uint8_t>& last_received(uint8_t channel) const { return last_poci_in_.at(channel); }
    endpoint::TriggerRegistry& triggers() noexcept { return triggers_; }

private:
    endpoint::TriggerRegistry                            triggers_;
    std::array<std::vector<uint8_t>, kMaxChannels>       last_pico_out_;
    std::array<std::vector<uint8_t>, kMaxChannels>       last_poci_in_;
};

} // namespace spi
} // namespace rcp

// Enable std::error_code construction from rcp::spi::SpiErrc.
namespace std {
template <>
struct is_error_code_enum<rcp::spi::SpiErrc> : true_type {};
} // namespace std
