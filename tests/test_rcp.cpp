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
// fusa:test REQ-LOAN-007
//
// Rebound (cpp-RCP-FS-04, #87): this file used to certify the retired
// Zone/Priority/CommandType/Command/Response/Status model's exact numeric
// values and zero-value defaults (Zone::FrontLeft==1, CommandType::Set==1,
// Priority ordering, etc). That model is retired (cpp-RCP-FS-01, #84); those
// cases are replaced below with conformance coverage for what rcp.hpp
// actually still defines — the rcp::Errc sentinel category, rcp::Context,
// and rcp::Loan — plus a couple of real assertions about the TC18 surface
// (rcp/avtp.hpp's ByteBusId, rcp/acf.hpp's AcfMessageInfo default value)
// that a reader coming from the old Zone/Command certification tests would
// otherwise have no equivalent for.
#include <catch2/catch_test_macros.hpp>
#include <rcp/acf.hpp>
#include <rcp/avtp.hpp>
#include <rcp/rcp.hpp>
#include <limits>
#include <thread>
#include <type_traits>

using namespace rcp;

// ── Sentinel error codes ──────────────────────────────────────────────────────

TEST_CASE("Sentinel errors are truthy (non-null)", "[errors][REQ-ERR-001][REQ-ERR-002][REQ-ERR-003][REQ-ERR-004][REQ-ERR-005]") {
    REQUIRE(ErrClosed        == make_error_code(Errc::closed));
    REQUIRE(ErrNotFound      == make_error_code(Errc::not_found));
    REQUIRE(ErrAlreadyExists == make_error_code(Errc::already_exists));
    REQUIRE(ErrTimeout       == make_error_code(Errc::timeout));
    REQUIRE(ErrBusy          == make_error_code(Errc::busy));
    REQUIRE(ErrClosed);
    REQUIRE(ErrNotFound);
    REQUIRE(ErrAlreadyExists);
    REQUIRE(ErrTimeout);
    REQUIRE(ErrBusy);
}

TEST_CASE("All sentinel errors are mutually distinct", "[errors][REQ-ERR-006]") {
    const std::vector<std::error_code> sentinels{
        ErrClosed, ErrNotFound, ErrAlreadyExists, ErrTimeout, ErrBusy,
    };
    for (size_t i = 0; i < sentinels.size(); ++i) {
        for (size_t j = 0; j < sentinels.size(); ++j) {
            if (i != j) REQUIRE(sentinels[i] != sentinels[j]);
        }
    }
}

TEST_CASE("Sentinel errors belong to the rcp category", "[errors][REQ-ERR-007][REQ-ERR-008][REQ-ERR-009][REQ-ERR-010]") {
    const std::vector<std::error_code> sentinels{
        ErrClosed, ErrNotFound, ErrAlreadyExists, ErrTimeout, ErrBusy,
    };
    for (auto& ec : sentinels) {
        REQUIRE(&ec.category() == &rcp_category());
    }
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

// ── Loan ──────────────────────────────────────────────────────────────────────

TEST_CASE("Loan invokes its release function exactly once on destruction",
          "[loan][REQ-LOAN-007]") {
    int released = 0;
    {
        Loan l({0x01, 0x02}, [&] { ++released; });
        REQUIRE(released == 0);
        REQUIRE(l.payload.size() == 2);
    }
    REQUIRE(released == 1);
}

TEST_CASE("Loan::ret() releases immediately and destruction does not release again",
          "[loan][REQ-LOAN-007]") {
    int released = 0;
    {
        Loan l({}, [&] { ++released; });
        l.ret();
        REQUIRE(released == 1);
    } // destructor must not double-release
    REQUIRE(released == 1);
}

TEST_CASE("A default-constructed Loan releases nothing on destruction", "[loan][REQ-LOAN-007]") {
    Loan l; // no release function bound — must not crash on destruction
    REQUIRE(l.payload.empty());
}

// ── TC18 surface: rcp/avtp.hpp's ByteBusId, rcp/acf.hpp's AcfMessageInfo ──────
// A couple of real assertions about the TC18-based model that replaced the
// retired Zone/Command surface above — see rcp/acf.hpp/rcp/avtp.hpp (not
// modified by this change) for the full conformance suites (test_acf.cpp,
// test_avtp.cpp).

TEST_CASE("avtp::ByteBusId spans the full single-byte endpoint address range",
          "[tc18][bytebusid]") {
    static_assert(std::is_same<avtp::ByteBusId, uint8_t>::value,
        "ByteBusId must be a single byte (RELAY spec §15.5)");
    REQUIRE(static_cast<unsigned>(std::numeric_limits<avtp::ByteBusId>::min()) == 0);
    REQUIRE(static_cast<unsigned>(std::numeric_limits<avtp::ByteBusId>::max()) == 255);
}

TEST_CASE("Zero-value AcfMessageInfo has safe read/request defaults", "[tc18][acf]") {
    acf::AcfMessageInfo info;
    REQUIRE(info.acf_msg_type == acf::kAcfMsgTypeAbb);
    REQUIRE(info.byte_bus_id == 0);
    REQUIRE(info.transaction_num == 0);
    REQUIRE_FALSE(info.op);  // false = read, the non-mutating default
    REQUIRE_FALSE(info.rsp);
    REQUIRE_FALSE(info.err);
    REQUIRE_FALSE(info.ms);
    REQUIRE_FALSE(info.evt_ack);
}
