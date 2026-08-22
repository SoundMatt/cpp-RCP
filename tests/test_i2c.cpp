// fusa:test REQ-I2C-001
// fusa:test REQ-I2C-002
// fusa:test REQ-I2C-003
// fusa:test REQ-I2C-004
// fusa:test REQ-I2C-005
// fusa:test REQ-I2C-006
// fusa:test REQ-I2C-007
// fusa:test REQ-I2C-008
// fusa:test REQ-I2C-009
// fusa:test REQ-I2C-010
// fusa:test REQ-I2C-011
// fusa:test REQ-I2C-012
// fusa:test REQ-I2C-013
// fusa:test REQ-I2C-014
// fusa:test REQ-I2C-015
// fusa:test REQ-I2C-016
// fusa:test REQ-I2C-017
// fusa:test REQ-I2C-018
// fusa:test REQ-I2C-020
// fusa:test REQ-I2C-021
// fusa:test REQ-I2C-022
// fusa:test REQ-I2C-023
// fusa:test REQ-I2C-024
// fusa:test REQ-I2C-025
// fusa:test REQ-I2C-026

// Tests for rcp/i2c.hpp — the I2C endpoint type (ep_type 0x04), ported from
// c-RCP's tests/test_ep_i2c.c (this project's RC5-spec-conformant
// reference) as part of Phase 3 of the ground-up rewrite (cpp-RCP issue
// #129, ROADMAP.md "Phase 17").

#include <catch2/catch_test_macros.hpp>
#include <rcp/acf.hpp>
#include <rcp/i2c.hpp>
#include <rcp/lifecycle.hpp>

using namespace rcp::i2c;

// ── i2c_mode ───────────────────────────────────────────────────────────────────

TEST_CASE("i2c_mode_valid accepts 0..4 and rejects everything else", "[i2c][REQ-I2C-001]") {
    for (uint8_t v = 0; v <= 4; ++v) REQUIRE(i2c_mode_valid(v));
    REQUIRE_FALSE(i2c_mode_valid(5));
    REQUIRE_FALSE(i2c_mode_valid(255));
}

// ── Functional config ─────────────────────────────────────────────────────────

TEST_CASE("i2c_functional_cfg_init zeroes every field", "[i2c][REQ-I2C-002]") {
    I2cFunctionalCfg cfg;
    cfg.ep_enable = cfg.ep_clear_req_storage = cfg.ep_req_crc_enable = true;
    cfg.i2c_mode = static_cast<uint8_t>(I2cMode::UltraFast);
    cfg.ep_status = 0xBEEF;
    cfg.clock_divider = 0xAA;
    cfg.trail = 0xCC;

    i2c_functional_cfg_init(cfg);

    REQUIRE_FALSE(cfg.ep_enable);
    REQUIRE_FALSE(cfg.ep_clear_req_storage);
    REQUIRE_FALSE(cfg.ep_req_crc_enable);
    REQUIRE_FALSE(cfg.ep_response_ts_enable);
    REQUIRE_FALSE(cfg.ep_suppress_response);
    REQUIRE(cfg.i2c_mode == static_cast<uint8_t>(I2cMode::Standard));
    REQUIRE(cfg.ep_status == 0);
    REQUIRE(cfg.clock_divider == 0);
    REQUIRE(cfg.trail == 0);
}

TEST_CASE("i2c_functional_cfg_writable is false in HwUnconfigured regardless of writer",
          "[i2c][REQ-I2C-003]") {
    rcp::lifecycle::WriterCtx writer;
    writer.via_root_client_ep0 = true;
    writer.via_owning_stream   = true;
    REQUIRE_FALSE(i2c_functional_cfg_writable(rcp::lifecycle::ServerState::HwUnconfigured, writer));
}

