// fusa:test REQ-FI-001
// fusa:test REQ-FI-002
// fusa:test REQ-FI-003
// fusa:test REQ-FI-004
// fusa:test REQ-FI-005
// fusa:test REQ-FI-006
// fusa:test REQ-FI-007
// fusa:test REQ-FI-008
//
// Rebound (cpp-RCP-FS-03/#86): faultinject::Interceptor now wraps an
// rcp::RequestFn (rcp/adapt.hpp) instead of decorating the retired
// rcp::Controller — see rcp/faultinject.hpp's own header comment. The old
// REQ-FI-008 case asserted zone() passthrough, which has no analog on a
// plain RequestFn; it is replaced below with a case asserting that an
// Interceptor is itself usable wherever an rcp::RequestFn is expected.
#include <catch2/catch_test_macros.hpp>

#include "rcp/faultinject.hpp"
#include "rcp/mock.hpp"

#include <atomic>
#include <chrono>
#include <functional>
#include <thread>
#include <vector>

using namespace rcp;
using namespace std::chrono_literals;

namespace {

RequestFn make_mock_fn(std::shared_ptr<mock::Server> srv) {
    return [srv](const Context&, const acf::AcfMessageInfo& req,
                 const std::vector<uint8_t>& payload,
                 acf::AcfMessageInfo& out, std::vector<uint8_t>& out_payload) {
        return srv->dispatch(0, req, payload, out, out_payload);
    };
}

std::shared_ptr<mock::Server> make_server() {
    auto srv = std::make_shared<mock::Server>();
    srv->advance_to_rcp_configured();
    return srv;
}

acf::AcfMessageInfo gpio_read_request() {
    return acf::make_standard_request(mock::kGpioByteBusId, 0, false, 0);
}

} // namespace

// ── No rules ─────────────────────────────────────────────────────────────────

TEST_CASE("FaultInject::send passes through when no rules active", "[faultinject][REQ-FI-001]") {
    faultinject::Interceptor fi(make_mock_fn(make_server()));
    auto req = gpio_read_request();
    acf::AcfMessageInfo out;
    std::vector<uint8_t> out_payload;
    REQUIRE_FALSE(fi.send(Context::background(), req, {}, out, out_payload));
    REQUIRE_FALSE(out.err);
}

// ── Drop ──────────────────────────────────────────────────────────────────────

TEST_CASE("FaultInject Drop rule causes send to return an error", "[faultinject][REQ-FI-002]") {
    faultinject::Interceptor fi(make_mock_fn(make_server()));

    faultinject::Rule r;
    r.type  = faultinject::FaultType::Drop;
    r.count = -1; // infinite
    fi.add_rule(r);

    auto req = gpio_read_request();
    acf::AcfMessageInfo out;
    std::vector<uint8_t> out_payload;
    REQUIRE(fi.send(Context::background(), req, {}, out, out_payload));
}

// ── Error ─────────────────────────────────────────────────────────────────────

TEST_CASE("FaultInject Error rule returns a response with err set", "[faultinject][REQ-FI-003]") {
    faultinject::Interceptor fi(make_mock_fn(make_server()));

    faultinject::Rule r;
    r.type  = faultinject::FaultType::Error;
    r.count = -1;
    fi.add_rule(r);

    auto req = gpio_read_request();
    acf::AcfMessageInfo out;
    std::vector<uint8_t> out_payload;
    auto ec = fi.send(Context::background(), req, {}, out, out_payload);
    REQUIRE_FALSE(ec);
    REQUIRE(out.err);
    REQUIRE(out.rsp);
}

// ── Slow ──────────────────────────────────────────────────────────────────────

TEST_CASE("FaultInject Slow rule adds latency", "[faultinject][REQ-FI-004]") {
    faultinject::Interceptor fi(make_mock_fn(make_server()));

    faultinject::Rule r;
    r.type    = faultinject::FaultType::Slow;
    r.latency = std::chrono::milliseconds(20);
    r.count   = 1;
    fi.add_rule(r);

    auto req = gpio_read_request();
    acf::AcfMessageInfo out;
    std::vector<uint8_t> out_payload;

    auto start   = std::chrono::steady_clock::now();
    auto ec_slow = fi.send(Context::background(), req, {}, out, out_payload);
    (void)ec_slow;
    auto elapsed = std::chrono::steady_clock::now() - start;
    REQUIRE(elapsed >= std::chrono::milliseconds(20));
}

// ── Count-based expiry ────────────────────────────────────────────────────────

TEST_CASE("FaultInject rule expires after count sends", "[faultinject][REQ-FI-005]") {
    faultinject::Interceptor fi(make_mock_fn(make_server()));

    faultinject::Rule r;
    r.type  = faultinject::FaultType::Drop;
    r.count = 2; // fires twice then expires
    fi.add_rule(r);

    auto req = gpio_read_request();
    acf::AcfMessageInfo out;
    std::vector<uint8_t> out_payload;

    { auto ec = fi.send(Context::background(), req, {}, out, out_payload); REQUIRE(ec); } // drop 1
    { auto ec = fi.send(Context::background(), req, {}, out, out_payload); REQUIRE(ec); } // drop 2
    REQUIRE_FALSE(fi.send(Context::background(), req, {}, out, out_payload)); // passes
}

