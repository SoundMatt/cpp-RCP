// fusa:req REQ-E2E-001
// fusa:req REQ-E2E-002
// fusa:req REQ-E2E-003
// fusa:req REQ-E2E-004
// fusa:req REQ-E2E-005
// fusa:req REQ-E2E-006
// fusa:req REQ-E2E-007
// fusa:req REQ-E2E-008
// fusa:req REQ-E2E-009
// fusa:req REQ-E2E-010
// fusa:req REQ-E2E-011
// fusa:req REQ-E2E-012
// fusa:req REQ-E2E-013
// fusa:req REQ-E2E-014

// End-to-end CRC safe points — the OPEN Alliance TC18 Remote Control
// Protocol Specification v0.5.1_RC's actual E2E integrity mechanism, plus
// the per-request-stream watchdog and safe-state primitives that the
// safety-tagged (0x8x) request variants (rcp/sequencer.hpp) depend on
// (extraction §3.8, §4.4, §4.7, §6 item 4).
//
// ROADMAP.md milestone 50, "E2E CRC Safe Points & Safety-Request Variants
// (v2.6.0)": this header REPLACES this file's pre-replacement content in
// full, per the Satellite Package Disposition table's entry for `e2e.hpp`
// — the prior ad-hoc CRC-16/CCITT-FALSE + sequence-counter + replay-window
// wrapper around rcp.hpp's Controller is discarded, not adapted. Nothing
// else in this tree depended on that old API (only this file's own test
// did), so no legacy shim is needed here, unlike rcp/wire.hpp's
// rcp/legacy_wire.hpp split at v2.0.0.
//
// This header rides on top of rcp/wire.hpp's AcfMessageInfo/StreamId
// (v2.0.0), rcp/regmap.hpp's RequestStreamConfig/EndpointGenericConfig
// (v2.1.0, expanded to their full v2.6.0 field set alongside this header),
// and rcp/sequencer.hpp's RequestRecord/RequestLedger/SequencerTable
// (v2.5.0/v2.6.0) without modifying any of their core framing — the CRC
// coverage builder below serializes an already-built AcfMessageInfo rather
// than reaching into rcp/wire.hpp's codec internals, and the
// watchdog-overflow queue behavior reuses
// rcp::sequencer::RequestLedger::cancel_all(non_safestate_only) directly
// rather than reimplementing cancellation.
//
// Field names and behavior below implement TC18's *behavior* as described
// in an internal structured extraction of the specification named above;
// no text from that document is reproduced here. The concrete CRC
// bit-packing, byte layout of the coverage buffer, and watchdog/safe-state
// class shapes chosen in this file are this implementation's own encoding
// of that behavior — full bit-for-bit conformance against other TC18
// implementations is not claimed, same as the equivalent disclaimers in
// rcp/wire.hpp, rcp/regmap.hpp, and rcp/sequencer.hpp. This header
// provides primitives, not a running scheduler or timer thread — deciding
// *when* to call overflowed()/kick()/should_emit_info_notification() is
// left to the embedding application, same as every other header in this
// codebase.
#pragma once

