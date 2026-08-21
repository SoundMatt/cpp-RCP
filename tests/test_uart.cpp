// fusa:test REQ-UART-001
// fusa:test REQ-UART-002
// fusa:test REQ-UART-003
// fusa:test REQ-UART-004
// fusa:test REQ-UART-005
// fusa:test REQ-UART-006
// fusa:test REQ-UART-007
// fusa:test REQ-UART-008
// fusa:test REQ-UART-009

// Tests for rcp/uart.hpp — the UART endpoint type (ROADMAP.md milestone 48,
// "Basic Endpoint Types II — I2C, UART, ADC, PWM_OUT, PWM_IN", v2.4.0).

#include <catch2/catch_test_macros.hpp>
#include <rcp/acf.hpp>
#include <rcp/endpoint.hpp>
#include <rcp/fragment.hpp>
#include <rcp/lifecycle.hpp>
#include <rcp/uart.hpp>

using namespace rcp::uart;

// ── Independent TX/RX queues ─────────────────────────────────────────────────

TEST_CASE("enqueue_tx / drain_tx are independent of the RX FIFO", "[uart][REQ-UART-001]") {
    UartEndpoint ep;
    REQUIRE_FALSE(ep.enqueue_tx({0x01, 0x02}));
    REQUIRE_FALSE(ep.rx_fill({0xAA, 0xBB}));

    auto tx = ep.drain_tx();
    REQUIRE(tx == std::vector<uint8_t>{0x01, 0x02});
    REQUIRE(ep.rx_available() == 2); // rx_fill's bytes are unaffected by draining tx
}

TEST_CASE("drain_tx clears the TX queue after returning it", "[uart][REQ-UART-001]") {
    UartEndpoint ep;
    REQUIRE_FALSE(ep.enqueue_tx({0x01}));
    REQUIRE(ep.drain_tx().size() == 1);
    REQUIRE(ep.drain_tx().empty());
}

TEST_CASE("enqueue_tx rejects a push beyond kTxQueueCapacity", "[uart][REQ-UART-001]") {
    UartEndpoint ep;
    std::vector<uint8_t> big(kTxQueueCapacity + 1, 0x00);
    auto ec = ep.enqueue_tx(big);
    REQUIRE(ec == make_error_code(UartErrc::tx_queue_overflow));
}

// ── RX FIFO fill/drain semantics ─────────────────────────────────────────────

TEST_CASE("rx_fill appends to the RX FIFO and rx_available reflects it", "[uart][REQ-UART-002]") {
    UartEndpoint ep;
    REQUIRE_FALSE(ep.rx_fill({0x01, 0x02, 0x03}));
    REQUIRE(ep.rx_available() == 3);
}

TEST_CASE("rx_fill rejects a push beyond kRxFifoCapacity", "[uart][REQ-UART-002]") {
    UartEndpoint ep;
    std::vector<uint8_t> big(kRxFifoCapacity + 1, 0x00);
    auto ec = ep.rx_fill(big);
    REQUIRE(ec == make_error_code(UartErrc::rx_fifo_overflow));
}

// ── Read completion: read_size reached vs. uart_timeout elapsed ─────────────

TEST_CASE("handle_read completes on read_size reached before timeout", "[uart][REQ-UART-003]") {
    UartEndpoint ep;
    REQUIRE_FALSE(ep.rx_fill({0x01, 0x02, 0x03, 0x04}));

    std::vector<uint8_t> out;
    bool timed_out = true;
    auto ec = ep.handle_read(/*read_size=*/2, /*elapsed_ms=*/1, /*uart_timeout_ms=*/100, out, timed_out);
    REQUIRE_FALSE(ec);
    REQUIRE(out == std::vector<uint8_t>{0x01, 0x02});
    REQUIRE_FALSE(timed_out);
    REQUIRE(ep.rx_available() == 2); // remaining bytes stay buffered (drain semantics)
}

TEST_CASE("handle_read reports timed_out when read_size is not reached and uart_timeout elapsed",
          "[uart][REQ-UART-003]") {
    UartEndpoint ep;
    REQUIRE_FALSE(ep.rx_fill({0x01}));

    std::vector<uint8_t> out;
    bool timed_out = false;
    auto ec = ep.handle_read(/*read_size=*/4, /*elapsed_ms=*/100, /*uart_timeout_ms=*/50, out, timed_out);
    REQUIRE_FALSE(ec);
    REQUIRE(out == std::vector<uint8_t>{0x01}); // whatever was available is still returned
    REQUIRE(timed_out);
}

TEST_CASE("handle_read neither completes nor times out while still within the timeout window",
          "[uart][REQ-UART-003]") {
    UartEndpoint ep;
    REQUIRE_FALSE(ep.rx_fill({0x01}));

    std::vector<uint8_t> out;
    bool timed_out = true;
    auto ec = ep.handle_read(/*read_size=*/4, /*elapsed_ms=*/10, /*uart_timeout_ms=*/50, out, timed_out);
    REQUIRE_FALSE(ec);
    REQUIRE_FALSE(timed_out);
}

TEST_CASE("handle_read rejects a read_size beyond kMaxReadSize", "[uart][REQ-UART-003]") {
    UartEndpoint ep;
    std::vector<uint8_t> out;
    bool timed_out = false;
    auto ec = ep.handle_read(static_cast<uint16_t>(kMaxReadSize + 1), 0, 100, out, timed_out);
    REQUIRE(ec == make_error_code(UartErrc::read_size_exceeds_bound));
}

// ── Payload-less "pure" read requests ────────────────────────────────────────

TEST_CASE("handle_pure_read drains everything currently buffered, non-blocking",
          "[uart][REQ-UART-004]") {
    UartEndpoint ep;
    REQUIRE_FALSE(ep.rx_fill({0x01, 0x02, 0x03}));

    auto out = ep.handle_pure_read();
    REQUIRE(out == std::vector<uint8_t>{0x01, 0x02, 0x03});
    REQUIRE(ep.rx_available() == 0);
}

TEST_CASE("handle_pure_read returns empty when the RX FIFO is empty", "[uart][REQ-UART-004]") {
    UartEndpoint ep;
    REQUIRE(ep.handle_pure_read().empty());
}

// ── Sub-octet bit-width padding rules ────────────────────────────────────────

TEST_CASE("pack_frame_to_octet masks a value to bits_per_frame low bits", "[uart][REQ-UART-005]") {
    uint8_t out = 0xFF;
    auto ec = pack_frame_to_octet(0b11111111, 5, out);
    REQUIRE_FALSE(ec);
    REQUIRE(out == 0b00011111); // low 5 bits kept, high bits zero-padded
}

TEST_CASE("unpack_frame_bits round-trips pack_frame_to_octet", "[uart][REQ-UART-005]") {
    uint8_t packed = 0;
    REQUIRE_FALSE(pack_frame_to_octet(0b01011010, 6, packed));
    uint8_t value = 0xFF;
    REQUIRE_FALSE(unpack_frame_bits(packed, 6, value));
    REQUIRE(value == (0b01011010 & 0x3F));
}

