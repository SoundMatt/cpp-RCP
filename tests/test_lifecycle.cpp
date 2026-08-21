// fusa:test REQ-LIFECYCLE-001
// fusa:test REQ-LIFECYCLE-002
// fusa:test REQ-LIFECYCLE-003
// fusa:test REQ-LIFECYCLE-004
// fusa:test REQ-LIFECYCLE-005
// fusa:test REQ-LIFECYCLE-006
// fusa:test REQ-LIFECYCLE-007
// fusa:test REQ-LIFECYCLE-014
// fusa:test REQ-LIFECYCLE-022
// fusa:test REQ-LIFECYCLE-023
// fusa:test REQ-LIFECYCLE-024
// fusa:test REQ-LIFECYCLE-027
// fusa:test REQ-LIFECYCLE-031
// fusa:test REQ-LIFECYCLE-033
// fusa:test REQ-LIFECYCLE-037
// fusa:test REQ-LIFECYCLE-038
// fusa:test REQ-RMAP-049
// fusa:test REQ-RMAP-055

// Tests for rcp/lifecycle.hpp — the RC Server 3-state lifecycle machine
// (ROADMAP.md milestone 45, "RC Server Lifecycle & Register-Map Model",
// v2.1.0), the lifecycle-state-changed trigger signal added at ROADMAP.md
// milestone 54 ("Watchdog & Liveness Rebuild", v2.10.0), and the access-
// control layer (plausibility snapshots, writer authorization, idle-gating,
// per-state request filtering, register-locking-by-state) content-corrected
// against c-RCP's lifecycle.h/lifecycle.c during the Phase 2 pass (cpp-RCP
// issue #129) — see lifecycle.hpp's own top-of-file note for the full list
// of what was added.

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

// ── PlausibilitySnapshot / check_hw_cfg / check_rcp_cfg ─────────────────────────
// Ported from c-RCP's rcp_lifecycle_check_hw_cfg()/_check_rcp_cfg() — the
// actual plausibility-check CONTENT ServerLifecycle's own pre-existing
// PlausibilityCheck callback hook never modeled (a caller had to supply an
// opaque bool predicate and implement this logic itself, with no shared,
// tested implementation of TC18 §12.3.1.2's own rules anywhere in this
// tree).

TEST_CASE("check_hw_cfg: an empty snapshot is vacuously consistent", "[lifecycle][REQ-LIFECYCLE-003]") {
    PlausibilitySnapshot snap;
    REQUIRE_FALSE(check_hw_cfg(snap));
}

TEST_CASE("check_hw_cfg: an unused endpoint is ignored regardless of its other fields",
          "[lifecycle][REQ-LIFECYCLE-003]") {
    PlausibilitySnapshot snap;
    snap.endpoints.push_back(EndpointPlausibility{}); // ep_used = false, everything else false too
    REQUIRE_FALSE(check_hw_cfg(snap));
}

TEST_CASE("check_hw_cfg: a used endpoint needs both hw_pin_mapped and has_request_stream",
          "[lifecycle][REQ-LIFECYCLE-003]") {
    {
        PlausibilitySnapshot snap;
        EndpointPlausibility ep;
        ep.ep_used = true; // missing hw_pin_mapped
        snap.endpoints.push_back(ep);
        REQUIRE(check_hw_cfg(snap) == make_error_code(LifecycleErrc::hw_cfg_inconsistent));
    }
    {
        PlausibilitySnapshot snap;
        EndpointPlausibility ep;
        ep.ep_used       = true;
        ep.hw_pin_mapped = true; // missing has_request_stream
        snap.endpoints.push_back(ep);
        REQUIRE(check_hw_cfg(snap) == make_error_code(LifecycleErrc::hw_cfg_inconsistent));
    }
    {
        PlausibilitySnapshot snap;
        EndpointPlausibility ep;
        ep.ep_used            = true;
        ep.hw_pin_mapped      = true;
        ep.has_request_stream = true;
        snap.endpoints.push_back(ep);
        REQUIRE_FALSE(check_hw_cfg(snap));
    }
}

