// fusa:req REQ-SIM-001
// fusa:req REQ-SIM-002
// fusa:req REQ-SIM-003
// fusa:req REQ-SIM-004
// fusa:req REQ-SIM-005
// fusa:req REQ-SIM-006
// fusa:req REQ-SIM-007

// Timing-realistic RC Server simulator for SiL/HIL testing — wraps
// rcp/mock.hpp's in-process Server with configurable latency/jitter and
// Fault/Recover scenario controls, and wires watchdog-miss detection
// through rcp/watchdog.hpp's per-stream driver (v2.10.0).
//
// ROADMAP.md milestone 56, "Test & Simulation Harness Rebuild (v2.12.0)":
// this header REPLACES this file's pre-replacement content in full, per
// the Satellite Package Disposition table's entry for `sim.hpp` — the
// prior sim::Controller, which implemented the full old rcp::Controller
// interface plus a client-driven CommandType::Watchdog kick model, is
// discarded, not adapted (that request/response shape and that watchdog
// model both have no analog in the target protocol; the target protocol's
// actual per-stream watchdog reset rule already exists as
// rcp/watchdog.hpp's StreamWatchdog/Manager, v2.10.0). Nothing else in
// this tree depended on the old sim::Controller API (only this file's own
// test did), so no legacy shim is needed here — unlike rcp/mock.hpp's
// legacy_mock.hpp split, sim.hpp's old content is not preserved anywhere.
//
// This header, like every Phase 14 primitive header since v2.9.0/v2.10.0,
// supplies no clock or background thread of its own — the pre-replacement
// sim::Controller's status/watchdog polling threads are not carried
// forward. Every timed effect (simulated latency, watchdog kicks/polls) is
// driven by an explicit now_ms a caller supplies, the same "primitive, not
// scheduler" convention rcp/watchdog.hpp's Manager, rcp/deadline.hpp's
// Monitor, and rcp/e2e.hpp's RxWatchdog already established.
//
// Field names and behavior below implement TC18's *behavior* as described
// in an internal structured extraction of the specification named above;
// no text from that document is reproduced here. The concrete latency/
// jitter model and Fault/Recover shape chosen in this file are this
// implementation's own, carried forward unchanged in concept from the
// pre-replacement sim::Controller — full bit-for-bit conformance against
// other TC18 implementations is not claimed, same as the equivalent
// disclaimer in rcp/mock.hpp.
#pragma once

#include <rcp/mock.hpp>
#include <rcp/watchdog.hpp>

#include <chrono>
#include <cstdint>
#include <random>
#include <system_error>
#include <vector>

namespace rcp {
namespace sim {

// ── LatencyModel ─────────────────────────────────────────────────────────────
// Carried forward unchanged in concept from the pre-replacement
// sim::Controller: Constant always reports the same delay; Jitter adds a
// uniformly distributed extra delay on top of it.

enum class LatencyModel : uint8_t {
    Constant = 0,
    Jitter   = 1,
};

// ── Config ────────────────────────────────────────────────────────────────────

struct Config {
    std::chrono::milliseconds base_latency{2};
    std::chrono::milliseconds jitter{1};
    LatencyModel              latency_model{LatencyModel::Jitter};
};

// ── Simulator ─────────────────────────────────────────────────────────────────
// Wraps one mock::Server with scenario-testing controls. Not copyable —
// mock::Server itself is not copyable (see its own header comment).
class Simulator final {
public:
    explicit Simulator(Config cfg = {}) : cfg_(cfg), rng_(std::random_device{}()) {}

    Simulator(const Simulator&)            = delete;
    Simulator& operator=(const Simulator&) = delete;

    mock::Server&      server() noexcept { return server_; }
    watchdog::Manager&  watchdog() noexcept { return watchdog_mgr_; }

