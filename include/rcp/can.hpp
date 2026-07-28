// fusa:req REQ-CANEP-001
// fusa:req REQ-CANEP-002
// fusa:req REQ-CANEP-003
// fusa:req REQ-CANEP-004
// fusa:req REQ-CANEP-005
// fusa:req REQ-CANEP-006
// fusa:req REQ-CANEP-007

// CAN controller endpoint (ep_type 0x0B) — the OPEN Alliance TC18 Remote
// Control Protocol Specification v0.5.1_RC's Classical/FD/XL frame-format
// selection, data-frame-only transfer model, CAN XL's extra header region
// and extended payload ceiling, the three per-phase bit-timing register
// sets, and CAN-XL-specific acceptance/receive filtering (extraction §5.11,
// §7).
//
// ROADMAP.md milestone 51, "Remaining Endpoint Types — LIN, CAN (incl. CAN
// XL), ISELED, MDIO, Wakeup Control (v2.7.0)": CAN is the most structurally
// involved of this milestone's five endpoint types. Three points the
// roadmap calls out explicitly:
//
//   1. Data frames only — this header has no remote-frame request/response
//      shape anywhere in it. The extraction does not name a remote-frame
//      concept for this endpoint type, so none is modeled (not even as a
//      rejected/unsupported code path) rather than inventing one.
//   2. CAN XL's payload can reach up to 2054 bytes (`kMaxXlPayloadSpec`
//      below), which necessarily spans multiple AVTPDUs once it exceeds a
//      single frame's budget. ROADMAP.md milestone 52 ("Fragmentation —
//      Go/No-Go Decision", v2.8.0) has already made its go/no-go call for
//      this development cycle: no-go. Consistent with that already-final
//      decision — and following the same pattern rcp/uart.hpp's
//      `kMaxReadSize` already established for UART's own single-AVTPDU
//      accepted limitation — `kMaxXlPayloadSingleAvtpdu` bounds what this
//      header will accept for a single CAN XL transfer to a conservative
//      value strictly below the specification's own 2054-byte field width.
//      A frame whose payload falls between that bound and the spec ceiling
//      is reported via `CanErrc::xl_payload_exceeds_single_avtpdu_bound`
//      rather than silently truncated or accepted — this header does not
//      introduce any new fragmentation logic to carry the excess.
//   3. Unlike every other device-facing endpoint type in this codebase
//      (GPIO, SPI, I2C, UART, ADC, PWM_OUT, PWM_IN, LIN, ISELED, MDIO — all
//      of which build on rcp/endpoint.hpp's TriggerRegistry), the
//      specification defines no trigger-signal table for CAN at all. This
//      is not an oversight in this header — CanEndpoint deliberately has no
//      TriggerRegistry member and no signal-id helper function, and no
//      "fire a trigger" call anywhere below. A future reader adding one
//      should first confirm the extraction actually defines one for this
//      endpoint type, not assume it was merely forgotten here.
//
// Field names and behavior below implement TC18's *behavior* as described in
// an internal structured extraction of the specification named above; no
// text from that document is reproduced here. The concrete bit-timing
// register field layout, the accepted single-AVTPDU XL payload bound, and
// the acceptance-filter matching rule chosen in this file are this
// implementation's own, same as the equivalent disclaimers in rcp/wire.hpp,
// rcp/regmap.hpp, rcp/endpoint.hpp, and rcp/uart.hpp.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <system_error>
#include <vector>

