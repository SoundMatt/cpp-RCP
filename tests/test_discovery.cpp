// fusa:test REQ-DISC-001
// fusa:test REQ-DISC-002
// fusa:test REQ-DISC-003
// fusa:test REQ-DISC-004
// fusa:test REQ-DISC-005
// fusa:test REQ-DISC-006
// fusa:test REQ-DISC-007
// fusa:test REQ-DISC-008
// fusa:test REQ-DISC-009

// Tests for rcp/discovery.hpp — the RC Server discovery request and
// discovery-stream claiming mechanism (ROADMAP.md milestone 46, "Discovery",
// v2.2.0).

#include <catch2/catch_test_macros.hpp>
#include <rcp/discovery.hpp>

using namespace rcp::discovery;
using rcp::lifecycle::ServerState;

namespace {

using Clock     = DiscoveryClaim::Clock;
using TimePoint = DiscoveryClaim::TimePoint;

TimePoint at(int64_t ms) {
    return TimePoint(std::chrono::milliseconds(ms));
}

} // namespace

// ── Discovery request framing ────────────────────────────────────────────────

TEST_CASE("make_discovery_request targets byte_bus_id 0 as an unconditional read", "[discovery][REQ-DISC-001]") {
    auto info = make_discovery_request(/*transaction_num=*/7);
    REQUIRE(info.byte_bus_id == kDiscoveryByteBusId);
    REQUIRE(info.byte_bus_id == 0);
    REQUIRE_FALSE(info.op); // read, not write
    REQUIRE_FALSE(info.rsp);
    REQUIRE(info.transaction_num == 7);
    REQUIRE(info.read_size_or_segment_num == kDiscoveryDefaultReadSize);
}

TEST_CASE("kDiscoveryRegisterAddress fixes the discovery read at register-map address 0",
          "[discovery][REQ-DISC-001]") {
    REQUIRE(kDiscoveryRegisterAddress == 0);
}

// ── NTSCF-only framing; TSCF-headed discovery is dropped ────────────────────────

TEST_CASE("encode_discovery_request always produces an NTSCF-headed frame", "[discovery][REQ-DISC-002]") {
    rcp::avtp::StreamId sid;
    sid.mac    = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    sid.suffix = 1;

    auto frame = encode_discovery_request(sid, /*sequence_num=*/0, /*transaction_num=*/1);
    REQUIRE_FALSE(frame.empty());
    REQUIRE(frame[0] == rcp::avtp::kSubtypeNtscf);
}

TEST_CASE("decode_discovery_request round-trips an encoded discovery request", "[discovery][REQ-DISC-002]") {
    rcp::avtp::StreamId sid;
    sid.mac    = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    sid.suffix = 42;

    auto frame = encode_discovery_request(sid, /*sequence_num=*/3, /*transaction_num=*/9, /*read_size=*/4);

    rcp::avtp::NtscfHeader     hdr;
    rcp::acf::AcfMessageInfo  info;
    std::vector<uint8_t>       payload;
    auto ec = decode_discovery_request(frame.data(), frame.size(), hdr, info, payload);

    REQUIRE_FALSE(ec);
    REQUIRE(hdr.stream_id == sid);
    REQUIRE(hdr.sequence_num == 3);
    REQUIRE(info.byte_bus_id == kDiscoveryByteBusId);
    REQUIRE(info.transaction_num == 9);
    REQUIRE_FALSE(info.op);
}

TEST_CASE("A TSCF-headed discovery request is dropped, not decoded", "[discovery][REQ-DISC-002]") {
    // Build a TSCF frame around the same discovery-shaped ACF_ABB payload —
    // this must never be treated as a valid discovery request.
    rcp::avtp::TscfHeader hdr;
    hdr.stream_id.mac    = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    hdr.stream_id.suffix = 1;

    auto info = make_discovery_request(/*transaction_num=*/1);
    auto acf  = rcp::acf::encode_acf_abb(info, {});
    hdr.control_data_length = static_cast<uint16_t>(acf.size());

    auto frame = rcp::avtp::encode_tscf_header(hdr);
    frame.insert(frame.end(), acf.begin(), acf.end());

    rcp::avtp::NtscfHeader    out_hdr;
    rcp::acf::AcfMessageInfo out_info;
    std::vector<uint8_t>      out_payload;
    auto ec = decode_discovery_request(frame.data(), frame.size(), out_hdr, out_info, out_payload);

    REQUIRE(ec);
    REQUIRE(ec == make_error_code(DiscoveryErrc::tscf_headed_request_dropped));
}

// ── Any-state answering ──────────────────────────────────────────────────────────

TEST_CASE("A server answers discovery in every lifecycle state", "[discovery][REQ-DISC-009]") {
    REQUIRE(should_answer_discovery(ServerState::HwUnconfigured));
    REQUIRE(should_answer_discovery(ServerState::HwConfigured));
    REQUIRE(should_answer_discovery(ServerState::RcpConfigured));
}

// ── Discovery-stream claiming: first request claims it ──────────────────────────

TEST_CASE("The first discovery request in HW_UNCONFIGURED claims the discovery stream",
          "[discovery][REQ-DISC-003]") {
    DiscoveryClaim claim;
    auto outcome = claim.on_discovery_request(/*client=*/1, ServerState::HwUnconfigured, at(0));
    REQUIRE(outcome == DiscoveryClaim::ClaimOutcome::Claimed);
    REQUIRE(claim.has_active_claim(at(0)));
    REQUIRE(claim.current_holder(at(0)) == std::optional<size_t>(1));
}

