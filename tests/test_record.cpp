// fusa:test REQ-REC-001
// fusa:test REQ-REC-002
// fusa:test REQ-REC-003
// fusa:test REQ-REC-004
// fusa:test REQ-REC-005
// fusa:test REQ-REC-006
// fusa:test REQ-REC-007
// fusa:test REQ-REC-008

// Tests for rcp/record.hpp — binary record/replay of RC-Client-level
// request/response traffic (ROADMAP.md milestone 58, "Auxiliary Transport
// & Cross-Cutting Rebind", v2.14.0).

#include <catch2/catch_test_macros.hpp>

#include "rcp/record.hpp"

#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

using namespace rcp;
using namespace rcp::record;

namespace {
acf::AcfMessageInfo standard_request(avtp::ByteBusId bus_id, uint8_t transaction_num) {
    return acf::make_standard_request(bus_id, transaction_num, /*write=*/false, /*read_size=*/2);
}

// echo_request is a stand-in "send-equivalent call" (RequestFn) that always
// answers a ReadResponse echoing a fixed payload.
RequestFn echo_request(std::vector<uint8_t> payload = {0xAB}, std::error_code result = {}) {
    return [payload, result](const Context&, const acf::AcfMessageInfo& req,
                              const std::vector<uint8_t>&,
                              acf::AcfMessageInfo& out_resp, std::vector<uint8_t>& out_payload) {
        out_resp    = acf::make_response(req, acf::ResponseKind::ReadResponse);
        out_payload = payload;
        return result;
    };
}
} // namespace

TEST_CASE("record: RecordingClient captures request and response for every call",
          "[record][REQ-REC-001]") {
    auto rec  = std::make_shared<Record>();
    auto rc   = new_recording_client(echo_request({0xAB}), rec);

    auto req = standard_request(1, 5);
    acf::AcfMessageInfo   resp;
    std::vector<uint8_t>  resp_payload;
    REQUIRE_FALSE(rc->request(Context{}, req, {}, resp, resp_payload));

    REQUIRE(rec->size() == 1);
    auto entries = rec->entries();
    REQUIRE(entries[0].request.byte_bus_id == 1);
    REQUIRE(entries[0].request.transaction_num == 5);
    REQUIRE(entries[0].response_payload == std::vector<uint8_t>{0xAB});
    REQUIRE_FALSE(entries[0].error);
}

TEST_CASE("record: Record::size reflects the number of completed request() calls",
          "[record][REQ-REC-002]") {
    auto rec = std::make_shared<Record>();
    auto rc  = new_recording_client(echo_request(), rec);

    for (int i = 0; i < 3; ++i) {
        auto req = standard_request(1, static_cast<uint8_t>(i));
        acf::AcfMessageInfo   resp;
        std::vector<uint8_t>  resp_payload;
        auto ec = rc->request(Context{}, req, {}, resp, resp_payload);
        (void)ec;
    }

    REQUIRE(rec->size() == 3);
}

TEST_CASE("record: write_binary/read_binary round-trips entries losslessly",
          "[record][REQ-REC-003]") {
    auto rec = std::make_shared<Record>();
    auto rc  = new_recording_client(echo_request({0x11, 0x22, 0x33}), rec);

    auto req = standard_request(3, 9);
    acf::AcfMessageInfo   resp;
    std::vector<uint8_t>  resp_payload;
    auto ec = rc->request(Context{}, req, std::vector<uint8_t>{0x01, 0x02}, resp, resp_payload);
    (void)ec;

    auto path = (std::filesystem::temp_directory_path() / "rcp_test_record.bin").string();
    REQUIRE_FALSE(rec->write_binary(path));

    std::ifstream f(path, std::ios::binary | std::ios::ate);
    REQUIRE(f.is_open());
    REQUIRE(f.tellg() > 0);
    f.close();

    Record roundtrip;
    REQUIRE_FALSE(roundtrip.read_binary(path));
    REQUIRE(roundtrip.size() == rec->size());

    auto original = rec->entries();
    auto reread   = roundtrip.entries();
    REQUIRE(reread[0].timestamp_ns == original[0].timestamp_ns);
    REQUIRE(reread[0].request.byte_bus_id == 3);
    REQUIRE(reread[0].request.transaction_num == 9);
    REQUIRE(reread[0].request_payload == std::vector<uint8_t>{0x01, 0x02});
    REQUIRE(reread[0].response_payload == std::vector<uint8_t>{0x11, 0x22, 0x33});

    std::filesystem::remove(path);
}

