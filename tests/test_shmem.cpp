// fusa:test REQ-SHMEM-001
// fusa:test REQ-SHMEM-002
// fusa:test REQ-SHMEM-003
// fusa:test REQ-SHMEM-004
// fusa:test REQ-SHMEM-005
// fusa:test REQ-SHMEM-006
// fusa:test REQ-SHMEM-007
// fusa:test REQ-SHMEM-008
// fusa:test REQ-SHMEM-009
// fusa:test REQ-SHMEM-010

// Tests for rcp/shmem.hpp — the in-process, bounded-queue request channel
// (cpp-RCP issue #129 Phase 5 wave 2).
//
// NOTE: an earlier draft of this comment claimed REQ-SHMEM-004/007 had "no
// analog" here, based on c-RCP's own shmem.h numbering (where those ids mean
// recv() timeout / recv() destination-buffer-too-small). That was a
// numbering mix-up: THIS project's .fusa-reqs.json assigns REQ-SHMEM-004 to
// "Registry::lookup finds a channel registered via add_channel" and
// REQ-SHMEM-007 to "the caller-supplied client id is forwarded to the
// handler unchanged" — both ordinary, directly testable behaviors, covered
// below like every other entry in this file.

#include <catch2/catch_test_macros.hpp>
#include <rcp/shmem.hpp>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

using namespace rcp;
using namespace rcp::shmem;

namespace {
acf::AcfMessageInfo standard_request(avtp::ByteBusId bus_id, uint8_t transaction_num) {
    return acf::make_standard_request(bus_id, transaction_num, /*write=*/false, /*read_size=*/4);
}
} // namespace

// ── Real byte-level round trip ──────────────────────────────────────────────
// This pass's core content fix: a request no longer reaches the handler as
// the caller's own in-memory objects — it is genuinely encoded to ACF_ABB/
// ACF_GBB bytes and decoded back first (rcp/shmem.hpp detail::
// encode_acf_message/decode_acf_message), the same codec rcp/acf.hpp's
// encode_acf_abb()/decode_acf_abb() apply on a real transport.

