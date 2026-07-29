// fusa:test REQ-DOIP-001
// fusa:test REQ-DOIP-002
// fusa:test REQ-DOIP-003
// fusa:test REQ-DOIP-004

// doipbr protocol-bridge stub conformance tests (ROADMAP.md milestone 59,
// "Application-Layer Protocol Bridge Rebind", v2.15.0).
//
// DoIpBridge is a compile-time interface stub: until a concrete DoIP
// stack is linked, request() returns std::errc::function_not_supported,
// stream_key()/endpoint() report the values supplied at construction, and
// close() succeeds. These tests pin that contract so callers get a
// well-defined error rather than undefined behaviour.
#include <catch2/catch_test_macros.hpp>

#include "rcp/doipbr.hpp"

#include <system_error>

using namespace rcp;

namespace {
const std::error_code kUnsupported =
    std::make_error_code(std::errc::function_not_supported);
} // namespace

TEST_CASE("doipbr: request returns function_not_supported when stub", "[doipbr][REQ-DOIP-001]") {
    auto bridge = doipbr::new_bridge(/*stream_key=*/1, /*endpoint=*/3, doipbr::Config{});
    auto req = acf::make_standard_request(3, 1, /*write=*/false, /*read_size=*/2);
    acf::AcfMessageInfo   resp;
    std::vector<uint8_t>  resp_payload;
    REQUIRE(bridge->request(Context{}, req, {}, resp, resp_payload) == kUnsupported);
}

TEST_CASE("doipbr: stream_key returns configured value", "[doipbr][REQ-DOIP-002]") {
    auto bridge = doipbr::new_bridge(42, 3, doipbr::Config{});
    REQUIRE(bridge->stream_key() == 42);
}

TEST_CASE("doipbr: endpoint returns configured value", "[doipbr][REQ-DOIP-003]") {
    auto bridge = doipbr::new_bridge(1, /*endpoint=*/9, doipbr::Config{});
    REQUIRE(bridge->endpoint() == 9);
}

TEST_CASE("doipbr: close returns no error", "[doipbr][REQ-DOIP-004]") {
    auto bridge = doipbr::new_bridge(1, 3, doipbr::Config{});
    REQUIRE_FALSE(bridge->close());
}
