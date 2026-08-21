// fusa:test REQ-ISELED-001
// fusa:test REQ-ISELED-002
// fusa:test REQ-ISELED-003
// fusa:test REQ-ISELED-004
// fusa:test REQ-ISELED-005
// fusa:test REQ-ISELED-006
// fusa:test REQ-ISELED-007
// fusa:test REQ-ISELED-008
// fusa:test REQ-ISELED-009
// fusa:test REQ-ISELED-010
// fusa:test REQ-ISELED-011
// fusa:test REQ-ISELED-012
// fusa:test REQ-ISELED-013
// fusa:test REQ-ISELED-014
// fusa:test REQ-ISELED-015
// fusa:test REQ-ISELED-016
// fusa:test REQ-ISELED-017
// fusa:test REQ-ISELED-018
// fusa:test REQ-ISELED-019
// fusa:test REQ-ISELED-020
// fusa:test REQ-ISELED-021
// fusa:test REQ-ISELED-022
// fusa:test REQ-ISELED-023
// fusa:test REQ-ISELED-024
// fusa:test REQ-ISELED-025
// fusa:test REQ-ISELED-026
// fusa:test REQ-ISELED-027
// fusa:test REQ-ISELED-029
// fusa:test REQ-ISELED-030
// fusa:test REQ-ISELED-031
// fusa:test REQ-ISELED-032
// fusa:test REQ-ISELED-033
// fusa:test REQ-ISELED-034
// fusa:test REQ-ISELED-035
// fusa:test REQ-ISELED-036
// fusa:test REQ-ISELED-037
// fusa:test REQ-ISELED-038
// fusa:test REQ-ISELED-039
// fusa:test REQ-ISELED-040
// fusa:test REQ-ISELED-041
// fusa:test REQ-ISELED-042

// Tests for rcp/iseled.hpp — the ISELED endpoint type (ep_type 0x0C), ported
// from c-RCP's tests/test_ep_iseled.c (this project's RC5-spec-conformant
// reference) as part of Phase 3 of the ground-up rewrite (cpp-RCP issue
// #129, ROADMAP.md "Phase 17").

#include <catch2/catch_test_macros.hpp>
#include <rcp/acf.hpp>
#include <rcp/endpoint.hpp>
#include <rcp/fragment.hpp>
#include <rcp/iseled.hpp>
#include <rcp/lifecycle.hpp>

using namespace rcp::iseled;

// ── ep_type id ────────────────────────────────────────────────────────────────

TEST_CASE("ISELED's ep_type id is 0x0C", "[iseled][REQ-ISELED-001]") {
    REQUIRE(rcp::endpoint::kEndpointTypeIseled == 0x0C);
}

// ── symbol_encode / symbol_decode ─────────────────────────────────────────────

TEST_CASE("symbol_encode/symbol_decode round-trip every nibble", "[iseled][REQ-ISELED-001]") {
    for (uint8_t nibble = 0; nibble <= 0x0F; ++nibble) {
        uint8_t symbol = symbol_encode(nibble);
        REQUIRE(symbol <= 0x1F);
        uint8_t decoded = 0xFF;
        REQUIRE(symbol_decode(symbol, decoded));
        REQUIRE(decoded == nibble);
    }
}

TEST_CASE("symbol_encode masks high bits of its input", "[iseled][REQ-ISELED-001]") {
    REQUIRE(symbol_encode(0x05) == symbol_encode(0xF5));
}

TEST_CASE("symbol_encode gives 0x0 and 0xF distinct parity", "[iseled][REQ-ISELED-001]") {
    REQUIRE(symbol_encode(0x0) != symbol_encode(0xF));
}

TEST_CASE("symbol_decode rejects a corrupted parity bit", "[iseled][REQ-ISELED-002]") {
    uint8_t valid = symbol_encode(0x03);
    uint8_t corrupted = static_cast<uint8_t>(valid ^ 0x10);
    uint8_t decoded = 0xFF;
    REQUIRE_FALSE(symbol_decode(corrupted, decoded));
}

TEST_CASE("symbol_decode accepts valid parity and sets the nibble", "[iseled][REQ-ISELED-032]") {
    uint8_t symbol = symbol_encode(0x0A);
    uint8_t decoded = 0xFF;
    REQUIRE(symbol_decode(symbol, decoded));
    REQUIRE(decoded == 0x0A);
}

// ── bitframe_encoded_len ──────────────────────────────────────────────────────

TEST_CASE("bitframe_encoded_len computes 2*(data_len + crc)", "[iseled][REQ-ISELED-003]") {
    REQUIRE(bitframe_encoded_len(0, false) == 0);
    REQUIRE(bitframe_encoded_len(0, true) == 2);
    REQUIRE(bitframe_encoded_len(3, false) == 6);
    REQUIRE(bitframe_encoded_len(3, true) == 8);
}

// ── encode_bitframe / decode_bitframe round trips ─────────────────────────────

TEST_CASE("encode_bitframe/decode_bitframe round-trip without CRC", "[iseled][REQ-ISELED-004]") {
    const std::vector<uint8_t> data{0x12, 0xAB, 0x00};
    auto framed = encode_bitframe(data, false);
    REQUIRE(framed.size() == 6);
    for (auto b : framed) REQUIRE(b <= 0x1F);

    std::vector<uint8_t> decoded;
    REQUIRE_FALSE(decode_bitframe(framed.data(), framed.size(), false, decoded));
    REQUIRE(decoded == data);
}

