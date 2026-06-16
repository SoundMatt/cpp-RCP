// fusa:req REQ-ADMIN-001
// fusa:req REQ-ADMIN-002
// fusa:req REQ-ADMIN-003
#include <catch2/catch_test_macros.hpp>

#include "rcp/admin.hpp"
#include "rcp/mock.hpp"

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
