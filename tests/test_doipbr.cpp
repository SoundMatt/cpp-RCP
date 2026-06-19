// fusa:test REQ-DOIP-001
// fusa:test REQ-DOIP-002
// fusa:test REQ-DOIP-003
// fusa:test REQ-DOIP-004

// doipbr protocol-bridge stub conformance tests.
//
// The DoIpController is a compile-time interface stub: until a concrete backend is
// linked, send()/subscribe() return std::errc::function_not_supported, zone()
// reports the configured zone, and close() succeeds. These tests pin that
// contract so callers get a well-defined error rather than undefined behaviour.
#include <catch2/catch_test_macros.hpp>

#include "rcp/doipbr.hpp"

#include <system_error>

using namespace rcp;

namespace {
const std::error_code kUnsupported =
    std::make_error_code(std::errc::function_not_supported);
} // namespace

TEST_CASE("doipbr: send returns function_not_supported when stub", "[doipbr][REQ-DOIP-001]") {
    auto ctrl = doipbr::new_controller(Zone::FrontLeft, doipbr::Config{});
    Command cmd;
    cmd.zone = Zone::FrontLeft;
    cmd.type = CommandType::Set;
    Response resp;
    REQUIRE(ctrl->send(Context{}, cmd, resp) == kUnsupported);
}

TEST_CASE("doipbr: zone returns configured zone", "[doipbr][REQ-DOIP-002]") {
    auto ctrl = doipbr::new_controller(Zone::RearRight, doipbr::Config{});
    REQUIRE(ctrl->zone() == Zone::RearRight);
}

TEST_CASE("doipbr: subscribe returns function_not_supported when stub", "[doipbr][REQ-DOIP-003]") {
    auto ctrl = doipbr::new_controller(Zone::Central, doipbr::Config{});
    std::shared_ptr<StatusChannel> ch;
    REQUIRE(ctrl->subscribe(Context{}, ch) == kUnsupported);
}

TEST_CASE("doipbr: close returns no error", "[doipbr][REQ-DOIP-004]") {
    auto ctrl = doipbr::new_controller(Zone::FrontRight, doipbr::Config{});
    REQUIRE_FALSE(ctrl->close());
}
