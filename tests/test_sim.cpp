// fusa:test REQ-SIM-001
// fusa:test REQ-SIM-002
// fusa:test REQ-SIM-003
// fusa:test REQ-SIM-004
// fusa:test REQ-SIM-005
// fusa:test REQ-SIM-006
// fusa:test REQ-SIM-007

// Tests for rcp/sim.hpp — the timing-realistic RC Server simulator
// (ROADMAP.md milestone 56, "Test & Simulation Harness Rebuild", v2.12.0).

#include <catch2/catch_test_macros.hpp>
#include <rcp/sim.hpp>

using namespace rcp;

namespace {

acf::AcfMessageInfo gpio_read_request() {
    return acf::make_standard_request(mock::kGpioByteBusId, /*transaction_num=*/1,
                                       /*write=*/false, /*read_size=*/0);
}

} // namespace

// ── dispatch forwards to mock::Server ─────────────────────────────────────────

TEST_CASE("dispatch forwards to the wrapped mock::Server by default", "[sim][REQ-SIM-001]") {
    sim::Simulator sim;
    REQUIRE_FALSE(sim.server().advance_to_rcp_configured());

    auto req = gpio_read_request();
    acf::AcfMessageInfo resp;
    std::vector<uint8_t> resp_payload;
    REQUIRE_FALSE(sim.dispatch(/*stream_key=*/1, /*client=*/0, req, {}, resp, resp_payload,
                                /*now_ms=*/0));
    REQUIRE(acf::response_kind_of(resp) == acf::ResponseKind::ReadResponse);
}

// ── Fault injection ───────────────────────────────────────────────────────────

TEST_CASE("fault() causes dispatch to fail without touching the server",
          "[sim][REQ-SIM-002]") {
    sim::Simulator sim;
    REQUIRE_FALSE(sim.server().advance_to_rcp_configured());
    sim.fault(std::make_error_code(std::errc::connection_reset));
    REQUIRE(sim.faulted());

    auto req = gpio_read_request();
    acf::AcfMessageInfo resp;
    std::vector<uint8_t> resp_payload;
    auto ec = sim.dispatch(1, 0, req, {}, resp, resp_payload, 0);
    REQUIRE(ec == std::make_error_code(std::errc::connection_reset));
    REQUIRE(acf::response_kind_of(resp) == acf::ResponseKind::ErrorResponse);
}

TEST_CASE("recover() restores normal dispatch", "[sim][REQ-SIM-003]") {
    sim::Simulator sim;
    REQUIRE_FALSE(sim.server().advance_to_rcp_configured());
    sim.fault(std::make_error_code(std::errc::connection_reset));
    sim.recover();
    REQUIRE_FALSE(sim.faulted());

    auto req = gpio_read_request();
    acf::AcfMessageInfo resp;
    std::vector<uint8_t> resp_payload;
    REQUIRE_FALSE(sim.dispatch(1, 0, req, {}, resp, resp_payload, 0));
}

// ── Latency modeling ──────────────────────────────────────────────────────────

TEST_CASE("Constant latency model always reports base_latency", "[sim][REQ-SIM-004]") {
    sim::Config cfg;
    cfg.latency_model = sim::LatencyModel::Constant;
    cfg.base_latency   = std::chrono::milliseconds(7);
    sim::Simulator sim(cfg);

    for (int i = 0; i < 5; ++i) {
        REQUIRE(sim.simulated_latency_ms() == std::chrono::milliseconds(7));
    }
}

TEST_CASE("Jitter latency model reports a delay within [base_latency, base_latency+jitter]",
          "[sim][REQ-SIM-005]") {
    sim::Config cfg;
    cfg.latency_model = sim::LatencyModel::Jitter;
    cfg.base_latency   = std::chrono::milliseconds(5);
    cfg.jitter          = std::chrono::milliseconds(3);
    sim::Simulator sim(cfg);

    for (int i = 0; i < 50; ++i) {
        auto d = sim.simulated_latency_ms();
        REQUIRE(d >= std::chrono::milliseconds(5));
        REQUIRE(d <= std::chrono::milliseconds(8));
    }
}

// ── Watchdog wiring ───────────────────────────────────────────────────────────

TEST_CASE("dispatch kicks a registered stream's watchdog on every accepted request",
          "[sim][REQ-SIM-006]") {
    sim::Simulator sim;
    REQUIRE_FALSE(sim.server().advance_to_rcp_configured());
    sim.register_stream(/*stream_key=*/7);

    regmap::RequestStreamConfig cfg;
    cfg.rx_wd_enable           = true;
    cfg.rx_wd_timeout_interval = 100;
    request::RequestLedger ledger;

    std::vector<watchdog::HealthEvent> events;
    sim.watchdog().subscribe([&](const watchdog::HealthEvent& ev) { events.push_back(ev); });

    // Never kicked yet: nothing to report.
    REQUIRE_FALSE(sim.poll_watchdog(7, cfg, ledger, 1'000));
    REQUIRE(events.empty());

    auto req = gpio_read_request();
    acf::AcfMessageInfo resp;
    std::vector<uint8_t> resp_payload;
    REQUIRE_FALSE(sim.dispatch(7, 0, req, {}, resp, resp_payload, /*now_ms=*/1'000));

    // Kicked at 1'000ms: polling before the timeout elapses reports nothing...
    REQUIRE_FALSE(sim.poll_watchdog(7, cfg, ledger, 1'050));
    REQUIRE(events.empty());

    // ...but polling once rx_wd_timeout_interval has elapsed since the kick
    // reports the overflow.
    REQUIRE_FALSE(sim.poll_watchdog(7, cfg, ledger, 1'150));
    REQUIRE(events.size() == 1);
    REQUIRE(events[0].overflowed);
}

TEST_CASE("dispatch on an unregistered stream does not touch the watchdog manager",
          "[sim][REQ-SIM-007]") {
    sim::Simulator sim;
    REQUIRE_FALSE(sim.server().advance_to_rcp_configured());
    REQUIRE_FALSE(sim.watchdog().is_registered(9));

    auto req = gpio_read_request();
    acf::AcfMessageInfo resp;
    std::vector<uint8_t> resp_payload;
    REQUIRE_FALSE(sim.dispatch(9, 0, req, {}, resp, resp_payload, 0));
    REQUIRE_FALSE(sim.watchdog().is_registered(9));
}
