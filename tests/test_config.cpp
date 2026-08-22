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

// ── Gap-closure (parity audit vs c-RCP's config.c/config.h) ───────────────────

TEST_CASE("config: parse_json test-gap closure — \"extra\" metadata is actually asserted",
          "[config]") {
    const std::string json = R"({
        "endpoints": [ { "stream_key": "0", "byte_bus_id": 1, "extra": "note-42" } ]
    })";
    auto m = config::parse_json(json);
    REQUIRE(m.endpoints.size() == 1);
    REQUIRE(m.endpoints[0].extra == "note-42");
}

TEST_CASE("config: bug-fix — an endpoint entry missing byte_bus_id is now rejected, "
          "not silently skipped", "[config]") {
    // Previously: an object carrying only "stream_key" (or only
    // "byte_bus_id") failed the old "both keys present" routing check and
    // was dropped with no error at all — the same latent defect class
    // c-RCP's own config.c independently found and fixed. It must now be
    // rejected as malformed.
    const std::string json = R"({ "endpoints": [{ "stream_key": "0" }] })";
    REQUIRE_THROWS_AS(config::parse_json(json), config::ParseError);
}

TEST_CASE("config: bug-fix — an endpoint entry missing stream_key is now rejected, "
          "not silently skipped", "[config]") {
    const std::string json = R"({ "endpoints": [{ "byte_bus_id": 1 }] })";
    REQUIRE_THROWS_AS(config::parse_json(json), config::ParseError);
}

TEST_CASE("config: parse_json parses the \"server\" block (vendor_id/device_id/magic)",
          "[config]") {
    const std::string json =
        R"({ "server": { "vendor_id": 17, "device_id": 42, "magic": 12345 } })";
    auto m = config::parse_json(json);
    REQUIRE(m.server.vendor_id == 17);
    REQUIRE(m.server.device_id == 42);
    REQUIRE(m.server.magic == 12345);
}

TEST_CASE("config: parse_json parses svr_implemented_options named bits "
          "(REQ-RMAP-030 five independent bits)", "[config]") {
    const std::string json =
        R"({ "server": { "svr_implemented_options": ["time_sync", "compound_bundles"] } })";
    auto m = config::parse_json(json);
    REQUIRE((m.server.svr_implemented_options & regmap::kOptTimeSync)     != 0);
    REQUIRE((m.server.svr_implemented_options & regmap::kOptCompoundWait) != 0);
    REQUIRE((m.server.svr_implemented_options & regmap::kOptEnhCancel)    == 0);
    REQUIRE((m.server.svr_implemented_options & regmap::kOptTrigger)      == 0);
    REQUIRE((m.server.svr_implemented_options & regmap::kOptChained)      == 0);
}

TEST_CASE("config: parse_json parses svr_implemented_options' trigger/chained bits",
          "[config]") {
    const std::string json =
        R"({ "server": { "svr_implemented_options": ["trigger", "chained"] } })";
    auto m = config::parse_json(json);
    REQUIRE((m.server.svr_implemented_options & regmap::kOptTrigger) != 0);
    REQUIRE((m.server.svr_implemented_options & regmap::kOptChained) != 0);
}

TEST_CASE("config: parse_json parses a \"hw_pin_map\" array", "[config]") {
    const std::string json = R"({
        "hw_pin_map": [
            { "hw_ep_nr": 0, "hw_ep_pin_nr": 3, "hw_pin_type": ["push_pull", "pull_up"] }
        ]
    })";
    auto m = config::parse_json(json);
    REQUIRE(m.hw_pin_map.size() == 1);
    REQUIRE(m.hw_pin_map[0].hw_ep_nr == 0);
    REQUIRE(m.hw_pin_map[0].hw_ep_pin_nr == 3);
    REQUIRE((m.hw_pin_map[0].hw_pin_type & regmap::hw_pin::kStagePushPull) != 0);
    REQUIRE((m.hw_pin_map[0].hw_pin_type & regmap::hw_pin::kPullUp)       != 0);
}

