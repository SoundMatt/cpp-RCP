// fusa:test REQ-RELAY-001
// fusa:test REQ-RELAY-002
// fusa:test REQ-RELAY-003
// fusa:test REQ-RELAY-004
// fusa:test REQ-RELAY-005
// fusa:test REQ-RELAY-006
// fusa:test REQ-RELAY-008
// fusa:test REQ-RELAY-009
// fusa:test REQ-RELAY-010
// fusa:test REQ-RELAY-012

// rcp/adapt.hpp conformance and behavioral-equivalence tests (RELAY spec
// §10.3, §15.7.5) — cpp-RCP issue #129, ROADMAP.md Phase 17 ("Phase 4")
// rewrite.
//
// This file's own scope, and how it relates to test_relay.cpp: test_relay.cpp
// (pre-existing) already covers RELAY §18.2/§5.1/§5.2/§19.4 core-conformance
// checks (Protocol enum, error sentinels, Context alias, Channel<T>) plus a
// handful of top-level Adapt()/message_to_request()/response_to_message()
// smoke tests. This file goes deeper on rcp/adapt.hpp specifically — the
// meta<->AcfMessageInfo field mapping this header owns — and is the direct
// product of a line-by-line behavioral comparison against c-RCP's
// include/rcp/adapt.h + src/adapt.c (this project's content source of truth
// for this module, tests/test_adapt.c its test-coverage source of truth).
//
// That comparison found this header's generic RequestFn-passthrough design
// (Adapt()/RcpCallerAdapter, op/evt_op meta + raw payload) IS behaviorally
// equivalent to c-RCP's much richer per-endpoint-type rcp_adapt_op_t
// field-table design for every op whose translation this header is actually
// responsible for -- evt[2:0] is one shared ACF wire field regardless of
// which endpoint type reuses it (GPIO/PWM_OUT write-semantics, SPI channel
// select), and MDIO/CAN/wakeup/discovery's own endpoint-specific fields are
// packed into the opaque `payload` region by each ep-type module's own codec
// (rcp/mdio.hpp etc.), not by this bridging layer, in cpp-RCP's split
// architecture -- WITH ONE EXCEPTION: read_size_or_segment_num, the ACF
// header's own 12-bit read-length field, was never threaded through
// message_to_request()/response_to_message() at all before this pass. That
// is a genuine, load-bearing gap (c-RCP's own per-op table threads the
// identical wire field through 5 of its rows: rcp.uart.read_size/rcp.spi.
// read_size/rcp.adc.read_size/rcp.i2c.read_size/rcp.iseled.read_size) --
// fixed in rcp/adapt.hpp via a single generic "rcp.read_size" meta key
// (read_size_from_meta()). Several TEST_CASEs below are direct regression
// coverage for that fix, translated from c-RCP's test_adapt.c's own
// test_uart_read_request_*/test_i2c_transfer_*_read_size* cases into
// equivalence-of-outcome checks against this header's simpler API (a single
// generic meta key standing in for c-RCP's five per-endpoint-type ones),
// not a line-by-line port of tests that assume c-RCP's own rcp_adapt_op_t
// internal structure.
//
// Test cases c-RCP's test_adapt.c has that this file deliberately does NOT
// port, because they exercise concepts with no cpp-RCP counterpart in this
// header's own simpler design (see rcp/adapt.hpp's own "Phase 4 rewrite"
// header-comment section for the full architecture comparison):
//   - rcp_adapt_op_t / rcp_adapt_op_kind() / rcp_adapt_op_string() round
//     trips: this header has no per-op opcode enum at all -- byte_bus_id
//     alone selects the destination, exactly as every other RequestFn-based
//     bridge in this codebase (record.hpp, observe.hpp) already does.
//   - MDIO addr/word_count, CAN frame_format/arbitration_id, and discovery's
//     own magic/svr_version/vendor_id/device_id/svr_ep_count field mapping:
//     none of these are AcfMessageInfo fields in cpp-RCP's split
//     architecture -- they are packed into the opaque `payload` region by
//     rcp/mdio.hpp's own codec (or, for CAN/discovery, are still-open scope
//     gaps in rcp/can.hpp/rcp/mock.hpp's own dispatch wiring, not this
//     header's problem to solve -- see this file's own "payload passthrough
//     is opaque" section below for what IS tested instead).
//   - wire-level encode/decode failure propagation (rcp_message_to_request's
//     RCP_ADAPT_ERR_ENCODE / rcp_response_to_message's RCP_ADAPT_ERR_DECODE
//     for a malformed frame): this header never touches wire bytes itself
//     -- it maps meta<->AcfMessageInfo fields only, so there is no encode/
//     decode step here that can fail this way. A RequestFn's own failure
//     (whatever a real wire codec inside it reports) is covered generically
//     by this file's "call()/send() propagate the wrapped RequestFn's own
//     error" cases below.
//   - rcp_relay_caller_t manual retain/refcount and rcp_adapt()'s own
//     transport-binding/transaction-numbering (avtp transport, sequence
//     numbers): rcp::RcpCallerAdapter owns no transport and assigns no
//     transaction numbers itself -- see rcp/adapt.hpp's own header comment
//     on why "there is no unified client-side send() chokepoint left to
//     wrap" (v2.14.0). std::unique_ptr already gives Adapt()'s return value
//     RAII lifetime with no manual retain/release protocol to test.
#include <catch2/catch_test_macros.hpp>