#include <rcp/regmap.hpp>
#include <rcp/sequencer.hpp>
#include <rcp/wire.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace rcp {
namespace e2e {

// ── Errors ────────────────────────────────────────────────────────────────────

enum class E2eErrc : int {
    crc_error          = 1, // CRC_ERROR — computed CRC does not match the received trailer, or the stream is latched
    sequence_violation = 2, // rx_enforce_seq: received sequence number is not strictly increasing
};

inline const std::error_category& e2e_category() noexcept {
    struct Cat : std::error_category {
        const char* name() const noexcept override { return "rcp.e2e"; }
        std::string message(int ev) const override {
            switch (static_cast<E2eErrc>(ev)) {
            case E2eErrc::crc_error:          return "rcp/e2e: CRC_ERROR";
            case E2eErrc::sequence_violation: return "rcp/e2e: sequence number is not strictly increasing";
            default:                          return "rcp/e2e: unknown error";
            }
        }
    };
    static Cat instance;
    return instance;
}

inline std::error_code make_error_code(E2eErrc e) noexcept {
    return {static_cast<int>(e), e2e_category()};
}

// ── CRC32 primitive (extraction §4.7) ────────────────────────────────────────
// 32-bit width, polynomial 0xF4ACFB13, initial value 0xFFFFFFFF, both input
// and output reflected, final XOR 0xFFFFFFFF. This replaces the ad-hoc
// CRC-16/CCITT-FALSE this file used prior to v2.6.0 in full — the two
// schemes share no code.
//
// Implemented as the standard table-less right-shifting technique for a
// RefIn=true/RefOut=true CRC (the same structure zlib's crc32 uses for its
// own, unrelated, polynomial): the working polynomial is bit-reflected once
// up front, after which every byte is folded in LSB-first with no
// per-byte reflection step needed. This is a well-known, independently
// verifiable construction, not something guessed at from the confidential
// specification text.

namespace detail {

constexpr uint32_t kCrc32PolyNormal = 0xF4ACFB13; // TC18 end-to-end CRC polynomial, non-reflected form

constexpr uint32_t reflect32(uint32_t v) noexcept {
    uint32_t r = 0;
    for (int i = 0; i < 32; ++i) {
        r = (r << 1) | (v & 1u);
        v >>= 1;
    }
    return r;
}

inline uint32_t crc32_reflected_poly() noexcept {
    static const uint32_t poly = reflect32(kCrc32PolyNormal);
    return poly;
}

inline uint32_t crc32_update(uint32_t crc, uint8_t byte_in) noexcept {
    crc ^= byte_in;
    const uint32_t poly = crc32_reflected_poly();
    for (int i = 0; i < 8; ++i) {
        crc = (crc & 1u) ? ((crc >> 1) ^ poly) : (crc >> 1);
    }
    return crc;
}

} // namespace detail

// crc32 computes the TC18 end-to-end CRC over an arbitrary byte range
// (extraction §4.7).
inline uint32_t crc32(const uint8_t* data, size_t len) noexcept {
    uint32_t crc = 0xFFFFFFFFu; // initial value
    for (size_t i = 0; i < len; ++i) crc = detail::crc32_update(crc, data[i]);
    return crc ^ 0xFFFFFFFFu; // final XOR
}

inline uint32_t crc32(const std::vector<uint8_t>& data) noexcept {
    return crc32(data.data(), data.size());
}

// ── CRC coverage & the trailing-CRC length pre-adjustment (extraction §4.7) ──
// Coverage is exactly: stream_id + avtp_timestamp (zero-filled when the
// frame rides under an NTSCF header, since NTSCF carries no avtp_timestamp
// field to begin with) + the full ACF shared header + payload. Because the
// CRC is computed *over* the ACF header, and the header's own
// acf_msg_length field must already reflect the trailer's length before
// that header is serialized, the length adjustment has to be applied
// first, not patched in after the fact.

constexpr uint16_t kCrcLengthAdjustQuadlets = 1; // +1 quadlet on AcfMessageInfo::acf_msg_length
constexpr uint16_t kCrcLengthAdjustOctets   = 4; // +4 octets on an outer AVTPDU frame-length field

// apply_acf_length_adjustment mutates `info.acf_msg_length` in place, per
// kCrcLengthAdjustQuadlets. Call this before encode_acf_abb/encode_acf_gbb
// so the trailing CRC's length is already baked into the header the CRC
// itself covers.
inline void apply_acf_length_adjustment(wire::AcfMessageInfo& info) noexcept {
    info.acf_msg_length = static_cast<uint16_t>(info.acf_msg_length + kCrcLengthAdjustQuadlets);
}

// apply_frame_length_adjustment mutates an outer AVTPDU header's
// control_data_length in place, per kCrcLengthAdjustOctets — the paired
// +4 octet adjustment at the frame level. Templated over
// wire::NtscfHeader/wire::TscfHeader since both carry a control_data_length
// field of the same name but rcp/wire.hpp deliberately keeps them as two
// independent structs with no common base to adjust through instead.
template <typename Header>
inline void apply_frame_length_adjustment(Header& hdr) noexcept {
    hdr.control_data_length = static_cast<uint16_t>(hdr.control_data_length + kCrcLengthAdjustOctets);
}

namespace detail {
inline void put_u32_be(std::vector<uint8_t>& buf, uint32_t v) {
    buf.push_back(static_cast<uint8_t>(v >> 24));
    buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>(v & 0xFF));
}
inline void put_u64_be(std::vector<uint8_t>& buf, uint64_t v) {
    for (int i = 0; i < 8; ++i) buf.push_back(static_cast<uint8_t>((v >> (56 - 8 * i)) & 0xFF));
}
} // namespace detail

