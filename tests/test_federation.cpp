// fusa:test REQ-FED-001
// fusa:test REQ-FED-002
// fusa:test REQ-FED-003
// fusa:test REQ-FED-004
// fusa:test REQ-FED-005
// fusa:test REQ-FED-006
// fusa:test REQ-FED-007
// fusa:test REQ-FED-008
#include <catch2/catch_test_macros.hpp>

#include "rcp/federation.hpp"
#include "rcp/mock.hpp"

using namespace rcp;

TEST_CASE("federation: local controller preferred over lease", "[federation]") {
    auto reg = federation::new_registry("hpc-a");
    auto local_ctrl = std::make_shared<mock::Controller>(Zone::FrontLeft);
    REQUIRE_FALSE(reg->register_ctrl(local_ctrl));

    // Add a lease for the same zone — local should win
    auto remote_ctrl = std::make_shared<mock::Controller>(Zone::FrontLeft);
    federation::Lease lease;
    lease.owner       = "hpc-b";
    lease.expires_at  = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    lease.remote_ctrl = remote_ctrl;
    REQUIRE_FALSE(reg->add_lease(Zone::FrontLeft, lease));

    std::shared_ptr<Controller> ctrl;
    REQUIRE_FALSE(reg->lookup(Zone::FrontLeft, ctrl));
    REQUIRE(ctrl.get() == local_ctrl.get()); // local wins
}

TEST_CASE("federation: remote lease used when no local", "[federation][REQ-FED-004]") {
    auto reg = federation::new_registry("hpc-a");

    auto remote_ctrl = std::make_shared<mock::Controller>(Zone::RearLeft);
    federation::Lease lease;
    lease.owner       = "hpc-b";
    lease.expires_at  = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    lease.remote_ctrl = remote_ctrl;
    REQUIRE_FALSE(reg->add_lease(Zone::RearLeft, lease));

    std::shared_ptr<Controller> ctrl;
    REQUIRE_FALSE(reg->lookup(Zone::RearLeft, ctrl));
    REQUIRE(ctrl.get() == remote_ctrl.get());
}

TEST_CASE("federation: expired lease returns ErrNotFound", "[federation]") {
    auto reg = federation::new_registry("hpc-a");

    auto remote_ctrl = std::make_shared<mock::Controller>(Zone::Central);
    federation::Lease lease;
    lease.owner       = "hpc-b";
    lease.expires_at  = std::chrono::steady_clock::now() - std::chrono::seconds(1); // expired
    lease.remote_ctrl = remote_ctrl;
    REQUIRE_FALSE(reg->add_lease(Zone::Central, lease));

    std::shared_ptr<Controller> ctrl;
    auto ec = reg->lookup(Zone::Central, ctrl);
    REQUIRE(ec == ErrNotFound);
}

TEST_CASE("federation: revoke_lease removes lease", "[federation]") {
    auto reg = federation::new_registry("hpc-a");

    auto remote_ctrl = std::make_shared<mock::Controller>(Zone::FrontRight);
    federation::Lease lease;
    lease.owner       = "hpc-b";
    lease.expires_at  = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    lease.remote_ctrl = remote_ctrl;
    REQUIRE_FALSE(reg->add_lease(Zone::FrontRight, lease));
    REQUIRE_FALSE(reg->revoke_lease(Zone::FrontRight));

    std::shared_ptr<Controller> ctrl;
    REQUIRE(reg->lookup(Zone::FrontRight, ctrl) == ErrNotFound);
}

TEST_CASE("federation: local_id is preserved", "[federation][REQ-FED-005]") {
    auto reg = federation::new_registry("hpc-main");
    REQUIRE(reg->local_id() == "hpc-main");
}

TEST_CASE("federation: register_ctrl rejects duplicate zone", "[federation][REQ-FED-008]") {
    auto reg = federation::new_registry("hpc-a");
    REQUIRE_FALSE(reg->register_ctrl(std::make_shared<mock::Controller>(Zone::Central)));
    auto ec = reg->register_ctrl(std::make_shared<mock::Controller>(Zone::Central));
    REQUIRE(ec == ErrAlreadyExists);
}

TEST_CASE("federation: close closes local and remote controllers", "[federation][REQ-FED-006]") {
    auto reg = federation::new_registry("hpc-a");

    auto local_ctrl = std::make_shared<mock::Controller>(Zone::FrontLeft);
    REQUIRE_FALSE(reg->register_ctrl(local_ctrl));

    auto remote_ctrl = std::make_shared<mock::Controller>(Zone::RearLeft);
    federation::Lease lease;
    lease.owner       = "hpc-b";
    lease.expires_at  = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    lease.remote_ctrl = remote_ctrl;
    REQUIRE_FALSE(reg->add_lease(Zone::RearLeft, lease));

    REQUIRE_FALSE(reg->close());

    // Both the local and the leased remote controller are now closed.
    Command cmd; cmd.zone = Zone::FrontLeft; Response resp;
    REQUIRE(local_ctrl->send(Context{}, cmd, resp) == ErrClosed);
    Command cmd2; cmd2.zone = Zone::RearLeft; Response resp2;
    REQUIRE(remote_ctrl->send(Context{}, cmd2, resp2) == ErrClosed);
}

TEST_CASE("federation: closed registry returns ErrClosed on lookup", "[federation][REQ-FED-007]") {
    auto reg = federation::new_registry("hpc-a");
    REQUIRE_FALSE(reg->close());
    std::shared_ptr<Controller> ctrl;
    REQUIRE(reg->lookup(Zone::FrontLeft, ctrl) == ErrClosed);
}
