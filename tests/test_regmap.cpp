// fusa:test REQ-REGMAP-001
// fusa:test REQ-REGMAP-002
// fusa:test REQ-REGMAP-003
// fusa:test REQ-REGMAP-004
// fusa:test REQ-REGMAP-005
// fusa:test REQ-REGMAP-006
// fusa:test REQ-REGMAP-007
// fusa:test REQ-REGMAP-008
// fusa:test REQ-REGMAP-009
// fusa:test REQ-REGMAP-010
// fusa:test REQ-REGMAP-011
// fusa:test REQ-REGMAP-012
// fusa:test REQ-REGMAP-013
// fusa:test REQ-REGMAP-014

// Tests for rcp/regmap.hpp — the RC Server register-map data model and EP0
// pseudo-endpoint (ROADMAP.md milestone 45, "RC Server Lifecycle &
// Register-Map Model", v2.1.0).

#include <catch2/catch_test_macros.hpp>
#include <rcp/regmap.hpp>

using namespace rcp::regmap;
using rcp::lifecycle::ServerLifecycle;
using rcp::lifecycle::ServerState;

namespace {

// make_map builds a RegisterMap with `n` endpoints and pre-sized per-endpoint
// tables, as a real EP0 owner would before handing it to Ep0.
RegisterMap make_map(uint16_t n) {
    RegisterMap m;
    m.endpoint_count = n;
    m.generic_configs.resize(n);
    m.functional_configs.resize(n);
    return m;
}

} // namespace

// ── Generic vs. functional config split ─────────────────────────────────────────

TEST_CASE("EndpointGenericConfig and EndpointFunctionalConfig are distinct, independently settable types",
          "[regmap][REQ-REGMAP-001]") {
    EndpointGenericConfig generic;
    generic.hw_pin_indices = {0, 1, 2};
    generic.request_queue_size  = 4;
    generic.response_queue_size = 4;

    EndpointFunctionalConfig functional;
    functional.data = {0xAA, 0xBB};

    REQUIRE(generic.hw_pin_indices.size() == 3);
    REQUIRE(functional.data.size() == 2);
}

// ── EP0 whole-map read ───────────────────────────────────────────────────────────

TEST_CASE("Any client may read the whole register map through EP0", "[regmap][REQ-REGMAP-002]") {
    auto map = make_map(2);
    map.vendor_id = 0x1234;
    ServerLifecycle lc;
    Ep0 ep0(map, lc);

    REQUIRE_FALSE(ep0.check_read_access(kEp0));
    REQUIRE_FALSE(ep0.check_read_access(1));
    REQUIRE(ep0.read_whole_map().vendor_id == 0x1234);
}

// ── EP0 whole-map write is root-client-only ─────────────────────────────────────

TEST_CASE("Only the root client may write the whole register map through EP0", "[regmap][REQ-REGMAP-003]") {
    auto map = make_map(1);
    ServerLifecycle lc;
    Ep0 ep0(map, lc);

    RegisterMap replacement = make_map(1);
    replacement.vendor_id = 0x99;

    // No root client claimed yet: even the would-be client is unauthorized.
    auto ec = ep0.write_whole_map(/*client=*/1, replacement);
    REQUIRE(ec);
    REQUIRE(ec == make_error_code(RegMapErrc::unauthorized_access));

    REQUIRE_FALSE(ep0.claim_root_client(1));
    REQUIRE_FALSE(ep0.write_whole_map(1, replacement));
    REQUIRE(ep0.read_whole_map().vendor_id == 0x99);

    // A different client is still refused.
    auto ec2 = ep0.write_whole_map(/*client=*/2, replacement);
    REQUIRE(ec2);
    REQUIRE(ec2 == make_error_code(RegMapErrc::unauthorized_access));
}

// ── Root-client claim is exclusive ──────────────────────────────────────────────

TEST_CASE("A second, distinct client cannot claim the root-client slot while it is held",
          "[regmap][REQ-REGMAP-004]") {
    auto map = make_map(1);
    ServerLifecycle lc;
    Ep0 ep0(map, lc);

    REQUIRE_FALSE(ep0.claim_root_client(1));
    auto ec = ep0.claim_root_client(2);
    REQUIRE(ec);
    REQUIRE(ec == make_error_code(RegMapErrc::request_rejected));
    REQUIRE(ep0.is_root_client(1));
    REQUIRE_FALSE(ep0.is_root_client(2));

    // The same client re-claiming its own slot is a harmless no-op.
    REQUIRE_FALSE(ep0.claim_root_client(1));

    // Once released, a different client may claim it.
    ep0.release_root_client();
    REQUIRE_FALSE(ep0.claim_root_client(2));
    REQUIRE(ep0.is_root_client(2));
}

// ── Per-endpoint write restriction ──────────────────────────────────────────────

