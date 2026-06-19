// fusa:test REQ-TSN-001
// fusa:test REQ-TSN-002
// fusa:test REQ-TSN-003
// fusa:test REQ-TSN-004
// fusa:test REQ-TSN-005
// fusa:test REQ-TSN-006

// TSN / IEEE 802.1p priority-mapping tests.
//
// The TSN wrapper maps rcp::Priority to an 802.1p PCP value and applies
// SO_PRIORITY to the egress socket before delegating to the inner controller.
// The setsockopt() call is platform-gated (Linux); these tests pin the
// priority→PCP mapping and the pass-through behaviour, which are portable.
#include <catch2/catch_test_macros.hpp>

#include "rcp/mock.hpp"
#include "rcp/tsn.hpp"

using namespace rcp;

TEST_CASE("tsn: PCPMap maps Normal/High/Critical to PCP values", "[tsn][REQ-TSN-002]") {
    auto m = tsn::default_pcp_map();
    REQUIRE(tsn::pcp_for(m, Priority::Normal)   == m.normal);
    REQUIRE(tsn::pcp_for(m, Priority::High)     == m.high);
    REQUIRE(tsn::pcp_for(m, Priority::Critical) == m.critical);
}

TEST_CASE("tsn: Critical maps to the highest PCP", "[tsn][REQ-TSN-003]") {
    auto m = tsn::default_pcp_map();
    REQUIRE(tsn::pcp_for(m, Priority::Critical) == 7);
    REQUIRE(tsn::pcp_for(m, Priority::Critical) > tsn::pcp_for(m, Priority::High));
}

TEST_CASE("tsn: High maps to a mid-range PCP", "[tsn][REQ-TSN-004]") {
    auto m = tsn::default_pcp_map();
    auto high = tsn::pcp_for(m, Priority::High);
    REQUIRE(high == 5);
    REQUIRE(high > tsn::pcp_for(m, Priority::Normal));
    REQUIRE(high < tsn::pcp_for(m, Priority::Critical));
}

TEST_CASE("tsn: Normal maps to a low PCP", "[tsn][REQ-TSN-005]") {
    auto m = tsn::default_pcp_map();
    REQUIRE(tsn::pcp_for(m, Priority::Normal) == 2);
    REQUIRE(tsn::pcp_for(m, Priority::Normal) < tsn::pcp_for(m, Priority::High));
}

TEST_CASE("tsn: send applies priority class then delegates to inner",
          "[tsn][REQ-TSN-001]") {
    // Capture the command type/priority the inner controller actually receives,
    // proving the wrapper forwards after applying the (platform-gated) PCP.
    Priority seen = Priority::Normal;
    auto inner = std::make_shared<mock::Controller>(
        Zone::FrontLeft, [&](const Command& c) {
            seen = c.priority;
            return Response{c.id, Zone::FrontLeft, ResponseStatus::OK, {}};
        });
    // fd = -1 → SO_PRIORITY is skipped, send still delegates.
    auto ctrl = tsn::new_controller(inner, -1, tsn::default_tsn_config());

    Command cmd;
    cmd.zone     = Zone::FrontLeft;
    cmd.type     = CommandType::Set;
    cmd.priority = Priority::Critical;
    Response resp;
    REQUIRE_FALSE(ctrl->send(Context{}, cmd, resp));
    REQUIRE(resp.status == ResponseStatus::OK);
    REQUIRE(seen == Priority::Critical); // forwarded unchanged to the inner controller
}

TEST_CASE("tsn: subscribe delegates to inner", "[tsn][REQ-TSN-006]") {
    auto inner = std::make_shared<mock::Controller>(Zone::Central);
    auto ctrl  = tsn::new_controller(inner, -1, tsn::default_tsn_config());

    std::shared_ptr<StatusChannel> ch;
    REQUIRE_FALSE(ctrl->subscribe(Context{}, ch));
    REQUIRE(ch != nullptr);
    REQUIRE(ctrl->zone() == Zone::Central);
    REQUIRE_FALSE(ctrl->close());
}
