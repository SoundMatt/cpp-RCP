// fusa:req REQ-AUTH-001
// fusa:req REQ-AUTH-002
// fusa:req REQ-AUTH-003
// fusa:req REQ-AUTH-004
#include <catch2/catch_test_macros.hpp>

#include "rcp/authz.hpp"
#include "rcp/mock.hpp"

using namespace rcp;

static std::shared_ptr<mock::Controller> make_ctrl(Zone z = Zone::FrontLeft) {
    return std::make_shared<mock::Controller>(z);
}

TEST_CASE("authz: permitted identity succeeds", "[authz]") {
    auto policy = std::make_shared<authz::AccessPolicy>();
    policy->allow({"alice", {Zone::FrontLeft}, {CommandType::Set}});

    auto inner = make_ctrl();
    auto ctrl  = authz::new_controller(inner, policy);
    ctrl->set_identity("alice");

    Command cmd;
    cmd.zone = Zone::FrontLeft;
    cmd.type = CommandType::Set;
    Response resp;
    auto ec = ctrl->send(Context{}, cmd, resp);
    REQUIRE_FALSE(ec);
}

TEST_CASE("authz: denied identity returns ErrForbidden", "[authz]") {
    auto policy = std::make_shared<authz::AccessPolicy>();
    policy->allow({"alice", {Zone::FrontLeft}, {CommandType::Set}});

    auto inner = make_ctrl();
    auto ctrl  = authz::new_controller(inner, policy);
    ctrl->set_identity("eve");

    Command cmd;
    cmd.zone = Zone::FrontLeft;
    cmd.type = CommandType::Set;
    Response resp;
    auto ec = ctrl->send(Context{}, cmd, resp);
    REQUIRE(ec == authz::ErrForbidden);
}

TEST_CASE("authz: empty zones/types means all allowed", "[authz]") {
    auto policy = std::make_shared<authz::AccessPolicy>();
    policy->allow({"admin", {}, {}}); // all zones, all types

    auto inner = make_ctrl(Zone::Central);
    auto ctrl  = authz::new_controller(inner, policy);
    ctrl->set_identity("admin");

    Command cmd;
    cmd.zone = Zone::Central;
    cmd.type = CommandType::Reset;
    Response resp;
    REQUIRE_FALSE(ctrl->send(Context{}, cmd, resp));
}

TEST_CASE("authz: wrong zone is forbidden", "[authz]") {
    auto policy = std::make_shared<authz::AccessPolicy>();
    policy->allow({"alice", {Zone::FrontLeft}, {}});

    auto inner = make_ctrl(Zone::RearRight);
    auto ctrl  = authz::new_controller(inner, policy);
    ctrl->set_identity("alice");

    Command cmd;
    cmd.zone = Zone::RearRight;
    cmd.type = CommandType::Set;
    Response resp;
    auto ec = ctrl->send(Context{}, cmd, resp);
    REQUIRE(ec == authz::ErrForbidden);
}

TEST_CASE("authz: identity_fn overrides set_identity", "[authz]") {
    auto policy = std::make_shared<authz::AccessPolicy>();
    policy->allow({"dynamic", {}, {}});

    auto inner = make_ctrl();
    auto ctrl  = authz::new_controller(inner, policy, []{ return "dynamic"; });
    ctrl->set_identity("wrong");

    Command cmd;
    cmd.zone = Zone::FrontLeft;
    cmd.type = CommandType::Get;
    Response resp;
    REQUIRE_FALSE(ctrl->send(Context{}, cmd, resp));
}

TEST_CASE("authz: zone() delegates to inner", "[authz]") {
    auto policy = std::make_shared<authz::AccessPolicy>();
    auto inner  = make_ctrl(Zone::RearLeft);
    auto ctrl   = authz::new_controller(inner, policy);
    REQUIRE(ctrl->zone() == Zone::RearLeft);
}