TEST_CASE("i2c_functional_cfg_writable in HwConfigured requires EP0/owning-stream/discovery-stream",
          "[i2c][REQ-I2C-004]") {
    rcp::lifecycle::WriterCtx none, via_ep0, via_stream, via_discovery;
    via_ep0.via_root_client_ep0   = true;
    via_stream.via_owning_stream  = true;
    via_discovery.via_discovery_stream = true;

    REQUIRE_FALSE(i2c_functional_cfg_writable(rcp::lifecycle::ServerState::HwConfigured, none));
    REQUIRE(i2c_functional_cfg_writable(rcp::lifecycle::ServerState::HwConfigured, via_ep0));
    REQUIRE(i2c_functional_cfg_writable(rcp::lifecycle::ServerState::HwConfigured, via_stream));
    REQUIRE(i2c_functional_cfg_writable(rcp::lifecycle::ServerState::HwConfigured, via_discovery));
}

TEST_CASE("i2c_functional_cfg_writable in RcpConfigured requires EP0/owning-stream, not discovery",
          "[i2c][REQ-I2C-005]") {
    rcp::lifecycle::WriterCtx none, via_ep0, via_stream;
    via_ep0.via_root_client_ep0  = true;
    via_stream.via_owning_stream = true;

    REQUIRE_FALSE(i2c_functional_cfg_writable(rcp::lifecycle::ServerState::RcpConfigured, none));
    REQUIRE(i2c_functional_cfg_writable(rcp::lifecycle::ServerState::RcpConfigured, via_ep0));
    REQUIRE(i2c_functional_cfg_writable(rcp::lifecycle::ServerState::RcpConfigured, via_stream));
}

TEST_CASE("set_mode rejects an invalid mode regardless of authorization", "[i2c][REQ-I2C-006]") {
    I2cFunctionalCfg cfg;
    i2c_functional_cfg_init(cfg);
    rcp::lifecycle::WriterCtx authorized;
    authorized.via_root_client_ep0 = true;

    REQUIRE_FALSE(set_mode(cfg, static_cast<I2cMode>(99), rcp::lifecycle::ServerState::HwConfigured, authorized));
    REQUIRE(cfg.i2c_mode == static_cast<uint8_t>(I2cMode::Standard));
}

TEST_CASE("set_mode rejects an unauthorized writer even with a valid mode", "[i2c][REQ-I2C-007]") {
    I2cFunctionalCfg cfg;
    i2c_functional_cfg_init(cfg);
    rcp::lifecycle::WriterCtx none;

    REQUIRE_FALSE(set_mode(cfg, I2cMode::Fast, rcp::lifecycle::ServerState::RcpConfigured, none));
    REQUIRE(cfg.i2c_mode == static_cast<uint8_t>(I2cMode::Standard));
}

TEST_CASE("set_mode applies when valid and authorized", "[i2c][REQ-I2C-008]") {
    I2cFunctionalCfg cfg;
    i2c_functional_cfg_init(cfg);
    rcp::lifecycle::WriterCtx writer;
    writer.via_owning_stream = true;

    REQUIRE(set_mode(cfg, I2cMode::HighSpeed, rcp::lifecycle::ServerState::HwConfigured, writer));
    REQUIRE(cfg.i2c_mode == static_cast<uint8_t>(I2cMode::HighSpeed));
}

// ── The EP_func register block ────────────────────────────────────────────────

TEST_CASE("render_registers matches the corrected Table 49 offsets", "[i2c][REQ-I2C-021]") {
    I2cFunctionalCfg cfg;
    i2c_functional_cfg_init(cfg);
    cfg.ep_enable      = true;
    cfg.ep_status      = 0x1234;
    cfg.clock_divider  = 0x55;
    cfg.i2c_mode        = static_cast<uint8_t>(I2cMode::UltraFast);
    cfg.trail           = 0x77;

    const auto out = render_registers(cfg);

    REQUIRE(out[kRegEpLen] == static_cast<uint8_t>(kEpFuncLen));
    REQUIRE(out[kRegReserved01] == 0);
    REQUIRE((out[kRegEpEnableClr] & 0x01) != 0);
    REQUIRE(out[kRegBaseClk] == 0);      // base_clk always renders 0
    REQUIRE(out[kRegBaseClk + 1] == 0);
    REQUIRE(out[kRegEpStatus] == 0x12);
    REQUIRE(out[kRegEpStatus + 1] == 0x34);
    REQUIRE(out[kRegClockDivider] == 0x55);
    REQUIRE(out[kRegMode] == static_cast<uint8_t>(I2cMode::UltraFast));
    REQUIRE(out[kRegTrail] == 0x77);
    REQUIRE(kEpFuncLen == 0x000Bu);
}

