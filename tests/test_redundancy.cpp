// fusa:req REQ-RED-001
// fusa:req REQ-RED-002
// fusa:req REQ-RED-003
#include <catch2/catch_test_macros.hpp>

#include "rcp/mock.hpp"
#include "rcp/redundancy.hpp"

using namespace rcp;

TEST_CASE("redundancy: primary succeeds, standby unused", "[redundancy]") {
    auto primary = std::make_shared<mock::Controller>(Zone::FrontLeft);
    auto standby = std::make_shared<mock::Controller>(Zone::FrontLeft);

    auto ctrl = redundancy::new_controller(primary, standby);
    REQUIRE(ctrl->is_primary_active());

    Command cmd;
    cmd.zone = Zone::FrontLeft;
    cmd.type = CommandType::Get;
    Response resp;
    REQUIRE_FALSE(ctrl->send(Context{}, cmd, resp));
    REQUIRE(ctrl->is_primary_active());
}

TEST_CASE("redundancy: manual promote switches to standby", "[redundancy]") {
    auto primary = std::make_shared<mock::Controller>(Zone::FrontLeft);
    auto standby = std::make_shared<mock::Controller>(Zone::FrontLeft);
    auto ctrl    = redundancy::new_controller(primary, standby);

    ctrl->promote();
    REQUIRE_FALSE(ctrl->is_primary_active());

    Command cmd;
    cmd.zone = Zone::FrontLeft;
    cmd.type = CommandType::Get;
    Response resp;
    REQUIRE_FALSE(ctrl->send(Context{}, cmd, resp));
}

TEST_CASE("redundancy: double promote returns to primary", "[redundancy]") {
    auto primary = std::make_shared<mock::Controller>(Zone::FrontLeft);
    auto standby = std::make_shared<mock::Controller>(Zone::FrontLeft);
    auto ctrl    = redundancy::new_controller(primary, standby);

    ctrl->promote();
    ctrl->promote();
    REQUIRE(ctrl->is_primary_active());
}

TEST_CASE("redundancy: zone() returns zone of active controller", "[redundancy]") {
    auto primary = std::make_shared<mock::Controller>(Zone::RearRight);
    auto standby = std::make_shared<mock::Controller>(Zone::RearRight);
    auto ctrl    = redundancy::new_controller(primary, standby);
    REQUIRE(ctrl->zone() == Zone::RearRight);
}
