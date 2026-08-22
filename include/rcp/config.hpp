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
//
// Parity-audit gap-closure (Phase 8 batch A) — c-RCP's own config.c/config.h
// went through a LATER "Satellite Package Rework" (its own milestone 77)
// that replaced ITS zone-manifest schema with a richer one: a "server"
// object (vendor_id/device_id/magic/svr_implemented_options), a
// "hw_pin_map" array, an "endpoints" array shaped completely differently
// from the one above (byte_bus_id/ep_type/ep_enable, not stream_key/
// byte_bus_id/priority), and a "streams" array — all feeding
// rcp_mock_server_t, a test double, via rcp_config_apply_to_mock(). That is
// a different purpose (SiL/HIL test-double configuration) from this
// loader's own (bootstrapping a real rcp::shmem::Registry ahead of/instead
// of discovery), so the two "endpoints" shapes are NOT unified here — doing
// so would silently break either this loader's existing stream_key/
// byte_bus_id manifests or any future test-double manifest, for no shared
// benefit. Two of c-RCP's four new sections ARE ported below, additively,
// as their own top-level keys that cannot collide with "endpoints" above:
//   - "server" (ServerManifest below) + apply_to_mock() — vendor_id/
//     device_id/magic/svr_implemented_options are plain rcp::mock::Server
//     regmap fields (Server::registers().general), reachable with no
//     mock.hpp change at all.
//   - "hw_pin_map" (HwPinManifestEntry below) + apply_to_mock() — likewise
//     just a rcp::regmap::RegisterMap::hw_pin_map vector, reachable the
//     same way.
// NOT ported: c-RCP's "endpoints" (ep_type/ep_enable) and its
// rcp_mock_server_add_endpoint() target. rcp::mock::Server has no dynamic
// endpoint-registration API — its representative endpoint set (gpio/spi/
// i2c/...) is fixed at compile time inside Server::dispatch() — so there is
// nothing in this tree yet for a ported apply_to_mock() to register a
// manifest-named endpoint into. Adding one is a rcp/mock.hpp change, out of
// this batch's scope. c-RCP's "streams" section is likewise not ported:
// c-RCP's own rcp_config_apply_to_mock() never applies it either (parsed
// data only, by that function's own design) — nothing to port there.
#pragma once

#include "avtp.hpp"
#include "mock.hpp"
#include "rcp.hpp"
#include "regmap.hpp"
#include "shmem.hpp"

#include <algorithm>
#include <cstdint>
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

// ── ServerManifest ────────────────────────────────────────────────────────────
// An optional top-level "server" object naming this RC Server's own
// vendor_id/device_id/magic and svr_implemented_options — see this file's
// own header comment. `magic` of 0 means "not specified": apply_to_mock()
// only overwrites Server::registers().general.magic when the manifest
// supplies a nonzero value, matching c-RCP's own rcp_config_apply_to_mock()
// (which would otherwise stomp GeneralMap's own real default with an
// absent manifest field's zero). svr_implemented_options is OR'd into the
// target's existing bits, not overwritten, for the same reason (also
// matching c-RCP).
struct ServerManifest {
    uint16_t vendor_id             = 0;
    uint16_t device_id             = 0;
    uint32_t magic                 = 0;
    uint8_t  svr_implemented_options = 0; // regmap::kOpt{TimeSync,EnhCancel,Trigger,Chained,CompoundWait}
};

// ── HwPinManifestEntry ────────────────────────────────────────────────────────
// One row of an optional top-level "hw_pin_map" array — field-for-field the
// same shape as regmap::HwPinMapEntry (hw_ep_nr/hw_ep_pin_nr/hw_pin_type),
// matching c-RCP's own rcp_config_hw_pin_t/rcp_regmap_hw_pin_map_entry_t
// (this file's own header comment).

struct HwPinManifestEntry {
    uint8_t hw_ep_nr     = 0;
    uint8_t hw_ep_pin_nr = 0;
    uint8_t hw_pin_type  = 0; // regmap::hw_pin::k* bitmask, named-bit array in JSON
};

// ── Manifest ──────────────────────────────────────────────────────────────────

struct Manifest {
    std::vector<EndpointManifestEntry> endpoints;
    ServerManifest                     server;
    std::vector<HwPinManifestEntry>    hw_pin_map;
};

// ── ParseError ────────────────────────────────────────────────────────────────

struct ParseError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

