// fusa:test REQ-REST-001
// fusa:test REQ-REST-002
// fusa:test REQ-REST-003
// fusa:test REQ-REST-004

// restbridge protocol-bridge stub conformance tests.
//
// The RestController is a compile-time interface stub: until a concrete backend is
// linked, send()/subscribe() return std::errc::function_not_supported, zone()
// reports the configured zone, and close() succeeds. These tests pin that
// contract so callers get a well-defined error rather than undefined behaviour.
#include <catch2/catch_test_macros.hpp>

#include "rcp/restbridge.hpp"

#include <system_error>

using namespace rcp;

namespace {
const std::error_code kUnsupported =
    std::make_error_code(std::errc::function_not_supported);
} // namespace

TEST_CASE("restbridge: send returns function_not_supported when stub", "[restbridge][REQ-REST-001]") {
    auto ctrl = restbridge::new_controller(Zone::FrontLeft, restbridge::Config{});
    Command cmd;
    cmd.zone = Zone::FrontLeft;
    cmd.type = CommandType::Set;
    Response resp;
    REQUIRE(ctrl->send(Context{}, cmd, resp) == kUnsupported);
}

TEST_CASE("restbridge: zone returns configured zone", "[restbridge][REQ-REST-002]") {
    auto ctrl = restbridge::new_controller(Zone::RearRight, restbridge::Config{});
    REQUIRE(ctrl->zone() == Zone::RearRight);
}

TEST_CASE("restbridge: subscribe returns function_not_supported when stub", "[restbridge][REQ-REST-003]") {
    auto ctrl = restbridge::new_controller(Zone::Central, restbridge::Config{});
    std::shared_ptr<StatusChannel> ch;
    REQUIRE(ctrl->subscribe(Context{}, ch) == kUnsupported);
}

TEST_CASE("restbridge: close returns no error", "[restbridge][REQ-REST-004]") {
    auto ctrl = restbridge::new_controller(Zone::FrontRight, restbridge::Config{});
    REQUIRE_FALSE(ctrl->close());
}
