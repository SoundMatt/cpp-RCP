// fusa:test REQ-MDIO-001
// fusa:test REQ-MDIO-002
// fusa:test REQ-MDIO-003
// fusa:test REQ-MDIO-004
// fusa:test REQ-MDIO-005
// fusa:test REQ-MDIO-006
// fusa:test REQ-MDIO-007
// fusa:test REQ-MDIO-008
// fusa:test REQ-MDIO-009
// fusa:test REQ-MDIO-010
// fusa:test REQ-MDIO-011
// fusa:test REQ-MDIO-012
// fusa:test REQ-MDIO-013
// fusa:test REQ-MDIO-014
// fusa:test REQ-MDIO-015
// fusa:test REQ-MDIO-016
// fusa:test REQ-MDIO-017
// fusa:test REQ-MDIO-018
// fusa:test REQ-MDIO-019
// fusa:test REQ-MDIO-020
// fusa:test REQ-MDIO-021
// fusa:test REQ-MDIO-022
// fusa:test REQ-MDIO-023
// fusa:test REQ-MDIO-024
// fusa:test REQ-MDIO-025
// fusa:test REQ-MDIO-026
// fusa:test REQ-MDIO-027
// fusa:test REQ-MDIO-028

// Tests for rcp/mdio.hpp — the MDIO endpoint type (ep_type 0x0D), ported
// from c-RCP's tests/test_ep_mdio.c (this project's RC5-spec-conformant
// reference, and the single largest endpoint test file in c-RCP at 2219
// lines) as part of Phase 3 of the ground-up rewrite (cpp-RCP issue #129,
// ROADMAP.md "Phase 17"). Allocation-failure tests from test_ep_mdio.c
// (rcp_alloc_set_hooks()-based MC/DC coverage of c-RCP's own rcp_malloc()
// failure paths) have no cpp-RCP equivalent — this codec uses std::vector
// throughout, with no allocation-hook seam anywhere else in this codebase
// either (see e.g. rcp/i2c.hpp's/rcp/iseled.hpp's own test files, which
// carry none) — and are therefore not ported.

#include <catch2/catch_test_macros.hpp>
#include <rcp/acf.hpp>
#include <rcp/endpoint.hpp>
#include <rcp/lifecycle.hpp>
#include <rcp/mdio.hpp>

using namespace rcp::mdio;

namespace {
MdioAddr clause22_addr(uint8_t prtad, uint16_t regad) {
    MdioAddr addr;
    addr.clause = MdioClause::Clause22;
    addr.prtad  = prtad;
    addr.devad  = 0;
    addr.regad  = regad;
    return addr;
}
MdioAddr clause45_addr(uint8_t prtad, uint8_t devad, uint16_t regad) {
    MdioAddr addr;
    addr.clause = MdioClause::Clause45;
    addr.prtad  = prtad;
    addr.devad  = devad;
    addr.regad  = regad;
    return addr;
}
MdioMmsAddr mms_addr(uint8_t mms, uint16_t addr_val) {
    MdioMmsAddr addr;
    addr.mms  = mms;
    addr.addr = addr_val;
    return addr;
}
} // namespace

// ── ep_type id ────────────────────────────────────────────────────────────────

TEST_CASE("MDIO's ep_type id is 0x0D", "[mdio][REQ-MDIO-001]") {
    REQUIRE(rcp::endpoint::kEndpointTypeMdio == 0x0D);
}

// ── addr_valid (Clause-22/Clause-45), REQ-MDIO-001 ───────────────────────────

TEST_CASE("addr_valid accepts Clause-22 addresses in range", "[mdio][REQ-MDIO-001]") {
    REQUIRE(addr_valid(clause22_addr(0, 0)));
    REQUIRE(addr_valid(clause22_addr(0x1F, 0x1F)));
}

TEST_CASE("addr_valid rejects a Clause-22 address with a nonzero devad", "[mdio][REQ-MDIO-001]") {
    MdioAddr addr = clause22_addr(1, 1);
    addr.devad     = 1;
    REQUIRE_FALSE(addr_valid(addr));
}

TEST_CASE("addr_valid rejects a Clause-22 regad above 5 bits", "[mdio][REQ-MDIO-001]") {
    REQUIRE_FALSE(addr_valid(clause22_addr(0, 0x20)));
}

TEST_CASE("addr_valid accepts Clause-45 addresses in range", "[mdio][REQ-MDIO-001]") {
    REQUIRE(addr_valid(clause45_addr(0, 0, 0)));
    REQUIRE(addr_valid(clause45_addr(0x1F, 0x1F, 0xFFFF)));
}

TEST_CASE("addr_valid rejects a Clause-45 devad above 5 bits", "[mdio][REQ-MDIO-001]") {
    REQUIRE_FALSE(addr_valid(clause45_addr(0, 0x20, 0)));
}

TEST_CASE("addr_valid rejects prtad above 5 bits for either clause", "[mdio][REQ-MDIO-001]") {
    REQUIRE_FALSE(addr_valid(clause22_addr(0x20, 0)));
    REQUIRE_FALSE(addr_valid(clause45_addr(0x20, 0, 0)));
}

TEST_CASE("addr_valid rejects an unknown clause value", "[mdio][REQ-MDIO-001]") {
    MdioAddr addr = clause22_addr(0, 0);
    addr.clause    = static_cast<MdioClause>(2);
    REQUIRE_FALSE(addr_valid(addr));
}

// ── burst_next_regad, REQ-MDIO-002 ───────────────────────────────────────────

TEST_CASE("burst_next_regad increments within Clause-22's 5-bit range", "[mdio][REQ-MDIO-002]") {
    REQUIRE(burst_next_regad(MdioClause::Clause22, 0) == 1);
}
TEST_CASE("burst_next_regad wraps at Clause-22's 5-bit boundary", "[mdio][REQ-MDIO-002]") {
    REQUIRE(burst_next_regad(MdioClause::Clause22, 0x1F) == 0);
}
TEST_CASE("burst_next_regad increments within Clause-45's 16-bit range", "[mdio][REQ-MDIO-002]") {
    REQUIRE(burst_next_regad(MdioClause::Clause45, 0x1234) == 0x1235);
}
TEST_CASE("burst_next_regad wraps at Clause-45's 16-bit boundary", "[mdio][REQ-MDIO-002]") {
    REQUIRE(burst_next_regad(MdioClause::Clause45, 0xFFFF) == 0);
}
TEST_CASE("burst_next_regad leaves regad unchanged for an unknown clause", "[mdio][REQ-MDIO-002]") {
    REQUIRE(burst_next_regad(static_cast<MdioClause>(2), 42) == 42);
}

// ── mdio_mode, REQ-MDIO-021 ───────────────────────────────────────────────────

TEST_CASE("mode_for_word_count selects single for word_count==1, multi otherwise",
          "[mdio][REQ-MDIO-021]") {
    REQUIRE(mode_for_word_count(1) == MdioMode::MmdSingleWord);
    REQUIRE(mode_for_word_count(2) == MdioMode::MmdMultiWord);
    REQUIRE(mode_for_word_count(kMaxBurstWords) == MdioMode::MmdMultiWord);
}

TEST_CASE("mode_is_unsupported_mms is true only for the MMS mode values", "[mdio][REQ-MDIO-021]") {
    REQUIRE_FALSE(mode_is_unsupported_mms(MdioMode::MmdSingleWord));
    REQUIRE_FALSE(mode_is_unsupported_mms(MdioMode::MmdMultiWord));
    REQUIRE(mode_is_unsupported_mms(MdioMode::MmsSingleWord));
    REQUIRE(mode_is_unsupported_mms(MdioMode::MmsMultiWord));
}

TEST_CASE("encode_read_request's leading mdio_mode octet reflects word_count", "[mdio][REQ-MDIO-021]") {
    {
        const auto frame = encode_read_request(2, clause22_addr(1, 0), 1, 0);
        REQUIRE_FALSE(frame.empty());
        rcp::acf::AcfMessageInfo hdr;
        std::vector<uint8_t>     payload;
        REQUIRE_FALSE(rcp::acf::decode_acf_abb(frame.data(), frame.size(), hdr, payload));
        REQUIRE(payload[0] == static_cast<uint8_t>(MdioMode::MmdSingleWord));
    }
    {
        const auto frame = encode_read_request(2, clause22_addr(1, 0), 4, 0);
        rcp::acf::AcfMessageInfo hdr;
        std::vector<uint8_t>     payload;
        REQUIRE_FALSE(rcp::acf::decode_acf_abb(frame.data(), frame.size(), hdr, payload));
        REQUIRE(payload[0] == static_cast<uint8_t>(MdioMode::MmdMultiWord));
    }
}

TEST_CASE("encode_write_request's leading mdio_mode octet reflects word_count", "[mdio][REQ-MDIO-021]") {
    {
        const uint16_t word  = 0x1234;
        const auto     frame = encode_write_request(2, clause45_addr(1, 2, 0), &word, 1, 0);
        rcp::acf::AcfMessageInfo hdr;
        std::vector<uint8_t>     payload;
        REQUIRE_FALSE(rcp::acf::decode_acf_abb(frame.data(), frame.size(), hdr, payload));
        REQUIRE(payload[0] == static_cast<uint8_t>(MdioMode::MmdSingleWord));
    }
    {
        const uint16_t words[3] = {1, 2, 3};
        const auto     frame    = encode_write_request(2, clause45_addr(1, 2, 0), words, 3, 0);
        rcp::acf::AcfMessageInfo hdr;
        std::vector<uint8_t>     payload;
        REQUIRE_FALSE(rcp::acf::decode_acf_abb(frame.data(), frame.size(), hdr, payload));
        REQUIRE(payload[0] == static_cast<uint8_t>(MdioMode::MmdMultiWord));
    }
}

