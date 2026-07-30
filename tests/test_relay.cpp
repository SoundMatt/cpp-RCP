// fusa:test REQ-RELAY-001
// fusa:test REQ-RELAY-002
// fusa:test REQ-RELAY-003
// fusa:test REQ-RELAY-004
// fusa:test REQ-RELAY-005

// RELAY conformance tests (RELAY spec §18.2, §5.1, §5.2, §10.3, §14, §19.4).
//
// Verifies that cpp-RCP satisfies the mandatory RELAY-conformance requirements
// as enumerated in Appendix A of the RELAY spec.
//
// cpp-RCP-05/#74: kRelaySpecVersion bumped 1.11 -> 2.0 (the breaking RCP
// revision, RELAY spec §15.5). cpp-RCP-FS-01/#84: the retired
// Zone/Command/Status model — including rcp::StatusChannel, which aliased
// relay::Channel<Status> — is gone, so the §18.2 StatusChannel case below is
// removed along with it; rcp::Context's §18.2 alias is unaffected and still
// covered. cpp-RCP-FS-05/#88: endpoint_id_to_relay_id()/
// relay_id_to_endpoint_id()/response_to_message()/message_to_request() are
// rebound to the plain decimal ByteBusID id RELAY spec §15.7.5 actually
// specifies (no stream_key folded in) — see rcp/adapt.hpp's own header
// comment.
#include <catch2/catch_test_macros.hpp>

#include <rcp/acf.hpp>
#include <rcp/adapt.hpp>
#include <rcp/mock.hpp>
#include <relay/relay.hpp>

#include <thread>
#include <chrono>

using namespace std::chrono_literals;

// ── §19.4: SpecVersion constant ───────────────────────────────────────────────

TEST_CASE("relay: kRelaySpecVersion is 2.0", "[relay][conformance]") {
    REQUIRE(relay::kRelaySpecVersion == "2.0");
}

// ── §3: Protocol enum ─────────────────────────────────────────────────────────

TEST_CASE("relay: Protocol enum values match spec §3", "[relay][conformance]") {
    REQUIRE(static_cast<int>(relay::Protocol::CAN)    == 1);
    REQUIRE(static_cast<int>(relay::Protocol::DDS)    == 2);
    REQUIRE(static_cast<int>(relay::Protocol::LIN)    == 3);
    REQUIRE(static_cast<int>(relay::Protocol::MQTT)   == 4);
    REQUIRE(static_cast<int>(relay::Protocol::RCP)    == 5);
    REQUIRE(static_cast<int>(relay::Protocol::SOMEIP) == 6);
}

// ── §5.1: Mandatory error sentinels ───────────────────────────────────────────

TEST_CASE("relay: mandatory error sentinels exist", "[relay][conformance]") {
    REQUIRE(relay::ErrClosed.category()          == relay::relay_category());
    REQUIRE(relay::ErrNotConnected.category()    == relay::relay_category());
    REQUIRE(relay::ErrTimeout.category()         == relay::relay_category());
    REQUIRE(relay::ErrPayloadTooLarge.category() == relay::relay_category());
}

TEST_CASE("relay: sentinel error category name is 'relay'", "[relay][conformance]") {
    REQUIRE(std::string(relay::relay_category().name()) == "relay");
}

TEST_CASE("relay: sentinel messages match spec §5.1", "[relay][conformance]") {
    REQUIRE(relay::ErrClosed.message()          == "relay: closed");
    REQUIRE(relay::ErrNotConnected.message()    == "relay: not connected");
    REQUIRE(relay::ErrTimeout.message()         == "relay: timeout");
    REQUIRE(relay::ErrPayloadTooLarge.message() == "relay: payload too large");
}

// ── §5.2: rcp::Errc → relay::Errc std::error_condition equivalence ────────────

TEST_CASE("relay: rcp::ErrClosed maps to relay::ErrClosed", "[relay][conformance]") {
    REQUIRE(rcp::ErrClosed == relay::ErrClosed);
}