// WIDENED (Phase 3 content correction, matching c-RCP's RCP_EP_UART_NR_BITS_
// MIN..MAX): this range used to be the pre-rewrite, cpp-RCP-only [5,8];
// c-RCP's own file header is explicit that 1..8 is exactly what this
// one-byte-per-word wire representation can carry, so a genuinely valid
// 1-4-bit UART word width is no longer rejected here.
TEST_CASE("pack_frame_to_octet rejects bits_per_frame outside [1,8]", "[uart][REQ-UART-005]") {
    uint8_t out = 0;
    REQUIRE(pack_frame_to_octet(0x01, 0, out) == make_error_code(UartErrc::bits_per_frame_out_of_range));
    REQUIRE(pack_frame_to_octet(0x01, 9, out) == make_error_code(UartErrc::bits_per_frame_out_of_range));
    REQUIRE_FALSE(pack_frame_to_octet(0x01, 4, out)); // now valid — see the file header
}

TEST_CASE("pack_frame_to_octet with bits_per_frame=8 is a full-octet passthrough",
          "[uart][REQ-UART-005]") {
    uint8_t out = 0;
    REQUIRE_FALSE(pack_frame_to_octet(0xAB, 8, out));
    REQUIRE(out == 0xAB);
}

// ── Single-AVTPDU accepted-limitation bound ──────────────────────────────────

// FIXED (REQ-UART-034): kMaxReadSize is now the ACF header's own real 12-bit
// read_size_or_segment_num width, decoupled from this convenience class's
// own, separate kRxFifoCapacity/kTxQueueCapacity bound — see the file header.
TEST_CASE("kMaxReadSize is the ACF header's real 12-bit width, decoupled from the queue capacities",
          "[uart][REQ-UART-006][REQ-UART-034]") {
    REQUIRE(kMaxReadSize == 0x0FFFu);
    REQUIRE(kRxFifoCapacity == 512);
    REQUIRE(kTxQueueCapacity == 512);
}

// ── UartErrc category sanity ──────────────────────────────────────────────────

TEST_CASE("UartErrc reports a non-empty message in its own category", "[uart][REQ-UART-007]") {
    auto ec = make_error_code(UartErrc::rx_fifo_overflow);
    REQUIRE(ec.category() == uart_category());
    REQUIRE_FALSE(ec.message().empty());
}

// ── Table 33 Row 2 evt[2:0] validation (handle_request) ─────────────────────

TEST_CASE("UartEndpoint::handle_request delegates a Plain (evt[2:0]==000b) write request to "
          "enqueue_tx()",
          "[uart][REQ-UART-008]") {
    UartEndpoint ep;
    std::vector<uint8_t> data;
    bool timed_out = true;

    auto ec = ep.handle_request(/*evt_op=*/0, /*is_write=*/true, {0x01, 0x02}, /*read_size=*/0,
                                 /*elapsed_ms=*/0, /*uart_timeout_ms=*/0, data, timed_out);
    REQUIRE_FALSE(ec);
    REQUIRE(ep.drain_tx() == std::vector<uint8_t>{0x01, 0x02});
    // A write request produces no read data — out_data/out_timed_out are
    // left exactly as the caller passed them.
    REQUIRE(data.empty());
    REQUIRE(timed_out);
}

TEST_CASE("UartEndpoint::handle_request delegates a Plain (evt[2:0]==000b) read request to "
          "handle_read()",
          "[uart][REQ-UART-008]") {
    UartEndpoint ep;
    REQUIRE_FALSE(ep.rx_fill({0xAA, 0xBB, 0xCC}));

    std::vector<uint8_t> data;
    bool timed_out = true;
    auto ec = ep.handle_request(/*evt_op=*/0, /*is_write=*/false, /*tx_bytes=*/{}, /*read_size=*/2,
                                 /*elapsed_ms=*/1, /*uart_timeout_ms=*/100, data, timed_out);
    REQUIRE_FALSE(ec);
    REQUIRE(data == std::vector<uint8_t>{0xAA, 0xBB});
    REQUIRE_FALSE(timed_out);
    REQUIRE(ep.rx_available() == 1); // remaining byte stays buffered (drain semantics)
}

TEST_CASE("UartEndpoint::handle_request rejects every reserved evt[2:0] value (001b-110b) for "
          "both write and read requests, touching neither queue",
          "[uart][REQ-UART-008]") {
    for (uint8_t evt_op = 1; evt_op <= 6; ++evt_op) {
        UartEndpoint write_ep;
        std::vector<uint8_t> write_data;
        bool write_timed_out = false;
        auto write_ec = write_ep.handle_request(evt_op, /*is_write=*/true, {0xAA}, 0, 0, 0,
                                                  write_data, write_timed_out);
        REQUIRE(write_ec == rcp::endpoint::make_error_code(rcp::endpoint::EndpointErrc::reserved_evt_row2));
        REQUIRE(write_ep.drain_tx().empty());

        UartEndpoint read_ep;
        REQUIRE_FALSE(read_ep.rx_fill({0x11, 0x22}));
        std::vector<uint8_t> read_data;
        bool read_timed_out = false;
        auto read_ec = read_ep.handle_request(evt_op, /*is_write=*/false, {}, /*read_size=*/2, 0, 0,
                                                read_data, read_timed_out);
        REQUIRE(read_ec == rcp::endpoint::make_error_code(rcp::endpoint::EndpointErrc::reserved_evt_row2));
        // A rejected reserved evt must not touch the RX FIFO — the bytes
        // scripted above are still fully buffered.
        REQUIRE(read_ep.rx_available() == 2);
    }
}

TEST_CASE("UartEndpoint::handle_request reports config_write_not_supported for evt[2:0]==111b "
          "without crashing or touching either queue",
          "[uart][REQ-UART-009]") {
    UartEndpoint write_ep;
    std::vector<uint8_t> write_data;
    bool write_timed_out = false;
    auto write_ec =
        write_ep.handle_request(/*evt_op=*/7, /*is_write=*/true, {0x00, 0xAB}, 0, 0, 0, write_data, write_timed_out);
    REQUIRE(write_ec == make_error_code(UartErrc::config_write_not_supported));
    REQUIRE(write_ep.drain_tx().empty());

    UartEndpoint read_ep;
    REQUIRE_FALSE(read_ep.rx_fill({0x11}));
    std::vector<uint8_t> read_data;
    bool read_timed_out = false;
    auto read_ec =
        read_ep.handle_request(/*evt_op=*/7, /*is_write=*/false, {}, /*read_size=*/1, 0, 0, read_data, read_timed_out);
    REQUIRE(read_ec == make_error_code(UartErrc::config_write_not_supported));
    REQUIRE(read_ep.rx_available() == 1);
}

TEST_CASE("UartEndpoint::handle_request masks evt_op down to 3 bits before classifying",
          "[uart][REQ-UART-008]") {
    UartEndpoint ep;
    std::vector<uint8_t> data;
    bool timed_out = false;
    REQUIRE_FALSE(ep.handle_request(/*evt_op=*/0xF8, /*is_write=*/true, {0x01}, 0, 0, 0, data,
                                     timed_out)); // low 3 bits 000 -> Plain
    auto ec = ep.handle_request(/*evt_op=*/0xF9, /*is_write=*/true, {0x01}, 0, 0, 0, data,
                                 timed_out); // low 3 bits 001 -> Reserved
    REQUIRE(ec == rcp::endpoint::make_error_code(rcp::endpoint::EndpointErrc::reserved_evt_row2));
}

