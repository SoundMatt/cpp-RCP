// fusa:req REQ-DISC-001
// fusa:req REQ-DISC-002
// fusa:req REQ-DISC-003
// fusa:req REQ-DISC-004
// fusa:req REQ-DISC-005
// fusa:req REQ-DISC-006
// fusa:req REQ-DISC-007
// fusa:req REQ-DISC-008
// fusa:req REQ-DISC-009

// RC Server discovery — the broadcastable, byte_bus_id-0 read request every
// OPEN Alliance TC18 Remote Control Protocol Specification v0.5.1_RC server
// must answer regardless of lifecycle state, plus discovery-stream claiming
// for configuration (extraction §3.1, §3.5).
//
// ROADMAP.md milestone 46, "Discovery (v2.2.0)": this header rides directly
// on rcp/avtp.hpp's NTSCF framing and rcp/acf.hpp's ACF_ABB message format
// and byte_bus_id addressing (v2.0.0, split from the original rcp/wire.hpp
// per RELAY spec §13.7.2)
// — it needs no changes to that codec — queries rcp/lifecycle.hpp's
// ServerState to decide whether an incoming discovery request is eligible to
// claim the discovery stream (v2.1.0), and targets rcp/regmap.hpp's EP0
// (byte_bus_id 0, register-map address 0 — the general bootstrap/magic-number
// field region) as the thing being read (also v2.1.0). This module does not
// itself implement a byte-level serialization of the whole register map to
// and from the wire; per regmap.hpp's own header comment that remains a
// later-milestone concern. It only fixes the address a discovery request
// must carry so that later serialization work has a stable, already-decided
// target to hook into.
//
// Field names and behavior below implement TC18's *behavior* as described in
// an internal structured extraction of the specification named above; no
// text from that document is reproduced here. The concrete claim-state
// machine and default timeout chosen in this file are this implementation's
// own encoding of that behavior, same as the equivalent disclaimers in
// rcp/avtp.hpp, rcp/acf.hpp, rcp/lifecycle.hpp, and rcp/regmap.hpp.
#pragma once

