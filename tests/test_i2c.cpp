// fusa:test REQ-I2C-001
// fusa:test REQ-I2C-002
// fusa:test REQ-I2C-003
// fusa:test REQ-I2C-004
// fusa:test REQ-I2C-005
// fusa:test REQ-I2C-006

// Tests for rcp/i2c.hpp — the I2C endpoint type (ROADMAP.md milestone 48,
// "Basic Endpoint Types II — I2C, UART, ADC, PWM_OUT, PWM_IN", v2.4.0).

#include <catch2/catch_test_macros.hpp>
#include <rcp/i2c.hpp>

using namespace rcp::i2c;

// ── i2c_mode open item ───────────────────────────────────────────────────────

TEST_CASE("i2c_mode_of decodes only the coarse high-speed-requested bit", "[i2c][REQ-I2C-001]") {
    REQUIRE(i2c_mode_of(false) == I2cMode::Standard);
    REQUIRE(i2c_mode_of(true) == I2cMode::HighSpeed);
}

// ── Compound-wait arbitrary-bit-sequence match ───────────────────────────────

TEST_CASE("compound_wait_matches_bits compares an exact whole-byte-multiple bit length",
          "[i2c][REQ-I2C-002]") {
    std::vector<uint8_t> received{0b10110000, 0b11110000};
    std::vector<uint8_t> expected{0b10110000, 0b11110000};
    REQUIRE(compound_wait_matches_bits(received, expected, 16));
}

TEST_CASE("compound_wait_matches_bits compares only the top bits of a partial final byte",
          "[i2c][REQ-I2C-002]") {
    std::vector<uint8_t> received{0b10110000, 0b11111111}; // low nibble of byte 1 differs
    std::vector<uint8_t> expected{0b10110000, 0b11110000};
    REQUIRE(compound_wait_matches_bits(received, expected, 12)); // 8 + top 4 bits of byte 1
}

TEST_CASE("compound_wait_matches_bits reports a mismatch within the compared window",
          "[i2c][REQ-I2C-002]") {
    std::vector<uint8_t> received{0b10110000};
    std::vector<uint8_t> expected{0b10100000};
    REQUIRE_FALSE(compound_wait_matches_bits(received, expected, 8));
}

TEST_CASE("compound_wait_matches_bits returns false for a zero bit_len or short buffers",
          "[i2c][REQ-I2C-002]") {
    std::vector<uint8_t> received{0x01};
    std::vector<uint8_t> expected{0x01};
    REQUIRE_FALSE(compound_wait_matches_bits(received, expected, 0));
    REQUIRE_FALSE(compound_wait_matches_bits(received, expected, 16)); // not enough bytes
}

// ── Controller-only raw byte-stream transfer, address bytes included ────────

TEST_CASE("I2cEndpoint::transfer records the raw sent/received byte streams",
          "[i2c][REQ-I2C-003]") {
    I2cEndpoint ep;
    // First byte models the address+R/W bit, per this milestone's "raw byte
    // stream including address bytes" scope.
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

// ── I2cErrc category sanity ───────────────────────────────────────────────────

TEST_CASE("I2cErrc reports a non-empty message in its own category", "[i2c][REQ-I2C-005]") {
    auto ec = make_error_code(I2cErrc::nack);
    REQUIRE(ec.category() == i2c_category());
    REQUIRE_FALSE(ec.message().empty());
}

// ── TC18 §13.7.7.4 Table 47: the trigger ids are an implementation choice ────

TEST_CASE("I2C trigger signal ids occupy Table 47's first two slots and leave id 2 free",
          "[i2c][REQ-I2C-006]") {
    // Table 47 lists three I2C trigger-signal ids (0, 1, 2) with the Event
    // column left blank for all three in v0.5.1_RC, so the specification
    // assigns no meaning to any of them. TransferComplete/Nack are therefore
    // this implementation's own assignment; pin the ids so the deviation stays
    // visible if the table is ever filled in.
    REQUIRE(i2c_signal_id(I2cSignal::TransferComplete) == 0);
    REQUIRE(i2c_signal_id(I2cSignal::Nack)             == 1);

    // Id 2 is not assigned by this implementation: an armed listener on it must
    // never be notified by a transfer, acknowledged or not.
    I2cEndpoint ep;
    ep.triggers().enable(2);
    REQUIRE_FALSE(ep.transfer({0xA0}, {0x01}));
    REQUIRE(ep.transfer({0xA0}, {0x01}, /*acked=*/false));
    REQUIRE(ep.triggers().drain().empty());
}
