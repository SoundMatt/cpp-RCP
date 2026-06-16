// fusa:req REQ-OBS-001
// fusa:req REQ-OBS-002
// fusa:req REQ-OBS-003
// fusa:req REQ-OBS-004
// fusa:req REQ-OBS-005
// fusa:req REQ-OBS-006
// fusa:req REQ-OBS-007
// fusa:req REQ-OBS-008

// OpenTelemetry-style observability: spans, gauges, and counters (v0.25.0).
//
// ObservingController wraps any rcp::Controller and records a latency span
// around every send().  Metrics are exported via a configurable MetricsSink.
// The default sink is a no-op; users supply a real sink for production use.
//
// Span and Metric types are intentionally plain so they can be adapted to any
// observability backend (OTel gRPC, Prometheus, etc.) without pulling in
// heavy dependencies.
#pragma once

#include "rcp.hpp"

#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace rcp {
namespace observe {

// ── Span ──────────────────────────────────────────────────────────────────────

struct Span {
    std::string                           name;
    Zone                                  zone;
    CommandType                           cmd_type;
    std::chrono::steady_clock::time_point start_time;
    std::chrono::steady_clock::time_point end_time;
    std::error_code                       result;

    std::chrono::microseconds duration() const {
        return std::chrono::duration_cast<std::chrono::microseconds>(
            end_time - start_time);
    }
};

// ── Metric ────────────────────────────────────────────────────────────────────

struct Metric {
    std::string name;
    double      value;
    Zone        zone;
};

// ── MetricsSink ───────────────────────────────────────────────────────────────

class MetricsSink {
public:
    virtual ~MetricsSink() = default;
    virtual void record_span(const Span&)   = 0;
    virtual void record_gauge(const Metric&) = 0;
    virtual void record_counter(const std::string& name, Zone zone, double delta) = 0;
};

class NoopSink final : public MetricsSink {
public:
    void record_span(const Span&)   override {}
    void record_gauge(const Metric&) override {}
    void record_counter(const std::string&, Zone, double) override {}
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
    void record_counter(const std::string&, Zone, double) override {}

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

// ── ObservingController ───────────────────────────────────────────────────────

class ObservingController final : public rcp::Controller {
public:
    ObservingController(std::shared_ptr<rcp::Controller> inner,
                         std::shared_ptr<MetricsSink>     sink)
        : inner_(std::move(inner))
        , sink_(std::move(sink)) {}

    Zone zone() const noexcept override { return inner_->zone(); }

    std::error_code send(const rcp::Context& ctx,
                          const Command&      cmd,
                          Response&           out) override {
        Span span;
        span.name       = "rcp.send";
        span.zone       = cmd.zone;
        span.cmd_type   = cmd.type;
        span.start_time = std::chrono::steady_clock::now();

        auto ec = inner_->send(ctx, cmd, out);

        span.end_time = std::chrono::steady_clock::now();
        span.result   = ec;
        sink_->record_span(span);
        sink_->record_counter("rcp.commands.total", cmd.zone, 1.0);
        if (ec) sink_->record_counter("rcp.commands.errors", cmd.zone, 1.0);

        return ec;
    }

    std::error_code subscribe(const rcp::Context&             ctx,
                               std::shared_ptr<StatusChannel>& out) override {
        return inner_->subscribe(ctx, out);
    }

    std::error_code close() override { return inner_->close(); }

private:
    std::shared_ptr<rcp::Controller> inner_;
    std::shared_ptr<MetricsSink>     sink_;
};

inline std::shared_ptr<ObservingController> new_controller(
        std::shared_ptr<rcp::Controller> inner,
        std::shared_ptr<MetricsSink>     sink = std::make_shared<NoopSink>()) {
    return std::make_shared<ObservingController>(std::move(inner), std::move(sink));
}

} // namespace observe
} // namespace rcp
