// fusa:req REQ-FED-001
// fusa:req REQ-FED-002
// fusa:req REQ-FED-003
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

TEST_CASE("federation: remote lease used when no local", "[federation]") {
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

TEST_CASE("federation: local_id is preserved", "[federation]") {
    auto reg = federation::new_registry("hpc-main");
    REQUIRE(reg->local_id() == "hpc-main");
}
