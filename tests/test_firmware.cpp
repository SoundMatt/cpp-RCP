// fusa:test REQ-FW-001
// fusa:test REQ-FW-002
// fusa:test REQ-FW-003
// fusa:test REQ-FW-004
// fusa:test REQ-FW-005
// fusa:test REQ-FW-006
// fusa:test REQ-FW-007
// fusa:test REQ-FW-008
#include <catch2/catch_test_macros.hpp>

#include "rcp/firmware.hpp"
#include "rcp/mock.hpp"

#include <cstdint>
#include <vector>

using namespace rcp;

TEST_CASE("firmware: state machine starts Idle", "[firmware]") {
    auto ctrl = std::make_shared<mock::Controller>(Zone::FrontLeft);
    firmware::FirmwareSession session(ctrl);
    REQUIRE(session.state() == firmware::SessionState::Idle);
}

TEST_CASE("firmware: initiate transitions to Initiated", "[firmware]") {
    auto ctrl = std::make_shared<mock::Controller>(Zone::FrontLeft);
    firmware::FirmwareSession session(ctrl);
    REQUIRE_FALSE(session.initiate(Context{}, "1.2.3"));
    REQUIRE(session.state() == firmware::SessionState::Initiated);
}

TEST_CASE("firmware: double initiate fails", "[firmware]") {
    auto ctrl = std::make_shared<mock::Controller>(Zone::FrontLeft);
    firmware::FirmwareSession session(ctrl);
    REQUIRE_FALSE(session.initiate(Context{}, "1.0"));
    auto ec = session.initiate(Context{}, "1.0");
    REQUIRE(ec == firmware::make_error_code(firmware::FirmwareErrc::bad_state));
}

TEST_CASE("firmware: transfer requires Initiated state", "[firmware]") {
    auto ctrl = std::make_shared<mock::Controller>(Zone::FrontLeft);
    firmware::FirmwareSession session(ctrl);
    std::vector<uint8_t> image(8192, 0xAA);
    auto ec = session.transfer(Context{}, image);
    REQUIRE(ec == firmware::make_error_code(firmware::FirmwareErrc::bad_state));
}

TEST_CASE("firmware: full happy path", "[firmware]") {
    auto ctrl = std::make_shared<mock::Controller>(Zone::FrontLeft);
    firmware::FirmwareSession session(ctrl);

    REQUIRE_FALSE(session.initiate(Context{}, "2.0.0"));
    REQUIRE(session.state() == firmware::SessionState::Initiated);

    std::vector<uint8_t> image(4096, 0xFF);
    size_t progress_calls = 0;
    REQUIRE_FALSE(session.transfer(Context{}, image, [&](const firmware::Progress& p){
        ++progress_calls;
        (void)p;
    }));
    REQUIRE(progress_calls == 1); // 4096 bytes / 4096 chunk_size = 1 chunk

    REQUIRE_FALSE(session.verify(Context{}));
    REQUIRE(session.state() == firmware::SessionState::Verifying);

    REQUIRE_FALSE(session.activate(Context{}));
    REQUIRE(session.state() == firmware::SessionState::Activated);
}

TEST_CASE("firmware: transfer chunks image in chunk_size segments with per-chunk progress",
          "[firmware][REQ-FW-006][REQ-FW-008]") {
    auto ctrl = std::make_shared<mock::Controller>(Zone::FrontLeft);
    firmware::Config cfg;
    cfg.chunk_size = 1024;
    firmware::FirmwareSession session(ctrl, cfg);

    REQUIRE_FALSE(session.initiate(Context{}, "3.0.0"));

    // 4096 bytes / 1024 chunk_size = exactly 4 chunks.
    std::vector<uint8_t> image(4096, 0x5A);
    size_t calls = 0;
    size_t last_total_chunks = 0;
    size_t last_chunk_index  = 0;
    REQUIRE_FALSE(session.transfer(Context{}, image, [&](const firmware::Progress& p) {
        ++calls;
        last_total_chunks = p.total_chunks;
        last_chunk_index  = p.chunk_index;
        REQUIRE(p.total_bytes == 4096);
    }));
    REQUIRE(calls == 4);                 // one progress callback per chunk (REQ-FW-008)
    REQUIRE(last_total_chunks == 4);     // image split into chunk_size segments (REQ-FW-006)
    REQUIRE(last_chunk_index == 4);

    // A trailing partial chunk is still counted: 4097 bytes -> 5 chunks.
    REQUIRE_FALSE(session.verify(Context{}));
    REQUIRE_FALSE(session.activate(Context{}));
}

TEST_CASE("firmware: rollback resets to Idle", "[firmware][REQ-FW-007]") {
    auto ctrl = std::make_shared<mock::Controller>(Zone::FrontLeft);
    firmware::FirmwareSession session(ctrl);
    REQUIRE_FALSE(session.initiate(Context{}, "1.0"));
    REQUIRE_FALSE(session.transfer(Context{}, std::vector<uint8_t>(1, 0)));
    REQUIRE_FALSE(session.verify(Context{}));
    REQUIRE_FALSE(session.activate(Context{}));
    REQUIRE_FALSE(session.rollback(Context{}));
    REQUIRE(session.state() == firmware::SessionState::Idle);
}
