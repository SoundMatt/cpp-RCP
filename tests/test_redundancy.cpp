// fusa:test REQ-RED-001
// fusa:test REQ-RED-002
// fusa:test REQ-RED-003
// fusa:test REQ-RED-004
// fusa:test REQ-RED-005
// fusa:test REQ-RED-006
// fusa:test REQ-RED-007
// fusa:test REQ-RED-008
//
// Rebound (cpp-RCP-FS-03/#86): redundancy::RedundantRequestFn now holds a
// pair of rcp::RequestFn (rcp/adapt.hpp) instead of decorating the retired
// rcp::Controller — see rcp/redundancy.hpp's own header comment. The old
// REQ-RED-006/REQ-RED-008 cases asserted Controller::close()/zone()
// passthrough, neither of which has an analog on a plain RequestFn (a
// RequestFn owns no closeable resource of its own, the same reasoning
// rcp/adapt.hpp's own close() note documents); they are replaced below with
// cases covering RedundantRequestFn's own usability as an rcp::RequestFn.
#include <catch2/catch_test_macros.hpp>

#include "rcp/mock.hpp"
#include "rcp/redundancy.hpp"

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
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

// fail_fn always fails with a fixed error code, regardless of the request.
// Used to deterministically trigger redundant failover.
RequestFn fail_fn(std::error_code ec) {
    return [ec](const Context&, const acf::AcfMessageInfo&, const std::vector<uint8_t>&,
                acf::AcfMessageInfo&, std::vector<uint8_t>&) { return ec; };
}

acf::AcfMessageInfo gpio_read_request() {
    return acf::make_standard_request(mock::kGpioByteBusId, 0, false, 0);
}

} // namespace

TEST_CASE("redundancy: primary succeeds, standby unused", "[redundancy]") {
    redundancy::RedundantRequestFn rr(make_mock_fn(make_server()), make_mock_fn(make_server()));
    REQUIRE(rr.is_primary_active());

    auto req = gpio_read_request();
    acf::AcfMessageInfo out;
    std::vector<uint8_t> out_payload;
    REQUIRE_FALSE(rr.send(Context{}, req, {}, out, out_payload));
    REQUIRE(rr.is_primary_active());
}

TEST_CASE("redundancy: manual promote switches to standby", "[redundancy]") {
    redundancy::RedundantRequestFn rr(make_mock_fn(make_server()), make_mock_fn(make_server()));

    rr.promote();
    REQUIRE_FALSE(rr.is_primary_active());

    auto req = gpio_read_request();
    acf::AcfMessageInfo out;
    std::vector<uint8_t> out_payload;
    REQUIRE_FALSE(rr.send(Context{}, req, {}, out, out_payload));
}

TEST_CASE("redundancy: double promote returns to primary", "[redundancy]") {
    redundancy::RedundantRequestFn rr(make_mock_fn(make_server()), make_mock_fn(make_server()));

    rr.promote();
    rr.promote();
    REQUIRE(rr.is_primary_active());
}

TEST_CASE("redundancy: auto-promotes standby on ErrClosed", "[redundancy][REQ-RED-002]") {
    redundancy::RedundantRequestFn rr(fail_fn(ErrClosed), make_mock_fn(make_server()));
    REQUIRE(rr.is_primary_active());

    auto req = gpio_read_request();
    acf::AcfMessageInfo out;
    std::vector<uint8_t> out_payload;
    REQUIRE_FALSE(rr.send(Context{}, req, {}, out, out_payload)); // succeeds via standby
    REQUIRE_FALSE(rr.is_primary_active());                         // standby is now active
}

TEST_CASE("redundancy: auto-promotes standby on ErrTimeout", "[redundancy][REQ-RED-003]") {
    redundancy::RedundantRequestFn rr(fail_fn(ErrTimeout), make_mock_fn(make_server()));

    auto req = gpio_read_request();
    acf::AcfMessageInfo out;
    std::vector<uint8_t> out_payload;
    REQUIRE_FALSE(rr.send(Context{}, req, {}, out, out_payload));
    REQUIRE_FALSE(rr.is_primary_active());
}

TEST_CASE("redundancy: auto_promote=false disables automatic failover",
          "[redundancy][REQ-RED-007]") {
    redundancy::Config cfg;
    cfg.auto_promote = false;
    redundancy::RedundantRequestFn rr(fail_fn(ErrClosed), make_mock_fn(make_server()), cfg);

    auto req = gpio_read_request();
    acf::AcfMessageInfo out;
    std::vector<uint8_t> out_payload;
    REQUIRE(rr.send(Context{}, req, {}, out, out_payload) == ErrClosed); // error surfaced, no failover
    REQUIRE(rr.is_primary_active());                                     // still on primary
}

TEST_CASE("redundancy: still on standby after a second consecutive primary failure",
          "[redundancy][REQ-RED-006]") {
    redundancy::RedundantRequestFn rr(fail_fn(ErrClosed), make_mock_fn(make_server()));

    auto req = gpio_read_request();
    acf::AcfMessageInfo out;
    std::vector<uint8_t> out_payload;
    REQUIRE_FALSE(rr.send(Context{}, req, {}, out, out_payload)); // promotes to standby
    REQUIRE_FALSE(rr.is_primary_active());
    REQUIRE_FALSE(rr.send(Context{}, req, {}, out, out_payload)); // standby still serves
    REQUIRE_FALSE(rr.is_primary_active());
}

