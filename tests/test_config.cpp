// fusa:test REQ-CFG-001
// fusa:test REQ-CFG-002
// fusa:test REQ-CFG-003
// fusa:test REQ-CFG-004
// fusa:test REQ-CFG-005
// fusa:test REQ-CFG-006
//
// Rebound (cpp-RCP-FS-03/#86): config::load now bootstraps an
// rcp::shmem::Registry (keyed by stream_key) from a stream_key/byte_bus_id
// manifest instead of registering rcp::legacy_mock::Controller instances
// into the retired rcp::Registry keyed by Zone — see rcp/config.hpp's own
// header comment.
#include <catch2/catch_test_macros.hpp>

#include "rcp/config.hpp"

#include <string>

using namespace rcp;

TEST_CASE("config: parse_json two endpoints on one stream", "[config]") {
    const std::string json = R"({
        "endpoints": [
            { "stream_key": "0", "byte_bus_id": 1, "priority": "Normal" },
            { "stream_key": "0", "byte_bus_id": 2, "priority": "High"   }
        ]
    })";

    auto m = config::parse_json(json);
    REQUIRE(m.endpoints.size() == 2);
    REQUIRE(m.endpoints[0].stream_key == 0);
    REQUIRE(m.endpoints[0].byte_bus_id == 1);
    REQUIRE(m.endpoints[1].byte_bus_id == 2);
    REQUIRE(m.endpoints[1].priority == "High");
}

TEST_CASE("config: parse_json accepts a hex stream_key", "[config]") {
    const std::string json = R"({
        "endpoints": [ { "stream_key": "0x2A", "byte_bus_id": 1 } ]
    })";

    auto m = config::parse_json(json);
    REQUIRE(m.endpoints.size() == 1);
    REQUIRE(m.endpoints[0].stream_key == 0x2A);
}

TEST_CASE("config: parse_json out-of-range byte_bus_id throws", "[config]") {
    const std::string json = R"({ "endpoints": [{ "stream_key": "0", "byte_bus_id": 300 }] })";
    REQUIRE_THROWS_AS(config::parse_json(json), config::ParseError);
}

TEST_CASE("config: parse_json invalid stream_key throws", "[config]") {
    const std::string json = R"({ "endpoints": [{ "stream_key": "not-a-number", "byte_bus_id": 1 }] })";
    REQUIRE_THROWS_AS(config::parse_json(json), config::ParseError);
}

TEST_CASE("config: load bootstraps one channel per distinct stream_key", "[config]") {
    const std::string json = R"({
        "endpoints": [
            { "stream_key": "1", "byte_bus_id": 1 },
            { "stream_key": "1", "byte_bus_id": 2 },
            { "stream_key": "2", "byte_bus_id": 1 }
        ]
    })";

    shmem::Registry reg;
    REQUIRE_FALSE(config::load(json, reg));

    std::shared_ptr<shmem::Channel> ch;
    REQUIRE_FALSE(reg.lookup(1, ch));
    REQUIRE_FALSE(reg.lookup(2, ch));
    REQUIRE(reg.channels().size() == 2); // one channel per distinct stream_key, not per endpoint
}

TEST_CASE("config: load returns ErrAlreadyExists for a stream_key already in the registry",
          "[config]") {
    const std::string json = R"({ "endpoints": [{ "stream_key": "5", "byte_bus_id": 1 }] })";

    shmem::Registry reg;
    REQUIRE_FALSE(reg.add_channel(shmem::new_channel(5)));

    auto ec = config::load(json, reg);
    REQUIRE(ec == ErrAlreadyExists);
}

TEST_CASE("config: ParseError is a std::runtime_error subclass", "[config][REQ-CFG-006]") {
    // Catchable as std::runtime_error (and thus std::exception) and carries a message.
    bool caught = false;
    try {
        config::parse_json(R"({ "endpoints": [{ "stream_key": "0", "byte_bus_id": 999 }] })");
    } catch (const std::runtime_error& e) {
        caught = true;
        REQUIRE(std::string(e.what()).find("999") != std::string::npos);
    }
    REQUIRE(caught);
    REQUIRE(std::is_base_of<std::runtime_error, config::ParseError>::value);
}