namespace detail {

// extract_str_in reads the first quoted string found at or after `start`
// within `s`. Shared by the endpoint/hw_pin_map/server parsing below —
// operates on whichever string (`json` or one object's own substring) the
// caller passes, so callers need no absolute/relative position bookkeeping.
inline std::string extract_str_in(const std::string& s, size_t start) {
    auto q1 = s.find('"', start);
    if (q1 == std::string::npos) throw ParseError("missing string value");
    auto q2 = s.find('"', q1 + 1);
    if (q2 == std::string::npos) throw ParseError("unterminated string value");
    return s.substr(q1 + 1, q2 - q1 - 1);
}

// extract_uint_in reads the decimal digits immediately following the ':' at
// colon_pos (skipping whitespace), stopping at the first non-digit.
inline unsigned long long extract_uint_in(const std::string& s, size_t colon_pos) {
    size_t p = colon_pos + 1;
    while (p < s.size() && (s[p] == ' ' || s[p] == '\t')) ++p;
    size_t begin = p;
    while (p < s.size() && s[p] >= '0' && s[p] <= '9') ++p;
    if (p == begin) throw ParseError("missing numeric value");
    return std::stoull(s.substr(begin, p - begin));
}

struct NamedBit { const char* name; uint8_t bit; };

// or_named_bits ORs into `out` every name in `names` whose quoted form
// (e.g. "time_sync") appears anywhere within [begin, end) of `s`. An
// unrecognized name already present in the manifest is silently ignored —
// matching c-RCP's own or_named_bits_u8()/or_named_bits_options()
// leniency, not a validating parse.
inline void or_named_bits(const std::string& s, size_t begin, size_t end, uint8_t& out,
                           const NamedBit* names, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        std::string needle = std::string("\"") + names[i].name + "\"";
        auto pos = s.find(needle, begin);
        if (pos != std::string::npos && pos < end) out |= names[i].bit;
    }
}

// find_array_span locates the '[' ... ']' span of the array value for `key`
// within `s`, starting the search at or after `key_pos` (typically just
// past a matched key). Non-nesting, matching every other array this parser
// understands (svr_implemented_options/hw_pin_type name arrays never
// themselves nest brackets).
inline bool find_array_span(const std::string& s, size_t key_pos, size_t& out_begin, size_t& out_end) {
    auto lb = s.find('[', key_pos);
    if (lb == std::string::npos) return false;
    auto rb = s.find(']', lb);
    if (rb == std::string::npos) return false;
    out_begin = lb + 1;
    out_end   = rb;
    return true;
}

inline const NamedBit* option_bit_names() {
    static const NamedBit kNames[] = {
        {"time_sync",        regmap::kOptTimeSync},
        {"enhanced_cancel",  regmap::kOptEnhCancel},
        {"trigger",          regmap::kOptTrigger},
        {"chained",          regmap::kOptChained},
        {"compound_bundles", regmap::kOptCompoundWait},
    };
    return kNames;
}
constexpr size_t kOptionBitNamesLen = 5;

inline const NamedBit* pin_prop_names() {
    static const NamedBit kNames[] = {
        {"pull_down",       regmap::hw_pin::kPullDown},
        {"pull_up",         regmap::hw_pin::kPullUp},
        {"open_drain",      regmap::hw_pin::kStageOpenDrain},
        {"open_source",     regmap::hw_pin::kStageOpenSource},
        {"push_pull",       regmap::hw_pin::kStagePushPull},
        {"low_drive",       regmap::hw_pin::kDriveLow},
        {"medium_drive",    regmap::hw_pin::kDriveMedium},
        {"high_drive",      regmap::hw_pin::kDriveHigh},
        {"schmitt_trigger", regmap::hw_pin::kSchmittTrigger},
    };
    return kNames;
}
constexpr size_t kPinPropNamesLen = 9;

