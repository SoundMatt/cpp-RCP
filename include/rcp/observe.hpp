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
// Manager and rcp/shmem.hpp's Registry already key on.

struct Metric {
    std::string name;
    double      value;
    uint64_t    stream_key;
};

// ── MetricsSink ───────────────────────────────────────────────────────────────

class MetricsSink {
public:
    virtual ~MetricsSink() = default;
    virtual void record_span(const Span&)   = 0;
    virtual void record_gauge(const Metric&) = 0;
    virtual void record_counter(const std::string& name, uint64_t stream_key, double delta) = 0;
};

class NoopSink final : public MetricsSink {
public:
    void record_span(const Span&)   override {}
    void record_gauge(const Metric&) override {}
    void record_counter(const std::string&, uint64_t, double) override {}
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
    void record_counter(const std::string&, uint64_t, double) override {}

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
        Span span;
        span.name        = "rcp.request";
        span.byte_bus_id  = req.byte_bus_id;
        span.acf_msg_type = req.acf_msg_type;
        span.start_time   = std::chrono::steady_clock::now();

        auto ec = inner_(ctx, req, req_payload, out_resp, out_resp_payload);

        span.end_time = std::chrono::steady_clock::now();
        span.result   = ec;
        sink_->record_span(span);
        sink_->record_counter("rcp.requests.total", stream_key_, 1.0);
        if (ec) sink_->record_counter("rcp.requests.errors", stream_key_, 1.0);

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
