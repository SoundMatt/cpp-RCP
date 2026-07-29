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
#include <vector>

using namespace rcp;
using namespace rcp::observe;

namespace {
// CountingSink records counter deltas by metric name and the last span
// seen, so tests can assert on rcp.requests.total / rcp.requests.errors
// and span.result.
class CountingSink final : public MetricsSink {
public:
    void record_span(const Span& s) override {
        std::lock_guard<std::mutex> lk(mu_);
        last_span_ = s;
    }
    void record_gauge(const Metric&) override {}
    void record_counter(const std::string& name, uint64_t, double delta) override {
        std::lock_guard<std::mutex> lk(mu_);
        counters_[name] += delta;
    }
    double counter(const std::string& name) const {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = counters_.find(name);
        return it == counters_.end() ? 0.0 : it->second;
    }
    Span last_span() const {
        std::lock_guard<std::mutex> lk(mu_);
        return last_span_;
    }
private:
    mutable std::mutex mu_;
    std::map<std::string, double> counters_;
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