#include <rcp/acf.hpp>
#include <rcp/adapt.hpp>
#include <rcp/gpio.hpp>
#include <rcp/mock.hpp>
#include <rcp/spi.hpp>
#include <relay/relay.hpp>

#include <chrono>
#include <memory>

using namespace std::chrono_literals;

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

// ── op_from_meta ──────────────────────────────────────────────────────────

TEST_CASE("adapt: op_from_meta defaults to read (false) when rcp.op is absent",
          "[adapt][meta]") {
    std::map<std::string, std::string> meta;
    REQUIRE_FALSE(rcp::op_from_meta(meta));
}

TEST_CASE("adapt: op_from_meta reports write only for the exact string \"write\"",
          "[adapt][meta]") {
    std::map<std::string, std::string> meta;
    meta["rcp.op"] = "write";
    REQUIRE(rcp::op_from_meta(meta));

    meta["rcp.op"] = "read";
    REQUIRE_FALSE(rcp::op_from_meta(meta));

    meta["rcp.op"] = "Write"; // case-sensitive, not "write"
    REQUIRE_FALSE(rcp::op_from_meta(meta));

    meta["rcp.op"] = "";
    REQUIRE_FALSE(rcp::op_from_meta(meta));
}

// ── evt_op_from_meta ──────────────────────────────────────────────────────

TEST_CASE("adapt: evt_op_from_meta defaults to 0 when rcp.evt_op is absent",
          "[adapt][meta]") {
    std::map<std::string, std::string> meta;
    REQUIRE(rcp::evt_op_from_meta(meta) == 0);
}

TEST_CASE("adapt: evt_op_from_meta parses every value in its 0-7 range", "[adapt][meta]") {
    std::map<std::string, std::string> meta;
    for (unsigned v = 0; v <= 7; ++v) {
        meta["rcp.evt_op"] = std::to_string(v);
        REQUIRE(rcp::evt_op_from_meta(meta) == static_cast<uint8_t>(v));
    }
}

TEST_CASE("adapt: evt_op_from_meta defaults to 0 for a value above 7", "[adapt][meta]") {
    std::map<std::string, std::string> meta;
    meta["rcp.evt_op"] = "8";
    REQUIRE(rcp::evt_op_from_meta(meta) == 0);
}

TEST_CASE("adapt: evt_op_from_meta defaults to 0 for trailing garbage or an empty value",
          "[adapt][meta]") {
    std::map<std::string, std::string> meta;
    meta["rcp.evt_op"] = "3x";
    REQUIRE(rcp::evt_op_from_meta(meta) == 0);

    meta["rcp.evt_op"] = "";
    REQUIRE(rcp::evt_op_from_meta(meta) == 0);

    meta["rcp.evt_op"] = "-1";
    REQUIRE(rcp::evt_op_from_meta(meta) == 0);
}

