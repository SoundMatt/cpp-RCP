// fusa:test REQ-UDS-001
// fusa:test REQ-UDS-002
// fusa:test REQ-UDS-003
// fusa:test REQ-UDS-004

// udsbr protocol-bridge stub conformance tests.
//
// The UdsController is a compile-time interface stub: until a concrete backend is
// linked, send()/subscribe() return std::errc::function_not_supported, zone()
// reports the configured zone, and close() succeeds. These tests pin that
// contract so callers get a well-defined error rather than undefined behaviour.
#include <catch2/catch_test_macros.hpp>

#include "rcp/udsbr.hpp"

#include <system_error>

using namespace rcp;

namespace {
const std::error_code kUnsupported =
    std::make_error_code(std::errc::function_not_supported);
} // namespace

TEST_CASE("udsbr: send returns function_not_supported when stub", "[udsbr][REQ-UDS-001]") {
    auto ctrl = udsbr::new_controller(Zone::FrontLeft, udsbr::Config{});
    Command cmd;
    cmd.zone = Zone::FrontLeft;
    cmd.type = CommandType::Set;
    Response resp;
    REQUIRE(ctrl->send(Context{}, cmd, resp) == kUnsupported);
}

TEST_CASE("udsbr: zone returns configured zone", "[udsbr][REQ-UDS-002]") {
    auto ctrl = udsbr::new_controller(Zone::RearRight, udsbr::Config{});
    REQUIRE(ctrl->zone() == Zone::RearRight);
}

TEST_CASE("udsbr: subscribe returns function_not_supported when stub", "[udsbr][REQ-UDS-003]") {
    auto ctrl = udsbr::new_controller(Zone::Central, udsbr::Config{});
    std::shared_ptr<StatusChannel> ch;
    REQUIRE(ctrl->subscribe(Context{}, ch) == kUnsupported);
}

TEST_CASE("udsbr: close returns no error", "[udsbr][REQ-UDS-004]") {
    auto ctrl = udsbr::new_controller(Zone::FrontRight, udsbr::Config{});
    REQUIRE_FALSE(ctrl->close());
}