TEST_CASE("The first discovery request in HW_CONFIGURED also claims the discovery stream",
          "[discovery][REQ-DISC-003]") {
    DiscoveryClaim claim;
    auto outcome = claim.on_discovery_request(/*client=*/5, ServerState::HwConfigured, at(0));
    REQUIRE(outcome == DiscoveryClaim::ClaimOutcome::Claimed);
    REQUIRE(claim.may_configure(5, at(1)));
}

// ── Claim scoped to HW_UNCONFIGURED / HW_CONFIGURED only ────────────────────────

TEST_CASE("A discovery request in RCP_CONFIGURED never claims the stream", "[discovery][REQ-DISC-007]") {
    DiscoveryClaim claim;
    auto outcome = claim.on_discovery_request(/*client=*/1, ServerState::RcpConfigured, at(0));
    REQUIRE(outcome == DiscoveryClaim::ClaimOutcome::NotEligible);
    REQUIRE_FALSE(claim.has_active_claim(at(0)));
    REQUIRE_FALSE(claim.may_configure(1, at(0)));
}

// ── Claim lapse after Discovery_TimeOut ──────────────────────────────────────────

TEST_CASE("An unclaimed-follow-up claim lapses after Discovery_TimeOut elapses", "[discovery][REQ-DISC-004]") {
    DiscoveryClaim claim(std::chrono::milliseconds(20));
    REQUIRE(claim.on_discovery_request(1, ServerState::HwUnconfigured, at(0))
            == DiscoveryClaim::ClaimOutcome::Claimed);
    REQUIRE(claim.may_configure(1, at(19)));

    // Past the timeout, the claim has lapsed: the original holder may no
    // longer configure ...
    REQUIRE_FALSE(claim.may_configure(1, at(21)));
    REQUIRE_FALSE(claim.has_active_claim(at(21)));

    // ... and a different client's next discovery request is free to claim
    // the now-lapsed stream.
    auto outcome = claim.on_discovery_request(2, ServerState::HwUnconfigured, at(25));
    REQUIRE(outcome == DiscoveryClaim::ClaimOutcome::Claimed);
    REQUIRE(claim.may_configure(2, at(25)));
}

TEST_CASE("Discovery_TimeOut defaults to approximately 20ms", "[discovery][REQ-DISC-004]") {
    REQUIRE(DiscoveryClaim::kDefaultTimeout == std::chrono::milliseconds(20));
}

// ── Configuration request consumes the active claim ─────────────────────────────

TEST_CASE("A configuration request from the claim holder succeeds and consumes the claim",
          "[discovery][REQ-DISC-006]") {
    DiscoveryClaim claim;
    REQUIRE(claim.on_discovery_request(1, ServerState::HwUnconfigured, at(0))
            == DiscoveryClaim::ClaimOutcome::Claimed);

    REQUIRE(claim.on_configuration_request(1, at(5)));
    // The claim is spent: neither the same client's follow-up configuration
    // request, nor a query, sees it as still active.
    REQUIRE_FALSE(claim.has_active_claim(at(5)));
    REQUIRE_FALSE(claim.on_configuration_request(1, at(6)));
}

TEST_CASE("A configuration request from a client that does not hold the claim is refused, "
          "leaving the real holder's claim untouched",
          "[discovery][REQ-DISC-006]") {
    DiscoveryClaim claim;
    REQUIRE(claim.on_discovery_request(1, ServerState::HwUnconfigured, at(0))
            == DiscoveryClaim::ClaimOutcome::Claimed);

    REQUIRE_FALSE(claim.on_configuration_request(/*client=*/2, at(1)));
    // Client 1's claim survives client 2's rejected attempt.
    REQUIRE(claim.may_configure(1, at(2)));
}

// ── Concurrent reads: other clients keep being answered during an active claim ──

TEST_CASE("Another client's discovery request during an active claim does not dislodge or "
          "duplicate it, but that client's read is unaffected",
          "[discovery][REQ-DISC-005]") {
    DiscoveryClaim claim;
    REQUIRE(claim.on_discovery_request(1, ServerState::HwUnconfigured, at(0))
            == DiscoveryClaim::ClaimOutcome::Claimed);

    // A second client's discovery request during the still-active window: it
    // does not claim the stream ...
    auto outcome = claim.on_discovery_request(2, ServerState::HwUnconfigured, at(5));
    REQUIRE(outcome == DiscoveryClaim::ClaimOutcome::HeldByOther);
    // ... yet answering that client's underlying discovery *read* is a
    // separate, unconditional concern the claim never gates — modeled here
    // by should_answer_discovery being state-only and never consulting claim
    // state at all.
    REQUIRE(should_answer_discovery(ServerState::HwUnconfigured));

    // The original holder's claim is unaffected by the other client's request.
    REQUIRE(claim.may_configure(1, at(6)));
}

TEST_CASE("The claim holder re-requesting discovery before it lapses is reported as AlreadyHeld",
          "[discovery][REQ-DISC-005]") {
    DiscoveryClaim claim;
    REQUIRE(claim.on_discovery_request(1, ServerState::HwUnconfigured, at(0))
            == DiscoveryClaim::ClaimOutcome::Claimed);
    auto outcome = claim.on_discovery_request(1, ServerState::HwUnconfigured, at(2));
    REQUIRE(outcome == DiscoveryClaim::ClaimOutcome::AlreadyHeld);
}

// ── DiscoveryErrc category sanity ────────────────────────────────────────────────

TEST_CASE("DiscoveryErrc reports a non-empty message in its own category", "[discovery][REQ-DISC-008]") {
    auto ec = make_error_code(DiscoveryErrc::tscf_headed_request_dropped);
    REQUIRE(ec.category() == discovery_category());
    REQUIRE_FALSE(ec.message().empty());
}