TEST_CASE("decode_read_request rejects a frame whose mdio_mode octet is an MMS value",
          "[mdio][REQ-MDIO-013][REQ-MDIO-021]") {
    std::vector<uint8_t> payload(8, 0);
    payload[0] = static_cast<uint8_t>(MdioMode::MmsSingleWord);
    payload[7] = 1; // word_count -- otherwise-valid

    rcp::acf::AcfMessageInfo hdr;
    hdr.byte_bus_id = 2;
    hdr.op          = false; // read
    const auto frame = rcp::acf::encode_acf_abb(hdr, payload);

    MdioAddr out_addr;
    size_t   out_word_count = 0;
    uint8_t  txn            = 0;
    REQUIRE(decode_read_request(frame.data(), frame.size(), 2, out_addr, out_word_count, txn) ==
            make_error_code(MdioErrc::unsupported_mms));
}

TEST_CASE("decode_write_request rejects a frame whose mdio_mode octet is an MMS value",
          "[mdio][REQ-MDIO-017][REQ-MDIO-021]") {
    std::vector<uint8_t> payload(1 + 5 + 2, 0);
    payload[0] = static_cast<uint8_t>(MdioMode::MmsMultiWord);

    rcp::acf::AcfMessageInfo hdr;
    hdr.byte_bus_id = 2;
    hdr.op          = true; // write
    const auto frame = rcp::acf::encode_acf_abb(hdr, payload);

    MdioAddr              out_addr;
    std::vector<uint8_t>  out_words;
    size_t                out_word_count = 0;
    uint8_t                txn            = 0;
    REQUIRE(decode_write_request(frame.data(), frame.size(), 2, out_addr, out_words, out_word_count, txn) ==
            make_error_code(MdioErrc::unsupported_mms));
}

// ── MMS addressing, REQ-MDIO-022/024 ─────────────────────────────────────────

TEST_CASE("mms_addr_valid accepts mms in range", "[mdio][REQ-MDIO-024]") {
    REQUIRE(mms_addr_valid(mms_addr(0, 0)));
    REQUIRE(mms_addr_valid(mms_addr(kMmsMax, 0xFFFF)));
}
TEST_CASE("mms_addr_valid rejects mms above kMmsMax", "[mdio][REQ-MDIO-024]") {
    REQUIRE_FALSE(mms_addr_valid(mms_addr(static_cast<uint8_t>(kMmsMax + 1), 0)));
}
TEST_CASE("mms_uses_32bit_words is true only for MMS0 and MMS1", "[mdio][REQ-MDIO-022]") {
    REQUIRE(mms_uses_32bit_words(0));
    REQUIRE(mms_uses_32bit_words(1));
    for (uint8_t mms = 2; mms <= kMmsMax; ++mms) REQUIRE_FALSE(mms_uses_32bit_words(mms));
}
TEST_CASE("mms_burst_next_addr increments and wraps at 16 bits", "[mdio][REQ-MDIO-024]") {
    REQUIRE(mms_burst_next_addr(0x0001) == 0x0002);
    REQUIRE(mms_burst_next_addr(0xFFFF) == 0x0000);
}
TEST_CASE("mms_mode_for_word_count selects single for word_count==1, multi otherwise",
          "[mdio][REQ-MDIO-022]") {
    REQUIRE(mms_mode_for_word_count(1) == MdioMode::MmsSingleWord);
    REQUIRE(mms_mode_for_word_count(2) == MdioMode::MmsMultiWord);
    REQUIRE(mms_mode_for_word_count(kMaxBurstWords) == MdioMode::MmsMultiWord);
}

// ── Register-word packing: MMD family (16-bit), REQ-MDIO-003..008 ───────────

TEST_CASE("word_encode/word_decode round-trip and are big-endian", "[mdio][REQ-MDIO-003][REQ-MDIO-004]") {
    uint8_t buf[2];
    word_encode(0xBEEF, buf);
    REQUIRE(word_decode(buf) == 0xBEEF);

    word_encode(0x1234, buf);
    REQUIRE(buf[0] == 0x12);
    REQUIRE(buf[1] == 0x34);
}

TEST_CASE("pack_len is word_count * 2", "[mdio][REQ-MDIO-005]") {
    REQUIRE(pack_len(0) == 0);
    REQUIRE(pack_len(3) == 6);
}

TEST_CASE("pack_words/word_count_of/unpack_word_at round-trip", "[mdio][REQ-MDIO-006][REQ-MDIO-008]") {
    const uint16_t words[3] = {0x0001, 0xBEEF, 0xFFFF};
    const auto     packed   = pack_words(words, 3);
    REQUIRE(packed.size() == 6);

    size_t word_count = 0;
    REQUIRE(word_count_of(packed.size(), word_count));
    REQUIRE(word_count == 3);
    for (size_t i = 0; i < 3; ++i) REQUIRE(unpack_word_at(packed.data(), i) == words[i]);
}

TEST_CASE("pack_words returns an empty vector for word_count 0", "[mdio][REQ-MDIO-006]") {
    REQUIRE(pack_words(nullptr, 0).empty());
}

TEST_CASE("word_count_of rejects an odd byte length", "[mdio][REQ-MDIO-007]") {
    size_t word_count = 0;
    REQUIRE_FALSE(word_count_of(3, word_count));
}
TEST_CASE("word_count_of accepts an even byte length", "[mdio][REQ-MDIO-007]") {
    size_t word_count = 0;
    REQUIRE(word_count_of(8, word_count));
    REQUIRE(word_count == 4);
}

// ── Register-word packing: MMS family (16- or 32-bit), REQ-MDIO-022 ─────────

TEST_CASE("word32_encode/word32_decode round-trip and are big-endian", "[mdio][REQ-MDIO-022]") {
    uint8_t buf[4];
    word32_encode(0xDEADBEEFu, buf);
    REQUIRE(word32_decode(buf) == 0xDEADBEEFu);

    word32_encode(0x12345678u, buf);
    REQUIRE(buf[0] == 0x12);
    REQUIRE(buf[1] == 0x34);
    REQUIRE(buf[2] == 0x56);
    REQUIRE(buf[3] == 0x78);
}

TEST_CASE("mms_pack_len is 4 octets/word for MMS0/1, 2 otherwise", "[mdio][REQ-MDIO-022]") {
    REQUIRE(mms_pack_len(0, 3) == 12);
    REQUIRE(mms_pack_len(2, 3) == 6);
}

TEST_CASE("mms_pack_words/mms_unpack_word_at round-trip at 32-bit width", "[mdio][REQ-MDIO-022]") {
    const uint32_t words[2] = {0x11223344u, 0xAABBCCDDu};
    const auto     packed   = mms_pack_words(1 /* MMS1: 32-bit */, words, 2);
    REQUIRE(packed.size() == 8);
    REQUIRE(mms_unpack_word_at(1, packed.data(), 0) == 0x11223344u);
    REQUIRE(mms_unpack_word_at(1, packed.data(), 1) == 0xAABBCCDDu);
}

TEST_CASE("mms_pack_words/mms_unpack_word_at round-trip at 16-bit width", "[mdio][REQ-MDIO-022]") {
    const uint32_t words[2] = {0x1234u, 0xBEEFu};
    const auto     packed   = mms_pack_words(4 /* not 0/1: 16-bit */, words, 2);
    REQUIRE(packed.size() == 4);
    REQUIRE(mms_unpack_word_at(4, packed.data(), 0) == 0x1234u);
    REQUIRE(mms_unpack_word_at(4, packed.data(), 1) == 0xBEEFu);
}

TEST_CASE("mms_pack_words returns an empty vector for word_count 0", "[mdio][REQ-MDIO-022]") {
    REQUIRE(mms_pack_words(0, nullptr, 0).empty());
}

TEST_CASE("mms_word_count_of rejects a byte length not a multiple of the 32-bit width",
          "[mdio][REQ-MDIO-022]") {
    size_t out = 0;
    REQUIRE_FALSE(mms_word_count_of(0, 5, out));
    REQUIRE(mms_word_count_of(0, 8, out));
    REQUIRE(out == 2);
}
TEST_CASE("mms_word_count_of rejects an odd byte length at 16-bit width", "[mdio][REQ-MDIO-022]") {
    size_t out = 0;
    REQUIRE_FALSE(mms_word_count_of(3, 3, out));
    REQUIRE(mms_word_count_of(3, 4, out));
    REQUIRE(out == 2);
}

// ── Functional config, REQ-MDIO-009/010 ──────────────────────────────────────

TEST_CASE("functional_cfg_init zeroes every field", "[mdio][REQ-MDIO-009]") {
    MdioFunctionalCfg cfg;
    cfg.ep_enable = cfg.ep_clear_req_storage = cfg.ep_req_crc_enable = true;
    cfg.ep_status = 0xBEEF;

    functional_cfg_init(cfg);

    REQUIRE_FALSE(cfg.ep_enable);
    REQUIRE_FALSE(cfg.ep_clear_req_storage);
    REQUIRE_FALSE(cfg.ep_req_crc_enable);
    REQUIRE_FALSE(cfg.ep_response_ts_enable);
    REQUIRE_FALSE(cfg.ep_suppress_response);
    REQUIRE(cfg.ep_status == 0);
}