// coverage_buffer assembles the exact byte sequence the E2E CRC is computed
// over (extraction §4.7). `avtp_timestamp` is std::nullopt when the frame
// rides under an NTSCF header — coverage_buffer supplies the documented
// zero-filled stand-in in that case rather than requiring the caller to
// pass an explicit zero (making the NTSCF case a visible, named choice at
// every call site instead of an easy-to-miss "just pass 0"). `info` must
// already have apply_acf_length_adjustment() applied if the caller wants
// the trailer's length reflected in the coverage — this function only
// serializes whatever AcfMessageInfo it is given.
inline std::vector<uint8_t> coverage_buffer(const wire::StreamId& stream_id,
                                             std::optional<uint32_t> avtp_timestamp,
                                             const wire::AcfMessageInfo& info,
                                             const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> buf;
    buf.reserve(8 + 4 + wire::kAcfCommonHeaderLen + payload.size());

    detail::put_u64_be(buf, stream_id.to_u64());
    detail::put_u32_be(buf, avtp_timestamp.value_or(0)); // zero-filled stand-in under NTSCF

    uint8_t hdr[wire::kAcfCommonHeaderLen];
    wire::encode_acf_message_info(info, hdr);
    buf.insert(buf.end(), hdr, hdr + wire::kAcfCommonHeaderLen);

    buf.insert(buf.end(), payload.begin(), payload.end());
    return buf;
}

// compute_crc is coverage_buffer() + crc32() in one call — the usual way a
// caller actually wants this used.
inline uint32_t compute_crc(const wire::StreamId& stream_id, std::optional<uint32_t> avtp_timestamp,
                             const wire::AcfMessageInfo& info, const std::vector<uint8_t>& payload) {
    return crc32(coverage_buffer(stream_id, avtp_timestamp, info, payload));
}

// append_crc appends the 4-octet big-endian CRC trailer to `frame` — the
// kCrcLengthAdjustOctets the frame-length field above must already account
// for.
inline void append_crc(std::vector<uint8_t>& frame, uint32_t crc) {
    frame.push_back(static_cast<uint8_t>(crc >> 24));
    frame.push_back(static_cast<uint8_t>((crc >> 16) & 0xFF));
    frame.push_back(static_cast<uint8_t>((crc >> 8) & 0xFF));
    frame.push_back(static_cast<uint8_t>(crc & 0xFF));
}

// verify_crc recomputes the CRC over stream_id/avtp_timestamp/info/payload
// and compares it against `received_crc` (as decoded from a frame's
// trailing 4 octets). Returns E2eErrc::crc_error — the CRC_ERROR failure
// path — on mismatch.
inline std::error_code verify_crc(const wire::StreamId& stream_id, std::optional<uint32_t> avtp_timestamp,
                                   const wire::AcfMessageInfo& info, const std::vector<uint8_t>& payload,
                                   uint32_t received_crc) {
    if (compute_crc(stream_id, avtp_timestamp, info, payload) != received_crc)
        return make_error_code(E2eErrc::crc_error);
    return {};
}

// ── Per-endpoint opt-in safe mode (extraction §4.4, §4.7) ────────────────────
// Which of an ACF message's three roles the caller is currently
// checking/producing a CRC for — regmap::EndpointGenericConfig's three
// ep_*_crc_enable toggles are independently settable per role.

enum class MessageRole { Request, Acknowledge, Response };

// crc_required reports whether `cfg` opts the given message role into E2E
// CRC checking. A caller that gets `false` back should neither compute nor
// expect a trailing CRC for that role.
inline bool crc_required(const regmap::EndpointGenericConfig& cfg, MessageRole role) noexcept {
    switch (role) {
    case MessageRole::Request:     return cfg.ep_req_crc_enable;
    case MessageRole::Acknowledge: return cfg.ep_ack_crc_enable;
    case MessageRole::Response:    return cfg.ep_response_crc_enable;
    }
    return false;
}