namespace rcp {
namespace can {

// ── Frame format ──────────────────────────────────────────────────────────────
// The explicit FrameFormat sub-field a CAN request/response selects between
// (extraction §5.11): Classical CAN, CAN FD, and CAN XL each have their own
// payload ceiling and, for FD/XL, their own additional framing flags.

enum class FrameFormat : uint8_t { Classical = 0, Fd = 1, Xl = 2 };

constexpr size_t kMaxClassicalPayload = 8;
constexpr size_t kMaxFdPayload        = 64;

// The specification's own CAN XL payload field width (extraction §5.11) —
// distinct from, and larger than, this implementation's accepted
// single-AVTPDU bound below.
constexpr size_t kMaxXlPayloadSpec = 2054;

// This implementation's accepted single-AVTPDU ceiling for CAN XL, per the
// header-comment disclaimer above (point 2). Deliberately well below
// kMaxXlPayloadSpec; not derived from any exact MTU/overhead computation,
// same conservative-bound disclaimer as rcp/uart.hpp's kMaxReadSize.
constexpr size_t kMaxXlPayloadSingleAvtpdu = 256;

// CAN XL's extra header region beyond the framing Classical/FD already carry
// (extraction §5.11) — this header does not itself lay out that region's
// sub-fields, only accounts for its length where relevant (e.g. a future
// wire codec sizing a CAN XL frame's on-wire footprint).
constexpr size_t kXlHeaderExtraLen = 6;

constexpr size_t max_payload_for(FrameFormat fmt) noexcept {
    switch (fmt) {
    case FrameFormat::Classical: return kMaxClassicalPayload;
    case FrameFormat::Fd:        return kMaxFdPayload;
    case FrameFormat::Xl:        return kMaxXlPayloadSingleAvtpdu;
    default:                     return 0;
    }
}

// ── Identifier ────────────────────────────────────────────────────────────────

constexpr uint32_t kMaxStandardId = 0x7FFu;      // 11-bit identifier
constexpr uint32_t kMaxExtendedId = 0x1FFFFFFFu; // 29-bit identifier

struct CanIdentifier {
    uint32_t value    = 0;
    bool     extended = false;
};

// ── Errors ────────────────────────────────────────────────────────────────────

enum class CanErrc : int {
    identifier_out_of_range                = 1, // CanIdentifier::value exceeds its 11-/29-bit range
    payload_exceeds_format_limit           = 2, // payload length exceeds the selected FrameFormat's own max
    xl_payload_exceeds_single_avtpdu_bound = 3, // within the spec's 2054-byte ceiling, but beyond this
                                                  // implementation's accepted single-AVTPDU bound (see header comment)
};

inline const std::error_category& can_category() noexcept {
    struct Cat : std::error_category {
        const char* name() const noexcept override { return "rcp.can"; }
        std::string message(int ev) const override {
            switch (static_cast<CanErrc>(ev)) {
            case CanErrc::identifier_out_of_range:
                return "rcp/can: identifier exceeds its 11-/29-bit range";
            case CanErrc::payload_exceeds_format_limit:
                return "rcp/can: payload exceeds the selected FrameFormat's payload limit";
            case CanErrc::xl_payload_exceeds_single_avtpdu_bound:
                return "rcp/can: CAN XL payload requires multi-AVTPDU fragmentation, not supported this cycle";
            default:
                return "rcp/can: unknown error";
            }
        }
    };
    static Cat instance;
    return instance;
}

inline std::error_code make_error_code(CanErrc e) noexcept {
    return {static_cast<int>(e), can_category()};
}

inline std::error_code validate_identifier(const CanIdentifier& id) noexcept {
    const uint32_t max = id.extended ? kMaxExtendedId : kMaxStandardId;
    if (id.value > max) return make_error_code(CanErrc::identifier_out_of_range);
    return {};
}

// ── Data frame (no remote-frame shape — see header comment point 1) ─────────

struct CanDataFrame {
    CanIdentifier         id;
    FrameFormat           format                  = FrameFormat::Classical;
    bool                  bit_rate_switch          = false; // FD/XL data-phase BRS; ignored for Classical
    bool                  error_state_indicator    = false; // FD/XL ESI; ignored for Classical
    std::vector<uint8_t>  data;
};

// validate_frame checks the identifier's own range, then the payload against
// the selected format's ceiling — for FrameFormat::Xl specifically
// distinguishing "exceeds the specification's own field width" from
// "exceeds this implementation's accepted single-AVTPDU bound" so callers
// can tell a genuinely invalid frame from one this development cycle simply
// does not carry (extraction §5.11; see header comment point 2).
inline std::error_code validate_frame(const CanDataFrame& f) noexcept {
    auto ec = validate_identifier(f.id);
    if (ec) return ec;

    if (f.format == FrameFormat::Xl && f.data.size() > kMaxXlPayloadSingleAvtpdu) {
        if (f.data.size() > kMaxXlPayloadSpec)
            return make_error_code(CanErrc::payload_exceeds_format_limit);
        return make_error_code(CanErrc::xl_payload_exceeds_single_avtpdu_bound);
    }

    if (f.data.size() > max_payload_for(f.format))
        return make_error_code(CanErrc::payload_exceeds_format_limit);

    return {};
}

// ── Per-phase bit-timing register sets ───────────────────────────────────────
// Separate register sets for the arbitration phase (used by every frame
// format for at least its identifier/arbitration field) and the FD/XL
// data-phase bit rates, which run faster than arbitration once BRS is set
// (extraction §5.11). CAN XL's data phase is its own third register set,
// distinct from FD's — the two are not required to share timing.

struct CanBitTimingPhase {
    uint32_t prescaler       = 1;
    uint16_t prop_seg        = 0;
    uint16_t phase_seg1      = 0;
    uint16_t phase_seg2      = 0;
    uint16_t sync_jump_width = 0;
};

struct CanBitTimingConfig {
    CanBitTimingPhase arbitration; // Classical's only phase; FD/XL's arbitration-phase register set
    CanBitTimingPhase fd_data;     // FD's data-phase register set
    CanBitTimingPhase xl_data;     // XL's data-phase register set, independent of fd_data
};

// ── CAN-XL-specific acceptance / receive filters ─────────────────────────────
// A single id/mask acceptance filter shape reused for both CAN's general
// acceptance filtering and CAN XL's own receive filters (extraction §5.11).
// acceptance_filter_matches implements the standard masked-compare rule:
// bits set in `mask` must match between the filter's `id` and the candidate
// identifier; bits clear in `mask` are don't-care.

struct CanAcceptanceFilter {
    uint32_t id       = 0;
    uint32_t mask     = 0;
    bool     extended = false;
};

inline bool acceptance_filter_matches(const CanAcceptanceFilter& filt, const CanIdentifier& id) noexcept {
    if (filt.extended != id.extended) return false;
    return (id.value & filt.mask) == (filt.id & filt.mask);
}

// ── CanEndpoint ───────────────────────────────────────────────────────────────
// No TriggerRegistry member — see header comment point 3. transmit/receive
// model one data-frame exchange each; receive() applies the configured
// acceptance filters (general, then CAN-XL-specific for an XL-format frame)
// when any are configured, dropping frames that match none.
class CanEndpoint {
public:
    std::error_code configure_bit_timing(CanBitTimingConfig cfg) noexcept {
        timing_ = cfg;
        return {};
    }
    const CanBitTimingConfig& bit_timing() const noexcept { return timing_; }

