// Benchmark tests for the rcp::mock transport.
//
// Measures round-trip latency and throughput for common operations.
// Run with: ctest -R bench --output-on-failure
// Or directly: ./tests/bench_mock [!benchmark]
#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>
#include <rcp/mock.hpp>

#include <thread>
#include <vector>

using namespace rcp;

// ── Helpers ───────────────────────────────────────────────────────────────────

static std::shared_ptr<mock::Controller> make_ctrl(Zone z = Zone::FrontLeft) {
    return std::make_shared<mock::Controller>(z);
}

// ── Sanity test (always runs in CTest) ───────────────────────────────────────

TEST_CASE("bench mock baseline send succeeds", "[bench]") {
    auto ctrl = make_ctrl();
    Command cmd;
    cmd.zone = Zone::FrontLeft;
    Response resp;
    REQUIRE_FALSE(ctrl->send(Context::background(), cmd, resp));
}

// ── Benchmarks (run with: bench_mock [!benchmark]) ───────────────────────────

TEST_CASE("Benchmark: Send_RoundTrip", "[!benchmark]") {
    auto ctrl = make_ctrl();
    Command cmd;
    cmd.zone = Zone::FrontLeft;
    Response resp;
    auto ctx = Context::background();

    BENCHMARK("mock::Controller::send round-trip") {
        auto ec = ctrl->send(ctx, cmd, resp);
        return ec;
    };
}

TEST_CASE("Benchmark: Send_RoundTrip_WithPayload", "[!benchmark]") {
    auto ctrl = make_ctrl();
    Command cmd;
    cmd.zone    = Zone::FrontLeft;
    cmd.payload = std::vector<uint8_t>(64, 0xAB);
    Response resp;
    auto ctx = Context::background();

    BENCHMARK("mock::Controller::send 64-byte payload") {
        auto ec = ctrl->send(ctx, cmd, resp);
        return ec;
    };
}

TEST_CASE("Benchmark: Send_Concurrent", "[!benchmark]") {
    auto ctrl = make_ctrl();
    constexpr int N = 8;

    BENCHMARK("8-thread concurrent send") {
        std::vector<std::thread> threads;
        threads.reserve(N);
        for (int i = 0; i < N; ++i) {
            threads.emplace_back([&ctrl] {
                Command cmd;
                cmd.zone = Zone::FrontLeft;
                Response resp;
                auto ec = ctrl->send(Context::background(), cmd, resp);
                (void)ec;
            });
        }
        for (auto& t : threads) t.join();
        return N;
    };
}

TEST_CASE("Benchmark: Publish_FanOut", "[!benchmark]") {
    auto ctrl = make_ctrl();
    constexpr int kSubs = 8;

    // Register 8 subscribers.
    std::vector<std::shared_ptr<StatusChannel>> channels;
    for (int i = 0; i < kSubs; ++i) {
        std::shared_ptr<StatusChannel> ch;
        auto ec = ctrl->subscribe(Context::background(), ch);
        (void)ec;
        channels.push_back(ch);
    }

    std::vector<uint8_t> payload(32, 0);

    BENCHMARK("publish to 8 subscribers") {
        ctrl->publish(payload);
        return kSubs;
    };

    for (auto& c : channels) c->close();
}

TEST_CASE("Benchmark: Registry_Lookup", "[!benchmark]") {
    auto reg = mock::new_registry();

    BENCHMARK("mock::Registry::lookup") {
        std::shared_ptr<rcp::Controller> out;
        auto ec = reg->lookup(Zone::FrontLeft, out);
        return ec;
    };
}