TEST_CASE("encode_bitframe/decode_bitframe round-trip with CRC", "[iseled][REQ-ISELED-005]") {
    const std::vector<uint8_t> data{0xDE, 0xAD, 0xBE, 0xEF};
    auto framed = encode_bitframe(data, true);
    REQUIRE(framed.size() == 10); // (4 + 1 trailer) * 2

    std::vector<uint8_t> decoded;
    REQUIRE_FALSE(decode_bitframe(framed.data(), framed.size(), true, decoded));
    REQUIRE(decoded == data);
}

TEST_CASE("encode_bitframe/decode_bitframe handle an empty, no-CRC buffer", "[iseled][REQ-ISELED-020]") {
    auto framed = encode_bitframe({}, false);
    REQUIRE(framed.empty());

    std::vector<uint8_t> decoded{1, 2, 3}; // start non-empty
    REQUIRE_FALSE(decode_bitframe(nullptr, 0, false, decoded));
    REQUIRE(decoded.empty());
}

TEST_CASE("decode_bitframe rejects an odd symbol count", "[iseled][REQ-ISELED-016]") {
    const uint8_t symbols[3] = {0, 0, 0};
    std::vector<uint8_t> decoded;
    REQUIRE(decode_bitframe(symbols, sizeof(symbols), false, decoded) ==
            make_error_code(IseledErrc::odd_symbol_count));
    REQUIRE(decoded.empty());
}

TEST_CASE("decode_bitframe rejects a bad hi-nibble symbol", "[iseled][REQ-ISELED-017]") {
    const std::vector<uint8_t> data{0x42};
    auto framed = encode_bitframe(data, false);
    framed[0] = static_cast<uint8_t>(framed[0] ^ 0x10); // flip hi symbol's parity bit

    std::vector<uint8_t> decoded;
    REQUIRE(decode_bitframe(framed.data(), framed.size(), false, decoded) ==
            make_error_code(IseledErrc::bad_symbol));
    REQUIRE(decoded.empty());
}

// MC/DC: the hi-symbol arm above always short-circuits before the lo-symbol
// operand is evaluated — isolate it with an otherwise-valid hi symbol.
TEST_CASE("decode_bitframe rejects a bad lo-nibble symbol with a valid hi symbol",
          "[iseled][REQ-ISELED-017]") {
    const std::vector<uint8_t> data{0x42};
    auto framed = encode_bitframe(data, false);
    framed[1] = static_cast<uint8_t>(framed[1] ^ 0x10); // flip lo symbol's parity bit only

    std::vector<uint8_t> decoded;
    REQUIRE(decode_bitframe(framed.data(), framed.size(), false, decoded) ==
            make_error_code(IseledErrc::bad_symbol));
    REQUIRE(decoded.empty());
}

TEST_CASE("decode_bitframe rejects a CRC mismatch", "[iseled][REQ-ISELED-019]") {
    const std::vector<uint8_t> data{0x11, 0x22};
    auto framed = encode_bitframe(data, true);
    framed[1] = symbol_encode(0x0F); // corrupt the first content octet's lo nibble

    std::vector<uint8_t> decoded;
    REQUIRE(decode_bitframe(framed.data(), framed.size(), true, decoded) ==
            make_error_code(IseledErrc::crc_mismatch));
    REQUIRE(decoded.empty());
}

TEST_CASE("decode_bitframe rejects a too-short frame when a CRC trailer is expected",
          "[iseled][REQ-ISELED-018]") {
    std::vector<uint8_t> decoded;
    REQUIRE(decode_bitframe(nullptr, 0, true, decoded) == make_error_code(IseledErrc::short_frame));
    REQUIRE(decoded.empty());
}

// ── crc8 ───────────────────────────────────────────────────────────────────────

TEST_CASE("crc8 is deterministic", "[iseled][REQ-ISELED-006]") {
    const std::vector<uint8_t> a{0x01, 0x02, 0x03};
    REQUIRE(crc8(a) == crc8(a));
}

TEST_CASE("crc8 differs for different content", "[iseled][REQ-ISELED-034]") {
    const std::vector<uint8_t> a{0x01, 0x02, 0x03};
    const std::vector<uint8_t> b{0x01, 0x02, 0x04};
    REQUIRE(crc8(a) != crc8(b));
}

TEST_CASE("crc8 of an empty input is 0x00", "[iseled][REQ-ISELED-033]") {
    REQUIRE(crc8(nullptr, 0) == 0x00);
}

// ── requires_isp_n ─────────────────────────────────────────────────────────────

TEST_CASE("requires_isp_n is true iff use_rcv_clk is true", "[iseled][REQ-ISELED-007]") {
    REQUIRE(requires_isp_n(true));
    REQUIRE_FALSE(requires_isp_n(false));
}

// ── Transmission-complete trigger ─────────────────────────────────────────────

TEST_CASE("trigger_fires(None, ...) is always false", "[iseled][REQ-ISELED-008]") {
    REQUIRE_FALSE(trigger_fires(IseledTrigger::None, true));
    REQUIRE_FALSE(trigger_fires(IseledTrigger::None, false));
}

TEST_CASE("trigger_fires(TxComplete, ...) passes tx_complete_event through", "[iseled][REQ-ISELED-035]") {
    REQUIRE(trigger_fires(IseledTrigger::TxComplete, true));
    REQUIRE_FALSE(trigger_fires(IseledTrigger::TxComplete, false));
}

// ── Functional config ─────────────────────────────────────────────────────────