    void set_acceptance_filters(std::vector<CanAcceptanceFilter> filters) {
        filters_ = std::move(filters);
    }
    const std::vector<CanAcceptanceFilter>& acceptance_filters() const noexcept { return filters_; }

    // set_xl_receive_filters configures CAN-XL-specific receive filters,
    // kept as a bank distinct from the general acceptance_filters() above
    // (extraction §5.11) — a CAN XL frame is matched against this bank
    // instead of the general one when it is non-empty.
    void set_xl_receive_filters(std::vector<CanAcceptanceFilter> filters) {
        xl_filters_ = std::move(filters);
    }
    const std::vector<CanAcceptanceFilter>& xl_receive_filters() const noexcept { return xl_filters_; }

    std::error_code transmit(CanDataFrame frame) {
        auto ec = validate_frame(frame);
        if (ec) return ec;
        last_tx_ = std::move(frame);
        return {};
    }

    // receive models one inbound data frame arriving off the bus. Returns
    // false (and does not record the frame) when the frame's format-scoped
    // filter bank is non-empty and none of its entries match — true
    // otherwise, including whenever no filter bank applies at all.
    bool receive(CanDataFrame frame) {
        const std::vector<CanAcceptanceFilter>& bank =
            (frame.format == FrameFormat::Xl && !xl_filters_.empty()) ? xl_filters_ : filters_;
        if (!bank.empty()) {
            const bool matched = std::any_of(bank.begin(), bank.end(), [&](const CanAcceptanceFilter& f) {
                return acceptance_filter_matches(f, frame.id);
            });
            if (!matched) return false;
        }
        last_rx_ = std::move(frame);
        return true;
    }

    const CanDataFrame& last_transmitted() const noexcept { return last_tx_; }
    const CanDataFrame& last_received() const noexcept { return last_rx_; }

private:
    CanBitTimingConfig                timing_;
    std::vector<CanAcceptanceFilter>  filters_;
    std::vector<CanAcceptanceFilter>  xl_filters_;
    CanDataFrame                      last_tx_;
    CanDataFrame                      last_rx_;
};

} // namespace can
} // namespace rcp

// Enable std::error_code construction from rcp::can::CanErrc.
namespace std {
template <>
struct is_error_code_enum<rcp::can::CanErrc> : true_type {};
} // namespace std
