// fusa:test REQ-RL-001
// fusa:test REQ-RL-002
// fusa:test REQ-RL-003
// fusa:test REQ-RL-004
// fusa:test REQ-RL-005
// fusa:test REQ-RL-006
// fusa:test REQ-RL-007
// fusa:test REQ-RL-008

#include <catch2/catch_test_macros.hpp>
#include <rcp/mock.hpp>
#include <rcp/ratelimit.hpp>

using namespace rcp;

static std::shared_ptr<mock::Controller> make_mock(Zone z = Zone::FrontLeft) {
    return std::make_shared<mock::Controller>(z);
}

// ── Basic send ────────────────────────────────────────────────────────────────

TEST_CASE("RateLimit::send forwards command when bucket has tokens", "[ratelimit][REQ-RL-001]") {
    auto inner = make_mock();
    ratelimit::Config cfg;
    cfg.rate  = 1000;
    cfg.burst = 10;
    ratelimit::Controller rl(inner, cfg);

    Command cmd;
    cmd.zone = Zone::FrontLeft;
    Response resp;
    REQUIRE_FALSE(rl.send(Context::background(), cmd, resp));
    REQUIRE(resp.status == ResponseStatus::OK);
}

TEST_CASE("RateLimit::zone returns inner zone", "[ratelimit][REQ-RL-002]") {
    auto inner = make_mock(Zone::Central);
    ratelimit::Controller rl(inner);
    REQUIRE(rl.zone() == Zone::Central);
}

// ── Token exhaustion ──────────────────────────────────────────────────────────

TEST_CASE("RateLimit::send returns ErrBusy when bucket exhausted", "[ratelimit][REQ-RL-003]") {
    auto inner = make_mock();
    ratelimit::Config cfg;
    cfg.rate  = 0.001; // nearly zero refill
    cfg.burst = 2;
    cfg.exempt_critical = false;
    ratelimit::Controller rl(inner, cfg);

    Command cmd;
    cmd.zone     = Zone::FrontLeft;
    cmd.priority = Priority::Normal;
    Response resp;

    // Drain the 2 burst tokens.
    { auto ec = rl.send(Context::background(), cmd, resp); (void)ec; }
    { auto ec = rl.send(Context::background(), cmd, resp); (void)ec; }

    // Next send should hit ErrBusy.
    auto ec = rl.send(Context::background(), cmd, resp);
    REQUIRE(ec == ErrBusy);
}

// ── Critical exemption ────────────────────────────────────────────────────────

TEST_CASE("Critical commands bypass the bucket when exempt_critical=true", "[ratelimit][REQ-RL-004]") {
    auto inner = make_mock();
    ratelimit::Config cfg;
    cfg.rate            = 0.001;
    cfg.burst           = 1;
    cfg.exempt_critical = true;
    ratelimit::Controller rl(inner, cfg);

    Command cmd;
    cmd.zone     = Zone::FrontLeft;
    cmd.priority = Priority::Normal;
    Response resp;

    // Exhaust the 1 token.
    { auto ec = rl.send(Context::background(), cmd, resp); (void)ec; }

    // Critical should still pass.
    cmd.priority = Priority::Critical;
    auto ec = rl.send(Context::background(), cmd, resp);
    REQUIRE_FALSE(ec);
}

TEST_CASE("Critical commands do NOT bypass bucket when exempt_critical=false", "[ratelimit][REQ-RL-005]") {
    auto inner = make_mock();
    ratelimit::Config cfg;
    cfg.rate            = 0.001;
    cfg.burst           = 1;
    cfg.exempt_critical = false;
    ratelimit::Controller rl(inner, cfg);

    Command cmd;
    cmd.zone     = Zone::FrontLeft;
    cmd.priority = Priority::Normal;
    Response resp;
    { auto ec = rl.send(Context::background(), cmd, resp); (void)ec; }

    cmd.priority = Priority::Critical;
    auto ec = rl.send(Context::background(), cmd, resp);
    REQUIRE(ec == ErrBusy);
}

// ── Zone mismatch / closed ────────────────────────────────────────────────────

TEST_CASE("RateLimit::send returns ErrZoneMismatch on wrong zone", "[ratelimit][REQ-RL-006]") {
    auto inner = make_mock(Zone::FrontLeft);
    ratelimit::Controller rl(inner);
    Command cmd;
    cmd.zone = Zone::RearLeft;
    Response resp;
    REQUIRE(rl.send(Context::background(), cmd, resp) == ErrZoneMismatch);
}

TEST_CASE("RateLimit::close stops sends", "[ratelimit][REQ-RL-007][REQ-RL-008]") {
    auto inner = make_mock();
    ratelimit::Controller rl(inner);
    REQUIRE_FALSE(rl.close());

    Command cmd;
    cmd.zone = Zone::FrontLeft;
    Response resp;
    REQUIRE(rl.send(Context::background(), cmd, resp) == ErrClosed);
}