TEST_CASE("apply_reconfig writes the clock divider register", "[i2c][REQ-I2C-022]") {
    I2cFunctionalCfg cfg;
    i2c_functional_cfg_init(cfg);
    const uint8_t payload[3] = {0x00, static_cast<uint8_t>(kRegClockDivider), 0x42};

    REQUIRE_FALSE(apply_reconfig(cfg, payload, sizeof(payload)));
    REQUIRE(cfg.clock_divider == 0x42);
}

TEST_CASE("apply_reconfig writes a multi-register span", "[i2c][REQ-I2C-022]") {
    I2cFunctionalCfg cfg;
    i2c_functional_cfg_init(cfg);
    const uint8_t payload[6] = {0x00, static_cast<uint8_t>(kRegEpStatus),
                                 0xAB, 0xCD,                              // ep_status
                                 0x03,                                    // clock_divider
                                 static_cast<uint8_t>(I2cMode::Fast)};    // i2c_mode

    REQUIRE_FALSE(apply_reconfig(cfg, payload, sizeof(payload)));
    REQUIRE(cfg.ep_status == 0xABCD);
    REQUIRE(cfg.clock_divider == 0x03);
    REQUIRE(cfg.i2c_mode == static_cast<uint8_t>(I2cMode::Fast));
}

TEST_CASE("apply_reconfig ignores read-only registers (EP_LEN/reserved)", "[i2c][REQ-I2C-022]") {
    I2cFunctionalCfg cfg;
    i2c_functional_cfg_init(cfg);
    const uint8_t payload[6] = {0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF};

    REQUIRE_FALSE(apply_reconfig(cfg, payload, sizeof(payload)));

    const auto out = render_registers(cfg);
    REQUIRE(out[kRegEpLen] == static_cast<uint8_t>(kEpFuncLen));
    REQUIRE(out[kRegReserved01] == 0);
}

// MC/DC: the read-only-offset OR chain's base_clk arms need their own
// isolated single-octet writes (offsets 0x00-0x03 alone never reaches
// 0x04/0x05) — matches c-RCP's own dedicated regression for this.
TEST_CASE("apply_reconfig ignores base_clk's own two octets individually", "[i2c][REQ-I2C-021]") {
    I2cFunctionalCfg cfg;
    i2c_functional_cfg_init(cfg);
    const uint8_t payload[4] = {0x00, static_cast<uint8_t>(kRegBaseClk), 0xFF, 0xFF};

    REQUIRE_FALSE(apply_reconfig(cfg, payload, sizeof(payload)));

    const auto out = render_registers(cfg);
    REQUIRE(out[kRegBaseClk] == 0);
    REQUIRE(out[kRegBaseClk + 1] == 0);
}

TEST_CASE("apply_reconfig rejects a write extending past EP_LEN", "[i2c][REQ-I2C-022]") {
    I2cFunctionalCfg cfg;
    i2c_functional_cfg_init(cfg);
    const uint8_t payload[3] = {0x00, static_cast<uint8_t>(kEpFuncLen), 0xFF};

    REQUIRE(apply_reconfig(cfg, payload, sizeof(payload)) == make_error_code(I2cReconfigErrc::out_of_range));
    REQUIRE(cfg.trail == 0);
}

