// fusa:req REQ-CFG-001
// fusa:req REQ-CFG-002
// fusa:req REQ-CFG-003
// fusa:req REQ-CFG-004
// fusa:req REQ-CFG-005
// fusa:req REQ-CFG-006

// RC Server/endpoint topology manifest loader from JSON/YAML configuration
// files (v0.28.0).
//
// ConfigLoader parses a JSON manifest describing known RC Servers — each
// identified by an opaque stream_key (typically an avtp::StreamId::to_u64())
// — and the Endpoints (byte_bus_id) each server exposes, and bootstraps an
// rcp::shmem::Registry with one Channel per distinct stream_key so an
// embedding application can wire a Handler and start dispatching immediately,
// ahead of or instead of rcp/discovery.hpp's own discovery mechanism. YAML
// is supported transparently (requires an external YAML→JSON shim in the
// build; the loader only handles JSON natively).
//
// Example manifest (JSON):
//   { "endpoints": [
//       { "stream_key": "0", "byte_bus_id": 1, "priority": "Normal" },
//       { "stream_key": "0", "byte_bus_id": 2, "priority": "Normal" }
//   ]}
//
// Rebound onto the TC18 stream_key/byte_bus_id addressing model (cpp-RCP-
// FS-03, #86): previously this loader parsed a Zone-name manifest and
// constructed one rcp::legacy_mock::Controller per zone into an rcp::Registry
// — both retired alongside the rest of the pre-TC18 placeholder model
// (cpp-RCP-FS-01, #84). There is no direct per-entry replacement for that
// in-process Controller construction, since TC18 addresses Endpoints by
// stream_key/byte_bus_id on an already-connected RC Server rather than by
// zone lookup; this loader now bootstraps the stream-keyed
// rcp::shmem::Registry (rcp/shmem.hpp) instead, per the Satellite Package
// Disposition table's entry for `config.hpp` — "rebuilt around a
// server/endpoint manifest schema; still useful for bootstrapping known
// topologies alongside discovery."
#pragma once

#include "avtp.hpp"
#include "rcp.hpp"
#include "shmem.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

namespace rcp {
namespace config {

// ── EndpointManifestEntry ─────────────────────────────────────────────────────

struct EndpointManifestEntry {
    uint64_t        stream_key  = 0;
    avtp::ByteBusId byte_bus_id = 0;
    std::string     priority;  // "Low", "Normal", "High", "Critical" — opaque to this loader
    std::string     extra;     // opaque metadata
};

// ── Manifest ──────────────────────────────────────────────────────────────────

struct Manifest {
    std::vector<EndpointManifestEntry> endpoints;
};

// ── ParseError ────────────────────────────────────────────────────────────────

struct ParseError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// ── parse_json ────────────────────────────────────────────────────────────────

// Minimal JSON manifest parser (hand-rolled; no external dependency).
// Supports the schema described above. Throws ParseError on malformed input.
inline Manifest parse_json(const std::string& json) {
    Manifest m;

    auto extract_str = [&](size_t start) -> std::string {
        auto q1 = json.find('"', start);
        if (q1 == std::string::npos) throw ParseError("missing string value");
        auto q2 = json.find('"', q1 + 1);
        if (q2 == std::string::npos) throw ParseError("unterminated string value");
        return json.substr(q1 + 1, q2 - q1 - 1);
    };

    // extract_uint reads the decimal digits immediately following the ':'
    // at colon_pos (skipping whitespace), stopping at the first non-digit.
    auto extract_uint = [&](size_t colon_pos) -> unsigned long long {
        size_t p = colon_pos + 1;
        while (p < json.size() && (json[p] == ' ' || json[p] == '\t')) ++p;
        size_t begin = p;
        while (p < json.size() && json[p] >= '0' && json[p] <= '9') ++p;
        if (p == begin) throw ParseError("missing numeric value");
        return std::stoull(json.substr(begin, p - begin));
    };

    size_t pos = 0;
    while ((pos = json.find('{', pos)) != std::string::npos) {
        auto close = json.find('}', pos);
        if (close == std::string::npos) break;
        std::string obj = json.substr(pos, close - pos + 1);

        // Only objects carrying both "stream_key" and "byte_bus_id" are
        // endpoint entries; anything else (e.g. the enclosing manifest
        // object itself) is skipped.
        auto sk = obj.find("\"stream_key\"");
        auto bk = obj.find("\"byte_bus_id\"");
        if (sk == std::string::npos || bk == std::string::npos) { pos = close + 1; continue; }

        EndpointManifestEntry entry;

        // stream_key is a decimal or "0x"-prefixed hex string.
        auto sk_str = extract_str(pos + sk + 12);
        try {
            size_t consumed = 0;
            entry.stream_key = std::stoull(sk_str, &consumed, 0);
            if (consumed != sk_str.size()) throw std::invalid_argument(sk_str);
        } catch (const std::exception&) {
            throw ParseError("invalid stream_key: " + sk_str);
        }

        // byte_bus_id is a bare JSON number in [0, 255].
        auto bk_colon = obj.find(':', bk);
        if (bk_colon == std::string::npos) throw ParseError("malformed byte_bus_id");
        auto bus_val = extract_uint(pos + bk_colon);
        if (bus_val > 0xFF) throw ParseError("byte_bus_id out of range: " + std::to_string(bus_val));
        entry.byte_bus_id = static_cast<avtp::ByteBusId>(bus_val);

        auto pk = obj.find("\"priority\"");
        if (pk != std::string::npos)
            entry.priority = extract_str(pos + pk + 10);

        auto ek = obj.find("\"extra\"");
        if (ek != std::string::npos)
            entry.extra = extract_str(pos + ek + 7);

        m.endpoints.push_back(entry);
        pos = close + 1;
    }

    return m;
}

// ── load ──────────────────────────────────────────────────────────────────────

// load parses a JSON manifest string and bootstraps `reg` with one
// shmem::Channel per distinct stream_key named in the manifest. The
// embedding application still owns wiring each Channel's Handler — there is
// no more generic in-process Controller this loader can construct on the
// caller's behalf (see this header's own comment). Returns an error code if
// registering a channel fails (e.g. `reg` already has a channel for that
// stream_key).
inline std::error_code load(const std::string& json, shmem::Registry& reg) {
    Manifest m = parse_json(json);

    std::vector<uint64_t> bootstrapped;
    for (auto& entry : m.endpoints) {
        if (std::find(bootstrapped.begin(), bootstrapped.end(), entry.stream_key) !=
            bootstrapped.end())
            continue; // already bootstrapped this stream_key from an earlier entry
        bootstrapped.push_back(entry.stream_key);

        auto ec = reg.add_channel(shmem::new_channel(entry.stream_key));
        if (ec) return ec;
    }
    return {};
}

} // namespace config
} // namespace rcp