TEST_CASE("A non-root client may only write the endpoint config it owns", "[regmap][REQ-REGMAP-005]") {
    auto map = make_map(2);
    ServerLifecycle lc;
    Ep0 ep0(map, lc);
    REQUIRE_FALSE(ep0.set_endpoint_owner(1, /*client=*/10));
    REQUIRE_FALSE(ep0.set_endpoint_owner(2, /*client=*/20));

    EndpointGenericConfig cfg;
    cfg.request_queue_size = 8;

    REQUIRE_FALSE(ep0.write_generic_config(/*client=*/10, /*target=*/1, cfg));
    REQUIRE(ep0.read_whole_map().generic_configs[0].request_queue_size == 8);

    auto ec = ep0.write_generic_config(/*client=*/10, /*target=*/2, cfg);
    REQUIRE(ec);
    REQUIRE(ec == make_error_code(RegMapErrc::unauthorized_access));

    // The root client, once claimed, may write any endpoint's config.
    REQUIRE_FALSE(ep0.claim_root_client(/*client=*/99));
    REQUIRE_FALSE(ep0.write_generic_config(/*client=*/99, /*target=*/2, cfg));
    REQUIRE(ep0.read_whole_map().generic_configs[1].request_queue_size == 8);
}

TEST_CASE("Writing to an out-of-range endpoint id is rejected as INVALID_PARAMETER", "[regmap][REQ-REGMAP-005]") {
    auto map = make_map(1);
    ServerLifecycle lc;
    Ep0 ep0(map, lc);
    REQUIRE_FALSE(ep0.claim_root_client(1));

    EndpointFunctionalConfig cfg;
    auto ec = ep0.write_functional_config(1, /*target=*/5, cfg);
    REQUIRE(ec);
    REQUIRE(ec == make_error_code(RegMapErrc::invalid_parameter));
}

// ── Register-locking interacts with EP0 write access ────────────────────────────

TEST_CASE("Generic config writes are refused once the lifecycle locks the generic block",
          "[regmap][REQ-REGMAP-006]") {
    auto map = make_map(1);
    ServerLifecycle lc;
    Ep0 ep0(map, lc);
    REQUIRE_FALSE(ep0.set_endpoint_owner(1, 10));
    REQUIRE_FALSE(lc.advance(ServerState::HwConfigured));

    EndpointGenericConfig cfg;
    auto ec = ep0.write_generic_config(10, 1, cfg);
    REQUIRE(ec);
    REQUIRE(ec == make_error_code(RegMapErrc::locked_mem_access));
}

TEST_CASE("Functional config remains writable at HW_CONFIGURED but locks at RCP_CONFIGURED",
          "[regmap][REQ-REGMAP-006]") {
    auto map = make_map(1);
    ServerLifecycle lc;
    Ep0 ep0(map, lc);
    REQUIRE_FALSE(ep0.set_endpoint_owner(1, 10));
    REQUIRE_FALSE(lc.advance(ServerState::HwConfigured));

    EndpointFunctionalConfig cfg;
    cfg.data = {0x01};
    REQUIRE_FALSE(ep0.write_functional_config(10, 1, cfg));

    REQUIRE_FALSE(lc.advance(ServerState::RcpConfigured));
    auto ec = ep0.write_functional_config(10, 1, cfg);
    REQUIRE(ec);
    REQUIRE(ec == make_error_code(RegMapErrc::locked_mem_access));
}

// ── General bootstrap register fields ───────────────────────────────────────────

TEST_CASE("RegisterMap bootstrap fields hold the values assigned to them", "[regmap][REQ-REGMAP-007]") {
    RegisterMap m;
    m.magic = kRegisterMapMagic;
    m.protocol_version_major = 1;
    m.protocol_version_minor = 0;
    m.vendor_id = 0x00AB;
    m.device_id = 0x00CD;
    m.endpoint_count = 4;
    m.max_streams = 8;
    m.max_queue_depth = 16;
    m.svr_implemented_options = kOptConditionalRequests | kOptSafetyRequests;

    m.hw_pin_map_table       = {0x100, 32};
    m.request_stream_table   = {0x200, 8};
    m.response_stream_table  = {0x280, 8};
    m.ep_id_mapping_table    = {0x300, 4};
    m.functional_config_table = {0x400, 4};

    REQUIRE(m.magic == kRegisterMapMagic);
    REQUIRE(m.vendor_id == 0x00AB);
    REQUIRE(m.device_id == 0x00CD);
    REQUIRE(m.endpoint_count == 4);
    REQUIRE((m.svr_implemented_options & kOptConditionalRequests) != 0);
    REQUIRE((m.svr_implemented_options & kOptFragmentation) == 0);
    REQUIRE(m.hw_pin_map_table.capacity == 32);
    REQUIRE(m.functional_config_table.offset == 0x400);
}

// ── HW pin-mapping config ───────────────────────────────────────────────────────