TEST_CASE("apply_reconfig rejects a payload with no data octet", "[i2c][REQ-I2C-022]") {
    I2cFunctionalCfg cfg;
    i2c_functional_cfg_init(cfg);
    const uint8_t addr_only[2] = {0x00, 0x08};

    REQUIRE(apply_reconfig(cfg, addr_only, sizeof(addr_only)) ==
            make_error_code(I2cReconfigErrc::short_payload));
    REQUIRE(apply_reconfig(cfg, nullptr, 0) == make_error_code(I2cReconfigErrc::short_payload));
}

TEST_CASE("encode_reconfig_request round-trips through acf::decode_acf_abb", "[i2c][REQ-I2C-025]") {
    const std::vector<uint8_t> data{0xAB, 0xCD};
    const auto frame = encode_reconfig_request(0x03, 0x0006, data, 7);
    REQUIRE_FALSE(frame.empty());

    rcp::acf::AcfMessageInfo hdr;
    std::vector<uint8_t> payload;
    REQUIRE_FALSE(rcp::acf::decode_acf_abb(frame.data(), frame.size(), hdr, payload));
    REQUIRE(hdr.byte_bus_id == 0x03);
    REQUIRE(hdr.op);
    REQUIRE(hdr.evt_op == 0x7);
    REQUIRE_FALSE(hdr.evt_ack);
    REQUIRE(hdr.transaction_num == 7);
    REQUIRE(payload == std::vector<uint8_t>{0x00, 0x06, 0xAB, 0xCD});
}

TEST_CASE("encode_reconfig_request rejects empty data", "[i2c][REQ-I2C-025]") {
    REQUIRE(encode_reconfig_request(0x00, 0, {}, 0).empty());
}

TEST_CASE("reconfig error category reports a distinct, non-empty message per code", "[i2c][REQ-I2C-026]") {
    auto short_ec = make_error_code(I2cReconfigErrc::short_payload);
    auto range_ec = make_error_code(I2cReconfigErrc::out_of_range);
    REQUIRE_FALSE(short_ec.message().empty());
    REQUIRE_FALSE(range_ec.message().empty());
    REQUIRE(short_ec.message() != range_ec.message());
}

// ── I2cErrc category sanity ───────────────────────────────────────────────────

TEST_CASE("I2cErrc reports a non-empty, distinct message per code", "[i2c][REQ-I2C-009]") {
    const I2cErrc codes[] = {I2cErrc::short_frame,  I2cErrc::bad_msg_type, I2cErrc::wrong_bus,
                              I2cErrc::wrong_op,     I2cErrc::bad_evt,      I2cErrc::nack,
                              I2cErrc::config_write_not_supported};
    std::vector<std::string> seen;
    for (auto c : codes) {
        auto ec = make_error_code(c);
        REQUIRE(ec.category() == i2c_category());
        REQUIRE_FALSE(ec.message().empty());
        for (const auto& s : seen) REQUIRE(s != ec.message());
        seen.push_back(ec.message());
    }
    REQUIRE_FALSE(make_error_code(static_cast<I2cErrc>(999)).message().empty());
}

// ── Transfer direction ────────────────────────────────────────────────────────

TEST_CASE("encode_transfer_request read direction carries op=read and read_size", "[i2c][REQ-I2C-010]") {
    const std::vector<uint8_t> tx{0xA3}; // 7-bit address with the payload's own R/W bit set
    const auto frame = encode_transfer_request(6, I2cDir::Read, tx, 10, 7);
    REQUIRE_FALSE(frame.empty());

    rcp::acf::AcfMessageInfo hdr;
    std::vector<uint8_t> payload;
    REQUIRE_FALSE(rcp::acf::decode_acf_abb(frame.data(), frame.size(), hdr, payload));
    REQUIRE_FALSE(hdr.op); // op=false means "read" in this codec's convention
    REQUIRE(hdr.read_size_or_segment_num == 10);
    REQUIRE(payload[0] == 0xA3);
}

