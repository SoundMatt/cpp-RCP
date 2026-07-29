// fusa:req REQ-TSN-001
// fusa:req REQ-TSN-002
// fusa:req REQ-TSN-003
// fusa:req REQ-TSN-004
// fusa:req REQ-TSN-005
// fusa:req REQ-TSN-006

// IEEE 802.1Qbv-aware priority hint for the UDP/IP transport, keyed off the
// specification's own execution-priority ordering across request kinds.
//
// ROADMAP.md milestone 58, "Auxiliary Transport & Cross-Cutting Rebind
// (v2.14.0)": this header is ADAPTed, per the Satellite Package
// Disposition table's entry for `tsn.hpp` — this file's own pre-v2.14.0
// header comment already flagged this exact rebind as pending. The old
// 3-level rcp::Priority enum (rcp/prioqueue.hpp, DEPRECATEd outright per
// the disposition table, no TC18 analog) is gone; PCPMap below instead maps
// rcp::request::RequestCategory (v2.5.0) — the seven-way cancellation >
// triggered > timed > compound > compound-wait > chained > standard
// ordering extraction §3.14 already defines for simultaneously-due
// requests — onto IEEE 802.1p PCP classes. apply_priority() is a free
// function taking a raw socket fd, same as the pre-replacement Controller
// wrapper's only real dependency (its own header comment already noted it
// "depends only on the socket fd passed in, not on any concrete transport
// header"); there is no unified client-side send() chokepoint yet to wrap
// (that unification, if any, does not land until the CLI/capi/adapt
// rebuilds at v2.16.0 — see rcp/authz.hpp's equivalent v2.11.0 note), so
// this is a primitive an embedding calls directly before udp::Client::
// request, the same "primitives driven by the embedding application"
// pattern every Phase 14/15 header has used since v2.9.0.
//
// Full 802.1Qbv gate scheduling requires a TSN-capable NIC and kernel
// >= 4.15; on standard hardware this provides best-effort priority mapping
// only. Where genuine IEEE 1722 stream reservation (802.1Qat SRP) is
// available on the platform/NIC, prefer it over this socket-level
// SO_PRIORITY hint — SRP carries an admission-controlled bandwidth
// reservation end to end, while SO_PRIORITY only ever influences local
// egress queueing on this host; this header does not implement SRP itself,
// same "primitive, not the full mechanism" scope as this file's other
// pre-v2.14.0 disclaimers.
#pragma once

#include "rcp.hpp" // for std::error_code convention only — see this header's own scope note above
#include "request.hpp"

#include <cerrno>
#include <cstdint>
#include <system_error>

#if defined(__linux__)
#  include <sys/socket.h>
#  define RCP_TSN_SO_PRIORITY 1
#endif

namespace rcp {
namespace tsn {

// ── PCPMap ────────────────────────────────────────────────────────────────────
// Maps each rcp::request::RequestCategory to an IEEE 802.1p PCP value
// (0-7). Category rank 0 (Cancellation) is the highest execution priority
// (extraction §3.14) and gets the highest default PCP; rank 6 (Standard,
// the mandatory baseline kind) gets the lowest.
struct PCPMap {
    uint8_t cancellation  = 7; // RequestCategory::Cancellation  (rank 0, highest)
    uint8_t triggered     = 6; // RequestCategory::Triggered     (rank 1)
    uint8_t timed         = 5; // RequestCategory::Timed         (rank 2)
    uint8_t compound      = 4; // RequestCategory::Compound      (rank 3)
    uint8_t compound_wait = 3; // RequestCategory::CompoundWait  (rank 4)
    uint8_t chained       = 2; // RequestCategory::Chained       (rank 5)
    uint8_t standard      = 1; // RequestCategory::Standard      (rank 6, lowest)
};

inline PCPMap default_pcp_map() { return PCPMap{}; }

inline uint8_t pcp_for(const PCPMap& m, request::RequestCategory c) noexcept {
    switch (c) {
    case request::RequestCategory::Cancellation:  return m.cancellation;
    case request::RequestCategory::Triggered:     return m.triggered;
    case request::RequestCategory::Timed:         return m.timed;
    case request::RequestCategory::Compound:      return m.compound;
    case request::RequestCategory::CompoundWait:  return m.compound_wait;
    case request::RequestCategory::Chained:       return m.chained;
    case request::RequestCategory::Standard:
    default:                                       return m.standard;
    }
}

// ── TSNConfig ─────────────────────────────────────────────────────────────────

struct TSNConfig {
    PCPMap  pcp_map;
    int     vlan_id  = 0; // 0 = untagged
    int     cycle_ns = 0; // 802.1Qbv gate cycle in nanoseconds (0 = disabled)
};

inline TSNConfig default_tsn_config() { return TSNConfig{}; }

// ── apply_priority ────────────────────────────────────────────────────────────
// apply_priority sets SO_PRIORITY on `fd` to the PCP value `cfg.pcp_map`
// assigns `category`, so the egress qdisc places the next datagram sent on
// this socket into the correct 802.1p traffic class. A caller invokes this
// once per outbound request, immediately before udp::Client::request, with
// the RequestCategory that request's decoded (or about-to-be-encoded)
// request_type opcode maps to via rcp::request::category_of.
//
// Outside RCP_TSN_SO_PRIORITY (non-Linux platforms, where this hint has no
// portable socket-option equivalent) this is a no-op success rather than a
// failure — the caller's send path is unaffected either way, same as the
// pre-replacement Controller wrapper's own `#if defined(...)` gating.
inline std::error_code apply_priority(int fd, const TSNConfig& cfg,
                                       request::RequestCategory category) noexcept {
    if (fd < 0) return std::make_error_code(std::errc::invalid_argument);
#if defined(RCP_TSN_SO_PRIORITY)
    int pcp = static_cast<int>(pcp_for(cfg.pcp_map, category));
    if (::setsockopt(fd, SOL_SOCKET, SO_PRIORITY, &pcp, sizeof(pcp)) < 0)
        return std::error_code(errno, std::generic_category());
#else
    (void)cfg;
    (void)category;
#endif
    return {};
}

} // namespace tsn
} // namespace rcp
