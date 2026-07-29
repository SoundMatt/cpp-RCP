// fusa:test REQ-GRPC-001
// fusa:test REQ-GRPC-002
// fusa:test REQ-GRPC-003
// fusa:test REQ-GRPC-004

// grpcbridge protocol-bridge stub conformance tests (ROADMAP.md milestone
// 59, "Application-Layer Protocol Bridge Rebind", v2.15.0).
//
// GrpcBridge is a compile-time interface stub: until a concrete gRPC
// adapter is linked, request() returns std::errc::function_not_supported,
// stream_key()/endpoint() report the values supplied at construction, and
// close() succeeds. These tests pin that contract so callers get a
// well-defined error rather than undefined behaviour.
#include <catch2/catch_test_macros.hpp>

#include "rcp/grpcbridge.hpp"

#include <system_error>

using namespace rcp;

namespace {
const std::error_code kUnsupported =
    std::make_error_code(std::errc::function_not_supported);
} // namespace

TEST_CASE("grpcbridge: request returns function_not_supported when stub", "[grpcbridge][REQ-GRPC-001]") {
    auto bridge = grpcbridge::new_bridge(/*stream_key=*/1, /*endpoint=*/3, grpcbridge::Config{});
    auto req = acf::make_standard_request(3, 1, /*write=*/false, /*read_size=*/2);
    acf::AcfMessageInfo   resp;
    std::vector<uint8_t>  resp_payload;
    REQUIRE(bridge->request(Context{}, req, {}, resp, resp_payload) == kUnsupported);
}

TEST_CASE("grpcbridge: stream_key returns configured value", "[grpcbridge][REQ-GRPC-002]") {
    auto bridge = grpcbridge::new_bridge(42, 3, grpcbridge::Config{});
    REQUIRE(bridge->stream_key() == 42);
}

TEST_CASE("grpcbridge: endpoint returns configured value", "[grpcbridge][REQ-GRPC-003]") {
    auto bridge = grpcbridge::new_bridge(1, /*endpoint=*/9, grpcbridge::Config{});
    REQUIRE(bridge->endpoint() == 9);
}

TEST_CASE("grpcbridge: close returns no error", "[grpcbridge][REQ-GRPC-004]") {
    auto bridge = grpcbridge::new_bridge(1, 3, grpcbridge::Config{});
    REQUIRE_FALSE(bridge->close());
}
