// fusa:test REQ-ZONE-001
// fusa:test REQ-ZONE-002
// fusa:test REQ-ZONE-003
// fusa:test REQ-ZONE-004
// fusa:test REQ-ZONE-005
// fusa:test REQ-ZONE-006
// fusa:test REQ-ZONE-007
// fusa:test REQ-ZONE-008
// fusa:test REQ-PRI-001
// fusa:test REQ-PRI-002
// fusa:test REQ-PRI-003
// fusa:test REQ-CMD-001
// fusa:test REQ-CMD-002
// fusa:test REQ-CMD-003
// fusa:test REQ-CMD-004
// fusa:test REQ-CMD-005
// fusa:test REQ-CMD-006
// fusa:test REQ-STATUS-001
// fusa:test REQ-STATUS-002
// fusa:test REQ-STATUS-003
// fusa:test REQ-STATUS-004
// fusa:test REQ-STATUS-005
// fusa:test REQ-STATUS-006
// fusa:test REQ-ERR-001
// fusa:test REQ-ERR-002
// fusa:test REQ-ERR-003
// fusa:test REQ-ERR-004
// fusa:test REQ-ERR-005
// fusa:test REQ-ERR-006
// fusa:test REQ-ERR-007
// fusa:test REQ-ERR-008
// fusa:test REQ-ERR-009
// fusa:test REQ-ERR-010
// fusa:test REQ-ERR-011
// fusa:test REQ-CMDSTRUCT-001
// fusa:test REQ-CMDSTRUCT-002
// fusa:test REQ-RESP-003
// fusa:test REQ-STAT-005

#include <catch2/catch_test_macros.hpp>
#include <rcp/rcp.hpp>
#include <set>
#include <string>
#include <thread>

using namespace rcp;

// ── Zone constants ────────────────────────────────────────────────────────────

TEST_CASE("Zone::to_string returns unique non-empty name", "[zone][REQ-ZONE-001]") {
    const std::vector<Zone> zones{
        Zone::FrontLeft, Zone::FrontRight,
        Zone::RearLeft,  Zone::RearRight,
        Zone::Central,
    };
    std::set<std::string> seen;
    for (auto z : zones) {
        auto s = to_string(z);
        REQUIRE_FALSE(s.empty());
        REQUIRE(seen.find(s) == seen.end());
        seen.insert(s);
    }
}

TEST_CASE("Zone constants have correct numeric values", "[zone][REQ-ZONE-002][REQ-ZONE-003][REQ-ZONE-004][REQ-ZONE-005][REQ-ZONE-006][REQ-ZONE-007]") {
    REQUIRE(static_cast<uint8_t>(Zone::Unknown)    == 0);
    REQUIRE(static_cast<uint8_t>(Zone::FrontLeft)  == 1);
    REQUIRE(static_cast<uint8_t>(Zone::FrontRight) == 2);
    REQUIRE(static_cast<uint8_t>(Zone::RearLeft)   == 3);
    REQUIRE(static_cast<uint8_t>(Zone::RearRight)  == 4);
    REQUIRE(static_cast<uint8_t>(Zone::Central)    == 5);
}

TEST_CASE("All Zone constants are distinct", "[zone][REQ-ZONE-008]") {
    const std::vector<Zone> zones{
        Zone::FrontLeft, Zone::FrontRight,
        Zone::RearLeft,  Zone::RearRight,
        Zone::Central,
    };
    for (size_t i = 0; i < zones.size(); ++i) {
        for (size_t j = 0; j < zones.size(); ++j) {
            if (i != j) REQUIRE(zones[i] != zones[j]);
        }
    }
}

// ── Priority constants ────────────────────────────────────────────────────────

TEST_CASE("Priority constants have correct ordering", "[priority][REQ-PRI-001][REQ-PRI-002][REQ-PRI-003]") {
    REQUIRE(static_cast<uint8_t>(Priority::Normal)   == 0);
    REQUIRE(Priority::High     > Priority::Normal);
    REQUIRE(Priority::Critical > Priority::High);
}

// ── CommandType constants ─────────────────────────────────────────────────────

TEST_CASE("CommandType constants have correct numeric values", "[cmdtype][REQ-CMD-001][REQ-CMD-002][REQ-CMD-003][REQ-CMD-004][REQ-CMD-005]") {
    REQUIRE(static_cast<uint16_t>(CommandType::Noop)     == 0);
    REQUIRE(static_cast<uint16_t>(CommandType::Set)      == 1);
    REQUIRE(static_cast<uint16_t>(CommandType::Get)      == 2);
    REQUIRE(static_cast<uint16_t>(CommandType::Reset)    == 3);
    REQUIRE(static_cast<uint16_t>(CommandType::Watchdog) == 4);
    REQUIRE(static_cast<uint16_t>(CommandType::Sleep)    == 5);
    REQUIRE(static_cast<uint16_t>(CommandType::Wake)     == 6);
}

TEST_CASE("All CommandType constants are distinct", "[cmdtype][REQ-CMD-006]") {
    const std::vector<CommandType> cmds{
        CommandType::Noop, CommandType::Set, CommandType::Get,
        CommandType::Reset, CommandType::Watchdog,
    };
    for (size_t i = 0; i < cmds.size(); ++i) {
        for (size_t j = 0; j < cmds.size(); ++j) {
            if (i != j) REQUIRE(cmds[i] != cmds[j]);
        }
    }
}

// ── ResponseStatus constants ──────────────────────────────────────────────────