TEST_CASE("iseled_functional_cfg_init zeroes every field", "[iseled][REQ-ISELED-009]") {
    IseledFunctionalCfg cfg;
    cfg.ep_enable = true;
    cfg.bit_clk_divider = 42;
    cfg.use_rcv_clk = cfg.crc_enable = true;
    cfg.trigger = static_cast<uint8_t>(IseledTrigger::TxComplete);
    cfg.base_clk = cfg.ep_status = cfg.nr_leds = cfg.rcv_timeout = 0xBEEF;
    cfg.wire_clk_divider = 0xAA;
    cfg.collect_resp = true;

    iseled_functional_cfg_init(cfg);

    REQUIRE_FALSE(cfg.ep_enable);
    REQUIRE_FALSE(cfg.ep_clear_req_storage);
    REQUIRE_FALSE(cfg.ep_req_crc_enable);
    REQUIRE_FALSE(cfg.ep_response_ts_enable);
    REQUIRE_FALSE(cfg.ep_suppress_response);
    REQUIRE(cfg.bit_clk_divider == 0);
    REQUIRE_FALSE(cfg.use_rcv_clk);
    REQUIRE_FALSE(cfg.crc_enable);
    REQUIRE(cfg.trigger == static_cast<uint8_t>(IseledTrigger::None));
    REQUIRE(cfg.base_clk == 0);
    REQUIRE(cfg.ep_status == 0);
    REQUIRE(cfg.wire_clk_divider == 0);
    REQUIRE_FALSE(cfg.collect_resp);
    REQUIRE(cfg.nr_leds == 0);
    REQUIRE(cfg.rcv_timeout == 0);
}

TEST_CASE("iseled_functional_cfg_writable is false in HwUnconfigured", "[iseled][REQ-ISELED-010]") {
    rcp::lifecycle::WriterCtx writer;
    writer.via_root_client_ep0 = true;
    writer.via_owning_stream   = true;
    REQUIRE_FALSE(iseled_functional_cfg_writable(rcp::lifecycle::ServerState::HwUnconfigured, writer));
}

TEST_CASE("iseled_functional_cfg_writable in HwConfigured requires EP0/owning-stream/discovery-stream",
          "[iseled][REQ-ISELED-010]") {
    rcp::lifecycle::WriterCtx none, via_ep0, via_stream, via_discovery;
    via_ep0.via_root_client_ep0        = true;
    via_stream.via_owning_stream       = true;
    via_discovery.via_discovery_stream = true;

    REQUIRE_FALSE(iseled_functional_cfg_writable(rcp::lifecycle::ServerState::HwConfigured, none));
    REQUIRE(iseled_functional_cfg_writable(rcp::lifecycle::ServerState::HwConfigured, via_ep0));
    REQUIRE(iseled_functional_cfg_writable(rcp::lifecycle::ServerState::HwConfigured, via_stream));
    REQUIRE(iseled_functional_cfg_writable(rcp::lifecycle::ServerState::HwConfigured, via_discovery));
}

TEST_CASE("iseled_functional_cfg_writable in RcpConfigured requires EP0/owning-stream, not discovery",
          "[iseled][REQ-ISELED-010]") {
    rcp::lifecycle::WriterCtx none, via_ep0, via_stream;
    via_ep0.via_root_client_ep0  = true;
    via_stream.via_owning_stream = true;

    REQUIRE_FALSE(iseled_functional_cfg_writable(rcp::lifecycle::ServerState::RcpConfigured, none));
    REQUIRE(iseled_functional_cfg_writable(rcp::lifecycle::ServerState::RcpConfigured, via_ep0));
    REQUIRE(iseled_functional_cfg_writable(rcp::lifecycle::ServerState::RcpConfigured, via_stream));
}

TEST_CASE("set_bit_clk_divider rejects an unauthorized writer", "[iseled][REQ-ISELED-011]") {
    IseledFunctionalCfg cfg;
    iseled_functional_cfg_init(cfg);
    rcp::lifecycle::WriterCtx none;
    REQUIRE_FALSE(set_bit_clk_divider(cfg, 42, rcp::lifecycle::ServerState::RcpConfigured, none));
    REQUIRE(cfg.bit_clk_divider == 0);
}

TEST_CASE("set_bit_clk_divider applies when authorized", "[iseled][REQ-ISELED-036]") {
    IseledFunctionalCfg cfg;
    iseled_functional_cfg_init(cfg);
    rcp::lifecycle::WriterCtx writer;
    writer.via_owning_stream = true;
    REQUIRE(set_bit_clk_divider(cfg, 42, rcp::lifecycle::ServerState::HwConfigured, writer));
    REQUIRE(cfg.bit_clk_divider == 42);
}

TEST_CASE("set_use_rcv_clk rejects an unauthorized writer", "[iseled][REQ-ISELED-012]") {
    IseledFunctionalCfg cfg;
    iseled_functional_cfg_init(cfg);
    rcp::lifecycle::WriterCtx none;
    REQUIRE_FALSE(set_use_rcv_clk(cfg, true, rcp::lifecycle::ServerState::RcpConfigured, none));
    REQUIRE_FALSE(cfg.use_rcv_clk);
}

TEST_CASE("set_use_rcv_clk applies when authorized", "[iseled][REQ-ISELED-037]") {
    IseledFunctionalCfg cfg;
    iseled_functional_cfg_init(cfg);
    rcp::lifecycle::WriterCtx writer;
    writer.via_owning_stream = true;
    REQUIRE(set_use_rcv_clk(cfg, true, rcp::lifecycle::ServerState::HwConfigured, writer));
    REQUIRE(cfg.use_rcv_clk);
}

TEST_CASE("set_crc_enable rejects an unauthorized writer", "[iseled][REQ-ISELED-013]") {
    IseledFunctionalCfg cfg;
    iseled_functional_cfg_init(cfg);
    rcp::lifecycle::WriterCtx none;
    REQUIRE_FALSE(set_crc_enable(cfg, true, rcp::lifecycle::ServerState::RcpConfigured, none));
    REQUIRE_FALSE(cfg.crc_enable);
}

