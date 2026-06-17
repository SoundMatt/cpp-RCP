// fusa:req REQ-RELAY-001
// fusa:req REQ-RELAY-002
// fusa:req REQ-RELAY-003
// fusa:req REQ-RELAY-004
// fusa:req REQ-RELAY-005

// RELAY conformance tests (RELAY spec §18.2, §5.1, §5.2, §10.3, §14, §19.4).
//
// Verifies that cpp-RCP satisfies the mandatory RELAY-conformance requirements
// as enumerated in Appendix A of the RELAY spec.
#include <catch2/catch_test_macros.hpp>

#include <rcp/adapt.hpp>
#include <rcp/mock.hpp>
#include <relay/relay.hpp>

#include <thread>
#include <chrono>

using namespace std::chrono_literals;

// ── §19.4: SpecVersion constant ───────────────────────────────────────────────

TEST_CASE("relay: kRelaySpecVersion is 0.2", "[relay][conformance]") {
    REQUIRE(relay::kRelaySpecVersion == "0.2");
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
    REQUIRE(rcp::ErrNotFound == relay::ErrNotConnected);
}

TEST_CASE("relay: rcp::ErrZoneMismatch maps to relay::ErrNotConnected", "[relay][conformance]") {
    REQUIRE(rcp::ErrZoneMismatch == relay::ErrNotConnected);
}

TEST_CASE("relay: rcp::ErrAlreadyExists is standalone (no relay sentinel)", "[relay][conformance]") {
    // Per RELAY spec §5.4 update: ErrAlreadyExists is not a relay sentinel.
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

// ── §18.2: StatusChannel aliased from relay::Channel<Status> ─────────────────

TEST_CASE("relay: rcp::StatusChannel is relay::Channel<rcp::Status>", "[relay][conformance]") {
    static_assert(
        std::is_same<rcp::StatusChannel, relay::Channel<rcp::Status>>::value,
        "rcp::StatusChannel must be relay::Channel<rcp::Status> (§18.2)");
    auto ch = std::make_shared<rcp::StatusChannel>(4);
    rcp::Status s;
    s.zone    = rcp::Zone::FrontLeft;
    s.healthy = true;
    REQUIRE(ch->push(s));
    auto got = ch->try_recv();
    REQUIRE(got.has_value());
    REQUIRE(got->zone == rcp::Zone::FrontLeft);
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

// ── §10.3: Adapt() wraps Controller as relay::Caller ─────────────────────────

TEST_CASE("relay: Adapt() returns non-null relay::Caller", "[relay][adapt]") {
    auto ctrl   = std::make_shared<rcp::mock::Controller>(rcp::Zone::FrontLeft);
    auto caller = rcp::Adapt(ctrl);
    REQUIRE(caller != nullptr);
}

TEST_CASE("relay: Adapt() protocol() returns RCP", "[relay][adapt]") {
    auto ctrl   = std::make_shared<rcp::mock::Controller>(rcp::Zone::FrontLeft);
    auto caller = rcp::Adapt(ctrl);
    REQUIRE(caller->protocol() == relay::Protocol::RCP);
}

TEST_CASE("relay: Adapt() call() sends command and returns response", "[relay][adapt]") {
    auto ctrl   = std::make_shared<rcp::mock::Controller>(rcp::Zone::FrontLeft);
    auto caller = rcp::Adapt(ctrl);

    relay::Message req;
    req.protocol            = relay::Protocol::RCP;
    req.id                  = "FrontLeft";
    req.meta["rcp.cmd_type"] = "get";

    auto ctx = relay::Context::with_timeout(1s);
    auto [resp, ec] = caller->call(ctx, req);
    REQUIRE_FALSE(ec);
    REQUIRE(resp.id == "FrontLeft");
    REQUIRE(resp.protocol == relay::Protocol::RCP);
}

TEST_CASE("relay: Adapt() send() succeeds", "[relay][adapt]") {
    auto ctrl   = std::make_shared<rcp::mock::Controller>(rcp::Zone::FrontLeft);
    auto caller = rcp::Adapt(ctrl);

    relay::Message msg;
    msg.id                  = "FrontLeft";
    msg.meta["rcp.cmd_type"] = "set";

    auto ctx = relay::Context::with_timeout(1s);
    REQUIRE_FALSE(caller->send(ctx, msg));
}

TEST_CASE("relay: Adapt() subscribe() returns valid channel", "[relay][adapt]") {
    auto ctrl   = std::make_shared<rcp::mock::Controller>(rcp::Zone::FrontLeft);
    auto caller = rcp::Adapt(ctrl);

    auto [ch, ec] = caller->subscribe();
    REQUIRE_FALSE(ec);
    REQUIRE(ch != nullptr);

    // Publish a status and verify it arrives via the relay channel.
    ctrl->publish({0x01, 0x02});

    relay::Message msg;
    bool got = false;
    auto deadline = std::chrono::steady_clock::now() + 500ms;
    while (std::chrono::steady_clock::now() < deadline) {
        auto m = ch->try_recv();
        if (m) {
            msg = *m;
            got = true;
            break;
        }
        std::this_thread::sleep_for(1ms);
    }

    REQUIRE(got);
    REQUIRE(msg.protocol == relay::Protocol::RCP);
    REQUIRE(msg.id == "FrontLeft");
    REQUIRE(msg.meta.count("rcp.healthy") == 1);
}

TEST_CASE("relay: Adapt() close() idempotent", "[relay][adapt]") {
    auto ctrl   = std::make_shared<rcp::mock::Controller>(rcp::Zone::FrontLeft);
    auto caller = rcp::Adapt(ctrl);
    REQUIRE_FALSE(caller->close());
    REQUIRE_FALSE(caller->close()); // second call must be no-op
}

// ── §15.7.5: zone_to_relay_id / zone_from_relay_id round-trips ───────────────

TEST_CASE("relay: zone round-trips through relay ID", "[relay][conform]") {
    using rcp::Zone;
    for (auto z : {Zone::FrontLeft, Zone::FrontRight,
                   Zone::RearLeft,  Zone::RearRight, Zone::Central}) {
        REQUIRE(rcp::zone_from_relay_id(rcp::zone_to_relay_id(z)) == z);
    }
}