// ── read_size_from_meta (NEW — this pass's own fix) ──────────────────────
// Direct regression coverage for the gap this pass found and fixed: before
// this fix, message_to_request() never populated
// AcfMessageInfo::read_size_or_segment_num from any meta key at all, so a
// relay::Message could never carry a non-zero UART/SPI/ADC/I2C/ISELED read
// length through Adapt() -- see rcp/adapt.hpp's own "Phase 4 rewrite"
// header-comment section and read_size_from_meta()'s own doc comment for
// the full citation trail against c-RCP's src/adapt.c.

TEST_CASE("adapt: read_size_from_meta defaults to 0 when rcp.read_size is absent",
          "[adapt][meta][read_size]") {
    std::map<std::string, std::string> meta;
    REQUIRE(rcp::read_size_from_meta(meta) == 0);
}

TEST_CASE("adapt: read_size_from_meta parses a valid decimal value", "[adapt][meta][read_size]") {
    std::map<std::string, std::string> meta;
    meta["rcp.read_size"] = "128";
    REQUIRE(rcp::read_size_from_meta(meta) == 128);
}

TEST_CASE("adapt: read_size_from_meta accepts the ACF header's own 12-bit boundary (4095)",
          "[adapt][meta][read_size]") {
    std::map<std::string, std::string> meta;
    meta["rcp.read_size"] = "4095";
    REQUIRE(rcp::read_size_from_meta(meta) == 4095);
}

TEST_CASE("adapt: read_size_from_meta defaults to 0 for a value above the 12-bit bound (4096)",
          "[adapt][meta][read_size]") {
    std::map<std::string, std::string> meta;
    meta["rcp.read_size"] = "4096";
    REQUIRE(rcp::read_size_from_meta(meta) == 0);
}

TEST_CASE("adapt: read_size_from_meta defaults to 0 for trailing garbage or an empty value",
          "[adapt][meta][read_size]") {
    std::map<std::string, std::string> meta;
    meta["rcp.read_size"] = "10x";
    REQUIRE(rcp::read_size_from_meta(meta) == 0);

    meta["rcp.read_size"] = "";
    REQUIRE(rcp::read_size_from_meta(meta) == 0);

    meta["rcp.read_size"] = "-5";
    REQUIRE(rcp::read_size_from_meta(meta) == 0);
}

// ── endpoint_id_to_relay_id / relay_id_to_endpoint_id round-trips ─────────
// (test_relay.cpp already covers the spec-example/rejection cases; this
// file adds the boundary values relevant to this header's own encode path.)

TEST_CASE("adapt: byte_bus_id 0 and 255 round-trip through relay id", "[adapt][id]") {
    for (rcp::avtp::ByteBusId bus_id : {rcp::avtp::ByteBusId{0}, rcp::avtp::ByteBusId{255}}) {
        auto id = rcp::endpoint_id_to_relay_id(bus_id);
        rcp::avtp::ByteBusId decoded = 0;
        REQUIRE(rcp::relay_id_to_endpoint_id(id, decoded));
        REQUIRE(decoded == bus_id);
    }
}

// ── message_to_request: full field mapping ────────────────────────────────

TEST_CASE("adapt: message_to_request rejects a message whose id doesn't decode",
          "[adapt][message_to_request]") {
    relay::Message msg;
    msg.id = "not-a-number";
    rcp::acf::AcfMessageInfo req;
    std::vector<uint8_t> payload;
    REQUIRE_FALSE(rcp::message_to_request(msg, req, payload));
}

TEST_CASE("adapt: message_to_request maps byte_bus_id/op/evt_op/read_size/payload together",
          "[adapt][message_to_request][read_size]") {
    relay::Message msg;
    msg.id                    = rcp::endpoint_id_to_relay_id(42);
    msg.meta["rcp.op"]        = "write";
    msg.meta["rcp.evt_op"]    = "6";
    msg.meta["rcp.read_size"] = "300";
    msg.payload                = {0xDE, 0xAD, 0xBE, 0xEF};

    rcp::acf::AcfMessageInfo req;
    std::vector<uint8_t> payload;
    REQUIRE(rcp::message_to_request(msg, req, payload));
    REQUIRE(req.byte_bus_id == 42);
    REQUIRE(req.op == true);
    REQUIRE(req.evt_op == 6);
    REQUIRE(req.read_size_or_segment_num == 300); // the fix under test
    REQUIRE(payload == std::vector<uint8_t>{0xDE, 0xAD, 0xBE, 0xEF});
}

