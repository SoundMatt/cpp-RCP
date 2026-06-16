// fusa:req REQ-CFG-001
// fusa:req REQ-CFG-002
// fusa:req REQ-CFG-003
#include <catch2/catch_test_macros.hpp>

#include "rcp/config.hpp"
#include "rcp/mock.hpp"
#include "rcp/proxy.hpp"

using namespace rcp;

TEST_CASE("config: parse_json two zones", "[config]") {
    const std::string json = R"({
        "zones": [
            { "zone": "FrontLeft",  "priority": "Normal" },
            { "zone": "FrontRight", "priority": "High"   }
        ]
    })";

    auto m = config::parse_json(json);
    REQUIRE(m.zones.size() == 2);
    REQUIRE(m.zones[0].zone == Zone::FrontLeft);
    REQUIRE(m.zones[1].zone == Zone::FrontRight);
    REQUIRE(m.zones[1].priority == "High");
}

TEST_CASE("config: parse_json unknown zone throws", "[config]") {
    const std::string json = R"({ "zones": [{ "zone": "BadZone" }] })";
    REQUIRE_THROWS_AS(config::parse_json(json), config::ParseError);
}

TEST_CASE("config: load registers controllers", "[config]") {
    const std::string json = R"({
        "zones": [
            { "zone": "RearLeft"  },
            { "zone": "RearRight" }
        ]
    })";

    mock::Registry reg;
    // Remove pre-registered zones first (mock::Registry pre-populates all 5)
    // We test that load succeeds when zones are not yet registered.
    // Use a proxy registry which starts empty.
    proxy::ProxyRegistry preg;
    REQUIRE_FALSE(config::load(json, preg));

    std::shared_ptr<Controller> ctrl;
    REQUIRE_FALSE(preg.lookup(Zone::RearLeft, ctrl));
    REQUIRE_FALSE(preg.lookup(Zone::RearRight, ctrl));
}

TEST_CASE("config: load duplicate zone returns ErrAlreadyExists", "[config]") {
    const std::string json = R"({
        "zones": [
            { "zone": "Central" },
            { "zone": "Central" }
        ]
    })";

    proxy::ProxyRegistry preg;
    auto ec = config::load(json, preg);
    REQUIRE(ec == ErrAlreadyExists);
}