TEST_CASE("UartErrc::config_write_not_supported reports a non-empty message in its own category",
          "[uart][REQ-UART-009]") {
    auto ec = make_error_code(UartErrc::config_write_not_supported);
    REQUIRE(ec.category() == uart_category());
    REQUIRE_FALSE(ec.message().empty());
}

// ═══════════════════════════════════════════════════════════════════════════
// Phase 3 content-correction pass — ported from c-RCP's tests/test_ep_uart.c
// (this project's RC5-spec-conformant reference), covering the free-function
// ACF wire codec, functional config, Table 51 register block, Table 52
// triggers, REQ-UART-037's three-value StopBits enum, and the Phase 20
// fragmentation wiring rcp/uart.hpp previously had none of at all.
// ═══════════════════════════════════════════════════════════════════════════

// ── Word format / bit-padding ────────────────────────────────────────────────

TEST_CASE("nr_bits_valid accepts 1..8 and rejects everything else", "[uart][REQ-UART-001]") {
    REQUIRE_FALSE(nr_bits_valid(0));
    for (uint8_t v = 1; v <= 8; ++v) REQUIRE(nr_bits_valid(v));
    REQUIRE_FALSE(nr_bits_valid(9));
    REQUIRE_FALSE(nr_bits_valid(255));
}

TEST_CASE("bit_pad_mask values match c-RCP's rcp_ep_uart_bit_pad_mask", "[uart][REQ-UART-002]") {
    REQUIRE(bit_pad_mask(1) == 0x01);
    REQUIRE(bit_pad_mask(5) == 0x1F);
    REQUIRE(bit_pad_mask(7) == 0x7F);
    REQUIRE(bit_pad_mask(8) == 0xFF);
    REQUIRE(bit_pad_mask(0) == 0x00);
    REQUIRE(bit_pad_mask(9) == 0x00);
}

TEST_CASE("apply_bit_padding masks every byte in place", "[uart][REQ-UART-003]") {
    uint8_t buf[3] = {0xFF, 0xFF, 0xFF};
    apply_bit_padding(buf, sizeof(buf), 7);
    REQUIRE(buf[0] == 0x7F);
    REQUIRE(buf[1] == 0x7F);
    REQUIRE(buf[2] == 0x7F);
}

TEST_CASE("apply_bit_padding is a no-op for nr_bits == 8", "[uart][REQ-UART-003]") {
    uint8_t buf[2] = {0xAB, 0xCD};
    apply_bit_padding(buf, sizeof(buf), 8);
    REQUIRE(buf[0] == 0xAB);
    REQUIRE(buf[1] == 0xCD);
}

TEST_CASE("apply_bit_padding zeroes the buffer for an invalid nr_bits", "[uart][REQ-UART-003]") {
    uint8_t buf[2] = {0xAB, 0xCD};
    apply_bit_padding(buf, sizeof(buf), 0);
    REQUIRE(buf[0] == 0x00);
    REQUIRE(buf[1] == 0x00);
}

// ── HW trigger signals (§13.7.8.4 Table 52) ─────────────────────────────────

TEST_CASE("trigger_fires: None never fires", "[uart][REQ-UART-041]") {
    REQUIRE_FALSE(trigger_fires(UartTrigger::None, UartEvent::TxRequestFinalized));
    REQUIRE_FALSE(trigger_fires(UartTrigger::None, UartEvent::ReadRequestFinalized));
}

TEST_CASE("trigger_fires: TxFinalized fires only on its own event", "[uart][REQ-UART-042]") {
    REQUIRE(trigger_fires(UartTrigger::TxFinalized, UartEvent::TxRequestFinalized));
    REQUIRE_FALSE(trigger_fires(UartTrigger::TxFinalized, UartEvent::ReadRequestFinalized));
}

TEST_CASE("trigger_fires: RxFinalized fires only on its own event", "[uart][REQ-UART-043]") {
    REQUIRE(trigger_fires(UartTrigger::RxFinalized, UartEvent::ReadRequestFinalized));
    REQUIRE_FALSE(trigger_fires(UartTrigger::RxFinalized, UartEvent::TxRequestFinalized));
}

TEST_CASE("Table 52 off-by-one is preserved: TxFinalized==1 (signal 0), RxFinalized==2 (signal 1)",
          "[uart][REQ-UART-041]") {
    REQUIRE(static_cast<uint8_t>(UartTrigger::None) == 0);
    REQUIRE(static_cast<uint8_t>(UartTrigger::TxFinalized) == 1);
    REQUIRE(static_cast<uint8_t>(UartTrigger::RxFinalized) == 2);
}

// ── Functional config ─────────────────────────────────────────────────────────

TEST_CASE("functional_cfg_init zeroes every field except uart_nr_bits", "[uart][REQ-UART-004]") {
    UartFunctionalCfg cfg;
    cfg.ep_enable = cfg.ep_clear_req_storage = cfg.ep_req_crc_enable = true;
    cfg.baud_rate = 115200;
    cfg.parity = static_cast<uint8_t>(Parity::Even);
    cfg.stop_bits = static_cast<uint8_t>(StopBits::Two);
    cfg.ep_rx_buffer_size = 256;
    cfg.uart_timeout_ms = 50;
    cfg.ep_status = 0xBEEF;
    cfg.baud_rate_kbps = 0xAAAA;
    cfg.rts_enable = cfg.cts_enable = cfg.half_duplex = true;
    cfg.wire_timeout_bit_times = 9;
    cfg.trail = 10;
    cfg.trigger = UartTrigger::RxFinalized;

    functional_cfg_init(cfg);

    REQUIRE_FALSE(cfg.ep_enable);
    REQUIRE_FALSE(cfg.ep_clear_req_storage);
    REQUIRE_FALSE(cfg.ep_req_crc_enable);
    REQUIRE_FALSE(cfg.ep_response_ts_enable);
    REQUIRE_FALSE(cfg.ep_suppress_response);
    REQUIRE(cfg.baud_rate == 0);
    REQUIRE(cfg.parity == static_cast<uint8_t>(Parity::None));
    REQUIRE(cfg.stop_bits == static_cast<uint8_t>(StopBits::One));
    REQUIRE(cfg.ep_rx_buffer_size == 0);
    REQUIRE(cfg.uart_timeout_ms == 0);
    REQUIRE(cfg.uart_nr_bits == kNrBitsMax);
    REQUIRE(cfg.ep_status == 0);
    REQUIRE(cfg.baud_rate_kbps == 0);
    REQUIRE_FALSE(cfg.rts_enable);
    REQUIRE_FALSE(cfg.cts_enable);
    REQUIRE_FALSE(cfg.half_duplex);
    REQUIRE(cfg.wire_timeout_bit_times == 0);
    REQUIRE(cfg.trail == 0);
    REQUIRE(cfg.trigger == UartTrigger::None);
}

