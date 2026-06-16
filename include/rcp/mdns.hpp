// fusa:req REQ-MDNS-001
// fusa:req REQ-MDNS-002
// fusa:req REQ-MDNS-003
// fusa:req REQ-MDNS-004
// fusa:req REQ-MDNS-005
// fusa:req REQ-MDNS-006
// fusa:req REQ-MDNS-007
// fusa:req REQ-MDNS-008

// mDNS/DNS-SD zone controller discovery (RFC 6762 + RFC 6763).
//
// Provides the abstract Discoverer interface and a StaticDiscoverer for testing.
// A full mDNS responder (Avahi-compatible) requires platform APIs not available
// in a header-only library. Use the avahi-client or systemd-resolved backend on
// Linux, or dns_sd on macOS, and wrap it with this Discoverer interface.
#pragma once

#include "rcp.hpp"

#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace rcp {
namespace mdns {

// ── ZoneInfo ─────────────────────────────────────────────────────────────────

struct ZoneInfo {
    Zone        zone;
    std::string host;
    uint16_t    port = 0;
    std::string instance_name; // e.g. "front-left.myhost._rcp._udp.local"
};

// ── DiscoveryEvent ────────────────────────────────────────────────────────────

enum class EventType : uint8_t { Added = 0, Removed = 1 };

struct DiscoveryEvent {
    EventType event;
    ZoneInfo  info;
};

// ── Discoverer ────────────────────────────────────────────────────────────────

// Discoverer is the abstract interface for mDNS-based zone discovery.
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

// StaticDiscoverer emits a fixed set of ZoneInfo entries immediately on start().
// Suitable for testing and static configuration.
class StaticDiscoverer final : public Discoverer {
public:
    explicit StaticDiscoverer(std::vector<ZoneInfo> zones)
        : zones_(std::move(zones)) {}

    std::error_code start(Callback cb) override {
        stopped_.store(false);
        for (auto& z : zones_) {
            if (stopped_.load()) break;
            cb({EventType::Added, z});
        }
        return {};
    }

    void stop() override { stopped_.store(true); }

private:
    std::vector<ZoneInfo> zones_;
    std::atomic<bool>     stopped_{false};
};

// ── Announcer (passive interface) ─────────────────────────────────────────────

// Announcer registers a ZoneInfo record in the local mDNS responder.
// Platform implementations call the underlying mDNS API (Avahi, dns_sd, etc.)
class Announcer {
public:
    virtual ~Announcer() = default;

    virtual std::error_code announce(const ZoneInfo& info) = 0;
    virtual void withdraw(Zone z) = 0;
};

// ── Helpers ───────────────────────────────────────────────────────────────────

// make_instance_name builds the standard _rcp._udp.local service instance name.
inline std::string make_instance_name(Zone z, const std::string& hostname) {
    return rcp::to_string(z) + "." + hostname + "._rcp._udp.local";
}

inline std::unique_ptr<StaticDiscoverer> new_static_discoverer(std::vector<ZoneInfo> zones) {
    return std::make_unique<StaticDiscoverer>(std::move(zones));
}

} // namespace mdns
} // namespace rcp