TEST_CASE("relay: rcp::ErrTimeout maps to relay::ErrTimeout", "[relay][conformance]") {
    REQUIRE(rcp::ErrTimeout == relay::ErrTimeout);
}

TEST_CASE("relay: rcp::ErrBusy maps to relay::ErrTimeout", "[relay][conformance]") {
    REQUIRE(rcp::ErrBusy == relay::ErrTimeout);
}

TEST_CASE("relay: rcp::ErrNotFound maps to relay::ErrNotConnected", "[relay][conformance]") {
    // RELAY spec §5.4 (v2.0): "RCP | ErrNotFound | ErrNotConnected |
    // Message.ID does not parse to a valid ByteBusID (0-255)".
    REQUIRE(rcp::ErrNotFound == relay::ErrNotConnected);
}

TEST_CASE("relay: rcp::ErrAlreadyExists is standalone (no relay sentinel)", "[relay][conformance]") {
    // Per RELAY spec §5.4: ErrAlreadyExists is not a relay sentinel.
    REQUIRE(rcp::ErrAlreadyExists != relay::ErrClosed);
    REQUIRE(rcp::ErrAlreadyExists != relay::ErrNotConnected);
    REQUIRE(rcp::ErrAlreadyExists != relay::ErrTimeout);
    REQUIRE(rcp::ErrAlreadyExists != relay::ErrPayloadTooLarge);
}

// ── §18.2: Context aliased from relay::Context ───────────────────────────────

TEST_CASE("relay: rcp::Context is relay::Context", "[relay][conformance]") {
    static_assert(std::is_same<rcp::Context, relay::Context>::value,
        "rcp::Context must be an alias for relay::Context (§18.2)");
    auto ctx = rcp::Context::with_timeout(100ms);
    REQUIRE_FALSE(ctx.done());
}

// ── §14: BackPressurePolicy default is drop_newest ───────────────────────────

TEST_CASE("relay: SubscriberOptions default channel depth is 64", "[relay][conformance]") {
    relay::SubscriberOptions opts;
    REQUIRE(opts.channel_depth == 64);
    REQUIRE(opts.back_pressure == relay::BackPressurePolicy::drop_newest);
}

// ── §18.2: Channel<T> push returns false when full ───────────────────────────

TEST_CASE("relay: Channel push returns false when full", "[relay][channel]") {
    relay::Channel<int> ch(2);
    REQUIRE(ch.push(1));
    REQUIRE(ch.push(2));
    REQUIRE_FALSE(ch.push(3)); // full
}

TEST_CASE("relay: Channel push returns false after close", "[relay][channel]") {
    relay::Channel<int> ch(8);
    ch.close();
    REQUIRE_FALSE(ch.push(1));
}

TEST_CASE("relay: Channel recv returns nullopt after close with empty queue", "[relay][channel]") {
    relay::Channel<int> ch(8);
    ch.close();
    REQUIRE_FALSE(ch.recv().has_value());
}

// ── §10.3: Adapt() wraps a RequestFn as relay::Caller ────────────────────────
// mock_request_fn wires a fresh rcp::mock::Server (v2.12.0) as the RequestFn
// every test case below adapts — the same "client-side send-equivalent
// call" shape rcp/record.hpp's and rcp/observe.hpp's own RequestFn already
// standardize on (v2.14.0).

namespace {
std::shared_ptr<rcp::mock::Server> make_configured_mock_server() {
    auto srv = std::make_shared<rcp::mock::Server>();
    srv->advance_to_rcp_configured();
    return srv;
}

rcp::RequestFn mock_request_fn(std::shared_ptr<rcp::mock::Server> srv) {
    return [srv](const rcp::Context&, const rcp::acf::AcfMessageInfo& req,
                 const std::vector<uint8_t>& payload,
                 rcp::acf::AcfMessageInfo& out, std::vector<uint8_t>& out_payload) {
        return srv->dispatch(0, req, payload, out, out_payload);
    };
}
} // namespace

