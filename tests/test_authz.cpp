// fusa:test REQ-AUTH-001
// fusa:test REQ-AUTH-002
// fusa:test REQ-AUTH-003
// fusa:test REQ-AUTH-004
// fusa:test REQ-AUTH-005
// fusa:test REQ-AUTH-006
// fusa:test REQ-AUTH-007
// fusa:test REQ-AUTH-008
#include <catch2/catch_test_macros.hpp>

#include "rcp/authz.hpp"
#include "rcp/mock.hpp"

#include <atomic>
#include <thread>
#include <vector>

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

TEST_CASE("authz: zone() delegates to inner", "[authz][REQ-AUTH-005]") {
    auto policy = std::make_shared<authz::AccessPolicy>();
    auto inner  = make_ctrl(Zone::RearLeft);
    auto ctrl   = authz::new_controller(inner, policy);
    REQUIRE(ctrl->zone() == Zone::RearLeft);
}

TEST_CASE("authz: subscribe() delegates to inner", "[authz][REQ-AUTH-006]") {
    auto policy = std::make_shared<authz::AccessPolicy>();
    auto inner  = make_ctrl(Zone::FrontLeft);
    auto ctrl   = authz::new_controller(inner, policy);

    std::shared_ptr<StatusChannel> ch;
    auto ec = ctrl->subscribe(Context{}, ch);
    REQUIRE_FALSE(ec);
    REQUIRE(ch != nullptr);
    REQUIRE_FALSE(ctrl->close());
}

TEST_CASE("authz: close() delegates to inner", "[authz][REQ-AUTH-007]") {
    auto policy = std::make_shared<authz::AccessPolicy>();
    auto inner  = make_ctrl(Zone::FrontLeft);
    auto ctrl   = authz::new_controller(inner, policy);

    REQUIRE_FALSE(ctrl->close());
    // After the inner controller is closed, sends through it report ErrClosed.
    Command cmd;
    cmd.zone = Zone::FrontLeft;
    Response resp;
    REQUIRE(inner->send(Context{}, cmd, resp) == ErrClosed);
}

TEST_CASE("authz: ErrForbidden is a distinct error code", "[authz][REQ-AUTH-008]") {
    REQUIRE(authz::ErrForbidden);
    REQUIRE(authz::ErrForbidden != ErrClosed);
    REQUIRE(authz::ErrForbidden != ErrBusy);
    REQUIRE(std::string(authz::ErrForbidden.category().name()) == "rcp.authz");
}

TEST_CASE("authz: AccessPolicy permits concurrently without data races",
          "[authz][REQ-AUTH-004]") {
    auto policy = std::make_shared<authz::AccessPolicy>();
    policy->allow({"alice", {Zone::FrontLeft}, {CommandType::Set}});

    std::atomic<int> permits{0};
    std::vector<std::thread> ts;
    for (int t = 0; t < 8; ++t) {
        ts.emplace_back([&] {
            for (int i = 0; i < 5000; ++i) {
                // Concurrent readers (permit) interleaved with a writer (allow).
                if (policy->permit("alice", Zone::FrontLeft, CommandType::Set))
                    permits.fetch_add(1, std::memory_order_relaxed);
                if ((i & 0x3ff) == 0)
                    policy->allow({"tmp", {Zone::Central}, {}});
            }
        });
    }
    for (auto& th : ts) th.join();
    REQUIRE(permits.load() == 8 * 5000);
}
