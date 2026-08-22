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
// fusa:test REQ-REGMAP-015
//
// c-RCP-derived test coverage added in this batch (Phase 17 / cpp-RCP issue
// #129, "Phase 4 batch A"), ported from c-RCP's tests/test_regmap.c and the
// relevant slice of tests/test_tc18_gaps_regmap.c:
// fusa:test REQ-RMAP-001
// fusa:test REQ-RMAP-003
// fusa:test REQ-RMAP-009
// fusa:test REQ-RMAP-010
// fusa:test REQ-RMAP-011
// fusa:test REQ-RMAP-012
// fusa:test REQ-RMAP-016
// fusa:test REQ-RMAP-024
// fusa:test REQ-RMAP-025
// fusa:test REQ-RMAP-030
// fusa:test REQ-RMAP-066
// fusa:test REQ-RMAP-067
// fusa:test REQ-RMAP-070
// fusa:test REQ-RMAP-076
// fusa:test REQ-RMAP-077
// fusa:test REQ-RMAP-078
// fusa:test REQ-RMAP-079
// fusa:test REQ-RMAP-081
// fusa:test REQ-RMAP-086
// fusa:test REQ-RMAP-087

// Tests for rcp/regmap.hpp — the RC Server register-map data model and EP0
// pseudo-endpoint (ROADMAP.md milestone 45, "RC Server Lifecycle &
// Register-Map Model", v2.1.0; content-expanded in ROADMAP.md Phase 17
// "Phase 4 batch A" against c-RCP's current src/regmap.c/include/rcp/regmap.h).

#include <catch2/catch_test_macros.hpp>
#include <rcp/acf.hpp>
#include <rcp/regmap.hpp>

using namespace rcp;
using namespace rcp::regmap;
using rcp::lifecycle::ServerLifecycle;
using rcp::lifecycle::ServerState;

namespace {

// make_map builds a RegisterMap with `n` endpoints and pre-sized per-endpoint
// tables, as a real EP0 owner would before handing it to Ep0.
RegisterMap make_map(uint16_t n) {
    RegisterMap m;
    m.general.svr_ep_count = n;
    m.generic_configs.resize(n);
    m.functional_configs.resize(n);
    return m;
}

} // namespace

// ── EP0 / is_ep0 ─────────────────────────────────────────────────────────────

TEST_CASE("is_ep0 is true only for EP0's own index", "[regmap][REQ-RMAP-001]") {
    REQUIRE(is_ep0(kEp0));
    REQUIRE_FALSE(is_ep0(1));
    REQUIRE_FALSE(is_ep0(42));
    REQUIRE_FALSE(is_ep0(0xFFFF));
}

// ── Generic vs. functional config split ─────────────────────────────────────────

TEST_CASE("EndpointGenericConfig and EndpointFunctionalConfig are distinct, independently settable types",
          "[regmap][REQ-REGMAP-001]") {
    EndpointGenericConfig generic;
    generic.ep_type            = 0x03; // SPI, per c-RCP's Table 29/30 ep_type enum
    generic.ep_description      = 0x11223344;
    generic.ep_tx_buffer_size  = 4;

    EndpointFunctionalConfig functional;
    functional.data = {0xAA, 0xBB};

    REQUIRE(generic.ep_type == 0x03);
    REQUIRE(functional.data.size() == 2);
}

// ── EP0 whole-map read ───────────────────────────────────────────────────────────

TEST_CASE("Any client may read the whole register map through EP0", "[regmap][REQ-REGMAP-002]") {
    auto map = make_map(2);
    map.general.vendor_id = 0x1234;
    ServerLifecycle lc;
    Ep0 ep0(map, lc);

    REQUIRE_FALSE(ep0.check_read_access(kEp0));
    REQUIRE_FALSE(ep0.check_read_access(1));
    REQUIRE(ep0.read_whole_map().general.vendor_id == 0x1234);
}

// ── EP0 whole-map write is root-client-only ─────────────────────────────────────