// implemented_options_bit reports regmap::kOptSafetyRequests when the
// caller's server actually implements the E2E CRC safe-point / safety-
// request mechanism — an explicit, testable single call site rather than a
// bit a caller might set by hand and forget to gate on real support,
// mirroring rcp::sequencer::implemented_options_bits' equivalent pattern.
inline uint32_t implemented_options_bit(bool safety_requests_implemented) noexcept {
    return safety_requests_implemented ? regmap::kOptSafetyRequests : 0;
}

// ── RxStreamGuard — rx_enforce_e2e (extraction §3.8) ─────────────────────────
// Implements the per-request-drop vs. whole-stream-latch choice: with
// rx_enforce_e2e clear, one failing request's CRC_ERROR is reported for
// that request alone; with it set, the first failure additionally latches
// the whole stream so every subsequent request also reports CRC_ERROR
// until reset_latch() is called (e.g. on stream reconfiguration).
class RxStreamGuard {
public:
    // record_crc_result is called once per checked request on this stream.
    // `ok` is the outcome of that request's own verify_crc() call.
    std::error_code record_crc_result(const regmap::RequestStreamConfig& cfg, bool ok) noexcept {
        if (latched_) return make_error_code(E2eErrc::crc_error);
        if (ok) return {};
        if (cfg.rx_enforce_e2e) latched_ = true;
        return make_error_code(E2eErrc::crc_error);
    }

    bool latched() const noexcept { return latched_; }
    void reset_latch() noexcept { latched_ = false; }

private:
    bool latched_ = false;
};

// ── RxSequenceGuard — rx_enforce_seq / rx_seq_safestate_enable ───────────────
// Monotonic sequence-number check, orthogonal to the watchdog below — a
// stream can enforce either, both, or neither independently (extraction
// §3.8). This class only reports the violation; whether that additionally
// drives the endpoint into safe state is the caller's decision, gated on
// cfg.rx_seq_safestate_enable, same "primitive, not policy" split
// RxWatchdog uses below.
class RxSequenceGuard {
public:
    // check verifies `seq` is strictly greater than the last accepted
    // sequence number when cfg.rx_enforce_seq is set; disabled entirely
    // (always accepts) when it is clear. The first observed sequence
    // number is always accepted, bootstrapping the comparison.
    std::error_code check(const regmap::RequestStreamConfig& cfg, uint32_t seq) noexcept {
        if (!cfg.rx_enforce_seq) return {};
        if (has_last_ && seq <= last_seq_) return make_error_code(E2eErrc::sequence_violation);
        has_last_ = true;
        last_seq_ = seq;
        return {};
    }

private:
    bool     has_last_ = false;
    uint32_t last_seq_ = 0;
};

// ── RxWatchdog — rx_wd_* / rx_ovrflw_safestate_enable (extraction §3.8) ──────
// Per-request-stream watchdog and safe-state latch. Deciding *when* to call
// kick()/overflowed() (i.e. running a timer loop) is left to the embedding
// application, same as rcp::sequencer::select_next_due's equivalent
// "primitives, not a scheduler" split.
class RxWatchdog {
public:
    // kick resets the watchdog's last-seen-activity clock to `now_ms`.
    // Callers invoke this once per accepted request on the stream.
    void kick(uint64_t now_ms) noexcept {
        last_kick_ms_ = now_ms;
        kicked_       = true;
    }

    // overflowed reports whether more than cfg.rx_wd_timeout_interval
    // milliseconds have elapsed since the last kick(), gated on
    // cfg.rx_wd_enable — a disabled watchdog never overflows, and one that
    // has never been kicked has nothing to time out yet.
    bool overflowed(const regmap::RequestStreamConfig& cfg, uint64_t now_ms) const noexcept {
        if (!cfg.rx_wd_enable || !kicked_) return false;
        return (now_ms - last_kick_ms_) > cfg.rx_wd_timeout_interval;
    }

    // Safe-state latch. Entering safe state on watchdog/queue overflow is
    // gated by the caller (see apply_watchdog_overflow/apply_queue_overflow
    // below) on the relevant *_safestate_enable field — a register-map
    // field existing does not by itself mean the behavior it gates is on.
    void enter_safe_state() noexcept { in_safe_state_ = true; }
    void clear_safe_state() noexcept { in_safe_state_ = false; }
    bool in_safe_state() const noexcept { return in_safe_state_; }

