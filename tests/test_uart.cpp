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
#include <rcp/endpoint.hpp>
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

TEST_CASE("pack_frame_to_octet rejects bits_per_frame outside [5,8]", "[uart][REQ-UART-005]") {
    uint8_t out = 0;
    REQUIRE(pack_frame_to_octet(0x01, 4, out) == make_error_code(UartErrc::bits_per_frame_out_of_range));
    REQUIRE(pack_frame_to_octet(0x01, 9, out) == make_error_code(UartErrc::bits_per_frame_out_of_range));
}

TEST_CASE("pack_frame_to_octet with bits_per_frame=8 is a full-octet passthrough",
          "[uart][REQ-UART-005]") {
    uint8_t out = 0;
    REQUIRE_FALSE(pack_frame_to_octet(0xAB, 8, out));
    REQUIRE(out == 0xAB);
}

// ── Single-AVTPDU accepted-limitation bound ──────────────────────────────────

TEST_CASE("kMaxReadSize bounds both the RX FIFO and TX queue capacities", "[uart][REQ-UART-006]") {
    REQUIRE(kRxFifoCapacity == kMaxReadSize);
    REQUIRE(kTxQueueCapacity == kMaxReadSize);
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
