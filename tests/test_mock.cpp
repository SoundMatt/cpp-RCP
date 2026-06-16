// fusa:test REQ-CTRL-001
// fusa:test REQ-CTRL-002
// fusa:test REQ-CTRL-003
// fusa:test REQ-CTRL-004
// fusa:test REQ-CTRL-005
// fusa:test REQ-CTRL-006
// fusa:test REQ-CTRL-007
// fusa:test REQ-CTRL-008
// fusa:test REQ-CTRL-009
// fusa:test REQ-CTRL-010
// fusa:test REQ-CTRL-011
// fusa:test REQ-CTRL-012
// fusa:test REQ-CTRL-013
// fusa:test REQ-CTRL-014
// fusa:test REQ-CTRL-015
// fusa:test REQ-CTRL-016
// fusa:test REQ-CTRL-017
// fusa:test REQ-CTRL-018
// fusa:test REQ-CTRL-019
// fusa:test REQ-CTRL-020
// fusa:test REQ-CTRL-021
// fusa:test REQ-CTRL-022
// fusa:test REQ-CTRL-023
// fusa:test REQ-CTRL-024
// fusa:test REQ-CTRL-025
// fusa:test REQ-CTRL-026
// fusa:test REQ-CTRL-027
// fusa:test REQ-REG-001
// fusa:test REQ-REG-002
// fusa:test REQ-REG-003
// fusa:test REQ-REG-004
// fusa:test REQ-REG-005
// fusa:test REQ-REG-006
// fusa:test REQ-REG-007
// fusa:test REQ-REG-008
// fusa:test REQ-REG-009
// fusa:test REQ-REG-010
// fusa:test REQ-REG-011
// fusa:test REQ-REG-012
// fusa:test REQ-REG-013
// fusa:test REQ-RESP-001
// fusa:test REQ-RESP-002
// fusa:test REQ-STAT-001
// fusa:test REQ-STAT-002
// fusa:test REQ-STAT-003
// fusa:test REQ-STAT-004
// fusa:test REQ-ERR-011

#include <catch2/catch_test_macros.hpp>
#include <rcp/mock.hpp>

#include <atomic>
#include <thread>
#include <vector>

using namespace rcp;

// ── Registry pre-population ───────────────────────────────────────────────────

TEST_CASE("NewRegistry pre-populates all five standard zones", "[registry][REQ-REG-001]") {
    auto reg = mock::new_registry();
    for (auto z : {Zone::FrontLeft, Zone::FrontRight,
                    Zone::RearLeft,  Zone::RearRight,
                    Zone::Central}) {
        std::shared_ptr<Controller> ctrl;
        REQUIRE_FALSE(reg->lookup(z, ctrl));
        REQUIRE(ctrl->zone() == z);
    }
    reg->close();
}

TEST_CASE("Registry::register_ctrl returns ErrAlreadyExists for duplicate zone", "[registry][REQ-REG-002]") {
    auto reg  = mock::new_registry();
    auto dup  = std::make_shared<mock::Controller>(Zone::FrontLeft);
    REQUIRE(reg->register_ctrl(dup) == ErrAlreadyExists);
    reg->close();
}

TEST_CASE("Registry::deregister removes and closes the controller", "[registry][REQ-REG-003]") {
    auto reg = mock::new_registry();
    REQUIRE_FALSE(reg->deregister(Zone::FrontLeft));
    std::shared_ptr<Controller> ctrl;
    REQUIRE(reg->lookup(Zone::FrontLeft, ctrl) == ErrNotFound);
    reg->close();
}

TEST_CASE("Registry::lookup returns ErrNotFound for absent zone", "[registry][REQ-REG-004]") {
    auto reg = mock::new_registry();
    REQUIRE_FALSE(reg->deregister(Zone::FrontLeft));
    std::shared_ptr<Controller> ctrl;
    REQUIRE(reg->lookup(Zone::FrontLeft, ctrl) == ErrNotFound);
    reg->close();
}

TEST_CASE("Registry::close is idempotent", "[registry][REQ-REG-005]") {
    auto reg = mock::new_registry();
    REQUIRE_FALSE(reg->close());
    REQUIRE_FALSE(reg->close());
}

TEST_CASE("Registry::controllers returns all registered controllers", "[registry][REQ-REG-006]") {
    auto reg   = mock::new_registry();
    auto ctrls = reg->controllers();
    REQUIRE(ctrls.size() == 5);
    reg->close();
}

TEST_CASE("Registry::register_ctrl returns ErrClosed after close", "[registry][REQ-REG-007]") {
    auto reg   = mock::new_registry();
    reg->close();
    auto extra = std::make_shared<mock::Controller>(Zone::FrontLeft);
    REQUIRE(reg->register_ctrl(extra) == ErrClosed);
}