TEST_CASE("functional_cfg_writable is false in HwUnconfigured regardless of writer",
          "[mdio][REQ-MDIO-010]") {
    rcp::lifecycle::WriterCtx writer;
    writer.via_root_client_ep0 = true;
    writer.via_owning_stream   = true;
    REQUIRE_FALSE(functional_cfg_writable(rcp::lifecycle::ServerState::HwUnconfigured, writer));
}

TEST_CASE("functional_cfg_writable in HwConfigured requires EP0/owning-stream/discovery-stream",
          "[mdio][REQ-MDIO-010]") {
    rcp::lifecycle::WriterCtx none, via_ep0, via_stream, via_discovery;
    via_ep0.via_root_client_ep0        = true;
    via_stream.via_owning_stream       = true;
    via_discovery.via_discovery_stream = true;

    REQUIRE_FALSE(functional_cfg_writable(rcp::lifecycle::ServerState::HwConfigured, none));
    REQUIRE(functional_cfg_writable(rcp::lifecycle::ServerState::HwConfigured, via_ep0));
    REQUIRE(functional_cfg_writable(rcp::lifecycle::ServerState::HwConfigured, via_stream));
    REQUIRE(functional_cfg_writable(rcp::lifecycle::ServerState::HwConfigured, via_discovery));
}

TEST_CASE("functional_cfg_writable in RcpConfigured requires EP0/owning-stream, not discovery",
          "[mdio][REQ-MDIO-010]") {
    rcp::lifecycle::WriterCtx none, via_ep0, via_stream;
    via_ep0.via_root_client_ep0  = true;
    via_stream.via_owning_stream = true;

    REQUIRE_FALSE(functional_cfg_writable(rcp::lifecycle::ServerState::RcpConfigured, none));
    REQUIRE(functional_cfg_writable(rcp::lifecycle::ServerState::RcpConfigured, via_ep0));
    REQUIRE(functional_cfg_writable(rcp::lifecycle::ServerState::RcpConfigured, via_stream));
}

// ── The EP_func register block, REQ-MDIO-020/023 ─────────────────────────────

TEST_CASE("render_registers matches the corrected Table 59 offsets (no base_clk row)",
          "[mdio][REQ-MDIO-020][REQ-MDIO-023]") {
    MdioFunctionalCfg cfg;
    functional_cfg_init(cfg);
    cfg.ep_enable = true;
    cfg.ep_status = 0x1234;

    const auto out = render_registers(cfg);

    REQUIRE(out[kRegEpLen] == static_cast<uint8_t>(kEpFuncLen));
    REQUIRE(out[kRegReserved01] == 0);
    REQUIRE((out[kRegEpEnableClr] & 0x01) != 0);
    REQUIRE(out[kRegEpStatus] == 0x12);
    REQUIRE(out[kRegEpStatus + 1] == 0x34);
    REQUIRE(kEpFuncLen == 0x0006u);
}

TEST_CASE("apply_reconfig writes ep_status", "[mdio][REQ-MDIO-023]") {
    MdioFunctionalCfg cfg;
    functional_cfg_init(cfg);
    const uint8_t payload[4] = {0x00, static_cast<uint8_t>(kRegEpStatus), 0xAB, 0xCD};

    REQUIRE_FALSE(apply_reconfig(cfg, payload, sizeof(payload)));
    REQUIRE(cfg.ep_status == 0xABCD);
}

TEST_CASE("apply_reconfig ignores EP_LEN and the reserved octet", "[mdio][REQ-MDIO-023]") {
    MdioFunctionalCfg cfg;
    functional_cfg_init(cfg);
    const uint8_t payload[4] = {0x00, 0x00, 0xFF, 0xFF};

    REQUIRE_FALSE(apply_reconfig(cfg, payload, sizeof(payload)));

    const auto out = render_registers(cfg);
    REQUIRE(out[kRegEpLen] == static_cast<uint8_t>(kEpFuncLen));
    REQUIRE(out[kRegReserved01] == 0);
}

TEST_CASE("apply_reconfig rejects a write extending past EP_LEN", "[mdio][REQ-MDIO-023]") {
    MdioFunctionalCfg cfg;
    functional_cfg_init(cfg);
    const uint8_t payload[3] = {0x00, 0x06, 0xFF}; // 0x06 == kEpFuncLen -- one past the last valid offset

    REQUIRE(apply_reconfig(cfg, payload, sizeof(payload)) == make_error_code(MdioReconfigErrc::out_of_range));
    REQUIRE(cfg.ep_status == 0);
}

TEST_CASE("apply_reconfig rejects a payload with no data octet", "[mdio][REQ-MDIO-023]") {
    MdioFunctionalCfg cfg;
    functional_cfg_init(cfg);
    const uint8_t addr_only[2] = {0x00, 0x04};

    REQUIRE(apply_reconfig(cfg, addr_only, sizeof(addr_only)) ==
            make_error_code(MdioReconfigErrc::short_payload));
    REQUIRE(apply_reconfig(cfg, nullptr, 0) == make_error_code(MdioReconfigErrc::short_payload));
}

TEST_CASE("encode_reconfig_request round-trips through acf::decode_acf_abb", "[mdio][REQ-MDIO-023]") {
    const std::vector<uint8_t> data{0xAB, 0xCD};
    const auto                 frame = encode_reconfig_request(0x03, 0x0004, data, 7);
    REQUIRE_FALSE(frame.empty());

    rcp::acf::AcfMessageInfo hdr;
    std::vector<uint8_t>     payload;
    REQUIRE_FALSE(rcp::acf::decode_acf_abb(frame.data(), frame.size(), hdr, payload));
    REQUIRE(hdr.byte_bus_id == 0x03);
    REQUIRE(hdr.op);
    REQUIRE(hdr.evt_op == 0x7);
    REQUIRE(hdr.transaction_num == 7);
    REQUIRE(payload == std::vector<uint8_t>{0x00, 0x04, 0xAB, 0xCD});
}

TEST_CASE("encode_reconfig_request rejects empty data", "[mdio][REQ-MDIO-023]") {
    REQUIRE(encode_reconfig_request(0x00, 0, {}, 0).empty());
}

TEST_CASE("mdio reconfig error category reports a distinct, non-empty message per code",
          "[mdio][REQ-MDIO-023]") {
    auto short_ec = make_error_code(MdioReconfigErrc::short_payload);
    auto range_ec = make_error_code(MdioReconfigErrc::out_of_range);
    REQUIRE_FALSE(short_ec.message().empty());
    REQUIRE_FALSE(range_ec.message().empty());
    REQUIRE(short_ec.message() != range_ec.message());
}

// ── MdioErrc category sanity, REQ-MDIO-011 ───────────────────────────────────

TEST_CASE("MdioErrc reports a non-empty, distinct message per code", "[mdio][REQ-MDIO-011]") {
    const MdioErrc codes[] = {
        MdioErrc::payload_exceeds_mode_width, MdioErrc::config_write_not_supported,
        MdioErrc::short_frame,                MdioErrc::bad_msg_type,
        MdioErrc::wrong_bus,                  MdioErrc::wrong_op,
        MdioErrc::bad_addr,                   MdioErrc::bad_word_count,
        MdioErrc::bad_evt,                    MdioErrc::unsupported_mms,
        MdioErrc::bad_mms_addr,               MdioErrc::wrong_mdio_mode,
    };
    std::vector<std::string> seen;
    for (auto c : codes) {
        auto ec = make_error_code(c);
        REQUIRE(ec.category() == mdio_category());
        REQUIRE_FALSE(ec.message().empty());
        for (const auto& s : seen) REQUIRE(s != ec.message());
        seen.push_back(ec.message());
    }
    REQUIRE_FALSE(make_error_code(static_cast<MdioErrc>(999)).message().empty());
}

// ── Read request round trip (MMD family), REQ-MDIO-012/013 ──────────────────

TEST_CASE("read request round-trips a single-word Clause-45 address", "[mdio][REQ-MDIO-012][REQ-MDIO-013]") {
    const auto addr  = clause45_addr(3, 1, 0x1234);
    const auto frame = encode_read_request(6, addr, 1, 9);
    REQUIRE_FALSE(frame.empty());

    MdioAddr out_addr;
    size_t   out_word_count = 0;
    uint8_t  txn            = 0;
    REQUIRE_FALSE(decode_read_request(frame.data(), frame.size(), 6, out_addr, out_word_count, txn));
    REQUIRE(out_addr.clause == MdioClause::Clause45);
    REQUIRE(out_addr.prtad == 3);
    REQUIRE(out_addr.devad == 1);
    REQUIRE(out_addr.regad == 0x1234);
    REQUIRE(out_word_count == 1);
    REQUIRE(txn == 9);
}

TEST_CASE("read request round-trips a burst", "[mdio][REQ-MDIO-012][REQ-MDIO-013]") {
    const auto frame = encode_read_request(4, clause22_addr(2, 5), 16, 1);
    MdioAddr   out_addr;
    size_t     out_word_count = 0;
    uint8_t    txn            = 0;
    REQUIRE_FALSE(decode_read_request(frame.data(), frame.size(), 4, out_addr, out_word_count, txn));
    REQUIRE(out_word_count == 16);
}

TEST_CASE("encode_read_request rejects an invalid address", "[mdio][REQ-MDIO-012]") {
    MdioAddr addr = clause22_addr(0, 0);
    addr.devad     = 1; // invalid for Clause-22
    REQUIRE(encode_read_request(1, addr, 1, 0).empty());
}
TEST_CASE("encode_read_request rejects word_count 0 or above kMaxBurstWords", "[mdio][REQ-MDIO-012]") {
    REQUIRE(encode_read_request(1, clause22_addr(0, 0), 0, 0).empty());
    REQUIRE(encode_read_request(1, clause22_addr(0, 0), kMaxBurstWords + 1, 0).empty());
}