TEST_CASE("config: parse_json hw_pin_map entry missing hw_ep_pin_nr throws", "[config]") {
    const std::string json = R"({ "hw_pin_map": [{ "hw_ep_nr": 0 }] })";
    REQUIRE_THROWS_AS(config::parse_json(json), config::ParseError);
}

TEST_CASE("config: parse_json empty object parses to an all-default manifest", "[config]") {
    auto m = config::parse_json("{}");
    REQUIRE(m.endpoints.empty());
    REQUIRE(m.hw_pin_map.empty());
    REQUIRE(m.server.vendor_id == 0);
}

TEST_CASE("config: apply_to_mock writes vendor_id/device_id/magic/svr_implemented_options "
          "into the mock::Server's regmap", "[config]") {
    const std::string json = R"({
        "server": { "vendor_id": 7, "device_id": 9, "magic": 555,
                     "svr_implemented_options": ["time_sync"] }
    })";
    mock::Server srv;
    REQUIRE_FALSE(config::apply_to_mock(config::parse_json(json), srv));

    REQUIRE(srv.registers().general.vendor_id == 7);
    REQUIRE(srv.registers().general.device_id == 9);
    REQUIRE(srv.registers().general.magic == 555);
    REQUIRE((srv.registers().general.svr_implemented_options & regmap::kOptTimeSync) != 0);
}

TEST_CASE("config: apply_to_mock preserves the existing magic when the manifest's is zero",
          "[config]") {
    mock::Server srv;
    auto original_magic = srv.registers().general.magic;
    REQUIRE(original_magic != 0); // GeneralMap defaults magic to a real, nonzero value

    REQUIRE_FALSE(config::apply_to_mock(config::parse_json(R"({ "server": { "vendor_id": 1 } })"), srv));
    REQUIRE(srv.registers().general.magic == original_magic); // untouched
}

TEST_CASE("config: apply_to_mock ORs svr_implemented_options into existing bits, "
          "never clearing what was already set", "[config]") {
    mock::Server srv;
    srv.registers().general.svr_implemented_options = regmap::kOptChained;

    REQUIRE_FALSE(config::apply_to_mock(
        config::parse_json(R"({ "server": { "svr_implemented_options": ["time_sync"] } })"), srv));

    REQUIRE((srv.registers().general.svr_implemented_options & regmap::kOptChained)  != 0);
    REQUIRE((srv.registers().general.svr_implemented_options & regmap::kOptTimeSync) != 0);
}

TEST_CASE("config: apply_to_mock installs hw_pin_map rows into the mock::Server's regmap",
          "[config]") {
    const std::string json = R"({
        "hw_pin_map": [
            { "hw_ep_nr": 1, "hw_ep_pin_nr": 2, "hw_pin_type": ["schmitt_trigger"] }
        ]
    })";
    mock::Server srv;
    REQUIRE_FALSE(config::apply_to_mock(config::parse_json(json), srv));

    REQUIRE(srv.registers().hw_pin_map.size() == 1);
    REQUIRE(srv.registers().hw_pin_map[0].hw_ep_nr == 1);
    REQUIRE(srv.registers().hw_pin_map[0].hw_ep_pin_nr == 2);
    REQUIRE((srv.registers().hw_pin_map[0].hw_pin_type & regmap::hw_pin::kSchmittTrigger) != 0);
    REQUIRE(srv.registers().hw_pin_map_table.capacity == 1);
}

TEST_CASE("config: apply_to_mock rejects a hw_pin_map larger than kMaxEntries", "[config]") {
    config::Manifest m;
    m.hw_pin_map.resize(regmap::hw_pin_map::kMaxEntries + 1);

    mock::Server srv;
    auto ec = config::apply_to_mock(m, srv);
    REQUIRE(ec == std::make_error_code(std::errc::value_too_large));
}

TEST_CASE("config: load_to_mock combines parse_json + apply_to_mock in one call", "[config]") {
    const std::string json = R"({ "server": { "vendor_id": 3 } })";
    mock::Server srv;
    REQUIRE_FALSE(config::load_to_mock(json, srv));
    REQUIRE(srv.registers().general.vendor_id == 3);
}
