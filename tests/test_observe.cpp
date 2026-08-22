// fusa:test REQ-OBS-001
// fusa:test REQ-OBS-002
// fusa:test REQ-OBS-003
// fusa:test REQ-OBS-004
// fusa:test REQ-OBS-005
// fusa:test REQ-OBS-006
// fusa:test REQ-OBS-007
// fusa:test REQ-OBS-008

// Tests for rcp/observe.hpp — OpenTelemetry-style spans/counters around
// RC-Client request/response traffic (ROADMAP.md milestone 58, "Auxiliary
// Transport & Cross-Cutting Rebind", v2.14.0).

#include <catch2/catch_test_macros.hpp>

#include "rcp/observe.hpp"

#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace rcp;
using namespace rcp::observe;

namespace {
// CountingSink records counter deltas by metric name (and, separately, by
// (name, byte_bus_id) pair — REQ-OBS gap-closure: record_counter now
// carries byte_bus_id, not just stream_key, so a sink can distinguish two
// endpoints on the same stream), the last span seen, and every gauge
// recorded, so tests can assert on rcp.requests.total / rcp.requests.errors,
// span.result, and per-byte_bus_id granularity.
class CountingSink final : public MetricsSink {
public:
    void record_span(const Span& s) override {
        std::lock_guard<std::mutex> lk(mu_);
        last_span_ = s;
        spans_.push_back(s);
    }
    void record_gauge(const Metric& m) override {
        std::lock_guard<std::mutex> lk(mu_);
        gauges_.push_back(m);
    }
    void record_counter(const std::string& name, uint64_t stream_key,
                         avtp::ByteBusId byte_bus_id, double delta) override {
        std::lock_guard<std::mutex> lk(mu_);
        counters_[name] += delta;
        per_bus_counters_[{name, byte_bus_id}] += delta;
        (void)stream_key;
    }
    double counter(const std::string& name) const {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = counters_.find(name);
        return it == counters_.end() ? 0.0 : it->second;
    }
    double counter_for_bus(const std::string& name, avtp::ByteBusId byte_bus_id) const {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = per_bus_counters_.find({name, byte_bus_id});
        return it == per_bus_counters_.end() ? 0.0 : it->second;
    }
    Span last_span() const {
        std::lock_guard<std::mutex> lk(mu_);
        return last_span_;
    }
    std::vector<Span> spans() const {
        std::lock_guard<std::mutex> lk(mu_);
        return spans_;
    }
    std::vector<Metric> gauges() const {
        std::lock_guard<std::mutex> lk(mu_);
        return gauges_;
    }
private:
    mutable std::mutex mu_;
    std::map<std::string, double> counters_;
    std::map<std::pair<std::string, avtp::ByteBusId>, double> per_bus_counters_;
    std::vector<Span>   spans_;
    std::vector<Metric> gauges_;
    Span last_span_{};
};

acf::AcfMessageInfo standard_request(avtp::ByteBusId bus_id, uint8_t transaction_num) {
    return acf::make_standard_request(bus_id, transaction_num, /*write=*/false, /*read_size=*/2);
}

RequestFn ok_request(std::error_code result = {}) {
    return [result](const Context&, const acf::AcfMessageInfo& req, const std::vector<uint8_t>&,
                     acf::AcfMessageInfo& out_resp, std::vector<uint8_t>&) {
        out_resp = acf::make_response(req, acf::ResponseKind::Acknowledge);
        return result;
    };
}
} // namespace

TEST_CASE("observe: span recorded on successful request", "[observe][REQ-OBS-001]") {
    auto sink = std::make_shared<InMemorySink>();
    auto oc   = new_observing_client(ok_request(), /*stream_key=*/1, sink);

    auto req = standard_request(3, 1);
    acf::AcfMessageInfo   resp;
    std::vector<uint8_t>  resp_payload;
    REQUIRE_FALSE(oc->request(Context{}, req, {}, resp, resp_payload));

    REQUIRE(sink->span_count() == 1);
}

TEST_CASE("observe: span captures byte_bus_id and acf_msg_type", "[observe][REQ-OBS-002]") {
    auto sink = std::make_shared<InMemorySink>();
    auto oc   = new_observing_client(ok_request(), 1, sink);

    auto req = standard_request(/*bus_id=*/7, 1);
    acf::AcfMessageInfo   resp;
    std::vector<uint8_t>  resp_payload;
    REQUIRE_FALSE(oc->request(Context{}, req, {}, resp, resp_payload));

    auto spans = sink->spans();
    REQUIRE(spans[0].byte_bus_id == 7);
    REQUIRE(spans[0].acf_msg_type == acf::kAcfMsgTypeAbb);
}