TEST_CASE("check_rcp_cfg: a used endpoint needs has_stream_assoc", "[lifecycle][REQ-LIFECYCLE-004]") {
    PlausibilitySnapshot snap;
    EndpointPlausibility ep;
    ep.ep_used = true; // missing has_stream_assoc
    snap.endpoints.push_back(ep);
    REQUIRE(check_rcp_cfg(snap) == make_error_code(LifecycleErrc::rcp_cfg_inconsistent));
}

TEST_CASE("check_rcp_cfg: a configured request stream needs has_response_stream", "[lifecycle][REQ-LIFECYCLE-004]") {
    PlausibilitySnapshot snap;
    RequestStreamPlausibility rs;
    rs.configured = true; // missing has_response_stream
    snap.request_streams.push_back(rs);
    REQUIRE(check_rcp_cfg(snap) == make_error_code(LifecycleErrc::rcp_cfg_inconsistent));
}

// REQ-RMAP-049 (c-RCP issue #338): has_response_stream alone only proves
// SOME association was recorded, not that response_stream_index names a
// response stream that actually exists.
TEST_CASE("check_rcp_cfg: response_stream_index must be within response_stream_count",
          "[lifecycle][REQ-LIFECYCLE-004][REQ-RMAP-049]") {
    PlausibilitySnapshot snap;
    RequestStreamPlausibility rs;
    rs.configured              = true;
    rs.has_response_stream     = true;
    rs.response_stream_index   = 3;
    snap.request_streams.push_back(rs);
    snap.response_stream_count = 2; // index 3 is out of range

    // Also needs a bound endpoint to isolate this specific failure from the
    // orphaned-stream one below.
    EndpointPlausibility ep;
    ep.ep_used              = true;
    ep.has_stream_assoc     = true;
    ep.request_stream_index = 0;
    snap.endpoints.push_back(ep);

    REQUIRE(check_rcp_cfg(snap) == make_error_code(LifecycleErrc::rcp_cfg_inconsistent));

    snap.response_stream_count = 4; // now in range
    REQUIRE_FALSE(check_rcp_cfg(snap));
}

// REQ-LIFECYCLE-038 (c-RCP issue #201): a configured stream with no
// endpoint referencing it (an orphaned, unused stream slot) is also
// inconsistent — the mirror-image of the ep_used-with-no-stream check.
TEST_CASE("check_rcp_cfg: a configured stream with no bound in-use endpoint is inconsistent",
          "[lifecycle][REQ-LIFECYCLE-004][REQ-LIFECYCLE-038]") {
    PlausibilitySnapshot snap;
    RequestStreamPlausibility rs;
    rs.configured              = true;
    rs.has_response_stream     = true;
    rs.response_stream_index   = 0;
    snap.request_streams.push_back(rs);
    snap.response_stream_count = 1;
    // No endpoint at all references request_stream_index 0.
    REQUIRE(check_rcp_cfg(snap) == make_error_code(LifecycleErrc::rcp_cfg_inconsistent));

    // An endpoint that is NOT ep_used must not count as covering it either
    // (only a genuinely in-use endpoint counts).
    EndpointPlausibility unused_ep;
    unused_ep.has_stream_assoc     = true;
    unused_ep.request_stream_index = 0;
    // ep_used left false
    snap.endpoints.push_back(unused_ep);
    REQUIRE(check_rcp_cfg(snap) == make_error_code(LifecycleErrc::rcp_cfg_inconsistent));

    snap.endpoints[0].ep_used = true;
    REQUIRE_FALSE(check_rcp_cfg(snap));
}

