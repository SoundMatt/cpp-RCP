// fusa:req REQ-DYN-001
// fusa:req REQ-DYN-002
// fusa:req REQ-DYN-003
// fusa:req REQ-DYN-004
// fusa:req REQ-DYN-005
// fusa:req REQ-DYN-006

// Runtime schema registry and dynamic payload encoding (v0.30.0).
//
// DynamicPayload is a self-describing envelope: a 4-byte schema ID followed by
// the encoded value blob.  SchemaRegistry maps schema IDs to human-readable
// names and optional field descriptors.
#pragma once

#include "rcp.hpp"

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace rcp {
namespace dyndata {

// ── SchemaId ──────────────────────────────────────────────────────────────────

using SchemaId = uint32_t;

// ── FieldDescriptor ───────────────────────────────────────────────────────────

struct FieldDescriptor {
    std::string name;
    std::string type; // "uint8","uint16","uint32","float","bytes"
    uint32_t    offset{0};
    uint32_t    size{0};
};

// ── SchemaEntry ───────────────────────────────────────────────────────────────

struct SchemaEntry {
    SchemaId                      id;
    std::string                   name;
    std::vector<FieldDescriptor>  fields;
};

// ── SchemaRegistry ────────────────────────────────────────────────────────────

class SchemaRegistry {
public:
    std::error_code add(SchemaEntry entry) {
        std::lock_guard<std::mutex> lk(mu_);
        if (schemas_.count(entry.id)) return ErrAlreadyExists;
        schemas_[entry.id] = std::move(entry);
        return {};
    }

    bool lookup(SchemaId id, SchemaEntry& out) const {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = schemas_.find(id);
        if (it == schemas_.end()) return false;
        out = it->second;
        return true;
    }

    size_t size() const {
        std::lock_guard<std::mutex> lk(mu_);
        return schemas_.size();
    }

private:
    mutable std::mutex                       mu_;
    std::unordered_map<SchemaId, SchemaEntry> schemas_;
};

// ── DynamicPayload ────────────────────────────────────────────────────────────

struct DynamicPayload {
    SchemaId             schema_id = 0;
    std::vector<uint8_t> data;

    // encode packs schema_id (big-endian) + data into a wire-ready vector.
    std::vector<uint8_t> encode() const {
        std::vector<uint8_t> out;
        out.reserve(4 + data.size());
        out.push_back(static_cast<uint8_t>(schema_id >> 24));
        out.push_back(static_cast<uint8_t>(schema_id >> 16));
        out.push_back(static_cast<uint8_t>(schema_id >>  8));
        out.push_back(static_cast<uint8_t>(schema_id));
        out.insert(out.end(), data.begin(), data.end());
        return out;
    }

    // decode parses a wire payload into a DynamicPayload.
    static DynamicPayload decode(const std::vector<uint8_t>& raw) {
        DynamicPayload dp;
        if (raw.size() < 4) return dp;
        dp.schema_id = (static_cast<uint32_t>(raw[0]) << 24)
                     | (static_cast<uint32_t>(raw[1]) << 16)
                     | (static_cast<uint32_t>(raw[2]) <<  8)
                     |  static_cast<uint32_t>(raw[3]);
        dp.data.assign(raw.begin() + 4, raw.end());
        return dp;
    }
};

} // namespace dyndata
} // namespace rcp
