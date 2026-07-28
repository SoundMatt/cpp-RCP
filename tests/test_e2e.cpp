// fusa:test REQ-E2E-001
// fusa:test REQ-E2E-002
// fusa:test REQ-E2E-003
// fusa:test REQ-E2E-004
// fusa:test REQ-E2E-005
// fusa:test REQ-E2E-006
// fusa:test REQ-E2E-007
// fusa:test REQ-E2E-008
// fusa:test REQ-E2E-009
// fusa:test REQ-E2E-010
// fusa:test REQ-E2E-011
// fusa:test REQ-E2E-012
// fusa:test REQ-E2E-013
// fusa:test REQ-E2E-014

// Tests for rcp/e2e.hpp — E2E CRC safe points and the per-request-stream
// watchdog/safe-state primitives (ROADMAP.md milestone 50, "E2E CRC Safe
// Points & Safety-Request Variants", v2.6.0).

#include <catch2/catch_test_macros.hpp>
#include <rcp/e2e.hpp>

using namespace rcp::e2e;
using rcp::regmap::EndpointGenericConfig;
using rcp::regmap::RequestStreamConfig;
using rcp::regmap::RxSafetyMeasure;
using rcp::sequencer::RequestLedger;
using rcp::sequencer::RequestRecord;
using rcp::sequencer::RequestTypeOpcode;
using rcp::sequencer::SequencerTable;
using rcp::sequencer::request_record_for;
using rcp::wire::AcfMessageInfo;
using rcp::wire::StreamId;

namespace {

StreamId make_stream_id(uint8_t mac_seed, uint16_t suffix) {
    StreamId id;
    for (auto& b : id.mac) b = mac_seed++;
    id.suffix = suffix;
    return id;
}

} // namespace

// ── CRC32 primitive ───────────────────────────────────────────────────────────

TEST_CASE("crc32 of empty input is the all-ones init XORed with all-ones xorout", "[e2e][REQ-E2E-001]") {
    // With init=0xFFFFFFFF and no bytes processed, the update loop never
    // runs, so the result is init ^ xorout == 0xFFFFFFFF ^ 0xFFFFFFFF == 0.
    REQUIRE(crc32(nullptr, 0) == 0u);
}

TEST_CASE("crc32 is deterministic and sensitive to every input byte", "[e2e][REQ-E2E-001]") {
    std::vector<uint8_t> a{0x01, 0x02, 0x03, 0x04};
    std::vector<uint8_t> b{0x01, 0x02, 0x03, 0x05};
    REQUIRE(crc32(a) == crc32(a));
    REQUIRE(crc32(a) != crc32(b));
}

