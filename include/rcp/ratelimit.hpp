// fusa:req REQ-RL-001
// fusa:req REQ-RL-002
// fusa:req REQ-RL-003
// fusa:req REQ-RL-004
// fusa:req REQ-RL-005
// fusa:req REQ-RL-006
// fusa:req REQ-RL-007
// fusa:req REQ-RL-008

// Per-endpoint token-bucket admission control (SG-009, H-009) — the
// client-side defense the specification leaves overrun-avoidance to
// (extraction §3.14): the specification models a request-queue overrun as
// an unconditional drop with no server-side flow control, so this package
// stays exactly where the pre-replacement design already put it, one layer
// above that unconditional drop.
//
// ROADMAP.md milestone 55, "Authorization & Admission-Control Rebind
// (v2.11.0)": this header ADAPTs its pre-replacement content per the
// Satellite Package Disposition table's entry for `ratelimit.hpp` — the
// token-bucket admission-control concept and `Config` shape survive,
// rebound from one bucket per `Controller` instance (i.e., implicitly one
// per pre-replacement `Zone`) onto one bucket per (target server, target
// endpoint) admission-control domain, addressed the same
// stream-key/`avtp::ByteBusId` way rcp/authz.hpp (this milestone) and
// rcp/watchdog.hpp (v2.10.0) already address those concepts — see either
// header's own comment for the "opaque uint64_t stream key, typically
// avtp::StreamId::to_u64()" convention this file reuses unchanged.
//
// The pre-replacement `Priority::Critical` bypass has no analog in the
// target model — `rcp/prioqueue.hpp`'s whole client-side-priority-wrapper
// concept is DEPRECATE per the disposition table, not carried forward.
// The traffic class that must not be throttled here is instead
// `rcp::request::is_safety_variant`'s three MSB-set (0x8x) safety-tagged
// opcodes (v2.6.0): a safety-tagged request is what ultimately drives an
// endpoint through its configured safe state once it executes (see
// rcp/e2e.hpp's `may_execute_now`), making it the closest real analog to
// "must not be dropped by an admission-control layer" the roadmap calls
// for — `Manager::admit` below takes that classification as an explicit
// caller-supplied `is_safety_tagged` argument rather than re-deriving it,
// since a caller evaluating admission for a not-yet-fully-decoded request
// is the one place that classification is known.
//
// Like every other Phase 14 primitive header, this module has no clock,
// thread, or I/O of its own: `TokenBucket::take`/`Manager::admit` take an
// explicit `now_ms` rather than reading a clock internally, the same
// "explicit time, no clock of its own" convention rcp/e2e.hpp's
// `RxWatchdog` and rcp/watchdog.hpp's `StreamWatchdog` already use —
// deciding what clock feeds `now_ms` is the embedding application's job.
//
// Field names and behavior below implement TC18's *behavior* as described
// in an internal structured extraction of the specification named above;
// no text from that document is reproduced here. The token-bucket
// refill/burst mechanics and the per-endpoint-domain keying convention
// chosen in this file are this implementation's own encoding, same as the
// equivalent disclaimers in rcp/authz.hpp, rcp/e2e.hpp, and
// rcp/watchdog.hpp.
#pragma once

#include <rcp/avtp.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <system_error>

