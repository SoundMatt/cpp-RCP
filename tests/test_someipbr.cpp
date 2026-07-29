// fusa:test REQ-SOMEIP-001
// fusa:test REQ-SOMEIP-002
// fusa:test REQ-SOMEIP-003
// fusa:test REQ-SOMEIP-004

// someipbr protocol-bridge stub conformance tests (ROADMAP.md milestone
// 59, "Application-Layer Protocol Bridge Rebind", v2.15.0).
//
// SomeIpBridge is a compile-time interface stub: until a concrete vsomeip
// adapter is linked, request() returns std::errc::function_not_supported,
// stream_key()/endpoint() report the values supplied at construction, and
// close() succeeds. These tests pin that contract so callers get a
// well-defined error rather than undefined behaviour.
#include <catch2/catch_test_macros.hpp>

#include "rcp/someipbr.hpp"

#include <system_error>

using namespace rcp;

namespace {
const std::error_code kUnsupported =
    std::make_error_code(std::errc::function_not_supported);
} // namespace

TEST_CASE("someipbr: request returns function_not_supported when stub", "[someipbr][REQ-SOMEIP-001]") {
    auto bridge = someipbr::new_bridge(/*stream_key=*/1, /*endpoint=*/3, someipbr::Config{});
    auto req = acf::make_standard_request(3, 1, /*write=*/false, /*read_size=*/2);
    acf::AcfMessageInfo   resp;
    std::vector<uint8_t>  resp_payload;
    REQUIRE(bridge->request(Context{}, req, {}, resp, resp_payload) == kUnsupported);
}

TEST_CASE("someipbr: stream_key returns configured value", "[someipbr][REQ-SOMEIP-002]") {
    auto bridge = someipbr::new_bridge(42, 3, someipbr::Config{});
    REQUIRE(bridge->stream_key() == 42);
}

TEST_CASE("someipbr: endpoint returns configured value", "[someipbr][REQ-SOMEIP-003]") {
    auto bridge = someipbr::new_bridge(1, /*endpoint=*/9, someipbr::Config{});
    REQUIRE(bridge->endpoint() == 9);
}

TEST_CASE("someipbr: close returns no error", "[someipbr][REQ-SOMEIP-004]") {
    auto bridge = someipbr::new_bridge(1, 3, someipbr::Config{});
    REQUIRE_FALSE(bridge->close());
}
