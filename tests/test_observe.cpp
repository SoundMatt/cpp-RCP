// fusa:test REQ-OBS-001
// fusa:test REQ-OBS-002
// fusa:test REQ-OBS-003
// fusa:test REQ-OBS-004
// fusa:test REQ-OBS-005
// fusa:test REQ-OBS-006
// fusa:test REQ-OBS-007
// fusa:test REQ-OBS-008
#include <catch2/catch_test_macros.hpp>

#include "rcp/mock.hpp"
#include "rcp/observe.hpp"

#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace rcp;

namespace {
// CountingSink records counter deltas by metric name and the last span seen,
// so tests can assert on rcp.commands.total / rcp.commands.errors and span.result.
class CountingSink final : public observe::MetricsSink {
public:
    void record_span(const observe::Span& s) override {
        std::lock_guard<std::mutex> lk(mu_);
        last_span_ = s;
    }
    void record_gauge(const observe::Metric&) override {}
    void record_counter(const std::string& name, Zone, double delta) override {
        std::lock_guard<std::mutex> lk(mu_);
        counters_[name] += delta;
    }
    double counter(const std::string& name) const {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = counters_.find(name);
        return it == counters_.end() ? 0.0 : it->second;
    }
    observe::Span last_span() const {
        std::lock_guard<std::mutex> lk(mu_);
        return last_span_;
    }
private:
    mutable std::mutex mu_;
    std::map<std::string, double> counters_;
    observe::Span last_span_{};
};
} // namespace

TEST_CASE("observe: span recorded on successful send", "[observe]") {
    auto sink  = std::make_shared<observe::InMemorySink>();
    auto inner = std::make_shared<mock::Controller>(Zone::FrontLeft);
    auto ctrl  = observe::new_controller(inner, sink);

    Command cmd;
    cmd.zone = Zone::FrontLeft;
    cmd.type = CommandType::Get;
    Response resp;
    REQUIRE_FALSE(ctrl->send(Context{}, cmd, resp));

    REQUIRE(sink->span_count() == 1);
    auto spans = sink->spans();
    REQUIRE(spans[0].zone == Zone::FrontLeft);
    REQUIRE(spans[0].cmd_type == CommandType::Get);
    REQUIRE(!spans[0].result);
}

TEST_CASE("observe: multiple sends accumulate spans", "[observe]") {
    auto sink  = std::make_shared<observe::InMemorySink>();
    auto inner = std::make_shared<mock::Controller>(Zone::FrontLeft);
    auto ctrl  = observe::new_controller(inner, sink);

    for (int i = 0; i < 5; ++i) {
        Command cmd;
        cmd.zone = Zone::FrontLeft;
        cmd.type = CommandType::Set;
        Response resp;
        auto ec = ctrl->send(Context{}, cmd, resp);
        (void)ec;
    }

    REQUIRE(sink->span_count() == 5);
}

TEST_CASE("observe: span duration is non-negative", "[observe]") {
    auto sink  = std::make_shared<observe::InMemorySink>();
    auto inner = std::make_shared<mock::Controller>(Zone::Central);
    auto ctrl  = observe::new_controller(inner, sink);

    Command cmd;
    cmd.zone = Zone::Central;
    cmd.type = CommandType::Watchdog;
    Response resp;
    auto ec = ctrl->send(Context{}, cmd, resp);
    (void)ec;

    REQUIRE(sink->span_count() == 1);
    REQUIRE(sink->spans()[0].duration().count() >= 0);
}

TEST_CASE("observe: noop sink does not crash", "[observe][REQ-OBS-005]") {
    auto inner = std::make_shared<mock::Controller>(Zone::FrontLeft);
    auto ctrl  = observe::new_controller(inner); // default noop sink

    Command cmd;
    cmd.zone = Zone::FrontLeft;
    cmd.type = CommandType::Get;
    Response resp;
    REQUIRE_FALSE(ctrl->send(Context{}, cmd, resp));
}

TEST_CASE("observe: commands.total counter increments per send", "[observe][REQ-OBS-007]") {
    auto sink  = std::make_shared<CountingSink>();
    auto inner = std::make_shared<mock::Controller>(Zone::FrontLeft);
    auto ctrl  = observe::new_controller(inner, sink);

    for (int i = 0; i < 3; ++i) {
        Command cmd; cmd.zone = Zone::FrontLeft; cmd.type = CommandType::Set;
        Response resp;
        REQUIRE_FALSE(ctrl->send(Context{}, cmd, resp));
    }
    REQUIRE(sink->counter("rcp.commands.total") == 3.0);
    REQUIRE(sink->counter("rcp.commands.errors") == 0.0);
}

TEST_CASE("observe: span captures error code and error counter increments on failure",
          "[observe][REQ-OBS-004][REQ-OBS-008]") {
    auto sink  = std::make_shared<CountingSink>();
    auto inner = std::make_shared<mock::Controller>(Zone::FrontLeft);
    REQUIRE_FALSE(inner->close()); // a closed controller fails every send with ErrClosed
    auto ctrl  = observe::new_controller(inner, sink);

    Command cmd; cmd.zone = Zone::FrontLeft; cmd.type = CommandType::Get;
    Response resp;
    auto ec = ctrl->send(Context{}, cmd, resp);
    REQUIRE(ec == ErrClosed);

    REQUIRE(sink->last_span().result == ErrClosed);     // span records the error (REQ-OBS-004)
    REQUIRE(sink->counter("rcp.commands.total") == 1.0);
    REQUIRE(sink->counter("rcp.commands.errors") == 1.0); // error counter (REQ-OBS-008)
}

TEST_CASE("observe: InMemorySink is thread-safe under concurrent spans",
          "[observe][REQ-OBS-006]") {
    auto sink  = std::make_shared<observe::InMemorySink>();
    auto inner = std::make_shared<mock::Controller>(Zone::Central);
    auto ctrl  = observe::new_controller(inner, sink);

    constexpr int kThreads = 8;
    constexpr int kPerThread = 500;
    std::vector<std::thread> ts;
    for (int t = 0; t < kThreads; ++t) {
        ts.emplace_back([&] {
            for (int i = 0; i < kPerThread; ++i) {
                Command cmd; cmd.zone = Zone::Central; cmd.type = CommandType::Get;
                Response resp;
                auto ec = ctrl->send(Context{}, cmd, resp);
                (void)ec;
            }
        });
    }
    for (auto& th : ts) th.join();
    REQUIRE(sink->span_count() == kThreads * kPerThread);
}