TEST_CASE("adapt: message_to_request leaves read_size_or_segment_num at 0 when absent "
          "(matches AcfMessageInfo's own default, not a silent misparse)",
          "[adapt][message_to_request][read_size]") {
    relay::Message msg;
    msg.id = rcp::endpoint_id_to_relay_id(1);

    rcp::acf::AcfMessageInfo req;
    std::vector<uint8_t> payload;
    REQUIRE(rcp::message_to_request(msg, req, payload));
    REQUIRE(req.read_size_or_segment_num == 0);
}

// ── response_to_message: full field mapping ───────────────────────────────

TEST_CASE("adapt: response_to_message echoes read_size_or_segment_num as rcp.read_size",
          "[adapt][response_to_message][read_size]") {
    rcp::acf::AcfMessageInfo resp;
    resp.byte_bus_id              = 9;
    resp.rsp                      = true;
    resp.read_size_or_segment_num = 17;

    auto msg = rcp::response_to_message(resp, {});
    REQUIRE(msg.meta.at("rcp.read_size") == "17");
}

TEST_CASE("adapt: response_to_message reports rcp.err for an error response",
          "[adapt][response_to_message]") {
    rcp::acf::AcfMessageInfo resp;
    resp.byte_bus_id = 2;
    resp.rsp         = true;
    resp.err         = true;

    auto msg = rcp::response_to_message(resp, {});
    REQUIRE(msg.meta.at("rcp.err") == "true");
}

// ── UART_READ end-to-end via mock::Server: the fix's own regression test ──
// Before this pass, this exact scenario was IMPOSSIBLE through Adapt():
// message_to_request() always produced read_size_or_segment_num == 0, and
// rcp/mock.hpp's own dispatch_uart() reads that field directly
// (`uart_.handle_request(req.evt_op, req.op, payload,
// req.read_size_or_segment_num, ...)`, rcp/mock.hpp) -- so a UART_READ
// relay::Message could only ever drain 0 bytes, no matter how much data
// was actually buffered. This is this file's clearest possible proof the
// fix is real, not cosmetic.

TEST_CASE("adapt: UART_READ via Adapt() drains exactly rcp.read_size bytes end-to-end",
          "[adapt][regression][read_size]") {
    auto srv = make_configured_mock_server();
    REQUIRE_FALSE(srv->uart().rx_fill({0x11, 0x22, 0x33, 0x44, 0x55}));

    auto caller = rcp::Adapt(mock_request_fn(srv));

    relay::Message req;
    req.id                    = rcp::endpoint_id_to_relay_id(rcp::mock::kUartByteBusId);
    req.meta["rcp.op"]        = "read";
    req.meta["rcp.read_size"] = "3";

    auto ctx = relay::Context::with_timeout(1s);
    auto [resp, ec] = caller->call(ctx, req);
    REQUIRE_FALSE(ec);
    REQUIRE(resp.payload == std::vector<uint8_t>{0x11, 0x22, 0x33});
    REQUIRE(srv->uart().rx_available() == 2); // 2 bytes left undrained
}

TEST_CASE("adapt: UART_READ via Adapt() with no rcp.read_size meta drains nothing "
          "(the pre-fix, read_size-always-0 behavior, still reachable on purpose)",
          "[adapt][regression][read_size]") {
    auto srv = make_configured_mock_server();
    REQUIRE_FALSE(srv->uart().rx_fill({0xAA, 0xBB}));

    auto caller = rcp::Adapt(mock_request_fn(srv));

    relay::Message req;
    req.id             = rcp::endpoint_id_to_relay_id(rcp::mock::kUartByteBusId);
    req.meta["rcp.op"] = "read";
    // no rcp.read_size meta key at all

    auto ctx = relay::Context::with_timeout(1s);
    auto [resp, ec] = caller->call(ctx, req);
    REQUIRE_FALSE(ec);
    REQUIRE(resp.payload.empty());
    REQUIRE(srv->uart().rx_available() == 2); // untouched
}