TEST_CASE("set_crc_enable applies when authorized", "[iseled][REQ-ISELED-038]") {
    IseledFunctionalCfg cfg;
    iseled_functional_cfg_init(cfg);
    rcp::lifecycle::WriterCtx writer;
    writer.via_owning_stream = true;
    REQUIRE(set_crc_enable(cfg, true, rcp::lifecycle::ServerState::HwConfigured, writer));
    REQUIRE(cfg.crc_enable);
}

TEST_CASE("set_trigger rejects an unauthorized writer", "[iseled][REQ-ISELED-014]") {
    IseledFunctionalCfg cfg;
    iseled_functional_cfg_init(cfg);
    rcp::lifecycle::WriterCtx none;
    REQUIRE_FALSE(set_trigger(cfg, IseledTrigger::TxComplete, rcp::lifecycle::ServerState::RcpConfigured, none));
    REQUIRE(cfg.trigger == static_cast<uint8_t>(IseledTrigger::None));
}

TEST_CASE("set_trigger applies when authorized", "[iseled][REQ-ISELED-039]") {
    IseledFunctionalCfg cfg;
    iseled_functional_cfg_init(cfg);
    rcp::lifecycle::WriterCtx writer;
    writer.via_owning_stream = true;
    REQUIRE(set_trigger(cfg, IseledTrigger::TxComplete, rcp::lifecycle::ServerState::HwConfigured, writer));
    REQUIRE(cfg.trigger == static_cast<uint8_t>(IseledTrigger::TxComplete));
}

// ── The EP_func register block ──────────────────────────────────────────────

TEST_CASE("render_registers matches the corrected Table 58 offsets", "[iseled][REQ-ISELED-029]") {
    IseledFunctionalCfg cfg;
    iseled_functional_cfg_init(cfg);
    cfg.ep_enable      = true;
    cfg.ep_status      = 0x1234;
    cfg.wire_clk_divider = 0x55;
    cfg.collect_resp     = true;
    cfg.use_rcv_clk      = true;
    cfg.nr_leds           = 0xABCD;
    cfg.rcv_timeout        = 0x9876;

    const auto out = render_registers(cfg);

    REQUIRE(out[kRegEpLen] == static_cast<uint8_t>(kEpFuncLen));
    REQUIRE(out[kRegReserved01] == 0);
    REQUIRE((out[kRegEpEnableClr] & 0x01) != 0);
    REQUIRE(out[kRegBaseClk] == 0);
    REQUIRE(out[kRegBaseClk + 1] == 0);
    REQUIRE(out[kRegEpStatus] == 0x12);
    REQUIRE(out[kRegEpStatus + 1] == 0x34);
    REQUIRE(out[kRegClkDivider] == 0x55);
    REQUIRE((out[kRegFlags] & kFlagCollectResp) != 0);
    REQUIRE((out[kRegFlags] & kFlagUseRcvClk) != 0);
    REQUIRE(out[kRegNrLeds] == 0xAB);
    REQUIRE(out[kRegNrLeds + 1] == 0xCD);
    REQUIRE(out[kRegRcvTimeout] == 0x98);
    REQUIRE(out[kRegRcvTimeout + 1] == 0x76);
    REQUIRE(kEpFuncLen == 0x000Eu);
}

TEST_CASE("render_registers never touches crc_enable", "[iseled][REQ-ISELED-029]") {
    IseledFunctionalCfg off, on;
    iseled_functional_cfg_init(off);
    iseled_functional_cfg_init(on);
    on.crc_enable = true;

    REQUIRE(render_registers(off) == render_registers(on));
}

TEST_CASE("apply_reconfig writes a multi-register span", "[iseled][REQ-ISELED-029]") {
    IseledFunctionalCfg cfg;
    iseled_functional_cfg_init(cfg);
    const uint8_t payload[8] = {0x00, static_cast<uint8_t>(kRegEpStatus),
                                 0xAB, 0xCD,             // ep_status
                                 0x11,                   // wire_clk_divider
                                 kFlagCollectResp,        // flags
                                 0x22, 0x33};             // nr_leds

    REQUIRE_FALSE(apply_reconfig(cfg, payload, sizeof(payload)));
    REQUIRE(cfg.ep_status == 0xABCD);
    REQUIRE(cfg.wire_clk_divider == 0x11);
    REQUIRE(cfg.collect_resp);
    REQUIRE_FALSE(cfg.use_rcv_clk);
    REQUIRE(cfg.nr_leds == 0x2233);
}

TEST_CASE("apply_reconfig ignores read-only registers (EP_LEN/reserved)", "[iseled][REQ-ISELED-029]") {
    IseledFunctionalCfg cfg;
    iseled_functional_cfg_init(cfg);
    const uint8_t payload[6] = {0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF};

    REQUIRE_FALSE(apply_reconfig(cfg, payload, sizeof(payload)));

    const auto out = render_registers(cfg);
    REQUIRE(out[kRegEpLen] == static_cast<uint8_t>(kEpFuncLen));
    REQUIRE(out[kRegReserved01] == 0);
    REQUIRE(out[kRegBaseClk] == 0);
    REQUIRE(out[kRegBaseClk + 1] == 0);
}

TEST_CASE("apply_reconfig ignores base_clk's own two octets individually", "[iseled][REQ-ISELED-029]") {
    IseledFunctionalCfg cfg;
    iseled_functional_cfg_init(cfg);
    {
        const uint8_t payload[3] = {0x00, static_cast<uint8_t>(kRegBaseClk), 0xFF};
        REQUIRE_FALSE(apply_reconfig(cfg, payload, sizeof(payload)));
        REQUIRE(cfg.base_clk == 0);
        REQUIRE(cfg.wire_clk_divider == 0);
    }
    {
        const uint8_t payload[3] = {0x00, static_cast<uint8_t>(kRegBaseClk + 1), 0xFF};
        REQUIRE_FALSE(apply_reconfig(cfg, payload, sizeof(payload)));
        REQUIRE(cfg.base_clk == 0);
        REQUIRE(cfg.wire_clk_divider == 0);
    }
}

