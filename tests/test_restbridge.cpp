// fusa:test REQ-REST-001
// fusa:test REQ-REST-002
// fusa:test REQ-REST-003
// fusa:test REQ-REST-004

// restbridge protocol-bridge stub conformance tests (ROADMAP.md milestone
// 59, "Application-Layer Protocol Bridge Rebind", v2.15.0).
//
// RestBridge is a compile-time interface stub: until a concrete HTTP
// client backend is linked, request() returns
// std::errc::function_not_supported, stream_key()/endpoint() report the
// values supplied at construction, and close() succeeds. These tests pin
// that contract so callers get a well-defined error rather than undefined
// behaviour.
#include <catch2/catch_test_macros.hpp>

#include "rcp/restbridge.hpp"

#include <system_error>

using namespace rcp;

namespace {
const std::error_code kUnsupported =
    std::make_error_code(std::errc::function_not_supported);
} // namespace

TEST_CASE("restbridge: request returns function_not_supported when stub", "[restbridge][REQ-REST-001]") {
    auto bridge = restbridge::new_bridge(/*stream_key=*/1, /*endpoint=*/3, restbridge::Config{});
    auto req = acf::make_standard_request(3, 1, /*write=*/false, /*read_size=*/2);
    acf::AcfMessageInfo   resp;
    std::vector<uint8_t>  resp_payload;
    REQUIRE(bridge->request(Context{}, req, {}, resp, resp_payload) == kUnsupported);
}

TEST_CASE("restbridge: stream_key returns configured value", "[restbridge][REQ-REST-002]") {
    auto bridge = restbridge::new_bridge(42, 3, restbridge::Config{});
    REQUIRE(bridge->stream_key() == 42);
}

TEST_CASE("restbridge: endpoint returns configured value", "[restbridge][REQ-REST-003]") {
    auto bridge = restbridge::new_bridge(1, /*endpoint=*/9, restbridge::Config{});
    REQUIRE(bridge->endpoint() == 9);
}

TEST_CASE("restbridge: close returns no error", "[restbridge][REQ-REST-004]") {
    auto bridge = restbridge::new_bridge(1, 3, restbridge::Config{});
    REQUIRE_FALSE(bridge->close());
}
