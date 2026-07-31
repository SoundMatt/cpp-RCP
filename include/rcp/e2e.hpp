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
// safety-tagged (0x8x) request variants (rcp/request.hpp) depend on
// (extraction §3.8, §4.4, §4.7, §6 item 4).
//
// ROADMAP.md milestone 50, "E2E CRC Safe Points & Safety-Request Variants
// (v2.6.0)": this header REPLACES this file's pre-replacement content in
// full, per the Satellite Package Disposition table's entry for `e2e.hpp`
// — the prior ad-hoc CRC-16/CCITT-FALSE + sequence-counter + replay-window
// wrapper around rcp.hpp's Controller is discarded, not adapted. Nothing
// else in this tree depended on that old API (only this file's own test
// did), so no legacy shim is needed here, unlike rcp/avtp.hpp's/rcp/acf.hpp's
// rcp/legacy_wire.hpp split at v2.0.0.
//
// This header rides on top of rcp/acf.hpp's AcfMessageInfo and
// rcp/avtp.hpp's StreamId (v2.0.0, split from the original rcp/wire.hpp per
// RELAY spec §13.7.2), rcp/regmap.hpp's RequestStreamConfig/EndpointGenericConfig
// (v2.1.0, expanded to their full v2.6.0 field set alongside this header),
// and rcp/request.hpp's RequestRecord/RequestLedger/SequencerTable
// (v2.5.0/v2.6.0, renamed from rcp/sequencer.hpp per RELAY spec §13.7.2)
// without modifying any of their core framing — the CRC coverage builder
// below serializes an already-built AcfMessageInfo rather than reaching
// into rcp/acf.hpp's codec internals, and the watchdog-overflow queue
// behavior reuses rcp::request::RequestLedger::cancel_all(non_safestate_only)
// directly rather than reimplementing cancellation.
//
// Field names and behavior below implement TC18's *behavior* as described
// in an internal structured extraction of the specification named above;
// no text from that document is reproduced here. The CRC32 algorithm itself
// (polynomial, init/refin/refout/xorout) was already independently
// verifiable and unchanged by this note. The coverage buffer's *byte
// layout* was corrected alongside rcp/acf.hpp's/rcp/avtp.hpp's own wire
// conformance pass (v2.19.0, issue cpp-RCP-N2-03): it now inserts an
// ACF_GBB message's 8-byte message_timestamp between the Message Info
// header and the payload — matching what rcp/acf.hpp's encode_acf_gbb
// actually transmits — where it previously omitted that field from
// coverage entirely. See rcp/acf.hpp's and rcp/avtp.hpp's own header
// comments, and this repository's pull request description, for the
// specification derivation and its honestly-stated confidence level; this
// file's coverage_buffer is a direct, mechanical consequence of that
// derivation rather than a second independent one. This header provides
// primitives, not a running scheduler or timer thread — deciding *when* to
// call overflowed()/kick()/should_emit_info_notification() is left to the
// embedding application, same as every other header in this codebase.
#pragma once

#include <rcp/acf.hpp>
#include <rcp/avtp.hpp>
#include <rcp/regmap.hpp>
#include <rcp/request.hpp>

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

// ── Numeric TC18 wire error code (extraction Table 27; issue cpp-RCP-08) ─────
// The E2eErrc enum's own ordinal values (1, 2, ...) are this codec's
// internal std::error_code values — unrelated to, and not meant to collide
// with, the numeric error codes TC18 actually puts in an error response's
// byte_msg_payload (extraction Table 27, "Error codes in responses"; no
// other error enum in this codebase currently exposes that numeric-code
// mapping either, so there is no existing convention here to match beyond
// keeping the C++ enum's own ordinal untouched). Of E2eErrc's two values,
// only crc_error has a direct TC18 wire error code: POCI_FAILURE (12),
// "CRC of request does not match". sequence_violation has no dedicated TC18
// error code of its own in that table, so wire_error_code() reports
// std::nullopt for it rather than guessing a mapping the specification
// does not state.