TEST_CASE("observe: span duration is non-negative", "[observe][REQ-OBS-003]") {
    auto sink = std::make_shared<InMemorySink>();
    auto oc   = new_observing_client(ok_request(), 1, sink);

    auto req = standard_request(1, 1);
    acf::AcfMessageInfo   resp;
    std::vector<uint8_t>  resp_payload;
    auto ec = oc->request(Context{}, req, {}, resp, resp_payload);
    (void)ec;

    REQUIRE(sink->span_count() == 1);
    REQUIRE(sink->spans()[0].duration().count() >= 0);
}

TEST_CASE("observe: span captures error code and error counter increments on failure",
          "[observe][REQ-OBS-004][REQ-OBS-008]") {
    auto sink = std::make_shared<CountingSink>();
    auto oc   = new_observing_client(ok_request(ErrClosed), 1, sink);

    auto req = standard_request(1, 1);
    acf::AcfMessageInfo   resp;
    std::vector<uint8_t>  resp_payload;
    auto ec = oc->request(Context{}, req, {}, resp, resp_payload);
    REQUIRE(ec == ErrClosed);

    REQUIRE(sink->last_span().result == ErrClosed);        // span records the error (REQ-OBS-004)
    REQUIRE(sink->counter("rcp.requests.total") == 1.0);
    REQUIRE(sink->counter("rcp.requests.errors") == 1.0);  // error counter (REQ-OBS-008)
}

TEST_CASE("observe: NoopSink does not crash on any metric", "[observe][REQ-OBS-005]") {
    auto oc = new_observing_client(ok_request(), 1); // default noop sink

    auto req = standard_request(1, 1);
    acf::AcfMessageInfo   resp;
    std::vector<uint8_t>  resp_payload;
    REQUIRE_FALSE(oc->request(Context{}, req, {}, resp, resp_payload));
}

TEST_CASE("observe: InMemorySink is thread-safe under concurrent spans",
          "[observe][REQ-OBS-006]") {
    auto sink = std::make_shared<InMemorySink>();
    auto oc   = new_observing_client(ok_request(), 1, sink);

    constexpr int kThreads = 8;
    constexpr int kPerThread = 500;
    std::vector<std::thread> ts;
    for (int t = 0; t < kThreads; ++t) {
        ts.emplace_back([&] {
            for (int i = 0; i < kPerThread; ++i) {
                auto req = standard_request(1, 1);
                acf::AcfMessageInfo   resp;
                std::vector<uint8_t>  resp_payload;
                auto ec = oc->request(Context{}, req, {}, resp, resp_payload);
                (void)ec;
            }
        });
    }
    for (auto& th : ts) th.join();
    REQUIRE(sink->span_count() == kThreads * kPerThread);
}

TEST_CASE("observe: rcp.requests.total counter increments per request", "[observe][REQ-OBS-007]") {
    auto sink = std::make_shared<CountingSink>();
    auto oc   = new_observing_client(ok_request(), 1, sink);

    for (int i = 0; i < 3; ++i) {
        auto req = standard_request(1, static_cast<uint8_t>(i));
        acf::AcfMessageInfo   resp;
        std::vector<uint8_t>  resp_payload;
        REQUIRE_FALSE(oc->request(Context{}, req, {}, resp, resp_payload));
    }
    REQUIRE(sink->counter("rcp.requests.total") == 3.0);
    REQUIRE(sink->counter("rcp.requests.errors") == 0.0);
}

// ── Gap-closure: Span::stream_key / Metric+record_counter::byte_bus_id ────────
// (audit finding: c-RCP's rcp_span_t/rcp_metric_t carry the full
// rcp_avtp_addr_t (stream_id + byte_bus_id); this module previously carried
// only one half of that pair on each side — Span had byte_bus_id but not
// stream_key, Metric/record_counter had stream_key but not byte_bus_id.)

TEST_CASE("observe: span carries the ObservingClient's stream_key", "[observe]") {
    auto sink = std::make_shared<InMemorySink>();
    auto oc   = new_observing_client(ok_request(), /*stream_key=*/0xC0FFEE, sink);

    auto req = standard_request(3, 1);
    acf::AcfMessageInfo   resp;
    std::vector<uint8_t>  resp_payload;
    REQUIRE_FALSE(oc->request(Context{}, req, {}, resp, resp_payload));

    REQUIRE(sink->spans()[0].stream_key == 0xC0FFEE);
}