TEST_CASE("check_rcp_cfg: a fully-consistent snapshot passes", "[lifecycle][REQ-LIFECYCLE-004]") {
    PlausibilitySnapshot snap;
    EndpointPlausibility ep;
    ep.ep_used              = true;
    ep.has_stream_assoc     = true;
    ep.request_stream_index = 0;
    snap.endpoints.push_back(ep);

    RequestStreamPlausibility rs;
    rs.configured             = true;
    rs.has_response_stream    = true;
    rs.response_stream_index  = 0;
    snap.request_streams.push_back(rs);
    snap.response_stream_count = 1;

    REQUIRE_FALSE(check_rcp_cfg(snap));
}

// ── ServerLifecycle::transition — the fully writer/idle-gated state machine ────
// Ported from c-RCP's rcp_lifecycle_transition() (REQ-LIFECYCLE-022/031/037,
// c-RCP issue #198's own access-control gap-closure). Coexists with
// advance() above — see transition()'s own doc comment for why.

TEST_CASE("transition: same-state is always a no-op success, unlike advance()",
          "[lifecycle][REQ-LIFECYCLE-002]") {
    ServerLifecycle lc;
    PlausibilitySnapshot snap;
    WriterCtx writer; // everything false — deliberately unauthorized/non-idle
    REQUIRE_FALSE(lc.transition(ServerState::HwUnconfigured, snap, writer, /*all_other_eps_idle=*/false));
    REQUIRE(lc.state() == ServerState::HwUnconfigured);
}

TEST_CASE("transition: HwUnconfigured -> HwConfigured is guarded by check_hw_cfg, not by writer/idle",
          "[lifecycle][REQ-LIFECYCLE-002][REQ-LIFECYCLE-003]") {
    ServerLifecycle lc;
    PlausibilitySnapshot bad_snap; // no endpoints — vacuously consistent actually; use a real failure
    EndpointPlausibility ep;
    ep.ep_used = true; // missing hw_pin_mapped/has_request_stream
    bad_snap.endpoints.push_back(ep);

    WriterCtx writer; // unauthorized and (irrelevant here) non-idle
    auto ec = lc.transition(ServerState::HwConfigured, bad_snap, writer, false);
    REQUIRE(ec == make_error_code(LifecycleErrc::hw_cfg_inconsistent));
    REQUIRE(lc.state() == ServerState::HwUnconfigured);

    PlausibilitySnapshot ok_snap; // empty — vacuously consistent
    REQUIRE_FALSE(lc.transition(ServerState::HwConfigured, ok_snap, writer, false));
    REQUIRE(lc.state() == ServerState::HwConfigured);
}

TEST_CASE("transition: HwConfigured -> RcpConfigured requires writer authorization",
          "[lifecycle][REQ-LIFECYCLE-002][REQ-LIFECYCLE-031]") {
    ServerLifecycle lc;
    PlausibilitySnapshot snap;
    WriterCtx unauthorized;
    REQUIRE_FALSE(lc.transition(ServerState::HwConfigured, snap, unauthorized, false));

    auto ec = lc.transition(ServerState::RcpConfigured, snap, unauthorized, false);
    REQUIRE(ec == make_error_code(LifecycleErrc::unauthorized));
    REQUIRE(lc.state() == ServerState::HwConfigured);

    WriterCtx authorized;
    authorized.via_discovery_stream = true;
    REQUIRE_FALSE(lc.transition(ServerState::RcpConfigured, snap, authorized, false));
    REQUIRE(lc.state() == ServerState::RcpConfigured);
}

TEST_CASE("transition: HwConfigured -> RcpConfigured is also guarded by check_rcp_cfg once "
          "authorized",
          "[lifecycle][REQ-LIFECYCLE-002][REQ-LIFECYCLE-004]") {
    ServerLifecycle lc;
    PlausibilitySnapshot empty_snap;
    WriterCtx writer;
    writer.via_root_client_ep0 = true;
    REQUIRE_FALSE(lc.transition(ServerState::HwConfigured, empty_snap, writer, false));

    PlausibilitySnapshot bad_snap;
    EndpointPlausibility ep;
    ep.ep_used = true; // missing has_stream_assoc
    bad_snap.endpoints.push_back(ep);

    auto ec = lc.transition(ServerState::RcpConfigured, bad_snap, writer, false);
    REQUIRE(ec == make_error_code(LifecycleErrc::rcp_cfg_inconsistent));
    REQUIRE(lc.state() == ServerState::HwConfigured);
}