TEST_CASE("encode_transfer_request write direction carries op=write and no read_size",
          "[i2c][REQ-I2C-010]") {
    const std::vector<uint8_t> tx{0xA2, 0x10, 0x20};
    const auto frame = encode_transfer_request(6, I2cDir::Write, tx, 0, 7);
    REQUIRE_FALSE(frame.empty());

    rcp::acf::AcfMessageInfo hdr;
    std::vector<uint8_t> payload;
    REQUIRE_FALSE(rcp::acf::decode_acf_abb(frame.data(), frame.size(), hdr, payload));
    REQUIRE(hdr.op);
    REQUIRE(hdr.read_size_or_segment_num == 0);
    REQUIRE(payload[0] == 0xA2);
}

TEST_CASE("i2c_dir_valid accepts Write/Read and rejects everything else", "[i2c][REQ-I2C-017]") {
    REQUIRE(i2c_dir_valid(I2cDir::Write));
    REQUIRE(i2c_dir_valid(I2cDir::Read));
    REQUIRE_FALSE(i2c_dir_valid(static_cast<I2cDir>(2)));
    REQUIRE_FALSE(i2c_dir_valid(static_cast<I2cDir>(255)));
}

TEST_CASE("encode_transfer_request rejects invalid direction/read_size combinations",
          "[i2c][REQ-I2C-018]") {
    const std::vector<uint8_t> tx{0xA2};

    REQUIRE(encode_transfer_request(6, static_cast<I2cDir>(2), tx, 0, 0).empty());

    REQUIRE(encode_transfer_request(6, I2cDir::Read, tx, 0x1000, 0).empty());
    REQUIRE_FALSE(encode_transfer_request(6, I2cDir::Read, tx, kMaxReadSize, 0).empty());

    // A write request's header slot is a segment_num, not a read_size.
    REQUIRE(encode_transfer_request(6, I2cDir::Write, tx, 4, 0).empty());
}

// ── Transfer request round trip ───────────────────────────────────────────────

TEST_CASE("decode_transfer_request round-trips address bytes unmodified", "[i2c][REQ-I2C-023]") {
    const std::vector<uint8_t> tx{0xA2, 0x10, 0x20, 0x30};
    const auto frame = encode_transfer_request(6, I2cDir::Write, tx, 0, 7);
    REQUIRE_FALSE(frame.empty());

    I2cDir dir = I2cDir::Read;
    std::vector<uint8_t> out_tx;
    uint16_t read_size = 99;
    uint8_t txn = 0;
    REQUIRE_FALSE(decode_transfer_request(frame.data(), frame.size(), 6, dir, out_tx, read_size, txn));
    REQUIRE(dir == I2cDir::Write);
    REQUIRE(out_tx == tx);
    REQUIRE(read_size == 0);
    REQUIRE(txn == 7);
}

TEST_CASE("decode_transfer_request round-trips the read direction", "[i2c][REQ-I2C-011]") {
    const std::vector<uint8_t> tx{0xF2, 0xA3};
    const auto frame = encode_transfer_request(6, I2cDir::Read, tx, 5, 8);

    I2cDir dir = I2cDir::Write;
    std::vector<uint8_t> out_tx;
    uint16_t read_size = 0;
    uint8_t txn = 0;
    REQUIRE_FALSE(decode_transfer_request(frame.data(), frame.size(), 6, dir, out_tx, read_size, txn));
    REQUIRE(dir == I2cDir::Read);
    REQUIRE(out_tx == tx);
    REQUIRE(read_size == 5);
    REQUIRE(txn == 8);
}

TEST_CASE("decode_transfer_request round-trips an empty payload", "[i2c][REQ-I2C-011]") {
    const auto frame = encode_transfer_request(1, I2cDir::Write, {}, 0, 1);
    I2cDir dir;
    std::vector<uint8_t> out_tx{1}; // start non-empty to prove it gets cleared
    uint16_t read_size;
    uint8_t txn;
    REQUIRE_FALSE(decode_transfer_request(frame.data(), frame.size(), 1, dir, out_tx, read_size, txn));
    REQUIRE(out_tx.empty());
}

