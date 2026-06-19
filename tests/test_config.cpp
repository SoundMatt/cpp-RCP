// fusa:test REQ-CFG-001
// fusa:test REQ-CFG-002
// fusa:test REQ-CFG-003
// fusa:test REQ-CFG-004
// fusa:test REQ-CFG-005
// fusa:test REQ-CFG-006
#include <catch2/catch_test_macros.hpp>

#include "rcp/config.hpp"
#include "rcp/mock.hpp"
#include "rcp/proxy.hpp"

#include <stdexcept>
#include <string>

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

TEST_CASE("config: ParseError is a std::runtime_error subclass", "[config][REQ-CFG-006]") {
    // Catchable as std::runtime_error (and thus std::exception) and carries a message.
    bool caught = false;
    try {
        config::parse_json(R"({ "zones": [{ "zone": "Nope" }] })");
    } catch (const std::runtime_error& e) {
        caught = true;
        REQUIRE(std::string(e.what()).find("Nope") != std::string::npos);
    }
    REQUIRE(caught);
    REQUIRE(std::is_base_of<std::runtime_error, config::ParseError>::value);
}