TEST_CASE("Registry::deregister returns ErrNotFound for never-registered zone", "[registry][REQ-REG-008]") {
    auto reg = mock::new_registry();
    REQUIRE_FALSE(reg->deregister(Zone::FrontLeft));
    REQUIRE(reg->deregister(Zone::FrontLeft) == ErrNotFound);
    reg->close();
}

TEST_CASE("Registered controller is immediately retrievable via lookup", "[registry][REQ-REG-009]") {
    auto reg = mock::new_registry();
    REQUIRE_FALSE(reg->deregister(Zone::FrontLeft));
    auto nc  = std::make_shared<mock::Controller>(Zone::FrontLeft);
    REQUIRE_FALSE(reg->register_ctrl(nc));
    std::shared_ptr<Controller> got;
    REQUIRE_FALSE(reg->lookup(Zone::FrontLeft, got));
    REQUIRE(got.get() == nc.get());
    reg->close();
}

TEST_CASE("Registry::close closes all registered controllers", "[registry][REQ-REG-010]") {
    auto reg = mock::new_registry();
    std::shared_ptr<Controller> ctrl;
    REQUIRE_FALSE(reg->lookup(Zone::FrontLeft, ctrl));
    reg->close();
    Response out;
    auto ec = ctrl->send(Context::background(), Command{0, Zone::FrontLeft}, out);
    REQUIRE(ec == ErrClosed);
}

TEST_CASE("Registry::lookup on closed registry returns ErrClosed", "[registry][REQ-REG-011][REQ-REG-013]") {
    auto reg = mock::new_registry();
    reg->close();
    std::shared_ptr<Controller> ctrl;
    REQUIRE(reg->lookup(Zone::FrontLeft, ctrl) == ErrClosed);
}

TEST_CASE("Registry::deregister for already-deregistered zone returns ErrNotFound", "[registry][REQ-REG-012]") {
    auto reg = mock::new_registry();
    REQUIRE_FALSE(reg->deregister(Zone::FrontLeft));
    REQUIRE(reg->deregister(Zone::FrontLeft) == ErrNotFound);
    reg->close();
}

// ── Controller::send ──────────────────────────────────────────────────────────

TEST_CASE("Controller::send returns StatusOK with no handler", "[controller][REQ-CTRL-001]") {
    mock::Controller ctrl(Zone::FrontLeft);
    Response         out;
    REQUIRE_FALSE(ctrl.send(Context::background(), Command{1, Zone::FrontLeft}, out));
    REQUIRE(out.status == ResponseStatus::OK);
}

TEST_CASE("Controller::send dispatches to handler and returns response verbatim", "[controller][REQ-CTRL-002][REQ-CTRL-016]") {
    bool called = false;
    mock::Controller ctrl(Zone::FrontLeft, [&](const Command& cmd) -> Response {
        called = true;
        return Response{cmd.id, cmd.zone, ResponseStatus::Error, {}};
    });
    Response out;
    REQUIRE_FALSE(ctrl.send(Context::background(), Command{42, Zone::FrontLeft}, out));
    REQUIRE(called);
    REQUIRE(out.status == ResponseStatus::Error);
    REQUIRE(out.command_id == 42);
}

TEST_CASE("Controller::send returns ErrClosed after close", "[controller][REQ-CTRL-003]") {
    mock::Controller ctrl(Zone::FrontLeft);
    ctrl.close();
    Response out;
    REQUIRE(ctrl.send(Context::background(), Command{1, Zone::FrontLeft}, out) == ErrClosed);
}

TEST_CASE("Controller::send returns ErrTimeout when ctx is already done", "[controller][REQ-CTRL-004][REQ-CTRL-023]") {
    bool handler_called = false;
    mock::Controller ctrl(Zone::FrontLeft, [&](const Command&) -> Response {
        handler_called = true;
        return {};
    });
    auto past = Context::with_deadline(
        std::chrono::steady_clock::now() - std::chrono::seconds(1));
    Response out;
    REQUIRE(ctrl.send(past, Command{1, Zone::FrontLeft}, out) == ErrTimeout);
    REQUIRE_FALSE(handler_called);
}

TEST_CASE("Controller::close is idempotent", "[controller][REQ-CTRL-005]") {
    mock::Controller ctrl(Zone::FrontLeft);
    REQUIRE_FALSE(ctrl.close());
    REQUIRE_FALSE(ctrl.close());
}

TEST_CASE("Controller::subscribe channel closes when controller closes", "[controller][REQ-CTRL-007]") {
    auto ctrl = std::make_unique<mock::Controller>(Zone::FrontLeft);
    std::shared_ptr<StatusChannel> ch;
    REQUIRE_FALSE(ctrl->subscribe(Context::background(), ch));
    ctrl->close();
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    REQUIRE(ch->is_closed());
}

