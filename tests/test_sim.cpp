// fusa:test REQ-SIM-001
// fusa:test REQ-SIM-002
// fusa:test REQ-SIM-003
// fusa:test REQ-SIM-004
// fusa:test REQ-SIM-005
// fusa:test REQ-SIM-006
// fusa:test REQ-SIM-007
// fusa:test REQ-SIM-008

#include <catch2/catch_test_macros.hpp>
#include <rcp/sim.hpp>

using namespace rcp;

// ── Basic send ────────────────────────────────────────────────────────────────

TEST_CASE("sim Controller::send returns OK by default", "[sim][REQ-SIM-001]") {
    sim::Config cfg;
    cfg.zone         = Zone::FrontLeft;
    cfg.latency_model = sim::LatencyModel::Constant;
    cfg.base_latency = std::chrono::milliseconds(0);

    sim::Controller ctrl(cfg);
    Command cmd;
    cmd.zone = Zone::FrontLeft;
    Response resp;
    REQUIRE_FALSE(ctrl.send(Context::background(), cmd, resp));
    REQUIRE(resp.status == ResponseStatus::OK);
}

TEST_CASE("sim Controller::zone returns configured zone", "[sim][REQ-SIM-002]") {
    sim::Config cfg;
    cfg.zone = Zone::RearRight;
    sim::Controller ctrl(cfg);
    REQUIRE(ctrl.zone() == Zone::RearRight);
}

// ── Latency ───────────────────────────────────────────────────────────────────

TEST_CASE("sim Controller applies constant latency", "[sim][REQ-SIM-003]") {
    sim::Config cfg;
    cfg.zone          = Zone::FrontLeft;
    cfg.latency_model = sim::LatencyModel::Constant;
    cfg.base_latency  = std::chrono::milliseconds(10);

    sim::Controller ctrl(cfg);
    Command cmd;
    cmd.zone = Zone::FrontLeft;
    Response resp;

    auto start = std::chrono::steady_clock::now();
    { auto ec = ctrl.send(Context::background(), cmd, resp); (void)ec; }
    auto elapsed = std::chrono::steady_clock::now() - start;

    REQUIRE(elapsed >= std::chrono::milliseconds(10));
}

// ── Fault injection ───────────────────────────────────────────────────────────

TEST_CASE("sim Controller::fault causes send to fail", "[sim][REQ-SIM-004]") {
    sim::Config cfg;
    cfg.zone = Zone::FrontLeft;
    sim::Controller ctrl(cfg);

    ctrl.fault(ErrClosed);

    Command cmd;
    cmd.zone = Zone::FrontLeft;
    Response resp;
    REQUIRE(ctrl.send(Context::background(), cmd, resp));
}

TEST_CASE("sim Controller::recover restores send to OK", "[sim][REQ-SIM-005]") {
    sim::Config cfg;
    cfg.zone = Zone::FrontLeft;
    sim::Controller ctrl(cfg);

    ctrl.fault(ErrClosed);
    ctrl.recover();

    Command cmd;
    cmd.zone = Zone::FrontLeft;
    Response resp;
    REQUIRE_FALSE(ctrl.send(Context::background(), cmd, resp));
}

// ── Subscribe ─────────────────────────────────────────────────────────────────

TEST_CASE("sim Controller::subscribe returns a channel", "[sim][REQ-SIM-006]") {
    sim::Config cfg;
    cfg.zone            = Zone::FrontLeft;
    cfg.status_interval = std::chrono::milliseconds(50);
    sim::Controller ctrl(cfg);

    std::shared_ptr<StatusChannel> ch;
    REQUIRE_FALSE(ctrl.subscribe(Context::background(), ch));
    REQUIRE(ch != nullptr);
    ch->close();
}

TEST_CASE("sim Controller publishes status on interval", "[sim][REQ-SIM-007]") {
    sim::Config cfg;
    cfg.zone            = Zone::FrontLeft;
    cfg.status_interval = std::chrono::milliseconds(20);
    sim::Controller ctrl(cfg);

    std::shared_ptr<StatusChannel> ch;
    { auto ec = ctrl.subscribe(Context::background(), ch); (void)ec; }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    auto st = ch->try_recv();
    REQUIRE(st.has_value());
    ch->close();
}

// ── Close ─────────────────────────────────────────────────────────────────────

TEST_CASE("sim Controller::close stops background threads", "[sim][REQ-SIM-008]") {
    sim::Config cfg;
    cfg.zone            = Zone::FrontLeft;
    cfg.status_interval = std::chrono::milliseconds(50);
    sim::Controller ctrl(cfg);
    REQUIRE_FALSE(ctrl.close());
}