TEST_CASE("decode_read_request rejects the wrong bus", "[mdio][REQ-MDIO-013]") {
    const auto frame = encode_read_request(4, clause22_addr(0, 0), 1, 0);
    MdioAddr   out_addr;
    size_t     out_word_count;
    uint8_t    txn;
    REQUIRE(decode_read_request(frame.data(), frame.size(), 5, out_addr, out_word_count, txn) ==
            make_error_code(MdioErrc::wrong_bus));
}

TEST_CASE("decode_read_request rejects a write-op frame", "[mdio][REQ-MDIO-013]") {
    rcp::acf::AcfMessageInfo hdr;
    hdr.byte_bus_id   = 2;
    hdr.op            = true; // not a read request
    const auto frame  = rcp::acf::encode_acf_abb(hdr, {});

    MdioAddr out_addr;
    size_t   out_word_count;
    uint8_t  txn;
    REQUIRE(decode_read_request(frame.data(), frame.size(), 2, out_addr, out_word_count, txn) ==
            make_error_code(MdioErrc::wrong_op));
}

TEST_CASE("decode_read_request rejects a nonzero evt[2:0]", "[mdio][REQ-MDIO-013]") {
    rcp::acf::AcfMessageInfo hdr;
    hdr.byte_bus_id  = 2;
    hdr.op           = false;
    hdr.evt_op       = 0x4; // reserved in MDIO's Table 33 row
    const auto frame = rcp::acf::encode_acf_abb(hdr, {});

    MdioAddr out_addr;
    size_t   out_word_count;
    uint8_t  txn;
    REQUIRE(decode_read_request(frame.data(), frame.size(), 2, out_addr, out_word_count, txn) ==
            make_error_code(MdioErrc::bad_evt));
}

TEST_CASE("decode_read_request rejects a non-ABB frame", "[mdio][REQ-MDIO-013]") {
    rcp::acf::AcfMessageInfo hdr;
    hdr.byte_bus_id  = 2;
    hdr.op           = false;
    const auto frame = rcp::acf::encode_acf_gbb(hdr, 0, {});

    MdioAddr out_addr;
    size_t   out_word_count;
    uint8_t  txn;
    REQUIRE(decode_read_request(frame.data(), frame.size(), 2, out_addr, out_word_count, txn) ==
            make_error_code(MdioErrc::bad_msg_type));
}

TEST_CASE("decode_read_request rejects a frame too short for the mode+address prefix",
          "[mdio][REQ-MDIO-013]") {
    rcp::acf::AcfMessageInfo hdr;
    hdr.byte_bus_id  = 2;
    hdr.op           = false;
    const std::vector<uint8_t> too_short{0, 0, 0};
    const auto                 frame = rcp::acf::encode_acf_abb(hdr, too_short);

    MdioAddr out_addr;
    size_t   out_word_count;
    uint8_t  txn;
    REQUIRE(decode_read_request(frame.data(), frame.size(), 2, out_addr, out_word_count, txn) ==
            make_error_code(MdioErrc::short_frame));
}

TEST_CASE("decode_read_request rejects an invalid decoded address", "[mdio][REQ-MDIO-013]") {
    std::vector<uint8_t> payload(8, 0);
    payload[0] = static_cast<uint8_t>(MdioMode::MmdSingleWord);
    payload[3] = 1; // devad -- invalid for Clause-22 (payload[1]==clause==0)
    payload[7] = 1; // word_count

    rcp::acf::AcfMessageInfo hdr;
    hdr.byte_bus_id  = 2;
    hdr.op           = false;
    const auto frame = rcp::acf::encode_acf_abb(hdr, payload);

    MdioAddr out_addr;
    size_t   out_word_count;
    uint8_t  txn;
    REQUIRE(decode_read_request(frame.data(), frame.size(), 2, out_addr, out_word_count, txn) ==
            make_error_code(MdioErrc::bad_addr));
}

TEST_CASE("decode_read_request rejects a zero word_count", "[mdio][REQ-MDIO-013]") {
    std::vector<uint8_t> payload(8, 0);
    payload[0] = static_cast<uint8_t>(MdioMode::MmdSingleWord);

    rcp::acf::AcfMessageInfo hdr;
    hdr.byte_bus_id  = 2;
    hdr.op           = false;
    const auto frame = rcp::acf::encode_acf_abb(hdr, payload);

    MdioAddr out_addr;
    size_t   out_word_count;
    uint8_t  txn;
    REQUIRE(decode_read_request(frame.data(), frame.size(), 2, out_addr, out_word_count, txn) ==
            make_error_code(MdioErrc::bad_word_count));
}

TEST_CASE("decode_read_request rejects a word_count above kMaxBurstWords", "[mdio][REQ-MDIO-013]") {
    std::vector<uint8_t> payload(8, 0);
    payload[0] = static_cast<uint8_t>(MdioMode::MmdMultiWord);
    payload[6] = static_cast<uint8_t>((kMaxBurstWords + 1) >> 8);
    payload[7] = static_cast<uint8_t>((kMaxBurstWords + 1) & 0xFF);

    rcp::acf::AcfMessageInfo hdr;
    hdr.byte_bus_id  = 2;
    hdr.op           = false;
    const auto frame = rcp::acf::encode_acf_abb(hdr, payload);

    MdioAddr out_addr;
    size_t   out_word_count;
    uint8_t  txn;
    REQUIRE(decode_read_request(frame.data(), frame.size(), 2, out_addr, out_word_count, txn) ==
            make_error_code(MdioErrc::bad_word_count));
}

// ── Read response round trip (MMD family), REQ-MDIO-014/015/025/026 ─────────

TEST_CASE("read response round-trips untimed", "[mdio][REQ-MDIO-014][REQ-MDIO-015]") {
    const uint16_t words[2] = {0x1111, 0x2222};
    const auto     frame    = encode_read_response(3, words, 2, 5, false, 0);
    REQUIRE_FALSE(frame.empty());

    std::vector<uint8_t> out_words;
    size_t                out_word_count = 0;
    bool                  timed          = true;
    uint64_t              ts             = 1;
    uint8_t                txn            = 0;
    REQUIRE_FALSE(decode_read_response(frame.data(), frame.size(), 3, out_words, out_word_count, timed, ts, txn));
    REQUIRE(out_word_count == 2);
    REQUIRE(unpack_word_at(out_words.data(), 0) == 0x1111);
    REQUIRE(unpack_word_at(out_words.data(), 1) == 0x2222);
    REQUIRE_FALSE(timed);
    REQUIRE(ts == 0);
    REQUIRE(txn == 5);
}

TEST_CASE("read response round-trips timed", "[mdio][REQ-MDIO-025][REQ-MDIO-026]") {
    const uint16_t words[1] = {0xABCD};
    const auto     frame    = encode_read_response(3, words, 1, 2, true, 424242);

    std::vector<uint8_t> out_words;
    size_t                out_word_count = 0;
    bool                  timed          = false;
    uint64_t              ts             = 0;
    uint8_t                txn            = 0;
    REQUIRE_FALSE(decode_read_response(frame.data(), frame.size(), 3, out_words, out_word_count, timed, ts, txn));
    REQUIRE(timed);
    REQUIRE(ts == 424242);
}

TEST_CASE("read response round-trips an empty word set", "[mdio][REQ-MDIO-014][REQ-MDIO-015]") {
    const auto frame = encode_read_response(3, nullptr, 0, 2, false, 0);
    REQUIRE_FALSE(frame.empty());

    std::vector<uint8_t> out_words;
    size_t                out_word_count = 1;
    bool                  timed          = false;
    uint64_t              ts             = 0;
    uint8_t                txn            = 0;
    REQUIRE_FALSE(decode_read_response(frame.data(), frame.size(), 3, out_words, out_word_count, timed, ts, txn));
    REQUIRE(out_word_count == 0);
}

TEST_CASE("decode_read_response rejects the wrong bus", "[mdio][REQ-MDIO-015]") {
    const uint16_t words[1] = {1};
    const auto     frame    = encode_read_response(3, words, 1, 0, false, 0);
    std::vector<uint8_t> out_words;
    size_t                out_word_count;
    bool                  timed;
    uint64_t              ts;
    uint8_t                txn;
    REQUIRE(decode_read_response(frame.data(), frame.size(), 4, out_words, out_word_count, timed, ts, txn) ==
            make_error_code(MdioErrc::wrong_bus));
}

TEST_CASE("decode_read_response rejects a short frame", "[mdio][REQ-MDIO-015]") {
    const uint8_t too_short[3] = {static_cast<uint8_t>(rcp::acf::kAcfMsgTypeAbb << 1), 0, 0};
    std::vector<uint8_t> out_words;
    size_t                out_word_count;
    bool                  timed;
    uint64_t              ts;
    uint8_t                txn;
    REQUIRE(decode_read_response(too_short, sizeof(too_short), 2, out_words, out_word_count, timed, ts, txn) ==
            make_error_code(MdioErrc::short_frame));
}

