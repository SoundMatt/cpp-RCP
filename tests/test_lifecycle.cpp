// fusa:test REQ-LIFECYCLE-001
// fusa:test REQ-LIFECYCLE-002
// fusa:test REQ-LIFECYCLE-003
// fusa:test REQ-LIFECYCLE-004
// fusa:test REQ-LIFECYCLE-005
// fusa:test REQ-LIFECYCLE-006
// fusa:test REQ-LIFECYCLE-007

// Tests for rcp/lifecycle.hpp — the RC Server 3-state lifecycle machine
// (ROADMAP.md milestone 45, "RC Server Lifecycle & Register-Map Model",
// v2.1.0), plus the lifecycle-state-changed trigger signal added at
// ROADMAP.md milestone 54 ("Watchdog & Liveness Rebuild", v2.10.0).

#include <catch2/catch_test_macros.hpp>
#include <rcp/lifecycle.hpp>

#include <utility>
#include <vector>

using namespace rcp::lifecycle;

// ── State encoding ─────────────────────────────────────────────────────────────

TEST_CASE("ServerState values match the documented byte encoding", "[lifecycle][REQ-LIFECYCLE-001]") {
    REQUIRE(static_cast<uint8_t>(ServerState::HwUnconfigured) == 0x00);
    REQUIRE(static_cast<uint8_t>(ServerState::HwConfigured)   == 0x55);
    REQUIRE(static_cast<uint8_t>(ServerState::RcpConfigured)  == 0xAA);
}

TEST_CASE("A fresh ServerLifecycle starts HW_UNCONFIGURED", "[lifecycle][REQ-LIFECYCLE-001]") {
    ServerLifecycle lc;
    REQUIRE(lc.state() == ServerState::HwUnconfigured);
}

// ── Forward transition guard ────────────────────────────────────────────────────

TEST_CASE("advance() walks HW_UNCONFIGURED -> HW_CONFIGURED -> RCP_CONFIGURED", "[lifecycle][REQ-LIFECYCLE-002]") {
    ServerLifecycle lc;
    REQUIRE_FALSE(lc.advance(ServerState::HwConfigured));
    REQUIRE(lc.state() == ServerState::HwConfigured);
    REQUIRE_FALSE(lc.advance(ServerState::RcpConfigured));
    REQUIRE(lc.state() == ServerState::RcpConfigured);
}

TEST_CASE("advance() rejects skipping HW_CONFIGURED entirely", "[lifecycle][REQ-LIFECYCLE-002]") {
    ServerLifecycle lc;
    auto ec = lc.advance(ServerState::RcpConfigured);
    REQUIRE(ec);
    REQUIRE(ec == make_error_code(LifecycleErrc::invalid_transition));
    REQUIRE(lc.state() == ServerState::HwUnconfigured);
}

TEST_CASE("advance() rejects moving backward", "[lifecycle][REQ-LIFECYCLE-002]") {
    ServerLifecycle lc;
    REQUIRE_FALSE(lc.advance(ServerState::HwConfigured));
    auto ec = lc.advance(ServerState::HwUnconfigured);
    REQUIRE(ec);
    REQUIRE(ec == make_error_code(LifecycleErrc::invalid_transition));
}

TEST_CASE("advance() rejects re-requesting the current state", "[lifecycle][REQ-LIFECYCLE-002]") {
    ServerLifecycle lc;
    auto ec = lc.advance(ServerState::HwUnconfigured);
    REQUIRE(ec);
    REQUIRE(ec == make_error_code(LifecycleErrc::invalid_transition));
}

// ── HW_CFG_INCONSISTENT plausibility check ──────────────────────────────────────

TEST_CASE("A failing HW config plausibility check yields HW_CFG_INCONSISTENT and blocks the transition",
          "[lifecycle][REQ-LIFECYCLE-003]") {
    ServerLifecycle lc(/*hw_cfg_check=*/[] { return false; });
    auto ec = lc.advance(ServerState::HwConfigured);
    REQUIRE(ec);
    REQUIRE(ec == make_error_code(LifecycleErrc::hw_cfg_inconsistent));
    REQUIRE(lc.state() == ServerState::HwUnconfigured);
}

TEST_CASE("A passing HW config plausibility check allows the transition", "[lifecycle][REQ-LIFECYCLE-003]") {
    ServerLifecycle lc(/*hw_cfg_check=*/[] { return true; });
    REQUIRE_FALSE(lc.advance(ServerState::HwConfigured));
    REQUIRE(lc.state() == ServerState::HwConfigured);
}

// ── RCP_CFG_INCONSISTENT plausibility check ─────────────────────────────────────

TEST_CASE("A failing RCP config plausibility check yields RCP_CFG_INCONSISTENT and blocks the transition",
          "[lifecycle][REQ-LIFECYCLE-004]") {
    ServerLifecycle lc(/*hw_cfg_check=*/{}, /*rcp_cfg_check=*/[] { return false; });
    REQUIRE_FALSE(lc.advance(ServerState::HwConfigured));
    auto ec = lc.advance(ServerState::RcpConfigured);
    REQUIRE(ec);
    REQUIRE(ec == make_error_code(LifecycleErrc::rcp_cfg_inconsistent));
    REQUIRE(lc.state() == ServerState::HwConfigured);
}