#include <rcp/acf.hpp>
#include <rcp/avtp.hpp>
#include <rcp/lifecycle.hpp>
#include <rcp/regmap.hpp>

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace rcp {
namespace discovery {

// ── Discovery addressing ─────────────────────────────────────────────────────
// A discovery request targets byte_bus_id 0 — the same id regmap::kEp0 uses
// — and, within whatever EP0 exposes, register-map address 0: the general
// bootstrap fields, magic number first among them (extraction §3.5).

constexpr avtp::ByteBusId kDiscoveryByteBusId = static_cast<avtp::ByteBusId>(regmap::kEp0);
constexpr uint32_t        kDiscoveryRegisterAddress = 0;

// A discovery read's default response size: just enough to carry the 32-bit
// magic number at register-map address 0. Callers reading more of the
// bootstrap block may pass a larger read_size to make_discovery_request.
constexpr uint16_t kDiscoveryDefaultReadSize = sizeof(uint32_t);

// ── Errors ────────────────────────────────────────────────────────────────────

enum class DiscoveryErrc : int {
    // A TSCF-headed discovery request was received. Discovery is NTSCF-only
    // (extraction §3.5); such a request is dropped rather than answered or
    // partially processed.
    tscf_headed_request_dropped = 1,
};

inline const std::error_category& discovery_category() noexcept {
    struct Cat : std::error_category {
        const char* name() const noexcept override { return "rcp.discovery"; }
        std::string message(int ev) const override {
            switch (static_cast<DiscoveryErrc>(ev)) {
            case DiscoveryErrc::tscf_headed_request_dropped:
                return "rcp/discovery: TSCF-headed discovery request dropped (discovery is NTSCF-only)";
            default:
                return "rcp/discovery: unknown error";
            }
        }
    };
    static Cat instance;
    return instance;
}

inline std::error_code make_error_code(DiscoveryErrc e) noexcept {
    return {static_cast<int>(e), discovery_category()};
}

// ── Discovery request framing ────────────────────────────────────────────────
// make_discovery_request builds the ACF_ABB-level header for a discovery
// read: unconditional read (op=false), targeting byte_bus_id 0
// (extraction §3.5). It rides on acf::make_standard_request unchanged —
// discovery is the mandatory standard request kind addressed at a fixed
// endpoint, not a distinct wire-level message shape of its own.
inline acf::AcfMessageInfo make_discovery_request(uint8_t transaction_num,
                                                    uint16_t read_size = kDiscoveryDefaultReadSize) noexcept {
    return acf::make_standard_request(kDiscoveryByteBusId, transaction_num,
                                       /*write=*/false, read_size);
}

// encode_discovery_request wraps make_discovery_request's ACF_ABB message in
// an NTSCF header addressed to `stream_id`. Discovery requests are
// NTSCF-only and broadcastable (extraction §3.5) — this function never
// produces a TSCF-headed frame, so there is no way to misuse it into
// building the kind of request decode_discovery_request below must drop.
inline std::vector<uint8_t> encode_discovery_request(const avtp::StreamId& stream_id,
                                                       uint16_t sequence_num,
                                                       uint8_t transaction_num,
                                                       uint16_t read_size = kDiscoveryDefaultReadSize) {
    const auto info = make_discovery_request(transaction_num, read_size);
    const auto acf_msg = acf::encode_acf_abb(info, {});

    avtp::NtscfHeader hdr;
    hdr.stream_id           = stream_id;
    hdr.sequence_num        = sequence_num;
    hdr.control_data_length = static_cast<uint16_t>(acf_msg.size());

    auto out = avtp::encode_ntscf_header(hdr);
    out.insert(out.end(), acf_msg.begin(), acf_msg.end());
    return out;
}

// decode_discovery_request decodes a raw AVTPDU frame as a discovery
// request. Per extraction §3.5, discovery is NTSCF-only: if the frame's
// AVTPDU subtype byte is TSCF, this function returns
// DiscoveryErrc::tscf_headed_request_dropped without decoding further —
// modeling "drop" as a decode failure the caller cannot accidentally ignore
// the way it could ignore a boolean flag on an otherwise-successful decode.
inline std::error_code decode_discovery_request(const uint8_t* buf, size_t len,
                                                  avtp::NtscfHeader& out_hdr,
                                                  acf::AcfMessageInfo& out_info,
                                                  std::vector<uint8_t>& out_payload) {
    if (len < 1) return avtp::make_error_code(avtp::AvtpErrc::short_buffer);
    if (buf[0] == avtp::kSubtypeTscf) {
        return make_error_code(DiscoveryErrc::tscf_headed_request_dropped);
    }

    auto ec = avtp::decode_ntscf_header(buf, len, out_hdr);
    if (ec) return ec;

    const size_t acf_off = avtp::kNtscfHeaderLen;
    if (len < acf_off) return avtp::make_error_code(avtp::AvtpErrc::short_buffer);
    return acf::decode_acf_abb(buf + acf_off, len - acf_off, out_info, out_payload);
}

// should_answer_discovery documents, as a single always-true call site
// rather than an implicit assumption scattered across a lifecycle switch
// statement, that a server answers a (NTSCF-headed) discovery request in
// *every* lifecycle state — including RCP_CONFIGURED, where the claiming
// mechanism below no longer applies but discovery reads still must
// (extraction §3.1, §3.5's mandatory-baseline requirement).
constexpr bool should_answer_discovery(lifecycle::ServerState /*state*/) noexcept {
    return true;
}

// ── Discovery-stream claiming ────────────────────────────────────────────────
// DiscoveryClaim models the reservation described in extraction §3.5: the
// first discovery request a server receives while it is in HW_UNCONFIGURED
// or HW_CONFIGURED reserves the discovery stream for that client's
// subsequent configuration writes. If no configuration request follows
// within Discovery_TimeOut, the reservation lapses and any client's next
// discovery request may claim it anew. A claim never blocks reads — every
// client, holder or not, keeps getting discovery reads answered — it only
// gates whether a *configuration* (write) request is allowed to proceed.
//
// Time is supplied by the caller as a std::chrono::steady_clock::time_point
// rather than read internally, so embedding code (and tests) control the
// clock explicitly instead of this class racing a real timer.
class DiscoveryClaim {
public:
    using Clock     = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    // Discovery_TimeOut default, ~20 ms per extraction §3.5. The
    // specification calls this out as configurable; this is this
    // implementation's chosen default, not a mandated value.
    static constexpr std::chrono::milliseconds kDefaultTimeout{20};

    // Outcome of a discovery request arriving at on_discovery_request.
    enum class ClaimOutcome {
        NotEligible, // server_state is neither HwUnconfigured nor HwConfigured; no claim made
        Claimed,     // no active claim existed (or the prior one had lapsed); `client` now holds it
        AlreadyHeld, // `client` already held the still-active claim; unchanged
        HeldByOther, // a different client's claim is still active; this request did not claim
    };

    explicit DiscoveryClaim(std::chrono::milliseconds timeout = kDefaultTimeout) noexcept
        : timeout_(timeout) {}

    // on_discovery_request should be called for every discovery request a
    // server accepts (i.e. every one decode_discovery_request did not drop),
    // tagged with the requesting client's opaque id, the server's current
    // lifecycle state, and the current time. The claiming mechanism itself
    // only applies in HW_UNCONFIGURED/HW_CONFIGURED (extraction §3.5); in
    // RCP_CONFIGURED this always reports NotEligible without touching any
    // existing claim, since by that point configuration is already done.
    // This call never affects whether the request's *read* gets answered —
    // callers must answer it regardless of the outcome returned here.
    ClaimOutcome on_discovery_request(size_t client, lifecycle::ServerState server_state,
                                       TimePoint now) noexcept {
        if (server_state != lifecycle::ServerState::HwUnconfigured &&
            server_state != lifecycle::ServerState::HwConfigured) {
            return ClaimOutcome::NotEligible;
        }
        if (holder_.has_value() && !lapsed(now)) {
            return (*holder_ == client) ? ClaimOutcome::AlreadyHeld : ClaimOutcome::HeldByOther;
        }
        holder_     = client;
        claimed_at_ = now;
        return ClaimOutcome::Claimed;
    }

    // may_configure reports whether `client` currently holds an active,
    // unlapsed claim — i.e. is presently allowed to issue a configuration
    // (write) request on the discovery stream. It does not itself lapse or
    // consume the claim; it is a pure query.
    bool may_configure(size_t client, TimePoint now) const noexcept {
        return holder_.has_value() && *holder_ == client && !lapsed(now);
    }

    // on_configuration_request should be called when a configuration
    // request arrives on the discovery stream. It returns true iff `client`
    // held an active claim at `now`, in which case that claim is consumed
    // (released) — a spent reservation is not carried forward, so the very
    // next discovery request, from any client, is free to claim the stream
    // again. A request from a client that does not hold the active claim
    // returns false and leaves the existing claim (if any, held by someone
    // else) untouched, so the real holder's window is not disturbed by an
    // unrelated client's rejected attempt.
    bool on_configuration_request(size_t client, TimePoint now) noexcept {
        if (!may_configure(client, now)) return false;
        holder_.reset();
        return true;
    }

    // has_active_claim / current_holder are read-only introspection for
    // callers (e.g. admin/observability surfaces) that want to report claim
    // state without being able to mutate it.
    bool has_active_claim(TimePoint now) const noexcept {
        return holder_.has_value() && !lapsed(now);
    }
    std::optional<size_t> current_holder(TimePoint now) const noexcept {
        return has_active_claim(now) ? holder_ : std::nullopt;
    }

private:
    bool lapsed(TimePoint now) const noexcept {
        return !holder_.has_value() || (now - claimed_at_) >= timeout_;
    }

    std::chrono::milliseconds timeout_;
    std::optional<size_t>     holder_;
    TimePoint                 claimed_at_{};
};

} // namespace discovery
} // namespace rcp

// Enable std::error_code construction from rcp::discovery::DiscoveryErrc.
namespace std {
template <>
struct is_error_code_enum<rcp::discovery::DiscoveryErrc> : true_type {};
} // namespace std