TEST_CASE("apply_reconfig rejects a write extending past EP_LEN", "[iseled][REQ-ISELED-029]") {
    IseledFunctionalCfg cfg;
    iseled_functional_cfg_init(cfg);
    const uint8_t payload[3] = {0x00, static_cast<uint8_t>(kEpFuncLen), 0xFF};

    REQUIRE(apply_reconfig(cfg, payload, sizeof(payload)) ==
            make_error_code(IseledReconfigErrc::out_of_range));
    REQUIRE(cfg.rcv_timeout == 0);
}

TEST_CASE("apply_reconfig rejects a payload with no data octet", "[iseled][REQ-ISELED-029]") {
    IseledFunctionalCfg cfg;
    iseled_functional_cfg_init(cfg);
    const uint8_t addr_only[2] = {0x00, 0x08};

    REQUIRE(apply_reconfig(cfg, addr_only, sizeof(addr_only)) ==
            make_error_code(IseledReconfigErrc::short_payload));
    REQUIRE(apply_reconfig(cfg, nullptr, 0) == make_error_code(IseledReconfigErrc::short_payload));
}

TEST_CASE("encode_reconfig_request round-trips through acf::decode_acf_abb", "[iseled][REQ-ISELED-029]") {
    const std::vector<uint8_t> data{0xAB, 0xCD};
    const auto frame = encode_reconfig_request(0x03, 0x0006, data, 7);
    REQUIRE_FALSE(frame.empty());

    rcp::acf::AcfMessageInfo hdr;
    std::vector<uint8_t> payload;
    REQUIRE_FALSE(rcp::acf::decode_acf_abb(frame.data(), frame.size(), hdr, payload));
    REQUIRE(hdr.byte_bus_id == 0x03);
    REQUIRE(hdr.op);
    REQUIRE(hdr.evt_op == 0x7);
    REQUIRE(hdr.transaction_num == 7);
    REQUIRE(payload == std::vector<uint8_t>{0x00, 0x06, 0xAB, 0xCD});
}

TEST_CASE("encode_reconfig_request rejects empty data", "[iseled][REQ-ISELED-029]") {
    REQUIRE(encode_reconfig_request(0x00, 0, {}, 0).empty());
}

TEST_CASE("reconfig error category reports a distinct, non-empty message per code",
          "[iseled][REQ-ISELED-042]") {
    auto short_ec = make_error_code(IseledReconfigErrc::short_payload);
    auto range_ec = make_error_code(IseledReconfigErrc::out_of_range);
    REQUIRE_FALSE(short_ec.message().empty());
    REQUIRE_FALSE(range_ec.message().empty());
    REQUIRE(short_ec.message() != range_ec.message());
}

// ── IseledErrc category sanity ────────────────────────────────────────────────

TEST_CASE("IseledErrc reports a non-empty, distinct message per code", "[iseled][REQ-ISELED-015]") {
    const IseledErrc codes[] = {
        IseledErrc::short_frame,      IseledErrc::bad_msg_type, IseledErrc::wrong_bus,
        IseledErrc::wrong_op,         IseledErrc::bad_symbol,   IseledErrc::crc_mismatch,
        IseledErrc::odd_symbol_count, IseledErrc::bad_evt,      IseledErrc::config_write_not_supported,
    };
    std::vector<std::string> seen;
    for (auto c : codes) {
        auto ec = make_error_code(c);
        REQUIRE(ec.category() == iseled_category());
        REQUIRE_FALSE(ec.message().empty());
        for (const auto& s : seen) REQUIRE(s != ec.message());
        seen.push_back(ec.message());
    }
    REQUIRE_FALSE(make_error_code(static_cast<IseledErrc>(999)).message().empty());
}

// ── Command request round trip (write direction) ──────────────────────────────

TEST_CASE("encode_command_request/decode_command_request round-trip raw bytes",
          "[iseled][REQ-ISELED-021]") {
    const std::vector<uint8_t> tx{0x01, 0x02, 0x10, 0x20};
    const auto frame = encode_command_request(6, tx, 7);
    REQUIRE_FALSE(frame.empty());

    std::vector<uint8_t> out_tx;
    uint8_t txn = 0;
    REQUIRE_FALSE(decode_command_request(frame.data(), frame.size(), 6, out_tx, txn));
    REQUIRE(out_tx == tx);
    REQUIRE(txn == 7);
}

TEST_CASE("encode_command_request/decode_command_request round-trip an empty payload",
          "[iseled][REQ-ISELED-022]") {
    const auto frame = encode_command_request(1, {}, 1);
    std::vector<uint8_t> out_tx{1};
    uint8_t txn = 0;
    REQUIRE_FALSE(decode_command_request(frame.data(), frame.size(), 1, out_tx, txn));
    REQUIRE(out_tx.empty());
}

TEST_CASE("decode_command_request rejects the wrong bus", "[iseled][REQ-ISELED-022]") {
    const std::vector<uint8_t> tx{0xAB};
    const auto frame = encode_command_request(4, tx, 0);
    std::vector<uint8_t> out_tx;
    uint8_t txn;
    REQUIRE(decode_command_request(frame.data(), frame.size(), 5, out_tx, txn) ==
            make_error_code(IseledErrc::wrong_bus));
}

