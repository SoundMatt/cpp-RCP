// fusa:test REQ-LIN-001
// fusa:test REQ-LIN-002
// fusa:test REQ-LIN-003
// fusa:test REQ-LIN-004

// linbr protocol-bridge stub conformance tests.
//
// The LinController is a compile-time interface stub: until a concrete backend is
// linked, send()/subscribe() return std::errc::function_not_supported, zone()
// reports the configured zone, and close() succeeds. These tests pin that
// contract so callers get a well-defined error rather than undefined behaviour.
#include <catch2/catch_test_macros.hpp>

#include "rcp/linbr.hpp"

#include <system_error>

using namespace rcp;

namespace {
const std::error_code kUnsupported =
    std::make_error_code(std::errc::function_not_supported);
} // namespace

TEST_CASE("linbr: send returns function_not_supported when stub", "[linbr][REQ-LIN-001]") {
    auto ctrl = linbr::new_controller(Zone::FrontLeft, linbr::Config{});
    Command cmd;
    cmd.zone = Zone::FrontLeft;
    cmd.type = CommandType::Set;
    Response resp;
    REQUIRE(ctrl->send(Context{}, cmd, resp) == kUnsupported);
}

TEST_CASE("linbr: zone returns configured zone", "[linbr][REQ-LIN-002]") {
    auto ctrl = linbr::new_controller(Zone::RearRight, linbr::Config{});
    REQUIRE(ctrl->zone() == Zone::RearRight);
}

TEST_CASE("linbr: subscribe returns function_not_supported when stub", "[linbr][REQ-LIN-003]") {
    auto ctrl = linbr::new_controller(Zone::Central, linbr::Config{});
    std::shared_ptr<StatusChannel> ch;
    REQUIRE(ctrl->subscribe(Context{}, ch) == kUnsupported);
}

TEST_CASE("linbr: close returns no error", "[linbr][REQ-LIN-004]") {
    auto ctrl = linbr::new_controller(Zone::FrontRight, linbr::Config{});
    REQUIRE_FALSE(ctrl->close());
}