TEST_CASE("shmem Channel::request delivers a byte-decoded request to the handler and "
          "byte-decodes its response back to the caller",
          "[shmem][REQ-SHMEM-001]") {
    auto ch = new_channel(/*stream_key=*/42);
    ch->set_handler([](size_t, const acf::AcfMessageInfo& req,
                        const std::vector<uint8_t>& payload,
                        acf::AcfMessageInfo& out_resp, std::vector<uint8_t>& out_payload) {
        out_resp    = acf::make_response(req, acf::ResponseKind::ReadResponse);
        out_payload = payload; // echo unchanged -- proves the round trip is lossless
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
    REQUIRE(acf::response_kind_of(resp) == acf::ResponseKind::ReadResponse);
}

TEST_CASE("shmem Channel::request preserves every AcfMessageInfo wire field through the "
          "encode/decode round trip, not just byte_bus_id/transaction_num",
          "[shmem][REQ-SHMEM-001]") {
    auto ch = new_channel(7);
    acf::AcfMessageInfo seen_by_handler;
    ch->set_handler([&](size_t, const acf::AcfMessageInfo& req, const std::vector<uint8_t>&,
                         acf::AcfMessageInfo& out_resp, std::vector<uint8_t>&) {
        seen_by_handler = req;
        out_resp        = acf::make_response(req, acf::ResponseKind::WriteResponse);
        return std::error_code{};
    });

    acf::AcfMessageInfo req;
    req.acf_msg_type    = acf::kAcfMsgTypeAbb;
    req.byte_bus_id      = 0x123;
    req.transaction_num  = 200;
    req.evt_ack           = true;
    req.evt_op            = 0x5;
    req.hs                = true;
    req.cs                = true;
    req.op                = true;
    req.read_size_or_segment_num = 17;

    acf::AcfMessageInfo   resp;
    std::vector<uint8_t>  resp_payload;
    REQUIRE_FALSE(ch->request(0, req, {}, resp, resp_payload));

    REQUIRE(seen_by_handler.byte_bus_id == req.byte_bus_id);
    REQUIRE(seen_by_handler.transaction_num == req.transaction_num);
    REQUIRE(seen_by_handler.evt_ack == req.evt_ack);
    REQUIRE(seen_by_handler.evt_op == req.evt_op);
    REQUIRE(seen_by_handler.hs == req.hs);
    REQUIRE(seen_by_handler.cs == req.cs);
    REQUIRE(seen_by_handler.op == req.op);
    REQUIRE(seen_by_handler.read_size_or_segment_num == req.read_size_or_segment_num);

    // The response side round-trips too -- resp reflects what real bytes
    // would decode to, not the handler's in-memory object verbatim.
    REQUIRE(acf::response_kind_of(resp) == acf::ResponseKind::WriteResponse);
    REQUIRE(resp.byte_bus_id == req.byte_bus_id);
    REQUIRE(resp.transaction_num == req.transaction_num);
}

TEST_CASE("shmem Channel::request round-trips an ACF_GBB request through the GBB codec path",
          "[shmem][REQ-SHMEM-001]") {
    auto ch = new_channel(9);
    bool saw_gbb = false;
    ch->set_handler([&](size_t, const acf::AcfMessageInfo& req, const std::vector<uint8_t>&,
                         acf::AcfMessageInfo& out_resp, std::vector<uint8_t>&) {
        saw_gbb  = (req.acf_msg_type == acf::kAcfMsgTypeGbb);
        out_resp = acf::make_response(req, acf::ResponseKind::Acknowledge);
        return std::error_code{};
    });

    acf::AcfMessageInfo req;
    req.acf_msg_type   = acf::kAcfMsgTypeGbb;
    req.byte_bus_id     = 3;
    req.transaction_num = 9;

    acf::AcfMessageInfo   resp;
    std::vector<uint8_t>  resp_payload;
    REQUIRE_FALSE(ch->request(0, req, {}, resp, resp_payload));
    REQUIRE(saw_gbb);
    REQUIRE(resp.acf_msg_type == acf::kAcfMsgTypeGbb); // make_response() copies acf_msg_type from req
}

TEST_CASE("shmem Channel::request forwards the caller-supplied client id to the handler "
          "unchanged",
          "[shmem][REQ-SHMEM-007]") {
    auto ch = new_channel(5);
    size_t seen_client = static_cast<size_t>(-1);
    ch->set_handler([&](size_t client, const acf::AcfMessageInfo& req,
                         const std::vector<uint8_t>&, acf::AcfMessageInfo& out_resp,
                         std::vector<uint8_t>&) {
        seen_client = client;
        out_resp    = acf::make_response(req, acf::ResponseKind::Acknowledge);
        return std::error_code{};
    });

    auto req = standard_request(1, 1);
    acf::AcfMessageInfo   resp;
    std::vector<uint8_t>  resp_payload;

    REQUIRE_FALSE(ch->request(/*client=*/12345, req, {}, resp, resp_payload));
    REQUIRE(seen_client == 12345);

    // A different client id on a second call is forwarded independently --
    // not cached or defaulted from the first call.
    REQUIRE_FALSE(ch->request(/*client=*/0, req, {}, resp, resp_payload));
    REQUIRE(seen_client == 0);
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

// ── Lifecycle (REQ-SHMEM-005/010: Channel::request() plays both the "send"
// and "recv" role of c-RCP's own shmem_side_send()/shmem_side_recv(), each
// of which reports RCP_ERR_CLOSED once its side is closed — shmem.c:64-67/
// 100-113,109-113) ──────────────────────────────────────────────────────

TEST_CASE("shmem Channel::request returns ErrClosed after close()",
          "[shmem][REQ-SHMEM-005][REQ-SHMEM-010]") {
    auto ch = new_channel(1);
    ch->close();

    auto req = standard_request(1, 1);
    acf::AcfMessageInfo   resp;
    std::vector<uint8_t>  resp_payload;
    REQUIRE(ch->request(0, req, {}, resp, resp_payload) == ErrClosed);
}

// ── Capacity bound / backpressure (this pass's core fix: no bound existed
// at all before) ─────────────────────────────────────────────────────────

TEST_CASE("shmem Channel::queue_capacity reflects the constructor argument, clamped into "
          "[1, kMaxQueueCapacity]",
          "[shmem][REQ-SHMEM-006]") {
    REQUIRE(new_channel(1, /*queue_capacity=*/4)->queue_capacity() == 4);
    // 0 is clamped up to 1 -- a 0-slot pool would make every request() call
    // unconditionally busy, never a reachable, useful configuration (see
    // rcp/shmem.hpp's FrameSlots::set_logical_capacity(), mirroring c-RCP's
    // own rcp_shmem_avtp_pair_new() clamp, shmem.c:204).
    REQUIRE(new_channel(1, /*queue_capacity=*/0)->queue_capacity() == 1);
    // Clamped down to the physical ceiling.
    REQUIRE(new_channel(1, /*queue_capacity=*/1000)->queue_capacity() == kMaxQueueCapacity);
    // new_channel()'s own default matches Channel's own default.
    REQUIRE(new_channel(1)->queue_capacity() == kDefaultQueueCapacity);
}

TEST_CASE("shmem Channel::queue_depth reports one occupied slot while a request is being "
          "handled, and zero once it returns",
          "[shmem][REQ-SHMEM-006]") {
    auto ch = new_channel(1, /*queue_capacity=*/4);
    size_t depth_seen_by_handler = 999;
    ch->set_handler([&](size_t, const acf::AcfMessageInfo& req, const std::vector<uint8_t>&,
                         acf::AcfMessageInfo& out_resp, std::vector<uint8_t>&) {
        depth_seen_by_handler = ch->queue_depth();
        out_resp              = acf::make_response(req, acf::ResponseKind::Acknowledge);
        return std::error_code{};
    });

    REQUIRE(ch->queue_depth() == 0);
    auto req = standard_request(1, 1);
    acf::AcfMessageInfo   resp;
    std::vector<uint8_t>  resp_payload;
    REQUIRE_FALSE(ch->request(0, req, {}, resp, resp_payload));

    REQUIRE(depth_seen_by_handler == 1);
    REQUIRE(ch->queue_depth() == 0); // slot released once request() returns
}

TEST_CASE("shmem Channel::request returns ErrBusy immediately, without blocking, once "
          "queue_capacity requests are already in flight",
          "[shmem][REQ-SHMEM-006]") {
    auto ch = new_channel(1, /*queue_capacity=*/1);

    std::mutex              mu;
    std::condition_variable release_cv;
    bool                     entered = false;
    bool                     may_release = false;

    ch->set_handler([&](size_t, const acf::AcfMessageInfo& req, const std::vector<uint8_t>&,
                         acf::AcfMessageInfo& out_resp, std::vector<uint8_t>&) {
        {
            std::lock_guard<std::mutex> lk(mu);
            entered = true;
        }
        release_cv.notify_all();
        std::unique_lock<std::mutex> lk(mu);
        release_cv.wait(lk, [&] { return may_release; });
        out_resp = acf::make_response(req, acf::ResponseKind::Acknowledge);
        return std::error_code{};
    });

    auto req1 = standard_request(1, 1);
    acf::AcfMessageInfo   resp1;
    std::vector<uint8_t>  resp1_payload;
    std::error_code        ec1;
    std::thread blocked_caller([&] { ec1 = ch->request(0, req1, {}, resp1, resp1_payload); });

    {
        std::unique_lock<std::mutex> lk(mu);
        release_cv.wait(lk, [&] { return entered; });
    }

    // The one and only slot is occupied by blocked_caller's still-running
    // handler -- a second, concurrent caller must be rejected immediately
    // (REQ-SHMEM-006), not queued and not blocked waiting for room.
    auto req2 = standard_request(1, 2);
    acf::AcfMessageInfo   resp2;
    std::vector<uint8_t>  resp2_payload;
    REQUIRE(ch->request(1, req2, {}, resp2, resp2_payload) == ErrBusy);

    {
        std::lock_guard<std::mutex> lk(mu);
        may_release = true;
    }
    release_cv.notify_all();
    blocked_caller.join();

    REQUIRE_FALSE(ec1);
    REQUIRE(acf::response_kind_of(resp1) == acf::ResponseKind::Acknowledge);

    // The slot is free again -- a request that would previously have been
    // rejected now succeeds.
    acf::AcfMessageInfo   resp3;
    std::vector<uint8_t>  resp3_payload;
    REQUIRE_FALSE(ch->request(2, req2, {}, resp3, resp3_payload));
}

TEST_CASE("shmem Channel::request admits up to queue_capacity concurrent callers and rejects "
          "only the overflow",
          "[shmem][REQ-SHMEM-006]") {
    constexpr size_t kCapacity = 3;
    auto ch = new_channel(1, kCapacity);

    std::mutex              mu;
    std::condition_variable entered_cv, release_cv;
    size_t                   entered_count = 0;
    bool                     may_release   = false;

    ch->set_handler([&](size_t, const acf::AcfMessageInfo& req, const std::vector<uint8_t>&,
                         acf::AcfMessageInfo& out_resp, std::vector<uint8_t>&) {
        {
            std::lock_guard<std::mutex> lk(mu);
            ++entered_count;
        }
        entered_cv.notify_all();
        std::unique_lock<std::mutex> lk(mu);
        release_cv.wait(lk, [&] { return may_release; });
        out_resp = acf::make_response(req, acf::ResponseKind::Acknowledge);
        return std::error_code{};
    });

    std::vector<std::thread> threads;
    std::atomic<int>          ok_count{0};
    for (size_t i = 0; i < kCapacity; ++i) {
        threads.emplace_back([&, i] {
            auto req = standard_request(1, static_cast<uint8_t>(i));
            acf::AcfMessageInfo   resp;
            std::vector<uint8_t>  resp_payload;
            if (!ch->request(i, req, {}, resp, resp_payload)) ok_count.fetch_add(1);
        });
    }

    {
        std::unique_lock<std::mutex> lk(mu);
        entered_cv.wait(lk, [&] { return entered_count == kCapacity; });
    }

    // Every slot is now occupied -- one more concurrent caller must see
    // ErrBusy while all kCapacity handlers are still blocked.
    auto overflow_req = standard_request(1, 99);
    acf::AcfMessageInfo   overflow_resp;
    std::vector<uint8_t>  overflow_resp_payload;
    REQUIRE(ch->request(kCapacity, overflow_req, {}, overflow_resp, overflow_resp_payload) ==
            ErrBusy);

    {
        std::lock_guard<std::mutex> lk(mu);
        may_release = true;
    }
    release_cv.notify_all();
    for (auto& t : threads) t.join();

    REQUIRE(ok_count.load() == static_cast<int>(kCapacity));
}

// ── No cross-talk between concurrent in-flight requests (REQ-SHMEM-002/003
// adapted: c-RCP's two directions are independent and never observe each
// other's frames -- Channel::request() has one direction per call, so the
// analogous property is that one caller's own request/response bytes are
// never delivered to, or returned from, a different concurrent caller) ──

TEST_CASE("shmem Channel::request never delivers one concurrent caller's payload to another",
          "[shmem][REQ-SHMEM-002][REQ-SHMEM-003]") {
    auto ch = new_channel(1, /*queue_capacity=*/8);
    ch->set_handler([](size_t, const acf::AcfMessageInfo& req, const std::vector<uint8_t>& payload,
                        acf::AcfMessageInfo& out_resp, std::vector<uint8_t>& out_payload) {
        out_resp    = acf::make_response(req, acf::ResponseKind::ReadResponse);
        out_payload = payload;
        return std::error_code{};
    });

    constexpr int kThreads = 8;
    std::vector<std::thread> threads;
    std::atomic<int> mismatches{0};
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t] {
            for (int i = 0; i < 20; ++i) {
                auto req = standard_request(1, static_cast<uint8_t>(t));
                std::vector<uint8_t>  payload{static_cast<uint8_t>(t), static_cast<uint8_t>(i)};
                acf::AcfMessageInfo   resp;
                std::vector<uint8_t>  resp_payload;
                if (ch->request(static_cast<size_t>(t), req, payload, resp, resp_payload)) continue;
                if (resp_payload != payload) mismatches.fetch_add(1);
            }
        });
    }
    for (auto& th : threads) th.join();

    REQUIRE(mismatches.load() == 0);
}