constexpr int kPociFailureErrorCode = 12; // TC18 POCI_FAILURE — CRC of request does not match

inline std::optional<int> wire_error_code(E2eErrc e) noexcept {
    switch (e) {
    case E2eErrc::crc_error: return kPociFailureErrorCode;
    default:                 return std::nullopt;
    }
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
// field to begin with) + the complete ACF header + the complete payload.
// "Complete ACF header" (issue cpp-RCP-N2-03, fixed alongside cpp-RCP-04):
// for ACF_GBB this means the 8-byte Message Info *and* the 8-byte
// message_timestamp field that immediately follows it on the wire — the
// same message_timestamp encode_acf_gbb (rcp/acf.hpp) places right after
// the Message Info — not the Message Info alone. ACF_ABB has no
// message_timestamp field at all, so its coverage is the Message Info only,
// same as before. Before this fix, coverage_buffer had no way to include
// message_timestamp at all, so a GBB message's CRC silently omitted 8 real
// wire bytes and folded the payload in 8 bytes early relative to what
// encode_acf_gbb actually transmits. Because the CRC is computed *over* the
// ACF header, and the header's own acf_msg_length field must already
// reflect the trailer's length before that header is serialized, the
// length adjustment has to be applied first, not patched in after the fact.

constexpr uint16_t kCrcLengthAdjustQuadlets = 1; // +1 quadlet on AcfMessageInfo::acf_msg_length
constexpr uint16_t kCrcLengthAdjustOctets   = 4; // +4 octets on an outer AVTPDU frame-length field

// apply_acf_length_adjustment mutates `info.acf_msg_length` in place, per
// kCrcLengthAdjustQuadlets. This function only ever *adds* one quadlet to
// whatever `info.acf_msg_length` already holds — it does not compute a base
// length itself — so a CRC-protected caller must call
// rcp::acf::compute_acf_msg_length(info.acf_msg_type, payload.size()) (or
// otherwise set `info.acf_msg_length` to the correct base value) FIRST, and
// only then call this function, before calling encode_acf_abb/encode_acf_gbb
// (whose own cpp-RCP-01 auto-fill only fires when acf_msg_length is still
// the 0 default — it never overwrites a nonzero value this function already
// adjusted). This two-step order is required so the trailing CRC's length
// is already baked into the header before that header is serialized both
// for the real wire frame and for coverage_buffer()'s own CRC-coverage
// header encode below.
inline void apply_acf_length_adjustment(acf::AcfMessageInfo& info) noexcept {
    info.acf_msg_length = static_cast<uint16_t>(info.acf_msg_length + kCrcLengthAdjustQuadlets);
}

// apply_frame_length_adjustment mutates an outer AVTPDU header's
// control_data_length in place, per kCrcLengthAdjustOctets — the paired
// +4 octet adjustment at the frame level. Templated over
// avtp::NtscfHeader/avtp::TscfHeader since both carry a control_data_length
// field of the same name but rcp/avtp.hpp deliberately keeps them as two
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
// every call site instead of an easy-to-miss "just pass 0"). `message_timestamp`
// is std::nullopt for ACF_ABB (which has no such field) and the actual
// 64-bit value passed to encode_acf_gbb for ACF_GBB — coverage_buffer only
// inserts it into the buffer when `info.acf_msg_type == acf::kAcfMsgTypeGbb`,
// regardless of what the caller passes for a non-GBB `info`, so a caller
// cannot accidentally cover 8 bytes of timestamp for an ACF_ABB message.
// `info` must already have apply_acf_length_adjustment() applied if the
// caller wants the trailer's length reflected in the coverage — this
// function only serializes whatever AcfMessageInfo (and message_timestamp)
// it is given.
inline std::vector<uint8_t> coverage_buffer(const avtp::StreamId& stream_id,
                                             std::optional<uint32_t> avtp_timestamp,
                                             const acf::AcfMessageInfo& info,
                                             std::optional<uint64_t> message_timestamp,
                                             const std::vector<uint8_t>& payload) {
    const bool is_gbb = (info.acf_msg_type == acf::kAcfMsgTypeGbb);

    std::vector<uint8_t> buf;
    buf.reserve(8 + 4 + acf::kAcfCommonHeaderLen +
                (is_gbb ? acf::kAcfGbbTimestampLen : 0) + payload.size());

    detail::put_u64_be(buf, stream_id.to_u64());
    detail::put_u32_be(buf, avtp_timestamp.value_or(0)); // zero-filled stand-in under NTSCF

    uint8_t hdr[acf::kAcfCommonHeaderLen];
    acf::encode_acf_message_info(info, hdr);
    buf.insert(buf.end(), hdr, hdr + acf::kAcfCommonHeaderLen);

    if (is_gbb) {
        detail::put_u64_be(buf, message_timestamp.value_or(0));
    }

    buf.insert(buf.end(), payload.begin(), payload.end());
    return buf;
}

// compute_crc is coverage_buffer() + crc32() in one call — the usual way a
// caller actually wants this used.
inline uint32_t compute_crc(const avtp::StreamId& stream_id, std::optional<uint32_t> avtp_timestamp,
                             const acf::AcfMessageInfo& info, std::optional<uint64_t> message_timestamp,
                             const std::vector<uint8_t>& payload) {
    return crc32(coverage_buffer(stream_id, avtp_timestamp, info, message_timestamp, payload));
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

// verify_crc recomputes the CRC over
// stream_id/avtp_timestamp/info/message_timestamp/payload and compares it
// against `received_crc` (as decoded from a frame's trailing 4 octets).
// Returns E2eErrc::crc_error — the CRC_ERROR failure path, whose numeric
// TC18 wire error code is e2e::kPociFailureErrorCode /
// e2e::wire_error_code() above — on mismatch.
inline std::error_code verify_crc(const avtp::StreamId& stream_id, std::optional<uint32_t> avtp_timestamp,
                                   const acf::AcfMessageInfo& info, std::optional<uint64_t> message_timestamp,
                                   const std::vector<uint8_t>& payload, uint32_t received_crc) {
    if (compute_crc(stream_id, avtp_timestamp, info, message_timestamp, payload) != received_crc)
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
// mirroring rcp::request::implemented_options_bits' equivalent pattern.
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
// application, same as rcp::request::select_next_due's equivalent
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
// request::RequestLedger::cancel_all directly rather than reimplementing
// cancellation — see rcp/request.hpp's header comment. Returns the count
// cancel_all() itself returns (0 if the feature is disabled, since nothing
// is purged in that case).
inline size_t apply_watchdog_overflow(const regmap::RequestStreamConfig& cfg, RxWatchdog& wd,
                                       request::RequestLedger& ledger) noexcept {
    if (!cfg.rx_wd_safestate_enable) return 0;
    wd.enter_safe_state();
    return ledger.cancel_all(/*non_safestate_only=*/true);
}

// apply_queue_overflow is the analogous rule for a request-queue overrun —
// a distinct trigger from watchdog expiry (extraction §3.8) that can also
// be configured, via cfg.rx_ovrflw_safestate_enable, to drive the endpoint
// into safe state and purge normal requests the same way.
inline size_t apply_queue_overflow(const regmap::RequestStreamConfig& cfg, RxWatchdog& wd,
                                    request::RequestLedger& ledger) noexcept {
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
                                               const request::SequencerTable& sequencers,
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
// a safety-tagged (0x8x, request::RequestRecord::is_safety) record is
// only eligible once `endpoint_in_safe_state` is true.
inline bool may_execute_now(const request::RequestRecord& rec, bool endpoint_in_safe_state) noexcept {
    return !rec.is_safety || endpoint_in_safe_state;
}

} // namespace e2e
} // namespace rcp

// Enable std::error_code construction from rcp::e2e::E2eErrc.
namespace std {
template <>
struct is_error_code_enum<rcp::e2e::E2eErrc> : true_type {};
} // namespace std
