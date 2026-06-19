// fusa:test REQ-SOMEIP-001
// fusa:test REQ-SOMEIP-002
// fusa:test REQ-SOMEIP-003
// fusa:test REQ-SOMEIP-004

// someipbr protocol-bridge stub conformance tests.
//
// The SomeIpController is a compile-time interface stub: until a concrete backend is
// linked, send()/subscribe() return std::errc::function_not_supported, zone()
// reports the configured zone, and close() succeeds. These tests pin that
// contract so callers get a well-defined error rather than undefined behaviour.
#include <catch2/catch_test_macros.hpp>

#include "rcp/someipbr.hpp"

#include <system_error>

using namespace rcp;

namespace {
const std::error_code kUnsupported =
    std::make_error_code(std::errc::function_not_supported);
} // namespace

TEST_CASE("someipbr: send returns function_not_supported when stub", "[someipbr][REQ-SOMEIP-001]") {
    auto ctrl = someipbr::new_controller(Zone::FrontLeft, someipbr::Config{});
    Command cmd;
    cmd.zone = Zone::FrontLeft;
    cmd.type = CommandType::Set;
    Response resp;
    REQUIRE(ctrl->send(Context{}, cmd, resp) == kUnsupported);
}

TEST_CASE("someipbr: zone returns configured zone", "[someipbr][REQ-SOMEIP-002]") {
    auto ctrl = someipbr::new_controller(Zone::RearRight, someipbr::Config{});
    REQUIRE(ctrl->zone() == Zone::RearRight);
}

TEST_CASE("someipbr: subscribe returns function_not_supported when stub", "[someipbr][REQ-SOMEIP-003]") {
    auto ctrl = someipbr::new_controller(Zone::Central, someipbr::Config{});
    std::shared_ptr<StatusChannel> ch;
    REQUIRE(ctrl->subscribe(Context{}, ch) == kUnsupported);
}

TEST_CASE("someipbr: close returns no error", "[someipbr][REQ-SOMEIP-004]") {
    auto ctrl = someipbr::new_controller(Zone::FrontRight, someipbr::Config{});
    REQUIRE_FALSE(ctrl->close());
}
