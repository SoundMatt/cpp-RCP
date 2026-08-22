// fusa:test REQ-ADMIN-001
// fusa:test REQ-ADMIN-002
// fusa:test REQ-ADMIN-003
// fusa:test REQ-ADMIN-004
// fusa:test REQ-ADMIN-005
// fusa:test REQ-ADMIN-006
// fusa:test REQ-ADMIN-007
// fusa:test REQ-ADMIN-008
//
// Rebound (cpp-RCP-FS-01/#84): AdminServer now reports streams from an
// rcp::shmem::Registry instead of zones from the retired rcp::Registry —
// see rcp/admin.hpp's own header comment.
#include <catch2/catch_test_macros.hpp>

#include "rcp/admin.hpp"

#include <atomic>
#include <thread>
#include <vector>

using namespace rcp;

namespace {
std::shared_ptr<shmem::Channel> add(shmem::Registry& reg, uint64_t stream_key) {
    auto ch = shmem::new_channel(stream_key);
    reg.add_channel(ch);
    return ch;
}
} // namespace

TEST_CASE("admin: streams lists registered channels", "[admin]") {
    shmem::Registry reg;
    for (uint64_t k : {1, 2, 3, 4, 5}) add(reg, k);
    admin::AdminServer srv(reg);

    auto streams = srv.streams();
    REQUIRE(streams.size() == 5);
    for (auto& si : streams) {
        REQUIRE(si.registered);
    }
}

TEST_CASE("admin: subscribe receives emitted events", "[admin]") {
    shmem::Registry reg;
    admin::AdminServer srv(reg);

    std::vector<admin::Event> received;
    srv.subscribe([&](const admin::Event& ev) { received.push_back(ev); });

    srv.emit({admin::EventType::StreamRegistered, 1, {}});
    srv.emit({admin::EventType::StatusUpdate, 4, {}});

    REQUIRE(received.size() == 2);
    REQUIRE(received[0].type == admin::EventType::StreamRegistered);
    REQUIRE(received[1].stream_key == 4);
}

TEST_CASE("admin: metrics_text contains counter lines", "[admin]") {
    shmem::Registry reg;
    admin::AdminServer srv(reg);

    srv.record_counter("rcp.commands.total", "stream=\"1\"", 10.0);
    srv.record_counter("rcp.commands.total", "stream=\"1\"", 5.0);

    auto text = srv.metrics_text();
    REQUIRE(text.find("rcp.commands.total") != std::string::npos);
    REQUIRE(text.find("15") != std::string::npos);
}

TEST_CASE("admin: multiple subscribers all receive events", "[admin]") {
    shmem::Registry reg;
    admin::AdminServer srv(reg);

    int count_a = 0, count_b = 0;
    srv.subscribe([&](const admin::Event&) { ++count_a; });
    srv.subscribe([&](const admin::Event&) { ++count_b; });

    srv.emit({admin::EventType::StreamDeregistered, 3, {}});

    REQUIRE(count_a == 1);
    REQUIRE(count_b == 1);
}

TEST_CASE("admin: event delivers correct type and stream_key", "[admin][REQ-ADMIN-008]") {
    shmem::Registry reg;
    admin::AdminServer srv(reg);

    admin::Event got{};
    srv.subscribe([&](const admin::Event& ev) { got = ev; });
    srv.emit({admin::EventType::StatusUpdate, 2, {}});

    REQUIRE(got.type == admin::EventType::StatusUpdate);
    REQUIRE(got.stream_key == 2);
}

TEST_CASE("admin: concurrent record_counter and emit are thread-safe",
          "[admin][REQ-ADMIN-004][REQ-ADMIN-005]") {
    shmem::Registry reg;
    admin::AdminServer srv(reg);

    std::atomic<int> events{0};
    srv.subscribe([&](const admin::Event&) { events.fetch_add(1); });

    constexpr int kThreads = 8;
    constexpr int kPerThread = 1000;
    std::vector<std::thread> ts;
    for (int t = 0; t < kThreads; ++t) {
        ts.emplace_back([&] {
            for (int i = 0; i < kPerThread; ++i) {
                srv.record_counter("rcp.commands.total", "stream=\"1\"", 1.0);
                srv.emit({admin::EventType::StatusUpdate, 1, {}});
            }
        });
    }
    for (auto& th : ts) th.join();

    // record_counter accumulates every delta exactly once (REQ-ADMIN-004) and the
    // server tolerates concurrent mutation without data races (REQ-ADMIN-005).
    REQUIRE(events.load() == kThreads * kPerThread);
    auto text = srv.metrics_text();
    REQUIRE(text.find("rcp.commands.total") != std::string::npos);
    REQUIRE(text.find(std::to_string(kThreads * kPerThread)) != std::string::npos);
}