TEST_CASE("transition: RcpConfigured -> HwConfigured demotion requires root client or valid "
          "stream association, NOT the discovery stream alone",
          "[lifecycle][REQ-LIFECYCLE-002][REQ-LIFECYCLE-037]") {
    ServerLifecycle lc;
    PlausibilitySnapshot snap;
    WriterCtx via_root;
    via_root.via_root_client_ep0 = true;
    REQUIRE_FALSE(lc.transition(ServerState::HwConfigured, snap, via_root, false));
    REQUIRE_FALSE(lc.transition(ServerState::RcpConfigured, snap, via_root, false));
    REQUIRE(lc.state() == ServerState::RcpConfigured);

    // Discovery-stream-only authorization is NOT sufficient for this
    // specific demotion (REQ-LIFECYCLE-037) — unlike the HwConfigured ->
    // HwUnconfigured reset below, where it IS sufficient.
    WriterCtx via_discovery_only;
    via_discovery_only.via_discovery_stream = true;
    auto ec = lc.transition(ServerState::HwConfigured, snap, via_discovery_only, /*all_other_eps_idle=*/true);
    REQUIRE(ec == make_error_code(LifecycleErrc::unauthorized));
    REQUIRE(lc.state() == ServerState::RcpConfigured);

    // Authorized but not idle -> eps_not_idle.
    auto ec2 = lc.transition(ServerState::HwConfigured, snap, via_root, /*all_other_eps_idle=*/false);
    REQUIRE(ec2 == make_error_code(LifecycleErrc::eps_not_idle));
    REQUIRE(lc.state() == ServerState::RcpConfigured);

    // Authorized and idle -> succeeds.
    REQUIRE_FALSE(lc.transition(ServerState::HwConfigured, snap, via_root, /*all_other_eps_idle=*/true));
    REQUIRE(lc.state() == ServerState::HwConfigured);
}

TEST_CASE("transition: HwConfigured -> HwUnconfigured reset accepts discovery-stream "
          "authorization and requires idleness",
          "[lifecycle][REQ-LIFECYCLE-002][REQ-LIFECYCLE-022]") {
    ServerLifecycle lc;
    PlausibilitySnapshot snap;
    WriterCtx writer;
    writer.via_discovery_stream = true;
    REQUIRE_FALSE(lc.transition(ServerState::HwConfigured, snap, writer, false));

    auto ec = lc.transition(ServerState::HwUnconfigured, snap, writer, /*all_other_eps_idle=*/false);
    REQUIRE(ec == make_error_code(LifecycleErrc::eps_not_idle));
    REQUIRE(lc.state() == ServerState::HwConfigured);

    REQUIRE_FALSE(lc.transition(ServerState::HwUnconfigured, snap, writer, /*all_other_eps_idle=*/true));
    REQUIRE(lc.state() == ServerState::HwUnconfigured);
}

