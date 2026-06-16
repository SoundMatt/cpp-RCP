// fusa:test REQ-SHMEM-001
// fusa:test REQ-SHMEM-002
// fusa:test REQ-SHMEM-003
// fusa:test REQ-SHMEM-004
// fusa:test REQ-SHMEM-005
// fusa:test REQ-SHMEM-006
// fusa:test REQ-SHMEM-007
// fusa:test REQ-SHMEM-008

#include <catch2/catch_test_macros.hpp>
#include <rcp/shmem.hpp>

using namespace rcp;

// ── ZoneServer + Controller ───────────────────────────────────────────────────

TEST_CASE("shmem Controller::send returns OK via default handler", "[shmem][REQ-SHMEM-001]") {
    auto server = std::make_shared<shmem::ZoneServer>(Zone::FrontLeft);
    shmem::Controller ctrl(server);

    Command cmd;
    cmd.zone = Zone::FrontLeft;
    Response resp;
    REQUIRE_FALSE(ctrl.send(Context::background(), cmd, resp));
    REQUIRE(resp.status == ResponseStatus::OK);
}

TEST_CASE("shmem Controller dispatches to custom handler", "[shmem][REQ-SHMEM-002]") {
    auto server = std::make_shared<shmem::ZoneServer>(Zone::FrontLeft);
    server->set_handler([](const Command& c) {
        Response r;
        r.command_id = c.id;
        r.zone       = c.zone;
        r.status     = ResponseStatus::OK;
        r.payload    = {0xAB};
        return r;
    });

    shmem::Controller ctrl(server);
    Command cmd;
    cmd.id   = 7;
    cmd.zone = Zone::FrontLeft;
    Response resp;
    REQUIRE_FALSE(ctrl.send(Context::background(), cmd, resp));
    REQUIRE(resp.payload == std::vector<uint8_t>{0xAB});
}

TEST_CASE("shmem Controller::send returns ErrZoneMismatch for wrong zone", "[shmem][REQ-SHMEM-003]") {
    auto server = std::make_shared<shmem::ZoneServer>(Zone::FrontLeft);
    shmem::Controller ctrl(server);

    Command cmd;
    cmd.zone = Zone::RearLeft;
    Response resp;
    REQUIRE(ctrl.send(Context::background(), cmd, resp) == ErrZoneMismatch);
}

TEST_CASE("shmem Controller::send returns ErrClosed after close", "[shmem][REQ-SHMEM-004]") {
    auto server = std::make_shared<shmem::ZoneServer>(Zone::FrontLeft);
    shmem::Controller ctrl(server);
    { auto ec = ctrl.close(); (void)ec; }

    Command cmd;
    cmd.zone = Zone::FrontLeft;
    Response resp;
    REQUIRE(ctrl.send(Context::background(), cmd, resp) == ErrClosed);
}

// ── Publish / subscribe ───────────────────────────────────────────────────────

TEST_CASE("shmem ZoneServer::publish delivers status to subscribers", "[shmem][REQ-SHMEM-005]") {
    auto server = std::make_shared<shmem::ZoneServer>(Zone::FrontLeft);
    server->set_healthy(true);
    shmem::Controller ctrl(server);

    std::shared_ptr<StatusChannel> ch;
    REQUIRE_FALSE(ctrl.subscribe(Context::background(), ch));
    REQUIRE(ch != nullptr);

    server->publish({0x01, 0x02});
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    auto st = ch->try_recv();
    REQUIRE(st.has_value());
    REQUIRE(st->zone    == Zone::FrontLeft);
    REQUIRE(st->healthy == true);
    REQUIRE(st->payload == std::vector<uint8_t>{0x01, 0x02});

    ch->close();
}

// ── Payload copy isolation ────────────────────────────────────────────────────

TEST_CASE("shmem controller copies payload (no aliasing)", "[shmem][REQ-SHMEM-006]") {
    auto server = std::make_shared<shmem::ZoneServer>(Zone::FrontLeft);
    std::vector<uint8_t> received_payload;
    server->set_handler([&](const Command& c) {
        received_payload = c.payload;
        return Response{c.id, c.zone, ResponseStatus::OK, {}};
    });

    shmem::Controller ctrl(server);
    std::vector<uint8_t> original{1, 2, 3};
    Command cmd;
    cmd.zone    = Zone::FrontLeft;
    cmd.payload = original;

    Response resp;
    { auto ec = ctrl.send(Context::background(), cmd, resp); (void)ec; }

    // Mutate original after send — should not affect what the handler saw.
    original[0] = 0xFF;
    REQUIRE(received_payload[0] == 1);
}

// ── Registry ──────────────────────────────────────────────────────────────────

TEST_CASE("shmem Registry::lookup finds registered server", "[shmem][REQ-SHMEM-007]") {
    auto reg    = shmem::new_registry();
    auto server = std::make_shared<shmem::ZoneServer>(Zone::FrontLeft);
    REQUIRE_FALSE(reg->add_server(server));

    std::shared_ptr<rcp::Controller> ctrl_out;
    REQUIRE_FALSE(reg->lookup(Zone::FrontLeft, ctrl_out));
    REQUIRE(ctrl_out != nullptr);
}

TEST_CASE("shmem Registry::close releases all controllers", "[shmem][REQ-SHMEM-008]") {
    auto reg    = shmem::new_registry();
    auto server = std::make_shared<shmem::ZoneServer>(Zone::FrontLeft);
    { auto ec = reg->add_server(server); (void)ec; }
    REQUIRE_FALSE(reg->close());

    std::shared_ptr<rcp::Controller> ctrl_out;
    REQUIRE(reg->lookup(Zone::FrontLeft, ctrl_out) == ErrClosed);
}