TEST_CASE("Controller::subscribe returns ErrClosed after close", "[controller][REQ-CTRL-008]") {
    mock::Controller ctrl(Zone::FrontLeft);
    ctrl.close();
    std::shared_ptr<StatusChannel> ch;
    REQUIRE(ctrl.subscribe(Context::background(), ch) == ErrClosed);
}

TEST_CASE("Controller::zone returns declared zone", "[controller][REQ-CTRL-009]") {
    mock::Controller ctrl(Zone::RearRight);
    REQUIRE(ctrl.zone() == Zone::RearRight);
}

TEST_CASE("Status Seq values are strictly increasing", "[controller][REQ-CTRL-010][REQ-STAT-002]") {
    mock::Controller ctrl(Zone::Central);
    std::shared_ptr<StatusChannel> ch;
    REQUIRE_FALSE(ctrl.subscribe(Context::background(), ch));
    ctrl.publish({});
    ctrl.publish({});
    ctrl.publish({});
    uint32_t last = 0;
    for (int i = 0; i < 3; ++i) {
        auto s = ch->recv();
        REQUIRE(s.has_value());
        REQUIRE(s->seq > last);
        last = s->seq;
    }
    ctrl.close();
}

TEST_CASE("Subscribe channel is closed when its context expires", "[controller][REQ-CTRL-011]") {
    mock::Controller ctrl(Zone::FrontLeft);
    auto             ctx = Context::with_timeout(std::chrono::milliseconds(20));
    std::shared_ptr<StatusChannel> ch;
    REQUIRE_FALSE(ctrl.subscribe(ctx, ch));
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    REQUIRE(ch->is_closed());
    ctrl.close();
}

TEST_CASE("Multiple concurrent subscribers each receive published status", "[controller][REQ-CTRL-006][REQ-CTRL-012]") {
    mock::Controller ctrl(Zone::FrontLeft);
    constexpr int    N = 3;
    std::vector<std::shared_ptr<StatusChannel>> channels(N);
    for (auto& ch : channels) REQUIRE_FALSE(ctrl.subscribe(Context::background(), ch));
    ctrl.publish({0x01});
    for (auto& ch : channels) {
        auto s = ch->recv();
        REQUIRE(s.has_value());
        REQUIRE(s->zone == Zone::FrontLeft);
        REQUIRE(s->payload == std::vector<uint8_t>{0x01});
    }
    ctrl.close();
}

TEST_CASE("Controller::send with CmdNoop returns StatusOK", "[controller][REQ-CTRL-013]") {
    mock::Controller ctrl(Zone::FrontLeft);
    Response         out;
    REQUIRE_FALSE(ctrl.send(Context::background(), Command{1, Zone::FrontLeft, CommandType::Noop}, out));
    REQUIRE(out.status == ResponseStatus::OK);
}

TEST_CASE("Controller::send with CmdWatchdog returns StatusOK", "[controller][REQ-CTRL-014]") {
    mock::Controller ctrl(Zone::FrontLeft);
    Response         out;
    REQUIRE_FALSE(ctrl.send(Context::background(), Command{1, Zone::FrontLeft, CommandType::Watchdog}, out));
    REQUIRE(out.status == ResponseStatus::OK);
}

TEST_CASE("Controller::send with CmdReset returns StatusOK", "[controller][REQ-CTRL-015]") {
    mock::Controller ctrl(Zone::FrontLeft);
    Response         out;
    REQUIRE_FALSE(ctrl.send(Context::background(), Command{1, Zone::FrontLeft, CommandType::Reset}, out));
    REQUIRE(out.status == ResponseStatus::OK);
}

TEST_CASE("Publish on a closed controller does not throw or crash", "[controller][REQ-CTRL-017]") {
    mock::Controller ctrl(Zone::FrontLeft);
    ctrl.close();
    REQUIRE_NOTHROW(ctrl.publish({}));
}

TEST_CASE("Concurrent send calls are data-race free", "[controller][REQ-CTRL-018]") {
    mock::Controller ctrl(Zone::FrontLeft);
    constexpr int THREADS = 8, ITERS = 100;
    std::vector<std::thread> workers;
    workers.reserve(THREADS);
    std::atomic<int> ok{0};
    for (int t = 0; t < THREADS; ++t) {
        workers.emplace_back([&, t]() {
            for (int i = 0; i < ITERS; ++i) {
                Response out;
                Command  cmd{static_cast<uint32_t>(t * ITERS + i),
                              Zone::FrontLeft, CommandType::Get};
                if (!ctrl.send(Context::background(), cmd, out)) ++ok;
            }
        });
    }
    for (auto& w : workers) w.join();
    REQUIRE(ok == THREADS * ITERS);
    ctrl.close();
}

