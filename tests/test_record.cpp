// fusa:req REQ-REC-001
// fusa:req REQ-REC-002
// fusa:req REQ-REC-003
#include <catch2/catch_test_macros.hpp>

#include "rcp/mock.hpp"
#include "rcp/record.hpp"

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

    auto path = "/tmp/rcp_test_record.bin";
    REQUIRE_FALSE(rec->write_binary(path));

    // Verify file is non-empty
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    REQUIRE(f.is_open());
    REQUIRE(f.tellg() > 0);
}

TEST_CASE("record: playback replays entries against target", "[record]") {
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