TEST_CASE("transition: RcpConfigured -> HwUnconfigured reset requires the root client ALONE — "
          "discovery stream is no longer sufficient",
          "[lifecycle][REQ-LIFECYCLE-002][REQ-LIFECYCLE-037]") {
    ServerLifecycle lc;
    PlausibilitySnapshot snap;
    WriterCtx via_root;
    via_root.via_root_client_ep0 = true;
    REQUIRE_FALSE(lc.transition(ServerState::HwConfigured, snap, via_root, false));
    REQUIRE_FALSE(lc.transition(ServerState::RcpConfigured, snap, via_root, false));

    WriterCtx via_discovery_only;
    via_discovery_only.via_discovery_stream = true;
    auto ec = lc.transition(ServerState::HwUnconfigured, snap, via_discovery_only, true);
    REQUIRE(ec == make_error_code(LifecycleErrc::unauthorized));
    REQUIRE(lc.state() == ServerState::RcpConfigured);

    // via_valid_stream_association is also NOT sufficient here (unlike the
    // demotion above) — only via_root_client_ep0 authorizes this reset.
    WriterCtx via_stream_assoc;
    via_stream_assoc.via_valid_stream_association = true;
    auto ec2 = lc.transition(ServerState::HwUnconfigured, snap, via_stream_assoc, true);
    REQUIRE(ec2 == make_error_code(LifecycleErrc::unauthorized));

    auto ec3 = lc.transition(ServerState::HwUnconfigured, snap, via_root, /*all_other_eps_idle=*/false);
    REQUIRE(ec3 == make_error_code(LifecycleErrc::eps_not_idle));

    REQUIRE_FALSE(lc.transition(ServerState::HwUnconfigured, snap, via_root, /*all_other_eps_idle=*/true));
    REQUIRE(lc.state() == ServerState::HwUnconfigured);
}

TEST_CASE("transition: skipping HwConfigured entirely is always invalid_transition",
          "[lifecycle][REQ-LIFECYCLE-002]") {
    ServerLifecycle lc;
    PlausibilitySnapshot snap;
    WriterCtx writer;
    writer.via_root_client_ep0 = true;
    auto ec = lc.transition(ServerState::RcpConfigured, snap, writer, true);
    REQUIRE(ec == make_error_code(LifecycleErrc::invalid_transition));
    REQUIRE(lc.state() == ServerState::HwUnconfigured);
}

TEST_CASE("transition fires subscribe_state_changed the same way advance() does",
          "[lifecycle][REQ-LIFECYCLE-002][REQ-LIFECYCLE-007]") {
    ServerLifecycle lc;
    std::vector<std::pair<ServerState, ServerState>> seen;
    lc.subscribe_state_changed([&](ServerState previous, ServerState current) {
        seen.emplace_back(previous, current);
    });

    PlausibilitySnapshot snap;
    WriterCtx writer;
    writer.via_root_client_ep0 = true;
    REQUIRE_FALSE(lc.transition(ServerState::HwConfigured, snap, writer, false));
    REQUIRE_FALSE(lc.transition(ServerState::RcpConfigured, snap, writer, false));
    REQUIRE(seen.size() == 2);
    REQUIRE(seen[0] == std::make_pair(ServerState::HwUnconfigured, ServerState::HwConfigured));
    REQUIRE(seen[1] == std::make_pair(ServerState::HwConfigured, ServerState::RcpConfigured));

    // A same-state no-op does not fire (matches deconfigure()'s own "state
    // actually changed" contract).
    REQUIRE_FALSE(lc.transition(ServerState::RcpConfigured, snap, writer, false));
    REQUIRE(seen.size() == 2);
}

// ── should_accept — per-state request filtering ─────────────────────────────────

TEST_CASE("should_accept: a TSCF frame is dropped when time sync is unsupported and the policy "
          "is Drop, regardless of state",
          "[lifecycle][REQ-LIFECYCLE-014]") {
    auto d = should_accept(ServerState::RcpConfigured, /*time_sync_supported=*/false,
                            rcp::avtp::kSubtypeTscf, rcp::acf::kAcfMsgTypeAbb, kDiscoveryByteBusId,
                            rcp::avtp::TscfFallback::Drop);
    REQUIRE(d == Disposition::Drop);
}