TEST_CASE("observe: record_counter carries the request's byte_bus_id, "
          "distinguishing endpoints on the same stream", "[observe]") {
    auto sink = std::make_shared<CountingSink>();
    auto oc   = new_observing_client(ok_request(), /*stream_key=*/1, sink);

    acf::AcfMessageInfo   resp;
    std::vector<uint8_t>  resp_payload;
    REQUIRE_FALSE(oc->request(Context{}, standard_request(5, 0), {}, resp, resp_payload));
    REQUIRE_FALSE(oc->request(Context{}, standard_request(7, 0), {}, resp, resp_payload));
    REQUIRE_FALSE(oc->request(Context{}, standard_request(5, 1), {}, resp, resp_payload));

    REQUIRE(sink->counter("rcp.requests.total") == 3.0);
    REQUIRE(sink->counter_for_bus("rcp.requests.total", 5) == 2.0);
    REQUIRE(sink->counter_for_bus("rcp.requests.total", 7) == 1.0);
}

TEST_CASE("observe: record_gauge is exercised directly and is not a dead no-op path",
          "[observe]") {
    auto sink = std::make_shared<CountingSink>();
    Metric m{"rcp.queue_depth", 4.0, /*stream_key=*/1, /*byte_bus_id=*/9};
    sink->record_gauge(m);

    auto gauges = sink->gauges();
    REQUIRE(gauges.size() == 1);
    REQUIRE(gauges[0].name == "rcp.queue_depth");
    REQUIRE(gauges[0].value == 4.0);
    REQUIRE(gauges[0].stream_key == 1);
    REQUIRE(gauges[0].byte_bus_id == 9);

    // NoopSink/InMemorySink must also tolerate a direct record_gauge() call
    // without crashing (previously only exercised indirectly, if at all).
    NoopSink noop;
    noop.record_gauge(m);
    InMemorySink mem;
    mem.record_gauge(m);
}

TEST_CASE("observe: span name is \"rcp.request\"", "[observe]") {
    auto sink = std::make_shared<InMemorySink>();
    auto oc   = new_observing_client(ok_request(), 1, sink);

    auto req = standard_request(1, 1);
    acf::AcfMessageInfo   resp;
    std::vector<uint8_t>  resp_payload;
    REQUIRE_FALSE(oc->request(Context{}, req, {}, resp, resp_payload));

    REQUIRE(sink->spans()[0].name == "rcp.request");
}

TEST_CASE("observe: sequential spans are recorded in call order", "[observe]") {
    auto sink = std::make_shared<InMemorySink>();
    auto oc   = new_observing_client(ok_request(), 1, sink);

    for (avtp::ByteBusId bus :
         {avtp::ByteBusId{3}, avtp::ByteBusId{1}, avtp::ByteBusId{4}, avtp::ByteBusId{1}, avtp::ByteBusId{5}}) {
        auto req = standard_request(bus, 0);
        acf::AcfMessageInfo   resp;
        std::vector<uint8_t>  resp_payload;
        REQUIRE_FALSE(oc->request(Context{}, req, {}, resp, resp_payload));
    }

    auto spans = sink->spans();
    REQUIRE(spans.size() == 5);
    std::vector<avtp::ByteBusId> expected{3, 1, 4, 1, 5};
    for (size_t i = 0; i < spans.size(); ++i) {
        REQUIRE(spans[i].byte_bus_id == expected[i]);
    }
}

// ── Gap-closure: standalone record() (analogous to c-RCP's rcp_observe_record())

TEST_CASE("observe: record() records a span/counters directly, with no "
          "ObservingClient/RequestFn involved", "[observe]") {
    auto sink = std::make_shared<CountingSink>();

    auto start = std::chrono::steady_clock::now();
    auto end   = start + std::chrono::microseconds(5);
    record(sink, "custom.span", /*byte_bus_id=*/5, /*stream_key=*/99,
           acf::kAcfMsgTypeGbb, start, end, std::error_code{});

    auto s = sink->last_span();
    REQUIRE(s.name == "custom.span");
    REQUIRE(s.byte_bus_id == 5);
    REQUIRE(s.stream_key == 99);
    REQUIRE(s.acf_msg_type == acf::kAcfMsgTypeGbb);
    REQUIRE_FALSE(s.result);
    REQUIRE(sink->counter("rcp.requests.total") == 1.0);
    REQUIRE(sink->counter("rcp.requests.errors") == 0.0);
}

TEST_CASE("observe: record() increments the error counter when result is set", "[observe]") {
    auto sink = std::make_shared<CountingSink>();
    auto now  = std::chrono::steady_clock::now();
    record(sink, "custom.span", 5, 99, acf::kAcfMsgTypeAbb, now, now, ErrClosed);

    REQUIRE(sink->last_span().result == ErrClosed);
    REQUIRE(sink->counter("rcp.requests.total") == 1.0);
    REQUIRE(sink->counter("rcp.requests.errors") == 1.0);
}
