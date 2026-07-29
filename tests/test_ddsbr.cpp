// fusa:test REQ-DDS-001
// fusa:test REQ-DDS-002
// fusa:test REQ-DDS-003
// fusa:test REQ-DDS-004

// ddsbr protocol-bridge stub conformance tests (ROADMAP.md milestone 59,
// "Application-Layer Protocol Bridge Rebind", v2.15.0).
//
// DdsBridge is a compile-time interface stub: until a concrete DDS
// backend is linked, request() returns std::errc::function_not_supported,
// stream_key()/endpoint() report the values supplied at construction, and
// close() succeeds. These tests pin that contract so callers get a
// well-defined error rather than undefined behaviour.
#include <catch2/catch_test_macros.hpp>

#include "rcp/ddsbr.hpp"

#include <system_error>

using namespace rcp;

namespace {
const std::error_code kUnsupported =
    std::make_error_code(std::errc::function_not_supported);
} // namespace

TEST_CASE("ddsbr: request returns function_not_supported when stub", "[ddsbr][REQ-DDS-001]") {
    auto bridge = ddsbr::new_bridge(/*stream_key=*/1, /*endpoint=*/3, ddsbr::Config{});
    auto req = acf::make_standard_request(3, 1, /*write=*/false, /*read_size=*/2);
    acf::AcfMessageInfo   resp;
    std::vector<uint8_t>  resp_payload;
    REQUIRE(bridge->request(Context{}, req, {}, resp, resp_payload) == kUnsupported);
}

TEST_CASE("ddsbr: stream_key returns configured value", "[ddsbr][REQ-DDS-002]") {
    auto bridge = ddsbr::new_bridge(42, 3, ddsbr::Config{});
    REQUIRE(bridge->stream_key() == 42);
}

TEST_CASE("ddsbr: endpoint returns configured value", "[ddsbr][REQ-DDS-003]") {
    auto bridge = ddsbr::new_bridge(1, /*endpoint=*/9, ddsbr::Config{});
    REQUIRE(bridge->endpoint() == 9);
}

TEST_CASE("ddsbr: close returns no error", "[ddsbr][REQ-DDS-004]") {
    auto bridge = ddsbr::new_bridge(1, 3, ddsbr::Config{});
    REQUIRE_FALSE(bridge->close());
}
