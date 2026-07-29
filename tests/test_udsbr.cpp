// fusa:test REQ-UDS-001
// fusa:test REQ-UDS-002
// fusa:test REQ-UDS-003
// fusa:test REQ-UDS-004

// udsbr protocol-bridge stub conformance tests (ROADMAP.md milestone 59,
// "Application-Layer Protocol Bridge Rebind", v2.15.0).
//
// UdsBridge is a compile-time interface stub: until a concrete UDS stack
// is linked, request() returns std::errc::function_not_supported,
// stream_key()/endpoint() report the values supplied at construction, and
// close() succeeds. These tests pin that contract so callers get a
// well-defined error rather than undefined behaviour.
#include <catch2/catch_test_macros.hpp>

#include "rcp/udsbr.hpp"

#include <system_error>

using namespace rcp;

namespace {
const std::error_code kUnsupported =
    std::make_error_code(std::errc::function_not_supported);
} // namespace

TEST_CASE("udsbr: request returns function_not_supported when stub", "[udsbr][REQ-UDS-001]") {
    auto bridge = udsbr::new_bridge(/*stream_key=*/1, /*endpoint=*/3, udsbr::Config{});
    auto req = acf::make_standard_request(3, 1, /*write=*/false, /*read_size=*/2);
    acf::AcfMessageInfo   resp;
    std::vector<uint8_t>  resp_payload;
    REQUIRE(bridge->request(Context{}, req, {}, resp, resp_payload) == kUnsupported);
}

TEST_CASE("udsbr: stream_key returns configured value", "[udsbr][REQ-UDS-002]") {
    auto bridge = udsbr::new_bridge(42, 3, udsbr::Config{});
    REQUIRE(bridge->stream_key() == 42);
}

TEST_CASE("udsbr: endpoint returns configured value", "[udsbr][REQ-UDS-003]") {
    auto bridge = udsbr::new_bridge(1, /*endpoint=*/9, udsbr::Config{});
    REQUIRE(bridge->endpoint() == 9);
}

TEST_CASE("udsbr: close returns no error", "[udsbr][REQ-UDS-004]") {
    auto bridge = udsbr::new_bridge(1, 3, udsbr::Config{});
    REQUIRE_FALSE(bridge->close());
}
