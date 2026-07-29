// fusa:test REQ-MQTT-001
// fusa:test REQ-MQTT-002
// fusa:test REQ-MQTT-003
// fusa:test REQ-MQTT-004

// mqttbr protocol-bridge stub conformance tests (ROADMAP.md milestone 59,
// "Application-Layer Protocol Bridge Rebind", v2.15.0).
//
// MqttBridge is a compile-time interface stub: until a concrete MQTT
// backend is linked, request() returns std::errc::function_not_supported,
// stream_key()/endpoint() report the values supplied at construction, and
// close() succeeds. These tests pin that contract so callers get a
// well-defined error rather than undefined behaviour.
#include <catch2/catch_test_macros.hpp>

#include "rcp/mqttbr.hpp"

#include <system_error>

using namespace rcp;

namespace {
const std::error_code kUnsupported =
    std::make_error_code(std::errc::function_not_supported);
} // namespace

TEST_CASE("mqttbr: request returns function_not_supported when stub", "[mqttbr][REQ-MQTT-001]") {
    auto bridge = mqttbr::new_bridge(/*stream_key=*/1, /*endpoint=*/3, mqttbr::Config{});
    auto req = acf::make_standard_request(3, 1, /*write=*/false, /*read_size=*/2);
    acf::AcfMessageInfo   resp;
    std::vector<uint8_t>  resp_payload;
    REQUIRE(bridge->request(Context{}, req, {}, resp, resp_payload) == kUnsupported);
}

TEST_CASE("mqttbr: stream_key returns configured value", "[mqttbr][REQ-MQTT-002]") {
    auto bridge = mqttbr::new_bridge(42, 3, mqttbr::Config{});
    REQUIRE(bridge->stream_key() == 42);
}

TEST_CASE("mqttbr: endpoint returns configured value", "[mqttbr][REQ-MQTT-003]") {
    auto bridge = mqttbr::new_bridge(1, /*endpoint=*/9, mqttbr::Config{});
    REQUIRE(bridge->endpoint() == 9);
}

TEST_CASE("mqttbr: close returns no error", "[mqttbr][REQ-MQTT-004]") {
    auto bridge = mqttbr::new_bridge(1, 3, mqttbr::Config{});
    REQUIRE_FALSE(bridge->close());
}