TEST_CASE("decode_read_response rejects an odd payload length", "[mdio][REQ-MDIO-015]") {
    rcp::acf::AcfMessageInfo hdr;
    hdr.byte_bus_id  = 2;
    hdr.op           = false;
    const std::vector<uint8_t> odd_payload{0, 0, 0};
    const auto                 frame = rcp::acf::encode_acf_abb(hdr, odd_payload);

    std::vector<uint8_t> out_words;
    size_t                out_word_count;
    bool                  timed;
    uint64_t              ts;
    uint8_t                txn;
    REQUIRE(decode_read_response(frame.data(), frame.size(), 2, out_words, out_word_count, timed, ts, txn) ==
            make_error_code(MdioErrc::bad_word_count));
}

// ── Write request round trip (MMD family), REQ-MDIO-016/017 ─────────────────

TEST_CASE("write request round-trips a single word", "[mdio][REQ-MDIO-016][REQ-MDIO-017]") {
    const auto     addr  = clause45_addr(4, 2, 0x0010);
    const uint16_t words[1] = {0x9999};
    const auto     frame = encode_write_request(7, addr, words, 1, 3);
    REQUIRE_FALSE(frame.empty());

    MdioAddr              out_addr;
    std::vector<uint8_t>  out_words;
    size_t                out_word_count = 0;
    uint8_t                txn            = 0;
    REQUIRE_FALSE(decode_write_request(frame.data(), frame.size(), 7, out_addr, out_words, out_word_count, txn));
    REQUIRE(out_addr.clause == MdioClause::Clause45);
    REQUIRE(out_addr.prtad == 4);
    REQUIRE(out_addr.devad == 2);
    REQUIRE(out_addr.regad == 0x0010);
    REQUIRE(out_word_count == 1);
    REQUIRE(unpack_word_at(out_words.data(), 0) == 0x9999);
    REQUIRE(txn == 3);
}

TEST_CASE("write request round-trips a burst", "[mdio][REQ-MDIO-016][REQ-MDIO-017]") {
    const uint16_t words[4] = {1, 2, 3, 4};
    const auto     frame    = encode_write_request(5, clause22_addr(1, 0), words, 4, 0);

    MdioAddr              out_addr;
    std::vector<uint8_t>  out_words;
    size_t                out_word_count = 0;
    uint8_t                txn            = 0;
    REQUIRE_FALSE(decode_write_request(frame.data(), frame.size(), 5, out_addr, out_words, out_word_count, txn));
    REQUIRE(out_word_count == 4);
    for (size_t i = 0; i < 4; ++i) REQUIRE(unpack_word_at(out_words.data(), i) == words[i]);
}

TEST_CASE("encode_write_request rejects an invalid address", "[mdio][REQ-MDIO-016]") {
    const uint16_t word = 1;
    REQUIRE(encode_write_request(1, clause22_addr(0, 0x20), &word, 1, 0).empty());
}
TEST_CASE("encode_write_request rejects word_count 0 or above kMaxBurstWords", "[mdio][REQ-MDIO-016]") {
    REQUIRE(encode_write_request(1, clause22_addr(0, 0), nullptr, 0, 0).empty());
    REQUIRE(encode_write_request(1, clause22_addr(0, 0), nullptr, kMaxBurstWords + 1, 0).empty());
}

TEST_CASE("decode_write_request rejects the wrong bus", "[mdio][REQ-MDIO-017]") {
    const uint16_t word  = 1;
    const auto     frame = encode_write_request(4, clause22_addr(0, 0), &word, 1, 0);

    MdioAddr              out_addr;
    std::vector<uint8_t>  out_words;
    size_t                out_word_count;
    uint8_t                txn;
    REQUIRE(decode_write_request(frame.data(), frame.size(), 5, out_addr, out_words, out_word_count, txn) ==
            make_error_code(MdioErrc::wrong_bus));
}

TEST_CASE("decode_write_request rejects a read-op frame", "[mdio][REQ-MDIO-017]") {
    rcp::acf::AcfMessageInfo hdr;
    hdr.byte_bus_id  = 2;
    hdr.op           = false; // not a write request
    const auto frame = rcp::acf::encode_acf_abb(hdr, {});

    MdioAddr              out_addr;
    std::vector<uint8_t>  out_words;
    size_t                out_word_count;
    uint8_t                txn;
    REQUIRE(decode_write_request(frame.data(), frame.size(), 2, out_addr, out_words, out_word_count, txn) ==
            make_error_code(MdioErrc::wrong_op));
}

TEST_CASE("decode_write_request rejects a nonzero evt[2:0]", "[mdio][REQ-MDIO-017]") {
    rcp::acf::AcfMessageInfo hdr;
    hdr.byte_bus_id  = 2;
    hdr.op           = true;
    hdr.evt_op       = 0x1;
    const auto frame = rcp::acf::encode_acf_abb(hdr, {});

    MdioAddr              out_addr;
    std::vector<uint8_t>  out_words;
    size_t                out_word_count;
    uint8_t                txn;
    REQUIRE(decode_write_request(frame.data(), frame.size(), 2, out_addr, out_words, out_word_count, txn) ==
            make_error_code(MdioErrc::bad_evt));
}

TEST_CASE("decode_write_request rejects a frame too short for the mode+address prefix",
          "[mdio][REQ-MDIO-017]") {
    rcp::acf::AcfMessageInfo hdr;
    hdr.byte_bus_id  = 2;
    hdr.op           = true;
    const std::vector<uint8_t> too_short{0, 0};
    const auto                 frame = rcp::acf::encode_acf_abb(hdr, too_short);

    MdioAddr              out_addr;
    std::vector<uint8_t>  out_words;
    size_t                out_word_count;
    uint8_t                txn;
    REQUIRE(decode_write_request(frame.data(), frame.size(), 2, out_addr, out_words, out_word_count, txn) ==
            make_error_code(MdioErrc::short_frame));
}

TEST_CASE("decode_write_request rejects an invalid decoded address", "[mdio][REQ-MDIO-017]") {
    std::vector<uint8_t> payload(1 + 5 + 2, 0);
    payload[0] = static_cast<uint8_t>(MdioMode::MmdSingleWord);
    payload[3] = 1; // devad -- invalid for Clause-22

    rcp::acf::AcfMessageInfo hdr;
    hdr.byte_bus_id  = 2;
    hdr.op           = true;
    const auto frame = rcp::acf::encode_acf_abb(hdr, payload);

    MdioAddr              out_addr;
    std::vector<uint8_t>  out_words;
    size_t                out_word_count;
    uint8_t                txn;
    REQUIRE(decode_write_request(frame.data(), frame.size(), 2, out_addr, out_words, out_word_count, txn) ==
            make_error_code(MdioErrc::bad_addr));
}

TEST_CASE("decode_write_request rejects zero words after the address prefix", "[mdio][REQ-MDIO-017]") {
    std::vector<uint8_t> payload(1 + 5, 0);
    payload[0] = static_cast<uint8_t>(MdioMode::MmdSingleWord);

    rcp::acf::AcfMessageInfo hdr;
    hdr.byte_bus_id  = 2;
    hdr.op           = true;
    const auto frame = rcp::acf::encode_acf_abb(hdr, payload);

    MdioAddr              out_addr;
    std::vector<uint8_t>  out_words;
    size_t                out_word_count;
    uint8_t                txn;
    REQUIRE(decode_write_request(frame.data(), frame.size(), 2, out_addr, out_words, out_word_count, txn) ==
            make_error_code(MdioErrc::bad_word_count));
}

TEST_CASE("decode_write_request rejects a word_count above kMaxBurstWords", "[mdio][REQ-MDIO-017]") {
    std::vector<uint8_t> payload(1 + 5 + 2 * (kMaxBurstWords + 1), 0);
    payload[0] = static_cast<uint8_t>(MdioMode::MmdMultiWord);

    rcp::acf::AcfMessageInfo hdr;
    hdr.byte_bus_id  = 2;
    hdr.op           = true;
    const auto frame = rcp::acf::encode_acf_abb(hdr, payload);

    MdioAddr              out_addr;
    std::vector<uint8_t>  out_words;
    size_t                out_word_count;
    uint8_t                txn;
    REQUIRE(decode_write_request(frame.data(), frame.size(), 2, out_addr, out_words, out_word_count, txn) ==
            make_error_code(MdioErrc::bad_word_count));
}

// ── Write response round trip (MMD family), REQ-MDIO-018/019/027/028 ────────

TEST_CASE("write response round-trips untimed", "[mdio][REQ-MDIO-018][REQ-MDIO-019]") {
    const uint16_t accepted[2] = {0xAAAA, 0xBBBB};
    const auto     frame       = encode_write_response(3, accepted, 2, 6, false, 0);
    REQUIRE_FALSE(frame.empty());

    std::vector<uint8_t> out_words;
    size_t                out_word_count = 0;
    bool                  timed          = true;
    uint64_t              ts             = 1;
    uint8_t                txn            = 0;
    REQUIRE_FALSE(decode_write_response(frame.data(), frame.size(), 3, out_words, out_word_count, timed, ts, txn));
    REQUIRE(out_word_count == 2);
    REQUIRE(unpack_word_at(out_words.data(), 0) == 0xAAAA);
    REQUIRE(unpack_word_at(out_words.data(), 1) == 0xBBBB);
    REQUIRE_FALSE(timed);
    REQUIRE(txn == 6);
}

TEST_CASE("write response round-trips timed", "[mdio][REQ-MDIO-027][REQ-MDIO-028]") {
    const uint16_t accepted[1] = {0x1};
    const auto     frame       = encode_write_response(3, accepted, 1, 1, true, 55);

    std::vector<uint8_t> out_words;
    size_t                out_word_count = 0;
    bool                  timed          = false;
    uint64_t              ts             = 0;
    uint8_t                txn            = 0;
    REQUIRE_FALSE(decode_write_response(frame.data(), frame.size(), 3, out_words, out_word_count, timed, ts, txn));
    REQUIRE(timed);
    REQUIRE(ts == 55);
}

