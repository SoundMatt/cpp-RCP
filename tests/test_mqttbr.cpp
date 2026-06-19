// fusa:test REQ-MQTT-001
// fusa:test REQ-MQTT-002
// fusa:test REQ-MQTT-003
// fusa:test REQ-MQTT-004

// mqttbr protocol-bridge stub conformance tests.
//
// The MqttController is a compile-time interface stub: until a concrete backend is
// linked, send()/subscribe() return std::errc::function_not_supported, zone()
// reports the configured zone, and close() succeeds. These tests pin that
// contract so callers get a well-defined error rather than undefined behaviour.
#include <catch2/catch_test_macros.hpp>

#include "rcp/mqttbr.hpp"

#include <system_error>

using namespace rcp;

namespace {
const std::error_code kUnsupported =
    std::make_error_code(std::errc::function_not_supported);
} // namespace

TEST_CASE("mqttbr: send returns function_not_supported when stub", "[mqttbr][REQ-MQTT-001]") {
    auto ctrl = mqttbr::new_controller(Zone::FrontLeft, mqttbr::Config{});
    Command cmd;
    cmd.zone = Zone::FrontLeft;
    cmd.type = CommandType::Set;
    Response resp;
    REQUIRE(ctrl->send(Context{}, cmd, resp) == kUnsupported);
}

TEST_CASE("mqttbr: zone returns configured zone", "[mqttbr][REQ-MQTT-002]") {
    auto ctrl = mqttbr::new_controller(Zone::RearRight, mqttbr::Config{});
    REQUIRE(ctrl->zone() == Zone::RearRight);
}

TEST_CASE("mqttbr: subscribe returns function_not_supported when stub", "[mqttbr][REQ-MQTT-003]") {
    auto ctrl = mqttbr::new_controller(Zone::Central, mqttbr::Config{});
    std::shared_ptr<StatusChannel> ch;
    REQUIRE(ctrl->subscribe(Context{}, ch) == kUnsupported);
}

TEST_CASE("mqttbr: close returns no error", "[mqttbr][REQ-MQTT-004]") {
    auto ctrl = mqttbr::new_controller(Zone::FrontRight, mqttbr::Config{});
    REQUIRE_FALSE(ctrl->close());
}
