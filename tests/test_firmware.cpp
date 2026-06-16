// fusa:req REQ-FW-001
// fusa:req REQ-FW-002
// fusa:req REQ-FW-003
// fusa:req REQ-FW-004
// fusa:req REQ-FW-005
#include <catch2/catch_test_macros.hpp>

#include "rcp/firmware.hpp"
#include "rcp/mock.hpp"

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

TEST_CASE("firmware: rollback resets to Idle", "[firmware]") {
    auto ctrl = std::make_shared<mock::Controller>(Zone::FrontLeft);
    firmware::FirmwareSession session(ctrl);
    REQUIRE_FALSE(session.initiate(Context{}, "1.0"));
    REQUIRE_FALSE(session.transfer(Context{}, std::vector<uint8_t>(1, 0)));
    REQUIRE_FALSE(session.verify(Context{}));
    REQUIRE_FALSE(session.activate(Context{}));
    REQUIRE_FALSE(session.rollback(Context{}));
    REQUIRE(session.state() == firmware::SessionState::Idle);
}
