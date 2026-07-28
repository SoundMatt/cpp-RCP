// fusa:test REQ-SPI-001
// fusa:test REQ-SPI-002
// fusa:test REQ-SPI-003
// fusa:test REQ-SPI-004
// fusa:test REQ-SPI-005

// Tests for rcp/spi.hpp — the SPI endpoint type (ROADMAP.md milestone 47,
// "Basic Endpoint Types I — GPIO & SPI", v2.3.0).

#include <catch2/catch_test_macros.hpp>
#include <rcp/spi.hpp>

using namespace rcp::spi;

// ── Channel selection via evt[2:0] ───────────────────────────────────────────

TEST_CASE("kMaxChannels is 6", "[spi][REQ-SPI-001]") {
    REQUIRE(kMaxChannels == 6);
}

TEST_CASE("channel_of accepts evt[2:0] values 0..5", "[spi][REQ-SPI-001]") {
    for (uint8_t v = 0; v < kMaxChannels; ++v) {
        uint8_t out = 0xFF;
        auto ec = channel_of(v, out);
        REQUIRE_FALSE(ec);
        REQUIRE(out == v);
    }
}

TEST_CASE("channel_of rejects evt[2:0] values 6 and 7", "[spi][REQ-SPI-001]") {
    uint8_t out = 0xFF;
    REQUIRE(channel_of(6, out) == make_error_code(SpiErrc::channel_out_of_range));
    REQUIRE(channel_of(7, out) == make_error_code(SpiErrc::channel_out_of_range));
}

TEST_CASE("channel_of masks its input down to 3 bits before range-checking",
          "[spi][REQ-SPI-001]") {
    uint8_t out = 0xFF;
    auto ec = channel_of(0xF9, out); // low 3 bits = 1
    REQUIRE_FALSE(ec);
    REQUIRE(out == 1);
}

// ── Compound-wait status-byte truncation rule ────────────────────────────────

TEST_CASE("compound_wait_matches compares only the first 4 bytes", "[spi][REQ-SPI-002]") {
    std::vector<uint8_t> status{0x01, 0x02, 0x03, 0x04, 0xFF, 0xFF, 0xFF};
    std::vector<uint8_t> expected{0x01, 0x02, 0x03, 0x04};
    REQUIRE(compound_wait_matches(status, expected));
}

TEST_CASE("compound_wait_matches ignores a mismatch beyond byte 4", "[spi][REQ-SPI-002]") {
    std::vector<uint8_t> status(20, 0x00);
    status[0] = 0x01;
    status[1] = 0x02;
    status[2] = 0x03;
    status[3] = 0x04;
    status[19] = 0xAA; // differs from expected, but past the truncation window

    std::vector<uint8_t> expected{0x01, 0x02, 0x03, 0x04, 0x00 /* ... */};
    REQUIRE(compound_wait_matches(status, expected));
}

TEST_CASE("compound_wait_matches reports a mismatch within the first 4 bytes", "[spi][REQ-SPI-002]") {
    std::vector<uint8_t> status{0x01, 0x02, 0x03, 0x04};
    std::vector<uint8_t> expected{0x01, 0x02, 0x03, 0x05};
    REQUIRE_FALSE(compound_wait_matches(status, expected));
}

TEST_CASE("compound_wait_matches rejects a status transfer beyond kMaxStatusBytes",
          "[spi][REQ-SPI-002]") {
    std::vector<uint8_t> status(kMaxStatusBytes + 1, 0x00);
    std::vector<uint8_t> expected(kMaxStatusBytes + 1, 0x00);
    REQUIRE_FALSE(compound_wait_matches(status, expected));
}

TEST_CASE("compound_wait_matches returns false on empty input", "[spi][REQ-SPI-002]") {
    REQUIRE_FALSE(compound_wait_matches({}, {}));
    REQUIRE_FALSE(compound_wait_matches({0x01}, {}));
}