// ── evt_op end-to-end: GPIO write-semantics and SPI channel select ────────
// These translate c-RCP's test_gpio_write_request_maps_payload_and_evt_meta
// and test_spi_transfer_request_maps_channel_meta_and_payload
// (tests/test_adapt.c) into equivalence-of-outcome checks: cpp-RCP's single
// generic "rcp.evt_op" meta key stands in for c-RCP's per-endpoint-type
// "rcp.gpio.evt"/"rcp.spi.channel" keys, since evt[2:0] is the exact same
// ACF wire field in both cases (rcp/spi.hpp: "selected via the ACF
// byte_message_info header's evt[2:0] field directly as a channel number").

TEST_CASE("adapt: GPIO write via Adapt() applies evt_op as WriteSemantics::Or",
          "[adapt][regression][evt_op]") {
    auto srv = make_configured_mock_server();
    auto caller = rcp::Adapt(mock_request_fn(srv));
    auto ctx = relay::Context::with_timeout(1s);

    // GpioState's own input-pin write mask (rcp/gpio.hpp's apply_gpio_write:
    // "(out & state.directions) | (state.values & ~state.directions)") means
    // a plain value write only takes effect on pins already configured as
    // outputs — evt_op==7 (WriteSemantics::Reconfigure) sets `directions`
    // itself. Two Adapt()-routed writes, both selected purely by evt_op,
    // exercise the mapping end-to-end.
    relay::Message configure;
    configure.id                 = rcp::endpoint_id_to_relay_id(rcp::mock::kGpioByteBusId);
    configure.meta["rcp.op"]     = "write";
    configure.meta["rcp.evt_op"] = "7"; // WriteSemantics::Reconfigure -> sets directions
    configure.payload             = rcp::gpio::encode_gpio_payload(0x0F);
    auto [configure_resp, configure_ec] = caller->call(ctx, configure);
    REQUIRE_FALSE(configure_ec);

    relay::Message req;
    req.id                  = rcp::endpoint_id_to_relay_id(rcp::mock::kGpioByteBusId);
    req.meta["rcp.op"]      = "write";
    req.meta["rcp.evt_op"]  = "1"; // WriteSemantics::Or
    req.payload              = rcp::gpio::encode_gpio_payload(0x0F);
    auto [resp, ec] = caller->call(ctx, req);
    REQUIRE_FALSE(ec);

    rcp::gpio::PinMask value = 0;
    REQUIRE_FALSE(rcp::gpio::decode_gpio_payload(resp.payload.data(), resp.payload.size(), value));
    REQUIRE(value == 0x0F); // 0 | 0x0F, now that all 4 pins are outputs
}

TEST_CASE("adapt: SPI transfer via Adapt() selects the channel via evt_op",
          "[adapt][regression][evt_op]") {
    auto srv = make_configured_mock_server();
    srv->set_spi_poci(3, {0x77, 0x88});

    auto caller = rcp::Adapt(mock_request_fn(srv));

    relay::Message req;
    req.id                 = rcp::endpoint_id_to_relay_id(rcp::mock::kSpiByteBusId);
    req.meta["rcp.op"]     = "read";
    req.meta["rcp.evt_op"] = "3"; // channel 3
    req.payload             = {0x00, 0x00};

    auto ctx = relay::Context::with_timeout(1s);
    auto [resp, ec] = caller->call(ctx, req);
    REQUIRE_FALSE(ec);
    REQUIRE(resp.payload == std::vector<uint8_t>{0x77, 0x88});
}

// ── payload passthrough is opaque ──────────────────────────────────────────
// This header's own `payload` is raw endpoint-specific bytes, untouched and
// unreinterpreted for any op (rcp/adapt.hpp's own header comment) -- unlike
// c-RCP's adapt.c, which decomposes some endpoint types' addressing fields
// (MDIO clause/prtad/devad/regad, CAN frame_format/arbitration_id) into
// separate meta keys because it does full wire encoding itself. In
// cpp-RCP's split architecture those fields, where a wire codec for them
// exists at all (rcp/mdio.hpp), are packed INTO the payload by that
// codec's own caller, not by this header -- so the equivalent behavioral
// guarantee this header owns is simply "payload survives the round trip
// byte-for-byte, whatever it contains."