TEST_CASE("ResponseStatus::to_string returns unique non-empty name", "[status][REQ-STATUS-001]") {
    const std::vector<ResponseStatus> statuses{
        ResponseStatus::OK, ResponseStatus::Error,
        ResponseStatus::Timeout, ResponseStatus::Busy,
        ResponseStatus::Unknown,
    };
    std::set<std::string> seen;
    for (auto s : statuses) {
        auto str = to_string(s);
        REQUIRE_FALSE(str.empty());
        REQUIRE(seen.find(str) == seen.end());
        seen.insert(str);
    }
}

TEST_CASE("ResponseStatus constants have correct numeric values", "[status][REQ-STATUS-002][REQ-STATUS-003][REQ-STATUS-004][REQ-STATUS-005]") {
    REQUIRE(static_cast<uint8_t>(ResponseStatus::OK)      == 0);
    REQUIRE(static_cast<uint8_t>(ResponseStatus::Error)   == 1);
    REQUIRE(static_cast<uint8_t>(ResponseStatus::Timeout) == 2);
    REQUIRE(static_cast<uint8_t>(ResponseStatus::Busy)    == 3);
}

TEST_CASE("All ResponseStatus constants are distinct", "[status][REQ-STATUS-006]") {
    const std::vector<ResponseStatus> statuses{
        ResponseStatus::OK, ResponseStatus::Error,
        ResponseStatus::Timeout, ResponseStatus::Busy,
        ResponseStatus::Unknown,
    };
    for (size_t i = 0; i < statuses.size(); ++i) {
        for (size_t j = 0; j < statuses.size(); ++j) {
            if (i != j) REQUIRE(statuses[i] != statuses[j]);
        }
    }
}

// ── Sentinel error codes ──────────────────────────────────────────────────────

TEST_CASE("Sentinel errors are truthy (non-null)", "[errors][REQ-ERR-001][REQ-ERR-002][REQ-ERR-003][REQ-ERR-004][REQ-ERR-005]") {
    REQUIRE(ErrClosed        == make_error_code(Errc::closed));
    REQUIRE(ErrNotFound      == make_error_code(Errc::not_found));
    REQUIRE(ErrAlreadyExists == make_error_code(Errc::already_exists));
    REQUIRE(ErrTimeout       == make_error_code(Errc::timeout));
    REQUIRE(ErrBusy          == make_error_code(Errc::busy));
    REQUIRE(ErrZoneMismatch  == make_error_code(Errc::zone_mismatch));
    REQUIRE(ErrClosed);
    REQUIRE(ErrNotFound);
    REQUIRE(ErrAlreadyExists);
    REQUIRE(ErrTimeout);
    REQUIRE(ErrBusy);
    REQUIRE(ErrZoneMismatch);
}

TEST_CASE("All sentinel errors are mutually distinct", "[errors][REQ-ERR-006]") {
    const std::vector<std::error_code> sentinels{
        ErrClosed, ErrNotFound, ErrAlreadyExists,
        ErrTimeout, ErrBusy, ErrZoneMismatch,
    };
    for (size_t i = 0; i < sentinels.size(); ++i) {
        for (size_t j = 0; j < sentinels.size(); ++j) {
            if (i != j) REQUIRE(sentinels[i] != sentinels[j]);
        }
    }
}

TEST_CASE("Sentinel errors belong to the rcp category", "[errors][REQ-ERR-007][REQ-ERR-008][REQ-ERR-009][REQ-ERR-010][REQ-ERR-011]") {
    const std::vector<std::error_code> sentinels{
        ErrClosed, ErrNotFound, ErrAlreadyExists,
        ErrTimeout, ErrBusy, ErrZoneMismatch,
    };
    for (auto& ec : sentinels) {
        REQUIRE(&ec.category() == &rcp_category());
    }
}

TEST_CASE("ErrZoneMismatch is distinct from all other sentinels", "[errors][REQ-ERR-011]") {
    REQUIRE(ErrZoneMismatch != ErrClosed);
    REQUIRE(ErrZoneMismatch != ErrNotFound);
    REQUIRE(ErrZoneMismatch != ErrAlreadyExists);
    REQUIRE(ErrZoneMismatch != ErrTimeout);
    REQUIRE(ErrZoneMismatch != ErrBusy);
}

// ── Struct zero values ────────────────────────────────────────────────────────

TEST_CASE("Zero-value Command has safe defaults", "[command][REQ-CMDSTRUCT-001][REQ-CMDSTRUCT-002]") {
    Command cmd;
    REQUIRE(cmd.zone     == Zone::Unknown);
    REQUIRE(cmd.type     == CommandType::Noop);
    REQUIRE(cmd.priority == Priority::Normal);
    REQUIRE(cmd.payload.empty());
}

TEST_CASE("Zero-value Response has StatusOK", "[response][REQ-RESP-003]") {
    Response r;
    REQUIRE(r.status == ResponseStatus::OK);
}

TEST_CASE("Status with empty payload is valid", "[status][REQ-STAT-005]") {
    Status s;
    REQUIRE(s.payload.empty());
}

// ── Context ───────────────────────────────────────────────────────────────────

TEST_CASE("Context::background is never done", "[context]") {
    auto ctx = Context::background();
    REQUIRE_FALSE(ctx.done());
    REQUIRE_FALSE(ctx.deadline().has_value());
}

TEST_CASE("Context::with_timeout expires after duration", "[context]") {
    auto ctx = Context::with_timeout(std::chrono::milliseconds(1));
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    REQUIRE(ctx.done());
}

TEST_CASE("Context::with_deadline before now is immediately done", "[context]") {
    auto past = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    auto ctx  = Context::with_deadline(past);
    REQUIRE(ctx.done());
}