TEST_CASE("record: entry timestamps are monotonically non-decreasing", "[record][REQ-REC-006]") {
    auto rec = std::make_shared<Record>();
    auto rc  = new_recording_client(echo_request(), rec);

    for (int i = 0; i < 20; ++i) {
        auto req = standard_request(1, static_cast<uint8_t>(i));
        acf::AcfMessageInfo   resp;
        std::vector<uint8_t>  resp_payload;
        auto ec = rc->request(Context{}, req, {}, resp, resp_payload);
        (void)ec;
    }

    auto entries = rec->entries();
    REQUIRE(entries.size() == 20);
    for (size_t i = 1; i < entries.size(); ++i) {
        REQUIRE(entries[i].timestamp_ns >= entries[i - 1].timestamp_ns);
    }
}

TEST_CASE("record: RecordingClient::request forwards the inner error_code unchanged",
          "[record][REQ-REC-007]") {
    auto rec = std::make_shared<Record>();
    auto rc  = new_recording_client(echo_request({}, ErrClosed), rec);

    auto req = standard_request(1, 1);
    acf::AcfMessageInfo   resp;
    std::vector<uint8_t>  resp_payload;
    auto ec = rc->request(Context{}, req, {}, resp, resp_payload);
    REQUIRE(ec == ErrClosed);            // result passed through verbatim
    REQUIRE(rec->size() == 1);
    REQUIRE(rec->entries()[0].error == ErrClosed); // and captured in the log
}

TEST_CASE("record: Record::append tolerates concurrent callers", "[record][REQ-REC-008]") {
    auto rec = std::make_shared<Record>();
    auto rc  = new_recording_client(echo_request(), rec);

    constexpr int kThreads = 8;
    constexpr int kPerThread = 500;
    std::vector<std::thread> ts;
    for (int t = 0; t < kThreads; ++t) {
        ts.emplace_back([&] {
            for (int i = 0; i < kPerThread; ++i) {
                auto req = standard_request(1, 1);
                acf::AcfMessageInfo   resp;
                std::vector<uint8_t>  resp_payload;
                auto ec = rc->request(Context{}, req, {}, resp, resp_payload);
                (void)ec;
            }
        });
    }
    for (auto& th : ts) th.join();
    REQUIRE(rec->size() == kThreads * kPerThread);
}

TEST_CASE("record: Playback::run_all replays every entry against the target",
          "[record][REQ-REC-004][REQ-REC-005]") {
    auto rec = std::make_shared<Record>();
    auto rc  = new_recording_client(echo_request(), rec);

    for (int i = 0; i < 3; ++i) {
        auto req = standard_request(1, static_cast<uint8_t>(i));
        acf::AcfMessageInfo   resp;
        std::vector<uint8_t>  resp_payload;
        auto ec = rc->request(Context{}, req, {}, resp, resp_payload);
        (void)ec;
    }
    REQUIRE(rec->size() == 3);

    int replayed = 0;
    RequestFn target = [&](const Context&, const acf::AcfMessageInfo& req,
                            const std::vector<uint8_t>&, acf::AcfMessageInfo& out_resp,
                            std::vector<uint8_t>&) {
        ++replayed;
        out_resp = acf::make_response(req, acf::ResponseKind::Acknowledge);
        return std::error_code{};
    };

    Playback pb(target, *rec, PlaybackConfig{/*speed_factor=*/0.0}); // no delays
    REQUIRE_FALSE(pb.run_all(Context{}));
    REQUIRE(replayed == 3);
}