namespace rcp {
namespace ratelimit {

// ── Errors ────────────────────────────────────────────────────────────────────

enum class RateLimitErrc : int {
    admission_denied = 1, // the target endpoint's token bucket is exhausted
};

inline const std::error_category& ratelimit_category() noexcept {
    struct Cat : std::error_category {
        const char* name() const noexcept override { return "rcp.ratelimit"; }
        std::string message(int ev) const override {
            if (static_cast<RateLimitErrc>(ev) == RateLimitErrc::admission_denied)
                return "rcp/ratelimit: admission denied — endpoint token bucket exhausted";
            return "rcp/ratelimit: unknown error";
        }
    };
    static Cat instance;
    return instance;
}

inline std::error_code make_error_code(RateLimitErrc e) noexcept {
    return {static_cast<int>(e), ratelimit_category()};
}

inline const std::error_code ErrAdmissionDenied = make_error_code(RateLimitErrc::admission_denied);

// ── Config ────────────────────────────────────────────────────────────────────

struct Config {
    double rate               = 100.0; // sustained token refill rate (tokens/second)
    int    burst              = 20;    // maximum token accumulation
    bool   exempt_safety_tagged = true; // if true, is_safety_tagged admit() calls bypass the bucket
};

// default_config returns ASIL-B recommended values.
inline Config default_config() {
    return Config{};
}

// ── EndpointKey ───────────────────────────────────────────────────────────────
// One admission-control domain: the (target server, target endpoint) pair
// a request-queue's finite capacity actually belongs to. `stream_key` is
// the same opaque, caller-derived uint64_t rcp/authz.hpp and
// rcp/watchdog.hpp key on; `byte_bus_id` is only unique within its owning
// stream (extraction §2.1), so the pair together, not either field alone,
// identifies one domain.

struct EndpointKey {
    uint64_t        stream_key  = 0;
    avtp::ByteBusId byte_bus_id = 0;
};

inline bool operator<(const EndpointKey& a, const EndpointKey& b) noexcept {
    if (a.stream_key != b.stream_key) return a.stream_key < b.stream_key;
    return a.byte_bus_id < b.byte_bus_id;
}

inline bool operator==(const EndpointKey& a, const EndpointKey& b) noexcept {
    return a.stream_key == b.stream_key && a.byte_bus_id == b.byte_bus_id;
}

// ── TokenBucket ───────────────────────────────────────────────────────────────
// One domain's own bucket state. take() refills proportionally to elapsed
// time since the previous call before consuming a token, capped at
// cfg.burst; the very first call establishes the baseline without
// refilling (nothing has elapsed yet against an unset last-seen time),
// mirroring rcp/e2e.hpp's RxWatchdog::kick bootstrap handling.
class TokenBucket {
public:
    explicit TokenBucket(Config cfg) noexcept
        : cfg_(cfg), tokens_(static_cast<double>(cfg.burst)) {}

    bool take(uint64_t now_ms) noexcept {
        if (has_last_) {
            double secs = static_cast<double>(now_ms - last_ms_) / 1000.0;
            tokens_    += secs * cfg_.rate;
            if (tokens_ > static_cast<double>(cfg_.burst))
                tokens_ = static_cast<double>(cfg_.burst);
        }
        has_last_ = true;
        last_ms_  = now_ms;
        if (tokens_ < 1.0) return false;
        tokens_ -= 1.0;
        return true;
    }

private:
    Config   cfg_;
    bool     has_last_ = false;
    uint64_t last_ms_  = 0;
    double   tokens_;
};

// ── Manager ───────────────────────────────────────────────────────────────────
// Multiplexes one TokenBucket per EndpointKey, created lazily on first use
// with this Manager's shared Config — the multi-domain layer the roadmap
// calls for, mirroring rcp/watchdog.hpp's Manager multiplexing one
// StreamWatchdog per stream_key.
class Manager {
public:
    explicit Manager(Config cfg = default_config()) : cfg_(cfg) {}

    // admit evaluates one inbound request's admission against `key`'s
    // token bucket. A request the caller marks is_safety_tagged bypasses
    // the bucket entirely when cfg.exempt_safety_tagged is set — see this
    // file's header comment for why that classification, not the
    // pre-replacement Priority::Critical, is what must not be throttled
    // here. Returns ErrAdmissionDenied, unchanged, when the bucket is
    // exhausted; other domains' buckets are unaffected by that outcome.
    std::error_code admit(const EndpointKey& key, bool is_safety_tagged, uint64_t now_ms) {
        if (cfg_.exempt_safety_tagged && is_safety_tagged) return {};
        auto it = buckets_.find(key);
        if (it == buckets_.end())
            it = buckets_.emplace(key, TokenBucket(cfg_)).first;
        return it->second.take(now_ms) ? std::error_code{} : ErrAdmissionDenied;
    }

    // reset discards a domain's bucket state; its next admit() call
    // rebuilds it from this Manager's shared Config as if never seen.
    void reset(const EndpointKey& key) noexcept { buckets_.erase(key); }

    size_t domain_count() const noexcept { return buckets_.size(); }

private:
    Config                          cfg_;
    std::map<EndpointKey, TokenBucket> buckets_;
};

} // namespace ratelimit
} // namespace rcp

namespace std {
template <>
struct is_error_code_enum<rcp::ratelimit::RateLimitErrc> : true_type {};
} // namespace std
