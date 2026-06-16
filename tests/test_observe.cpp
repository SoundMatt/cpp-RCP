// fusa:req REQ-OBS-001
// fusa:req REQ-OBS-002
// fusa:req REQ-OBS-003
#include <catch2/catch_test_macros.hpp>

#include "rcp/mock.hpp"
#include "rcp/observe.hpp"

using namespace rcp;

TEST_CASE("observe: span recorded on successful send", "[observe]") {
    auto sink  = std::make_shared<observe::InMemorySink>();
    auto inner = std::make_shared<mock::Controller>(Zone::FrontLeft);
    auto ctrl  = observe::new_controller(inner, sink);

    Command cmd;
    cmd.zone = Zone::FrontLeft;
    cmd.type = CommandType::Get;
    Response resp;
    REQUIRE_FALSE(ctrl->send(Context{}, cmd, resp));

    REQUIRE(sink->span_count() == 1);
    auto spans = sink->spans();
    REQUIRE(spans[0].zone == Zone::FrontLeft);
    REQUIRE(spans[0].cmd_type == CommandType::Get);
    REQUIRE(!spans[0].result);
}

TEST_CASE("observe: multiple sends accumulate spans", "[observe]") {
    auto sink  = std::make_shared<observe::InMemorySink>();
    auto inner = std::make_shared<mock::Controller>(Zone::FrontLeft);
    auto ctrl  = observe::new_controller(inner, sink);

    for (int i = 0; i < 5; ++i) {
        Command cmd;
        cmd.zone = Zone::FrontLeft;
        cmd.type = CommandType::Set;
        Response resp;
        auto ec = ctrl->send(Context{}, cmd, resp);
        (void)ec;
    }

    REQUIRE(sink->span_count() == 5);
}

TEST_CASE("observe: span duration is non-negative", "[observe]") {
    auto sink  = std::make_shared<observe::InMemorySink>();
    auto inner = std::make_shared<mock::Controller>(Zone::Central);
    auto ctrl  = observe::new_controller(inner, sink);

    Command cmd;
    cmd.zone = Zone::Central;
    cmd.type = CommandType::Watchdog;
    Response resp;
    auto ec = ctrl->send(Context{}, cmd, resp);
    (void)ec;

    REQUIRE(sink->span_count() == 1);
    REQUIRE(sink->spans()[0].duration().count() >= 0);
}

TEST_CASE("observe: noop sink does not crash", "[observe]") {
    auto inner = std::make_shared<mock::Controller>(Zone::FrontLeft);
    auto ctrl  = observe::new_controller(inner); // default noop sink

    Command cmd;
    cmd.zone = Zone::FrontLeft;
    cmd.type = CommandType::Get;
    Response resp;
    REQUIRE_FALSE(ctrl->send(Context{}, cmd, resp));
}