TEST_CASE("relay: Adapt() returns non-null relay::Caller", "[relay][adapt]") {
    auto caller = rcp::Adapt(mock_request_fn(make_configured_mock_server()));
    REQUIRE(caller != nullptr);
}

TEST_CASE("relay: Adapt() protocol() returns RCP", "[relay][adapt]") {
    auto caller = rcp::Adapt(mock_request_fn(make_configured_mock_server()));
    REQUIRE(caller->protocol() == relay::Protocol::RCP);
}

TEST_CASE("relay: Adapt() call() sends a request and returns the response", "[relay][adapt]") {
    auto caller = rcp::Adapt(mock_request_fn(make_configured_mock_server()));

    relay::Message req;
    req.protocol = relay::Protocol::RCP;
    req.id       = rcp::endpoint_id_to_relay_id(rcp::mock::kGpioByteBusId);
    req.meta["rcp.op"] = "read";

    auto ctx = relay::Context::with_timeout(1s);
    auto [resp, ec] = caller->call(ctx, req);
    REQUIRE_FALSE(ec);
    REQUIRE(resp.id == req.id);
    REQUIRE(resp.protocol == relay::Protocol::RCP);
}

TEST_CASE("relay: Adapt() call() reports invalid_argument for an unparseable id",
          "[relay][adapt]") {
    auto caller = rcp::Adapt(mock_request_fn(make_configured_mock_server()));

    relay::Message req;
    req.id = "not-a-valid-id";

    auto ctx = relay::Context::with_timeout(1s);
    auto [resp, ec] = caller->call(ctx, req);
    REQUIRE(ec == std::errc::invalid_argument);
    REQUIRE(resp.id.empty());
}

TEST_CASE("relay: Adapt() send() succeeds", "[relay][adapt]") {
    auto caller = rcp::Adapt(mock_request_fn(make_configured_mock_server()));

    relay::Message msg;
    msg.id             = rcp::endpoint_id_to_relay_id(rcp::mock::kSpiByteBusId);
    msg.meta["rcp.op"] = "write";

    auto ctx = relay::Context::with_timeout(1s);
    REQUIRE_FALSE(caller->send(ctx, msg));
}

// subscribe() has no analog in the target specification's request/response
// shape — see rcp/adapt.hpp's own header comment and rcp/mqttbr.hpp's
// (v2.15.0) equivalent note for the seven ADAPTed application bridges.
TEST_CASE("relay: Adapt() subscribe() reports function_not_supported", "[relay][adapt]") {
    auto caller = rcp::Adapt(mock_request_fn(make_configured_mock_server()));

    auto [ch, ec] = caller->subscribe();
    REQUIRE(ch == nullptr);
    REQUIRE(ec == std::errc::function_not_supported);
}

TEST_CASE("relay: Adapt() close() idempotent", "[relay][adapt]") {
    auto caller = rcp::Adapt(mock_request_fn(make_configured_mock_server()));
    REQUIRE_FALSE(caller->close());
    REQUIRE_FALSE(caller->close()); // second call must be no-op
}

// ── §15.7.5: endpoint_id_to_relay_id / relay_id_to_endpoint_id round-trips ───
// cpp-RCP-FS-05/#88: Message.ID for RCP is just the decimal ByteBusID string
// (0-255) — the StreamID is not part of it (§8.5: one StreamID per Caller
// instance).

TEST_CASE("relay: byte_bus_id round-trips through relay ID as a plain decimal string",
          "[relay][conform]") {
    for (rcp::avtp::ByteBusId bus_id : {rcp::avtp::ByteBusId{0}, rcp::avtp::ByteBusId{9},
                                          rcp::avtp::ByteBusId{255}}) {
        auto id = rcp::endpoint_id_to_relay_id(bus_id);
        rcp::avtp::ByteBusId decoded = 0;
        REQUIRE(rcp::relay_id_to_endpoint_id(id, decoded));
        REQUIRE(decoded == bus_id);
    }
}