TEST_CASE("HW pin-map table entries are stored and retrievable", "[regmap][REQ-REGMAP-008]") {
    RegisterMap m;
    m.hw_pin_map.push_back({/*pin_id=*/3, /*function=*/1});
    m.hw_pin_map.push_back({/*pin_id=*/4, /*function=*/2});

    REQUIRE(m.hw_pin_map.size() == 2);
    REQUIRE(m.hw_pin_map[0].pin_id == 3);
    REQUIRE(m.hw_pin_map[1].function == 2);
}

// ── Request-stream config, including inert rx_* fields ──────────────────────────

TEST_CASE("RequestStreamConfig carries the rx_* fields needed later for watchdog/safe-state",
          "[regmap][REQ-REGMAP-009]") {
    RequestStreamConfig rsc;
    rsc.queue_size        = 4;
    rsc.rx_wd_timeout_s     = 5;
    rsc.rx_wd_action        = 1;
    rsc.rx_safety_measure   = 2;

    REQUIRE(rsc.rx_wd_timeout_s == 5);
    REQUIRE(rsc.rx_wd_action == 1);
    REQUIRE(rsc.rx_safety_measure == 2);
}

// ── EP-ID / byte_bus_id mapping table ────────────────────────────────────────────

TEST_CASE("EP-ID mapping table preserves client insertion order without re-sorting it",
          "[regmap][REQ-REGMAP-010]") {
    RegisterMap m;
    m.ep_id_mapping.push_back({/*ep_id=*/3, /*byte_bus_id=*/9});
    m.ep_id_mapping.push_back({/*ep_id=*/1, /*byte_bus_id=*/7});
    m.ep_id_mapping.push_back({/*ep_id=*/2, /*byte_bus_id=*/8});

    // The table is exactly what the client wrote, in the order it wrote it —
    // this server implementation does not re-derive or verify an ordering.
    REQUIRE(m.ep_id_mapping[0].ep_id == 3);
    REQUIRE(m.ep_id_mapping[1].ep_id == 1);
    REQUIRE(m.ep_id_mapping[2].ep_id == 2);
}

// ── Response / ack queue config ──────────────────────────────────────────────────

TEST_CASE("ResponseQueueConfig fields exist and are settable", "[regmap][REQ-REGMAP-011]") {
    ResponseQueueConfig rqc;
    rqc.response_queue_size = 6;
    rqc.ack_queue_size = 6;
    REQUIRE(rqc.response_queue_size == 6);
    REQUIRE(rqc.ack_queue_size == 6);
}

// ── Sequencer-state persistence ──────────────────────────────────────────────────

TEST_CASE("Sequencer-state registers persist independent 8-bit values", "[regmap][REQ-REGMAP-012]") {
    RegisterMap m;
    m.sequencer_states = {0, 0, 0};
    m.sequencer_states[1] = 42;

    REQUIRE(m.sequencer_states[0] == 0);
    REQUIRE(m.sequencer_states[1] == 42);
    REQUIRE(m.sequencer_states[2] == 0);
}

// ── INVALID_PARAMETER ─────────────────────────────────────────────────────────────

TEST_CASE("An out-of-range read target is rejected as INVALID_PARAMETER", "[regmap][REQ-REGMAP-013]") {
    auto map = make_map(1);
    ServerLifecycle lc;
    Ep0 ep0(map, lc);

    auto ec = ep0.check_read_access(/*target=*/7);
    REQUIRE(ec);
    REQUIRE(ec == make_error_code(RegMapErrc::invalid_parameter));
}

TEST_CASE("Assigning an owner to EP0 itself is rejected as INVALID_PARAMETER", "[regmap][REQ-REGMAP-013]") {
    auto map = make_map(1);
    ServerLifecycle lc;
    Ep0 ep0(map, lc);

    auto ec = ep0.set_endpoint_owner(kEp0, 1);
    REQUIRE(ec);
    REQUIRE(ec == make_error_code(RegMapErrc::invalid_parameter));
}

// ── The four mandatory error codes are distinct ─────────────────────────────────

TEST_CASE("The four mandatory register-map error codes are distinct values in their own category",
          "[regmap][REQ-REGMAP-014]") {
    auto unauthorized = make_error_code(RegMapErrc::unauthorized_access);
    auto locked        = make_error_code(RegMapErrc::locked_mem_access);
    auto rejected       = make_error_code(RegMapErrc::request_rejected);
    auto invalid         = make_error_code(RegMapErrc::invalid_parameter);

    REQUIRE(unauthorized.category() == regmap_category());
    REQUIRE(unauthorized != locked);
    REQUIRE(locked != rejected);
    REQUIRE(rejected != invalid);
    REQUIRE(invalid != unauthorized);

    // Every code has a non-empty, distinguishing message.
    REQUIRE_FALSE(unauthorized.message().empty());
    REQUIRE_FALSE(locked.message().empty());
    REQUIRE_FALSE(rejected.message().empty());
    REQUIRE_FALSE(invalid.message().empty());
}