TEST_CASE("decode_transfer_request rejects the wrong bus", "[i2c][REQ-I2C-012]") {
    const std::vector<uint8_t> tx{0xAB};
    const auto frame = encode_transfer_request(4, I2cDir::Write, tx, 0, 0);
    I2cDir dir;
    std::vector<uint8_t> out_tx;
    uint16_t read_size;
    uint8_t txn;
    REQUIRE(decode_transfer_request(frame.data(), frame.size(), 5, dir, out_tx, read_size, txn) ==
            make_error_code(I2cErrc::wrong_bus));
}

TEST_CASE("decode_transfer_request rejects a reserved evt[2:0] value", "[i2c][REQ-I2C-012]") {
    rcp::acf::AcfMessageInfo hdr;
    hdr.byte_bus_id = 4;
    hdr.op          = true;
    hdr.evt_op       = 0x3;
    const auto frame = rcp::acf::encode_acf_abb(hdr, {});

    I2cDir dir;
    std::vector<uint8_t> out_tx;
    uint16_t read_size;
    uint8_t txn;
    REQUIRE(decode_transfer_request(frame.data(), frame.size(), 4, dir, out_tx, read_size, txn) ==
            make_error_code(I2cErrc::bad_evt));
}

TEST_CASE("decode_transfer_request accepts a hand-built read-direction frame", "[i2c][REQ-I2C-023]") {
    rcp::acf::AcfMessageInfo hdr;
    hdr.byte_bus_id              = 4;
    hdr.op                       = false; // read
    hdr.read_size_or_segment_num = 12;
    const auto frame = rcp::acf::encode_acf_abb(hdr, {});

    I2cDir dir = I2cDir::Write;
    std::vector<uint8_t> out_tx;
    uint16_t read_size = 0;
    uint8_t txn;
    REQUIRE_FALSE(decode_transfer_request(frame.data(), frame.size(), 4, dir, out_tx, read_size, txn));
    REQUIRE(dir == I2cDir::Read);
    REQUIRE(read_size == 12);
}

TEST_CASE("decode_transfer_request rejects a non-ABB frame", "[i2c][REQ-I2C-012]") {
    rcp::acf::AcfMessageInfo hdr;
    hdr.byte_bus_id = 4;
    hdr.op          = true;
    const auto frame = rcp::acf::encode_acf_gbb(hdr, 0, {});

    I2cDir dir;
    std::vector<uint8_t> out_tx;
    uint16_t read_size;
    uint8_t txn;
    REQUIRE(decode_transfer_request(frame.data(), frame.size(), 4, dir, out_tx, read_size, txn) ==
            make_error_code(I2cErrc::bad_msg_type));
}

TEST_CASE("decode_transfer_request rejects a short frame", "[i2c][REQ-I2C-012]") {
    // byte0's top 7 bits must decode as ACF_ABB (0x0E) so short length, not
    // an unrecognized message type, is what gets diagnosed.
    const uint8_t too_short[3] = {static_cast<uint8_t>(rcp::acf::kAcfMsgTypeAbb << 1), 0, 0};
    I2cDir dir;
    std::vector<uint8_t> out_tx;
    uint16_t read_size;
    uint8_t txn;
    REQUIRE(decode_transfer_request(too_short, sizeof(too_short), 4, dir, out_tx, read_size, txn) ==
            make_error_code(I2cErrc::short_frame));
}

// ── Response round trip ───────────────────────────────────────────────────────

TEST_CASE("encode_response/decode_response round-trip untimed", "[i2c][REQ-I2C-014]") {
    const std::vector<uint8_t> rx{0xDE, 0xAD, 0xBE, 0xEF};
    const auto frame = encode_response(2, I2cDir::Read, rx, 11, false, 0);
    REQUIRE_FALSE(frame.empty());

    I2cDir dir = I2cDir::Write;
    std::vector<uint8_t> out_rx;
    bool timed = true;
    uint64_t ts = 1;
    uint8_t txn = 0;
    REQUIRE_FALSE(decode_response(frame.data(), frame.size(), 2, dir, out_rx, timed, ts, txn));
    REQUIRE(dir == I2cDir::Read);
    REQUIRE(out_rx == rx);
    REQUIRE_FALSE(timed);
    REQUIRE(ts == 0);
    REQUIRE(txn == 11);
}