TEST_CASE("relay: endpoint_id_to_relay_id(9) is the plain decimal string \"9\" (spec example)",
          "[relay][conform]") {
    // RELAY spec §15.7.5's own worked example: `ID` | `ByteBusID` | Decimal
    // string, e.g. "9".
    REQUIRE(rcp::endpoint_id_to_relay_id(9) == "9");
}

TEST_CASE("relay: relay_id_to_endpoint_id rejects the pre-#88 \"<hex>:<decimal>\" form",
          "[relay][conform]") {
    rcp::avtp::ByteBusId byte_bus_id = 0;
    REQUIRE_FALSE(rcp::relay_id_to_endpoint_id("0000000000000000:1", byte_bus_id));
}

TEST_CASE("relay: relay_id_to_endpoint_id rejects the pre-v2.16.0 zone-name form",
          "[relay][conform]") {
    rcp::avtp::ByteBusId byte_bus_id = 0;
    REQUIRE_FALSE(rcp::relay_id_to_endpoint_id("FrontLeft", byte_bus_id));
}

TEST_CASE("relay: relay_id_to_endpoint_id rejects a byte_bus_id above 255",
          "[relay][conform]") {
    rcp::avtp::ByteBusId byte_bus_id = 0;
    REQUIRE_FALSE(rcp::relay_id_to_endpoint_id("256", byte_bus_id));
}

// ── §15.7.5: response_to_message / message_to_request mapping ────────────────
//
// This implementation's own mapping from an ACF response (rcp/acf.hpp) to a
// relay::Message — not an externally pinned golden vector, since the
// pre-v2.16.0 Status/subscribe push model this milestone's own scope note
// retires has no analog to pin one against. Full bit-for-bit conformance of
// this encoding against any other implementation is not claimed, same as
// the equivalent disclaimers in rcp/avtp.hpp, rcp/acf.hpp, and rcp/udp.hpp.

TEST_CASE("relay: response_to_message carries the endpoint id, payload, and response_kind",
          "[relay][conformance]") {
    rcp::acf::AcfMessageInfo resp;
    resp.byte_bus_id = 7;
    resp.rsp         = true;
    resp.op          = false; // ReadResponse

    auto msg = rcp::response_to_message(resp, {0x01, 0x02});

    REQUIRE(static_cast<int>(msg.protocol) == 5); // relay.RCP
    REQUIRE(msg.id == "7");
    REQUIRE(msg.payload == std::vector<uint8_t>{0x01, 0x02});
    REQUIRE(msg.meta.at("rcp.err") == "false");
    REQUIRE(msg.meta.at("rcp.response_kind") ==
            std::to_string(static_cast<int>(rcp::acf::ResponseKind::ReadResponse)));
}

TEST_CASE("relay: message_to_request decodes op/evt_op from meta", "[relay][conformance]") {
    relay::Message msg;
    msg.id                   = rcp::endpoint_id_to_relay_id(3);
    msg.meta["rcp.op"]        = "write";
    msg.meta["rcp.evt_op"]    = "5";
    msg.payload                = {0xAA};

    rcp::acf::AcfMessageInfo req;
    std::vector<uint8_t> payload;
    REQUIRE(rcp::message_to_request(msg, req, payload));
    REQUIRE(req.byte_bus_id == 3);
    REQUIRE(req.op == true);
    REQUIRE(req.evt_op == 5);
    REQUIRE(payload == std::vector<uint8_t>{0xAA});
}

// ── §14.1 (v0.3): SubscriberOptions carries topic_name ───────────────────────

TEST_CASE("relay: SubscriberOptions has topic_name field, default empty",
          "[relay][conformance]") {
    relay::SubscriberOptions opts;
    REQUIRE(opts.topic_name.empty());

    // subscribe() reports function_not_supported regardless of topic_name —
    // see this file's own §10.3 section above.
    auto caller = rcp::Adapt(mock_request_fn(make_configured_mock_server()));
    opts.topic_name = "ignored-by-rcp";
    auto [ch, ec] = caller->subscribe(opts);
    REQUIRE(ch == nullptr);
    REQUIRE(ec == std::errc::function_not_supported);
}