TEST_CASE("write response round-trips nothing accepted", "[mdio][REQ-MDIO-018][REQ-MDIO-019]") {
    const auto frame = encode_write_response(3, nullptr, 0, 6, false, 0);
    REQUIRE_FALSE(frame.empty());

    std::vector<uint8_t> out_words;
    size_t                out_word_count = 1;
    bool                  timed          = false;
    uint64_t              ts             = 0;
    uint8_t                txn            = 0;
    REQUIRE_FALSE(decode_write_response(frame.data(), frame.size(), 3, out_words, out_word_count, timed, ts, txn));
    REQUIRE(out_word_count == 0);
}

TEST_CASE("decode_write_response rejects the wrong bus", "[mdio][REQ-MDIO-019]") {
    const uint16_t accepted[1] = {1};
    const auto     frame       = encode_write_response(3, accepted, 1, 0, false, 0);
    std::vector<uint8_t> out_words;
    size_t                out_word_count;
    bool                  timed;
    uint64_t              ts;
    uint8_t                txn;
    REQUIRE(decode_write_response(frame.data(), frame.size(), 9, out_words, out_word_count, timed, ts, txn) ==
            make_error_code(MdioErrc::wrong_bus));
}

TEST_CASE("decode_write_response rejects a short frame", "[mdio][REQ-MDIO-019]") {
    const uint8_t too_short[3] = {static_cast<uint8_t>(rcp::acf::kAcfMsgTypeAbb << 1), 0, 0};
    std::vector<uint8_t> out_words;
    size_t                out_word_count;
    bool                  timed;
    uint64_t              ts;
    uint8_t                txn;
    REQUIRE(decode_write_response(too_short, sizeof(too_short), 2, out_words, out_word_count, timed, ts, txn) ==
            make_error_code(MdioErrc::short_frame));
}

// ── MMS read request/response, REQ-MDIO-022/024 ─────────────────────────────

TEST_CASE("MMS read request round-trips a single 32-bit word (MMS0)", "[mdio][REQ-MDIO-022][REQ-MDIO-024]") {
    const auto addr  = mms_addr(0, 0xBEEF); // MMS0: 32-bit
    const auto frame = encode_mms_read_request(0x10, addr, 1, 7);
    REQUIRE_FALSE(frame.empty());

    MdioMmsAddr out_addr;
    size_t      out_word_count = 0;
    uint8_t      txn            = 0;
    REQUIRE_FALSE(decode_mms_read_request(frame.data(), frame.size(), 0x10, out_addr, out_word_count, txn));
    REQUIRE(out_addr.mms == 0);
    REQUIRE(out_addr.addr == 0xBEEF);
    REQUIRE(out_word_count == 1);
    REQUIRE(txn == 7);
}

TEST_CASE("MMS read request round-trips a 16-bit burst", "[mdio][REQ-MDIO-022][REQ-MDIO-024]") {
    const auto addr  = mms_addr(3, 0x0100); // not 0/1: 16-bit
    const auto frame = encode_mms_read_request(0x10, addr, 5, 9);

    MdioMmsAddr out_addr;
    size_t      out_word_count = 0;
    uint8_t      txn            = 0;
    REQUIRE_FALSE(decode_mms_read_request(frame.data(), frame.size(), 0x10, out_addr, out_word_count, txn));
    REQUIRE(out_addr.mms == 3);
    REQUIRE(out_addr.addr == 0x0100);
    REQUIRE(out_word_count == 5);
}

TEST_CASE("encode_mms_read_request rejects an invalid address or bad word_count",
          "[mdio][REQ-MDIO-024]") {
    REQUIRE(encode_mms_read_request(0x10, mms_addr(static_cast<uint8_t>(kMmsMax + 1), 0), 1, 1).empty());
    REQUIRE(encode_mms_read_request(0x10, mms_addr(0, 0), 0, 1).empty());
    REQUIRE(encode_mms_read_request(0x10, mms_addr(0, 0), kMaxBurstWords + 1, 1).empty());
}

TEST_CASE("decode_mms_read_request rejects the wrong bus", "[mdio][REQ-MDIO-024]") {
    const auto frame = encode_mms_read_request(0x10, mms_addr(0, 0), 1, 1);
    MdioMmsAddr out_addr;
    size_t      out_word_count;
    uint8_t      txn;
    REQUIRE(decode_mms_read_request(frame.data(), frame.size(), 0x11, out_addr, out_word_count, txn) ==
            make_error_code(MdioErrc::wrong_bus));
}

TEST_CASE("decode_mms_read_request rejects an invalid decoded MMS address", "[mdio][REQ-MDIO-024]") {
    std::vector<uint8_t> payload(6, 0);
    payload[0] = static_cast<uint8_t>(MdioMode::MmsSingleWord);
    payload[1] = static_cast<uint8_t>(kMmsMax + 1); // out-of-range mms
    payload[5] = 1;                                  // word_count

    rcp::acf::AcfMessageInfo hdr;
    hdr.byte_bus_id  = 0x10;
    hdr.op           = false;
    const auto frame = rcp::acf::encode_acf_abb(hdr, payload);

    MdioMmsAddr out_addr;
    size_t      out_word_count;
    uint8_t      txn;
    REQUIRE(decode_mms_read_request(frame.data(), frame.size(), 0x10, out_addr, out_word_count, txn) ==
            make_error_code(MdioErrc::bad_mms_addr));
}

TEST_CASE("decode_mms_read_request rejects a zero word_count", "[mdio][REQ-MDIO-024]") {
    std::vector<uint8_t> payload(6, 0);
    payload[0] = static_cast<uint8_t>(MdioMode::MmsSingleWord);

    rcp::acf::AcfMessageInfo hdr;
    hdr.byte_bus_id  = 0x10;
    hdr.op           = false;
    const auto frame = rcp::acf::encode_acf_abb(hdr, payload);

    MdioMmsAddr out_addr;
    size_t      out_word_count;
    uint8_t      txn;
    REQUIRE(decode_mms_read_request(frame.data(), frame.size(), 0x10, out_addr, out_word_count, txn) ==
            make_error_code(MdioErrc::bad_word_count));
}

TEST_CASE("decode_mms_read_request rejects a word_count above kMaxBurstWords", "[mdio][REQ-MDIO-024]") {
    std::vector<uint8_t> payload(6, 0);
    payload[0] = static_cast<uint8_t>(MdioMode::MmsSingleWord);
    payload[4] = static_cast<uint8_t>((kMaxBurstWords + 1) >> 8);
    payload[5] = static_cast<uint8_t>((kMaxBurstWords + 1) & 0xFF);

    rcp::acf::AcfMessageInfo hdr;
    hdr.byte_bus_id  = 0x10;
    hdr.op           = false;
    const auto frame = rcp::acf::encode_acf_abb(hdr, payload);

    MdioMmsAddr out_addr;
    size_t      out_word_count;
    uint8_t      txn;
    REQUIRE(decode_mms_read_request(frame.data(), frame.size(), 0x10, out_addr, out_word_count, txn) ==
            make_error_code(MdioErrc::bad_word_count));
}

TEST_CASE("decode_mms_read_request rejects an MMD-mode frame", "[mdio][REQ-MDIO-021][REQ-MDIO-024]") {
    std::vector<uint8_t> payload(6, 0);
    payload[0] = static_cast<uint8_t>(MdioMode::MmdSingleWord);
    payload[5] = 1;

    rcp::acf::AcfMessageInfo hdr;
    hdr.byte_bus_id  = 0x10;
    hdr.op           = false;
    const auto frame = rcp::acf::encode_acf_abb(hdr, payload);

    MdioMmsAddr out_addr;
    size_t      out_word_count;
    uint8_t      txn;
    REQUIRE(decode_mms_read_request(frame.data(), frame.size(), 0x10, out_addr, out_word_count, txn) ==
            make_error_code(MdioErrc::wrong_mdio_mode));
}

TEST_CASE("MMS read response round-trips untimed at 32-bit width", "[mdio][REQ-MDIO-022][REQ-MDIO-024]") {
    const uint32_t words[1] = {0xCAFEBABEu};
    const auto     frame    = encode_mms_read_response(0x10, 0, words, 1, 3, false, 0);
    REQUIRE_FALSE(frame.empty());

    std::vector<uint8_t> out_words;
    size_t                out_word_count;
    bool                  timed;
    uint64_t              ts;
    uint8_t                txn;
    REQUIRE_FALSE(decode_mms_read_response(frame.data(), frame.size(), 0x10, 0, out_words, out_word_count, timed, ts, txn));
    REQUIRE(out_word_count == 1);
    REQUIRE_FALSE(timed);
    REQUIRE(txn == 3);
    REQUIRE(mms_unpack_word_at(0, out_words.data(), 0) == 0xCAFEBABEu);
}

TEST_CASE("MMS read response round-trips timed at 16-bit width", "[mdio][REQ-MDIO-022][REQ-MDIO-024]") {
    const uint32_t words[1] = {0x4242};
    const auto     frame    = encode_mms_read_response(0x10, 5, words, 1, 4, true, 999);

    std::vector<uint8_t> out_words;
    size_t                out_word_count;
    bool                  timed;
    uint64_t              ts;
    uint8_t                txn;
    REQUIRE_FALSE(decode_mms_read_response(frame.data(), frame.size(), 0x10, 5, out_words, out_word_count, timed, ts, txn));
    REQUIRE(timed);
    REQUIRE(ts == 999);
    REQUIRE(mms_unpack_word_at(5, out_words.data(), 0) == 0x4242u);
}