TEST_CASE("crc32 known-vector regression check", "[e2e][REQ-E2E-001]") {
    // Cross-checked against an independent reference implementation of the
    // standard reflected-CRC construction (RefIn=true, RefOut=true, init
    // 0xFFFFFFFF, xorout 0xFFFFFFFF) using this file's own polynomial
    // (0xF4ACFB13) — not a vector taken from the confidential specification
    // text, purely a regression guard against this implementation
    // silently changing behavior.
    std::vector<uint8_t> data{'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    REQUIRE(crc32(data) == 0x1697D06Au);
}

// ── CRC coverage & length adjustment ──────────────────────────────────────────

TEST_CASE("coverage_buffer zero-fills avtp_timestamp under NTSCF (nullopt)", "[e2e][REQ-E2E-002]") {
    auto sid = make_stream_id(0x02, 0x1234);
    AcfMessageInfo info;
    info.byte_bus_id = 5;
    std::vector<uint8_t> payload{0xAA, 0xBB};

    auto with_zero    = coverage_buffer(sid, uint32_t{0}, info, payload);
    auto with_nullopt = coverage_buffer(sid, std::nullopt, info, payload);
    REQUIRE(with_zero == with_nullopt);
}

TEST_CASE("coverage_buffer layout is stream_id + avtp_timestamp + ACF header + payload",
          "[e2e][REQ-E2E-002]") {
    auto sid = make_stream_id(0x02, 0x0001);
    AcfMessageInfo info;
    info.byte_bus_id = 7;
    std::vector<uint8_t> payload{1, 2, 3};

    auto buf = coverage_buffer(sid, uint32_t{0xDEADBEEF}, info, payload);
    REQUIRE(buf.size() == 8 + 4 + rcp::wire::kAcfCommonHeaderLen + payload.size());

    // stream_id occupies the first 8 bytes, big-endian.
    REQUIRE(buf[0] == sid.mac[0]);
    // avtp_timestamp occupies the next 4 bytes, big-endian.
    REQUIRE(buf[8]  == 0xDE);
    REQUIRE(buf[9]  == 0xAD);
    REQUIRE(buf[10] == 0xBE);
    REQUIRE(buf[11] == 0xEF);
    // Payload is the final bytes, unchanged.
    REQUIRE(buf[buf.size() - 3] == 1);
    REQUIRE(buf[buf.size() - 1] == 3);
}

TEST_CASE("compute_crc changes when covered fields change", "[e2e][REQ-E2E-002]") {
    auto sid = make_stream_id(0x02, 0x0001);
    AcfMessageInfo info;
    info.byte_bus_id = 1;
    std::vector<uint8_t> payload{9, 9, 9};

    uint32_t base = compute_crc(sid, std::nullopt, info, payload);

    AcfMessageInfo different_info = info;
    different_info.byte_bus_id     = 2;
    REQUIRE(compute_crc(sid, std::nullopt, different_info, payload) != base);

    std::vector<uint8_t> different_payload{9, 9, 8};
    REQUIRE(compute_crc(sid, std::nullopt, info, different_payload) != base);

    REQUIRE(compute_crc(sid, uint32_t{1}, info, payload) != base);
}

TEST_CASE("apply_acf_length_adjustment adds exactly one quadlet", "[e2e][REQ-E2E-003]") {
    AcfMessageInfo info;
    info.acf_msg_length = 10;
    apply_acf_length_adjustment(info);
    REQUIRE(info.acf_msg_length == 10 + kCrcLengthAdjustQuadlets);
    REQUIRE(kCrcLengthAdjustQuadlets == 1);
}

TEST_CASE("apply_frame_length_adjustment adds exactly four octets to NTSCF and TSCF headers",
          "[e2e][REQ-E2E-003]") {
    rcp::wire::NtscfHeader ntscf;
    ntscf.control_data_length = 20;
    apply_frame_length_adjustment(ntscf);
    REQUIRE(ntscf.control_data_length == 20 + kCrcLengthAdjustOctets);

    rcp::wire::TscfHeader tscf;
    tscf.control_data_length = 30;
    apply_frame_length_adjustment(tscf);
    REQUIRE(tscf.control_data_length == 30 + kCrcLengthAdjustOctets);

    REQUIRE(kCrcLengthAdjustOctets == 4);
}

// ── verify_crc / append_crc ────────────────────────────────────────────────────

TEST_CASE("verify_crc accepts a matching CRC and rejects a corrupted one", "[e2e][REQ-E2E-004]") {
    auto sid = make_stream_id(0x02, 0x0002);
    AcfMessageInfo info;
    info.byte_bus_id = 3;
    std::vector<uint8_t> payload{1, 2, 3, 4};

    uint32_t crc = compute_crc(sid, std::nullopt, info, payload);
    REQUIRE_FALSE(verify_crc(sid, std::nullopt, info, payload, crc));
    REQUIRE(verify_crc(sid, std::nullopt, info, payload, crc ^ 0xFFFFFFFFu) ==
            make_error_code(E2eErrc::crc_error));
}

TEST_CASE("append_crc appends exactly 4 big-endian octets", "[e2e][REQ-E2E-004]") {
    std::vector<uint8_t> frame{0x11, 0x22};
    append_crc(frame, 0x01020304);
    REQUIRE(frame.size() == 6);
    REQUIRE(frame[2] == 0x01);
    REQUIRE(frame[3] == 0x02);
    REQUIRE(frame[4] == 0x03);
    REQUIRE(frame[5] == 0x04);
}

// ── Per-endpoint opt-in safe mode ─────────────────────────────────────────────

TEST_CASE("crc_required reflects each independently-toggled endpoint config field",
          "[e2e][REQ-E2E-005]") {
    EndpointGenericConfig cfg;
    REQUIRE_FALSE(crc_required(cfg, MessageRole::Request));
    REQUIRE_FALSE(crc_required(cfg, MessageRole::Acknowledge));
    REQUIRE_FALSE(crc_required(cfg, MessageRole::Response));

    cfg.ep_req_crc_enable = true;
    REQUIRE(crc_required(cfg, MessageRole::Request));
    REQUIRE_FALSE(crc_required(cfg, MessageRole::Acknowledge));
    REQUIRE_FALSE(crc_required(cfg, MessageRole::Response));

    cfg.ep_ack_crc_enable      = true;
    cfg.ep_response_crc_enable = true;
    REQUIRE(crc_required(cfg, MessageRole::Acknowledge));
    REQUIRE(crc_required(cfg, MessageRole::Response));
}

TEST_CASE("implemented_options_bit reports kOptSafetyRequests only when actually implemented",
          "[e2e][REQ-E2E-005]") {
    REQUIRE(implemented_options_bit(false) == 0u);
    REQUIRE(implemented_options_bit(true) == rcp::regmap::kOptSafetyRequests);
}

// ── RxStreamGuard — rx_enforce_e2e ────────────────────────────────────────────

TEST_CASE("RxStreamGuard drops only the failing request when rx_enforce_e2e is clear",
          "[e2e][REQ-E2E-006]") {
    RequestStreamConfig cfg; // rx_enforce_e2e defaults to false
    RxStreamGuard guard;

    REQUIRE(guard.record_crc_result(cfg, /*ok=*/false) == make_error_code(E2eErrc::crc_error));
    REQUIRE_FALSE(guard.latched());
    REQUIRE_FALSE(guard.record_crc_result(cfg, /*ok=*/true)); // next request unaffected
}

TEST_CASE("RxStreamGuard latches the whole stream when rx_enforce_e2e is set", "[e2e][REQ-E2E-006]") {
    RequestStreamConfig cfg;
    cfg.rx_enforce_e2e = true;
    RxStreamGuard guard;

    REQUIRE(guard.record_crc_result(cfg, /*ok=*/false) == make_error_code(E2eErrc::crc_error));
    REQUIRE(guard.latched());
    // Every subsequent request fails too, even one whose own CRC was fine.
    REQUIRE(guard.record_crc_result(cfg, /*ok=*/true) == make_error_code(E2eErrc::crc_error));

    guard.reset_latch();
    REQUIRE_FALSE(guard.latched());
    REQUIRE_FALSE(guard.record_crc_result(cfg, /*ok=*/true));
}

// ── RxSequenceGuard — rx_enforce_seq ───────────────────────────────────────────

TEST_CASE("RxSequenceGuard is a no-op when rx_enforce_seq is clear", "[e2e][REQ-E2E-007]") {
    RequestStreamConfig cfg; // rx_enforce_seq defaults to false
    RxSequenceGuard guard;
    REQUIRE_FALSE(guard.check(cfg, 5));
    REQUIRE_FALSE(guard.check(cfg, 1)); // would violate monotonicity if enforced
}

TEST_CASE("RxSequenceGuard rejects a non-increasing sequence number when enforced",
          "[e2e][REQ-E2E-007]") {
    RequestStreamConfig cfg;
    cfg.rx_enforce_seq = true;
    RxSequenceGuard guard;

    REQUIRE_FALSE(guard.check(cfg, 10)); // bootstrap accepts the first value
    REQUIRE_FALSE(guard.check(cfg, 11));
    REQUIRE(guard.check(cfg, 11) == make_error_code(E2eErrc::sequence_violation)); // repeat
    REQUIRE(guard.check(cfg, 9) == make_error_code(E2eErrc::sequence_violation));  // regression
}

// ── RxWatchdog — rx_wd_enable / rx_wd_timeout_interval ────────────────────────

TEST_CASE("RxWatchdog never overflows while disabled or before any kick", "[e2e][REQ-E2E-008]") {
    RequestStreamConfig cfg;
    cfg.rx_wd_timeout_interval = 100;
    RxWatchdog wd;
    REQUIRE_FALSE(wd.overflowed(cfg, /*now_ms=*/10'000)); // rx_wd_enable defaults to false

    cfg.rx_wd_enable = true;
    REQUIRE_FALSE(wd.overflowed(cfg, /*now_ms=*/10'000)); // never kicked
}

TEST_CASE("RxWatchdog overflows once the timeout interval elapses since the last kick",
          "[e2e][REQ-E2E-008]") {
    RequestStreamConfig cfg;
    cfg.rx_wd_enable           = true;
    cfg.rx_wd_timeout_interval = 100;
    RxWatchdog wd;

    wd.kick(1'000);
    REQUIRE_FALSE(wd.overflowed(cfg, 1'050));
    REQUIRE(wd.overflowed(cfg, 1'101));

    wd.kick(1'101);
    REQUIRE_FALSE(wd.overflowed(cfg, 1'150));
}

// ── Watchdog/queue overflow purge-normal/retain-safety ────────────────────────

TEST_CASE("apply_watchdog_overflow purges normal requests but retains safety-tagged ones",
          "[e2e][REQ-E2E-009]") {
    RequestStreamConfig cfg;
    cfg.rx_wd_safestate_enable = true;

    RequestLedger ledger;
    REQUIRE_FALSE(ledger.submit(request_record_for(1, RequestTypeOpcode::Compound, /*cs=*/false)));
    REQUIRE_FALSE(ledger.submit(request_record_for(2, RequestTypeOpcode::CompoundSafety, /*cs=*/false)));

    RxWatchdog wd;
    size_t purged = apply_watchdog_overflow(cfg, wd, ledger);

    REQUIRE(purged == 1);
    REQUIRE(wd.in_safe_state());
    REQUIRE(ledger.find(1)->state == rcp::sequencer::RequestState::Canceled);
    REQUIRE(ledger.find(2)->state == rcp::sequencer::RequestState::Pending);
}

TEST_CASE("apply_watchdog_overflow purges nothing and does not enter safe state when disabled",
          "[e2e][REQ-E2E-009]") {
    RequestStreamConfig cfg; // rx_wd_safestate_enable defaults to false
    RequestLedger ledger;
    REQUIRE_FALSE(ledger.submit(request_record_for(1, RequestTypeOpcode::Compound, /*cs=*/false)));

    RxWatchdog wd;
    REQUIRE(apply_watchdog_overflow(cfg, wd, ledger) == 0);
    REQUIRE_FALSE(wd.in_safe_state());
    REQUIRE(ledger.find(1)->state == rcp::sequencer::RequestState::Pending);
}

TEST_CASE("apply_queue_overflow implements the same purge-normal/retain-safety rule via a "
          "distinct trigger",
          "[e2e][REQ-E2E-010]") {
    RequestStreamConfig cfg;
    cfg.rx_ovrflw_safestate_enable = true;

    RequestLedger ledger;
    REQUIRE_FALSE(ledger.submit(request_record_for(1, RequestTypeOpcode::Triggered, /*cs=*/false)));
    REQUIRE_FALSE(ledger.submit(request_record_for(2, RequestTypeOpcode::TriggeredSafety, /*cs=*/false)));

    RxWatchdog wd;
    REQUIRE(apply_queue_overflow(cfg, wd, ledger) == 1);
    REQUIRE(wd.in_safe_state());
    REQUIRE(ledger.find(1)->state == rcp::sequencer::RequestState::Canceled);
    REQUIRE(ledger.find(2)->state == rcp::sequencer::RequestState::Pending);
}

// ── Safe-state gating ─────────────────────────────────────────────────────────

TEST_CASE("endpoint_in_configured_safe_state: ForceHighImpedance defers to the caller-supplied flag",
          "[e2e][REQ-E2E-011]") {
    RequestStreamConfig cfg; // rx_safety_measure defaults to ForceHighImpedance
    std::vector<rcp::regmap::SequencerState> states;
    SequencerTable sequencers(states);

    REQUIRE_FALSE(endpoint_in_configured_safe_state(cfg, sequencers, /*force_high_impedance_asserted=*/false));
    REQUIRE(endpoint_in_configured_safe_state(cfg, sequencers, /*force_high_impedance_asserted=*/true));
}

TEST_CASE("endpoint_in_configured_safe_state: RunSafeSequencer checks the target sequencer's value",
          "[e2e][REQ-E2E-011]") {
    RequestStreamConfig cfg;
    cfg.rx_safety_measure       = RxSafetyMeasure::RunSafeSequencer;
    cfg.rx_safestate_sequencer  = 0;
    cfg.rx_safe_sequencer_state = 3;

    std::vector<rcp::regmap::SequencerState> states;
    SequencerTable sequencers(states);
    sequencers.ensure_size(1); // starts at SequencerTable::kDefaultState (1), not 3

    REQUIRE_FALSE(endpoint_in_configured_safe_state(cfg, sequencers, /*force_high_impedance_asserted=*/true));

    states[0] = 3;
    REQUIRE(endpoint_in_configured_safe_state(cfg, sequencers, /*force_high_impedance_asserted=*/false));
}

TEST_CASE("endpoint_in_configured_safe_state: RunSafeSequencer with an out-of-range index is never safe",
          "[e2e][REQ-E2E-011]") {
    RequestStreamConfig cfg;
    cfg.rx_safety_measure      = RxSafetyMeasure::RunSafeSequencer;
    cfg.rx_safestate_sequencer = 5; // no sequencer at this index

    std::vector<rcp::regmap::SequencerState> states;
    SequencerTable sequencers(states);
    REQUIRE_FALSE(endpoint_in_configured_safe_state(cfg, sequencers, true));
}

TEST_CASE("may_execute_now: normal requests are always eligible; safety requests need safe state",
          "[e2e][REQ-E2E-012]") {
    RequestRecord normal = request_record_for(1, RequestTypeOpcode::Compound, false);
    RequestRecord safety = request_record_for(2, RequestTypeOpcode::CompoundSafety, false);

    REQUIRE(may_execute_now(normal, /*endpoint_in_safe_state=*/false));
    REQUIRE(may_execute_now(normal, /*endpoint_in_safe_state=*/true));
    REQUIRE_FALSE(may_execute_now(safety, /*endpoint_in_safe_state=*/false));
    REQUIRE(may_execute_now(safety, /*endpoint_in_safe_state=*/true));
}

// ── Watchdog info notification ────────────────────────────────────────────────

TEST_CASE("RxWatchdog emits the info notification only while latched with rx_wd_info_enable set",
          "[e2e][REQ-E2E-013]") {
    RequestStreamConfig cfg;
    RxWatchdog wd;

    REQUIRE_FALSE(wd.should_emit_info_notification(cfg)); // not latched yet

    wd.enter_safe_state();
    REQUIRE_FALSE(wd.should_emit_info_notification(cfg)); // latched, but feature disabled

    cfg.rx_wd_info_enable = true;
    REQUIRE(wd.should_emit_info_notification(cfg));

    wd.clear_safe_state();
    REQUIRE_FALSE(wd.should_emit_info_notification(cfg));
}

// ── Error category ────────────────────────────────────────────────────────────

TEST_CASE("E2eErrc is a distinct error category with non-empty, distinct messages",
          "[e2e][REQ-E2E-014]") {
    std::error_code crc  = make_error_code(E2eErrc::crc_error);
    std::error_code seq  = make_error_code(E2eErrc::sequence_violation);

    REQUIRE(crc.category() == e2e_category());
    REQUIRE(seq.category() == e2e_category());
    REQUIRE_FALSE(crc.message().empty());
    REQUIRE_FALSE(seq.message().empty());
    REQUIRE(crc.message() != seq.message());
}