TEST_CASE("encode_response/decode_response round-trip timed", "[i2c][REQ-I2C-015]") {
    const std::vector<uint8_t> rx{0x11, 0x22};
    const auto frame = encode_response(2, I2cDir::Read, rx, 200, true, 0x0102030405060708ull);

    I2cDir dir = I2cDir::Write;
    std::vector<uint8_t> out_rx;
    bool timed = false;
    uint64_t ts = 0;
    uint8_t txn = 0;
    REQUIRE_FALSE(decode_response(frame.data(), frame.size(), 2, dir, out_rx, timed, ts, txn));
    REQUIRE(dir == I2cDir::Read);
    REQUIRE(out_rx == rx);
    REQUIRE(timed);
    REQUIRE(ts == 0x0102030405060708ull);
    REQUIRE(txn == 200);
}

TEST_CASE("a write response carries op=write and no payload", "[i2c][REQ-I2C-013]") {
    const auto frame = encode_response(2, I2cDir::Write, {}, 11, false, 0);
    REQUIRE_FALSE(frame.empty());

    rcp::acf::AcfMessageInfo hdr;
    std::vector<uint8_t> payload;
    REQUIRE_FALSE(rcp::acf::decode_acf_abb(frame.data(), frame.size(), hdr, payload));
    REQUIRE(hdr.op);
    REQUIRE(payload.empty());

    I2cDir dir = I2cDir::Read;
    std::vector<uint8_t> out_rx;
    bool timed;
    uint64_t ts;
    uint8_t txn;
    REQUIRE_FALSE(decode_response(frame.data(), frame.size(), 2, dir, out_rx, timed, ts, txn));
    REQUIRE(dir == I2cDir::Write);
    REQUIRE(out_rx.empty());
}

TEST_CASE("encode_response rejects invalid direction or a write-direction payload",
          "[i2c][REQ-I2C-024]") {
    REQUIRE(encode_response(2, I2cDir::Write, {0xFF}, 11, false, 0).empty());
    REQUIRE(encode_response(2, static_cast<I2cDir>(2), {}, 11, false, 0).empty());
}

TEST_CASE("decode_response rejects the wrong bus", "[i2c][REQ-I2C-016]") {
    const auto frame = encode_response(2, I2cDir::Read, {}, 0, false, 0);
    I2cDir dir;
    std::vector<uint8_t> out_rx;
    bool timed;
    uint64_t ts;
    uint8_t txn;
    REQUIRE(decode_response(frame.data(), frame.size(), 3, dir, out_rx, timed, ts, txn) ==
            make_error_code(I2cErrc::wrong_bus));
}

TEST_CASE("decode_response rejects a short frame", "[i2c][REQ-I2C-016]") {
    const uint8_t too_short[2] = {rcp::acf::kAcfMsgTypeAbb << 1, 0};
    I2cDir dir;
    std::vector<uint8_t> out_rx;
    bool timed;
    uint64_t ts;
    uint8_t txn;
    REQUIRE(decode_response(too_short, sizeof(too_short), 2, dir, out_rx, timed, ts, txn) ==
            make_error_code(I2cErrc::short_frame));
}

// ── I2cEndpoint (controller-only raw byte-stream transfer, Table 33 Row 2) ───

TEST_CASE("I2cEndpoint::transfer records the raw sent/received byte streams", "[i2c][REQ-I2C-003]") {
    I2cEndpoint ep;
    auto ec = ep.transfer({0xA0, 0x10}, {0xFF});
    REQUIRE_FALSE(ec);
    REQUIRE(ep.last_sent() == std::vector<uint8_t>{0xA0, 0x10});
    REQUIRE(ep.last_received() == std::vector<uint8_t>{0xFF});
}