TEST_CASE("functional_cfg_writable is false in HwUnconfigured regardless of writer",
          "[uart][REQ-UART-005]") {
    rcp::lifecycle::WriterCtx writer;
    writer.via_root_client_ep0 = true;
    writer.via_owning_stream   = true;
    REQUIRE_FALSE(functional_cfg_writable(rcp::lifecycle::ServerState::HwUnconfigured, writer));
}

TEST_CASE("functional_cfg_writable in HwConfigured requires EP0/owning-stream/discovery-stream",
          "[uart][REQ-UART-006]") {
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
          "[uart][REQ-UART-007]") {
    rcp::lifecycle::WriterCtx none, via_ep0, via_stream;
    via_ep0.via_root_client_ep0  = true;
    via_stream.via_owning_stream = true;

    REQUIRE_FALSE(functional_cfg_writable(rcp::lifecycle::ServerState::RcpConfigured, none));
    REQUIRE(functional_cfg_writable(rcp::lifecycle::ServerState::RcpConfigured, via_ep0));
    REQUIRE(functional_cfg_writable(rcp::lifecycle::ServerState::RcpConfigured, via_stream));
}

TEST_CASE("set_baud_rate rejects an unauthorized writer", "[uart][REQ-UART-008]") {
    UartFunctionalCfg cfg;
    functional_cfg_init(cfg);
    rcp::lifecycle::WriterCtx none;

    REQUIRE_FALSE(set_baud_rate(cfg, 115200, rcp::lifecycle::ServerState::HwUnconfigured, none));
    REQUIRE(cfg.baud_rate == 0);
}

TEST_CASE("set_baud_rate applies when authorized", "[uart][REQ-UART-009]") {
    UartFunctionalCfg cfg;
    functional_cfg_init(cfg);
    rcp::lifecycle::WriterCtx writer;
    writer.via_owning_stream = true;

    REQUIRE(set_baud_rate(cfg, 115200, rcp::lifecycle::ServerState::HwConfigured, writer));
    REQUIRE(cfg.baud_rate == 115200);
}

TEST_CASE("set_frame_format rejects an invalid nr_bits", "[uart][REQ-UART-010]") {
    UartFunctionalCfg cfg;
    functional_cfg_init(cfg);
    rcp::lifecycle::WriterCtx writer;

    REQUIRE_FALSE(set_frame_format(cfg, 0, Parity::Even, StopBits::Two,
                                    rcp::lifecycle::ServerState::HwConfigured, writer));
    REQUIRE(cfg.uart_nr_bits == kNrBitsMax);
    REQUIRE(cfg.parity == static_cast<uint8_t>(Parity::None));
}

TEST_CASE("set_frame_format rejects an unauthorized writer", "[uart][REQ-UART-011]") {
    UartFunctionalCfg cfg;
    functional_cfg_init(cfg);
    rcp::lifecycle::WriterCtx none;

    REQUIRE_FALSE(set_frame_format(cfg, 7, Parity::Odd, StopBits::One,
                                    rcp::lifecycle::ServerState::HwUnconfigured, none));
    REQUIRE(cfg.uart_nr_bits == kNrBitsMax);
}

TEST_CASE("set_frame_format applies when valid and authorized", "[uart][REQ-UART-012]") {
    UartFunctionalCfg cfg;
    functional_cfg_init(cfg);
    rcp::lifecycle::WriterCtx writer;
    writer.via_owning_stream = true;

    REQUIRE(set_frame_format(cfg, 7, Parity::Even, StopBits::Two,
                              rcp::lifecycle::ServerState::HwConfigured, writer));
    REQUIRE(cfg.uart_nr_bits == 7);
    REQUIRE(cfg.parity == static_cast<uint8_t>(Parity::Even));
    REQUIRE(cfg.stop_bits == static_cast<uint8_t>(StopBits::Two));
}

TEST_CASE("set_rx_buffer_size rejects an unauthorized writer", "[uart][REQ-UART-013]") {
    UartFunctionalCfg cfg;
    functional_cfg_init(cfg);
    rcp::lifecycle::WriterCtx none;

    REQUIRE_FALSE(set_rx_buffer_size(cfg, 256, rcp::lifecycle::ServerState::HwUnconfigured, none));
    REQUIRE(cfg.ep_rx_buffer_size == 0);
}

TEST_CASE("set_rx_buffer_size applies when authorized", "[uart][REQ-UART-014]") {
    UartFunctionalCfg cfg;
    functional_cfg_init(cfg);
    rcp::lifecycle::WriterCtx writer;
    writer.via_owning_stream = true;

    REQUIRE(set_rx_buffer_size(cfg, 256, rcp::lifecycle::ServerState::HwConfigured, writer));
    REQUIRE(cfg.ep_rx_buffer_size == 256);
}

TEST_CASE("set_timeout rejects an unauthorized writer", "[uart][REQ-UART-015]") {
    UartFunctionalCfg cfg;
    functional_cfg_init(cfg);
    rcp::lifecycle::WriterCtx none;

    REQUIRE_FALSE(set_timeout(cfg, 50, rcp::lifecycle::ServerState::HwUnconfigured, none));
    REQUIRE(cfg.uart_timeout_ms == 0);
}

TEST_CASE("set_timeout applies when authorized", "[uart][REQ-UART-016]") {
    UartFunctionalCfg cfg;
    functional_cfg_init(cfg);
    rcp::lifecycle::WriterCtx writer;
    writer.via_owning_stream = true;

    REQUIRE(set_timeout(cfg, 50, rcp::lifecycle::ServerState::HwConfigured, writer));
    REQUIRE(cfg.uart_timeout_ms == 50);
}

TEST_CASE("set_trigger rejects an unauthorized writer", "[uart][REQ-UART-044]") {
    UartFunctionalCfg cfg;
    functional_cfg_init(cfg);
    rcp::lifecycle::WriterCtx none;

    REQUIRE_FALSE(set_trigger(cfg, UartTrigger::TxFinalized, rcp::lifecycle::ServerState::HwUnconfigured, none));
    REQUIRE(cfg.trigger == UartTrigger::None);
}

TEST_CASE("set_trigger applies when authorized", "[uart][REQ-UART-045]") {
    UartFunctionalCfg cfg;
    functional_cfg_init(cfg);
    rcp::lifecycle::WriterCtx writer;
    writer.via_owning_stream = true;

    REQUIRE(set_trigger(cfg, UartTrigger::RxFinalized, rcp::lifecycle::ServerState::HwConfigured, writer));
    REQUIRE(cfg.trigger == UartTrigger::RxFinalized);
}

// ── The EP_func register block (§13.7.8.2 Table 51) ──────────────────────────