// ── Concurrency (unchanged from before this pass) ───────────────────────────

TEST_CASE("shmem Channel::request is thread-safe for concurrent callers",
          "[shmem][REQ-SHMEM-006]") {
    auto ch = new_channel(1, /*queue_capacity=*/8);
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
          "[shmem][REQ-SHMEM-001]") {
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

TEST_CASE("shmem Channel::close and Registry::close are idempotent", "[shmem][registry]") {
    auto ch = new_channel(1);
    ch->close();
    ch->close(); // second call must not crash

    auto reg = new_registry();
    REQUIRE_FALSE(reg->add_channel(new_channel(2)));
    REQUIRE_FALSE(reg->close());
    REQUIRE_FALSE(reg->close()); // second call must not crash
}

// ── shared_ptr<Channel> ownership (REQ-SHMEM-008/009 adapted: c-RCP's own
// refcounted rcp_avtp_transport_t sides guarantee releasing one holder
// neither invalidates another holder's own use of the shared state
// (shmem.c:151-181) nor frees it more than once, regardless of release
// order -- std::shared_ptr<Channel> already gives cpp-RCP both properties
// automatically, see rcp/shmem.hpp's own Channel class comment) ──────────

TEST_CASE("shmem a Channel remains usable through one shared_ptr holder after another holder "
          "of the same Channel is released",
          "[shmem][REQ-SHMEM-008]") {
    auto reg = new_registry();
    auto ch  = new_channel(3);
    REQUIRE_FALSE(reg->add_channel(ch)); // Registry now holds its own shared_ptr copy too

    ch->set_handler([](size_t, const acf::AcfMessageInfo& req, const std::vector<uint8_t>&,
                        acf::AcfMessageInfo& out_resp, std::vector<uint8_t>&) {
        out_resp = acf::make_response(req, acf::ResponseKind::Acknowledge);
        return std::error_code{};
    });

    std::shared_ptr<Channel> from_registry;
    REQUIRE_FALSE(reg->lookup(3, from_registry));
    ch.reset(); // this test's own local holder released; Registry's copy remains

    auto req = standard_request(1, 1);
    acf::AcfMessageInfo   resp;
    std::vector<uint8_t>  resp_payload;
    REQUIRE_FALSE(from_registry->request(0, req, {}, resp, resp_payload));
    REQUIRE(acf::response_kind_of(resp) == acf::ResponseKind::Acknowledge);
}

TEST_CASE("shmem a Channel's state is freed exactly once regardless of shared_ptr release order",
          "[shmem][REQ-SHMEM-009]") {
    // No public way to observe the underlying allocation's lifetime
    // directly; this pins the externally-visible contract instead (the
    // same "no in-test assertion, verified by this suite's own sanitizer
    // run" approach c-RCP's own equivalent test documents, tests/
    // test_shmem.c's test_pair_state_freed_after_releasing_both_sides) --
    // constructing, copying, and releasing shared_ptr<Channel> handles in
    // either order must neither crash nor leak.
    std::weak_ptr<Channel> weak;
    {
        auto a = new_channel(5);
        weak    = a;
        auto b  = a; // second holder
        a.reset();
        REQUIRE_FALSE(weak.expired()); // b still holds it
        b.reset();
    }
    REQUIRE(weak.expired()); // freed once both holders released, either order
}

// ── Registry ──────────────────────────────────────────────────────────────────
// Registry itself has no c-RCP analog at all (rcp_shmem_avtp_pair_new()
// returns exactly one pair, with no keyed lookup of any kind) -- most of
// these stay untagged with a REQ-SHMEM-* id rather than force-fitting one.
// REQ-SHMEM-004 is the one exception: this project's own .fusa-reqs.json
// assigns that id specifically to the lookup-finds-a-registered-channel
// behavior below.

TEST_CASE("shmem Registry::lookup finds a channel registered via add_channel",
          "[shmem][registry][REQ-SHMEM-004]") {
    auto reg = new_registry();
    auto ch  = new_channel(7);
    REQUIRE_FALSE(reg->add_channel(ch));

    std::shared_ptr<Channel> out;
    REQUIRE_FALSE(reg->lookup(7, out));
    REQUIRE(out == ch);
}

TEST_CASE("shmem Registry::add_channel rejects a duplicate stream_key", "[shmem][registry]") {
    auto reg = new_registry();
    REQUIRE_FALSE(reg->add_channel(new_channel(7)));
    REQUIRE(reg->add_channel(new_channel(7)) == ErrAlreadyExists);
}

TEST_CASE("shmem Registry::deregister removes and closes a channel", "[shmem][registry]") {
    auto reg = new_registry();
    auto ch  = new_channel(7);
    REQUIRE_FALSE(reg->add_channel(ch));
    REQUIRE_FALSE(reg->deregister(7));

    std::shared_ptr<Channel> out;
    REQUIRE(reg->lookup(7, out) == ErrNotFound);
    REQUIRE_FALSE(ch->ok());
}

TEST_CASE("shmem Registry::close closes all registered channels", "[shmem][registry]") {
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

TEST_CASE("shmem Registry::channels enumerates every registered channel", "[shmem][registry]") {
    auto reg = new_registry();
    for (uint64_t k : {1, 2, 3}) REQUIRE_FALSE(reg->add_channel(new_channel(k)));
    REQUIRE(reg->channels().size() == 3);
}
