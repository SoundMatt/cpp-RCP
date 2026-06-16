// Command latency safety-timing test (GSN argument for ASIL-B timing budget).
//
// Runs a 30-second workload against the mock controller and records P50/P99/
// P999/Max latency. Writes results to COMMAND_LATENCY.md in the build directory
// (relative to CWD) so cpfusa trace can include it as a safety artifact.
//
// Pass/fail gate: P99 < 1 ms and Max < 10 ms (in-process mock baseline).
#include <catch2/catch_test_macros.hpp>
#include <rcp/mock.hpp>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <vector>

using namespace rcp;
using namespace std::chrono;

static int64_t ns(steady_clock::duration d) {
    return duration_cast<nanoseconds>(d).count();
}

static double us(int64_t n) { return static_cast<double>(n) / 1000.0; }

TEST_CASE("Command latency P99 < 1ms over 30s workload", "[latency][safety]") {
    auto ctrl = std::make_shared<mock::Controller>(Zone::FrontLeft);
    Command cmd;
    cmd.zone = Zone::FrontLeft;

    std::vector<int64_t> samples;
    samples.reserve(100'000);

    auto deadline = steady_clock::now() + seconds(30);
    while (steady_clock::now() < deadline) {
        Response resp;
        auto t0 = steady_clock::now();
        auto ec  = ctrl->send(Context::background(), cmd, resp);
        auto t1  = steady_clock::now();
        (void)ec;
        samples.push_back(ns(t1 - t0));
    }

    std::sort(samples.begin(), samples.end());

    size_t n     = samples.size();
    int64_t p50  = samples[n * 50  / 100];
    int64_t p99  = samples[n * 99  / 100];
    int64_t p999 = samples[n * 999 / 1000];
    int64_t max  = samples.back();

    // Write COMMAND_LATENCY.md for cpfusa trace ingestion.
    std::ofstream md("COMMAND_LATENCY.md");
    if (md) {
        md << "# Command Latency Results\n\n"
           << "Workload: " << n << " sends over 30 s (mock transport)\n\n"
           << "| Metric | Value |\n"
           << "|--------|-------|\n"
           << "| P50    | " << us(p50)  << " µs |\n"
           << "| P99    | " << us(p99)  << " µs |\n"
           << "| P99.9  | " << us(p999) << " µs |\n"
           << "| Max    | " << us(max)  << " µs |\n";
    }

    // Safety gate: in-process mock must be well under real-world ASIL-B budget.
    // P99 < 1 ms = 1,000,000 ns; Max < 10 ms = 10,000,000 ns
    REQUIRE(p99 < 1'000'000LL);
    REQUIRE(max < 10'000'000LL);
}