TEST_CASE("Only the root client may write the whole register map through EP0", "[regmap][REQ-REGMAP-003]") {
    auto map = make_map(1);
    ServerLifecycle lc;
    Ep0 ep0(map, lc);

    RegisterMap replacement = make_map(1);
    replacement.general.vendor_id = 0x99;

    // No root client claimed yet: even the would-be client is unauthorized.
    auto ec = ep0.write_whole_map(/*client=*/1, replacement);
    REQUIRE(ec);
    REQUIRE(ec == make_error_code(RegMapErrc::unauthorized_access));

    REQUIRE_FALSE(ep0.claim_root_client(1));
    REQUIRE_FALSE(ep0.write_whole_map(1, replacement));
    REQUIRE(ep0.read_whole_map().general.vendor_id == 0x99);

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

TEST_CASE("A non-root client may only write the functional config of the endpoint it owns",
          "[regmap][REQ-REGMAP-005]") {
    auto map = make_map(2);
    ServerLifecycle lc;
    Ep0 ep0(map, lc);
    REQUIRE_FALSE(ep0.set_endpoint_owner(1, /*client=*/10));
    REQUIRE_FALSE(ep0.set_endpoint_owner(2, /*client=*/20));

    EndpointFunctionalConfig cfg;
    cfg.data = {0xAA};

    REQUIRE_FALSE(ep0.write_functional_config(/*client=*/10, /*target=*/1, cfg));
    REQUIRE(ep0.read_whole_map().functional_configs[0].data == std::vector<uint8_t>{0xAA});

    // An endpoint owned by somebody else is off limits.
    auto ec = ep0.write_functional_config(/*client=*/10, /*target=*/2, cfg);
    REQUIRE(ec);
    REQUIRE(ec == make_error_code(RegMapErrc::unauthorized_access));

    // The root client, once claimed, may write any endpoint's functional config.
    REQUIRE_FALSE(ep0.claim_root_client(/*client=*/99));
    REQUIRE_FALSE(ep0.write_functional_config(/*client=*/99, /*target=*/2, cfg));
    REQUIRE(ep0.read_whole_map().functional_configs[1].data == std::vector<uint8_t>{0xAA});
}

// The generic config block is *not* covered by the owner grant above: TC18
// §13.1 grants a non-ROOT_CLIENT write access only to the functional config
// of the endpoints allocated to it, and §13.2 describes the generic part of
// the endpoint register map as owned by the RC Server. Before cpp-RCP-D2 this
// implementation applied one identical owner-based check to both blocks, so
// any client that merely owned an endpoint could rewrite that endpoint's HW
// pin mapping, queue sizing, and E2E-CRC enable toggles — a privilege
// escalation into data reserved to the root client.
TEST_CASE("Owning an endpoint does not grant a non-root client write access to its generic config",
          "[regmap][REQ-REGMAP-005]") {
    auto map = make_map(2);
    ServerLifecycle lc;
    Ep0 ep0(map, lc);
    REQUIRE_FALSE(ep0.set_endpoint_owner(1, /*client=*/10));
    REQUIRE_FALSE(ep0.set_endpoint_owner(2, /*client=*/20));

    EndpointGenericConfig cfg;
    cfg.ep_tx_buffer_size = 8;
    cfg.ep_description    = 7;

    // The owner of endpoint 1 is refused on its *own* endpoint's generic block.
    auto ec_own = ep0.write_generic_config(/*client=*/10, /*target=*/1, cfg);
    REQUIRE(ec_own);
    REQUIRE(ec_own == make_error_code(RegMapErrc::unauthorized_access));
    REQUIRE(ep0.read_whole_map().generic_configs[0].ep_tx_buffer_size != 8);
    REQUIRE(ep0.read_whole_map().generic_configs[0].ep_description == 0);

    // ...and, as before, on an endpoint owned by somebody else.
    auto ec_other = ep0.write_generic_config(/*client=*/10, /*target=*/2, cfg);
    REQUIRE(ec_other);
    REQUIRE(ec_other == make_error_code(RegMapErrc::unauthorized_access));

    // check_write_access reports the same asymmetry directly, and defaults to
    // the stricter (generic) check when no block is named.
    REQUIRE(ep0.check_write_access(/*client=*/10, /*target=*/1, Ep0::ConfigBlock::Generic) ==
            make_error_code(RegMapErrc::unauthorized_access));
    REQUIRE(ep0.check_write_access(/*client=*/10, /*target=*/1) ==
            make_error_code(RegMapErrc::unauthorized_access));
    REQUIRE_FALSE(ep0.check_write_access(/*client=*/10, /*target=*/1, Ep0::ConfigBlock::Functional));

    // Only the root client may write the generic block.
    REQUIRE_FALSE(ep0.claim_root_client(/*client=*/99));
    REQUIRE_FALSE(ep0.write_generic_config(/*client=*/99, /*target=*/1, cfg));
    REQUIRE(ep0.read_whole_map().generic_configs[0].ep_tx_buffer_size == 8);
    REQUIRE_FALSE(ep0.write_generic_config(/*client=*/99, /*target=*/2, cfg));
    REQUIRE(ep0.read_whole_map().generic_configs[1].ep_tx_buffer_size == 8);
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
    // The generic block is root-client-only (TC18 §13.1/§13.2, cpp-RCP-D2),
    // so the lock is what has to refuse this write — use the root client, or
    // the authorization check would mask the lock check being tested.
    REQUIRE_FALSE(ep0.claim_root_client(10));
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

// ── General bootstrap register fields (GeneralMap) ──────────────────────────────

TEST_CASE("GeneralMap default-constructs zeroed, with the no-root-client sentinel",
          "[regmap][REQ-RMAP-003]") {
    GeneralMap map;

    REQUIRE(map.magic == kRegisterMapMagic);
    REQUIRE(map.svr_version == 0);
    REQUIRE(map.vendor_id == 0);
    REQUIRE(map.device_id == 0);
    REQUIRE(map.svr_ep_count == 0);
    REQUIRE(map.svr_req_stream_max == 0);
    REQUIRE(map.svr_responder_streams_max == 0);
    REQUIRE(map.svr_sequencers_max == 0);
    REQUIRE(map.svr_configuration_lock == 0);
    REQUIRE(map.svr_responder_mem_size == 0);
    REQUIRE(map.svr_req_mem_size == 0);
    REQUIRE(map.svr_implemented_options == 0);
    REQUIRE(map.svr_root_client_index == kNoRootClient);
    REQUIRE(map.svr_hw_cfg_ptr == 0);
    REQUIRE(map.svr_request_stream_cfg_capacity == 0);
    REQUIRE(map.svr_response_stream_cfg_capacity == 0);
    REQUIRE(map.svr_ep_generic_cfg_ptr == 0);
    REQUIRE(map.svr_ep_generic_cfg_capacity == 0);
    REQUIRE(map.svr_ep_functional_cfg_ptr == 0);
    REQUIRE(map.svr_ep_bytebus_id_map_ptr == 0);
    REQUIRE(map.svr_ep_bytebus_id_map_capacity == 0);
    REQUIRE(map.svr_sequencer_state_ptr == 0);
    REQUIRE(map.svr_network_interface_cfg_ptr == 0);
    REQUIRE(map.svr_network_interface_cfg_capacity == 0);
    REQUIRE(map.svr_physical_layer_cfg_ptr == 0);
    REQUIRE(map.svr_physical_layer_cfg_capacity == 0);
    REQUIRE(map.svr_time_synch_cfg_ptr == 0);
    REQUIRE(map.svr_time_synch_cfg_capacity == 0);
    REQUIRE(map.svr_security_cfg_ptr == 0);
    REQUIRE(map.svr_security_cfg_capacity == 0);
    REQUIRE(map.svr_device_specific_cfg_ptr == 0);
    REQUIRE(map.svr_device_specific_cfg_capacity == 0);
}

TEST_CASE("RegisterMap bootstrap fields hold the values assigned to them", "[regmap][REQ-REGMAP-007]") {
    RegisterMap m;
    m.general.magic       = kRegisterMapMagic;
    m.general.svr_version = 0x00010000;
    m.general.vendor_id   = 0x00AB;
    m.general.device_id   = 0x00CD;
    m.general.svr_ep_count = 4;
    m.general.svr_implemented_options = kOptCompoundWait | kOptChained;

    m.hw_pin_map_table       = {0x100, 32};
    m.request_stream_table   = {0x200, 8};
    m.response_stream_table  = {0x280, 8};
    m.ep_id_mapping_table    = {0x300, 4};
    m.functional_config_table = {0x400, 4};

    REQUIRE(m.general.magic == kRegisterMapMagic);
    REQUIRE(m.general.vendor_id == 0x00AB);
    REQUIRE(m.general.device_id == 0x00CD);
    REQUIRE(m.general.svr_ep_count == 4);
    REQUIRE((m.general.svr_implemented_options & kOptCompoundWait) != 0);
    REQUIRE((m.general.svr_implemented_options & kOptTrigger) == 0);
    REQUIRE(m.hw_pin_map_table.capacity == 32);
    REQUIRE(m.functional_config_table.offset == 0x400);
}

// ── svr_implemented_options: five independent bits (REQ-RMAP-030) ───────────────

TEST_CASE("The five REQ-RMAP-030 option bits are pairwise distinct", "[regmap][REQ-RMAP-030]") {
    const uint8_t bits[5] = {kOptCompoundWait, kOptTrigger, kOptChained, kOptTimeSync, kOptEnhCancel};
    for (size_t i = 0; i < 5; ++i) {
        REQUIRE(bits[i] != 0);
        for (size_t j = 0; j < i; ++j) REQUIRE(bits[i] != bits[j]);
    }
}

TEST_CASE("Each option bit is independently settable with no sibling requirement",
          "[regmap][REQ-RMAP-030]") {
    GeneralMap map;

    map.svr_implemented_options = kOptTrigger; // just one bit -- no sibling needed
    REQUIRE(map.svr_implemented_options == kOptTrigger);

    map.svr_implemented_options = static_cast<uint8_t>(kOptCompoundWait | kOptChained);
    REQUIRE(map.svr_implemented_options == static_cast<uint8_t>(kOptCompoundWait | kOptChained));

    map.svr_implemented_options =
        static_cast<uint8_t>(kOptCompoundWait | kOptTrigger | kOptChained | kOptTimeSync | kOptEnhCancel);
    REQUIRE(map.svr_implemented_options == 0x1Fu); // all five bits, 0b00011111
}

// ── GeneralMap Table 20 wire codec (REQ-RMAP-024/025) ────────────────────────────

TEST_CASE("GeneralMap render() places each field at its own TC18-cited absolute address",
          "[regmap][REQ-RMAP-024]") {
    GeneralMap map;
    map.magic                    = 0x11223344;
    map.svr_version              = 0x00010203;
    map.vendor_id                = 0xAABB;
    map.device_id                = 0xCCDD;
    map.svr_ep_count             = 0x0005;
    map.svr_req_stream_max       = 0x06;
    map.svr_responder_streams_max = 0x07;
    map.svr_responder_mem_size   = 0x0809;
    map.svr_req_mem_size         = 0x0A0B;
    map.svr_sequencers_max       = 0x0C;
    map.svr_configuration_lock   = 0x00;
    map.svr_implemented_options  = kOptCompoundWait;
    map.svr_io_pin_count         = 0x0D0E;

    const auto image = render(map);
    REQUIRE(image.size() == kGeneralMapLen);
    REQUIRE(image[0x0000] == 0x11);
    REQUIRE(image[0x0003] == 0x44);
    REQUIRE(image[0x0008] == 0xAA);
    REQUIRE(image[0x0009] == 0xBB);
    REQUIRE(image[0x000A] == 0xCC);
    REQUIRE(image[0x000C] == 0x00);
    REQUIRE(image[0x000D] == 0x05);
    REQUIRE(image[0x000E] == 0x06);
    REQUIRE(image[0x000F] == 0x07);
    REQUIRE(image[0x0016] == kOptCompoundWait);
    REQUIRE(image[0x0018] == 0x0D);
    REQUIRE(image[0x0019] == 0x0E);
}

TEST_CASE("GeneralMap render() never places svr_lifecycle_state or svr_root_client_index on the wire",
          "[regmap][REQ-RMAP-023]") {
    GeneralMap map;
    map.svr_lifecycle_state   = 0xFF; // no genuine Table 20 address exists for this field
    map.svr_root_client_index = 42;

    const auto image = render(map);
    // 0x000E/0x000F are svr_req_stream_max/svr_responder_streams_max, both
    // left at their own zero default -- neither excluded field leaks in.
    REQUIRE(image[0x000E] == 0x00);
    REQUIRE(image[0x000F] == 0x00);
}

TEST_CASE("encode_read_response then decode_read_response round-trips GeneralMap's Table 20 fields",
          "[regmap][REQ-RMAP-024]") {
    GeneralMap map;
    map.magic             = 0xDEADBEEF;
    map.svr_version       = 0x00020000;
    map.vendor_id         = 0x1234;
    map.device_id         = 0x5678;
    map.svr_ep_count      = 3;
    map.svr_req_stream_max = 4;
    map.svr_implemented_options = kOptTrigger;
    map.svr_device_specific_cfg_capacity = 0x0010;

    const auto encoded = encode_read_response(map, static_cast<uint8_t>(kGeneralMapLen), /*transaction_num=*/7);

    GeneralMap decoded;
    auto ec = decode_read_response(encoded.data(), encoded.size(), decoded);
    REQUIRE_FALSE(ec);
    REQUIRE(decoded.magic == map.magic);
    REQUIRE(decoded.svr_version == map.svr_version);
    REQUIRE(decoded.vendor_id == map.vendor_id);
    REQUIRE(decoded.device_id == map.device_id);
    REQUIRE(decoded.svr_ep_count == map.svr_ep_count);
    REQUIRE(decoded.svr_req_stream_max == map.svr_req_stream_max);
    REQUIRE(decoded.svr_implemented_options == map.svr_implemented_options);
    REQUIRE(decoded.svr_device_specific_cfg_capacity == map.svr_device_specific_cfg_capacity);
    // Excluded from the wire image -- stays at decoded's own default, not map's.
    REQUIRE(decoded.svr_root_client_index == kNoRootClient);
}

TEST_CASE("decode_read_response rejects a non-EP0 byte_bus_id, a write op, and a too-short frame",
          "[regmap][REQ-RMAP-024]") {
    GeneralMap map;
    const auto encoded = encode_read_response(map, static_cast<uint8_t>(kGeneralMapLen), 1);

    GeneralMap decoded;
    REQUIRE_FALSE(decode_read_response(encoded.data(), encoded.size(), decoded));

    std::vector<uint8_t> too_short(encoded.begin(), encoded.begin() + 2);
    auto ec_short = decode_read_response(too_short.data(), too_short.size(), decoded);
    REQUIRE(ec_short == make_error_code(GeneralMapErrc::short_frame));
}

TEST_CASE("decode_write_request always reports LOCKED_MEM_ACCESS -- Table 20 is entirely read-only",
          "[regmap][REQ-RMAP-025]") {
    acf::AcfMessageInfo hdr;
    hdr.byte_bus_id     = static_cast<avtp::ByteBusId>(kEp0);
    hdr.op              = true; // write
    hdr.transaction_num = 42;
    const auto encoded = acf::encode_acf_abb(hdr, {0x00, 0x01, 0x02});

    acf::WireErrorCode out_error{};
    uint8_t            out_txn = 0;
    auto ec = decode_write_request(encoded.data(), encoded.size(), out_error, out_txn);
    REQUIRE_FALSE(ec);
    REQUIRE(out_error == acf::WireErrorCode::LockedMemAccess);
    REQUIRE(out_txn == 42);
}

TEST_CASE("decode_write_request rejects a read op (wrong_op) and a non-EP0 bus (wrong_bus)",
          "[regmap][REQ-RMAP-025]") {
    acf::AcfMessageInfo read_hdr;
    read_hdr.byte_bus_id = static_cast<avtp::ByteBusId>(kEp0);
    read_hdr.op          = false; // read, not write
    const auto read_encoded = acf::encode_acf_abb(read_hdr, {});

    acf::WireErrorCode out_error{};
    uint8_t            out_txn = 0;
    REQUIRE(decode_write_request(read_encoded.data(), read_encoded.size(), out_error, out_txn) ==
            make_error_code(GeneralMapErrc::wrong_op));

    acf::AcfMessageInfo wrong_bus_hdr;
    wrong_bus_hdr.byte_bus_id = static_cast<avtp::ByteBusId>(kEp0) + 1;
    wrong_bus_hdr.op          = true;
    const auto wrong_bus_encoded = acf::encode_acf_abb(wrong_bus_hdr, {});
    REQUIRE(decode_write_request(wrong_bus_encoded.data(), wrong_bus_encoded.size(), out_error, out_txn) ==
            make_error_code(GeneralMapErrc::wrong_bus));
}

// ── Root-client / per-EP-restricted-client model: writer_ctx() ───────────────────

TEST_CASE("writer_ctx grants via_root_client_ep0 only via EP0 on the exact root-client stream",
          "[regmap][REQ-RMAP-009]") {
    GeneralMap map;
    map.svr_root_client_index = 7;

    auto ctx = writer_ctx(map, nullptr, 7, /*via_ep0=*/true, /*via_unicast=*/true, false, 0, nullptr, 0);
    REQUIRE(ctx.via_root_client_ep0);
    REQUIRE_FALSE(ctx.via_owning_stream);

    REQUIRE_FALSE(writer_ctx(map, nullptr, 8, true, true, false, 0, nullptr, 0).via_root_client_ep0);
    REQUIRE_FALSE(writer_ctx(map, nullptr, 7, false, true, false, 0, nullptr, 0).via_root_client_ep0);
}

TEST_CASE("writer_ctx denies via_root_client_ep0 when no root client is configured",
          "[regmap][REQ-RMAP-009]") {
    GeneralMap map; // svr_root_client_index == kNoRootClient
    auto ctx = writer_ctx(map, nullptr, kNoRootClient, true, true, false, 0, nullptr, 0);
    REQUIRE_FALSE(ctx.via_root_client_ep0);
}

TEST_CASE("writer_ctx grants via_owning_stream only for the matching stream index",
          "[regmap][REQ-RMAP-010]") {
    GeneralMap map;
    EpClient   owner;
    owner.has_owning_stream   = true;
    owner.owning_stream_index = 3;

    auto ctx = writer_ctx(map, &owner, 3, false, true, false, 0, nullptr, 0);
    REQUIRE(ctx.via_owning_stream);
    REQUIRE_FALSE(ctx.via_root_client_ep0);

    REQUIRE_FALSE(writer_ctx(map, nullptr, 3, false, true, false, 0, nullptr, 0).via_owning_stream);

    EpClient no_owner;
    no_owner.has_owning_stream   = false;
    no_owner.owning_stream_index = 3;
    REQUIRE_FALSE(writer_ctx(map, &no_owner, 3, false, true, false, 0, nullptr, 0).via_owning_stream);

    REQUIRE_FALSE(writer_ctx(map, &owner, 4, false, true, false, 0, nullptr, 0).via_owning_stream);
}

TEST_CASE("writer_ctx plumbs via_unicast to via_non_unicast_frame independent of root-client status",
          "[regmap][REQ-LIFECYCLE-027]") {
    GeneralMap map;
    map.svr_root_client_index = 7;

    auto ctx_unicast = writer_ctx(map, nullptr, 7, true, true, false, 0, nullptr, 0);
    REQUIRE_FALSE(ctx_unicast.via_non_unicast_frame);
    REQUIRE(ctx_unicast.via_root_client_ep0);

    auto ctx_non_unicast = writer_ctx(map, nullptr, 7, true, false, false, 0, nullptr, 0);
    REQUIRE(ctx_non_unicast.via_non_unicast_frame);
    REQUIRE(ctx_non_unicast.via_root_client_ep0); // independent axis
}

TEST_CASE("writer_ctx plumbs via_discovery_stream straight through", "[regmap][REQ-RMAP-070]") {
    GeneralMap map; // no root client, no owning stream

    auto ctx = writer_ctx(map, nullptr, 3, false, true, true, 0, nullptr, 0);
    REQUIRE(ctx.via_discovery_stream);
    REQUIRE_FALSE(ctx.via_root_client_ep0);
    REQUIRE_FALSE(ctx.via_owning_stream);

    REQUIRE_FALSE(writer_ctx(map, nullptr, 3, false, true, false, 0, nullptr, 0).via_discovery_stream);
}

// REQ-LIFECYCLE-025/031 (issue #341 lineage, batch B): writer_ctx() now
// evaluates a real, matching EP-ID/byte_bus_id association via
// ep_id_map::is_valid_association() (batch A's own fail-closed stub is
// gone) -- see writer_ctx()'s own doc comment.
TEST_CASE("writer_ctx grants via_valid_stream_association for a real, matching association when no root client",
          "[regmap][REQ-LIFECYCLE-025]") {
    GeneralMap map; // no root client configured
    EpIdMappingEntry entries[1] = {{1, 0x100, 2}}; // ep 1, bbid 0x100, stream 2

    auto ctx = writer_ctx(map, nullptr, 2, false, true, false, 0x100, entries, 1);
    REQUIRE(ctx.via_valid_stream_association);
    REQUIRE_FALSE(ctx.via_root_client_ep0); // independent axis, not conflated
}

TEST_CASE("writer_ctx denies via_valid_stream_association once a root client is configured",
          "[regmap][REQ-LIFECYCLE-025]") {
    GeneralMap map;
    map.svr_root_client_index = 7; // a root client IS configured
    EpIdMappingEntry entries[1] = {{1, 0x100, 2}};

    // Same otherwise-valid (stream 2, bbid 0x100) association as the test
    // above -- denied purely because a root client now exists.
    auto ctx = writer_ctx(map, nullptr, 2, false, true, false, 0x100, entries, 1);
    REQUIRE_FALSE(ctx.via_valid_stream_association);
}

TEST_CASE("writer_ctx denies via_valid_stream_association for an unrecognized pair",
          "[regmap][REQ-LIFECYCLE-025]") {
    GeneralMap map; // no root client
    EpIdMappingEntry entries[1] = {{1, 0x100, 2}};

    // Right stream, wrong byte_bus_id -- not a real association.
    auto ctx = writer_ctx(map, nullptr, 2, false, true, false, 0x200, entries, 1);
    REQUIRE_FALSE(ctx.via_valid_stream_association);
}

TEST_CASE("writer_ctx explicitly assigns every member of the returned WriterCtx", "[regmap][REQ-RMAP-086]") {
    GeneralMap map;
    map.svr_root_client_index = 7;
    EpClient owner;
    owner.has_owning_stream   = true;
    owner.owning_stream_index = 7;
    EpIdMappingEntry entries[1] = {{1, 0x100, 2}};

    auto ctx = writer_ctx(map, &owner, 7, true, false, true, 0x100, entries, 1);
    REQUIRE(ctx.via_root_client_ep0);
    REQUIRE(ctx.via_owning_stream);
    REQUIRE(ctx.via_non_unicast_frame);
    REQUIRE(ctx.via_discovery_stream);
    REQUIRE_FALSE(ctx.via_valid_stream_association); // deterministically false: a root client IS configured

    GeneralMap no_root; // svr_root_client_index == kNoRootClient
    auto ctx2 = writer_ctx(no_root, nullptr, 2, false, true, false, 0x100, entries, 1);
    REQUIRE_FALSE(ctx2.via_root_client_ep0);
    REQUIRE_FALSE(ctx2.via_owning_stream);
    REQUIRE_FALSE(ctx2.via_non_unicast_frame);
    REQUIRE_FALSE(ctx2.via_discovery_stream);
    REQUIRE(ctx2.via_valid_stream_association); // the one member Call A couldn't exercise as true
}

// ── RC Server's own functional-configuration content (SvrEpCfg) ──────────────────

TEST_CASE("SvrEpCfg default-constructs with TC18's own stated discovery-timeout default",
          "[regmap][REQ-RMAP-066]") {
    SvrEpCfg cfg;
    REQUIRE(cfg.svr_discovery_timeout == 20000);
    REQUIRE(cfg.svr_ep_status == 0);

    cfg.svr_discovery_timeout = 5000;
    cfg.svr_ep_status         = 0x0001;
    REQUIRE(cfg.svr_discovery_timeout == 5000);
    REQUIRE(cfg.svr_ep_status == 0x0001);
}

// ── EndpointGenericConfig: per-endpoint E2E CRC safe-mode toggles (pre-existing) ─

TEST_CASE("EndpointGenericConfig's ep_*_crc_enable toggles default false and are independently settable",
          "[regmap][REQ-REGMAP-015]") {
    EndpointGenericConfig cfg;
    REQUIRE_FALSE(cfg.ep_req_crc_enable);
    REQUIRE_FALSE(cfg.ep_ack_crc_enable);
    REQUIRE_FALSE(cfg.ep_response_crc_enable);

    cfg.ep_ack_crc_enable = true;
    REQUIRE_FALSE(cfg.ep_req_crc_enable);
    REQUIRE(cfg.ep_ack_crc_enable);
    REQUIRE_FALSE(cfg.ep_response_crc_enable);
}

// ── ep_generic_cfg boundary conversions (REQ-RMAP-076/077) ───────────────────────

TEST_CASE("ep_delay_time_us_to_reg accepts exactly TC18's own four allowed values", "[regmap][REQ-RMAP-076]") {
    uint8_t reg = 0xFF;
    REQUIRE(ep_generic_cfg::ep_delay_time_us_to_reg(1, reg));
    REQUIRE(reg == 0);
    REQUIRE(ep_generic_cfg::ep_delay_time_us_to_reg(10, reg));
    REQUIRE(reg == 1);
    REQUIRE(ep_generic_cfg::ep_delay_time_us_to_reg(20, reg));
    REQUIRE(reg == 2);
    REQUIRE(ep_generic_cfg::ep_delay_time_us_to_reg(50, reg));
    REQUIRE(reg == 3);
}

TEST_CASE("ep_delay_time_us_to_reg rejects (not rounds) every other microsecond value",
          "[regmap][REQ-RMAP-076]") {
    uint8_t reg = 0xFF;
    REQUIRE_FALSE(ep_generic_cfg::ep_delay_time_us_to_reg(0, reg));
    REQUIRE_FALSE(ep_generic_cfg::ep_delay_time_us_to_reg(15, reg));
    REQUIRE_FALSE(ep_generic_cfg::ep_delay_time_us_to_reg(999999, reg));
    REQUIRE(reg == 0xFF); // untouched on rejection
}

TEST_CASE("ep_delay_time_reg_to_us covers all 4 register values and masks out-of-range input",
          "[regmap][REQ-RMAP-076]") {
    REQUIRE(ep_generic_cfg::ep_delay_time_reg_to_us(0) == 1);
    REQUIRE(ep_generic_cfg::ep_delay_time_reg_to_us(1) == 10);
    REQUIRE(ep_generic_cfg::ep_delay_time_reg_to_us(2) == 20);
    REQUIRE(ep_generic_cfg::ep_delay_time_reg_to_us(3) == 50);
    // Masked to 2 bits: 0b100 == 4 -> masked to 0.
    REQUIRE(ep_generic_cfg::ep_delay_time_reg_to_us(4) == 1);
}

TEST_CASE("ep_req_storage_size_words_to_octets is always exact", "[regmap][REQ-RMAP-077]") {
    REQUIRE(ep_generic_cfg::ep_req_storage_size_words_to_octets(0) == 0);
    REQUIRE(ep_generic_cfg::ep_req_storage_size_words_to_octets(2) == 8);
    REQUIRE(ep_generic_cfg::ep_req_storage_size_words_to_octets(0xFFFF) == 262140);
}

TEST_CASE("ep_req_storage_size_octets_to_words round-trips and rejects non-multiples-of-4/overflow",
          "[regmap][REQ-RMAP-077]") {
    uint16_t words = 0xFFFF;
    REQUIRE(ep_generic_cfg::ep_req_storage_size_octets_to_words(8, words));
    REQUIRE(words == 2);

    REQUIRE_FALSE(ep_generic_cfg::ep_req_storage_size_octets_to_words(9, words)); // not a multiple of 4
    REQUIRE(words == 2); // untouched on rejection

    REQUIRE_FALSE(ep_generic_cfg::ep_req_storage_size_octets_to_words(262144, words)); // one word past max
}

// ── ep_generic_cfg wire codec, READ side (REQ-RMAP-078/081) ──────────────────────

TEST_CASE("ep_generic_cfg::render places each field at its own TC18-cited byte offset",
          "[regmap][REQ-RMAP-078]") {
    EndpointGenericConfig row;
    row.ep_type            = 0x03; // SPI
    row.ep_used             = true;
    row.ep_delay_time       = 20;  // register value 2 (10b)
    row.ep_req_storage_size = 8;   // 2 words
    row.ep_description      = 0x11223344;
    row.ep_tx_buffer_size   = 0x5566;
    row.ep_rx_buffer_size   = 0x7788;

    uint8_t out[12];
    ep_generic_cfg::render(&row, 1, out);

    REQUIRE(out[0] == 0x03);
    REQUIRE(out[1] == 0x21); // ep_used=1 (bit0) | delay_reg=2 (bits4:5)
    REQUIRE(out[2] == 0x00);
    REQUIRE(out[3] == 0x02);
    REQUIRE(out[4] == 0x11);
    REQUIRE(out[5] == 0x22);
    REQUIRE(out[6] == 0x33);
    REQUIRE(out[7] == 0x44);
    REQUIRE(out[8] == 0x55);
    REQUIRE(out[9] == 0x66);
    REQUIRE(out[10] == 0x77);
    REQUIRE(out[11] == 0x88);
}

TEST_CASE("ep_generic_cfg::render uses a 12-octet stride across entries", "[regmap][REQ-RMAP-078]") {
    EndpointGenericConfig rows[2];
    rows[0].ep_type = 0xAA;
    rows[1].ep_type = 0xBB;

    uint8_t out[24];
    ep_generic_cfg::render(rows, 2, out);

    REQUIRE(out[0] == 0xAA);
    REQUIRE(out[12] == 0xBB);
}

TEST_CASE("ep_generic_cfg::render falls back to 1us for an unconfigured or disallowed delay time",
          "[regmap][REQ-RMAP-078]") {
    EndpointGenericConfig row; // ep_delay_time defaults to 0us -- not one of the 4 allowed values
    uint8_t out[12];
    ep_generic_cfg::render(&row, 1, out);
    REQUIRE((out[1] & 0x30u) == 0x00); // falls back to register 0 (1us)

    row.ep_delay_time = 999999;
    ep_generic_cfg::render(&row, 1, out);
    REQUIRE((out[1] & 0x30u) == 0x00);
}

TEST_CASE("ep_generic_cfg::render clamps an oversized or non-multiple-of-4 req_storage_size",
          "[regmap][REQ-RMAP-078]") {
    EndpointGenericConfig row;
    row.ep_req_storage_size = 262144; // one word past the register's own max
    uint8_t out[12];
    ep_generic_cfg::render(&row, 1, out);
    REQUIRE(out[2] == 0xFF);
    REQUIRE(out[3] == 0xFF); // clamped to max, not wrapped

    row.ep_req_storage_size = 9; // not an exact multiple of 4 -- floors to 2 words
    ep_generic_cfg::render(&row, 1, out);
    REQUIRE(out[2] == 0x00);
    REQUIRE(out[3] == 0x02);
}

// REQ-RMAP-081 (issue #467): TC18's own prose names a configuration
// parameter (EP_RESP_ON_ERROR) that Table 31 itself never actually defines
// -- see c-RCP's own investigation, ported into this header's own
// EndpointGenericConfig doc comment. This test pins that render() invents
// no bit for it: both reserved spans of octet 1 (bits [3:1] and [7:6], mask
// 0xCE) stay zero for every input, including inputs deliberately chosen to
// be non-zero/extreme everywhere else.
TEST_CASE("ep_generic_cfg::render never sets a bit for the dangling EP_RESP_ON_ERROR reference",
          "[regmap][REQ-RMAP-081]") {
    EndpointGenericConfig row;
    row.ep_type            = 0xFF;
    row.ep_used             = true;
    row.ep_delay_time       = 50;
    row.ep_req_storage_size = 0xFFFFFFFF;
    row.ep_description      = 0xFFFFFFFF;
    row.ep_tx_buffer_size   = 0xFFFF;
    row.ep_rx_buffer_size   = 0xFFFF;

    uint8_t out[12];
    ep_generic_cfg::render(&row, 1, out);
    REQUIRE((out[1] & 0xCEu) == 0x00);
}

// ── ep_generic_cfg wire codec, WRITE side (REQ-RMAP-079/087) ─────────────────────

TEST_CASE("ep_generic_cfg::apply_reconfig patches only the addressed octets", "[regmap][REQ-RMAP-079]") {
    EndpointGenericConfig rows[2];
    rows[0].ep_type = 0xAA; // must survive -- read-only

    const uint8_t patch[12] = {
        0xFF,             // ep_type -- must have no effect
        0x21,             // ep_used=1, delay_reg=2 (20us)
        0x00, 0x02,       // ep_req_storage_size: 2 words = 8 octets
        0x11, 0x22, 0x33, 0x44, // ep_description
        0x55, 0x66,       // ep_tx_buffer_size
        0x77, 0x88,       // ep_rx_buffer_size
    };

    auto ec = ep_generic_cfg::apply_reconfig(rows, 2, 0, patch, sizeof(patch));
    REQUIRE_FALSE(ec);
    REQUIRE(rows[0].ep_type == 0xAA); // unchanged -- read-only
    REQUIRE(rows[0].ep_used);
    REQUIRE(rows[0].ep_delay_time == 20);
    REQUIRE(rows[0].ep_req_storage_size == 8);
    REQUIRE(rows[0].ep_description == 0x11223344);
    REQUIRE(rows[0].ep_tx_buffer_size == 0x5566);
    REQUIRE(rows[0].ep_rx_buffer_size == 0x7788);
    // row 1 (relative 12-23) entirely untouched by this write.
    REQUIRE(rows[1].ep_type == 0);
    REQUIRE_FALSE(rows[1].ep_used);
}

TEST_CASE("ep_generic_cfg::apply_reconfig touching only ep_type is a no-op, confirmed normally (OK)",
          "[regmap][REQ-RMAP-079]") {
    EndpointGenericConfig row;
    row.ep_type = 0x03;
    const uint8_t patch[1] = {0xFF};

    // TC18 §13.7.1.2: "Writing data to read only registers has no effect
    // and request is confirmed normally" -- OK, not an error.
    REQUIRE_FALSE(ep_generic_cfg::apply_reconfig(&row, 1, 0, patch, sizeof(patch)));
    REQUIRE(row.ep_type == 0x03);
}

TEST_CASE("ep_generic_cfg::apply_reconfig forces row 0's ep_used true, honors it normally elsewhere",
          "[regmap][REQ-RMAP-087]") {
    EndpointGenericConfig row;
    row.ep_used = true; // EP0's own required-always-on state
    const uint8_t patch[1] = {0x00}; // ep_used=0, delay_reg=0 -- targets only octet 0x0001

    REQUIRE_FALSE(ep_generic_cfg::apply_reconfig(&row, 1, 1, patch, sizeof(patch)));
    REQUIRE(row.ep_used); // forced -- incoming 0 bit has no effect on row 0
    REQUIRE(row.ep_delay_time == 1); // ep_delay_time is NOT part of the override
}

TEST_CASE("ep_generic_cfg::apply_reconfig's row-0-only ep_used override does not leak to EP1",
          "[regmap][REQ-RMAP-087]") {
    EndpointGenericConfig rows[2];
    rows[0].ep_used = true;
    rows[1].ep_used = true;

    const uint8_t clear_row0[1] = {0x00};
    REQUIRE_FALSE(ep_generic_cfg::apply_reconfig(rows, 2, 1, clear_row0, 1));
    REQUIRE(rows[0].ep_used); // row 0: forced, write ignored

    const uint8_t clear_row1[1] = {0x00};
    REQUIRE_FALSE(ep_generic_cfg::apply_reconfig(rows, 2, 13, clear_row1, 1));
    REQUIRE_FALSE(rows[1].ep_used); // row 1: honored normally

    rows[1].ep_used = false;
    const uint8_t set_row1[1] = {0x01};
    REQUIRE_FALSE(ep_generic_cfg::apply_reconfig(rows, 2, 13, set_row1, 1));
    REQUIRE(rows[1].ep_used); // setting works normally too, not just clearing
}

TEST_CASE("ep_generic_cfg::apply_reconfig leaves a partially-covered multi-octet field unchanged",
          "[regmap][REQ-RMAP-079]") {
    EndpointGenericConfig row;
    row.ep_req_storage_size = 40; // pre-existing value
    const uint8_t patch[1] = {0x99}; // only byte 0 of the 2-byte field at relative 0x0002-0x0003

    REQUIRE_FALSE(ep_generic_cfg::apply_reconfig(&row, 1, 2, patch, sizeof(patch)));
    REQUIRE(row.ep_req_storage_size == 40); // unchanged -- not corrupted by a half-write
}

TEST_CASE("ep_generic_cfg::apply_reconfig rejects a zero-length write and an out-of-range write",
          "[regmap][REQ-RMAP-079]") {
    EndpointGenericConfig row;
    const uint8_t patch[1] = {0x00};

    REQUIRE(ep_generic_cfg::apply_reconfig(&row, 1, 0, patch, 0) ==
            make_error_code(EpGenericCfgReconfigErrc::short_write));
    REQUIRE(ep_generic_cfg::apply_reconfig(&row, 1, 12, patch, 1) ==
            make_error_code(EpGenericCfgReconfigErrc::out_of_range));
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

    // The two-element positional init above still compiles and zero-defaults
    // the two fields batch B appended (REQ-RMAP-052/053) — this is the exact
    // compatibility batch B's own EpIdMappingEntry doc comment relies on.
    REQUIRE(m.ep_id_mapping[0].request_stream_index == 0);
    REQUIRE_FALSE(m.ep_id_mapping[0].crc_required);
}

// ── Response / ack queue config (TC18 §12.7.9 Table 27) ──────────────────────────

TEST_CASE("ResponseQueueConfig fields exist and are settable", "[regmap][REQ-REGMAP-011]") {
    ResponseQueueConfig rqc;
    rqc.queue_size     = 6;
    rqc.flush_on_count = 6;
    REQUIRE(rqc.queue_size == 6);
    REQUIRE(rqc.flush_on_count == 6);
}

// ── Sequencer-state persistence (batch B, pre-existing) ──────────────────────────

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

// ── Size-invariant enforcement (cpp-RCP-N2-01 / cpp-RCP-N2-02, issues #64/#65) ──
// Ep0's access checks (check_read_access/check_write_access/set_endpoint_owner)
// all bound a target endpoint against endpoint_owner_, which is sized from
// general.svr_ep_count. The actual writes land in regs_.generic_configs /
// regs_.functional_configs instead. These tests confirm a RegisterMap whose
// general.svr_ep_count disagrees with those vectors' lengths is rejected/
// fails closed rather than letting an in-range access check be followed by
// an out-of-bounds write.

TEST_CASE("A RegisterMap whose config vectors disagree with svr_ep_count fails closed instead of allowing OOB access",
          "[regmap][REQ-REGMAP-013]") {
    RegisterMap m;
    m.general.svr_ep_count = 4;
    // generic_configs / functional_configs deliberately left empty/default
    // -- exactly the mismatch issue #64 describes.
    ServerLifecycle lc;
    Ep0 ep0(m, lc);

    REQUIRE_FALSE(ep0.is_endpoint_table_consistent());

    REQUIRE_FALSE(ep0.claim_root_client(1));

    // Every non-EP0 access check must fail closed (INVALID_PARAMETER), not
    // report a target in [1, svr_ep_count] as valid.
    auto ec_owner = ep0.set_endpoint_owner(1, 1);
    REQUIRE(ec_owner);
    REQUIRE(ec_owner == make_error_code(RegMapErrc::invalid_parameter));

    auto ec_read = ep0.check_read_access(1);
    REQUIRE(ec_read);
    REQUIRE(ec_read == make_error_code(RegMapErrc::invalid_parameter));

    EndpointGenericConfig gcfg;
    auto ec_gwrite = ep0.write_generic_config(1, /*target=*/1, gcfg);
    REQUIRE(ec_gwrite);
    REQUIRE(ec_gwrite == make_error_code(RegMapErrc::invalid_parameter));

    EndpointFunctionalConfig fcfg;
    auto ec_fwrite = ep0.write_functional_config(1, /*target=*/1, fcfg);
    REQUIRE(ec_fwrite);
    REQUIRE(ec_fwrite == make_error_code(RegMapErrc::invalid_parameter));

    // EP0 (whole-map) read/write are unaffected -- the invariant only
    // gates per-endpoint access.
    REQUIRE_FALSE(ep0.check_read_access(kEp0));
}

TEST_CASE("A RegisterMap with a smaller mismatch (nonzero but undersized config vectors) also fails closed",
          "[regmap][REQ-REGMAP-013]") {
    RegisterMap m;
    m.general.svr_ep_count = 4;
    m.generic_configs.resize(1);
    m.functional_configs.resize(1);
    ServerLifecycle lc;
    Ep0 ep0(m, lc);

    REQUIRE_FALSE(ep0.is_endpoint_table_consistent());
    REQUIRE_FALSE(ep0.claim_root_client(1));

    EndpointGenericConfig cfg;
    // target=4 would be in-range against svr_ep_count (and against the
    // pre-fix endpoint_owner_.size()==4), but generic_configs only has 1
    // slot -- this must not be allowed through.
    auto ec = ep0.write_generic_config(1, /*target=*/4, cfg);
    REQUIRE(ec);
    REQUIRE(ec == make_error_code(RegMapErrc::invalid_parameter));
}

TEST_CASE("write_whole_map rejects a replacement map whose config vectors disagree with its own svr_ep_count",
          "[regmap][REQ-REGMAP-013]") {
    auto map = make_map(2);
    ServerLifecycle lc;
    Ep0 ep0(map, lc);
    REQUIRE_FALSE(ep0.claim_root_client(1));

    RegisterMap bad;
    bad.general.svr_ep_count = 3;
    bad.generic_configs.resize(1);      // mismatched
    bad.functional_configs.resize(3);

    auto ec = ep0.write_whole_map(1, bad);
    REQUIRE(ec);
    REQUIRE(ec == make_error_code(RegMapErrc::invalid_parameter));

    // Rejected write must leave the live map untouched.
    REQUIRE(ep0.read_whole_map().general.svr_ep_count == 2);
    REQUIRE(ep0.is_endpoint_table_consistent());
}

TEST_CASE("write_whole_map with a valid replacement map resizes endpoint_owner_ to match the new svr_ep_count",
          "[regmap][REQ-REGMAP-005]") {
    auto map = make_map(2);
    ServerLifecycle lc;
    Ep0 ep0(map, lc);
    REQUIRE_FALSE(ep0.set_endpoint_owner(1, 10));
    REQUIRE_FALSE(ep0.set_endpoint_owner(2, 20));
    REQUIRE_FALSE(ep0.claim_root_client(1));

    // Grow: install a 4-endpoint replacement. Before the fix, endpoint_owner_
    // stayed sized at 2, so endpoint 4 could never get an owner (silent
    // DoS) even though write_generic_config's own bounds check would have
    // been the thing standing between a client and an OOB write.
    auto bigger = make_map(4);
    REQUIRE_FALSE(ep0.write_whole_map(1, bigger));
    REQUIRE(ep0.is_endpoint_table_consistent());

    // Prior per-endpoint owner assignments are discarded by the whole-map
    // replacement -- they described endpoints in the old map, not
    // necessarily the same endpoints in the new one.
    EndpointGenericConfig cfg;
    auto ec_stale_owner = ep0.write_generic_config(/*client=*/10, /*target=*/1, cfg);
    REQUIRE(ec_stale_owner);
    REQUIRE(ec_stale_owner == make_error_code(RegMapErrc::unauthorized_access));

    // The root client can write any endpoint in the new, larger range,
    // including endpoint 4, which did not exist under the old map.
    cfg.ep_tx_buffer_size = 99;
    REQUIRE_FALSE(ep0.write_generic_config(1, /*target=*/4, cfg));
    REQUIRE(ep0.read_whole_map().generic_configs[3].ep_tx_buffer_size == cfg.ep_tx_buffer_size);

    // Shrink: install a 1-endpoint replacement. endpoint 4 must no longer
    // be accepted as a write target -- this is exactly the stale-bounds
    // scenario issue #65 describes (endpoint_owner_ too large for the new,
    // shorter config vectors) if endpoint_owner_ were not resized down too.
    auto smaller = make_map(1);
    REQUIRE_FALSE(ep0.write_whole_map(1, smaller));
    REQUIRE(ep0.is_endpoint_table_consistent());

    auto ec_oob = ep0.write_generic_config(1, /*target=*/4, cfg);
    REQUIRE(ec_oob);
    REQUIRE(ec_oob == make_error_code(RegMapErrc::invalid_parameter));
}

// ── Error taxonomies are distinct ────────────────────────────────────────────────

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

TEST_CASE("GeneralMapErrc values are distinct and carry non-empty messages", "[regmap][REQ-RMAP-024]") {
    auto short_frame  = make_error_code(GeneralMapErrc::short_frame);
    auto bad_msg_type = make_error_code(GeneralMapErrc::bad_msg_type);
    auto wrong_bus    = make_error_code(GeneralMapErrc::wrong_bus);
    auto wrong_op     = make_error_code(GeneralMapErrc::wrong_op);

    REQUIRE(short_frame.category() == general_map_category());
    REQUIRE(short_frame != bad_msg_type);
    REQUIRE(bad_msg_type != wrong_bus);
    REQUIRE(wrong_bus != wrong_op);
    REQUIRE_FALSE(short_frame.message().empty());
    REQUIRE_FALSE(wrong_op.message().empty());
}

TEST_CASE("EpGenericCfgReconfigErrc values are distinct and carry non-empty messages", "[regmap][REQ-RMAP-079]") {
    auto short_write  = make_error_code(EpGenericCfgReconfigErrc::short_write);
    auto out_of_range = make_error_code(EpGenericCfgReconfigErrc::out_of_range);

    REQUIRE(short_write.category() == ep_generic_cfg_reconfig_category());
    REQUIRE(short_write != out_of_range);
    REQUIRE_FALSE(short_write.message().empty());
    REQUIRE_FALSE(out_of_range.message().empty());
}

// ═══════════════════════════════════════════════════════════════════════════
// Phase 4 batch B (cpp-RCP issue #129, ROADMAP.md "Phase 17") — ported from
// c-RCP's tests/test_regmap.c and the relevant slice of
// tests/test_tc18_gaps_regmap.c:
// fusa:test REQ-RMAP-017
// fusa:test REQ-RMAP-040
// fusa:test REQ-RMAP-041
// fusa:test REQ-RMAP-042
// fusa:test REQ-RMAP-044
// fusa:test REQ-RMAP-045
// fusa:test REQ-RMAP-047
// fusa:test REQ-RMAP-048
// fusa:test REQ-RMAP-049
// fusa:test REQ-RMAP-050
// fusa:test REQ-RMAP-051
// fusa:test REQ-RMAP-052
// fusa:test REQ-RMAP-053
// fusa:test REQ-RMAP-054
// fusa:test REQ-RMAP-056
// fusa:test REQ-RMAP-057
// fusa:test REQ-RMAP-058
// fusa:test REQ-RMAP-060
// fusa:test REQ-RMAP-061
// fusa:test REQ-RMAP-063
// fusa:test REQ-RMAP-065
// fusa:test REQ-RMAP-071
// fusa:test REQ-RMAP-083
// fusa:test REQ-RMAP-084
// fusa:test REQ-WAKEUP-020
// fusa:test REQ-E2E-029
// fusa:test REQ-E2E-030
// fusa:test REQ-E2E-045
// fusa:test REQ-E2E-046
// fusa:test REQ-LIFECYCLE-025
// fusa:test REQ-LIFECYCLE-031
// ═══════════════════════════════════════════════════════════════════════════

// ── EpFunctionalCfg (content-modeling only) ──────────────────────────────────

TEST_CASE("EpFunctionalCfg default-constructs zeroed and is independently settable",
          "[regmap][REQ-RMAP-017]") {
    EpFunctionalCfg cfg;
    REQUIRE_FALSE(cfg.ep_enable);
    REQUIRE_FALSE(cfg.ep_clear_req_storage);
    REQUIRE_FALSE(cfg.ep_req_crc_enable);
    REQUIRE_FALSE(cfg.ep_response_ts_enable);
    REQUIRE_FALSE(cfg.ep_suppress_response);

    cfg.ep_enable         = true;
    cfg.ep_req_crc_enable = true;
    REQUIRE(cfg.ep_enable);
    REQUIRE(cfg.ep_req_crc_enable);
    REQUIRE_FALSE(cfg.ep_clear_req_storage);
}

// ── HW pin mapping (TC18 §12.7.6 Tables 21/22) ───────────────────────────────

TEST_CASE("HwPinMapEntry defaults to all-zero and matches c-RCP's row shape", "[regmap][REQ-RMAP-042]") {
    HwPinMapEntry e;
    REQUIRE(e.hw_ep_nr == 0);
    REQUIRE(e.hw_ep_pin_nr == 0);
    REQUIRE(e.hw_pin_type == 0);
}

TEST_CASE("hw_pin bit-layout constants are non-overlapping within their own sub-field",
          "[regmap][REQ-RMAP-042]") {
    REQUIRE((hw_pin::kPullMask & hw_pin::kStageMask) == 0);
    REQUIRE((hw_pin::kStageMask & hw_pin::kDriveMask) == 0);
    REQUIRE((hw_pin::kDriveMask & hw_pin::kSchmittTrigger) == 0);
    REQUIRE(hw_pin::kStageInput == 0);
    REQUIRE(hw_pin::kStagePushPull == hw_pin::kStageMask);
}

TEST_CASE("hw_pin_map::render places each row at its own 3-octet stride", "[regmap][REQ-RMAP-040]") {
    HwPinMapEntry rows[2];
    rows[0].hw_ep_nr     = 1;
    rows[0].hw_ep_pin_nr = 2;
    rows[0].hw_pin_type  = hw_pin::kStagePushPull | hw_pin::kPullUp;
    rows[1].hw_ep_nr     = 3;
    rows[1].hw_ep_pin_nr = 4;
    rows[1].hw_pin_type  = hw_pin::kSchmittTrigger;

    uint8_t out[6];
    hw_pin_map::render(rows, 2, out);
    REQUIRE(out[0] == 1);
    REQUIRE(out[1] == 2);
    REQUIRE(out[2] == (hw_pin::kStagePushPull | hw_pin::kPullUp));
    REQUIRE(out[3] == 3);
    REQUIRE(out[4] == 4);
    REQUIRE(out[5] == hw_pin::kSchmittTrigger);
}

TEST_CASE("hw_pin_map::apply_reconfig patches only the addressed octets", "[regmap][REQ-RMAP-041]") {
    HwPinMapEntry rows[2];
    rows[0].hw_ep_nr = 9; // must survive if not addressed

    const uint8_t patch[3] = {1, 2, hw_pin::kPullDown};
    auto ec = hw_pin_map::apply_reconfig(rows, 2, 3, patch, sizeof(patch));
    REQUIRE_FALSE(ec);
    REQUIRE(rows[0].hw_ep_nr == 9); // row 0 untouched
    REQUIRE(rows[1].hw_ep_nr == 1);
    REQUIRE(rows[1].hw_ep_pin_nr == 2);
    REQUIRE(rows[1].hw_pin_type == hw_pin::kPullDown);
}

TEST_CASE("hw_pin_map::apply_reconfig rejects a zero-length write and an out-of-range write",
          "[regmap][REQ-RMAP-041]") {
    HwPinMapEntry row;
    const uint8_t patch[1] = {0};

    REQUIRE(hw_pin_map::apply_reconfig(&row, 1, 0, patch, 0) ==
            make_error_code(HwPinMapReconfigErrc::short_write));
    REQUIRE(hw_pin_map::apply_reconfig(&row, 1, 3, patch, 1) ==
            make_error_code(HwPinMapReconfigErrc::out_of_range));
}

TEST_CASE("HwPinMapReconfigErrc values are distinct and carry non-empty messages", "[regmap][REQ-RMAP-041]") {
    auto short_write  = make_error_code(HwPinMapReconfigErrc::short_write);
    auto out_of_range = make_error_code(HwPinMapReconfigErrc::out_of_range);
    REQUIRE(short_write.category() == hw_pin_map_reconfig_category());
    REQUIRE(short_write != out_of_range);
    REQUIRE_FALSE(short_write.message().empty());
    REQUIRE_FALSE(out_of_range.message().empty());
}

// ── Per-endpoint-type named-signal index (TC18 §12.7.6 Table 23) ────────────

TEST_CASE("named_signal_string never returns an empty string for a valid signal",
          "[regmap][REQ-RMAP-044]") {
    for (uint8_t i = 0; i < static_cast<uint8_t>(NamedSignal::Count); ++i) {
        auto sig = static_cast<NamedSignal>(i);
        REQUIRE_FALSE(std::string(named_signal_string(sig)).empty());
    }
}

TEST_CASE("named_signal_string returns \"unknown\" for an out-of-range value", "[regmap][REQ-RMAP-044]") {
    REQUIRE(std::string(named_signal_string(NamedSignal::Count)) == "unknown");
    REQUIRE(std::string(named_signal_string(static_cast<NamedSignal>(0xFF))) == "unknown");
}

TEST_CASE("named_signal_string names are unique across the whole index", "[regmap][REQ-RMAP-044]") {
    std::vector<std::string> names;
    for (uint8_t i = 0; i < static_cast<uint8_t>(NamedSignal::Count); ++i) {
        names.emplace_back(named_signal_string(static_cast<NamedSignal>(i)));
    }
    for (size_t i = 0; i < names.size(); ++i) {
        for (size_t j = i + 1; j < names.size(); ++j) {
            REQUIRE(names[i] != names[j]);
        }
    }
}

TEST_CASE("named_signal_ep_signal_nr restarts at 0 for each endpoint type", "[regmap][REQ-RMAP-045]") {
    REQUIRE(named_signal_ep_signal_nr(NamedSignal::Gpio0) == 0);
    REQUIRE(named_signal_ep_signal_nr(NamedSignal::Gpio31) == 31);
    REQUIRE(named_signal_ep_signal_nr(NamedSignal::SpiClk) == 0);
    REQUIRE(named_signal_ep_signal_nr(NamedSignal::SpiCs5) == 8);
    REQUIRE(named_signal_ep_signal_nr(NamedSignal::I2cScl) == 0);
    REQUIRE(named_signal_ep_signal_nr(NamedSignal::I2cSda) == 1);
    REQUIRE(named_signal_ep_signal_nr(NamedSignal::UartTx) == 0);
    REQUIRE(named_signal_ep_signal_nr(NamedSignal::UartCts) == 3);
    REQUIRE(named_signal_ep_signal_nr(NamedSignal::LinTxd) == 0);
    REQUIRE(named_signal_ep_signal_nr(NamedSignal::PwmOut) == 0);
    REQUIRE(named_signal_ep_signal_nr(NamedSignal::PwmOutn) == 1);
    REQUIRE(named_signal_ep_signal_nr(NamedSignal::PwmIn) == 0);
    REQUIRE(named_signal_ep_signal_nr(NamedSignal::AdcIn) == 0);
    REQUIRE(named_signal_ep_signal_nr(NamedSignal::DacOut) == 0);
    // TC18's own counter-intuitive order: RXD=0, TXD=1.
    REQUIRE(named_signal_ep_signal_nr(NamedSignal::CanRxd) == 0);
    REQUIRE(named_signal_ep_signal_nr(NamedSignal::CanTxd) == 1);
    REQUIRE(named_signal_ep_signal_nr(NamedSignal::IseledIspP) == 0);
    REQUIRE(named_signal_ep_signal_nr(NamedSignal::IseledIspN) == 1);
    REQUIRE(named_signal_ep_signal_nr(NamedSignal::MdioMdc) == 0);
    REQUIRE(named_signal_ep_signal_nr(NamedSignal::MdioData) == 1);
}

TEST_CASE("named_signal_ep_signal_nr returns 0 for Count or any other invalid value",
          "[regmap][REQ-RMAP-045]") {
    REQUIRE(named_signal_ep_signal_nr(NamedSignal::Count) == 0);
    REQUIRE(named_signal_ep_signal_nr(static_cast<NamedSignal>(0xFF)) == 0);
}

// ── Request-stream config: appended fields, boundary conversions ────────────

TEST_CASE("RequestStreamConfig's batch-B-appended fields default per TC18's own power-on rule",
          "[regmap][REQ-RMAP-047]") {
    RequestStreamConfig cfg;
    REQUIRE(cfg.rx_secure_channel_index == 0);
    REQUIRE(cfg.rx_ack_stream_index == 0);
    REQUIRE(cfg.rx_resp_stream_index == 1); // REQ-RMAP-049: power-on default is 1, not 0
    REQUIRE(cfg.rx_stream_max_request_size == 0);

    // Pre-existing fields batch A already established are untouched.
    REQUIRE_FALSE(cfg.rx_wd_enable);
    REQUIRE(cfg.rx_safety_measure == RxSafetyMeasure::ForceHighImpedance);
}

TEST_CASE("request_stream_cfg::wd_timeout_ms_to_ticks rounds down and bounds-checks",
          "[regmap][REQ-RMAP-050]") {
    uint16_t ticks = 0xFFFF;
    REQUIRE(request_stream_cfg::wd_timeout_ms_to_ticks(1000, 10, ticks));
    REQUIRE(ticks == 100);

    // Rounds down: 105ms / 10ms-per-tick = 10.5 -> 10 ticks, never 11.
    REQUIRE(request_stream_cfg::wd_timeout_ms_to_ticks(105, 10, ticks));
    REQUIRE(ticks == 10);

    REQUIRE_FALSE(request_stream_cfg::wd_timeout_ms_to_ticks(1000, 0, ticks)); // no zero-length tick
    REQUIRE(ticks == 10); // untouched on rejection

    REQUIRE_FALSE(request_stream_cfg::wd_timeout_ms_to_ticks(0xFFFFFFFFu, 1, ticks)); // overflows 16 bit
}

TEST_CASE("request_stream_cfg::wd_timeout_ticks_to_ms round-trips and rejects a zero rate",
          "[regmap][REQ-RMAP-050]") {
    uint32_t ms = 0xFFFFFFFFu;
    REQUIRE(request_stream_cfg::wd_timeout_ticks_to_ms(100, 10, ms));
    REQUIRE(ms == 1000);

    REQUIRE_FALSE(request_stream_cfg::wd_timeout_ticks_to_ms(100, 0, ms));
    REQUIRE(ms == 1000); // untouched on rejection
}

// ── request-stream-cfg wire codec (issue #306/#458) ──────────────────────────

TEST_CASE("request_stream_cfg::render places stream_id, secure channel, and ack/resp indices",
          "[regmap][REQ-RMAP-047]") {
    RequestStreamConfig row;
    row.stream_id.mac    = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    row.stream_id.suffix = 0x0102;
    row.rx_secure_channel_index = 5;
    row.rx_ack_stream_index     = 2;
    row.rx_resp_stream_index    = 3;

    uint8_t out[24];
    request_stream_cfg::render(&row, 1, out, /*watchdog_ms_per_tick=*/0, nullptr);

    REQUIRE(avtp::detail::get_u64(&out[0x0000]) == row.stream_id.to_u64());
    REQUIRE(out[0x000C] == 5);
    REQUIRE(out[0x0010] == 2);
    REQUIRE(out[0x0011] == 3);
}

TEST_CASE("request_stream_cfg::render saturates an oversized rx_stream_max_request_size",
          "[regmap][REQ-RMAP-071]") {
    RequestStreamConfig row;
    row.rx_stream_max_request_size = 0x1FFFF; // one past 16-bit max

    uint8_t out[24];
    request_stream_cfg::render(&row, 1, out, 0, nullptr);
    REQUIRE(avtp::detail::get_u16(&out[0x0008]) == 0xFFFF);
}

TEST_CASE("request_stream_cfg::render saturates an oversized rx_safestate_sequencer",
          "[regmap][REQ-RMAP-047]") {
    RequestStreamConfig row;
    row.rx_safestate_sequencer = 0x1FF; // one past 8-bit max

    uint8_t out[24];
    request_stream_cfg::render(&row, 1, out, 0, nullptr);
    REQUIRE(out[0x000E] == 0xFF);
}

TEST_CASE("request_stream_cfg::render couples the sequence/watchdog bits with AND, not OR",
          "[regmap][REQ-RMAP-051]") {
    uint8_t out[24];

    // Only the "block" dimension set -- must NOT render as if safe-state
    // were also entered (that would overstate a safety guarantee).
    RequestStreamConfig only_block;
    only_block.rx_enforce_seq          = true;
    only_block.rx_seq_safestate_enable = false;
    request_stream_cfg::render(&only_block, 1, out, 0, nullptr);
    REQUIRE((out[0x000D] & 0x02u) == 0x00u);

    RequestStreamConfig both;
    both.rx_enforce_seq          = true;
    both.rx_seq_safestate_enable = true;
    request_stream_cfg::render(&both, 1, out, 0, nullptr);
    REQUIRE((out[0x000D] & 0x02u) == 0x02u);

    RequestStreamConfig wd_only_block;
    wd_only_block.rx_wd_enable           = true;
    wd_only_block.rx_wd_safestate_enable = false;
    request_stream_cfg::render(&wd_only_block, 1, out, 0, nullptr);
    REQUIRE((out[0x000D] & 0x04u) == 0x00u);

    RequestStreamConfig wd_both;
    wd_both.rx_wd_enable           = true;
    wd_both.rx_wd_safestate_enable = true;
    request_stream_cfg::render(&wd_both, 1, out, 0, nullptr);
    REQUIRE((out[0x000D] & 0x04u) == 0x04u);
}

TEST_CASE("request_stream_cfg::render packs rx_enforce_e2e and rx_ovrflw_safestate_enable directly",
          "[regmap][REQ-RMAP-051]") {
    RequestStreamConfig row;
    row.rx_enforce_e2e            = true;
    row.rx_ovrflw_safestate_enable = true;

    uint8_t out[24];
    request_stream_cfg::render(&row, 1, out, 0, nullptr);
    REQUIRE((out[0x000D] & 0x01u) != 0);
    REQUIRE((out[0x000D] & 0x08u) != 0);
    REQUIRE((out[0x000D] & 0x70u) == 0); // bits [6:4] reserved, always 0
}

TEST_CASE("request_stream_cfg::render wires rx_stream_status from the live-status array",
          "[regmap][REQ-E2E-046]") {
    RequestStreamConfig rows[2];
    const bool blocked[2] = {false, true};

    uint8_t out[48];
    request_stream_cfg::render(rows, 2, out, 0, blocked);
    REQUIRE((out[0x000D] & 0x80u) == 0x00u);
    REQUIRE((out[24 + 0x000D] & 0x80u) == 0x80u);

    // nullptr means "no live status known" -- bit 7 renders 0 for every row.
    request_stream_cfg::render(rows, 2, out, 0, nullptr);
    REQUIRE((out[0x000D] & 0x80u) == 0x00u);
    REQUIRE((out[24 + 0x000D] & 0x80u) == 0x00u);
}

TEST_CASE("request_stream_cfg::render falls back to 0x0000 when the watchdog tick rate is unconfigured",
          "[regmap][REQ-RMAP-050]") {
    RequestStreamConfig row;
    row.rx_wd_timeout_interval = 1000;

    uint8_t out[24];
    request_stream_cfg::render(&row, 1, out, /*watchdog_ms_per_tick=*/0, nullptr);
    REQUIRE(out[0x000A] == 0x00);
    REQUIRE(out[0x000B] == 0x00);

    request_stream_cfg::render(&row, 1, out, /*watchdog_ms_per_tick=*/10, nullptr);
    REQUIRE(avtp::detail::get_u16(&out[0x000A]) == 100);
}

TEST_CASE("request_stream_cfg::apply_reconfig round-trips stream_id and the coupled bit pairs",
          "[regmap][REQ-RMAP-047]") {
    RequestStreamConfig row;
    row.stream_id.mac    = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    row.stream_id.suffix = 0x1234;
    row.rx_wd_timeout_interval = 500;

    uint8_t block[24];
    request_stream_cfg::render(&row, 1, block, /*watchdog_ms_per_tick=*/10, nullptr);
    block[0x000D] = 0x0F; // set all 4 real content bits

    RequestStreamConfig out_row;
    auto ec = request_stream_cfg::apply_reconfig(&out_row, 1, 0, block, sizeof(block),
                                                  /*watchdog_ms_per_tick=*/10);
    REQUIRE_FALSE(ec);
    REQUIRE(out_row.stream_id == row.stream_id);
    REQUIRE(out_row.rx_wd_timeout_interval == 500);
    REQUIRE(out_row.rx_enforce_e2e);
    REQUIRE(out_row.rx_enforce_seq);
    REQUIRE(out_row.rx_seq_safestate_enable); // both dimensions set together from one bit
    REQUIRE(out_row.rx_wd_enable);
    REQUIRE(out_row.rx_wd_safestate_enable);
    REQUIRE(out_row.rx_ovrflw_safestate_enable);
}

TEST_CASE("request_stream_cfg::apply_reconfig leaves rx_wd_timeout_interval unchanged when the tick rate is unconfigured",
          "[regmap][REQ-RMAP-050]") {
    RequestStreamConfig row;
    row.rx_wd_timeout_interval = 777; // pre-existing value

    uint8_t block[24];
    request_stream_cfg::render(&row, 1, block, 0, nullptr);

    auto ec = request_stream_cfg::apply_reconfig(&row, 1, 0, block, sizeof(block),
                                                  /*watchdog_ms_per_tick=*/0);
    REQUIRE_FALSE(ec);
    REQUIRE(row.rx_wd_timeout_interval == 777); // untouched, not zeroed
}

TEST_CASE("request_stream_cfg::apply_reconfig rejects a zero-length write and an out-of-range write",
          "[regmap][REQ-RMAP-047]") {
    RequestStreamConfig row;
    const uint8_t patch[1] = {0};

    REQUIRE(request_stream_cfg::apply_reconfig(&row, 1, 0, patch, 0, 10) ==
            make_error_code(RequestStreamCfgReconfigErrc::short_write));
    REQUIRE(request_stream_cfg::apply_reconfig(&row, 1, 24, patch, 1, 10) ==
            make_error_code(RequestStreamCfgReconfigErrc::out_of_range));
}

TEST_CASE("RequestStreamCfgReconfigErrc values are distinct and carry non-empty messages",
          "[regmap][REQ-RMAP-047]") {
    auto short_write  = make_error_code(RequestStreamCfgReconfigErrc::short_write);
    auto out_of_range = make_error_code(RequestStreamCfgReconfigErrc::out_of_range);
    REQUIRE(short_write.category() == request_stream_cfg_reconfig_category());
    REQUIRE(short_write != out_of_range);
    REQUIRE_FALSE(short_write.message().empty());
    REQUIRE_FALSE(out_of_range.message().empty());
}

TEST_CASE("request_stream_cfg::resolve_index matches by stream_id and returns a 1-based index",
          "[regmap][REQ-SEQ-013]") {
    RequestStreamConfig rows[2];
    rows[0].stream_id = avtp::StreamId::from_u64(100);
    rows[1].stream_id = avtp::StreamId::from_u64(200);

    REQUIRE(request_stream_cfg::resolve_index(rows, 2, 100) == 1);
    REQUIRE(request_stream_cfg::resolve_index(rows, 2, 200) == 2);
}

TEST_CASE("request_stream_cfg::resolve_index returns the 0 sentinel for no match, null, or empty",
          "[regmap][REQ-SEQ-013]") {
    RequestStreamConfig rows[1];
    rows[0].stream_id = avtp::StreamId::from_u64(100);

    REQUIRE(request_stream_cfg::resolve_index(rows, 1, 999) == 0);
    REQUIRE(request_stream_cfg::resolve_index(nullptr, 0, 100) == 0);
    REQUIRE(request_stream_cfg::resolve_index(rows, 0, 100) == 0);
}

// ── Response / ack queue config wire codec (TC18 §12.7.9 Table 27) ──────────

TEST_CASE("ResponseQueueConfig matches c-RCP's real per-queue row shape", "[regmap][REQ-RMAP-059]") {
    ResponseQueueConfig cfg;
    REQUIRE(cfg.stream_uid == 0);
    REQUIRE(cfg.max_avtpdu_size == 0);
    REQUIRE(cfg.queue_size == 0);
    REQUIRE(cfg.flush_on_count == 0);
    REQUIRE(cfg.flush_time_us == 0);
}

TEST_CASE("response_queue_stream_id combines stream_uid with the interface's own mac",
          "[regmap][REQ-RMAP-060]") {
    ResponseQueueConfig cfg;
    cfg.stream_uid = 0xABCD;
    std::array<uint8_t, 6> mac{0x01, 0x02, 0x03, 0x04, 0x05, 0x06};

    auto id = response_queue_stream_id(cfg, mac);
    REQUIRE(id.mac == mac);
    REQUIRE(id.suffix == 0xABCD);
}

TEST_CASE("response_queue_cfg::render places each field at its own TC18-cited byte offset",
          "[regmap][REQ-RMAP-061]") {
    ResponseQueueConfig cfg;
    cfg.stream_uid      = 0x1122;
    cfg.max_avtpdu_size = 0x3344;
    cfg.queue_size      = 0x5566;
    cfg.flush_on_count  = 0x7788;
    cfg.flush_time_us   = 0x99AA;

    uint8_t out[10];
    response_queue_cfg::render(&cfg, 1, out);
    REQUIRE(avtp::detail::get_u16(&out[0]) == 0x1122);
    REQUIRE(avtp::detail::get_u16(&out[2]) == 0x3344);
    REQUIRE(avtp::detail::get_u16(&out[4]) == 0x5566);
    REQUIRE(avtp::detail::get_u16(&out[6]) == 0x7788);
    REQUIRE(avtp::detail::get_u16(&out[8]) == 0x99AA);
}

TEST_CASE("response_queue_cfg::render saturates an oversized flush_time_us without wrapping",
          "[regmap][REQ-RMAP-065]") {
    ResponseQueueConfig cfg;
    cfg.flush_time_us = 0x10000; // one past the 16-bit wire register's own max

    uint8_t out[10];
    response_queue_cfg::render(&cfg, 1, out);
    REQUIRE(avtp::detail::get_u16(&out[8]) == 0xFFFF); // saturated, not wrapped to 0x0000
}

TEST_CASE("response_queue_cfg::apply_reconfig patches only the addressed octets",
          "[regmap][REQ-RMAP-061]") {
    ResponseQueueConfig rows[2];
    rows[0].stream_uid = 0xAAAA; // must survive if not addressed

    const uint8_t patch[10] = {0x00, 0x01, 0x00, 0x02, 0x00, 0x03, 0x00, 0x04, 0x00, 0x05};
    auto ec = response_queue_cfg::apply_reconfig(rows, 2, 10, patch, sizeof(patch));
    REQUIRE_FALSE(ec);
    REQUIRE(rows[0].stream_uid == 0xAAAA);
    REQUIRE(rows[1].stream_uid == 0x0001);
    REQUIRE(rows[1].max_avtpdu_size == 0x0002);
    REQUIRE(rows[1].queue_size == 0x0003);
    REQUIRE(rows[1].flush_on_count == 0x0004);
    REQUIRE(rows[1].flush_time_us == 0x0005);
}

TEST_CASE("response_queue_cfg::apply_reconfig rejects a zero-length write and an out-of-range write",
          "[regmap][REQ-RMAP-061]") {
    ResponseQueueConfig row;
    const uint8_t patch[1] = {0};

    REQUIRE(response_queue_cfg::apply_reconfig(&row, 1, 0, patch, 0) ==
            make_error_code(ResponseQueueCfgReconfigErrc::short_write));
    REQUIRE(response_queue_cfg::apply_reconfig(&row, 1, 10, patch, 1) ==
            make_error_code(ResponseQueueCfgReconfigErrc::out_of_range));
}

TEST_CASE("ResponseQueueCfgReconfigErrc values are distinct and carry non-empty messages",
          "[regmap][REQ-RMAP-061]") {
    auto short_write  = make_error_code(ResponseQueueCfgReconfigErrc::short_write);
    auto out_of_range = make_error_code(ResponseQueueCfgReconfigErrc::out_of_range);
    REQUIRE(short_write.category() == response_queue_cfg_reconfig_category());
    REQUIRE(short_write != out_of_range);
    REQUIRE_FALSE(short_write.message().empty());
    REQUIRE_FALSE(out_of_range.message().empty());
}

// ── EP-ID / byte_bus_id map: appended fields, diagnostics, wire codec ────────

TEST_CASE("EpIdMappingEntry's appended fields default false/0", "[regmap][REQ-RMAP-052]") {
    EpIdMappingEntry e;
    REQUIRE(e.request_stream_index == 0);
    REQUIRE_FALSE(e.crc_required);
}

TEST_CASE("ep_id_map::is_ascending is true for strictly increasing composite keys",
          "[regmap][REQ-RMAP-056]") {
    EpIdMappingEntry entries[3] = {{1, 10, 1, false}, {2, 20, 1, false}, {3, 5, 2, false}};
    // stream 1: bbid 10 < 20 (ascending); stream 2 > stream 1 (always ascending
    // regardless of its own bbid, even though 5 < 20).
    REQUIRE(ep_id_map::is_ascending(entries, 3));
}

TEST_CASE("ep_id_map::is_ascending is false for an equal or descending byte_bus_id within one stream",
          "[regmap][REQ-RMAP-056]") {
    EpIdMappingEntry equal_adjacent[2] = {{1, 10, 1, false}, {2, 10, 1, false}};
    REQUIRE_FALSE(ep_id_map::is_ascending(equal_adjacent, 2));

    EpIdMappingEntry descending[2] = {{1, 20, 1, false}, {2, 10, 1, false}};
    REQUIRE_FALSE(ep_id_map::is_ascending(descending, 2));
}

TEST_CASE("ep_id_map::is_ascending is false for a decreasing request_stream_index",
          "[regmap][REQ-RMAP-056]") {
    EpIdMappingEntry entries[2] = {{1, 10, 2, false}, {2, 20, 1, false}};
    REQUIRE_FALSE(ep_id_map::is_ascending(entries, 2));
}

TEST_CASE("ep_id_map::is_ascending is vacuously true for zero or one entries", "[regmap][REQ-RMAP-056]") {
    REQUIRE(ep_id_map::is_ascending(nullptr, 0));
    EpIdMappingEntry one[1] = {{1, 10, 1, false}};
    REQUIRE(ep_id_map::is_ascending(one, 1));
}

TEST_CASE("ep_id_map::effective_count stops at the first request_stream_index==0 sentinel",
          "[regmap][REQ-RMAP-054]") {
    EpIdMappingEntry entries[4] = {{1, 1, 1, false}, {2, 2, 1, false}, {0, 0, 0, false}, {4, 4, 1, false}};
    REQUIRE(ep_id_map::effective_count(entries, 4) == 2);
}

TEST_CASE("ep_id_map::effective_count returns capacity unchanged when no sentinel exists",
          "[regmap][REQ-RMAP-054]") {
    EpIdMappingEntry entries[2] = {{1, 1, 1, false}, {2, 2, 1, false}};
    REQUIRE(ep_id_map::effective_count(entries, 2) == 2);
}

TEST_CASE("ep_id_map::row_init_default permits EP0 access before any configuration is written",
          "[regmap][REQ-RMAP-084]") {
    EpIdMappingEntry row;
    row.ep_id                = 99;
    row.request_stream_index = 0;
    ep_id_map::row_init_default(row);
    REQUIRE(row.request_stream_index == 1); // smallest valid index, not the end-of-table sentinel
    REQUIRE(row.ep_id == kEp0);
    REQUIRE(row.byte_bus_id == 0);
}

TEST_CASE("ep_id_map::render packs BBID into bits[15:5] and crc_required into bit 4",
          "[regmap][REQ-RMAP-053]") {
    EpIdMappingEntry row;
    row.request_stream_index = 3;
    row.ep_id                = 7;
    row.byte_bus_id          = 0x0100;
    row.crc_required         = true;

    uint8_t out[4];
    ep_id_map::render(&row, 1, out);
    REQUIRE(out[0] == 3);
    REQUIRE(out[1] == 7);
    const uint16_t bbid_ctrl = avtp::detail::get_u16(&out[2]);
    REQUIRE(((bbid_ctrl >> 5) & 0x07FFu) == 0x0100);
    REQUIRE((bbid_ctrl & 0x10u) != 0);
    REQUIRE((bbid_ctrl & 0x0Fu) == 0); // Channel_selection always 0
}

TEST_CASE("ep_id_map::render and apply_reconfig round-trip BBID/crc_required per Table 25/26",
          "[regmap][REQ-RMAP-053]") {
    EpIdMappingEntry row;
    row.request_stream_index = 2;
    row.ep_id                = 5;
    row.byte_bus_id          = 0x0234;
    row.crc_required         = true;

    uint8_t block[4];
    ep_id_map::render(&row, 1, block);

    EpIdMappingEntry out_row;
    auto ec = ep_id_map::apply_reconfig(&out_row, 1, 0, block, sizeof(block));
    REQUIRE_FALSE(ec);
    REQUIRE(out_row.request_stream_index == 2);
    REQUIRE(out_row.ep_id == 5);
    REQUIRE(out_row.byte_bus_id == 0x0234);
    REQUIRE(out_row.crc_required);
}

TEST_CASE("ep_id_map::apply_reconfig patches only the addressed octets", "[regmap][REQ-RMAP-052]") {
    EpIdMappingEntry rows[2];
    rows[0].ep_id = 42; // must survive if not addressed

    const uint8_t patch[4] = {1, 9, 0x00, 0x00};
    auto ec = ep_id_map::apply_reconfig(rows, 2, 4, patch, sizeof(patch));
    REQUIRE_FALSE(ec);
    REQUIRE(rows[0].ep_id == 42);
    REQUIRE(rows[1].request_stream_index == 1);
    REQUIRE(rows[1].ep_id == 9);
}

TEST_CASE("ep_id_map::apply_reconfig rejects a zero-length write and an out-of-range write",
          "[regmap][REQ-RMAP-052]") {
    EpIdMappingEntry row;
    const uint8_t patch[1] = {0};

    REQUIRE(ep_id_map::apply_reconfig(&row, 1, 0, patch, 0) ==
            make_error_code(EpIdMapReconfigErrc::short_write));
    REQUIRE(ep_id_map::apply_reconfig(&row, 1, 4, patch, 1) ==
            make_error_code(EpIdMapReconfigErrc::out_of_range));
}

TEST_CASE("EpIdMapReconfigErrc values are distinct and carry non-empty messages",
          "[regmap][REQ-RMAP-052]") {
    auto short_write  = make_error_code(EpIdMapReconfigErrc::short_write);
    auto out_of_range = make_error_code(EpIdMapReconfigErrc::out_of_range);
    REQUIRE(short_write.category() == ep_id_map_reconfig_category());
    REQUIRE(short_write != out_of_range);
    REQUIRE_FALSE(short_write.message().empty());
    REQUIRE_FALSE(out_of_range.message().empty());
}

TEST_CASE("ep_id_map::has_single_client_per_ep flags an endpoint mapped under two distinct streams",
          "[regmap][REQ-RMAP-057]") {
    EpIdMappingEntry ok[2] = {{1, 10, 1, false}, {1, 20, 1, false}}; // same ep_id, same stream -- fine
    REQUIRE(ep_id_map::has_single_client_per_ep(ok, 2));

    EpIdMappingEntry multi_client[2] = {{1, 10, 1, false}, {1, 20, 2, false}}; // same ep_id, different stream
    REQUIRE_FALSE(ep_id_map::has_single_client_per_ep(multi_client, 2));
}

TEST_CASE("ep_id_map::shared_bus_homogeneous flags a shared bus with differing ep_types",
          "[regmap][REQ-RMAP-058]") {
    EpIdMappingEntry entries[2] = {{1, 10, 1, false}, {2, 10, 1, false}}; // shared (stream,bbid)
    const uint8_t    same_type[2] = {3, 3};
    REQUIRE(ep_id_map::shared_bus_homogeneous(entries, same_type, 2));

    const uint8_t different_type[2] = {3, 4};
    REQUIRE_FALSE(ep_id_map::shared_bus_homogeneous(entries, different_type, 2));
}

TEST_CASE("ep_id_map::ep_type_has_fixed_ep_id checks every row of the target type",
          "[regmap][REQ-WAKEUP-020]") {
    EpIdMappingEntry fixed[1] = {{1, 10, 1, false}};
    const uint8_t    fixed_types[1] = {7};
    REQUIRE(ep_id_map::ep_type_has_fixed_ep_id(fixed, fixed_types, 1, 7, 1));

    EpIdMappingEntry wrong[1] = {{2, 10, 1, false}};
    const uint8_t    wrong_types[1] = {7};
    REQUIRE_FALSE(ep_id_map::ep_type_has_fixed_ep_id(wrong, wrong_types, 1, 7, 1));

    // Vacuously true when no row of that ep_type exists.
    const uint8_t none_of_type[1] = {9};
    REQUIRE(ep_id_map::ep_type_has_fixed_ep_id(wrong, none_of_type, 1, 7, 1));
}

TEST_CASE("ep_id_map::byte_bus_ids_for_stream reports each distinct byte_bus_id once",
          "[regmap][REQ-E2E-029]") {
    EpIdMappingEntry entries[3] = {{1, 0x100, 5, false}, {2, 0x100, 5, false}, {3, 0x200, 5, false}};
    avtp::ByteBusId  out[4]     = {};

    auto found = ep_id_map::byte_bus_ids_for_stream(entries, 3, /*request_stream_index=*/5, out, 4);
    REQUIRE(found == 2); // 0x100 (twice, deduplicated) and 0x200
    REQUIRE(out[0] == 0x100);
    REQUIRE(out[1] == 0x200);
}

TEST_CASE("ep_id_map::byte_bus_ids_for_stream reports the total count even past out_capacity",
          "[regmap][REQ-E2E-030]") {
    EpIdMappingEntry entries[2] = {{1, 0x100, 5, false}, {2, 0x200, 5, false}};
    avtp::ByteBusId  out[1]     = {};

    auto found = ep_id_map::byte_bus_ids_for_stream(entries, 2, 5, out, /*out_capacity=*/1);
    REQUIRE(found == 2); // total, not just what fit
    REQUIRE(out[0] == 0x100);
}

TEST_CASE("ep_id_map::byte_bus_ids_for_stream ignores rows on a different request stream",
          "[regmap][REQ-E2E-045]") {
    EpIdMappingEntry entries[2] = {{1, 0x100, 5, false}, {2, 0x200, 6, false}};
    avtp::ByteBusId  out[4]     = {};

    auto found = ep_id_map::byte_bus_ids_for_stream(entries, 2, 5, out, 4);
    REQUIRE(found == 1);
    REQUIRE(out[0] == 0x100);
}

TEST_CASE("ep_id_map::is_valid_association matches an exact (stream, byte_bus_id) pair",
          "[regmap][REQ-LIFECYCLE-025]") {
    EpIdMappingEntry entries[1] = {{1, 0x100, 2, false}};
    REQUIRE(ep_id_map::is_valid_association(entries, 1, 2, 0x100));
    REQUIRE_FALSE(ep_id_map::is_valid_association(entries, 1, 2, 0x200)); // wrong bbid
    REQUIRE_FALSE(ep_id_map::is_valid_association(entries, 1, 3, 0x100)); // wrong stream
    REQUIRE_FALSE(ep_id_map::is_valid_association(nullptr, 0, 2, 0x100)); // empty table
}

// ── Optional-subsystem config sections (REQ-RMAP-039) ────────────────────────

TEST_CASE("OptionalSubsystemCfg default-constructs with len == 0, meaning \"not supported\"",
          "[regmap][REQ-RMAP-039]") {
    OptionalSubsystemCfg cfg;
    REQUIRE(cfg.len == 0);
}

TEST_CASE("optional_subsystem_cfg::apply_reconfig writes within the section's own current extent",
          "[regmap][REQ-RMAP-039]") {
    OptionalSubsystemCfg cfg;
    cfg.len = 4;
    const uint8_t patch[2] = {0xAA, 0xBB};

    auto ec = optional_subsystem_cfg::apply_reconfig(cfg, 1, patch, sizeof(patch));
    REQUIRE_FALSE(ec);
    REQUIRE(cfg.data[0] == 0x00);
    REQUIRE(cfg.data[1] == 0xAA);
    REQUIRE(cfg.data[2] == 0xBB);
    REQUIRE(cfg.data[3] == 0x00);
}

TEST_CASE("optional_subsystem_cfg::apply_reconfig rejects a zero-length write and an out-of-range write",
          "[regmap][REQ-RMAP-039]") {
    OptionalSubsystemCfg cfg;
    cfg.len = 2;
    const uint8_t patch[1] = {0};

    REQUIRE(optional_subsystem_cfg::apply_reconfig(cfg, 0, patch, 0) ==
            make_error_code(OptionalSubsystemCfgReconfigErrc::short_write));
    REQUIRE(optional_subsystem_cfg::apply_reconfig(cfg, 2, patch, 1) ==
            make_error_code(OptionalSubsystemCfgReconfigErrc::out_of_range));
}

TEST_CASE("optional_subsystem_cfg::apply_reconfig rejects a write past the section's own current"
          " extent even though kMaxOctets has room",
          "[regmap][REQ-RMAP-039]") {
    OptionalSubsystemCfg cfg; // cfg.len == 0 -- "not supported"
    const uint8_t patch[1] = {0xFF};
    REQUIRE(optional_subsystem_cfg::apply_reconfig(cfg, 0, patch, 1) ==
            make_error_code(OptionalSubsystemCfgReconfigErrc::out_of_range));
}

TEST_CASE("OptionalSubsystemCfgReconfigErrc values are distinct and carry non-empty messages",
          "[regmap][REQ-RMAP-039]") {
    auto short_write  = make_error_code(OptionalSubsystemCfgReconfigErrc::short_write);
    auto out_of_range = make_error_code(OptionalSubsystemCfgReconfigErrc::out_of_range);
    REQUIRE(short_write.category() == optional_subsystem_cfg_reconfig_category());
    REQUIRE(short_write != out_of_range);
    REQUIRE_FALSE(short_write.message().empty());
    REQUIRE_FALSE(out_of_range.message().empty());
}

TEST_CASE("RegisterMap carries all four optional-subsystem sections, each defaulting to \"not supported\"",
          "[regmap][REQ-RMAP-039]") {
    RegisterMap m;
    REQUIRE(m.network_interface_cfg.len == 0);
    REQUIRE(m.physical_layer_cfg.len == 0);
    REQUIRE(m.time_synch_cfg.len == 0);
    REQUIRE(m.security_cfg.len == 0);
}
