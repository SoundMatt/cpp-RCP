// fusa:req REQ-ADMIN-001
// fusa:req REQ-ADMIN-002
// fusa:req REQ-ADMIN-003
// fusa:req REQ-ADMIN-004
// fusa:req REQ-ADMIN-005
// fusa:req REQ-ADMIN-006
// fusa:req REQ-ADMIN-007
// fusa:req REQ-ADMIN-008

// In-process Admin API: zone listing, SSE events, Prometheus metrics (v0.26.0).
//
// AdminServer is a lightweight in-process interface: callers can query
// zone state, subscribe to events (SSE-style push channel), and snapshot
// Prometheus-format text metrics.  An actual HTTP binding is out of scope;
// use a libmicrohttpd or Asio adapter to expose the HTTP surface.
#pragma once

#include "rcp.hpp"

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace rcp {
namespace admin {

// ── ZoneInfo ──────────────────────────────────────────────────────────────────

struct ZoneInfo {
    Zone        zone;
    bool        registered;
    std::string extra; // JSON-encoded metadata blob, optional
};

// ── Event ─────────────────────────────────────────────────────────────────────

enum class EventType : uint8_t { ZoneRegistered = 1, ZoneDeregistered = 2, StatusUpdate = 3 };

struct Event {
    EventType                             type;
    Zone                                  zone;
    std::chrono::system_clock::time_point ts;
};

using EventCallback = std::function<void(const Event&)>;

// ── Counter ───────────────────────────────────────────────────────────────────

struct Counter {
    std::string name;
    std::string labels;
    double      value{0.0};
};

// ── AdminServer ───────────────────────────────────────────────────────────────

class AdminServer {
public:
    explicit AdminServer(rcp::Registry& reg) : reg_(reg) {}

    // zones returns a snapshot of all registered zones.
    std::vector<ZoneInfo> zones() const {
        auto ctrls = reg_.controllers();
        std::vector<ZoneInfo> out;
        out.reserve(ctrls.size());
        for (auto& c : ctrls) {
            out.push_back({c->zone(), true, {}});
        }
        return out;
    }

    // subscribe registers an SSE callback for registry events.
    void subscribe(EventCallback cb) {
        std::lock_guard<std::mutex> lk(mu_);
        subscribers_.push_back(std::move(cb));
    }

    // emit broadcasts an event to all subscribers.
    void emit(Event ev) {
        std::lock_guard<std::mutex> lk(mu_);
        for (auto& cb : subscribers_) cb(ev);
    }

    // record_counter increments a named metric counter.
    void record_counter(const std::string& name, const std::string& labels, double delta) {
        std::lock_guard<std::mutex> lk(mu_);
        auto& c = counters_[name + "{" + labels + "}"];
        c.name   = name;
        c.labels = labels;
        c.value += delta;
    }

    // metrics_text returns Prometheus text-format metric lines.
    std::string metrics_text() const {
        std::lock_guard<std::mutex> lk(mu_);
        std::ostringstream oss;
        for (auto& kv : counters_) {
            oss << "# TYPE " << kv.second.name << " counter\n";
            oss << kv.second.name;
            if (!kv.second.labels.empty())
                oss << "{" << kv.second.labels << "}";
            oss << " " << kv.second.value << "\n";
        }
        return oss.str();
    }

private:
    rcp::Registry&                           reg_;
    mutable std::mutex                        mu_;
    std::vector<EventCallback>                subscribers_;
    std::unordered_map<std::string, Counter>  counters_;
};

} // namespace admin
} // namespace rcp
