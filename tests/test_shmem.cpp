// fusa:test REQ-SHMEM-001
// fusa:test REQ-SHMEM-002
// fusa:test REQ-SHMEM-003
// fusa:test REQ-SHMEM-004
// fusa:test REQ-SHMEM-005
// fusa:test REQ-SHMEM-006
// fusa:test REQ-SHMEM-007
// fusa:test REQ-SHMEM-008

// Tests for rcp/shmem.hpp — the zero-copy in-process request channel
// (ROADMAP.md milestone 58, "Auxiliary Transport & Cross-Cutting Rebind",
// v2.14.0).

#include <catch2/catch_test_macros.hpp>
#include <rcp/shmem.hpp>

#include <atomic>
#include <thread>
#include <vector>

using namespace rcp;
using namespace rcp::shmem;

namespace {
acf::AcfMessageInfo standard_request(avtp::ByteBusId bus_id, uint8_t transaction_num) {
    return acf::make_standard_request(bus_id, transaction_num, /*write=*/false, /*read_size=*/4);
}
} // namespace

// ── Zero-copy delivery ────────────────────────────────────────────────────────

TEST_CASE("shmem Channel::request delivers to the handler directly, without a wire encode/decode",
          "[shmem][REQ-SHMEM-001]") {
    auto ch = new_channel(/*stream_key=*/42);
    ch->set_handler([](size_t, const acf::AcfMessageInfo& req,
                        const std::vector<uint8_t>& payload,
                        acf::AcfMessageInfo& out_resp, std::vector<uint8_t>& out_payload) {
        out_resp    = acf::make_response(req, acf::ResponseKind::ReadResponse);
        out_payload = payload; // echo unchanged -- proves no lossy round trip occurred
        return std::error_code{};
    });

    auto req = standard_request(1, 5);
    std::vector<uint8_t>  req_payload{0xDE, 0xAD, 0xBE, 0xEF};
    acf::AcfMessageInfo    resp;
    std::vector<uint8_t>   resp_payload;

    REQUIRE_FALSE(ch->request(0, req, req_payload, resp, resp_payload));
    REQUIRE(resp_payload == req_payload);
    REQUIRE(resp.byte_bus_id == req.byte_bus_id);
    REQUIRE(resp.transaction_num == req.transaction_num);
}

TEST_CASE("shmem Channel::request answers Acknowledge by default when no handler is set",
          "[shmem][REQ-SHMEM-002]") {
    auto ch  = new_channel(1);
    auto req = standard_request(1, 1);
    acf::AcfMessageInfo   resp;
    std::vector<uint8_t>  resp_payload;

    REQUIRE_FALSE(ch->request(0, req, {}, resp, resp_payload));
    REQUIRE(acf::response_kind_of(resp) == acf::ResponseKind::Acknowledge);
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────

TEST_CASE("shmem Channel::request returns ErrClosed after close()", "[shmem][REQ-SHMEM-003]") {
    auto ch = new_channel(1);
    ch->close();

    auto req = standard_request(1, 1);
    acf::AcfMessageInfo   resp;
    std::vector<uint8_t>  resp_payload;
    REQUIRE(ch->request(0, req, {}, resp, resp_payload) == ErrClosed);
}

// ── Registry ──────────────────────────────────────────────────────────────────

TEST_CASE("shmem Registry::lookup finds a channel registered via add_channel",
          "[shmem][REQ-SHMEM-004]") {
    auto reg = new_registry();
    auto ch  = new_channel(7);
    REQUIRE_FALSE(reg->add_channel(ch));

    std::shared_ptr<Channel> out;
    REQUIRE_FALSE(reg->lookup(7, out));
    REQUIRE(out == ch);
}

TEST_CASE("shmem Registry::close closes all registered channels", "[shmem][REQ-SHMEM-005]") {
    auto reg = new_registry();
    auto ch  = new_channel(7);
    REQUIRE_FALSE(reg->add_channel(ch));
    REQUIRE_FALSE(reg->close());

    std::shared_ptr<Channel> out;
    REQUIRE(reg->lookup(7, out) == ErrClosed);
    REQUIRE_FALSE(ch->ok()); // the channel itself was closed too, not just delisted

    auto req = standard_request(1, 1);
    acf::AcfMessageInfo   resp;
    std::vector<uint8_t>  resp_payload;
    REQUIRE(ch->request(0, req, {}, resp, resp_payload) == ErrClosed);
}

// ── Concurrency ───────────────────────────────────────────────────────────────

TEST_CASE("shmem Channel::request is thread-safe for concurrent callers",
          "[shmem][REQ-SHMEM-006]") {
    auto ch = new_channel(1);
    std::atomic<int> call_count{0};
    ch->set_handler([&](size_t, const acf::AcfMessageInfo& req, const std::vector<uint8_t>&,
                         acf::AcfMessageInfo& out_resp, std::vector<uint8_t>&) {
        call_count.fetch_add(1);
        out_resp = acf::make_response(req, acf::ResponseKind::Acknowledge);
        return std::error_code{};
    });

    constexpr int kThreads = 8;
    constexpr int kPerThread = 50;
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t] {
            for (int i = 0; i < kPerThread; ++i) {
                auto req = standard_request(1, static_cast<uint8_t>(t));
                acf::AcfMessageInfo   resp;
                std::vector<uint8_t>  resp_payload;
                auto ec = ch->request(static_cast<size_t>(t), req, {}, resp, resp_payload);
                (void)ec;
            }
        });
    }
    for (auto& th : threads) th.join();

    REQUIRE(call_count.load() == kThreads * kPerThread);
}

// ── Per-call client id pass-through ──────────────────────────────────────────

TEST_CASE("shmem Channel::request forwards the caller-supplied client id to the handler unchanged",
          "[shmem][REQ-SHMEM-007]") {
    auto ch = new_channel(1);
    size_t seen = 999;
    ch->set_handler([&](size_t client, const acf::AcfMessageInfo& req,
                         const std::vector<uint8_t>&, acf::AcfMessageInfo& out_resp,
                         std::vector<uint8_t>&) {
        seen     = client;
        out_resp = acf::make_response(req, acf::ResponseKind::Acknowledge);
        return std::error_code{};
    });

    auto req = standard_request(1, 1);
    acf::AcfMessageInfo   resp;
    std::vector<uint8_t>  resp_payload;
    REQUIRE_FALSE(ch->request(/*client=*/42, req, {}, resp, resp_payload));
    REQUIRE(seen == 42);
}

// ── Idempotent close ──────────────────────────────────────────────────────────

TEST_CASE("shmem Channel::close and Registry::close are idempotent", "[shmem][REQ-SHMEM-008]") {
    auto ch = new_channel(1);
    ch->close();
    ch->close(); // second call must not crash

    auto reg = new_registry();
    REQUIRE_FALSE(reg->add_channel(new_channel(2)));
    REQUIRE_FALSE(reg->close());
    REQUIRE_FALSE(reg->close()); // second call must not crash
}