TEST_CASE("MMS read response round-trips an empty word set", "[mdio][REQ-MDIO-022][REQ-MDIO-024]") {
    const auto frame = encode_mms_read_response(0x10, 0, nullptr, 0, 3, false, 0);
    REQUIRE_FALSE(frame.empty());

    std::vector<uint8_t> out_words;
    size_t                out_word_count;
    bool                  timed;
    uint64_t              ts;
    uint8_t                txn;
    REQUIRE_FALSE(decode_mms_read_response(frame.data(), frame.size(), 0x10, 0, out_words, out_word_count, timed, ts, txn));
    REQUIRE(out_word_count == 0);
}

TEST_CASE("decode_mms_read_response rejects the wrong bus", "[mdio][REQ-MDIO-024]") {
    const uint32_t words[1] = {1};
    const auto     frame    = encode_mms_read_response(0x10, 0, words, 1, 1, false, 0);
    std::vector<uint8_t> out_words;
    size_t                out_word_count;
    bool                  timed;
    uint64_t              ts;
    uint8_t                txn;
    REQUIRE(decode_mms_read_response(frame.data(), frame.size(), 0x11, 0, out_words, out_word_count, timed, ts, txn) ==
            make_error_code(MdioErrc::wrong_bus));
}

TEST_CASE("decode_mms_read_response rejects a byte length not a multiple of the 32-bit width",
          "[mdio][REQ-MDIO-022]") {
    rcp::acf::AcfMessageInfo hdr;
    hdr.byte_bus_id  = 0x10;
    hdr.op           = false;
    hdr.rsp          = true;
    const std::vector<uint8_t> odd_payload{0x01, 0x02, 0x03};
    const auto                 frame = rcp::acf::encode_acf_abb(hdr, odd_payload);

    std::vector<uint8_t> out_words;
    size_t                out_word_count;
    bool                  timed;
    uint64_t              ts;
    uint8_t                txn;
    REQUIRE(decode_mms_read_response(frame.data(), frame.size(), 0x10, 0 /* MMS0: 32-bit */, out_words,
                                      out_word_count, timed, ts, txn) == make_error_code(MdioErrc::bad_word_count));
}

// ── MMS write request/response, REQ-MDIO-022/024 ────────────────────────────

TEST_CASE("MMS write request round-trips a single 32-bit word (MMS1)", "[mdio][REQ-MDIO-022][REQ-MDIO-024]") {
    const auto     addr     = mms_addr(1, 0x0010); // MMS1: 32-bit
    const uint32_t words[1] = {0x11223344u};
    const auto     frame    = encode_mms_write_request(0x10, addr, words, 1, 6);
    REQUIRE_FALSE(frame.empty());

    MdioMmsAddr           out_addr;
    std::vector<uint8_t>  out_words;
    size_t                out_word_count = 0;
    uint8_t                txn            = 0;
    REQUIRE_FALSE(decode_mms_write_request(frame.data(), frame.size(), 0x10, out_addr, out_words, out_word_count, txn));
    REQUIRE(out_addr.mms == 1);
    REQUIRE(out_addr.addr == 0x0010);
    REQUIRE(out_word_count == 1);
    REQUIRE(mms_unpack_word_at(1, out_words.data(), 0) == 0x11223344u);
}

TEST_CASE("MMS write request round-trips a 16-bit burst", "[mdio][REQ-MDIO-022][REQ-MDIO-024]") {
    const auto     addr       = mms_addr(6, 0x0200); // not 0/1: 16-bit
    const uint32_t words[3]   = {0x1111, 0x2222, 0x3333};
    const auto     frame      = encode_mms_write_request(0x10, addr, words, 3, 8);

    MdioMmsAddr           out_addr;
    std::vector<uint8_t>  out_words;
    size_t                out_word_count = 0;
    uint8_t                txn            = 0;
    REQUIRE_FALSE(decode_mms_write_request(frame.data(), frame.size(), 0x10, out_addr, out_words, out_word_count, txn));
    REQUIRE(out_word_count == 3);
    REQUIRE(mms_unpack_word_at(6, out_words.data(), 1) == 0x2222u);
}

TEST_CASE("encode_mms_write_request rejects an invalid address or bad word_count",
          "[mdio][REQ-MDIO-024]") {
    const uint32_t word = 1;
    REQUIRE(encode_mms_write_request(0x10, mms_addr(static_cast<uint8_t>(kMmsMax + 1), 0), &word, 1, 1).empty());
    REQUIRE(encode_mms_write_request(0x10, mms_addr(0, 0), nullptr, 0, 1).empty());
    REQUIRE(encode_mms_write_request(0x10, mms_addr(0, 0), nullptr, kMaxBurstWords + 1, 1).empty());
}

TEST_CASE("decode_mms_write_request rejects a read-op frame", "[mdio][REQ-MDIO-024]") {
    const auto frame = encode_mms_read_request(0x10, mms_addr(0, 0), 1, 1);

    MdioMmsAddr           out_addr;
    std::vector<uint8_t>  out_words;
    size_t                out_word_count;
    uint8_t                txn;
    REQUIRE(decode_mms_write_request(frame.data(), frame.size(), 0x10, out_addr, out_words, out_word_count, txn) ==
            make_error_code(MdioErrc::wrong_op));
}

TEST_CASE("decode_mms_write_request rejects zero words after the address prefix", "[mdio][REQ-MDIO-024]") {
    std::vector<uint8_t> payload(4, 0);
    payload[0] = static_cast<uint8_t>(MdioMode::MmsSingleWord);

    rcp::acf::AcfMessageInfo hdr;
    hdr.byte_bus_id  = 0x10;
    hdr.op           = true;
    const auto frame = rcp::acf::encode_acf_abb(hdr, payload);

    MdioMmsAddr           out_addr;
    std::vector<uint8_t>  out_words;
    size_t                out_word_count;
    uint8_t                txn;
    REQUIRE(decode_mms_write_request(frame.data(), frame.size(), 0x10, out_addr, out_words, out_word_count, txn) ==
            make_error_code(MdioErrc::bad_word_count));
}

TEST_CASE("decode_mms_write_request rejects a word_count above kMaxBurstWords at 16-bit width",
          "[mdio][REQ-MDIO-024]") {
    std::vector<uint8_t> payload(1 + 3 + 2 * (kMaxBurstWords + 1), 0);
    payload[0] = static_cast<uint8_t>(MdioMode::MmsMultiWord);
    payload[1] = 2; // mms = 2 -> 16-bit words

    rcp::acf::AcfMessageInfo hdr;
    hdr.byte_bus_id  = 0x10;
    hdr.op           = true;
    const auto frame = rcp::acf::encode_acf_abb(hdr, payload);

    MdioMmsAddr           out_addr;
    std::vector<uint8_t>  out_words;
    size_t                out_word_count;
    uint8_t                txn;
    REQUIRE(decode_mms_write_request(frame.data(), frame.size(), 0x10, out_addr, out_words, out_word_count, txn) ==
            make_error_code(MdioErrc::bad_word_count));
}

TEST_CASE("decode_mms_write_request rejects an MMD-mode frame", "[mdio][REQ-MDIO-021][REQ-MDIO-024]") {
    std::vector<uint8_t> payload(6, 0);
    payload[0] = static_cast<uint8_t>(MdioMode::MmdSingleWord);

    rcp::acf::AcfMessageInfo hdr;
    hdr.byte_bus_id  = 0x10;
    hdr.op           = true;
    const auto frame = rcp::acf::encode_acf_abb(hdr, payload);

    MdioMmsAddr           out_addr;
    std::vector<uint8_t>  out_words;
    size_t                out_word_count;
    uint8_t                txn;
    REQUIRE(decode_mms_write_request(frame.data(), frame.size(), 0x10, out_addr, out_words, out_word_count, txn) ==
            make_error_code(MdioErrc::wrong_mdio_mode));
}

TEST_CASE("MMS write response round-trips", "[mdio][REQ-MDIO-022][REQ-MDIO-024]") {
    const uint32_t accepted[1] = {0xAAAA};
    const auto     frame       = encode_mms_write_response(0x10, 2, accepted, 1, 5, false, 0);
    REQUIRE_FALSE(frame.empty());

    std::vector<uint8_t> out_words;
    size_t                out_word_count;
    bool                  timed;
    uint64_t              ts;
    uint8_t                txn;
    REQUIRE_FALSE(decode_mms_write_response(frame.data(), frame.size(), 0x10, 2, out_words, out_word_count, timed, ts, txn));
    REQUIRE(out_word_count == 1);
    REQUIRE(mms_unpack_word_at(2, out_words.data(), 0) == 0xAAAAu);
}

TEST_CASE("MMS write response round-trips nothing accepted", "[mdio][REQ-MDIO-022][REQ-MDIO-024]") {
    const auto frame = encode_mms_write_response(0x10, 0, nullptr, 0, 5, false, 0);
    REQUIRE_FALSE(frame.empty());

    std::vector<uint8_t> out_words;
    size_t                out_word_count;
    bool                  timed;
    uint64_t              ts;
    uint8_t                txn;
    REQUIRE_FALSE(decode_mms_write_response(frame.data(), frame.size(), 0x10, 0, out_words, out_word_count, timed, ts, txn));
    REQUIRE(out_word_count == 0);
}

// ── MdioEndpoint convenience wrapper (source-compatible with rcp/mock.hpp) ──

