// fusa:test REQ-GRPC-001
// fusa:test REQ-GRPC-002
// fusa:test REQ-GRPC-003
// fusa:test REQ-GRPC-004

// grpcbridge protocol-bridge stub conformance tests.
//
// The GrpcController is a compile-time interface stub: until a concrete backend is
// linked, send()/subscribe() return std::errc::function_not_supported, zone()
// reports the configured zone, and close() succeeds. These tests pin that
// contract so callers get a well-defined error rather than undefined behaviour.
#include <catch2/catch_test_macros.hpp>

#include "rcp/grpcbridge.hpp"

#include <system_error>

using namespace rcp;

namespace {
const std::error_code kUnsupported =
    std::make_error_code(std::errc::function_not_supported);
} // namespace

TEST_CASE("grpcbridge: send returns function_not_supported when stub", "[grpcbridge][REQ-GRPC-001]") {
    auto ctrl = grpcbridge::new_controller(Zone::FrontLeft, grpcbridge::Config{});
    Command cmd;
    cmd.zone = Zone::FrontLeft;
    cmd.type = CommandType::Set;
    Response resp;
    REQUIRE(ctrl->send(Context{}, cmd, resp) == kUnsupported);
}

TEST_CASE("grpcbridge: zone returns configured zone", "[grpcbridge][REQ-GRPC-002]") {
    auto ctrl = grpcbridge::new_controller(Zone::RearRight, grpcbridge::Config{});
    REQUIRE(ctrl->zone() == Zone::RearRight);
}

TEST_CASE("grpcbridge: subscribe returns function_not_supported when stub", "[grpcbridge][REQ-GRPC-003]") {
    auto ctrl = grpcbridge::new_controller(Zone::Central, grpcbridge::Config{});
    std::shared_ptr<StatusChannel> ch;
    REQUIRE(ctrl->subscribe(Context{}, ch) == kUnsupported);
}

TEST_CASE("grpcbridge: close returns no error", "[grpcbridge][REQ-GRPC-004]") {
    auto ctrl = grpcbridge::new_controller(Zone::FrontRight, grpcbridge::Config{});
    REQUIRE_FALSE(ctrl->close());
}