// parse_server_fields scans the whole document (not object-bounded — it
// names a single object, not a repeated list, same rationale c-RCP's own
// parse_server_fields() documents) for the four ServerManifest fields.
inline void parse_server_fields(const std::string& json, ServerManifest& out) {
    if (auto k = json.find("\"vendor_id\""); k != std::string::npos) {
        if (auto colon = json.find(':', k); colon != std::string::npos)
            out.vendor_id = static_cast<uint16_t>(extract_uint_in(json, colon));
    }
    if (auto k = json.find("\"device_id\""); k != std::string::npos) {
        if (auto colon = json.find(':', k); colon != std::string::npos)
            out.device_id = static_cast<uint16_t>(extract_uint_in(json, colon));
    }
    if (auto k = json.find("\"magic\""); k != std::string::npos) {
        if (auto colon = json.find(':', k); colon != std::string::npos)
            out.magic = static_cast<uint32_t>(extract_uint_in(json, colon));
    }
    if (auto k = json.find("\"svr_implemented_options\""); k != std::string::npos) {
        size_t begin = 0, end = 0;
        if (find_array_span(json, k, begin, end))
            or_named_bits(json, begin, end, out.svr_implemented_options,
                          option_bit_names(), kOptionBitNamesLen);
    }
}

} // namespace detail

// parse_endpoint_entry parses one endpoint-manifest object ("stream_key" +
// "byte_bus_id", the pre-existing schema above). Both keys are required —
// this function is only reached once at least one of the two is already
// known present (see parse_json's own routing below), so a call here with
// only one of them actually found is a genuinely malformed entry, not an
// object of some other kind to be silently skipped (2026-08-13 fix,
// matching a bug class c-RCP's own config.c independently found and fixed
// in its "either key routes, so the real validator can reject" entry-
// sniffing dispatch — this parser used to require BOTH keys just to be
// *routed* here at all, which meant an entry with only one of the two was
// silently dropped instead of rejected).
inline EndpointManifestEntry parse_endpoint_entry(const std::string& obj) {
    EndpointManifestEntry entry;

    auto sk = obj.find("\"stream_key\"");
    if (sk == std::string::npos) throw ParseError("endpoint entry missing stream_key");
    // stream_key is a decimal or "0x"-prefixed hex string.
    auto sk_str = detail::extract_str_in(obj, sk + 12);
    try {
        size_t consumed = 0;
        entry.stream_key = std::stoull(sk_str, &consumed, 0);
        if (consumed != sk_str.size()) throw std::invalid_argument(sk_str);
    } catch (const std::exception&) {
        throw ParseError("invalid stream_key: " + sk_str);
    }

    auto bk = obj.find("\"byte_bus_id\"");
    if (bk == std::string::npos) throw ParseError("endpoint entry missing byte_bus_id");
    // byte_bus_id is a bare JSON number in [0, 255].
    auto bk_colon = obj.find(':', bk);
    if (bk_colon == std::string::npos) throw ParseError("malformed byte_bus_id");
    auto bus_val = detail::extract_uint_in(obj, bk_colon);
    if (bus_val > 0xFF) throw ParseError("byte_bus_id out of range: " + std::to_string(bus_val));
    entry.byte_bus_id = static_cast<avtp::ByteBusId>(bus_val);

    if (auto pk = obj.find("\"priority\""); pk != std::string::npos)
        entry.priority = detail::extract_str_in(obj, pk + 10);
    if (auto ek = obj.find("\"extra\""); ek != std::string::npos)
        entry.extra = detail::extract_str_in(obj, ek + 7);

    return entry;
}

// parse_hw_pin_entry parses one "hw_pin_map" array element. hw_ep_nr and
// hw_ep_pin_nr are required (matches c-RCP's own parse_pin_entry());
// hw_pin_type is an optional named-bit array, OR'd bit by bit.
inline HwPinManifestEntry parse_hw_pin_entry(const std::string& obj) {
    HwPinManifestEntry entry;

    auto nr = obj.find("\"hw_ep_nr\"");
    if (nr == std::string::npos) throw ParseError("hw_pin_map entry missing hw_ep_nr");
    auto nr_colon = obj.find(':', nr);
    if (nr_colon == std::string::npos) throw ParseError("malformed hw_ep_nr");
    entry.hw_ep_nr = static_cast<uint8_t>(detail::extract_uint_in(obj, nr_colon));

    auto pn = obj.find("\"hw_ep_pin_nr\"");
    if (pn == std::string::npos) throw ParseError("hw_pin_map entry missing hw_ep_pin_nr");
    auto pn_colon = obj.find(':', pn);
    if (pn_colon == std::string::npos) throw ParseError("malformed hw_ep_pin_nr");
    entry.hw_ep_pin_nr = static_cast<uint8_t>(detail::extract_uint_in(obj, pn_colon));

    if (auto pt = obj.find("\"hw_pin_type\""); pt != std::string::npos) {
        size_t begin = 0, end = 0;
        if (detail::find_array_span(obj, pt, begin, end))
            detail::or_named_bits(obj, begin, end, entry.hw_pin_type,
                                   detail::pin_prop_names(), detail::kPinPropNamesLen);
    }

    return entry;
}