TEST_CASE("should_accept: HwUnconfigured drops TSCF outright and only accepts ACF_ABB on the "
          "discovery byte_bus_id via NTSCF",
          "[lifecycle][REQ-LIFECYCLE-014][REQ-LIFECYCLE-033]") {
    // TSCF is dropped even with time sync supported.
    REQUIRE(should_accept(ServerState::HwUnconfigured, true, rcp::avtp::kSubtypeTscf,
                           rcp::acf::kAcfMsgTypeAbb, kDiscoveryByteBusId,
                           rcp::avtp::TscfFallback::Drop) == Disposition::Drop);

    // NTSCF not addressed to the discovery byte_bus_id is dropped.
    REQUIRE(should_accept(ServerState::HwUnconfigured, true, rcp::avtp::kSubtypeNtscf,
                           rcp::acf::kAcfMsgTypeAbb, /*byte_bus_id=*/7,
                           rcp::avtp::TscfFallback::Drop) == Disposition::Drop);

    // NTSCF + discovery byte_bus_id + ACF_ABB is accepted.
    REQUIRE(should_accept(ServerState::HwUnconfigured, true, rcp::avtp::kSubtypeNtscf,
                           rcp::acf::kAcfMsgTypeAbb, kDiscoveryByteBusId,
                           rcp::avtp::TscfFallback::Drop) == Disposition::Accept);

    // NTSCF + discovery byte_bus_id + ACF_GBB is rejected, not dropped.
    REQUIRE(should_accept(ServerState::HwUnconfigured, true, rcp::avtp::kSubtypeNtscf,
                           rcp::acf::kAcfMsgTypeGbb, kDiscoveryByteBusId,
                           rcp::avtp::TscfFallback::Drop) == Disposition::Reject);
}

TEST_CASE("should_accept: HwConfigured drops TSCF and non-EP0 traffic, accepts EP0 ACF_ABB, "
          "rejects EP0 ACF_GBB",
          "[lifecycle][REQ-LIFECYCLE-014][REQ-LIFECYCLE-033]") {
    REQUIRE(should_accept(ServerState::HwConfigured, true, rcp::avtp::kSubtypeTscf,
                           rcp::acf::kAcfMsgTypeAbb, kDiscoveryByteBusId,
                           rcp::avtp::TscfFallback::Drop) == Disposition::Drop);

    REQUIRE(should_accept(ServerState::HwConfigured, true, rcp::avtp::kSubtypeNtscf,
                           rcp::acf::kAcfMsgTypeAbb, /*byte_bus_id=*/9,
                           rcp::avtp::TscfFallback::Drop) == Disposition::Drop);

    REQUIRE(should_accept(ServerState::HwConfigured, true, rcp::avtp::kSubtypeNtscf,
                           rcp::acf::kAcfMsgTypeAbb, kDiscoveryByteBusId,
                           rcp::avtp::TscfFallback::Drop) == Disposition::Accept);

    REQUIRE(should_accept(ServerState::HwConfigured, true, rcp::avtp::kSubtypeNtscf,
                           rcp::acf::kAcfMsgTypeGbb, kDiscoveryByteBusId,
                           rcp::avtp::TscfFallback::Drop) == Disposition::Reject);
}

TEST_CASE("should_accept: RcpConfigured accepts everything beyond the general time-sync rule",
          "[lifecycle][REQ-LIFECYCLE-014]") {
    REQUIRE(should_accept(ServerState::RcpConfigured, true, rcp::avtp::kSubtypeNtscf,
                           rcp::acf::kAcfMsgTypeGbb, /*byte_bus_id=*/42,
                           rcp::avtp::TscfFallback::Drop) == Disposition::Accept);
    REQUIRE(should_accept(ServerState::RcpConfigured, true, rcp::avtp::kSubtypeTscf,
                           rcp::acf::kAcfMsgTypeAbb, kDiscoveryByteBusId,
                           rcp::avtp::TscfFallback::Drop) == Disposition::Accept);
}

// ── field_writable / field_write_error — register-locking-by-state ──────────────

