// fusa:req REQ-PROXY-001
// fusa:req REQ-PROXY-002
// fusa:req REQ-PROXY-003
#include <catch2/catch_test_macros.hpp>

#include "rcp/mock.hpp"
#include "rcp/proxy.hpp"

using namespace rcp;

TEST_CASE("proxy: ProxyController forwards command", "[proxy]") {
    auto inner = std::make_shared<mock::Controller>(Zone::FrontLeft);
    proxy::ProxyController pc(inner);

    Command cmd;
    cmd.zone = Zone::FrontLeft;
    cmd.type = CommandType::Get;
    Response resp;
    REQUIRE_FALSE(pc.send(Context{}, cmd, resp));
    REQUIRE(resp.status == ResponseStatus::OK);
}

TEST_CASE("proxy: ProxyRegistry lookup and send", "[proxy]") {
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

TEST_CASE("proxy: lookup unknown zone returns ErrNotFound", "[proxy]") {
    auto reg = proxy::new_registry();
    std::shared_ptr<Controller> ctrl;
    auto ec = reg->lookup(Zone::Central, ctrl);
    REQUIRE(ec == ErrNotFound);
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
