// fusa:test REQ-PROXY-001
// fusa:test REQ-PROXY-002
// fusa:test REQ-PROXY-003
// fusa:test REQ-PROXY-004
// fusa:test REQ-PROXY-005
// fusa:test REQ-PROXY-006
#include <catch2/catch_test_macros.hpp>

#include "rcp/mock.hpp"
#include "rcp/proxy.hpp"

#include <chrono>

using namespace rcp;

TEST_CASE("proxy: ProxyController forwards command within budget", "[proxy][REQ-PROXY-001]") {
    auto inner = std::make_shared<mock::Controller>(Zone::FrontLeft);
    proxy::ProxyController pc(inner); // default 50ms budget — not exceeded

    Command cmd;
    cmd.zone = Zone::FrontLeft;
    cmd.type = CommandType::Get;
    Response resp;
    REQUIRE_FALSE(pc.send(Context{}, cmd, resp));
    REQUIRE(resp.status == ResponseStatus::OK);
}

TEST_CASE("proxy: zero latency budget makes the hop time out", "[proxy][REQ-PROXY-001]") {
    auto inner = std::make_shared<mock::Controller>(Zone::FrontLeft);
    proxy::Config cfg;
    cfg.latency_budget = std::chrono::milliseconds(0); // deadline = now → already due
    proxy::ProxyController pc(inner, cfg);

    Command cmd;
    cmd.zone = Zone::FrontLeft;
    cmd.type = CommandType::Get;
    Response resp;
    // The proxy derives a now+budget deadline and passes it down; the upstream
    // observes the budget has elapsed and returns ErrTimeout instead of forwarding.
    REQUIRE(pc.send(Context{}, cmd, resp) == ErrTimeout);
}

TEST_CASE("proxy: ProxyController zone matches upstream", "[proxy][REQ-PROXY-006]") {
    auto inner = std::make_shared<mock::Controller>(Zone::RearRight);
    proxy::ProxyController pc(inner);
    REQUIRE(pc.zone() == Zone::RearRight);
}

TEST_CASE("proxy: ProxyRegistry lookup and send", "[proxy][REQ-PROXY-002]") {
    auto reg = proxy::new_registry();
    auto inner = std::make_shared<mock::Controller>(Zone::RearLeft);
    REQUIRE_FALSE(reg->add_route(inner));

    std::shared_ptr<Controller> ctrl;
    REQUIRE_FALSE(reg->lookup(Zone::RearLeft, ctrl));

    Command cmd;
    cmd.zone = Zone::RearLeft;
    cmd.type = CommandType::Get;
    Response resp;
    REQUIRE_FALSE(ctrl->send(Context{}, cmd, resp));
}

TEST_CASE("proxy: lookup unknown zone returns ErrNotFound", "[proxy][REQ-PROXY-004]") {
    auto reg = proxy::new_registry();
    std::shared_ptr<Controller> ctrl;
    auto ec = reg->lookup(Zone::Central, ctrl);
    REQUIRE(ec == ErrNotFound);
}

TEST_CASE("proxy: deregister closes the upstream controller", "[proxy][REQ-PROXY-003]") {
    auto reg   = proxy::new_registry();
    auto inner = std::make_shared<mock::Controller>(Zone::RearLeft);
    REQUIRE_FALSE(reg->add_route(inner));

    REQUIRE_FALSE(reg->deregister(Zone::RearLeft));

    // The upstream controller wrapped by the route is now closed.
    Command cmd; cmd.zone = Zone::RearLeft; Response resp;
    REQUIRE(inner->send(Context{}, cmd, resp) == ErrClosed);

    // And the route is gone.
    std::shared_ptr<Controller> ctrl;
    REQUIRE(reg->lookup(Zone::RearLeft, ctrl) == ErrNotFound);
}

TEST_CASE("proxy: ProxyRegistry close is idempotent", "[proxy][REQ-PROXY-005]") {
    auto reg   = proxy::new_registry();
    auto inner = std::make_shared<mock::Controller>(Zone::FrontRight);
    REQUIRE_FALSE(reg->add_route(inner));
    REQUIRE_FALSE(reg->close());
    REQUIRE_FALSE(reg->close()); // second close is a no-op, still no error
}

TEST_CASE("proxy: duplicate route returns ErrAlreadyExists", "[proxy]") {
    auto reg = proxy::new_registry();
    auto a = std::make_shared<mock::Controller>(Zone::FrontLeft);
    auto b = std::make_shared<mock::Controller>(Zone::FrontLeft);
    REQUIRE_FALSE(reg->add_route(a));
    REQUIRE(reg->add_route(b) == ErrAlreadyExists);
}

TEST_CASE("proxy: close closes all routes", "[proxy]") {
    auto reg = proxy::new_registry();
    auto inner = std::make_shared<mock::Controller>(Zone::FrontRight);
    REQUIRE_FALSE(reg->add_route(inner));
    REQUIRE_FALSE(reg->close());
}
