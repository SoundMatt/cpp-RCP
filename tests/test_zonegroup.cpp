// fusa:req REQ-ZG-001
// fusa:req REQ-ZG-002
// fusa:req REQ-ZG-003
#include <catch2/catch_test_macros.hpp>

#include "rcp/mock.hpp"
#include "rcp/proxy.hpp"
#include "rcp/zonegroup.hpp"

using namespace rcp;

TEST_CASE("zonegroup: ZoneGroup::all() has 5 zones", "[zonegroup]") {
    auto g = zonegroup::ZoneGroup::all();
    REQUIRE(g.size() == 5);
}

TEST_CASE("zonegroup: ZoneGroup::rear() has 2 zones", "[zonegroup]") {
    auto g = zonegroup::ZoneGroup::rear();
    REQUIRE(g.size() == 2);
}

TEST_CASE("zonegroup: send_group dispatches to all zones", "[zonegroup]") {
    mock::Registry reg;

    zonegroup::GroupRegistry gr(reg);

    Command cmd;
    cmd.type = CommandType::Get;

    auto resp = gr.send_group(Context{}, zonegroup::ZoneGroup::all(), cmd);
    REQUIRE(resp.ok());
    REQUIRE(resp.results.size() == 5);
}

TEST_CASE("zonegroup: send_group returns results per zone", "[zonegroup]") {
    mock::Registry reg;
    zonegroup::GroupRegistry gr(reg);

    Command cmd;
    cmd.type = CommandType::Set;

    auto resp = gr.send_group(Context{}, zonegroup::ZoneGroup::front(), cmd);
    REQUIRE(resp.results.count(Zone::FrontLeft));
    REQUIRE(resp.results.count(Zone::FrontRight));
    REQUIRE(resp.errors().empty());
}

TEST_CASE("zonegroup: missing zone returns error in result", "[zonegroup]") {
    mock::Registry reg;
    // Deregister all zones to create an empty registry
    auto ec = reg.close();
    (void)ec;

    // Fresh empty-ish registry: we need a test registry with no controllers
    // Use a proxy registry with no routes
    proxy::ProxyRegistry empty_reg;
    zonegroup::GroupRegistry gr(empty_reg);

    Command cmd;
    cmd.type = CommandType::Get;

    zonegroup::ZoneGroup g{Zone::FrontLeft};
    auto resp = gr.send_group(Context{}, g, cmd);
    REQUIRE_FALSE(resp.ok());
}