    // register_stream begins watchdog tracking for a request stream —
    // forwards to rcp::watchdog::Manager::register_stream (v2.10.0),
    // including its fixed-capacity result (Phase 17 c-RCP-reference pass,
    // cpp-RCP issue #129: Manager::kMaxStreams) rather than silently
    // discarding it. Call once per stream a scenario wants watchdog-miss
    // detection for; a stream dispatch() is never called for simply never
    // has its watchdog kicked or polled.
    std::error_code register_stream(uint64_t stream_key) { return watchdog_mgr_.register_stream(stream_key); }

    // fault injects `err` on every subsequent dispatch() call until
    // recover() is called — the same Fault/Recover scenario-testing
    // concept the pre-replacement sim::Controller offered, re-targeted at
    // this module's std::error_code/AcfMessageInfo response shape.
    void fault(std::error_code err) { fault_err_ = err; }

    // recover clears any injected fault, restoring normal dispatch.
    void recover() { fault_err_ = {}; }

    bool faulted() const noexcept { return static_cast<bool>(fault_err_); }

    // simulated_latency_ms draws this call's simulated response delay per
    // cfg_'s LatencyModel — Constant always returns base_latency; Jitter
    // adds a uniformly distributed extra delay in [0, jitter]. This class
    // never sleeps on the caller's behalf (see this header's own
    // "primitive, not scheduler" note) — a caller that wants to actually
    // simulate the delay (e.g. a real SiL/HIL harness, or a test advancing
    // its own now_ms clock) does so itself with the value returned here.
    std::chrono::milliseconds simulated_latency_ms() {
        if (cfg_.latency_model == LatencyModel::Constant || cfg_.jitter.count() <= 0)
            return cfg_.base_latency;
        std::uniform_int_distribution<std::chrono::milliseconds::rep> dist(0, cfg_.jitter.count());
        return cfg_.base_latency + std::chrono::milliseconds(dist(rng_));
    }

    // dispatch is mock::Server::dispatch's timing-realistic wrapper: an
    // injected fault (see fault() above) short-circuits before the server
    // is touched at all; otherwise `stream_key`'s watchdog is kicked (if
    // registered — see register_stream) per rcp/watchdog.hpp's "reset by
    // any inbound request" rule, and the request is forwarded to
    // mock::Server::dispatch unchanged. `now_ms` drives the watchdog kick
    // only — simulated latency is reported via simulated_latency_ms(), not
    // applied as a real sleep here, per this class's own scope note.
    std::error_code dispatch(uint64_t stream_key, size_t client,
                              const acf::AcfMessageInfo& req,
                              const std::vector<uint8_t>& req_payload,
                              acf::AcfMessageInfo& out_resp,
                              std::vector<uint8_t>& out_resp_payload,
                              uint64_t now_ms) {
        if (fault_err_) {
            out_resp = acf::make_response(req, acf::ResponseKind::ErrorResponse);
            out_resp_payload.clear();
            return fault_err_;
        }
        if (watchdog_mgr_.is_registered(stream_key)) {
            (void)watchdog_mgr_.on_request_received(stream_key, now_ms);
        }
        return server_.dispatch(client, req, req_payload, out_resp, out_resp_payload);
    }

    // poll_watchdog is a thin forwarder to rcp::watchdog::Manager::poll for
    // one registered stream — see rcp/watchdog.hpp. Deciding when to call
    // this (i.e. running a timer loop) is the embedding
    // application's/test's job, same as watchdog::Manager itself.
    std::error_code poll_watchdog(uint64_t stream_key, const regmap::RequestStreamConfig& cfg,
                                   request::RequestLedger& ledger, uint64_t now_ms) {
        return watchdog_mgr_.poll(stream_key, cfg, ledger, now_ms);
    }

private:
    Config            cfg_;
    std::mt19937      rng_;
    std::error_code   fault_err_;
    mock::Server      server_;
    watchdog::Manager watchdog_mgr_;
};

} // namespace sim
} // namespace rcp
