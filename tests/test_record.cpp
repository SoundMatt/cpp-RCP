// fusa:test REQ-REC-001
// fusa:test REQ-REC-002
// fusa:test REQ-REC-003
// fusa:test REQ-REC-004
// fusa:test REQ-REC-005
// fusa:test REQ-REC-006
// fusa:test REQ-REC-007
// fusa:test REQ-REC-008
#include <catch2/catch_test_macros.hpp>

#include "rcp/mock.hpp"
#include "rcp/record.hpp"

#include <filesystem>
#include <thread>
#include <vector>

using namespace rcp;

TEST_CASE("record: recording controller captures entries", "[record]") {
    auto rec   = std::make_shared<record::Record>();
    auto inner = std::make_shared<mock::Controller>(Zone::FrontLeft);
    auto ctrl  = record::new_controller(inner, rec);

    Command cmd;
    cmd.zone = Zone::FrontLeft;
    cmd.type = CommandType::Get;
    Response resp;
    REQUIRE_FALSE(ctrl->send(Context{}, cmd, resp));

    REQUIRE(rec->size() == 1);
    REQUIRE(rec->entries()[0].cmd.type == CommandType::Get);
    REQUIRE(!rec->entries()[0].error);
}

TEST_CASE("record: multiple sends produce sequential entries", "[record]") {
    auto rec   = std::make_shared<record::Record>();
    auto inner = std::make_shared<mock::Controller>(Zone::FrontLeft);
    auto ctrl  = record::new_controller(inner, rec);

    for (int i = 0; i < 3; ++i) {
        Command cmd;
        cmd.zone = Zone::FrontLeft;
        cmd.type = CommandType::Set;
        Response resp;
        auto ec = ctrl->send(Context{}, cmd, resp);
        (void)ec;
    }

    REQUIRE(rec->size() == 3);
}

TEST_CASE("record: write_binary creates file", "[record]") {
    auto rec   = std::make_shared<record::Record>();
    auto inner = std::make_shared<mock::Controller>(Zone::FrontLeft);
    auto ctrl  = record::new_controller(inner, rec);

    Command cmd;
    cmd.zone = Zone::FrontLeft;
    cmd.type = CommandType::Get;
    Response resp;
    auto ec = ctrl->send(Context{}, cmd, resp);
    (void)ec;

    auto path = (std::filesystem::temp_directory_path() / "rcp_test_record.bin").string();
    REQUIRE_FALSE(rec->write_binary(path));

    // Verify file is non-empty
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    REQUIRE(f.is_open());
    REQUIRE(f.tellg() > 0);
}

TEST_CASE("record: entry timestamps are monotonically non-decreasing", "[record][REQ-REC-006]") {
    auto rec   = std::make_shared<record::Record>();
    auto inner = std::make_shared<mock::Controller>(Zone::FrontLeft);
    auto ctrl  = record::new_controller(inner, rec);

    for (int i = 0; i < 20; ++i) {
        Command cmd; cmd.zone = Zone::FrontLeft; cmd.type = CommandType::Set;
        Response resp;
        auto ec = ctrl->send(Context{}, cmd, resp);
        (void)ec;
    }

    const auto& es = rec->entries();
    REQUIRE(es.size() == 20);
    for (size_t i = 1; i < es.size(); ++i) {
        REQUIRE(es[i].timestamp_ns >= es[i - 1].timestamp_ns);
    }
}

TEST_CASE("record: forwards inner send result unchanged", "[record][REQ-REC-007]") {
    auto rec   = std::make_shared<record::Record>();
    auto inner = std::make_shared<mock::Controller>(Zone::FrontLeft);
    REQUIRE_FALSE(inner->close()); // closed inner returns ErrClosed
    auto ctrl  = record::new_controller(inner, rec);

    Command cmd; cmd.zone = Zone::FrontLeft; cmd.type = CommandType::Get;
    Response resp;
    auto ec = ctrl->send(Context{}, cmd, resp);
    REQUIRE(ec == ErrClosed);            // result passed through verbatim
    REQUIRE(rec->size() == 1);
    REQUIRE(rec->entries()[0].error == ErrClosed); // and captured in the log
}

TEST_CASE("record: Record tolerates concurrent appends", "[record][REQ-REC-008]") {
    auto rec   = std::make_shared<record::Record>();
    auto inner = std::make_shared<mock::Controller>(Zone::Central);
    auto ctrl  = record::new_controller(inner, rec);

    constexpr int kThreads = 8;
    constexpr int kPerThread = 500;
    std::vector<std::thread> ts;
    for (int t = 0; t < kThreads; ++t) {
        ts.emplace_back([&] {
            for (int i = 0; i < kPerThread; ++i) {
                Command cmd; cmd.zone = Zone::Central; cmd.type = CommandType::Get;
                Response resp;
                auto ec = ctrl->send(Context{}, cmd, resp);
                (void)ec;
            }
        });
    }
    for (auto& th : ts) th.join();
    REQUIRE(rec->size() == kThreads * kPerThread);
}

TEST_CASE("record: playback replays entries against target", "[record][REQ-REC-004][REQ-REC-005]") {
    auto rec   = std::make_shared<record::Record>();
    auto inner = std::make_shared<mock::Controller>(Zone::FrontLeft);
    auto ctrl  = record::new_controller(inner, rec);

    Command cmd;
    cmd.zone = Zone::FrontLeft;
    cmd.type = CommandType::Get;
    Response resp;
    auto ec = ctrl->send(Context{}, cmd, resp);
    (void)ec;

    REQUIRE(rec->size() == 1);

    auto target = std::make_shared<mock::Controller>(Zone::FrontLeft);
    record::Playback pb(target, *rec, {0.0}); // no delays
    REQUIRE_FALSE(pb.run_all(Context{}));
}