TEST_CASE("payload_width_bits is 16 for both MMD sub-modes regardless of mms_is_0_or_1",
          "[mdio][REQ-MDIO-002]") {
    REQUIRE(payload_width_bits(MdioMode::MmdSingleWord, false) == 16);
    REQUIRE(payload_width_bits(MdioMode::MmdSingleWord, true) == 16);
    REQUIRE(payload_width_bits(MdioMode::MmdMultiWord, false) == 16);
    REQUIRE(payload_width_bits(MdioMode::MmdMultiWord, true) == 16);
}

TEST_CASE("payload_width_bits is 16 for MMS single word access", "[mdio][REQ-MDIO-002]") {
    REQUIRE(payload_width_bits(MdioMode::MmsSingleWord, false) == 16);
    REQUIRE(payload_width_bits(MdioMode::MmsSingleWord, true) == 16);
}

TEST_CASE("payload_width_bits is 32 for MMS multi-word access only on MMS0/MMS1",
          "[mdio][REQ-MDIO-002]") {
    REQUIRE(payload_width_bits(MdioMode::MmsMultiWord, true) == 32);
    REQUIRE(payload_width_bits(MdioMode::MmsMultiWord, false) == 16);
}

TEST_CASE("validate_request accepts a payload within its mode's width", "[mdio][REQ-MDIO-002]") {
    MdioRequest req;
    req.mode         = MdioMode::MmdSingleWord;
    req.mdio_payload = 0xFFFF;
    REQUIRE_FALSE(validate_request(req));

    req.mode          = MdioMode::MmsMultiWord;
    req.mms_is_0_or_1 = true;
    req.mdio_payload  = 0xFFFFFFFFu;
    REQUIRE_FALSE(validate_request(req));
}

TEST_CASE("validate_request rejects a payload exceeding its mode's width", "[mdio][REQ-MDIO-002]") {
    MdioRequest req;
    req.mode         = MdioMode::MmdSingleWord;
    req.mdio_payload = 0x10000; // exceeds 16 bits
    REQUIRE(validate_request(req) == make_error_code(MdioErrc::payload_exceeds_mode_width));

    MdioRequest req2;
    req2.mode          = MdioMode::MmsMultiWord;
    req2.mms_is_0_or_1 = false; // 16-bit width for non-MMS0/1
    req2.mdio_payload  = 0x10000;
    REQUIRE(validate_request(req2) == make_error_code(MdioErrc::payload_exceeds_mode_width));
}

TEST_CASE("MdioEndpoint::transact write then read round-trips the value", "[mdio][REQ-MDIO-003]") {
    MdioEndpoint ep;
    MdioRequest  write_req;
    write_req.mode         = MdioMode::MmdSingleWord;
    write_req.mdio_address = 5;
    write_req.is_write     = true;
    write_req.mdio_payload = 0xBEEF;

    MdioResponse out;
    REQUIRE_FALSE(ep.transact(write_req, out));
    REQUIRE(out.mdio_payload == 0xBEEF);

    MdioRequest read_req  = write_req;
    read_req.is_write      = false;
    read_req.mdio_payload  = 0;
    REQUIRE_FALSE(ep.transact(read_req, out));
    REQUIRE(out.mdio_payload == 0xBEEF);
}

TEST_CASE("MdioEndpoint::transact reads an unwritten register as zero", "[mdio][REQ-MDIO-003]") {
    MdioEndpoint ep;
    MdioRequest  req;
    req.mode         = MdioMode::MmsSingleWord;
    req.mdio_address = 1;
    req.is_write      = false;

    MdioResponse out;
    REQUIRE_FALSE(ep.transact(req, out));
    REQUIRE(out.mdio_payload == 0);
}

TEST_CASE("MdioEndpoint::transact keys on mdio_mode in addition to mdio_address, "
          "so different modes at the same address never collide",
          "[mdio][REQ-MDIO-004]") {
    MdioEndpoint ep;

    MdioRequest mmd_write;
    mmd_write.mode         = MdioMode::MmdSingleWord;
    mmd_write.mdio_address = 4;
    mmd_write.is_write     = true;
    mmd_write.mdio_payload = 0x1111;
    MdioResponse out;
    REQUIRE_FALSE(ep.transact(mmd_write, out));

    MdioRequest mms_read;
    mms_read.mode         = MdioMode::MmsSingleWord;
    mms_read.mdio_address = 4;
    mms_read.is_write      = false;
    REQUIRE_FALSE(ep.transact(mms_read, out));
    REQUIRE(out.mdio_payload == 0);

    MdioRequest mms_write = mms_read;
    mms_write.is_write     = true;
    mms_write.mdio_payload = 0x2222;
    REQUIRE_FALSE(ep.transact(mms_write, out));
    REQUIRE(out.mdio_payload == 0x2222);

    MdioRequest mmd_read = mmd_write;
    mmd_read.is_write     = false;
    REQUIRE_FALSE(ep.transact(mmd_read, out));
    REQUIRE(out.mdio_payload == 0x1111);
}

TEST_CASE("MdioErrc reports a non-empty message in its own category", "[mdio][REQ-MDIO-005]") {
    auto ec = make_error_code(MdioErrc::payload_exceeds_mode_width);
    REQUIRE(ec.category() == mdio_category());
    REQUIRE_FALSE(ec.message().empty());
}

TEST_CASE("MdioEndpoint::handle_request delegates a Plain (evt[2:0]==000b) request to transact()",
          "[mdio][REQ-MDIO-006]") {
    MdioEndpoint ep;
    MdioRequest  req;
    req.mode         = MdioMode::MmdSingleWord;
    req.mdio_address = 5;
    req.is_write      = true;
    req.mdio_payload  = 0xBEEF;

    MdioResponse out;
    auto ec = ep.handle_request(/*evt_op=*/0, req, out);
    REQUIRE_FALSE(ec);
    REQUIRE(out.mdio_payload == 0xBEEF);
    REQUIRE(ep.last_request().mdio_address == 5);
}

TEST_CASE("MdioEndpoint::handle_request rejects every reserved evt[2:0] value (001b-110b) "
          "without recording anything",
          "[mdio][REQ-MDIO-006]") {
    for (uint8_t evt_op = 1; evt_op <= 6; ++evt_op) {
        MdioEndpoint ep;
        MdioRequest  req;
        req.mode          = MdioMode::MmdSingleWord;
        req.mdio_address  = 9;
        req.is_write      = true;
        req.mdio_payload  = 0x1234;

        MdioResponse out;
        auto ec = ep.handle_request(evt_op, req, out);
        REQUIRE(ec == rcp::endpoint::make_error_code(rcp::endpoint::EndpointErrc::reserved_evt_row2));
        REQUIRE(ep.last_request().mdio_address == 0);

        MdioRequest read_back = req;
        read_back.is_write     = false;
        read_back.mdio_payload = 0;
        REQUIRE_FALSE(ep.handle_request(/*evt_op=*/0, read_back, out));
        REQUIRE(out.mdio_payload == 0);
    }
}

TEST_CASE("MdioEndpoint::handle_request reports config_write_not_supported for evt[2:0]==111b "
          "without crashing or recording anything",
          "[mdio][REQ-MDIO-007]") {
    MdioEndpoint ep;
    MdioRequest  req;
    req.mode          = MdioMode::MmsSingleWord;
    req.mdio_address  = 2;
    req.is_write      = true;
    req.mdio_payload  = 0x00FF;

    MdioResponse out;
    auto ec = ep.handle_request(/*evt_op=*/7, req, out);
    REQUIRE(ec == make_error_code(MdioErrc::config_write_not_supported));
    REQUIRE(ep.last_request().mdio_address == 0);
}

TEST_CASE("MdioEndpoint::handle_request masks evt_op down to 3 bits before classifying",
          "[mdio][REQ-MDIO-006]") {
    MdioEndpoint ep;
    MdioRequest  req;
    MdioResponse out;
    REQUIRE_FALSE(ep.handle_request(/*evt_op=*/0xF8, req, out)); // low 3 bits 000 -> Plain
    auto ec = ep.handle_request(/*evt_op=*/0xF9, req, out);      // low 3 bits 001 -> Reserved
    REQUIRE(ec == rcp::endpoint::make_error_code(rcp::endpoint::EndpointErrc::reserved_evt_row2));
}

TEST_CASE("MdioEndpoint::handle_request Reserved/ConfigWrite classification is independent of "
          "mode/mdio_address/mdio_payload — evt[2:0] carries no field-value selector",
          "[mdio][REQ-MDIO-006]") {
    MdioEndpoint ep;
    MdioRequest  req;
    req.mode          = MdioMode::MmsMultiWord;
    req.mms_is_0_or_1 = true;
    req.mdio_address  = 0xFFFF;
    req.is_write      = true;
    req.mdio_payload  = 0xFFFFFFFFu;

    MdioResponse out;
    REQUIRE(ep.handle_request(/*evt_op=*/3, req, out) ==
            rcp::endpoint::make_error_code(rcp::endpoint::EndpointErrc::reserved_evt_row2));
    REQUIRE(ep.handle_request(/*evt_op=*/7, req, out) == make_error_code(MdioErrc::config_write_not_supported));
}

TEST_CASE("MdioErrc::config_write_not_supported reports a non-empty message in its own category",
          "[mdio][REQ-MDIO-007]") {
    auto ec = make_error_code(MdioErrc::config_write_not_supported);
    REQUIRE(ec.category() == mdio_category());
    REQUIRE_FALSE(ec.message().empty());
}
