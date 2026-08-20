// fusa:test REQ-LINEP-001
// fusa:test REQ-LINEP-002
// fusa:test REQ-LINEP-003
// fusa:test REQ-LINEP-004
// fusa:test REQ-LINEP-005
// fusa:test REQ-LINEP-006

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

// ── Table 33 Row 2 evt[2:0] validation (handle_request) ─────────────────────

TEST_CASE("LinEndpoint::handle_request delegates a Plain (evt[2:0]==000b) request to transfer()",
          "[lin][REQ-LINEP-005]") {
    LinEndpoint ep;
    ep.triggers().enable(lin_signal_id(LinSignal::TransferComplete));

    auto ec = ep.handle_request(/*evt_op=*/0, {0x55, 0x21}, {0xAA, 0xBB});
    REQUIRE_FALSE(ec);
    REQUIRE(ep.last_sent() == std::vector<uint8_t>{0x55, 0x21});
    REQUIRE(ep.last_received() == std::vector<uint8_t>{0xAA, 0xBB});
    REQUIRE(ep.triggers().drain() == std::vector<rcp::endpoint::TriggerRegistry::SignalId>{
                                          lin_signal_id(LinSignal::TransferComplete)});
}

TEST_CASE("LinEndpoint::handle_request rejects every reserved evt[2:0] value (001b-110b)",
          "[lin][REQ-LINEP-005]") {
    for (uint8_t evt_op = 1; evt_op <= 6; ++evt_op) {
        LinEndpoint ep;
        auto ec = ep.handle_request(evt_op, {0x55}, {0xAA});
        REQUIRE(ec == rcp::endpoint::make_error_code(rcp::endpoint::EndpointErrc::reserved_evt_row2));
        // A rejected reserved evt must not record anything as sent/received.
        REQUIRE(ep.last_sent().empty());
        REQUIRE(ep.last_received().empty());
    }
}

TEST_CASE("LinEndpoint::handle_request reports config_write_not_supported for evt[2:0]==111b "
          "without crashing or touching transfer state",
          "[lin][REQ-LINEP-006]") {
    LinEndpoint ep;
    auto ec = ep.handle_request(/*evt_op=*/7, {0x00, 0xAB}, {});
    REQUIRE(ec == make_error_code(LinErrc::config_write_not_supported));
    REQUIRE(ep.last_sent().empty());
    REQUIRE(ep.last_received().empty());
}

TEST_CASE("LinEndpoint::handle_request masks evt_op down to 3 bits before classifying",
          "[lin][REQ-LINEP-005]") {
    LinEndpoint ep;
    REQUIRE_FALSE(ep.handle_request(/*evt_op=*/0xF8, {0x55}, {0xAA})); // low 3 bits 000 -> Plain
    auto ec = ep.handle_request(/*evt_op=*/0xF9, {0x55}, {0xAA});      // low 3 bits 001 -> Reserved
    REQUIRE(ec == rcp::endpoint::make_error_code(rcp::endpoint::EndpointErrc::reserved_evt_row2));
}

// ── LinErrc category sanity ───────────────────────────────────────────────────

TEST_CASE("LinErrc reports a non-empty message in its own category", "[lin][REQ-LINEP-004]") {
    auto ec = make_error_code(LinErrc::no_response);
    REQUIRE(ec.category() == lin_category());
    REQUIRE_FALSE(ec.message().empty());
}

TEST_CASE("LinErrc::config_write_not_supported reports a non-empty message in its own category",
          "[lin][REQ-LINEP-006]") {
    auto ec = make_error_code(LinErrc::config_write_not_supported);
    REQUIRE(ec.category() == lin_category());
    REQUIRE_FALSE(ec.message().empty());
}
