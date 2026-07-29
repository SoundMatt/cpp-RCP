// fusa:test REQ-TSN-001
// fusa:test REQ-TSN-002
// fusa:test REQ-TSN-003
// fusa:test REQ-TSN-004
// fusa:test REQ-TSN-005
// fusa:test REQ-TSN-006

// TSN / IEEE 802.1p priority-mapping tests (ROADMAP.md milestone 58,
// "Auxiliary Transport & Cross-Cutting Rebind", v2.14.0).
//
// apply_priority() maps rcp::request::RequestCategory to an 802.1p PCP
// value and applies SO_PRIORITY to the given socket fd. The setsockopt()
// call itself is platform-gated (Linux); these tests pin the
// category -> PCP mapping and the portable fd-validation behavior.
#include <catch2/catch_test_macros.hpp>

#include "rcp/tsn.hpp"

using namespace rcp;
using rcp::request::RequestCategory;

TEST_CASE("tsn: PCPMap maps every RequestCategory to a distinct PCP value",
          "[tsn][REQ-TSN-002]") {
    auto m = tsn::default_pcp_map();
    REQUIRE(tsn::pcp_for(m, RequestCategory::Cancellation)  == m.cancellation);
    REQUIRE(tsn::pcp_for(m, RequestCategory::Triggered)     == m.triggered);
    REQUIRE(tsn::pcp_for(m, RequestCategory::Timed)         == m.timed);
    REQUIRE(tsn::pcp_for(m, RequestCategory::Compound)      == m.compound);
    REQUIRE(tsn::pcp_for(m, RequestCategory::CompoundWait)  == m.compound_wait);
    REQUIRE(tsn::pcp_for(m, RequestCategory::Chained)       == m.chained);
    REQUIRE(tsn::pcp_for(m, RequestCategory::Standard)      == m.standard);
}

TEST_CASE("tsn: Cancellation (highest execution priority) maps to the highest PCP",
          "[tsn][REQ-TSN-003]") {
    auto m = tsn::default_pcp_map();
    REQUIRE(tsn::pcp_for(m, RequestCategory::Cancellation) == 7);
    REQUIRE(tsn::pcp_for(m, RequestCategory::Cancellation) >
            tsn::pcp_for(m, RequestCategory::Triggered));
}

TEST_CASE("tsn: Triggered maps to a mid/high-range PCP", "[tsn][REQ-TSN-004]") {
    auto m = tsn::default_pcp_map();
    auto triggered = tsn::pcp_for(m, RequestCategory::Triggered);
    REQUIRE(triggered == 6);
    REQUIRE(triggered < tsn::pcp_for(m, RequestCategory::Cancellation));
    REQUIRE(triggered > tsn::pcp_for(m, RequestCategory::Standard));
}

TEST_CASE("tsn: Standard (the mandatory baseline kind) maps to the lowest PCP",
          "[tsn][REQ-TSN-005]") {
    auto m = tsn::default_pcp_map();
    REQUIRE(tsn::pcp_for(m, RequestCategory::Standard) == 1);
    // Every priority-rank ordering the specification defines across the
    // seven categories (extraction §3.14) is preserved in the PCP mapping:
    // higher execution priority (lower rank) never yields a lower PCP.
    REQUIRE(tsn::pcp_for(m, RequestCategory::Standard) <
            tsn::pcp_for(m, RequestCategory::Chained));
    REQUIRE(tsn::pcp_for(m, RequestCategory::Chained) <
            tsn::pcp_for(m, RequestCategory::CompoundWait));
    REQUIRE(tsn::pcp_for(m, RequestCategory::CompoundWait) <
            tsn::pcp_for(m, RequestCategory::Compound));
    REQUIRE(tsn::pcp_for(m, RequestCategory::Compound) <
            tsn::pcp_for(m, RequestCategory::Timed));
    REQUIRE(tsn::pcp_for(m, RequestCategory::Timed) <
            tsn::pcp_for(m, RequestCategory::Triggered));
}

TEST_CASE("tsn: apply_priority succeeds (as a portable no-op where SO_PRIORITY "
          "is unavailable) for a valid fd", "[tsn][REQ-TSN-001]") {
    // fd = 0 is a valid descriptor value (stdin) so this exercises the real
    // code path on every platform without needing an actual UDP socket;
    // on non-Linux builds RCP_TSN_SO_PRIORITY is undefined and this is
    // unconditionally a no-op success.
    auto ec = tsn::apply_priority(0, tsn::default_tsn_config(), RequestCategory::Cancellation);
#if defined(RCP_TSN_SO_PRIORITY)
    // setsockopt on fd 0 (not a socket) fails on Linux -- apply_priority
    // must propagate that failure rather than silently swallowing it.
    REQUIRE(ec);
#else
    REQUIRE_FALSE(ec);
#endif
}

TEST_CASE("tsn: apply_priority rejects a negative fd on every platform",
          "[tsn][REQ-TSN-006]") {
    auto ec = tsn::apply_priority(-1, tsn::default_tsn_config(), RequestCategory::Standard);
    REQUIRE(ec == std::make_error_code(std::errc::invalid_argument));
}