// ── parse_json ────────────────────────────────────────────────────────────────

// Minimal JSON manifest parser (hand-rolled; no external dependency).
// Supports the schema described above. Throws ParseError on malformed input.
inline Manifest parse_json(const std::string& json) {
    Manifest m;

    // "server" is a single object, not a repeated list, so it is scanned
    // across the whole document rather than through the object loop below
    // — same rationale as c-RCP's own parse_server_fields().
    detail::parse_server_fields(json, m.server);

    size_t pos = 0;
    while ((pos = json.find('{', pos)) != std::string::npos) {
        auto close = json.find('}', pos);
        if (close == std::string::npos) break;
        std::string obj = json.substr(pos, close - pos + 1);

        // Route by whichever repeated-entry-kind's own distinctive key(s)
        // appear in this object; each kind's own parser then validates its
        // own required fields are ALL actually present (an object routed
        // here on the strength of just one matching key, but missing the
        // other required one, is rejected, not silently skipped — see
        // parse_endpoint_entry's own comment above).
        if (obj.find("\"hw_ep_nr\"") != std::string::npos) {
            m.hw_pin_map.push_back(parse_hw_pin_entry(obj));
        } else if (obj.find("\"stream_key\"") != std::string::npos ||
                   obj.find("\"byte_bus_id\"") != std::string::npos) {
            m.endpoints.push_back(parse_endpoint_entry(obj));
        }
        // Anything else (e.g. the enclosing manifest object itself, or the
        // "server" object already handled above) is silently skipped.

        pos = close + 1;
    }

    return m;
}

// ── apply_to_mock ─────────────────────────────────────────────────────────────
//
// Applies m's "server"/"hw_pin_map" sections to srv's own regmap — see
// this file's own header comment for why only these two of c-RCP's four
// rcp_config_apply_to_mock() sections are ported. Mirrors c-RCP's own
// merge semantics exactly: vendor_id/device_id are overwritten
// unconditionally; magic is overwritten only if the manifest supplied a
// nonzero value; svr_implemented_options is OR'd into whatever the target
// already had, not overwritten. hw_pin_map (if the manifest names any
// entries at all) replaces srv's table wholesale, capacity-checked against
// regmap::hw_pin_map::kMaxEntries — matching c-RCP's own
// RCP_MOCK_ERR_CAPACITY contract there — returned here as the standard
// std::errc::value_too_large (not regmap::HwPinMapReconfigErrc::
// out_of_range, which names a different, wire-reconfig-write-specific
// condition; reusing that enum's out_of_range value for this unrelated
// "the manifest named more rows than this table can hold" condition would
// be exactly the kind of same-ID-different-meaning collision this audit
// pass flagged elsewhere).
inline std::error_code apply_to_mock(const Manifest& m, mock::Server& srv) {
    auto& regs = srv.registers();

    regs.general.vendor_id = m.server.vendor_id;
    regs.general.device_id = m.server.device_id;
    if (m.server.magic != 0) regs.general.magic = m.server.magic;
    regs.general.svr_implemented_options |= m.server.svr_implemented_options;

    if (!m.hw_pin_map.empty()) {
        if (m.hw_pin_map.size() > regmap::hw_pin_map::kMaxEntries)
            return std::make_error_code(std::errc::value_too_large);

        std::vector<regmap::HwPinMapEntry> rows;
        rows.reserve(m.hw_pin_map.size());
        for (auto& e : m.hw_pin_map) {
            regmap::HwPinMapEntry row;
            row.hw_ep_nr     = e.hw_ep_nr;
            row.hw_ep_pin_nr = e.hw_ep_pin_nr;
            row.hw_pin_type  = e.hw_pin_type;
            rows.push_back(row);
        }
        regs.hw_pin_map_table.capacity = static_cast<uint16_t>(rows.size());
        regs.hw_pin_map = std::move(rows);
    }

    return {};
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

// load_to_mock is apply_to_mock()'s own convenience combinator — parses
// json and applies its "server"/"hw_pin_map" sections to srv in one call,
// the same parse-then-apply shape as load() above (and as c-RCP's own
// rcp_config_load()).
inline std::error_code load_to_mock(const std::string& json, mock::Server& srv) {
    return apply_to_mock(parse_json(json), srv);
}

} // namespace config
} // namespace rcp
