// fusa:test REQ-RED-001
// fusa:test REQ-RED-002
// fusa:test REQ-RED-003
// fusa:test REQ-RED-004
// fusa:test REQ-RED-005
// fusa:test REQ-RED-006
// fusa:test REQ-RED-007
// fusa:test REQ-RED-008
#include <catch2/catch_test_macros.hpp>

#include "rcp/mock.hpp"
#include "rcp/redundancy.hpp"

#include <memory>

using namespace rcp;

namespace {
// FailController always fails send() with a fixed error code, regardless of the
// context. Used to deterministically trigger redundant failover.
class FailController final : public rcp::Controller {
public:
    FailController(Zone z, std::error_code ec) : zone_(z), ec_(ec) {}
    Zone zone() const noexcept override { return zone_; }
    std::error_code send(const Context&, const Command&, Response&) override { return ec_; }
    std::error_code subscribe(const Context&, std::shared_ptr<StatusChannel>&) override {
        return ec_;
    }
    std::error_code close() override { return {}; }
private:
    Zone zone_;
    std::error_code ec_;
};
} // namespace

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

TEST_CASE("redundancy: zone() returns zone of active controller", "[redundancy][REQ-RED-008]") {
    auto primary = std::make_shared<mock::Controller>(Zone::RearRight);
    auto standby = std::make_shared<mock::Controller>(Zone::RearRight);
    auto ctrl    = redundancy::new_controller(primary, standby);
    REQUIRE(ctrl->zone() == Zone::RearRight);
}

TEST_CASE("redundancy: auto-promotes standby on ErrClosed", "[redundancy][REQ-RED-002]") {
    auto primary = std::make_shared<FailController>(Zone::FrontLeft, ErrClosed);
    auto standby = std::make_shared<mock::Controller>(Zone::FrontLeft);
    auto ctrl    = redundancy::new_controller(primary, standby);
    REQUIRE(ctrl->is_primary_active());

    Command cmd; cmd.zone = Zone::FrontLeft; cmd.type = CommandType::Get;
    Response resp;
    REQUIRE_FALSE(ctrl->send(Context{}, cmd, resp)); // succeeds via standby
    REQUIRE_FALSE(ctrl->is_primary_active());         // standby is now active
}

TEST_CASE("redundancy: auto-promotes standby on ErrTimeout", "[redundancy][REQ-RED-003]") {
    auto primary = std::make_shared<FailController>(Zone::FrontLeft, ErrTimeout);
    auto standby = std::make_shared<mock::Controller>(Zone::FrontLeft);
    auto ctrl    = redundancy::new_controller(primary, standby);

    Command cmd; cmd.zone = Zone::FrontLeft; cmd.type = CommandType::Get;
    Response resp;
    REQUIRE_FALSE(ctrl->send(Context{}, cmd, resp));
    REQUIRE_FALSE(ctrl->is_primary_active());
}

TEST_CASE("redundancy: auto_promote=false disables automatic failover",
          "[redundancy][REQ-RED-007]") {
    auto primary = std::make_shared<FailController>(Zone::FrontLeft, ErrClosed);
    auto standby = std::make_shared<mock::Controller>(Zone::FrontLeft);
    redundancy::Config cfg;
    cfg.auto_promote = false;
    auto ctrl = redundancy::new_controller(primary, standby, cfg);

    Command cmd; cmd.zone = Zone::FrontLeft; cmd.type = CommandType::Get;
    Response resp;
    REQUIRE(ctrl->send(Context{}, cmd, resp) == ErrClosed); // error surfaced, no failover
    REQUIRE(ctrl->is_primary_active());                      // still on primary
}

TEST_CASE("redundancy: close() closes both controllers", "[redundancy][REQ-RED-006]") {
    auto primary = std::make_shared<mock::Controller>(Zone::Central);
    auto standby = std::make_shared<mock::Controller>(Zone::Central);
    auto ctrl    = redundancy::new_controller(primary, standby);

    REQUIRE_FALSE(ctrl->close());

    Command cmd; cmd.zone = Zone::Central; Response resp;
    REQUIRE(primary->send(Context{}, cmd, resp) == ErrClosed);
    REQUIRE(standby->send(Context{}, cmd, resp) == ErrClosed);
}