TEST_CASE("decode_command_request rejects op=read", "[iseled][REQ-ISELED-022]") {
    rcp::acf::AcfMessageInfo hdr;
    hdr.byte_bus_id = 4;
    hdr.op          = false; // not a command request
    const auto frame = rcp::acf::encode_acf_abb(hdr, {});
    std::vector<uint8_t> out_tx;
    uint8_t txn;
    REQUIRE(decode_command_request(frame.data(), frame.size(), 4, out_tx, txn) ==
            make_error_code(IseledErrc::wrong_op));
}

TEST_CASE("decode_command_request rejects a nonzero evt[2:0]", "[iseled][REQ-ISELED-022]") {
    rcp::acf::AcfMessageInfo hdr;
    hdr.byte_bus_id = 4;
    hdr.op          = true;
    hdr.evt_op       = 0x2;
    const auto frame = rcp::acf::encode_acf_abb(hdr, {});
    std::vector<uint8_t> out_tx;
    uint8_t txn;
    REQUIRE(decode_command_request(frame.data(), frame.size(), 4, out_tx, txn) ==
            make_error_code(IseledErrc::bad_evt));
}

TEST_CASE("decode_command_request rejects a non-ABB frame", "[iseled][REQ-ISELED-022]") {
    rcp::acf::AcfMessageInfo hdr;
    hdr.byte_bus_id = 4;
    hdr.op          = true;
    const auto frame = rcp::acf::encode_acf_gbb(hdr, 0, {});
    std::vector<uint8_t> out_tx;
    uint8_t txn;
    REQUIRE(decode_command_request(frame.data(), frame.size(), 4, out_tx, txn) ==
            make_error_code(IseledErrc::bad_msg_type));
}

TEST_CASE("decode_command_request rejects a short frame", "[iseled][REQ-ISELED-022]") {
    const uint8_t too_short[3] = {static_cast<uint8_t>(rcp::acf::kAcfMsgTypeAbb << 1), 0, 0};
    std::vector<uint8_t> out_tx;
    uint8_t txn;
    REQUIRE(decode_command_request(too_short, sizeof(too_short), 4, out_tx, txn) ==
            make_error_code(IseledErrc::short_frame));
}

TEST_CASE("the write-direction command-request codec is unchanged by the read-request addition",
          "[iseled][REQ-ISELED-030]") {
    const std::vector<uint8_t> tx{0x01, 0x02, 0x03};
    const auto frame = encode_command_request(2, tx, 9);
    REQUIRE_FALSE(frame.empty());

    std::vector<uint8_t> out_tx;
    uint16_t read_size = 0;
    uint8_t txn = 0;
    REQUIRE(decode_read_request(frame.data(), frame.size(), 2, out_tx, read_size, txn) ==
            make_error_code(IseledErrc::wrong_op));

    REQUIRE_FALSE(decode_command_request(frame.data(), frame.size(), 2, out_tx, txn));
    REQUIRE(out_tx == tx);
    REQUIRE(txn == 9);
}

// ── Read request (read direction) ─────────────────────────────────────────────

TEST_CASE("encode_read_request/decode_read_request round-trip address and read_size",
          "[iseled][REQ-ISELED-030]") {
    const std::vector<uint8_t> tx{0x03, 0x40};
    const auto frame = encode_read_request(6, tx, 12, 7);
    REQUIRE_FALSE(frame.empty());

    std::vector<uint8_t> out_tx;
    uint16_t read_size = 0;
    uint8_t txn = 0;
    REQUIRE_FALSE(decode_read_request(frame.data(), frame.size(), 6, out_tx, read_size, txn));
    REQUIRE(out_tx == tx);
    REQUIRE(read_size == 12);
    REQUIRE(txn == 7);
}

TEST_CASE("encode_read_request/decode_read_request round-trip an empty payload",
          "[iseled][REQ-ISELED-031]") {
    const auto frame = encode_read_request(1, {}, 64, 1);
    std::vector<uint8_t> out_tx{1};
    uint16_t read_size = 0;
    uint8_t txn = 0;
    REQUIRE_FALSE(decode_read_request(frame.data(), frame.size(), 1, out_tx, read_size, txn));
    REQUIRE(out_tx.empty());
    REQUIRE(read_size == 64);
}

TEST_CASE("read_size == 0 is a legal, well-formed read request", "[iseled][REQ-ISELED-030]") {
    const auto frame = encode_read_request(3, {}, 0, 2);
    std::vector<uint8_t> out_tx;
    uint16_t read_size = 0xFFFF;
    uint8_t txn;
    REQUIRE_FALSE(decode_read_request(frame.data(), frame.size(), 3, out_tx, read_size, txn));
    REQUIRE(read_size == 0);
}

TEST_CASE("read_size at kMaxReadSize round-trips; one above is rejected at encode",
          "[iseled][REQ-ISELED-030]") {
    const auto frame = encode_read_request(3, {}, kMaxReadSize, 2);
    std::vector<uint8_t> out_tx;
    uint16_t read_size = 0;
    uint8_t txn;
    REQUIRE_FALSE(decode_read_request(frame.data(), frame.size(), 3, out_tx, read_size, txn));
    REQUIRE(read_size == kMaxReadSize);

    REQUIRE(encode_read_request(3, {}, static_cast<uint16_t>(kMaxReadSize + 1), 2).empty());
}

TEST_CASE("decode_read_request rejects the wrong bus", "[iseled][REQ-ISELED-031]") {
    const std::vector<uint8_t> tx{0xAB};
    const auto frame = encode_read_request(4, tx, 8, 0);
    std::vector<uint8_t> out_tx;
    uint16_t read_size;
    uint8_t txn;
    REQUIRE(decode_read_request(frame.data(), frame.size(), 5, out_tx, read_size, txn) ==
            make_error_code(IseledErrc::wrong_bus));
}

