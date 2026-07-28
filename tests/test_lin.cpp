// fusa:test REQ-LINEP-001
// fusa:test REQ-LINEP-002
// fusa:test REQ-LINEP-003
// fusa:test REQ-LINEP-004

// Tests for rcp/lin.hpp — the LIN commander endpoint type (ROADMAP.md
// milestone 51, "Remaining Endpoint Types — LIN, CAN (incl. CAN XL),
// ISELED, MDIO, Wakeup Control", v2.7.0).

#include <catch2/catch_test_macros.hpp>
#include <rcp/endpoint.hpp>
#include <rcp/lin.hpp>

using namespace rcp::lin;

// ── ep_type id ────────────────────────────────────────────────────────────────

TEST_CASE("LIN's ep_type id is 0x06", "[lin][REQ-LINEP-001]") {
    REQUIRE(rcp::endpoint::kEndpointTypeLin == 0x06);
}

// ── Raw-byte-pusher model: no frame-level concepts ───────────────────────────

TEST_CASE("LinEndpoint::transfer records raw bytes exactly, with no frame interpretation",
          "[lin][REQ-LINEP-002]") {
    LinEndpoint ep;
    // The caller's driver has already assembled break/sync/PID/data/checksum
    // into one opaque byte stream; this endpoint does not decompose it.
    std::vector<uint8_t> out_bytes{0x55, 0x21, 0xAA, 0xBB, 0x74};
    std::vector<uint8_t> in_bytes{0xAA, 0xBB, 0x74};

    auto ec = ep.transfer(out_bytes, in_bytes);
    REQUIRE_FALSE(ec);
    REQUIRE(ep.last_sent() == out_bytes);
    REQUIRE(ep.last_received() == in_bytes);
}

TEST_CASE("LinEndpoint::transfer treats an empty byte stream as valid (no minimum frame length)",
          "[lin][REQ-LINEP-002]") {
    LinEndpoint ep;
    auto ec = ep.transfer({}, {});
    REQUIRE_FALSE(ec);
    REQUIRE(ep.last_sent().empty());
    REQUIRE(ep.last_received().empty());
}

// ── No-response error path ───────────────────────────────────────────────────

TEST_CASE("LinEndpoint::transfer reports no_response and fires NoResponse when responded=false",
          "[lin][REQ-LINEP-003]") {
    LinEndpoint ep;
    ep.triggers().enable(lin_signal_id(LinSignal::TransferComplete));
    ep.triggers().enable(lin_signal_id(LinSignal::NoResponse));

    auto ec = ep.transfer({0x55, 0x21}, {}, /*responded=*/false);
    REQUIRE(ec == make_error_code(LinErrc::no_response));

    auto drained = ep.triggers().drain();
    REQUIRE(drained.size() == 1);
    REQUIRE(drained[0] == lin_signal_id(LinSignal::NoResponse));
}

TEST_CASE("LinEndpoint::transfer fires only TransferComplete on a normal responded transfer",
          "[lin][REQ-LINEP-003]") {
    LinEndpoint ep;
    ep.triggers().enable(lin_signal_id(LinSignal::TransferComplete));
    ep.triggers().enable(lin_signal_id(LinSignal::NoResponse));

    REQUIRE_FALSE(ep.transfer({0x55}, {0xAA}));

    auto drained = ep.triggers().drain();
    REQUIRE(drained.size() == 1);
    REQUIRE(drained[0] == lin_signal_id(LinSignal::TransferComplete));
}

// ── LinErrc category sanity ───────────────────────────────────────────────────

TEST_CASE("LinErrc reports a non-empty message in its own category", "[lin][REQ-LINEP-004]") {
    auto ec = make_error_code(LinErrc::no_response);
    REQUIRE(ec.category() == lin_category());
    REQUIRE_FALSE(ec.message().empty());
}
