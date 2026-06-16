// fusa:test REQ-LOAN-001
// fusa:test REQ-LOAN-002
// fusa:test REQ-LOAN-003
// fusa:test REQ-LOAN-004
// fusa:test REQ-LOAN-005
// fusa:test REQ-LOAN-006

#include <catch2/catch_test_macros.hpp>
#include <rcp/mock.hpp>
#include <rcp/loan.hpp>

using namespace rcp;

static std::shared_ptr<mock::Controller> make_mock(Zone z = Zone::FrontLeft) {
    return std::make_shared<mock::Controller>(z);
}

// ── loan() ────────────────────────────────────────────────────────────────────

TEST_CASE("loan::Controller::loan returns zeroed buffer of requested size", "[loan][REQ-LOAN-001]") {
    auto inner = make_mock();
    loan::Controller lc(inner);

    std::unique_ptr<Loan> loan_out;
    REQUIRE_FALSE(lc.loan(16, loan_out));
    REQUIRE(loan_out != nullptr);
    REQUIRE(loan_out->payload.size() == 16);
    for (auto b : loan_out->payload) REQUIRE(b == 0);
}

TEST_CASE("loan::Controller::loan returns ErrClosed when closed", "[loan][REQ-LOAN-002]") {
    auto inner = make_mock();
    loan::Controller lc(inner);
    { auto ec = lc.close(); (void)ec; }

    std::unique_ptr<Loan> loan_out;
    auto ec = lc.loan(8, loan_out);
    REQUIRE(ec == ErrClosed);
}

TEST_CASE("loan::Controller::loan returns error for negative size", "[loan][REQ-LOAN-003]") {
    auto inner = make_mock();
    loan::Controller lc(inner);

    std::unique_ptr<Loan> loan_out;
    auto ec = lc.loan(-1, loan_out);
    REQUIRE(ec); // any error is acceptable for invalid size
}

// ── send_loaned() ─────────────────────────────────────────────────────────────

TEST_CASE("loan::Controller::send_loaned delivers command and returns OK", "[loan][REQ-LOAN-004]") {
    auto inner = make_mock();
    loan::Controller lc(inner);

    std::unique_ptr<Loan> loan_out;
    REQUIRE_FALSE(lc.loan(4, loan_out));
    loan_out->payload = {0x01, 0x02, 0x03, 0x04};

    Command cmd;
    cmd.zone    = Zone::FrontLeft;
    cmd.payload = loan_out->payload;
    loan_out->ret(); // release back to pool (simulate send_loaned transfer)

    Response resp;
    auto ec = lc.send_loaned(Context::background(), cmd, resp);
    REQUIRE_FALSE(ec);
    REQUIRE(resp.status == ResponseStatus::OK);
}

// ── Pool return ───────────────────────────────────────────────────────────────

TEST_CASE("Loan RAII releases buffer to pool on destruction", "[loan][REQ-LOAN-005]") {
    auto inner = make_mock();
    loan::Controller lc(inner);

    {
        std::unique_ptr<Loan> loan_out;
        REQUIRE_FALSE(lc.loan(8, loan_out));
    } // loan_out goes out of scope here — should return to pool without crash

    // Loan again to verify pool is reusable.
    std::unique_ptr<Loan> loan_out2;
    REQUIRE_FALSE(lc.loan(8, loan_out2));
    REQUIRE(loan_out2 != nullptr);
}

// ── Zone passthrough ──────────────────────────────────────────────────────────

TEST_CASE("loan::Controller::zone returns inner zone", "[loan][REQ-LOAN-006]") {
    auto inner = make_mock(Zone::RearRight);
    loan::Controller lc(inner);
    REQUIRE(lc.zone() == Zone::RearRight);
}