TEST_CASE("decode_read_request rejects op=write", "[iseled][REQ-ISELED-031]") {
    rcp::acf::AcfMessageInfo hdr;
    hdr.byte_bus_id = 4;
    hdr.op          = true; // not a read request
    const auto frame = rcp::acf::encode_acf_abb(hdr, {});
    std::vector<uint8_t> out_tx;
    uint16_t read_size;
    uint8_t txn;
    REQUIRE(decode_read_request(frame.data(), frame.size(), 4, out_tx, read_size, txn) ==
            make_error_code(IseledErrc::wrong_op));
}

TEST_CASE("decode_read_request rejects a nonzero evt[2:0]", "[iseled][REQ-ISELED-031]") {
    rcp::acf::AcfMessageInfo hdr;
    hdr.byte_bus_id = 4;
    hdr.op          = false;
    hdr.evt_op       = 0x3;
    const auto frame = rcp::acf::encode_acf_abb(hdr, {});
    std::vector<uint8_t> out_tx;
    uint16_t read_size;
    uint8_t txn;
    REQUIRE(decode_read_request(frame.data(), frame.size(), 4, out_tx, read_size, txn) ==
            make_error_code(IseledErrc::bad_evt));
}

TEST_CASE("decode_read_request rejects a non-ABB frame", "[iseled][REQ-ISELED-031]") {
    rcp::acf::AcfMessageInfo hdr;
    hdr.byte_bus_id = 4;
    hdr.op          = false;
    const auto frame = rcp::acf::encode_acf_gbb(hdr, 0, {});
    std::vector<uint8_t> out_tx;
    uint16_t read_size;
    uint8_t txn;
    REQUIRE(decode_read_request(frame.data(), frame.size(), 4, out_tx, read_size, txn) ==
            make_error_code(IseledErrc::bad_msg_type));
}

TEST_CASE("decode_read_request rejects a short frame", "[iseled][REQ-ISELED-031]") {
    const uint8_t too_short[3] = {static_cast<uint8_t>(rcp::acf::kAcfMsgTypeAbb << 1), 0, 0};
    std::vector<uint8_t> out_tx;
    uint16_t read_size;
    uint8_t txn;
    REQUIRE(decode_read_request(too_short, sizeof(too_short), 4, out_tx, read_size, txn) ==
            make_error_code(IseledErrc::short_frame));
}

// ── Response round trip ───────────────────────────────────────────────────────

TEST_CASE("encode_response/decode_response round-trip untimed", "[iseled][REQ-ISELED-023]") {
    const std::vector<uint8_t> rx{0xDE, 0xAD, 0xBE, 0xEF};
    const auto frame = encode_response(2, rx, 11, false, 0);
    std::vector<uint8_t> out_rx;
    bool timed = true;
    uint64_t ts = 1;
    uint8_t txn = 0;
    REQUIRE_FALSE(decode_response(frame.data(), frame.size(), 2, out_rx, timed, ts, txn));
    REQUIRE(out_rx == rx);
    REQUIRE_FALSE(timed);
    REQUIRE(ts == 0);
    REQUIRE(txn == 11);
}

TEST_CASE("encode_response/decode_response round-trip timed", "[iseled][REQ-ISELED-023]") {
    const std::vector<uint8_t> rx{0x11, 0x22};
    const auto frame = encode_response(2, rx, 200, true, 0x0102030405060708ull);
    std::vector<uint8_t> out_rx;
    bool timed = false;
    uint64_t ts = 0;
    uint8_t txn = 0;
    REQUIRE_FALSE(decode_response(frame.data(), frame.size(), 2, out_rx, timed, ts, txn));
    REQUIRE(out_rx == rx);
    REQUIRE(timed);
    REQUIRE(ts == 0x0102030405060708ull);
    REQUIRE(txn == 200);
}

TEST_CASE("decode_response rejects the wrong bus", "[iseled][REQ-ISELED-024]") {
    const auto frame = encode_response(2, {}, 0, false, 0);
    std::vector<uint8_t> out_rx;
    bool timed;
    uint64_t ts;
    uint8_t txn;
    REQUIRE(decode_response(frame.data(), frame.size(), 3, out_rx, timed, ts, txn) ==
            make_error_code(IseledErrc::wrong_bus));
}

TEST_CASE("decode_response rejects a short frame", "[iseled][REQ-ISELED-024]") {
    const uint8_t too_short[2] = {rcp::acf::kAcfMsgTypeAbb << 1, 0};
    std::vector<uint8_t> out_rx;
    bool timed;
    uint64_t ts;
    uint8_t txn;
    REQUIRE(decode_response(too_short, sizeof(too_short), 2, out_rx, timed, ts, txn) ==
            make_error_code(IseledErrc::short_frame));
}

// ── REQ-ISELED-025/040: response fragmentation, bounded by read_size ─────────

TEST_CASE("response_fragment_count is 1 when capped data fits in one fragment",
          "[iseled][REQ-ISELED-040]") {
    REQUIRE(response_fragment_count(10, 10, 100) == 1);
}

TEST_CASE("response_fragment_count respects the read_size ceiling, not available_len",
          "[iseled][REQ-ISELED-040]") {
    REQUIRE(response_fragment_count(1000, 10, 100) == 1);
}

TEST_CASE("response_fragment_count splits the capped data across frames", "[iseled][REQ-ISELED-040]") {
    REQUIRE(response_fragment_count(1000, 250, 100) == 3); // 100 + 100 + 50
}