TEST_CASE("Concurrent publish and subscribe are data-race free", "[controller][REQ-CTRL-019]") {
    mock::Controller ctrl(Zone::FrontLeft);
    std::shared_ptr<StatusChannel> ch;
    REQUIRE_FALSE(ctrl.subscribe(Context::background(), ch));
    std::atomic<bool> stop{false};
    std::thread publisher([&]() {
        while (!stop) ctrl.publish({0xAB});
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    stop = true;
    publisher.join();
    ctrl.close();
}

TEST_CASE("Status.Zone is correct for every published update", "[controller][REQ-CTRL-020][REQ-STAT-001]") {
    mock::Controller ctrl(Zone::RearLeft);
    std::shared_ptr<StatusChannel> ch;
    REQUIRE_FALSE(ctrl.subscribe(Context::background(), ch));
    ctrl.publish({});
    auto s = ch->recv();
    REQUIRE(s.has_value());
    REQUIRE(s->zone == Zone::RearLeft);
    ctrl.close();
}

TEST_CASE("Status.Payload matches published payload", "[controller][REQ-CTRL-021][REQ-STAT-004]") {
    mock::Controller ctrl(Zone::FrontLeft);
    std::shared_ptr<StatusChannel> ch;
    REQUIRE_FALSE(ctrl.subscribe(Context::background(), ch));
    std::vector<uint8_t> payload{0xDE, 0xAD, 0xBE, 0xEF};
    ctrl.publish(payload);
    auto s = ch->recv();
    REQUIRE(s.has_value());
    REQUIRE(s->payload == payload);
    ctrl.close();
}

TEST_CASE("Status.Healthy is true while controller is open", "[controller][REQ-CTRL-022][REQ-STAT-003]") {
    mock::Controller ctrl(Zone::FrontLeft);
    std::shared_ptr<StatusChannel> ch;
    REQUIRE_FALSE(ctrl.subscribe(Context::background(), ch));
    ctrl.publish({});
    auto s = ch->recv();
    REQUIRE(s.has_value());
    REQUIRE(s->healthy);
    ctrl.close();
}

TEST_CASE("Controller::send with empty payload does not crash", "[controller][REQ-CTRL-024]") {
    mock::Controller ctrl(Zone::FrontLeft);
    Response         out;
    REQUIRE_NOTHROW(ctrl.send(Context::background(), Command{1, Zone::FrontLeft}, out));
}

TEST_CASE("Controller::send returns ErrZoneMismatch for wrong zone", "[controller][REQ-CTRL-025]") {
    mock::Controller ctrl(Zone::FrontLeft);
    Response         out;
    REQUIRE(ctrl.send(Context::background(), Command{1, Zone::FrontRight}, out) == ErrZoneMismatch);
}

TEST_CASE("Payload is copied before passing to handler", "[controller][REQ-CTRL-026]") {
    std::vector<uint8_t> seen_payload;
    mock::Controller ctrl(Zone::FrontLeft, [&](const Command& c) -> Response {
        seen_payload = c.payload;
        return {c.id, c.zone, ResponseStatus::OK, {}};
    });
    std::vector<uint8_t> payload{1, 2, 3};
    Command cmd{1, Zone::FrontLeft};
    cmd.payload = payload;
    Response out;
    ctrl.send(Context::background(), cmd, out);
    payload[0] = 0xFF;
    REQUIRE(seen_payload == std::vector<uint8_t>{1, 2, 3});
}

TEST_CASE("Publish copies payload before delivering to subscribers", "[controller][REQ-CTRL-027]") {
    mock::Controller ctrl(Zone::FrontLeft);
    std::shared_ptr<StatusChannel> ch;
    REQUIRE_FALSE(ctrl.subscribe(Context::background(), ch));
    std::vector<uint8_t> payload{0xAA, 0xBB};
    ctrl.publish(payload);
    payload[0] = 0x00;
    auto s = ch->recv();
    REQUIRE(s.has_value());
    REQUIRE(s->payload == std::vector<uint8_t>{0xAA, 0xBB});
    ctrl.close();
}

// ── Response fields ───────────────────────────────────────────────────────────

TEST_CASE("Response.command_id echoes the originating Command.id", "[response][REQ-RESP-001]") {
    mock::Controller ctrl(Zone::FrontLeft);
    Response         out;
    REQUIRE_FALSE(ctrl.send(Context::background(), Command{0xDEADBEEF, Zone::FrontLeft}, out));
    REQUIRE(out.command_id == 0xDEADBEEF);
}

TEST_CASE("Response.zone identifies the zone that processed the command", "[response][REQ-RESP-002]") {
    mock::Controller ctrl(Zone::RearRight);
    Response         out;
    REQUIRE_FALSE(ctrl.send(Context::background(), Command{1, Zone::RearRight}, out));
    REQUIRE(out.zone == Zone::RearRight);
}
