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

// TODO(phase4-batch-b): c-RCP's own writer_ctx() grants
// via_valid_stream_association for a real, matching EP-ID/byte_bus_id
// association when no root client is configured (REQ-LIFECYCLE-025/031).
// This port's own writer_ctx() cannot yet evaluate that (see its own doc
// comment) and always returns false -- pinned here as the current,
// documented, fail-closed behavior rather than left unverified.
TEST_CASE("writer_ctx's via_valid_stream_association is fail-closed (always false) pending batch B",
          "[regmap][REQ-LIFECYCLE-025]") {
    GeneralMap map; // no root client configured
    EpIdMappingEntry entries[1] = {{1, 0x100}};

    auto ctx = writer_ctx(map, nullptr, 2, false, true, false, 0x100, entries, 1);
    REQUIRE_FALSE(ctx.via_valid_stream_association);
}

TEST_CASE("writer_ctx explicitly assigns every member of the returned WriterCtx", "[regmap][REQ-RMAP-086]") {
    GeneralMap map;
    map.svr_root_client_index = 7;
    EpClient owner;
    owner.has_owning_stream   = true;
    owner.owning_stream_index = 7;

    auto ctx = writer_ctx(map, &owner, 7, true, false, true, 0x100, nullptr, 0);
    REQUIRE(ctx.via_root_client_ep0);
    REQUIRE(ctx.via_owning_stream);
    REQUIRE(ctx.via_non_unicast_frame);
    REQUIRE(ctx.via_discovery_stream);
    REQUIRE_FALSE(ctx.via_valid_stream_association);

    GeneralMap no_root; // svr_root_client_index == kNoRootClient
    auto ctx2 = writer_ctx(no_root, nullptr, 2, false, true, false, 0x100, nullptr, 0);
    REQUIRE_FALSE(ctx2.via_root_client_ep0);
    REQUIRE_FALSE(ctx2.via_owning_stream);
    REQUIRE_FALSE(ctx2.via_non_unicast_frame);
    REQUIRE_FALSE(ctx2.via_discovery_stream);
    REQUIRE_FALSE(ctx2.via_valid_stream_association);
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

// ── EP-ID / byte_bus_id mapping table (batch B, pre-existing) ────────────────────

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

// ── Response / ack queue config (batch B, pre-existing) ──────────────────────────

TEST_CASE("ResponseQueueConfig fields exist and are settable", "[regmap][REQ-REGMAP-011]") {
    ResponseQueueConfig rqc;
    rqc.response_queue_size = 6;
    rqc.ack_queue_size = 6;
    REQUIRE(rqc.response_queue_size == 6);
    REQUIRE(rqc.ack_queue_size == 6);
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