TEST_CASE("I2cEndpoint::transfer reports NACK and fires the Nack signal when unacknowledged",
          "[i2c][REQ-I2C-004]") {
    I2cEndpoint ep;
    ep.triggers().enable(i2c_signal_id(I2cSignal::TransferComplete));
    ep.triggers().enable(i2c_signal_id(I2cSignal::Nack));

    auto ec = ep.transfer({0xA0}, {}, /*acked=*/false);
    REQUIRE(ec == make_error_code(I2cErrc::nack));

    auto drained = ep.triggers().drain();
    REQUIRE(drained.size() == 2);
    REQUIRE(drained[0] == i2c_signal_id(I2cSignal::TransferComplete));
    REQUIRE(drained[1] == i2c_signal_id(I2cSignal::Nack));
}

TEST_CASE("I2cEndpoint::transfer fires only TransferComplete on a normal acked transfer",
          "[i2c][REQ-I2C-004]") {
    I2cEndpoint ep;
    ep.triggers().enable(i2c_signal_id(I2cSignal::TransferComplete));
    ep.triggers().enable(i2c_signal_id(I2cSignal::Nack));

    REQUIRE_FALSE(ep.transfer({0xA0}, {0x01}));

    auto drained = ep.triggers().drain();
    REQUIRE(drained.size() == 1);
    REQUIRE(drained[0] == i2c_signal_id(I2cSignal::TransferComplete));
}

TEST_CASE("I2cEndpoint::handle_request delegates a Plain (evt[2:0]==000b) request to transfer()",
          "[i2c][REQ-I2C-006]") {
    I2cEndpoint ep;
    ep.triggers().enable(i2c_signal_id(I2cSignal::TransferComplete));

    auto ec = ep.handle_request(/*evt_op=*/0, {0xA0, 0x10}, {0xFF});
    REQUIRE_FALSE(ec);
    REQUIRE(ep.last_sent() == std::vector<uint8_t>{0xA0, 0x10});
    REQUIRE(ep.last_received() == std::vector<uint8_t>{0xFF});
}

TEST_CASE("I2cEndpoint::handle_request rejects every reserved evt[2:0] value (001b-110b)",
          "[i2c][REQ-I2C-006]") {
    for (uint8_t evt_op = 1; evt_op <= 6; ++evt_op) {
        I2cEndpoint ep;
        auto ec = ep.handle_request(evt_op, {0xA0}, {0xFF});
        REQUIRE(ec == rcp::endpoint::make_error_code(rcp::endpoint::EndpointErrc::reserved_evt_row2));
        REQUIRE(ep.last_sent().empty());
        REQUIRE(ep.last_received().empty());
    }
}

TEST_CASE("I2cEndpoint::handle_request reports config_write_not_supported for evt[2:0]==111b",
          "[i2c][REQ-I2C-007]") {
    I2cEndpoint ep;
    auto ec = ep.handle_request(/*evt_op=*/7, {0x00, 0xAB}, {});
    REQUIRE(ec == make_error_code(I2cErrc::config_write_not_supported));
    REQUIRE(ep.last_sent().empty());
    REQUIRE(ep.last_received().empty());
}

TEST_CASE("I2cEndpoint::handle_request masks evt_op down to 3 bits before classifying",
          "[i2c][REQ-I2C-006]") {
    I2cEndpoint ep;
    REQUIRE_FALSE(ep.handle_request(/*evt_op=*/0xF8, {0xA0}, {0xFF})); // low 3 bits 000 -> Plain
    auto ec = ep.handle_request(/*evt_op=*/0xF9, {0xA0}, {0xFF});      // low 3 bits 001 -> Reserved
    REQUIRE(ec == rcp::endpoint::make_error_code(rcp::endpoint::EndpointErrc::reserved_evt_row2));
}