// ── [Phase 17 c-RCP-reference pass, cpp-RCP issue #129] emit() deadlock fix ──
// See rcp/admin.hpp's own header comment, delta #1: emit() used to hold mu_
// across every subscriber callback invocation, so a subscriber calling back
// into the same AdminServer (subscribe()/emit()/record_counter()) would
// deadlock against a plain non-recursive std::mutex. This test hangs forever
// pre-fix and completes immediately post-fix — a real regression test, not
// just a documentation stand-in.
TEST_CASE("admin: emit — a subscriber that calls back into the same server does not deadlock",
          "[admin][REQ-ADMIN-004]") {
    shmem::Registry reg;
    admin::AdminServer srv(reg);

    int reentrant_calls = 0;
    bool reentered = false;
    srv.subscribe([&](const admin::Event& ev) {
        // Re-enter the server from inside the callback exactly once, on
        // every distinct kind of call emit()'s own mu_ also guards, to
        // exercise all three re-entrant paths the header comment names.
        if (!reentered) {
            reentered = true;
            REQUIRE_FALSE(srv.record_counter("rcp.reentrant.total", "", 1.0));
            REQUIRE_FALSE(srv.subscribe([&](const admin::Event&) { ++reentrant_calls; }));
            srv.emit({admin::EventType::StatusUpdate, ev.stream_key, {}});
        }
    });

    srv.emit({admin::EventType::StreamRegistered, 7, {}});

    // Reaching here at all (rather than hanging) is the fix; the counts
    // below additionally confirm the re-entrant calls actually took effect.
    REQUIRE(srv.counter_count() == 1);
    REQUIRE(srv.subscriber_count() == 2);
    REQUIRE(reentrant_calls == 1);
}

// ── [c-RCP-17] Fixed-capacity subscriber list / counter table ───────────────
// Boundary coverage ported from c-RCP's tests/test_admin.c
// test_subscribe_at_max_succeeds_then_next_fails/
// test_record_counter_at_max_succeeds_then_next_new_one_fails — see
// rcp/admin.hpp's own header comment, delta #2.

TEST_CASE("admin: subscribe at max succeeds, then the next one fails", "[admin][REQ-ADMIN-003]") {
    shmem::Registry reg;
    admin::AdminServer srv(reg);

    for (size_t i = 0; i < admin::AdminServer::kMaxSubscribers; ++i) {
        REQUIRE_FALSE(srv.subscribe([](const admin::Event&) {}));
    }
    REQUIRE(srv.subscriber_count() == admin::AdminServer::kMaxSubscribers);

    // One more, at capacity: rejected, not silently grown.
    auto ec = srv.subscribe([](const admin::Event&) {});
    REQUIRE(ec == admin::AdminErrc::subscriber_capacity_exceeded);
    REQUIRE(srv.subscriber_count() == admin::AdminServer::kMaxSubscribers);
}

TEST_CASE("admin: record_counter at max succeeds, a repeat still succeeds, a new one fails",
          "[admin][REQ-ADMIN-005]") {
    shmem::Registry reg;
    admin::AdminServer srv(reg);

    for (size_t i = 0; i < admin::AdminServer::kMaxCounters; ++i) {
        REQUIRE_FALSE(srv.record_counter("counter_" + std::to_string(i), "", 1.0));
    }
    REQUIRE(srv.counter_count() == admin::AdminServer::kMaxCounters);

    // A repeat delta against an already-tracked counter still succeeds at
    // capacity -- only a genuinely new (name, labels) pair is rejected.
    REQUIRE_FALSE(srv.record_counter("counter_0", "", 1.0));
    REQUIRE(srv.counter_count() == admin::AdminServer::kMaxCounters);

    // One more, genuinely new, at capacity: rejected, not silently grown.
    auto ec = srv.record_counter("one_too_many", "", 1.0);
    REQUIRE(ec == admin::AdminErrc::counter_capacity_exceeded);
    REQUIRE(srv.counter_count() == admin::AdminServer::kMaxCounters);
}
