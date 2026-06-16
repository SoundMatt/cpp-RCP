// fusa:test REQ-FI-001
// fusa:test REQ-FI-002
// fusa:test REQ-FI-003
// fusa:test REQ-FI-004
// fusa:test REQ-FI-005
// fusa:test REQ-FI-006
// fusa:test REQ-FI-007
// fusa:test REQ-FI-008

#include <catch2/catch_test_macros.hpp>
#include <rcp/mock.hpp>
#include <rcp/faultinject.hpp>

using namespace rcp;

static std::shared_ptr<mock::Controller> make_mock(Zone z = Zone::FrontLeft) {
    return std::make_shared<mock::Controller>(z);
}

// ── No rules ─────────────────────────────────────────────────────────────────

TEST_CASE("FaultInject::send passes through when no rules active", "[faultinject][REQ-FI-001]") {
    auto inner = make_mock();
    faultinject::Controller fi(inner);

    Command cmd;
    cmd.zone = Zone::FrontLeft;
    Response resp;
    REQUIRE_FALSE(fi.send(Context::background(), cmd, resp));
    REQUIRE(resp.status == ResponseStatus::OK);
}

// ── Drop ──────────────────────────────────────────────────────────────────────

TEST_CASE("FaultInject Drop rule causes send to return an error", "[faultinject][REQ-FI-002]") {
    auto inner = make_mock();
    faultinject::Controller fi(inner);

    faultinject::Rule r;
    r.type  = faultinject::FaultType::Drop;
    r.count = -1; // infinite
    fi.add_rule(r);

    Command cmd;
    cmd.zone = Zone::FrontLeft;
    Response resp;
    REQUIRE(fi.send(Context::background(), cmd, resp));
}

// ── Error ─────────────────────────────────────────────────────────────────────

TEST_CASE("FaultInject Error rule returns ResponseStatus::Error", "[faultinject][REQ-FI-003]") {
    auto inner = make_mock();
    faultinject::Controller fi(inner);

    faultinject::Rule r;
    r.type  = faultinject::FaultType::Error;
    r.count = -1;
    fi.add_rule(r);

    Command cmd;
    cmd.zone = Zone::FrontLeft;
    Response resp;
    auto ec_e = fi.send(Context::background(), cmd, resp);
    (void)ec_e;
    REQUIRE(resp.status == ResponseStatus::Error);
}

// ── Slow ──────────────────────────────────────────────────────────────────────

TEST_CASE("FaultInject Slow rule adds latency", "[faultinject][REQ-FI-004]") {
    auto inner = make_mock();
    faultinject::Controller fi(inner);

    faultinject::Rule r;
    r.type    = faultinject::FaultType::Slow;
    r.latency = std::chrono::milliseconds(20);
    r.count   = 1;
    fi.add_rule(r);

    Command cmd;
    cmd.zone = Zone::FrontLeft;
    Response resp;

    auto start   = std::chrono::steady_clock::now();
    auto ec_slow = fi.send(Context::background(), cmd, resp);
    (void)ec_slow;
    auto elapsed = std::chrono::steady_clock::now() - start;
    REQUIRE(elapsed >= std::chrono::milliseconds(20));
}

// ── Count-based expiry ────────────────────────────────────────────────────────

TEST_CASE("FaultInject rule expires after count sends", "[faultinject][REQ-FI-005]") {
    auto inner = make_mock();
    faultinject::Controller fi(inner);

    faultinject::Rule r;
    r.type  = faultinject::FaultType::Drop;
    r.count = 2; // fires twice then expires
    fi.add_rule(r);

    Command cmd;
    cmd.zone = Zone::FrontLeft;
    Response resp;

    { auto ec = fi.send(Context::background(), cmd, resp); REQUIRE(ec); } // drop 1
    { auto ec = fi.send(Context::background(), cmd, resp); REQUIRE(ec); } // drop 2
    REQUIRE_FALSE(fi.send(Context::background(), cmd, resp)); // passes
}

// ── clear_rules ───────────────────────────────────────────────────────────────

TEST_CASE("FaultInject::clear_rules removes all active rules", "[faultinject][REQ-FI-006]") {
    auto inner = make_mock();
    faultinject::Controller fi(inner);

    faultinject::Rule r;
    r.type  = faultinject::FaultType::Drop;
    r.count = -1;
    fi.add_rule(r);
    fi.clear_rules(); // NOLINT(bugprone-unused-return-value) — void return

    Command cmd;
    cmd.zone = Zone::FrontLeft;
    Response resp;
    REQUIRE_FALSE(fi.send(Context::background(), cmd, resp));
}

// ── Timeout ───────────────────────────────────────────────────────────────────

TEST_CASE("FaultInject Timeout rule returns ErrTimeout", "[faultinject][REQ-FI-007]") {
    auto inner = make_mock();
    faultinject::Controller fi(inner);

    faultinject::Rule r;
    r.type  = faultinject::FaultType::Timeout;
    r.count = -1;
    fi.add_rule(r);

    Command cmd;
    cmd.zone = Zone::FrontLeft;
    Response resp;
    auto ec = fi.send(Context::background(), cmd, resp);
    REQUIRE(ec == ErrTimeout);
}

// ── Zone passthrough ──────────────────────────────────────────────────────────

TEST_CASE("FaultInject::zone returns inner zone", "[faultinject][REQ-FI-008]") {
    auto inner = make_mock(Zone::Central);
    faultinject::Controller fi(inner);
    REQUIRE(fi.zone() == Zone::Central);
}