// ── Expiry on the firing call must not read an erased rule (cpp-RCP-D6) ───────
// pick_rule() used to return a Rule* into its own std::vector and erase that
// very element before returning, so send() dereferenced an erased element to
// read type/latency. An unsanitized build usually got away with it (erasing a
// trailing element does not reallocate), which is why the count-expiry test
// above never caught it; AddressSanitizer reports a container-overflow. These
// cases pin the fixed contract: the rule is applied from a copy taken while
// the lock is held, so its effect is correct on the exact call that retires
// it, and remains correct with concurrent callers.

TEST_CASE("A rule that expires on the same call it fires is still applied correctly",
          "[faultinject][REQ-FI-005]") {
    faultinject::Interceptor fi(make_mock_fn(make_server()));

    faultinject::Rule r;
    r.type    = faultinject::FaultType::Slow;
    r.latency = 60ms;
    r.count   = 1; // fires once and is retired during that same pick_rule()
    fi.add_rule(r);

    auto req = gpio_read_request();
    acf::AcfMessageInfo out;
    std::vector<uint8_t> out_payload;

    // The Slow rule's latency is read after the rule has been erased; it must
    // still be the 60ms that was configured, not whatever the freed slot held.
    auto start = std::chrono::steady_clock::now();
    REQUIRE_FALSE(fi.send(Context::background(), req, {}, out, out_payload));
    auto elapsed = std::chrono::steady_clock::now() - start;
    REQUIRE(elapsed >= 50ms);

    // The rule really is gone: the next call passes straight through.
    start = std::chrono::steady_clock::now();
    REQUIRE_FALSE(fi.send(Context::background(), req, {}, out, out_payload));
    REQUIRE(std::chrono::steady_clock::now() - start < 50ms);
}

TEST_CASE("Concurrent senders retiring count-limited rules do not read erased rules",
          "[faultinject][REQ-FI-005]") {
    faultinject::Interceptor fi(make_mock_fn(make_server()));

    // Many single-shot rules, so nearly every send() retires the element it
    // just picked while other threads are picking/erasing/appending too.
    constexpr int kRules   = 256;
    constexpr int kThreads = 8;
    for (int i = 0; i < kRules; ++i) {
        faultinject::Rule r;
        r.type  = faultinject::FaultType::Timeout;
        r.count = 1;
        fi.add_rule(r);
    }

    std::atomic<int> timeouts{0};
    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&] {
            for (int i = 0; i < kRules / kThreads; ++i) {
                auto req = gpio_read_request();
                acf::AcfMessageInfo out;
                std::vector<uint8_t> out_payload;
                if (fi.send(Context::background(), req, {}, out, out_payload) == ErrTimeout)
                    timeouts.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    for (auto& th : threads) th.join();

    // Every send consumed exactly one single-shot Timeout rule; a torn read of
    // an erased rule would surface here as a wrong (or garbage) FaultType.
    REQUIRE(timeouts.load() == kRules);
}

// ── clear_rules ───────────────────────────────────────────────────────────────

TEST_CASE("FaultInject::clear_rules removes all active rules", "[faultinject][REQ-FI-006]") {
    faultinject::Interceptor fi(make_mock_fn(make_server()));

    faultinject::Rule r;
    r.type  = faultinject::FaultType::Drop;
    r.count = -1;
    fi.add_rule(r);
    fi.clear_rules();

    auto req = gpio_read_request();
    acf::AcfMessageInfo out;
    std::vector<uint8_t> out_payload;
    REQUIRE_FALSE(fi.send(Context::background(), req, {}, out, out_payload));
}

// ── Timeout ───────────────────────────────────────────────────────────────────

TEST_CASE("FaultInject Timeout rule returns ErrTimeout", "[faultinject][REQ-FI-007]") {
    faultinject::Interceptor fi(make_mock_fn(make_server()));

    faultinject::Rule r;
    r.type  = faultinject::FaultType::Timeout;
    r.count = -1;
    fi.add_rule(r);

    auto req = gpio_read_request();
    acf::AcfMessageInfo out;
    std::vector<uint8_t> out_payload;
    auto ec = fi.send(Context::background(), req, {}, out, out_payload);
    REQUIRE(ec == ErrTimeout);
}

// ── Usable as an rcp::RequestFn ───────────────────────────────────────────────

TEST_CASE("FaultInject::Interceptor is itself usable as an rcp::RequestFn via Adapt()",
          "[faultinject][REQ-FI-008]") {
    faultinject::Interceptor fi(make_mock_fn(make_server()));
    // Interceptor holds a std::mutex, so it is not copyable; Adapt()'s
    // RequestFn parameter would otherwise try to copy it into a
    // std::function. std::ref keeps this call-by-reference.
    auto caller = Adapt(std::ref(fi));

    relay::Message req;
    req.protocol       = relay::Protocol::RCP;
    req.id             = endpoint_id_to_relay_id(mock::kGpioByteBusId);
    req.meta["rcp.op"] = "read";

    auto [resp, ec] = caller->call(relay::Context::with_timeout(1s), req);
    REQUIRE_FALSE(ec);
    REQUIRE(resp.id == req.id);
}
