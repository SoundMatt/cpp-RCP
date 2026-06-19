// fusa:test REQ-ADMIN-001
// fusa:test REQ-ADMIN-002
// fusa:test REQ-ADMIN-003
// fusa:test REQ-ADMIN-004
// fusa:test REQ-ADMIN-005
// fusa:test REQ-ADMIN-006
// fusa:test REQ-ADMIN-007
// fusa:test REQ-ADMIN-008
#include <catch2/catch_test_macros.hpp>

#include "rcp/admin.hpp"
#include "rcp/mock.hpp"

#include <atomic>
#include <thread>
#include <vector>

using namespace rcp;

TEST_CASE("admin: zones lists registered controllers", "[admin]") {
    mock::Registry reg;
    admin::AdminServer srv(reg);

    auto zones = srv.zones();
    REQUIRE(zones.size() == 5); // mock::Registry pre-populates all 5
    for (auto& zi : zones) {
        REQUIRE(zi.registered);
    }
}

TEST_CASE("admin: subscribe receives emitted events", "[admin]") {
    mock::Registry reg;
    admin::AdminServer srv(reg);

    std::vector<admin::Event> received;
    srv.subscribe([&](const admin::Event& ev) { received.push_back(ev); });

    srv.emit({admin::EventType::ZoneRegistered, Zone::FrontLeft, {}});
    srv.emit({admin::EventType::StatusUpdate, Zone::RearRight, {}});

    REQUIRE(received.size() == 2);
    REQUIRE(received[0].type == admin::EventType::ZoneRegistered);
    REQUIRE(received[1].zone == Zone::RearRight);
}

TEST_CASE("admin: metrics_text contains counter lines", "[admin]") {
    mock::Registry reg;
    admin::AdminServer srv(reg);

    srv.record_counter("rcp.commands.total", "zone=\"FrontLeft\"", 10.0);
    srv.record_counter("rcp.commands.total", "zone=\"FrontLeft\"", 5.0);

    auto text = srv.metrics_text();
    REQUIRE(text.find("rcp.commands.total") != std::string::npos);
    REQUIRE(text.find("15") != std::string::npos);
}

TEST_CASE("admin: multiple subscribers all receive events", "[admin]") {
    mock::Registry reg;
    admin::AdminServer srv(reg);

    int count_a = 0, count_b = 0;
    srv.subscribe([&](const admin::Event&) { ++count_a; });
    srv.subscribe([&](const admin::Event&) { ++count_b; });

    srv.emit({admin::EventType::ZoneDeregistered, Zone::Central, {}});

    REQUIRE(count_a == 1);
    REQUIRE(count_b == 1);
}

TEST_CASE("admin: event delivers correct type and zone", "[admin][REQ-ADMIN-008]") {
    mock::Registry reg;
    admin::AdminServer srv(reg);

    admin::Event got{};
    srv.subscribe([&](const admin::Event& ev) { got = ev; });
    srv.emit({admin::EventType::StatusUpdate, Zone::RearLeft, {}});

    REQUIRE(got.type == admin::EventType::StatusUpdate);
    REQUIRE(got.zone == Zone::RearLeft);
}

TEST_CASE("admin: concurrent record_counter and emit are thread-safe",
          "[admin][REQ-ADMIN-004][REQ-ADMIN-005]") {
    mock::Registry reg;
    admin::AdminServer srv(reg);

    std::atomic<int> events{0};
    srv.subscribe([&](const admin::Event&) { events.fetch_add(1); });

    constexpr int kThreads = 8;
    constexpr int kPerThread = 1000;
    std::vector<std::thread> ts;
    for (int t = 0; t < kThreads; ++t) {
        ts.emplace_back([&] {
            for (int i = 0; i < kPerThread; ++i) {
                srv.record_counter("rcp.commands.total", "zone=\"FrontLeft\"", 1.0);
                srv.emit({admin::EventType::StatusUpdate, Zone::FrontLeft, {}});
            }
        });
    }
    for (auto& th : ts) th.join();

    // record_counter accumulates every delta exactly once (REQ-ADMIN-004) and the
    // server tolerates concurrent mutation without data races (REQ-ADMIN-005).
    REQUIRE(events.load() == kThreads * kPerThread);
    auto text = srv.metrics_text();
    REQUIRE(text.find("rcp.commands.total") != std::string::npos);
    REQUIRE(text.find(std::to_string(kThreads * kPerThread)) != std::string::npos);
}