    // should_emit_info_notification reports whether a repeating
    // "still in safe state" notification (rx_wd_info_enable) should be
    // sent right now — true exactly while latched into safe state with
    // that feature enabled. Repeat cadence/timing is left to the embedding
    // application.
    bool should_emit_info_notification(const regmap::RequestStreamConfig& cfg) const noexcept {
        return in_safe_state_ && cfg.rx_wd_info_enable;
    }

private:
    bool     kicked_        = false;
    uint64_t last_kick_ms_  = 0;
    bool     in_safe_state_ = false;
};

// apply_watchdog_overflow implements the roadmap's purge-normal/retain-
// safety rule for a watchdog-expiry trigger: when cfg.rx_wd_safestate_enable
// is set, `wd` latches into safe state and every pending/started *normal*
// (non-safety) request already queued on `ledger` for this stream is
// canceled exactly as clear-non-safestate (0x06) would, while safety-tagged
// (0x8x) records are left untouched so they can go on to drive the
// endpoint through its safe-state sequence. This reuses
// sequencer::RequestLedger::cancel_all directly rather than reimplementing
// cancellation — see rcp/sequencer.hpp's header comment. Returns the count
// cancel_all() itself returns (0 if the feature is disabled, since nothing
// is purged in that case).
inline size_t apply_watchdog_overflow(const regmap::RequestStreamConfig& cfg, RxWatchdog& wd,
                                       sequencer::RequestLedger& ledger) noexcept {
    if (!cfg.rx_wd_safestate_enable) return 0;
    wd.enter_safe_state();
    return ledger.cancel_all(/*non_safestate_only=*/true);
}

// apply_queue_overflow is the analogous rule for a request-queue overrun —
// a distinct trigger from watchdog expiry (extraction §3.8) that can also
// be configured, via cfg.rx_ovrflw_safestate_enable, to drive the endpoint
// into safe state and purge normal requests the same way.
inline size_t apply_queue_overflow(const regmap::RequestStreamConfig& cfg, RxWatchdog& wd,
                                    sequencer::RequestLedger& ledger) noexcept {
    if (!cfg.rx_ovrflw_safestate_enable) return 0;
    wd.enter_safe_state();
    return ledger.cancel_all(/*non_safestate_only=*/true);
}

// ── Safe-state gating for safety-tagged (0x8x) requests ──────────────────────
// The load-bearing rule the roadmap calls out as new relative to the
// pre-replacement design: a safety-tagged request only actually executes
// once the endpoint is in its configured safe state.

// endpoint_in_configured_safe_state implements the two rx_safety_measure
// strategies (extraction §3.8): RxSafetyMeasure::ForceHighImpedance's
// "safe state" is an external boolean this header does not derive on its
// own — there is no sequencer to consult, so the embedding hardware layer
// reports it directly (typically via an RxWatchdog::in_safe_state() this
// class already tracks once entered, but callers with a different safe-
// state source may pass anything here); RxSafetyMeasure::RunSafeSequencer's
// "safe state" is structural: cfg.rx_safestate_sequencer currently holding
// exactly cfg.rx_safe_sequencer_state.
inline bool endpoint_in_configured_safe_state(const regmap::RequestStreamConfig& cfg,
                                               const sequencer::SequencerTable& sequencers,
                                               bool force_high_impedance_asserted) noexcept {
    if (cfg.rx_safety_measure == regmap::RxSafetyMeasure::ForceHighImpedance)
        return force_high_impedance_asserted;
    regmap::SequencerState cur = 0;
    auto ec = sequencers.state_of(cfg.rx_safestate_sequencer, cur);
    if (ec) return false;
    return cur == cfg.rx_safe_sequencer_state;
}

// may_execute_now gates dispatch of one decoded request record: a
// non-safety record is always eligible as far as this rule is concerned
// (subject to whatever other checks the caller already applies elsewhere);
// a safety-tagged (0x8x, sequencer::RequestRecord::is_safety) record is
// only eligible once `endpoint_in_safe_state` is true.
inline bool may_execute_now(const sequencer::RequestRecord& rec, bool endpoint_in_safe_state) noexcept {
    return !rec.is_safety || endpoint_in_safe_state;
}

} // namespace e2e
} // namespace rcp

// Enable std::error_code construction from rcp::e2e::E2eErrc.
namespace std {
template <>
struct is_error_code_enum<rcp::e2e::E2eErrc> : true_type {};
} // namespace std