TEST_CASE("adapt: arbitrary opaque payload bytes survive message_to_request unchanged",
          "[adapt][message_to_request][payload]") {
    relay::Message msg;
    msg.id      = rcp::endpoint_id_to_relay_id(10);
    msg.payload = {0x00, 0x01, 0x02, 0xFF, 0xFE, 0x7F, 0x80};

    rcp::acf::AcfMessageInfo req;
    std::vector<uint8_t> payload;
    REQUIRE(rcp::message_to_request(msg, req, payload));
    REQUIRE(payload == msg.payload);
}

TEST_CASE("adapt: arbitrary opaque payload bytes survive response_to_message unchanged",
          "[adapt][response_to_message][payload]") {
    rcp::acf::AcfMessageInfo resp;
    resp.byte_bus_id = 11;
    std::vector<uint8_t> payload = {0xC0, 0xFF, 0xEE};

    auto msg = rcp::response_to_message(resp, payload);
    REQUIRE(msg.payload == payload);
}

// ── RcpCallerAdapter: send()/call() propagate the wrapped RequestFn's own
// error, and never invoke a null fn ─────────────────────────────────────

TEST_CASE("adapt: call() propagates the wrapped RequestFn's own error code",
          "[adapt][error_propagation]") {
    rcp::RequestFn fn = [](const rcp::Context&, const rcp::acf::AcfMessageInfo&,
                            const std::vector<uint8_t>&, rcp::acf::AcfMessageInfo&,
                            std::vector<uint8_t>&) {
        return std::make_error_code(std::errc::timed_out);
    };
    auto caller = rcp::Adapt(std::move(fn));

    relay::Message req;
    req.id = rcp::endpoint_id_to_relay_id(1);

    auto ctx = relay::Context::with_timeout(1s);
    auto [resp, ec] = caller->call(ctx, req);
    REQUIRE(ec == std::errc::timed_out);
}

TEST_CASE("adapt: send() propagates the wrapped RequestFn's own error code",
          "[adapt][error_propagation]") {
    rcp::RequestFn fn = [](const rcp::Context&, const rcp::acf::AcfMessageInfo&,
                            const std::vector<uint8_t>&, rcp::acf::AcfMessageInfo&,
                            std::vector<uint8_t>&) {
        return std::make_error_code(std::errc::connection_reset);
    };
    auto caller = rcp::Adapt(std::move(fn));

    relay::Message msg;
    msg.id = rcp::endpoint_id_to_relay_id(1);

    auto ctx = relay::Context::with_timeout(1s);
    REQUIRE(caller->send(ctx, msg) == std::errc::connection_reset);
}

TEST_CASE("adapt: call()/send() report not_connected for a default-constructed (empty) RequestFn",
          "[adapt][error_propagation]") {
    auto caller = rcp::Adapt(rcp::RequestFn{});

    relay::Message req;
    req.id = rcp::endpoint_id_to_relay_id(1);

    auto ctx = relay::Context::with_timeout(1s);
    auto [resp, call_ec] = caller->call(ctx, req);
    REQUIRE(call_ec == std::errc::not_connected);
    REQUIRE(caller->send(ctx, req) == std::errc::not_connected);
}

// ── Adapt()/RcpCallerAdapter basic contract (protocol/subscribe/close) ────
// (test_relay.cpp already covers these against a real mock::Server-backed
// RequestFn; kept here too, minimally, so this file stands alone as
// complete coverage of rcp/adapt.hpp.)

TEST_CASE("adapt: Adapt() returns a non-null relay::Caller whose protocol() is RCP",
          "[adapt][contract]") {
    auto caller = rcp::Adapt(mock_request_fn(make_configured_mock_server()));
    REQUIRE(caller != nullptr);
    REQUIRE(caller->protocol() == relay::Protocol::RCP);
}

TEST_CASE("adapt: subscribe() always reports function_not_supported", "[adapt][contract]") {
    auto caller = rcp::Adapt(mock_request_fn(make_configured_mock_server()));
    auto [ch, ec] = caller->subscribe();
    REQUIRE(ch == nullptr);
    REQUIRE(ec == std::errc::function_not_supported);
}

TEST_CASE("adapt: close() always succeeds and is idempotent", "[adapt][contract]") {
    auto caller = rcp::Adapt(mock_request_fn(make_configured_mock_server()));
    REQUIRE_FALSE(caller->close());
    REQUIRE_FALSE(caller->close());
}