TEST_CASE("field_writable: HwGeneric is writable only in HwUnconfigured via the discovery stream",
          "[lifecycle][REQ-LIFECYCLE-023]") {
    WriterCtx via_discovery;
    via_discovery.via_discovery_stream = true;
    REQUIRE(field_writable(ServerState::HwUnconfigured, FieldKind::HwGeneric, via_discovery));

    WriterCtx via_root;
    via_root.via_root_client_ep0 = true;
    REQUIRE_FALSE(field_writable(ServerState::HwUnconfigured, FieldKind::HwGeneric, via_root));

    REQUIRE_FALSE(field_writable(ServerState::HwConfigured, FieldKind::HwGeneric, via_discovery));
    REQUIRE_FALSE(field_writable(ServerState::RcpConfigured, FieldKind::HwGeneric, via_discovery));
}

TEST_CASE("field_writable: FunctionalW is unwritable in HwUnconfigured, needs authorization "
          "(incl. discovery) while HwConfigured, and needs authorization (excl. discovery) once "
          "RcpConfigured",
          "[lifecycle][REQ-LIFECYCLE-023]") {
    WriterCtx via_discovery;
    via_discovery.via_discovery_stream = true;
    WriterCtx via_owning;
    via_owning.via_owning_stream = true;
    WriterCtx none;

    REQUIRE_FALSE(field_writable(ServerState::HwUnconfigured, FieldKind::FunctionalW, via_owning));

    REQUIRE(field_writable(ServerState::HwConfigured, FieldKind::FunctionalW, via_discovery));
    REQUIRE(field_writable(ServerState::HwConfigured, FieldKind::FunctionalW, via_owning));
    REQUIRE_FALSE(field_writable(ServerState::HwConfigured, FieldKind::FunctionalW, none));

    // Discovery stream alone no longer suffices once RcpConfigured.
    REQUIRE_FALSE(field_writable(ServerState::RcpConfigured, FieldKind::FunctionalW, via_discovery));
    REQUIRE(field_writable(ServerState::RcpConfigured, FieldKind::FunctionalW, via_owning));
}

TEST_CASE("field_writable: FunctionalWStar is unconditionally writable in HwUnconfigured and "
          "permanently locked once RcpConfigured",
          "[lifecycle][REQ-LIFECYCLE-023]") {
    WriterCtx none;
    REQUIRE(field_writable(ServerState::HwUnconfigured, FieldKind::FunctionalWStar, none));

    WriterCtx via_owning;
    via_owning.via_owning_stream = true;
    REQUIRE(field_writable(ServerState::HwConfigured, FieldKind::FunctionalWStar, via_owning));
    REQUIRE_FALSE(field_writable(ServerState::HwConfigured, FieldKind::FunctionalWStar, none));

    // Locked for ANY writer once RcpConfigured, even a maximally-privileged one.
    REQUIRE_FALSE(field_writable(ServerState::RcpConfigured, FieldKind::FunctionalWStar, via_owning));
}

TEST_CASE("field_writable: ReadOnly is never writable, in any state, by any writer",
          "[lifecycle][REQ-LIFECYCLE-023]") {
    WriterCtx via_owning;
    via_owning.via_owning_stream = true;
    REQUIRE_FALSE(field_writable(ServerState::HwUnconfigured, FieldKind::ReadOnly, via_owning));
    REQUIRE_FALSE(field_writable(ServerState::HwConfigured, FieldKind::ReadOnly, via_owning));
    REQUIRE_FALSE(field_writable(ServerState::RcpConfigured, FieldKind::ReadOnly, via_owning));
}

TEST_CASE("field_writable: a non-unicast frame denies an otherwise-writable field",
          "[lifecycle][REQ-LIFECYCLE-023][REQ-LIFECYCLE-027]") {
    WriterCtx writer;
    writer.via_owning_stream     = true;
    writer.via_non_unicast_frame = true;
    REQUIRE_FALSE(field_writable(ServerState::HwConfigured, FieldKind::FunctionalW, writer));

    writer.via_non_unicast_frame = false;
    REQUIRE(field_writable(ServerState::HwConfigured, FieldKind::FunctionalW, writer));
}

