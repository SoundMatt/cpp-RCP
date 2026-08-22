// fusa:req REQ-OBS-001
// fusa:req REQ-OBS-002
// fusa:req REQ-OBS-003
// fusa:req REQ-OBS-004
// fusa:req REQ-OBS-005
// fusa:req REQ-OBS-006
// fusa:req REQ-OBS-007
// fusa:req REQ-OBS-008

// OpenTelemetry-style observability: spans and counters around RC-Client
// request/response traffic.
//
// ROADMAP.md milestone 58, "Auxiliary Transport & Cross-Cutting Rebind
// (v2.14.0)": this header is ADAPTed, per the Satellite Package
// Disposition table's entry for `observe.hpp` — the OTel-style span/
// counter approach itself is unaffected by the protocol replacement, only
// what it wraps changes. ObservingClient below wraps a RequestFn — the
// same client-side send-equivalent callable rcp/record.hpp's
// RecordingClient wraps at this same milestone; see that file's header
// comment for why it mirrors rcp/udp.hpp's Client::request's core shape
// rather than depending on rcp/udp.hpp directly — and records one Span per
// call, the same latency-around-send() concept the pre-replacement
// ObservingController used.
#pragma once

#include "acf.hpp"
#include "avtp.hpp"
#include "rcp.hpp" // for rcp::Context only — see this header's own scope note above

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace rcp {
namespace observe {

// RequestFn — see rcp/record.hpp's header comment for why this shape is
// shared between the two v2.14.0 wrapper headers.
using RequestFn = std::function<std::error_code(const rcp::Context&,
                                                  const acf::AcfMessageInfo&,
                                                  const std::vector<uint8_t>&,
                                                  acf::AcfMessageInfo&,
                                                  std::vector<uint8_t>&)>;

// ── Span ──────────────────────────────────────────────────────────────────────

struct Span {
    std::string                           name;
    avtp::ByteBusId                       byte_bus_id = 0;
    // stream_key identifies which stream this span's request traveled over
    // (typically avtp::StreamId::to_u64()) — added alongside byte_bus_id so
    // a Span carries the same full (stream, endpoint) address c-RCP's own
    // rcp_span_t.addr (rcp_avtp_addr_t) does, rather than byte_bus_id alone.
    // Populated by ObservingClient::request() from its own constructor-supplied
    // stream_key, and by record() (below) from its own stream_key parameter.
    uint64_t                              stream_key = 0;
    uint8_t                               acf_msg_type = acf::kAcfMsgTypeAbb;
    std::chrono::steady_clock::time_point start_time;
    std::chrono::steady_clock::time_point end_time;
    std::error_code                       result;

    std::chrono::microseconds duration() const {
        return std::chrono::duration_cast<std::chrono::microseconds>(
            end_time - start_time);
    }
};

// ── Metric ────────────────────────────────────────────────────────────────────
// `stream_key` replaces the old Metric::zone — the same opaque uint64_t
// per-sender identity (typically avtp::StreamId::to_u64()) rcp/watchdog.hpp's
// Manager and rcp/shmem.hpp's Registry already key on. `byte_bus_id` is
// carried alongside it (not folded into a single opaque key) so a metric
// keyed on the same stream but a different endpoint within it is still
// distinguishable — c-RCP's rcp_metric_t/record_counter carry the full
// rcp_avtp_addr_t (stream_id + byte_bus_id) for the same reason; this
// module previously dropped byte_bus_id here, which meant two different
// byte_bus_ids on one stream were indistinguishable in every counter/gauge.

struct Metric {
    std::string     name;
    double          value;
    uint64_t        stream_key;
    avtp::ByteBusId byte_bus_id = 0;
};

// ── MetricsSink ───────────────────────────────────────────────────────────────

class MetricsSink {
public:
    virtual ~MetricsSink() = default;
    virtual void record_span(const Span&)   = 0;
    virtual void record_gauge(const Metric&) = 0;
    virtual void record_counter(const std::string& name, uint64_t stream_key,
                                 avtp::ByteBusId byte_bus_id, double delta) = 0;
};

class NoopSink final : public MetricsSink {
public:
    void record_span(const Span&)   override {}
    void record_gauge(const Metric&) override {}
    void record_counter(const std::string&, uint64_t, avtp::ByteBusId, double) override {}
};

// ── InMemorySink ──────────────────────────────────────────────────────────────

// InMemorySink collects spans for test assertions.
class InMemorySink final : public MetricsSink {
public:
    void record_span(const Span& s) override {
        std::lock_guard<std::mutex> lk(mu_);
        spans_.push_back(s);
    }
    void record_gauge(const Metric&) override {}
    void record_counter(const std::string&, uint64_t, avtp::ByteBusId, double) override {}

    std::vector<Span> spans() const {
        std::lock_guard<std::mutex> lk(mu_);
        return spans_;
    }
    size_t span_count() const {
        std::lock_guard<std::mutex> lk(mu_);
        return spans_.size();
    }

private:
    mutable std::mutex mu_;
    std::vector<Span>  spans_;
};

// ── record ────────────────────────────────────────────────────────────────────
//
// record is the single, caller-driven recording primitive analogous to
// c-RCP's rcp_observe_record() (ROADMAP.md milestone 80's "Satellite
// Package Rework" rebind of observe.h): builds a Span from caller-supplied
// name/addressing/timestamps/result and forwards it to sink, then
// increments sink's "rcp.requests.total" counter (and, iff result is set,
// "rcp.requests.errors" too) — all without requiring the call to have gone
// through an ObservingClient-wrapped RequestFn at all. A caller that drives
// its own endpoint-specific send outside RequestFn's fixed shape (e.g.
// directly against rcp::mock::Server, or from a transport this module
// doesn't itself wrap) can call this directly and supply its own span name
// and pre-measured start/end timestamps, the same caller-driven convention
// rcp/watchdog.hpp's Manager::on_request_received and rcp/deadline.hpp's
// Monitor already use. ObservingClient::request() below is now implemented
// in terms of this function rather than duplicating its body.
inline void record(const std::shared_ptr<MetricsSink>& sink, const std::string& name,
                    avtp::ByteBusId byte_bus_id, uint64_t stream_key, uint8_t acf_msg_type,
                    std::chrono::steady_clock::time_point start_time,
                    std::chrono::steady_clock::time_point end_time,
                    std::error_code result) {
    Span span;
    span.name         = name;
    span.byte_bus_id  = byte_bus_id;
    span.stream_key   = stream_key;
    span.acf_msg_type = acf_msg_type;
    span.start_time   = start_time;
    span.end_time     = end_time;
    span.result       = result;

    sink->record_span(span);
    sink->record_counter("rcp.requests.total", stream_key, byte_bus_id, 1.0);
    if (result) sink->record_counter("rcp.requests.errors", stream_key, byte_bus_id, 1.0);
}

// ── ObservingClient ───────────────────────────────────────────────────────────

class ObservingClient {
public:
    // `stream_key` identifies which stream this client observes (typically
    // avtp::StreamId::to_u64() for the underlying transport, e.g.
    // rcp/udp.hpp's Client) — RequestFn's own signature carries no stream
    // identity of its own (see this file's header comment), so it is
    // supplied once here rather than re-derived per call.
    ObservingClient(RequestFn inner, uint64_t stream_key, std::shared_ptr<MetricsSink> sink)
        : inner_(std::move(inner))
        , stream_key_(stream_key)
        , sink_(std::move(sink)) {}

    std::error_code request(const rcp::Context& ctx,
                             const acf::AcfMessageInfo& req,
                             const std::vector<uint8_t>& req_payload,
                             acf::AcfMessageInfo&        out_resp,
                             std::vector<uint8_t>&       out_resp_payload) {
        auto start_time = std::chrono::steady_clock::now();
        auto ec = inner_(ctx, req, req_payload, out_resp, out_resp_payload);
        auto end_time = std::chrono::steady_clock::now();

        record(sink_, "rcp.request", req.byte_bus_id, stream_key_, req.acf_msg_type,
               start_time, end_time, ec);

        return ec;
    }

private:
    RequestFn                    inner_;
    uint64_t                     stream_key_;
    std::shared_ptr<MetricsSink> sink_;
};

inline std::shared_ptr<ObservingClient> new_observing_client(
        RequestFn inner, uint64_t stream_key,
        std::shared_ptr<MetricsSink> sink = std::make_shared<NoopSink>()) {
    return std::make_shared<ObservingClient>(std::move(inner), stream_key, std::move(sink));
}

} // namespace observe
} // namespace rcp
