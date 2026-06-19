// fusa:test REQ-CAN-001
// fusa:test REQ-CAN-002
// fusa:test REQ-CAN-003
// fusa:test REQ-CAN-004

// canbr protocol-bridge stub conformance tests.
//
// The CanController is a compile-time interface stub: until a concrete backend is
// linked, send()/subscribe() return std::errc::function_not_supported, zone()
// reports the configured zone, and close() succeeds. These tests pin that
// contract so callers get a well-defined error rather than undefined behaviour.
#include <catch2/catch_test_macros.hpp>

#include "rcp/canbr.hpp"

#include <system_error>

using namespace rcp;

namespace {
const std::error_code kUnsupported =
    std::make_error_code(std::errc::function_not_supported);
} // namespace

TEST_CASE("canbr: send returns function_not_supported when stub", "[canbr][REQ-CAN-001]") {
    auto ctrl = canbr::new_controller(Zone::FrontLeft, canbr::Config{});
    Command cmd;
    cmd.zone = Zone::FrontLeft;
    cmd.type = CommandType::Set;
    Response resp;
    REQUIRE(ctrl->send(Context{}, cmd, resp) == kUnsupported);
}

TEST_CASE("canbr: zone returns configured zone", "[canbr][REQ-CAN-002]") {
    auto ctrl = canbr::new_controller(Zone::RearRight, canbr::Config{});
    REQUIRE(ctrl->zone() == Zone::RearRight);
}

TEST_CASE("canbr: subscribe returns function_not_supported when stub", "[canbr][REQ-CAN-003]") {
    auto ctrl = canbr::new_controller(Zone::Central, canbr::Config{});
    std::shared_ptr<StatusChannel> ch;
    REQUIRE(ctrl->subscribe(Context{}, ch) == kUnsupported);
}

TEST_CASE("canbr: close returns no error", "[canbr][REQ-CAN-004]") {
    auto ctrl = canbr::new_controller(Zone::FrontRight, canbr::Config{});
    REQUIRE_FALSE(ctrl->close());
}
