// fusa:req REQ-CFG-001
// fusa:req REQ-CFG-002
// fusa:req REQ-CFG-003
// fusa:req REQ-CFG-004
// fusa:req REQ-CFG-005
// fusa:req REQ-CFG-006

// Zone registry loader from JSON/YAML configuration files (v0.28.0).
//
// ConfigLoader parses a JSON zone-registry manifest and populates an
// rcp::Registry with mock controllers for each defined zone.  YAML is
// supported transparently (requires an external YAML→JSON shim in the
// build; the loader only handles JSON natively).
//
// Example manifest (JSON):
//   { "zones": [
//       { "zone": "FrontLeft",  "priority": "Normal" },
//       { "zone": "FrontRight", "priority": "Normal" }
//   ]}
#pragma once

#include "mock.hpp"
#include "rcp.hpp"

#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace rcp {
namespace config {

// ── ZoneManifestEntry ─────────────────────────────────────────────────────────

struct ZoneManifestEntry {
    Zone        zone;
    std::string priority; // "Low", "Normal", "High", "Critical"
    std::string extra;    // opaque metadata
};

// ── Manifest ──────────────────────────────────────────────────────────────────

struct Manifest {
    std::vector<ZoneManifestEntry> zones;
};

// ── ParseError ────────────────────────────────────────────────────────────────

struct ParseError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// ── parse_json ────────────────────────────────────────────────────────────────

// Minimal JSON manifest parser (hand-rolled; no external dependency).
// Supports the schema described above.  Throws ParseError on malformed input.
inline Manifest parse_json(const std::string& json) {
    static const std::unordered_map<std::string, Zone> zone_map = {
        {"FrontLeft",  Zone::FrontLeft},
        {"FrontRight", Zone::FrontRight},
        {"RearLeft",   Zone::RearLeft},
        {"RearRight",  Zone::RearRight},
        {"Central",    Zone::Central},
    };

    Manifest m;

    auto extract_str = [&](size_t start) -> std::string {
        auto q1 = json.find('"', start);
        if (q1 == std::string::npos) throw ParseError("missing string value");
        auto q2 = json.find('"', q1 + 1);
        if (q2 == std::string::npos) throw ParseError("unterminated string value");
        return json.substr(q1 + 1, q2 - q1 - 1);
    };

    size_t pos = 0;
    while ((pos = json.find("{", pos)) != std::string::npos) {
        auto close = json.find("}", pos);
        if (close == std::string::npos) break;
        std::string obj = json.substr(pos, close - pos + 1);

        // Check if this object has a "zone" key.
        auto zk = obj.find("\"zone\"");
        if (zk == std::string::npos) { pos = close + 1; continue; }

        ZoneManifestEntry entry;
        entry.zone = Zone::Central; // default, overridden below

        // Extract zone name.
        auto zone_str = extract_str(pos + zk + 6);
        auto it = zone_map.find(zone_str);
        if (it == zone_map.end())
            throw ParseError("unknown zone: " + zone_str);
        entry.zone = it->second;

        // Extract optional priority.
        auto pk = obj.find("\"priority\"");
        if (pk != std::string::npos)
            entry.priority = extract_str(pos + pk + 10);

        m.zones.push_back(entry);
        pos = close + 1;
    }

    return m;
}

// ── load ──────────────────────────────────────────────────────────────────────

// load parses a JSON manifest string and registers one mock controller per
// zone entry into reg.  Returns an error code if any registration fails.
inline std::error_code load(const std::string& json, rcp::Registry& reg) {
    Manifest m = parse_json(json);
    for (auto& entry : m.zones) {
        auto ctrl = std::make_shared<rcp::mock::Controller>(entry.zone);
        auto ec   = reg.register_ctrl(ctrl);
        if (ec) return ec;
    }
    return {};
}

} // namespace config
} // namespace rcp
