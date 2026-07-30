// Benchmark tests for the rcp::mock transport.
//
// Measures round-trip latency and throughput for common operations.
// Run with: ctest -R bench --output-on-failure
// Or directly: ./tests/bench_mock [!benchmark]
//
// Rebound (cpp-RCP-FS-01/#84): this used to benchmark
// legacy_mock::Controller::send() against the retired Zone/Command/Response
// model. That model is retired; rcp::mock::Server (rcp/mock.hpp) — the
// TC18-shaped in-process RC Server simulator this file's own name already
// referred to — is the transport actually under benchmark now.
#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>
#include <rcp/acf.hpp>
#include <rcp/mock.hpp>

#include <memory>
#include <thread>
#include <vector>

using namespace rcp;

// ── Helpers ───────────────────────────────────────────────────────────────────

static std::shared_ptr<mock::Server> make_server() {
    auto srv = std::make_shared<mock::Server>();
    srv->advance_to_rcp_configured();
    return srv;
}

// ── Sanity test (always runs in CTest) ───────────────────────────────────────

TEST_CASE("bench mock baseline dispatch succeeds", "[bench]") {
    auto srv = make_server();
    auto req = acf::make_standard_request(mock::kGpioByteBusId, /*transaction_num=*/0,
                                           /*write=*/false, /*read_size=*/0);
    acf::AcfMessageInfo resp;
    std::vector<uint8_t> resp_payload;
    REQUIRE_FALSE(srv->dispatch(0, req, {}, resp, resp_payload));
}

// ── Benchmarks (run with: bench_mock [!benchmark]) ───────────────────────────

TEST_CASE("Benchmark: Dispatch_RoundTrip", "[!benchmark]") {
    auto srv = make_server();
    auto req = acf::make_standard_request(mock::kGpioByteBusId, 0, false, 0);
    acf::AcfMessageInfo resp;
    std::vector<uint8_t> resp_payload;

    BENCHMARK("mock::Server::dispatch round-trip") {
        auto ec = srv->dispatch(0, req, {}, resp, resp_payload);
        return ec;
    };
}

TEST_CASE("Benchmark: Dispatch_RoundTrip_WithPayload", "[!benchmark]") {
    auto srv = make_server();
    auto req = acf::make_standard_request(mock::kSpiByteBusId, 0, /*write=*/true, 0);
    std::vector<uint8_t> payload(64, 0xAB);
    acf::AcfMessageInfo resp;
    std::vector<uint8_t> resp_payload;

    BENCHMARK("mock::Server::dispatch 64-byte payload") {
        auto ec = srv->dispatch(0, req, payload, resp, resp_payload);
        return ec;
    };
}

TEST_CASE("Benchmark: Dispatch_Concurrent", "[!benchmark]") {
    // mock::Server (like rcp/shmem.hpp's Channel) dispatches synchronously
    // on the calling thread and documents no locking of its own, so this
    // benchmark gives each thread its own Server rather than sharing one —
    // the same "one StreamID per connection" shape a real multi-client
    // deployment would have anyway, and it avoids a genuine data race on
    // shared mutable register-map/endpoint state that concurrent dispatch()
    // calls against a single Server would otherwise introduce.
    constexpr int N = 8;

    BENCHMARK("8-thread concurrent dispatch (one Server per thread)") {
        std::vector<std::thread> threads;
        threads.reserve(N);
        for (int i = 0; i < N; ++i) {
            threads.emplace_back([] {
                auto srv = make_server();
                auto req = acf::make_standard_request(mock::kGpioByteBusId, 0, false, 0);
                acf::AcfMessageInfo resp;
                std::vector<uint8_t> resp_payload;
                auto ec = srv->dispatch(0, req, {}, resp, resp_payload);
                (void)ec;
            });
        }
        for (auto& t : threads) t.join();
        return N;
    };
}