TEST_CASE("redundancy: RedundantRequestFn is itself usable as an rcp::RequestFn via Adapt()",
          "[redundancy][REQ-RED-008]") {
    redundancy::RedundantRequestFn rr(make_mock_fn(make_server()), make_mock_fn(make_server()));
    // RedundantRequestFn holds a std::mutex, so it is not copyable;
    // Adapt()'s RequestFn parameter would otherwise try to copy it into a
    // std::function. std::ref keeps this call-by-reference.
    auto caller = Adapt(std::ref(rr));

    relay::Message req;
    req.protocol       = relay::Protocol::RCP;
    req.id             = endpoint_id_to_relay_id(mock::kGpioByteBusId);
    req.meta["rcp.op"] = "read";

    auto [resp, ec] = caller->call(relay::Context::with_timeout(1s), req);
    REQUIRE_FALSE(ec);
    REQUIRE(resp.id == req.id);
}

// ── Concurrency regression: promote() must not be a blind toggle ───────────
//
// send() reads active_ under a short lock, then calls the RequestFn pointer
// *outside* the lock. Two concurrent send() calls can therefore both observe
// active_ == &primary_, both have the primary fail, and both attempt to
// promote. An unconditional toggle (active_ = active_==&primary_ ? &standby_
// : &primary_) would apply twice in that case -- the first promote flips
// primary->standby, the second (serialized behind the same mutex, but blind
// to what the caller actually observed) flips it straight back
// standby->primary -- silently reverting to the confirmed-bad primary and
// defeating failover for every later caller. This is a real regression risk
// for REQ-RED-006 ("subsequent send() calls shall continue to be served by
// the standby without reverting to the primary on their own").
//
// The two send() calls are driven through an explicit two-phase handshake
// (entered_cv / release_cv), following this project's established pattern
// for deterministic concurrency tests (see e.g. shmem's
// "admits up to queue_capacity concurrent callers" case): both threads are
// held inside their (still-primary) RequestFn call -- i.e. both have already
// captured active_ == &primary_ under send()'s lock -- until both have
// entered, and are then released together so their promote attempts
// genuinely race on the *same* observed-primary pointer. This makes the race
// deterministic instead of depending on OS scheduling luck.
TEST_CASE("redundancy: concurrent send() failures on the same observed primary promote "
          "exactly once and never revert to primary",
          "[redundancy][thread][REQ-RED-006]") {
    constexpr int kThreads = 8;

    std::mutex              mu;
    std::condition_variable entered_cv;
    std::condition_variable release_cv;
    int                     entered_count = 0;
    bool                    may_release   = false;

    // Always fails (like fail_fn), but first blocks every caller until all
    // kThreads callers are simultaneously inside the primary call -- forcing
    // every send() to have observed active_ == &primary_ before any of them
    // can reach promote_from().
    RequestFn blocking_fail_fn = [&](const Context&, const acf::AcfMessageInfo&,
                                      const std::vector<uint8_t>&, acf::AcfMessageInfo&,
                                      std::vector<uint8_t>&) {
        {
            std::lock_guard<std::mutex> lk(mu);
            ++entered_count;
        }
        entered_cv.notify_all();

        std::unique_lock<std::mutex> lk(mu);
        release_cv.wait(lk, [&] { return may_release; });
        return ErrClosed;
    };

    // The standby is a trivial, stateless always-succeeds fn rather than a
    // mock::Server (which is not itself documented/guaranteed thread-safe
    // for concurrent dispatch()) -- this test's own retries after promotion
    // are deliberately concurrent, and must not introduce a second, unrelated
    // race of their own on top of the one under test.
    RequestFn always_ok_fn = [](const Context&, const acf::AcfMessageInfo&,
                                 const std::vector<uint8_t>&, acf::AcfMessageInfo&,
                                 std::vector<uint8_t>&) { return std::error_code{}; };

    redundancy::RedundantRequestFn rr(blocking_fail_fn, always_ok_fn);
    REQUIRE(rr.is_primary_active());

    auto req = gpio_read_request();

    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back([&] {
            acf::AcfMessageInfo  out;
            std::vector<uint8_t> out_payload;
            rr.send(Context{}, req, {}, out, out_payload);
        });
    }

    // Wait until every thread's send() is blocked inside the primary call --
    // each has already read active_ == &primary_ under the lock, before any
    // of them can call promote_from().
    {
        std::unique_lock<std::mutex> lk(mu);
        entered_cv.wait(lk, [&] { return entered_count == kThreads; });
    }

    // Release them all together: every thread's primary call now returns
    // ErrClosed and races to promote_from(observed == &primary_) at
    // (approximately) the same time, serialized only by RedundantRequestFn's
    // internal mutex.
    {
        std::lock_guard<std::mutex> lk(mu);
        may_release = true;
    }
    release_cv.notify_all();

    for (auto& th : threads) th.join();

    // Exactly one net promotion must have occurred: active_ must be the
    // standby, never reverted back to the primary that every caller observed
    // failing (REQ-RED-006). With the old blind-toggle promote(), an even
    // number of concurrent promote attempts on the same observed pointer
    // cancel back out to &primary_, and this REQUIRE fails.
    REQUIRE_FALSE(rr.is_primary_active());
}
