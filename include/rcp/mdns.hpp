// fusa:req REQ-MDNS-001
// fusa:req REQ-MDNS-002
// fusa:req REQ-MDNS-003
// fusa:req REQ-MDNS-004
// fusa:req REQ-MDNS-005
// fusa:req REQ-MDNS-006
// fusa:req REQ-MDNS-007
// fusa:req REQ-MDNS-008

// mDNS/DNS-SD host:port discovery (RFC 6762 + RFC 6763) for the UDP/IP
// transport variant specifically.
//
// ROADMAP.md milestone 58, "Auxiliary Transport & Cross-Cutting Rebind
// (v2.14.0)": this header is ADAPTed, per the Satellite Package Disposition
// table's entry for `mdns.hpp` — its scope is narrowed rather than carried
// forward unchanged. The OPEN Alliance TC18 Remote Control Protocol
// Specification v0.5.1_RC already defines its own wire-level discovery over
// an Ethernet-addressed stream (v2.2.0, rcp/discovery.hpp) that needs no
// name service at all: a native-Ethernet-framed RC Server is found by
// broadcasting a discovery request on the link, not by resolving a service
// name. mDNS/DNS-SD only earns its keep here when host:port discovery is
// separately needed for the IEEE1722-over-UDP/IP path (v2.13.0,
// rcp/udp.hpp), where "the link" is an IP network with no broadcast-domain
// guarantee and a server's UDP socket has no MAC-address-derived identity a
// client could otherwise rendezvous on.
//
// This is still an abstract interface, not a working mDNS responder — full
// mDNS requires platform APIs (avahi-client/systemd-resolved on Linux,
// dns_sd on macOS) not available in a header-only library. StaticDiscoverer
// below exists for testing and static configuration.
#pragma once

#include "rcp.hpp" // for rcp::Context — see this header's own scope note above
#include "avtp.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace rcp {
namespace mdns {

// ── ServerInfo ────────────────────────────────────────────────────────────────
// What a discovered (or announced) udp::Server is reachable at. `stream_id`
// replaces the old ZoneInfo::zone — it is the same opaque per-sender
// identity rcp/avtp.hpp's StreamId models, carried here as its uint64_t
// form (avtp::StreamId::to_u64()) for the same "opaque index" convention
// rcp/watchdog.hpp's Manager and rcp/regmap.hpp's Ep0 already use.

struct ServerInfo {
    uint64_t    stream_key = 0;
    std::string host;
    uint16_t    port = 0;
    std::string instance_name; // e.g. "0002xxxxxxxx1234.myhost._rcp._udp.local"
};

// ── DiscoveryEvent ────────────────────────────────────────────────────────────

enum class EventType : uint8_t { Added = 0, Removed = 1 };

struct DiscoveryEvent {
    EventType  event;
    ServerInfo info;
};

// ── Discoverer ────────────────────────────────────────────────────────────────

// Discoverer is the abstract interface for mDNS-based udp::Server discovery.
// Implementations call the registered callback for each add/remove event.
class Discoverer {
public:
    using Callback = std::function<void(const DiscoveryEvent&)>;

    virtual ~Discoverer() = default;

    // start begins discovery; calls cb for each event until stop() is called.
    virtual std::error_code start(Callback cb) = 0;

    // stop terminates discovery.
    virtual void stop() = 0;
};

// ── StaticDiscoverer ─────────────────────────────────────────────────────────

// StaticDiscoverer emits a fixed set of ServerInfo entries immediately on
// start(). Suitable for testing and static configuration where the set of
// UDP/IP servers is known in advance and does not need real mDNS resolution.
class StaticDiscoverer final : public Discoverer {
public:
    explicit StaticDiscoverer(std::vector<ServerInfo> servers)
        : servers_(std::move(servers)) {}

    std::error_code start(Callback cb) override {
        stopped_.store(false);
        for (auto& s : servers_) {
            if (stopped_.load()) break;
            cb({EventType::Added, s});
        }
        return {};
    }

    void stop() override { stopped_.store(true); }

private:
    std::vector<ServerInfo> servers_;
    std::atomic<bool>       stopped_{false};
};

// ── Announcer (passive interface) ─────────────────────────────────────────────

// Announcer registers a ServerInfo record in the local mDNS responder so
// that a udp::Server bound to `info.host`:`info.port` becomes discoverable.
// Platform implementations call the underlying mDNS API (Avahi, dns_sd, etc.)
class Announcer {
public:
    virtual ~Announcer() = default;

    virtual std::error_code announce(const ServerInfo& info) = 0;
    virtual void withdraw(uint64_t stream_key) = 0;
};

// ── Helpers ───────────────────────────────────────────────────────────────────

// make_instance_name builds a standard _rcp._udp.local service instance
// name from a stream_id and hostname, using stream_id's hex-encoded uint64_t
// form as the unique-per-sender label (the same identity avtp::StreamId
// already models — no zone/instance name of its own to draw from now).
inline std::string make_instance_name(avtp::StreamId stream_id, const std::string& hostname) {
    std::ostringstream oss;
    oss << std::hex << std::uppercase;
    oss.width(16);
    oss.fill('0');
    oss << stream_id.to_u64();
    return oss.str() + "." + hostname + "._rcp._udp.local";
}

inline std::unique_ptr<StaticDiscoverer> new_static_discoverer(std::vector<ServerInfo> servers) {
    return std::make_unique<StaticDiscoverer>(std::move(servers));
}

} // namespace mdns
} // namespace rcp