TEST_CASE("render_registers matches Table 51's own offsets", "[uart][REQ-UART-036][REQ-UART-038]") {
    UartFunctionalCfg cfg;
    functional_cfg_init(cfg);
    cfg.ep_enable             = true;
    cfg.ep_status             = 0x1234;
    cfg.baud_rate_kbps        = 0x5566;
    cfg.uart_nr_bits          = 7;
    cfg.parity                = static_cast<uint8_t>(Parity::Even);
    cfg.rts_enable            = true;
    cfg.cts_enable            = true;
    cfg.half_duplex           = true;
    cfg.stop_bits             = static_cast<uint8_t>(StopBits::Two);
    cfg.wire_timeout_bit_times = 9;
    cfg.trail                 = 10;

    EpFuncBlock out{};
    render_registers(cfg, out);

    REQUIRE(out[kRegEpLen] == static_cast<uint8_t>(kEpFuncLen));
    REQUIRE(out[kRegReserved01] == 0);
    REQUIRE((out[kRegEpEnableClr] & 0x01) != 0);
    REQUIRE(out[kRegEpStatus] == 0x12);
    REQUIRE(out[kRegEpStatus + 1] == 0x34);
    REQUIRE(out[kRegBaudRate] == 0x55);
    REQUIRE(out[kRegBaudRate + 1] == 0x66);
    REQUIRE(out[kRegNrBits] == 7);
    REQUIRE(out[kRegFlags] == (kFlagParityEnable | kFlagParityPol | kFlagRtsEnable | kFlagCtsEnable |
                                kFlagHalfDuplex));
    REQUIRE(out[kRegStopBits] == 4); // TWO -> half units 4
    REQUIRE(out[kRegTimeout] == 9);
    REQUIRE(out[kRegTrail] == 10);
    REQUIRE(kEpFuncLen == 0x000Du);
}

TEST_CASE("apply_reconfig writes a multi-register span", "[uart][REQ-UART-039][REQ-UART-040]") {
    UartFunctionalCfg cfg;
    functional_cfg_init(cfg);
    const uint8_t payload[9] = {
        0x00, static_cast<uint8_t>(kRegBaudRate),
        0xAB, 0xCD,                                              // baud_rate_kbps
        6,                                                       // nr_bits
        static_cast<uint8_t>(kFlagParityEnable | kFlagRtsEnable), // odd parity, RTS
        4,                                                       // stop_bits half units -> TWO
        11,                                                      // timeout
        12,                                                      // trail
    };

    REQUIRE_FALSE(apply_reconfig(cfg, payload, sizeof(payload)));
    REQUIRE(cfg.baud_rate_kbps == 0xABCD);
    REQUIRE(cfg.uart_nr_bits == 6);
    REQUIRE(cfg.parity == static_cast<uint8_t>(Parity::Odd));
    REQUIRE(cfg.rts_enable);
    REQUIRE_FALSE(cfg.cts_enable);
    REQUIRE_FALSE(cfg.half_duplex);
    REQUIRE(cfg.stop_bits == static_cast<uint8_t>(StopBits::Two));
    REQUIRE(cfg.wire_timeout_bit_times == 11);
    REQUIRE(cfg.trail == 12);
}

// REQ-UART-037/049: the real, three-value StopBits mapping — ONE_HALF (wire
// value 3) is now exact, not rounded up to TWO; only an out-of-range value
// (5) still falls back to the conservative TWO default.
TEST_CASE("apply_reconfig maps all three legal stop_bits register values exactly",
          "[uart][REQ-UART-049]") {
    UartFunctionalCfg cfg;

    const uint8_t payload_one[3]           = {0x00, static_cast<uint8_t>(kRegStopBits), 2};
    functional_cfg_init(cfg);
    REQUIRE_FALSE(apply_reconfig(cfg, payload_one, sizeof(payload_one)));
    REQUIRE(cfg.stop_bits == static_cast<uint8_t>(StopBits::One));

    const uint8_t payload_one_half[3]      = {0x00, static_cast<uint8_t>(kRegStopBits), 3};
    functional_cfg_init(cfg);
    REQUIRE_FALSE(apply_reconfig(cfg, payload_one_half, sizeof(payload_one_half)));
    REQUIRE(cfg.stop_bits == static_cast<uint8_t>(StopBits::OneHalf));

    const uint8_t payload_two[3]           = {0x00, static_cast<uint8_t>(kRegStopBits), 4};
    functional_cfg_init(cfg);
    REQUIRE_FALSE(apply_reconfig(cfg, payload_two, sizeof(payload_two)));
    REQUIRE(cfg.stop_bits == static_cast<uint8_t>(StopBits::Two));

    const uint8_t payload_out_of_range[3]  = {0x00, static_cast<uint8_t>(kRegStopBits), 5};
    functional_cfg_init(cfg);
    REQUIRE_FALSE(apply_reconfig(cfg, payload_out_of_range, sizeof(payload_out_of_range)));
    REQUIRE(cfg.stop_bits == static_cast<uint8_t>(StopBits::Two));
}

TEST_CASE("render_registers: StopBits::OneHalf renders as the distinct register value 3",
          "[uart][REQ-UART-049]") {
    UartFunctionalCfg cfg;
    functional_cfg_init(cfg);
    cfg.stop_bits = static_cast<uint8_t>(StopBits::OneHalf);

    EpFuncBlock out{};
    render_registers(cfg, out);
    REQUIRE(out[kRegStopBits] == 3);
}

TEST_CASE("apply_reconfig ignores read-only registers (EP_LEN/reserved)", "[uart][REQ-UART-040]") {
    UartFunctionalCfg cfg;
    functional_cfg_init(cfg);
    const uint8_t payload[4] = {0x00, 0x00, 0xFF, 0xFF};

    REQUIRE_FALSE(apply_reconfig(cfg, payload, sizeof(payload)));

    EpFuncBlock out{};
    render_registers(cfg, out);
    REQUIRE(out[kRegEpLen] == static_cast<uint8_t>(kEpFuncLen));
    REQUIRE(out[kRegReserved01] == 0);
}

TEST_CASE("apply_reconfig rejects a write extending past EP_LEN", "[uart][REQ-UART-040]") {
    UartFunctionalCfg cfg;
    functional_cfg_init(cfg);
    const uint8_t payload[3] = {0x00, static_cast<uint8_t>(kEpFuncLen), 0xFF};

    REQUIRE(apply_reconfig(cfg, payload, sizeof(payload)) == make_error_code(UartReconfigErrc::out_of_range));
    REQUIRE(cfg.trail == 0);
}

TEST_CASE("apply_reconfig rejects a payload with no data octet", "[uart][REQ-UART-040]") {
    UartFunctionalCfg cfg;
    functional_cfg_init(cfg);
    const uint8_t addr_only[2] = {0x00, 0x06};

    REQUIRE(apply_reconfig(cfg, addr_only, sizeof(addr_only)) ==
            make_error_code(UartReconfigErrc::short_payload));
    REQUIRE(apply_reconfig(cfg, nullptr, 0) == make_error_code(UartReconfigErrc::short_payload));
}