TEST_CASE("A passing RCP config plausibility check allows the transition", "[lifecycle][REQ-LIFECYCLE-004]") {
    ServerLifecycle lc(/*hw_cfg_check=*/{}, /*rcp_cfg_check=*/[] { return true; });
    REQUIRE_FALSE(lc.advance(ServerState::HwConfigured));
    REQUIRE_FALSE(lc.advance(ServerState::RcpConfigured));
    REQUIRE(lc.state() == ServerState::RcpConfigured);
}

// ── Register-locking behavior ───────────────────────────────────────────────────

TEST_CASE("Generic config locks from HW_CONFIGURED onward; functional config locks only at RCP_CONFIGURED",
          "[lifecycle][REQ-LIFECYCLE-005]") {
    ServerLifecycle lc;
    REQUIRE_FALSE(lc.generic_config_locked());
    REQUIRE_FALSE(lc.functional_config_locked());

    REQUIRE_FALSE(lc.advance(ServerState::HwConfigured));
    REQUIRE(lc.generic_config_locked());
    REQUIRE_FALSE(lc.functional_config_locked());

    REQUIRE_FALSE(lc.advance(ServerState::RcpConfigured));
    REQUIRE(lc.generic_config_locked());
    REQUIRE(lc.functional_config_locked());
}

// ── deconfigure() explicit backward path ────────────────────────────────────────

TEST_CASE("deconfigure() resets to HW_UNCONFIGURED and unlocks both config blocks",
          "[lifecycle][REQ-LIFECYCLE-006]") {
    ServerLifecycle lc;
    REQUIRE_FALSE(lc.advance(ServerState::HwConfigured));
    REQUIRE_FALSE(lc.advance(ServerState::RcpConfigured));

    lc.deconfigure();
    REQUIRE(lc.state() == ServerState::HwUnconfigured);
    REQUIRE_FALSE(lc.generic_config_locked());
    REQUIRE_FALSE(lc.functional_config_locked());

    // And the state machine can walk forward again from scratch.
    REQUIRE_FALSE(lc.advance(ServerState::HwConfigured));
    REQUIRE(lc.state() == ServerState::HwConfigured);
}

// ── Lifecycle-state-changed trigger signal (milestone 54, v2.10.0) ──────────────

TEST_CASE("subscribe_state_changed fires (previous, current) for each successful advance()",
          "[lifecycle][REQ-LIFECYCLE-007]") {
    ServerLifecycle lc;
    std::vector<std::pair<ServerState, ServerState>> seen;
    lc.subscribe_state_changed([&](ServerState previous, ServerState current) {
        seen.emplace_back(previous, current);
    });

    REQUIRE_FALSE(lc.advance(ServerState::HwConfigured));
    REQUIRE_FALSE(lc.advance(ServerState::RcpConfigured));

    REQUIRE(seen.size() == 2);
    REQUIRE(seen[0] == std::make_pair(ServerState::HwUnconfigured, ServerState::HwConfigured));
    REQUIRE(seen[1] == std::make_pair(ServerState::HwConfigured, ServerState::RcpConfigured));
}

TEST_CASE("subscribe_state_changed does not fire for a failed advance() call",
          "[lifecycle][REQ-LIFECYCLE-007]") {
    ServerLifecycle lc(/*hw_cfg_check=*/[] { return false; });
    int fired = 0;
    lc.subscribe_state_changed([&](ServerState, ServerState) { ++fired; });

    auto ec = lc.advance(ServerState::HwConfigured);
    REQUIRE(ec == make_error_code(LifecycleErrc::hw_cfg_inconsistent));
    REQUIRE(fired == 0);

    // An invalid_transition request (skipping a state) is likewise silent.
    ServerLifecycle lc2;
    int fired2 = 0;
    lc2.subscribe_state_changed([&](ServerState, ServerState) { ++fired2; });
    REQUIRE(lc2.advance(ServerState::RcpConfigured) == make_error_code(LifecycleErrc::invalid_transition));
    REQUIRE(fired2 == 0);
}

TEST_CASE("subscribe_state_changed fires on deconfigure() only when state actually changes",
          "[lifecycle][REQ-LIFECYCLE-007]") {
    ServerLifecycle lc;
    int fired = 0;
    lc.subscribe_state_changed([&](ServerState, ServerState) { ++fired; });

    // Already HwUnconfigured -- deconfigure() is a no-op state-wise, so it
    // must not fire.
    lc.deconfigure();
    REQUIRE(fired == 0);

    REQUIRE_FALSE(lc.advance(ServerState::HwConfigured));
    REQUIRE(fired == 1);

    lc.deconfigure();
    REQUIRE(fired == 2);
    REQUIRE(lc.state() == ServerState::HwUnconfigured);
}

TEST_CASE("Multiple subscribers all fire, in registration order", "[lifecycle][REQ-LIFECYCLE-007]") {
    ServerLifecycle lc;
    std::vector<int> order;
    lc.subscribe_state_changed([&](ServerState, ServerState) { order.push_back(1); });
    lc.subscribe_state_changed([&](ServerState, ServerState) { order.push_back(2); });

    REQUIRE_FALSE(lc.advance(ServerState::HwConfigured));
    REQUIRE(order == std::vector<int>{1, 2});
}