TEST_CASE("field_write_error distinguishes LockedMemAccess (state alone forbids) from "
          "UnauthorizedAccess (writer specifically does not qualify)",
          "[lifecycle][REQ-LIFECYCLE-024]") {
    WriterCtx none;
    // RcpConfigured + FunctionalWStar: even a maximally-privileged writer
    // would be denied -> LockedMemAccess.
    auto locked = field_write_error(ServerState::RcpConfigured, FieldKind::FunctionalWStar, none);
    REQUIRE(locked.has_value());
    REQUIRE(*locked == rcp::acf::WireErrorCode::LockedMemAccess);

    // HwConfigured + FunctionalW with an unauthorized writer: a
    // maximally-privileged writer WOULD succeed -> UnauthorizedAccess.
    auto unauthorized = field_write_error(ServerState::HwConfigured, FieldKind::FunctionalW, none);
    REQUIRE(unauthorized.has_value());
    REQUIRE(*unauthorized == rcp::acf::WireErrorCode::UnauthorizedAccess);

    // Writable -> nullopt.
    WriterCtx via_owning;
    via_owning.via_owning_stream = true;
    REQUIRE_FALSE(field_write_error(ServerState::HwConfigured, FieldKind::FunctionalW, via_owning).has_value());
}

// ── field_writable_w_plus / field_write_error_w_plus — TC18's W+ access type ────

TEST_CASE("field_writable_w_plus follows FunctionalWStar's own rule when unlocked",
          "[lifecycle][REQ-RMAP-055]") {
    WriterCtx via_owning;
    via_owning.via_owning_stream = true;
    REQUIRE(field_writable_w_plus(ServerState::HwUnconfigured, WriterCtx{}, /*locked=*/false));
    REQUIRE(field_writable_w_plus(ServerState::HwConfigured, via_owning, false));
    REQUIRE_FALSE(field_writable_w_plus(ServerState::RcpConfigured, via_owning, false));
}

TEST_CASE("field_writable_w_plus: an explicit lock always wins, in any lifecycle state",
          "[lifecycle][REQ-RMAP-055]") {
    WriterCtx via_owning;
    via_owning.via_owning_stream = true;
    REQUIRE_FALSE(field_writable_w_plus(ServerState::HwUnconfigured, via_owning, /*locked=*/true));
    REQUIRE_FALSE(field_writable_w_plus(ServerState::HwConfigured, via_owning, true));
}

TEST_CASE("field_write_error_w_plus reports LockedMemAccess for an explicit lock unconditionally",
          "[lifecycle][REQ-RMAP-055]") {
    WriterCtx via_owning;
    via_owning.via_owning_stream = true;
    auto err = field_write_error_w_plus(ServerState::HwUnconfigured, via_owning, /*locked=*/true);
    REQUIRE(err.has_value());
    REQUIRE(*err == rcp::acf::WireErrorCode::LockedMemAccess);
}

TEST_CASE("field_write_error_w_plus distinguishes locked/state-locked from writer-unauthorized "
          "when unlocked",
          "[lifecycle][REQ-RMAP-055]") {
    auto locked_by_state = field_write_error_w_plus(ServerState::RcpConfigured, WriterCtx{}, false);
    REQUIRE(locked_by_state.has_value());
    REQUIRE(*locked_by_state == rcp::acf::WireErrorCode::LockedMemAccess);

    auto unauthorized = field_write_error_w_plus(ServerState::HwConfigured, WriterCtx{}, false);
    REQUIRE(unauthorized.has_value());
    REQUIRE(*unauthorized == rcp::acf::WireErrorCode::UnauthorizedAccess);

    WriterCtx via_owning;
    via_owning.via_owning_stream = true;
    REQUIRE_FALSE(field_write_error_w_plus(ServerState::HwConfigured, via_owning, false).has_value());
}
