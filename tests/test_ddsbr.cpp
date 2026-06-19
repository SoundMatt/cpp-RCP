// fusa:test REQ-DDS-001
// fusa:test REQ-DDS-002
// fusa:test REQ-DDS-003
// fusa:test REQ-DDS-004

// ddsbr protocol-bridge stub conformance tests.
//
// The DdsController is a compile-time interface stub: until a concrete backend is
// linked, send()/subscribe() return std::errc::function_not_supported, zone()
// reports the configured zone, and close() succeeds. These tests pin that
// contract so callers get a well-defined error rather than undefined behaviour.
#include <catch2/catch_test_macros.hpp>

#include "rcp/ddsbr.hpp"

#include <system_error>

using namespace rcp;

namespace {
const std::error_code kUnsupported =
    std::make_error_code(std::errc::function_not_supported);
} // namespace

TEST_CASE("ddsbr: send returns function_not_supported when stub", "[ddsbr][REQ-DDS-001]") {
    auto ctrl = ddsbr::new_controller(Zone::FrontLeft, ddsbr::Config{});
    Command cmd;
    cmd.zone = Zone::FrontLeft;
    cmd.type = CommandType::Set;
    Response resp;
    REQUIRE(ctrl->send(Context{}, cmd, resp) == kUnsupported);
}

TEST_CASE("ddsbr: zone returns configured zone", "[ddsbr][REQ-DDS-002]") {
    auto ctrl = ddsbr::new_controller(Zone::RearRight, ddsbr::Config{});
    REQUIRE(ctrl->zone() == Zone::RearRight);
}

TEST_CASE("ddsbr: subscribe returns function_not_supported when stub", "[ddsbr][REQ-DDS-003]") {
    auto ctrl = ddsbr::new_controller(Zone::Central, ddsbr::Config{});
    std::shared_ptr<StatusChannel> ch;
    REQUIRE(ctrl->subscribe(Context{}, ch) == kUnsupported);
}

TEST_CASE("ddsbr: close returns no error", "[ddsbr][REQ-DDS-004]") {
    auto ctrl = ddsbr::new_controller(Zone::FrontRight, ddsbr::Config{});
    REQUIRE_FALSE(ctrl->close());
}