// End-to-end: encode a response whose available data exceeds both read_size
// and max_fragment_payload, fragment it, and reassemble every fragment via
// rcp/fragment.hpp's own Reassembler plus this module's own unmodified
// decode_response() as the per-fragment decoder.
TEST_CASE("fragment_worst_case_response_round_trip respects read_size", "[iseled][REQ-ISELED-025]") {
    std::vector<uint8_t> rx(300);
    for (size_t i = 0; i < rx.size(); ++i) rx[i] = static_cast<uint8_t>(i * 5 + 1);

    const uint16_t read_size            = 200;
    const size_t   max_fragment_payload = 64;

    REQUIRE(response_fragment_count(rx.size(), read_size, max_fragment_payload) == 4); // ceil(200/64)

    auto frames = encode_response_fragmented(9, rx, read_size, 42, false, 0, max_fragment_payload);
    REQUIRE(frames.size() == 4);

    rcp::fragment::Reassembler reasm(read_size);
    for (size_t i = 0; i < frames.size(); ++i) {
        std::vector<uint8_t> out_rx;
        bool timed;
        uint64_t ts;
        uint8_t txn;
        REQUIRE_FALSE(decode_response(frames[i].data(), frames[i].size(), 9, out_rx, timed, ts, txn));
        REQUIRE(txn == 42);
        REQUIRE_FALSE(timed);

        rcp::acf::AcfMessageInfo hdr;
        std::vector<uint8_t> payload;
        REQUIRE_FALSE(rcp::acf::decode_acf_abb(frames[i].data(), frames[i].size(), hdr, payload));

        auto rc = reasm.feed(hdr.ms, hdr.read_size_or_segment_num, payload.data(), payload.size());
        if (i + 1 < frames.size()) {
            REQUIRE(rc == rcp::fragment::ReasmResult::kContinue);
        } else {
            REQUIRE(rc == rcp::fragment::ReasmResult::kComplete);
        }
    }

    REQUIRE(reasm.size() == read_size);
    REQUIRE(std::vector<uint8_t>(reasm.data(), reasm.data() + reasm.size()) ==
            std::vector<uint8_t>(rx.begin(), rx.begin() + read_size));
}

// ── IseledEndpoint convenience wrapper ────────────────────────────────────────

TEST_CASE("IseledEndpoint::send records the raw sent bytes and fires TransferComplete",
          "[iseled][REQ-ISELED-008]") {
    IseledEndpoint ep;
    ep.triggers().enable(iseled_signal_id(IseledSignal::TransferComplete));

    ep.send({0x01, 0x02, 0x10, 0x20});
    REQUIRE(ep.last_sent() == std::vector<uint8_t>{0x01, 0x02, 0x10, 0x20});

    auto drained = ep.triggers().drain();
    REQUIRE(drained.size() == 1);
    REQUIRE(drained[0] == iseled_signal_id(IseledSignal::TransferComplete));
}

TEST_CASE("IseledEndpoint::receive records the raw received bytes without firing a trigger",
          "[iseled][REQ-ISELED-008]") {
    IseledEndpoint ep;
    ep.triggers().enable(iseled_signal_id(IseledSignal::TransferComplete));

    ep.receive({0xDE, 0xAD});
    REQUIRE(ep.last_received() == std::vector<uint8_t>{0xDE, 0xAD});
    REQUIRE_FALSE(ep.triggers().has_pending());
}

TEST_CASE("IseledEndpoint::handle_request delegates a Plain (evt[2:0]==000b) request to send()",
          "[iseled][REQ-ISELED-006]") {
    IseledEndpoint ep;
    ep.triggers().enable(iseled_signal_id(IseledSignal::TransferComplete));

    auto ec = ep.handle_request(/*evt_op=*/0, {0x01, 0x02, 0x10, 0x20});
    REQUIRE_FALSE(ec);
    REQUIRE(ep.last_sent() == std::vector<uint8_t>{0x01, 0x02, 0x10, 0x20});
}

TEST_CASE("IseledEndpoint::handle_request rejects every reserved evt[2:0] value (001b-110b) "
          "without recording anything",
          "[iseled][REQ-ISELED-006]") {
    for (uint8_t evt_op = 1; evt_op <= 6; ++evt_op) {
        IseledEndpoint ep;
        auto ec = ep.handle_request(evt_op, {0x01, 0x02});
        REQUIRE(ec == rcp::endpoint::make_error_code(rcp::endpoint::EndpointErrc::reserved_evt_row2));
        REQUIRE(ep.last_sent().empty());
        REQUIRE_FALSE(ep.triggers().has_pending());
    }
}

TEST_CASE("IseledEndpoint::handle_request reports config_write_not_supported for evt[2:0]==111b",
          "[iseled][REQ-ISELED-022]") {
    IseledEndpoint ep;
    auto ec = ep.handle_request(/*evt_op=*/7, {0x00, 0xAB});
    REQUIRE(ec == make_error_code(IseledErrc::config_write_not_supported));
    REQUIRE(ep.last_sent().empty());
    REQUIRE_FALSE(ep.triggers().has_pending());
}

TEST_CASE("IseledEndpoint::handle_request masks evt_op down to 3 bits before classifying",
          "[iseled][REQ-ISELED-006]") {
    IseledEndpoint ep;
    REQUIRE_FALSE(ep.handle_request(/*evt_op=*/0xF8, {0x01})); // low 3 bits 000 -> Plain
    auto ec = ep.handle_request(/*evt_op=*/0xF9, {0x01});      // low 3 bits 001 -> Reserved
    REQUIRE(ec == rcp::endpoint::make_error_code(rcp::endpoint::EndpointErrc::reserved_evt_row2));
}
