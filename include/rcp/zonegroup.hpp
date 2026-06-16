// fusa:req REQ-ZG-001
// fusa:req REQ-ZG-002
// fusa:req REQ-ZG-003
// fusa:req REQ-ZG-004
// fusa:req REQ-ZG-005
// fusa:req REQ-ZG-006

// Atomic multi-zone command broadcast with typed zone group sets (v0.21.0).
//
// ZoneGroup is a value type enumerating a subset of zones.
// GroupRegistry::send_group() dispatches one command to every zone in the
// group concurrently and collects per-zone responses into a GroupResponse.
#pragma once

#include "rcp.hpp"

#include <future>
#include <initializer_list>
#include <map>
#include <memory>
#include <vector>

namespace rcp {
namespace zonegroup {

// ── ZoneGroup ─────────────────────────────────────────────────────────────────

class ZoneGroup {
public:
    ZoneGroup() = default;

    ZoneGroup(std::initializer_list<Zone> zones)
        : zones_(zones.begin(), zones.end()) {}

    static ZoneGroup all() {
        return ZoneGroup{Zone::FrontLeft, Zone::FrontRight,
                         Zone::RearLeft,  Zone::RearRight,
                         Zone::Central};
    }

    static ZoneGroup rear() {
        return ZoneGroup{Zone::RearLeft, Zone::RearRight};
    }

    static ZoneGroup front() {
        return ZoneGroup{Zone::FrontLeft, Zone::FrontRight};
    }

    const std::vector<Zone>& zones() const noexcept { return zones_; }
    bool empty() const noexcept { return zones_.empty(); }
    size_t size() const noexcept { return zones_.size(); }

    void add(Zone z) { zones_.push_back(z); }

private:
    std::vector<Zone> zones_;
};

// ── GroupResponse ─────────────────────────────────────────────────────────────

struct ZoneResult {
    Response        response;
    std::error_code error;
};

struct GroupResponse {
    std::map<Zone, ZoneResult> results;

    // ok returns true if every zone response succeeded.
    bool ok() const {
        for (auto& kv : results) {
            if (kv.second.error || kv.second.response.status != ResponseStatus::OK)
                return false;
        }
        return true;
    }

    // errors returns zones that failed.
    std::vector<Zone> errors() const {
        std::vector<Zone> out;
        for (auto& kv : results) {
            if (kv.second.error) out.push_back(kv.first);
        }
        return out;
    }
};

// ── GroupRegistry ─────────────────────────────────────────────────────────────

// GroupRegistry wraps any rcp::Registry and adds send_group() broadcast.
class GroupRegistry {
public:
    explicit GroupRegistry(rcp::Registry& reg) : reg_(reg) {}

    // send_group dispatches cmd to every zone in group concurrently.
    // Uses the shared context deadline for all parallel sends.
    // Priority::Critical commands ignore any ErrBusy backpressure.
    GroupResponse send_group(const Context&   ctx,
                              const ZoneGroup& group,
                              const Command&   cmd) {
        GroupResponse result;

        // Launch one future per zone.
        std::vector<std::pair<Zone, std::future<ZoneResult>>> futures;
        futures.reserve(group.size());

        for (Zone z : group.zones()) {
            futures.emplace_back(z, std::async(std::launch::async,
                [this, &ctx, &cmd, z]() -> ZoneResult {
                    std::shared_ptr<rcp::Controller> ctrl;
                    auto ec = reg_.lookup(z, ctrl);
                    if (ec) return {Response{}, ec};
                    Command zone_cmd = cmd;
                    zone_cmd.zone   = z;
                    zone_cmd.id     = 0; // registry assigns id
                    Response resp;
                    ec = ctrl->send(ctx, zone_cmd, resp);
                    return {resp, ec};
                }));
        }

        for (auto& [z, fut] : futures) {
            result.results[z] = fut.get();
        }
        return result;
    }

private:
    rcp::Registry& reg_;
};

// Named group constants.
inline ZoneGroup group_all()   { return ZoneGroup::all(); }
inline ZoneGroup group_rear()  { return ZoneGroup::rear(); }
inline ZoneGroup group_front() { return ZoneGroup::front(); }

} // namespace zonegroup
} // namespace rcp