// ── Raw PICO-out/POCI-in transfer ────────────────────────────────────────────

TEST_CASE("SpiEndpoint::transfer records sent and received bytes per channel",
          "[spi][REQ-SPI-003]") {
    SpiEndpoint ep;
    auto ec = ep.transfer(/*channel=*/2, {0xDE, 0xAD}, {0xBE, 0xEF});
    REQUIRE_FALSE(ec);
    REQUIRE(ep.last_sent(2) == std::vector<uint8_t>{0xDE, 0xAD});
    REQUIRE(ep.last_received(2) == std::vector<uint8_t>{0xBE, 0xEF});
}

TEST_CASE("SpiEndpoint::transfer rejects a channel >= kMaxChannels", "[spi][REQ-SPI-003]") {
    SpiEndpoint ep;
    auto ec = ep.transfer(/*channel=*/6, {0x01}, {0x02});
    REQUIRE(ec == make_error_code(SpiErrc::channel_out_of_range));
}

TEST_CASE("SpiEndpoint tracks each channel's last transfer independently", "[spi][REQ-SPI-003]") {
    SpiEndpoint ep;
    REQUIRE_FALSE(ep.transfer(0, {0x01}, {0x11}));
    REQUIRE_FALSE(ep.transfer(1, {0x02}, {0x22}));
    REQUIRE(ep.last_received(0) == std::vector<uint8_t>{0x11});
    REQUIRE(ep.last_received(1) == std::vector<uint8_t>{0x22});
}

// ── Transfer-complete and per-CS assert/de-assert trigger signals ───────────

TEST_CASE("SpiEndpoint::transfer fires CsAssert, TransferComplete, CsDeassert in order when armed",
          "[spi][REQ-SPI-004]") {
    SpiEndpoint ep;
    ep.triggers().enable(spi_signal_id(3, SpiSignal::CsAssert));
    ep.triggers().enable(spi_signal_id(3, SpiSignal::TransferComplete));
    ep.triggers().enable(spi_signal_id(3, SpiSignal::CsDeassert));

    REQUIRE_FALSE(ep.transfer(3, {0x01}, {0x02}));

    auto drained = ep.triggers().drain();
    REQUIRE(drained.size() == 3);
    REQUIRE(drained[0] == spi_signal_id(3, SpiSignal::CsAssert));
    REQUIRE(drained[1] == spi_signal_id(3, SpiSignal::TransferComplete));
    REQUIRE(drained[2] == spi_signal_id(3, SpiSignal::CsDeassert));
}

TEST_CASE("SpiEndpoint's trigger signals are scoped per channel", "[spi][REQ-SPI-004]") {
    SpiEndpoint ep;
    ep.triggers().enable(spi_signal_id(0, SpiSignal::TransferComplete));
    // Channel 1's TransferComplete signal is deliberately left disabled.

    REQUIRE_FALSE(ep.transfer(1, {0xAA}, {0xBB}));
    REQUIRE_FALSE(ep.triggers().has_pending());

    REQUIRE_FALSE(ep.transfer(0, {0xAA}, {0xBB}));
    REQUIRE(ep.triggers().has_pending());
}

TEST_CASE("spi_signal_id gives every (channel, signal) pair a distinct id", "[spi][REQ-SPI-004]") {
    REQUIRE(spi_signal_id(0, SpiSignal::TransferComplete) != spi_signal_id(0, SpiSignal::CsAssert));
    REQUIRE(spi_signal_id(0, SpiSignal::TransferComplete) != spi_signal_id(1, SpiSignal::TransferComplete));
}

// ── SpiErrc category sanity ───────────────────────────────────────────────────

TEST_CASE("SpiErrc reports a non-empty message in its own category", "[spi][REQ-SPI-005]") {
    auto ec = make_error_code(SpiErrc::channel_out_of_range);
    REQUIRE(ec.category() == spi_category());
    REQUIRE_FALSE(ec.message().empty());
}