TEST_CASE("encode_reconfig_request round-trips through acf::decode_acf_abb", "[uart][REQ-UART-039]") {
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

TEST_CASE("encode_reconfig_request rejects empty data", "[uart][REQ-UART-039]") {
    REQUIRE(encode_reconfig_request(0x00, 0, {}, 0).empty());
}

TEST_CASE("uart reconfig error category reports a distinct, non-empty message per code",
          "[uart][REQ-UART-040]") {
    auto short_ec = make_error_code(UartReconfigErrc::short_payload);
    auto range_ec = make_error_code(UartReconfigErrc::out_of_range);
    REQUIRE_FALSE(short_ec.message().empty());
    REQUIRE_FALSE(range_ec.message().empty());
    REQUIRE(short_ec.message() != range_ec.message());
}

// ── UartErrc category sanity (full, extended enum) ────────────────────────────

TEST_CASE("UartErrc reports a non-empty, distinct message per code", "[uart][REQ-UART-017]") {
    const UartErrc codes[] = {
        UartErrc::read_size_exceeds_bound, UartErrc::rx_fifo_overflow,
        UartErrc::tx_queue_overflow,       UartErrc::bits_per_frame_out_of_range,
        UartErrc::config_write_not_supported, UartErrc::short_frame,
        UartErrc::bad_msg_type,            UartErrc::wrong_bus,
        UartErrc::wrong_op,                UartErrc::unknown_cmd,
        UartErrc::bad_evt,
    };
    std::vector<std::string> seen;
    for (auto c : codes) {
        auto ec = make_error_code(c);
        REQUIRE(ec.category() == uart_category());
        REQUIRE_FALSE(ec.message().empty());
        for (const auto& s : seen) REQUIRE(s != ec.message());
        seen.push_back(ec.message());
    }
    REQUIRE_FALSE(make_error_code(static_cast<UartErrc>(999)).message().empty());
}

TEST_CASE("wire_error maps bad_evt/unknown_cmd to UnsupportedCmd", "[uart][REQ-UART-020]") {
    REQUIRE(wire_error(UartErrc::bad_evt) == rcp::acf::WireErrorCode::UnsupportedCmd);
    REQUIRE(wire_error(UartErrc::unknown_cmd) == rcp::acf::WireErrorCode::UnsupportedCmd);
    REQUIRE_FALSE(wire_error(UartErrc::short_frame).has_value());
}

// ── TX: write request/response ────────────────────────────────────────────── ──

TEST_CASE("encode_write_request / decode_write_request round-trip", "[uart][REQ-UART-018][REQ-UART-019]") {
    const std::vector<uint8_t> tx{0x01, 0x02, 0x03};
    const auto frame = encode_write_request(4, tx, 9);
    REQUIRE_FALSE(frame.empty());

    std::vector<uint8_t> out_tx;
    uint8_t txn = 0;
    REQUIRE_FALSE(decode_write_request(frame.data(), frame.size(), 4, out_tx, txn));
    REQUIRE(out_tx == tx);
    REQUIRE(txn == 9);
}

TEST_CASE("decode_write_request rejects wrong_bus/wrong_op/bad_msg_type/short_frame",
          "[uart][REQ-UART-020]") {
    const std::vector<uint8_t> tx{0xAB};
    const auto wrong_bus_frame = encode_write_request(4, tx, 0);
    std::vector<uint8_t> out_tx;
    uint8_t txn = 0;
    REQUIRE(decode_write_request(wrong_bus_frame.data(), wrong_bus_frame.size(), 5, out_tx, txn) ==
            make_error_code(UartErrc::wrong_bus));

    rcp::acf::AcfMessageInfo wrong_op_hdr;
    wrong_op_hdr.byte_bus_id = 4;
    wrong_op_hdr.op           = false; // read, not write
    const auto wrong_op_frame = rcp::acf::encode_acf_abb(wrong_op_hdr, {});
    REQUIRE(decode_write_request(wrong_op_frame.data(), wrong_op_frame.size(), 4, out_tx, txn) ==
            make_error_code(UartErrc::wrong_op));

    rcp::acf::AcfMessageInfo bad_type_hdr;
    bad_type_hdr.byte_bus_id = 4;
    bad_type_hdr.op           = true;
    const auto bad_type_frame = rcp::acf::encode_acf_gbb(bad_type_hdr, 0, {});
    REQUIRE(decode_write_request(bad_type_frame.data(), bad_type_frame.size(), 4, out_tx, txn) ==
            make_error_code(UartErrc::bad_msg_type));

    const uint8_t too_short[2] = {0x1C, 0x01}; // valid ACF_ABB type marker, too short for the full header
    REQUIRE(decode_write_request(too_short, sizeof(too_short), 4, out_tx, txn) ==
            make_error_code(UartErrc::short_frame));
}

TEST_CASE("decode_write_request rejects a nonzero evt[2:0]", "[uart][REQ-UART-020]") {
    rcp::acf::AcfMessageInfo hdr;
    hdr.byte_bus_id = 4;
    hdr.op            = true;
    hdr.evt_op         = 0x6; // reserved in UART's own Table 33 row
    const auto frame  = rcp::acf::encode_acf_abb(hdr, {});

    std::vector<uint8_t> out_tx;
    uint8_t txn = 0;
    REQUIRE(decode_write_request(frame.data(), frame.size(), 4, out_tx, txn) ==
            make_error_code(UartErrc::bad_evt));
}

TEST_CASE("encode_write_response / decode_write_response round-trip untimed and timed",
          "[uart][REQ-UART-021][REQ-UART-022]") {
    const std::vector<uint8_t> accepted{0x55, 0x66};
    const auto untimed = encode_write_response(3, accepted, 4, false, 0);
    const auto timed_frame = encode_write_response(3, accepted, 4, true, 0xAABBCCDDull);

    std::vector<uint8_t> out_accepted;
    bool timed = true;
    uint64_t ts = 1;
    uint8_t txn = 0;
    REQUIRE_FALSE(decode_write_response(untimed.data(), untimed.size(), 3, out_accepted, timed, ts, txn));
    REQUIRE(out_accepted == accepted);
    REQUIRE_FALSE(timed);
    REQUIRE(ts == 0);

    REQUIRE_FALSE(decode_write_response(timed_frame.data(), timed_frame.size(), 3, out_accepted, timed, ts, txn));
    REQUIRE(timed);
    REQUIRE(ts == 0xAABBCCDDull);
    REQUIRE(txn == 4);
}

TEST_CASE("decode_write_response rejects wrong_bus and short_frame", "[uart][REQ-UART-046]") {
    const auto frame = encode_write_response(3, {}, 0, false, 0);
    std::vector<uint8_t> out_accepted;
    bool timed = false;
    uint64_t ts = 0;
    uint8_t txn = 0;
    REQUIRE(decode_write_response(frame.data(), frame.size(), 9, out_accepted, timed, ts, txn) ==
            make_error_code(UartErrc::wrong_bus));

    const uint8_t too_short[2] = {static_cast<uint8_t>(rcp::acf::kAcfMsgTypeAbb << 1), 0};
    REQUIRE(decode_write_response(too_short, sizeof(too_short), 3, out_accepted, timed, ts, txn) ==
            make_error_code(UartErrc::short_frame));
}

// ── RX: read request/response ─────────────────────────────────────────────── ──

TEST_CASE("encode_read_request / decode_read_request round-trip", "[uart][REQ-UART-023][REQ-UART-024]") {
    const auto frame = encode_read_request(6, 64, 3);
    REQUIRE_FALSE(frame.empty());

    uint16_t read_size = 0;
    uint8_t txn = 0;
    REQUIRE_FALSE(decode_read_request(frame.data(), frame.size(), 6, read_size, txn));
    REQUIRE(read_size == 64);
    REQUIRE(txn == 3);
}

// FIXED (REQ-UART-034): a read_size above 255 — previously inexpressible had
// this parameter been narrowed to uint8_t — round-trips through the ACF
// header's real 12-bit field.
TEST_CASE("encode_read_request / decode_read_request round-trip a read_size above 255",
          "[uart][REQ-UART-023][REQ-UART-024][REQ-UART-034]") {
    const auto frame = encode_read_request(6, 4000u, 3);
    REQUIRE_FALSE(frame.empty());

    uint16_t read_size = 0;
    uint8_t txn = 0;
    REQUIRE_FALSE(decode_read_request(frame.data(), frame.size(), 6, read_size, txn));
    REQUIRE(read_size == 4000u);
    REQUIRE(txn == 3);
}

TEST_CASE("decode_read_request rejects a payload-bearing read request with unknown_cmd",
          "[uart][REQ-UART-025]") {
    rcp::acf::AcfMessageInfo hdr;
    hdr.byte_bus_id                = 6;
    hdr.op                          = false;
    hdr.read_size_or_segment_num   = 8;
    const auto frame = rcp::acf::encode_acf_abb(hdr, {0x01});

    uint16_t read_size = 0;
    uint8_t txn = 0;
    REQUIRE(decode_read_request(frame.data(), frame.size(), 6, read_size, txn) ==
            make_error_code(UartErrc::unknown_cmd));
}

TEST_CASE("decode_read_request rejects wrong_bus/wrong_op/short_frame", "[uart][REQ-UART-025]") {
    const auto wrong_bus = encode_read_request(6, 8, 0);
    uint16_t read_size = 0;
    uint8_t txn = 0;
    REQUIRE(decode_read_request(wrong_bus.data(), wrong_bus.size(), 7, read_size, txn) ==
            make_error_code(UartErrc::wrong_bus));

    rcp::acf::AcfMessageInfo wrong_op_hdr;
    wrong_op_hdr.byte_bus_id = 6;
    wrong_op_hdr.op           = true; // write, not read
    const auto wrong_op_frame = rcp::acf::encode_acf_abb(wrong_op_hdr, {});
    REQUIRE(decode_read_request(wrong_op_frame.data(), wrong_op_frame.size(), 6, read_size, txn) ==
            make_error_code(UartErrc::wrong_op));

    const uint8_t too_short[2] = {0x1C, 0x01}; // valid ACF_ABB type marker, too short for the full header
    REQUIRE(decode_read_request(too_short, sizeof(too_short), 6, read_size, txn) ==
            make_error_code(UartErrc::short_frame));
}

TEST_CASE("decode_read_request rejects a nonzero evt[2:0]", "[uart][REQ-UART-025]") {
    rcp::acf::AcfMessageInfo hdr;
    hdr.byte_bus_id = 6;
    hdr.op            = false;
    hdr.evt_op         = 0x5; // reserved in UART's own Table 33 row
    const auto frame  = rcp::acf::encode_acf_abb(hdr, {});

    uint16_t read_size = 0;
    uint8_t txn = 0;
    REQUIRE(decode_read_request(frame.data(), frame.size(), 6, read_size, txn) ==
            make_error_code(UartErrc::bad_evt));
}

TEST_CASE("encode_read_response / decode_read_response round-trip full length",
          "[uart][REQ-UART-026][REQ-UART-027]") {
    const std::vector<uint8_t> rx{0xDE, 0xAD, 0xBE, 0xEF};
    const auto frame = encode_read_response(2, rx, 11, false, 0);
    REQUIRE_FALSE(frame.empty());

    std::vector<uint8_t> out_rx;
    bool timed = true;
    uint64_t ts = 1;
    uint8_t txn = 0;
    REQUIRE_FALSE(decode_read_response(frame.data(), frame.size(), 2, out_rx, timed, ts, txn));
    REQUIRE(out_rx == rx);
    REQUIRE_FALSE(timed);
    REQUIRE(txn == 11);
}

// Worst-case single-AVTPDU short read: the read request asks for read_size
// bytes but the uart_timeout_ms race completes first with fewer bytes
// actually captured — still just one ordinary ACF message, no
// segment_num-based reassembly needed.
TEST_CASE("read response round-trips a short read (fewer bytes than requested)",
          "[uart][REQ-UART-026][REQ-UART-027]") {
    const auto read_req = encode_read_request(2, 32, 5);
    const std::vector<uint8_t> rx{0x01, 0x02, 0x03}; // far fewer than the requested 32
    const auto frame = encode_read_response(2, rx, 5, true, 0x1122334455667788ull);

    uint16_t requested_read_size = 0;
    uint8_t req_txn = 0;
    REQUIRE_FALSE(decode_read_request(read_req.data(), read_req.size(), 2, requested_read_size, req_txn));
    REQUIRE(requested_read_size == 32);

    std::vector<uint8_t> out_rx;
    bool timed = false;
    uint64_t ts = 0;
    uint8_t txn = 0;
    REQUIRE_FALSE(decode_read_response(frame.data(), frame.size(), 2, out_rx, timed, ts, txn));
    REQUIRE(out_rx == rx);
    REQUIRE(out_rx.size() < requested_read_size);
    REQUIRE(timed);
    REQUIRE(ts == 0x1122334455667788ull);
    REQUIRE(txn == 5);
}

TEST_CASE("decode_read_response rejects wrong_bus and short_frame", "[uart][REQ-UART-028]") {
    const auto frame = encode_read_response(2, {}, 0, false, 0);
    std::vector<uint8_t> out_rx;
    bool timed = false;
    uint64_t ts = 0;
    uint8_t txn = 0;
    REQUIRE(decode_read_response(frame.data(), frame.size(), 3, out_rx, timed, ts, txn) ==
            make_error_code(UartErrc::wrong_bus));

    const uint8_t too_short[2] = {static_cast<uint8_t>(rcp::acf::kAcfMsgTypeAbb << 1), 0};
    REQUIRE(decode_read_response(too_short, sizeof(too_short), 2, out_rx, timed, ts, txn) ==
            make_error_code(UartErrc::short_frame));
}

// ── Read-completion arbitration (REQ-UART-033) ──────────────────────────────
// Not covered by c-RCP's own tests/test_ep_uart.c (that coverage lives in
// c-RCP's tests/test_tc18_gaps_ep2.c's test_uart_read_completion_decision());
// ported here since read_completion_decision() is a real production delta
// this pass introduces to rcp/uart.hpp for the first time.

TEST_CASE("read_completion_decision: FIRST trigger — read_size satisfied", "[uart][REQ-UART-033]") {
    REQUIRE(read_completion_decision(4, 4, 0, 250, 8) == UartReadCompletion::RespondNormal);
}

TEST_CASE("read_completion_decision: not yet complete while within the timeout window and fifo short",
          "[uart][REQ-UART-033]") {
    REQUIRE(read_completion_decision(2, 4, 100, 250, 8) == UartReadCompletion::NotYetComplete);
}

TEST_CASE("read_completion_decision: SECOND trigger — uart_timeout expired with a short fifo",
          "[uart][REQ-UART-033]") {
    REQUIRE(read_completion_decision(2, 4, 250, 250, 8) == UartReadCompletion::RespondNormal);
}

TEST_CASE("read_completion_decision: THIRD trigger — read_size exceeds rx_fifo_size and fifo is full",
          "[uart][REQ-UART-033]") {
    REQUIRE(read_completion_decision(8, 20, 0, 250, 8) == UartReadCompletion::RespondFragmented);
}

TEST_CASE("read_completion_decision: not yet complete while the oversized read's fifo has not yet filled",
          "[uart][REQ-UART-033]") {
    REQUIRE(read_completion_decision(3, 20, 50, 250, 8) == UartReadCompletion::NotYetComplete);
}

TEST_CASE("read_completion_decision: uart_timeout_ms == 0 completes immediately", "[uart][REQ-UART-033]") {
    REQUIRE(read_completion_decision(0, 4, 0, 0, 8) == UartReadCompletion::RespondNormal);
}

// ── Fragmented read response (Phase 20, rcp/fragment.hpp) ────────────────────

TEST_CASE("read_response_fragment_count is 1 when the payload already fits", "[uart][REQ-UART-029]") {
    REQUIRE(read_response_fragment_count(10, 100) == 1);
    REQUIRE(read_response_fragment_count(0, 0) == 1);
}

TEST_CASE("encode_read_response_fragmented, when unfragmented, matches encode_read_response exactly",
          "[uart][REQ-UART-030]") {
    const std::vector<uint8_t> rx{0x11, 0x22, 0x33};
    const auto plain = encode_read_response(6, rx, 12, false, 0);
    REQUIRE_FALSE(plain.empty());

    const auto fragmented = encode_read_response_fragmented(6, rx, 12, false, 0, 255);
    REQUIRE(fragmented.size() == 1);
    REQUIRE(fragmented[0] == plain);
}

// Exercises fragment.hpp's ms/segment_num mechanism against this endpoint's
// own wire codec end-to-end, using a deliberately small max_fragment_payload.
TEST_CASE("encode_read_response_fragmented / decode_read_response_fragment round-trip via a "
          "Reassembler with a small cap",
          "[uart][REQ-UART-030][REQ-UART-031]") {
    std::vector<uint8_t> rx(20);
    for (size_t i = 0; i < rx.size(); ++i) rx[i] = static_cast<uint8_t>(100 + i);

    const size_t max_fragment_payload = 6;
    REQUIRE(read_response_fragment_count(rx.size(), max_fragment_payload) == 4); // ceil(20/6)

    const auto frames =
        encode_read_response_fragmented(3, rx, 66, true, 0x0102030405060708ull, max_fragment_payload);
    REQUIRE(frames.size() == 4);

    rcp::fragment::Reassembler reasm(rx.size());
    for (size_t i = 0; i < frames.size(); ++i) {
        bool ms = false;
        uint16_t segnum = 0;
        std::vector<uint8_t> payload;
        bool timed = false;
        uint64_t ts = 0;
        uint8_t txn = 0;

        REQUIRE_FALSE(decode_read_response_fragment(frames[i].data(), frames[i].size(), 3, ms, segnum,
                                                      payload, timed, ts, txn));
        REQUIRE(txn == 66);
        REQUIRE(timed);
        REQUIRE(ts == 0x0102030405060708ull);

        const auto rc = reasm.feed(ms, segnum, payload.empty() ? nullptr : payload.data(), payload.size());
        if (i + 1 < frames.size()) {
            REQUIRE(rc == rcp::fragment::ReasmResult::kContinue);
        } else {
            REQUIRE(rc == rcp::fragment::ReasmResult::kComplete);
        }
    }

    REQUIRE(reasm.size() == rx.size());
    REQUIRE(std::vector<uint8_t>(reasm.data(), reasm.data() + reasm.size()) == rx);
}

TEST_CASE("decode_read_response_fragment rejects short_frame/bad_msg_type/wrong_bus",
          "[uart][REQ-UART-047]") {
    bool ms = false;
    uint16_t segnum = 0;
    std::vector<uint8_t> payload;
    bool timed = false;
    uint64_t ts = 0;
    uint8_t txn = 0;

    const uint8_t too_short[1] = {0};
    REQUIRE(decode_read_response_fragment(too_short, 0, 5, ms, segnum, payload, timed, ts, txn) ==
            make_error_code(UartErrc::short_frame));

    uint8_t bad_type[rcp::acf::kAcfCommonHeaderLen] = {0};
    bad_type[0] = static_cast<uint8_t>(0x01u << 1); // neither ACF_ABB nor ACF_GBB
    REQUIRE(decode_read_response_fragment(bad_type, sizeof(bad_type), 5, ms, segnum, payload, timed, ts,
                                           txn) == make_error_code(UartErrc::bad_msg_type));

    const std::vector<uint8_t> rx{0x01, 0x02};
    const auto frame = encode_read_response(2, rx, 1, false, 0);
    REQUIRE(decode_read_response_fragment(frame.data(), frame.size(), 9, ms, segnum, payload, timed, ts,
                                           txn) == make_error_code(UartErrc::wrong_bus));
}

TEST_CASE("encode_read_response_fragmented is disabled for a zero cap with an oversized payload",
          "[uart][REQ-UART-030]") {
    const std::vector<uint8_t> rx{1, 2, 3, 4};
    const auto frames = encode_read_response_fragmented(3, rx, 1, false, 0, 0);
    REQUIRE(frames.empty());
}

// ── wire_timeout_us (REQ-UART-037, issue #341 lineage) ───────────────────────

// At 3 kbit/s, one bit period is 1000/3 = 333.33...us; 10 bit periods is
// 3333.33...us, which ceilings to 3334 — proves the rounding is genuinely UP.
TEST_CASE("wire_timeout_us computes the ceiling of the bit-period count", "[uart][REQ-UART-037]") {
    REQUIRE(wire_timeout_us(3, 10) == 3334u);
}

TEST_CASE("wire_timeout_us has no off-by-one when the division is exact", "[uart][REQ-UART-037]") {
    REQUIRE(wire_timeout_us(1000, 10) == 10u);
}

TEST_CASE("wire_timeout_us fails open (returns 0) with no configured baud rate", "[uart][REQ-UART-037]") {
    REQUIRE(wire_timeout_us(0, 10) == 0u);
    REQUIRE(wire_timeout_us(0, 0) == 0u);
}

TEST_CASE("wire_timeout_us: zero bit times converts to zero microseconds", "[uart][REQ-UART-037]") {
    REQUIRE(wire_timeout_us(9600, 0) == 0u);
}

TEST_CASE("wire_timeout_us: the maximum representable inputs do not overflow uint32_t",
          "[uart][REQ-UART-037]") {
    REQUIRE(wire_timeout_us(1, 255) == 255000u); // 255 bit periods at 1 kbit/s: exactly 255*1000us
}
