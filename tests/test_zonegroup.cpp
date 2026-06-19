// fusa:test REQ-ZG-001
// fusa:test REQ-ZG-002
// fusa:test REQ-ZG-003
// fusa:test REQ-ZG-004
// fusa:test REQ-ZG-005
// fusa:test REQ-ZG-006
#include <catch2/catch_test_macros.hpp>

#include "rcp/mock.hpp"
#include "rcp/proxy.hpp"
#include "rcp/zonegroup.hpp"

#include <algorithm>
#include <chrono>

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

TEST_CASE("zonegroup: errors() lists every failing zone", "[zonegroup][REQ-ZG-003][REQ-ZG-004]") {
    proxy::ProxyRegistry empty_reg; // no routes → every lookup fails
    zonegroup::GroupRegistry gr(empty_reg);

    Command cmd; cmd.type = CommandType::Get;
    auto resp = gr.send_group(Context{}, zonegroup::ZoneGroup::front(), cmd);

    REQUIRE_FALSE(resp.ok());
    auto errs = resp.errors();
    REQUIRE(errs.size() == 2);
    REQUIRE(std::find(errs.begin(), errs.end(), Zone::FrontLeft)  != errs.end());
    REQUIRE(std::find(errs.begin(), errs.end(), Zone::FrontRight) != errs.end());
}

TEST_CASE("zonegroup: ZoneGroup is a copyable value type", "[zonegroup][REQ-ZG-005]") {
    zonegroup::ZoneGroup a{Zone::FrontLeft, Zone::FrontRight};
    zonegroup::ZoneGroup b = a;   // copy construct
    b.add(Zone::Central);         // mutate the copy only

    REQUIRE(a.size() == 2);       // original is unaffected → value semantics
    REQUIRE(b.size() == 3);

    zonegroup::ZoneGroup c;
    c = a;                        // copy assign
    REQUIRE(c.size() == 2);
}

TEST_CASE("zonegroup: send_group honours the caller context deadline",
          "[zonegroup][REQ-ZG-006]") {
    mock::Registry reg;
    zonegroup::GroupRegistry gr(reg);

    Command cmd; cmd.type = CommandType::Get;
    // An already-expired deadline must propagate to every per-zone send.
    auto ctx = Context::with_deadline(std::chrono::steady_clock::now() -
                                      std::chrono::seconds(1));
    auto resp = gr.send_group(ctx, zonegroup::ZoneGroup::all(), cmd);

    REQUIRE_FALSE(resp.ok());
    REQUIRE(resp.results.size() == 5);
    for (auto& kv : resp.results) {
        REQUIRE(kv.second.error == ErrTimeout); // deadline observed downstream
    }
}
