// fusa:test REQ-SEQ-001
// fusa:test REQ-SEQ-002
// fusa:test REQ-SEQ-003
// fusa:test REQ-SEQ-004
// fusa:test REQ-SEQ-005
// fusa:test REQ-SEQ-006
// fusa:test REQ-SEQ-007
// fusa:test REQ-SEQ-008
// fusa:test REQ-SEQ-009
// fusa:test REQ-SEQ-010
// fusa:test REQ-SEQ-011
// fusa:test REQ-SEQ-012
// fusa:test REQ-CMP-001
// fusa:test REQ-CMP-002
// fusa:test REQ-CMP-003
// fusa:test REQ-CMP-008
// fusa:test REQ-CMP-009
// fusa:test REQ-CMP-010
// fusa:test REQ-CMP-011
// fusa:test REQ-CMP-012
// fusa:test REQ-CMP-013
// fusa:test REQ-CMP-014
// fusa:test REQ-CMP-015
// fusa:test REQ-CMP-016
// fusa:test REQ-CMP-017
// fusa:test REQ-CMP-018
// fusa:test REQ-CMP-019
// fusa:test REQ-CMP-020
// fusa:test REQ-CMP-021
// fusa:test REQ-CMP-022
// fusa:test REQ-CMP-023
// fusa:test REQ-CMP-024
// fusa:test REQ-CMP-025
// fusa:test REQ-CMP-026
// fusa:test REQ-CMP-027
// fusa:test REQ-CMP-028
// fusa:test REQ-CMP-029
// fusa:test REQ-TRIG-001
// fusa:test REQ-TRIG-003
// fusa:test REQ-TRIG-004
// fusa:test REQ-TRIG-005
// fusa:test REQ-TRIG-006
// fusa:test REQ-TRIG-007
// fusa:test REQ-TRIG-008
// fusa:test REQ-TRIG-009
// fusa:test REQ-TRIG-010
// fusa:test REQ-TRIG-011
// fusa:test REQ-TRIG-012
// fusa:test REQ-TRIG-013
// fusa:test REQ-CHAIN-002
// fusa:test REQ-CHAIN-003
// fusa:test REQ-CHAIN-004
// fusa:test REQ-CHAIN-005
// fusa:test REQ-CHAIN-006
// fusa:test REQ-CHAIN-007
// fusa:test REQ-CHAIN-010
// fusa:test REQ-CHAIN-011
// fusa:test REQ-CHAIN-012
// fusa:test REQ-TIMED-002
// fusa:test REQ-TIMED-003
// fusa:test REQ-TIMED-004
// fusa:test REQ-TIMED-005
// fusa:test REQ-TIMED-006
// fusa:test REQ-TIMED-007
// fusa:test REQ-TIMED-008
// fusa:test REQ-TIMED-009
// fusa:test REQ-TIMED-010
// fusa:test REQ-TIMED-011
// fusa:test REQ-CANCEL-002
// fusa:test REQ-CANCEL-003
// fusa:test REQ-CANCEL-004
// fusa:test REQ-CANCEL-005
// fusa:test REQ-CANCEL-006
// fusa:test REQ-CANCEL-007
// fusa:test REQ-CANCEL-008
// fusa:test REQ-CANCEL-009
// fusa:test REQ-CANCEL-010
// fusa:test REQ-CANCEL-011
// fusa:test REQ-CANCEL-013
// fusa:test REQ-CANCEL-014
// fusa:test REQ-CANCEL-015
// fusa:test REQ-SCHED-002
// fusa:test REQ-SCHED-003
// fusa:test REQ-SCHED-007
// fusa:test REQ-SCHED-008

// Tests for rcp/request.hpp — conditional-request taxonomy, sequencer-state
// primitives, and the request lifecycle ledger. Phase 1 rewrite (cpp-RCP
// issue #129, ROADMAP.md "Phase 17"): ported from c-RCP's request.c/
// request_sequencer.c/scheduler.c and their own test files
// (test_request_compound.c, test_request_chained.c, test_request_triggered.c,
// test_request_timed.c, test_request_cancel.c, test_request_sequencer.c,
// test_scheduler.c) — see rcp/request.hpp's own file header for the full
// list of content deltas this pass found and fixed relative to the prior
// cpp-RCP implementation, including cpp-RCP issue #58 (cs-bit polarity
// inverted in should_execute_chained()).

#include <catch2/catch_test_macros.hpp>
#include <rcp/request.hpp>

using namespace rcp::request;

// ── message_timestamp-repurposing trick (mtv=0) ──────────────────────────────

TEST_CASE("encode_request_type/decode_request_type round-trip", "[request][REQ-SEQ-001]") {
    std::array<uint8_t, 7> params{0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77};
    const uint64_t ts = encode_request_type(RequestTypeOpcode::Compound, params);

    RequestTypeOpcode type{};
    std::array<uint8_t, 7> out_params{};
    auto ec = decode_request_type(/*mtv=*/false, ts, type, out_params);
    REQUIRE_FALSE(ec);
    REQUIRE(type == RequestTypeOpcode::Compound);
    REQUIRE(out_params == params);
}

TEST_CASE("decode_request_type rejects mtv=1 as not a repurposed slot", "[request][REQ-SEQ-001]") {
    RequestTypeOpcode type{};
    std::array<uint8_t, 7> out_params{};
    auto ec = decode_request_type(/*mtv=*/true, 0xFFFFFFFFFFFFFFFFull, type, out_params);
    REQUIRE(ec == make_error_code(RequestErrc::timestamp_not_repurposed));
}

TEST_CASE("decode_request_type rejects an unrecognized opcode byte", "[request][REQ-SEQ-001]") {
    RequestTypeOpcode type{};
    std::array<uint8_t, 7> out_params{};
    // 0x02 is not one of the 11 defined opcodes.
    const uint64_t ts = uint64_t{0x02} << 56;
    auto ec = decode_request_type(/*mtv=*/false, ts, type, out_params);
    REQUIRE(ec == make_error_code(RequestErrc::unknown_request_type));
}

TEST_CASE("decode_request_type rejects an MSB-set byte that is not one of the three defined "
          "safety opcodes",
          "[request][REQ-CMP-002]") {
    RequestTypeOpcode type{};
    std::array<uint8_t, 7> out_params{};
    auto ec = decode_request_type(/*mtv=*/false, uint64_t{0x80} << 56, type, out_params);
    REQUIRE(ec == make_error_code(RequestErrc::unknown_request_type));
}

TEST_CASE("make_conditional_request always sets ACF_GBB with mtv clear", "[request][REQ-SEQ-001]") {
    auto info = make_conditional_request(/*bus_id=*/7, /*transaction_num=*/9, /*cs=*/true);
    REQUIRE(info.acf_msg_type == rcp::acf::kAcfMsgTypeGbb);
    REQUIRE_FALSE(info.mtv);
    REQUIRE(info.byte_bus_id == 7);
    REQUIRE(info.transaction_num == 9);
    REQUIRE(info.cs);
}

// ── request_type classification helpers ───────────────────────────────────────

TEST_CASE("is_safety_variant identifies exactly the three 0x8x opcodes", "[request][REQ-CMP-001]") {
    REQUIRE(is_safety_variant(RequestTypeOpcode::CompoundSafety));
    REQUIRE(is_safety_variant(RequestTypeOpcode::CompoundWaitSafety));
    REQUIRE(is_safety_variant(RequestTypeOpcode::TriggeredSafety));

    REQUIRE_FALSE(is_safety_variant(RequestTypeOpcode::Compound));
    REQUIRE_FALSE(is_safety_variant(RequestTypeOpcode::CompoundWait));
    REQUIRE_FALSE(is_safety_variant(RequestTypeOpcode::Triggered));
    REQUIRE_FALSE(is_safety_variant(RequestTypeOpcode::Chained));
    REQUIRE_FALSE(is_safety_variant(RequestTypeOpcode::ClearAll));
}

TEST_CASE("is_compound recognizes Compound and CompoundSafety only", "[request][REQ-CMP-002]") {
    REQUIRE(is_compound(RequestTypeOpcode::Compound));
    REQUIRE(is_compound(RequestTypeOpcode::CompoundSafety));
    REQUIRE_FALSE(is_compound(RequestTypeOpcode::CompoundWait));
    REQUIRE_FALSE(is_compound(RequestTypeOpcode::CompoundWaitSafety));
    REQUIRE_FALSE(is_compound(RequestTypeOpcode::Chained));
}

TEST_CASE("is_compound_wait recognizes CompoundWait and CompoundWaitSafety only", "[request][REQ-CMP-003]") {
    REQUIRE(is_compound_wait(RequestTypeOpcode::CompoundWait));
    REQUIRE(is_compound_wait(RequestTypeOpcode::CompoundWaitSafety));
    REQUIRE_FALSE(is_compound_wait(RequestTypeOpcode::Compound));
    REQUIRE_FALSE(is_compound_wait(RequestTypeOpcode::CompoundSafety));
}

TEST_CASE("is_triggered recognizes Triggered and TriggeredSafety only", "[request][REQ-TRIG-001]") {
    REQUIRE(is_triggered(RequestTypeOpcode::Triggered));
    REQUIRE(is_triggered(RequestTypeOpcode::TriggeredSafety));
    REQUIRE_FALSE(is_triggered(RequestTypeOpcode::Compound));
    REQUIRE_FALSE(is_triggered(RequestTypeOpcode::Chained));
}

TEST_CASE("category_of maps every opcode to its documented category", "[request][REQ-SCHED-002]") {
    REQUIRE(category_of(std::nullopt) == RequestCategory::Standard);
    REQUIRE(category_of(RequestTypeOpcode::ClearAll) == RequestCategory::Cancellation);
    REQUIRE(category_of(RequestTypeOpcode::ClearNonSafestate) == RequestCategory::Cancellation);
    REQUIRE(category_of(RequestTypeOpcode::ClearSingle) == RequestCategory::Cancellation);
    REQUIRE(category_of(RequestTypeOpcode::Triggered) == RequestCategory::Triggered);
    REQUIRE(category_of(RequestTypeOpcode::Timed) == RequestCategory::Timed);
    REQUIRE(category_of(RequestTypeOpcode::Compound) == RequestCategory::Compound);
    REQUIRE(category_of(RequestTypeOpcode::CompoundWait) == RequestCategory::CompoundWait);
    REQUIRE(category_of(RequestTypeOpcode::Chained) == RequestCategory::Chained);
    REQUIRE(category_of(RequestTypeOpcode::CompoundSafety) == RequestCategory::Compound);
    REQUIRE(category_of(RequestTypeOpcode::CompoundWaitSafety) == RequestCategory::CompoundWait);
    REQUIRE(category_of(RequestTypeOpcode::TriggeredSafety) == RequestCategory::Triggered);
}

TEST_CASE("priority_rank orders categories cancellation..standard", "[request][REQ-SCHED-002]") {
    REQUIRE(priority_rank(RequestCategory::Cancellation) < priority_rank(RequestCategory::Triggered));
    REQUIRE(priority_rank(RequestCategory::Triggered) < priority_rank(RequestCategory::Timed));
    REQUIRE(priority_rank(RequestCategory::Timed) < priority_rank(RequestCategory::Compound));
    REQUIRE(priority_rank(RequestCategory::Compound) < priority_rank(RequestCategory::CompoundWait));
    REQUIRE(priority_rank(RequestCategory::CompoundWait) < priority_rank(RequestCategory::Chained));
    REQUIRE(priority_rank(RequestCategory::Chained) < priority_rank(RequestCategory::Standard));
}

TEST_CASE("select_next_due picks the highest-priority category regardless of arrival order",
          "[request][REQ-SCHED-003]") {
    std::vector<DueCandidate> due{
        {RequestCategory::Standard, 0},
        {RequestCategory::Chained, 1},
        {RequestCategory::Cancellation, 2},
    };
    auto winner = select_next_due(due);
    REQUIRE(winner.has_value());
    REQUIRE(*winner == 2);
}

TEST_CASE("select_next_due breaks ties within a category by FIFO arrival order", "[request][REQ-SCHED-003]") {
    std::vector<DueCandidate> due{
        {RequestCategory::Timed, 5},
        {RequestCategory::Timed, 2},
        {RequestCategory::Timed, 9},
    };
    auto winner = select_next_due(due);
    REQUIRE(winner.has_value());
    REQUIRE(*winner == 1);
}

TEST_CASE("select_next_due returns nullopt for an empty candidate set", "[request][REQ-SCHED-003]") {
    REQUIRE_FALSE(select_next_due({}).has_value());
}

// ── frame_timing_consistent (REQ-SCHED-007/008) ───────────────────────────────

TEST_CASE("frame_timing_consistent is trivially true for NTSCF frames", "[request][REQ-SCHED-007]") {
    REQUIRE(frame_timing_consistent(/*is_tscf=*/false, {true, false, true}));
    REQUIRE(frame_timing_consistent(/*is_tscf=*/false, {}));
}

TEST_CASE("frame_timing_consistent is trivially true for an empty member list", "[request][REQ-SCHED-007]") {
    REQUIRE(frame_timing_consistent(/*is_tscf=*/true, {}));
}

TEST_CASE("frame_timing_consistent accepts a uniform TSCF frame", "[request][REQ-SCHED-007]") {
    REQUIRE(frame_timing_consistent(/*is_tscf=*/true, {true, true, true}));
    REQUIRE(frame_timing_consistent(/*is_tscf=*/true, {false, false}));
}

TEST_CASE("frame_timing_consistent rejects a TSCF frame mixing timed and untimed members",
          "[request][REQ-SCHED-008]") {
    REQUIRE_FALSE(frame_timing_consistent(/*is_tscf=*/true, {true, false}));
    REQUIRE_FALSE(frame_timing_consistent(/*is_tscf=*/true, {false, true, false}));
}

// ── The `cs` field's one remaining meaning: chained abort-on-error ───────────
// cpp-RCP issue #58: should_execute_chained()'s cs polarity was inverted
// before this pass (old body: `return cs || !predecessor_errored;`, which
// never aborted a successor for cs=true no matter what). These tests pin
// the corrected, c-RCP-verified polarity: cs=false (CONTINUE_ON_ERROR)
// executes regardless; cs=true (ABORT_ON_ERROR) aborts iff the predecessor
// errored. Every REQUIRE below fails against the pre-pass body.

TEST_CASE("should_execute_chained: cs=false (continue-on-error) always executes regardless of "
          "predecessor outcome",
          "[request][REQ-CHAIN-010]") {
    REQUIRE(should_execute_chained(/*cs=*/false, /*predecessor_errored=*/true));
    REQUIRE(should_execute_chained(/*cs=*/false, /*predecessor_errored=*/false));
}

TEST_CASE("should_execute_chained: cs=true (abort-on-error) aborts only when the predecessor errored "
          "(cpp-RCP issue #58 regression pin)",
          "[request][REQ-CHAIN-010]") {
    REQUIRE_FALSE(should_execute_chained(/*cs=*/true, /*predecessor_errored=*/true));
    REQUIRE(should_execute_chained(/*cs=*/true, /*predecessor_errored=*/false));
}

// ── Compound / compound-wait (0x0F/0x8F, 0x0B/0x8B) ───────────────────────────

TEST_CASE("encode_compound_step_params/decode_compound_step_params round-trip at the "
          "specification's own sub-field offsets",
          "[request][REQ-CMP-010]") {
    CompoundStep step;
    step.start_state     = 3;
    step.next_state       = 5;
    step.sequencer_index  = 2;
    step.exec_delay        = 0x1234;
    step.repeat_count      = 0x5678;

    auto params = encode_compound_step_params(step);
    REQUIRE(params[0] == 3);
    REQUIRE(params[1] == 5);
    REQUIRE(params[2] == 2);
    REQUIRE(params[3] == 0x12);
    REQUIRE(params[4] == 0x34);
    REQUIRE(params[5] == 0x56);
    REQUIRE(params[6] == 0x78);

    auto decoded = decode_compound_step_params(params);
    REQUIRE(decoded.start_state == step.start_state);
    REQUIRE(decoded.next_state == step.next_state);
    REQUIRE(decoded.sequencer_index == step.sequencer_index);
    REQUIRE(decoded.exec_delay == step.exec_delay);
    REQUIRE(decoded.repeat_count == step.repeat_count);
}

TEST_CASE("kCompoundRepeatInfinite is the two-octet all-ones sentinel", "[request][REQ-CMP-010]") {
    REQUIRE(kCompoundRepeatInfinite == 0xFFFF);
}

TEST_CASE("compound request round-trips through encode_compound_request/decode_compound_request",
          "[request][REQ-CMP-011]") {
    CompoundStep step;
    step.start_state    = 1;
    step.next_state      = 2;
    step.sequencer_index = 0;
    step.exec_delay       = 10;
    step.repeat_count     = 3;
    std::vector<uint8_t> payload{0xAA, 0xBB};

    auto encoded = encode_compound_request(RequestTypeOpcode::Compound, /*byte_bus_id=*/5, step,
                                             /*evt_op=*/0, /*transaction_num=*/9, payload);

    CompoundRequest out;
    auto ec = decode_compound_request(encoded.data(), encoded.size(), out);
    REQUIRE_FALSE(ec);
    REQUIRE(out.type == RequestTypeOpcode::Compound);
    REQUIRE(out.byte_bus_id == 5);
    REQUIRE(out.step.start_state == 1);
    REQUIRE(out.step.next_state == 2);
    REQUIRE(out.step.exec_delay == 10);
    REQUIRE(out.step.repeat_count == 3);
    REQUIRE(out.transaction_num == 9);
    REQUIRE(out.payload == payload);
}

TEST_CASE("compound-wait safety request round-trips, evt_op independent of step sub-fields",
          "[request][REQ-CMP-026]") {
    CompoundStep step;
    step.start_state    = 4;
    step.next_state      = 0; // "leave it where it is" sentinel
    step.sequencer_index = 1;

    auto encoded = encode_compound_request(RequestTypeOpcode::CompoundWaitSafety, 2, step,
                                             /*evt_op=*/0x5, /*transaction_num=*/1);

    CompoundRequest out;
    REQUIRE_FALSE(decode_compound_request(encoded.data(), encoded.size(), out));
    REQUIRE(out.type == RequestTypeOpcode::CompoundWaitSafety);
    REQUIRE(out.evt_op == 0x5);
    REQUIRE(out.step.next_state == 0);
}

TEST_CASE("decode_compound_request rejects a request_type it does not recognize", "[request][REQ-CMP-014]") {
    // Encode a Chained request, then try to decode it as compound/compound-wait.
    auto encoded = encode_chained_member(1, 0, false, 1);
    CompoundRequest out;
    auto ec = decode_compound_request(encoded.data(), encoded.size(), out);
    REQUIRE(ec == make_error_code(RequestErrc::unknown_request_type));
}

TEST_CASE("decode_compound_request rejects a non-repurposed (mtv=1) message", "[request][REQ-CMP-013]") {
    rcp::acf::AcfMessageInfo info;
    info.byte_bus_id = 1;
    info.mtv          = true;
    auto encoded = rcp::acf::encode_acf_gbb(info, 0x1122334455667788ull, {});

    CompoundRequest out;
    auto ec = decode_compound_request(encoded.data(), encoded.size(), out);
    REQUIRE(ec == make_error_code(RequestErrc::timestamp_not_repurposed));
}

TEST_CASE("decode_compound_request rejects a short frame", "[request][REQ-CMP-011]") {
    // A genuine ACF_GBB-typed prefix (byte 0 correctly identifies GBB), but
    // truncated below the 16-byte ACF_GBB Message Info block.
    CompoundStep step;
    auto full = encode_compound_request(RequestTypeOpcode::Compound, 1, step, 0, 1);
    std::vector<uint8_t> too_short(full.begin(), full.begin() + 4);
    CompoundRequest out;
    auto ec = decode_compound_request(too_short.data(), too_short.size(), out);
    REQUIRE(ec == rcp::avtp::make_error_code(rcp::avtp::AvtpErrc::short_buffer));
}

TEST_CASE("decode_compound_request rejects a non-ACF_GBB message", "[request][REQ-CMP-012]") {
    rcp::acf::AcfMessageInfo info;
    auto encoded = rcp::acf::encode_acf_abb(info, {});
    CompoundRequest out;
    auto ec = decode_compound_request(encoded.data(), encoded.size(), out);
    REQUIRE(ec == rcp::acf::make_error_code(rcp::acf::AcfErrc::bad_acf_msg_type));
}

// ── clear-non-safestate (0x06) ────────────────────────────────────────────────

TEST_CASE("clear-non-safestate round-trips through encode/decode", "[request][REQ-CMP-016]") {
    auto encoded = encode_clear_non_safestate(/*byte_bus_id=*/3, /*transaction_num=*/7);
    ClearNonSafestateRequest out;
    REQUIRE_FALSE(decode_clear_non_safestate(encoded.data(), encoded.size(), out));
    REQUIRE(out.byte_bus_id == 3);
    REQUIRE(out.transaction_num == 7);
}

TEST_CASE("clear-non-safestate decode rejects a compound request's opcode", "[request][REQ-CMP-018]") {
    CompoundStep step;
    auto encoded = encode_compound_request(RequestTypeOpcode::Compound, 1, step, 0, 1);
    ClearNonSafestateRequest out;
    auto ec = decode_clear_non_safestate(encoded.data(), encoded.size(), out);
    REQUIRE(ec == make_error_code(RequestErrc::unknown_request_type));
}

TEST_CASE("clear-non-safestate decode rejects a nonzero reserved octet", "[request][REQ-CMP-028]") {
    rcp::acf::AcfMessageInfo info;
    info.byte_bus_id = 1;
    // opcode 0x06, one reserved octet set.
    const uint64_t ts = (uint64_t{0x06} << 56) | (uint64_t{1} << 40);
    auto encoded = rcp::acf::encode_acf_gbb(info, ts, {});
    ClearNonSafestateRequest out;
    auto ec = decode_clear_non_safestate(encoded.data(), encoded.size(), out);
    REQUIRE(ec == make_error_code(RequestErrc::reserved_field_nonzero));
}

TEST_CASE("clear-non-safestate decode rejects nonzero evt/hs/cs", "[request][REQ-CMP-029]") {
    rcp::acf::AcfMessageInfo info;
    info.byte_bus_id = 1;
    info.hs           = true;
    const uint64_t ts = uint64_t{0x06} << 56;
    auto encoded = rcp::acf::encode_acf_gbb(info, ts, {});
    ClearNonSafestateRequest out;
    auto ec = decode_clear_non_safestate(encoded.data(), encoded.size(), out);
    REQUIRE(ec == make_error_code(RequestErrc::evt_hs_cs_nonzero));
}

// ── Optional-feature bundling ─────────────────────────────────────────────────

TEST_CASE("validate_feature_bundles accepts a feature set with nothing conditional enabled",
          "[request][REQ-SEQ-003]") {
    FeatureSet f;
    REQUIRE_FALSE(validate_feature_bundles(f));
}

TEST_CASE("validate_feature_bundles accepts triggered/chained/timed independently of compound",
          "[request][REQ-SEQ-003]") {
    FeatureSet f;
    f.triggered = true;
    f.chained   = true;
    f.timed     = true;
    REQUIRE_FALSE(validate_feature_bundles(f));
}

TEST_CASE("validate_feature_bundles rejects compound claimed alone", "[request][REQ-SEQ-003]") {
    FeatureSet f;
    f.compound = true;
    REQUIRE(validate_feature_bundles(f) == make_error_code(RequestErrc::compound_bundle_incomplete));
}

TEST_CASE("validate_feature_bundles rejects compound_wait claimed alone", "[request][REQ-SEQ-003]") {
    FeatureSet f;
    f.compound_wait = true;
    REQUIRE(validate_feature_bundles(f) == make_error_code(RequestErrc::compound_bundle_incomplete));
}

TEST_CASE("validate_feature_bundles rejects compound+compound_wait with fewer than 4 sequencers",
          "[request][REQ-SEQ-003]") {
    FeatureSet f;
    f.compound            = true;
    f.compound_wait       = true;
    f.clear_non_safestate = true;
    f.sequencer_count     = kMinCompoundSequencers - 1;
    REQUIRE(validate_feature_bundles(f) == make_error_code(RequestErrc::compound_bundle_incomplete));
}

TEST_CASE("validate_feature_bundles rejects compound+compound_wait without clear_non_safestate",
          "[request][REQ-SEQ-003]") {
    FeatureSet f;
    f.compound        = true;
    f.compound_wait   = true;
    f.sequencer_count = kMinCompoundSequencers;
    REQUIRE(validate_feature_bundles(f) == make_error_code(RequestErrc::compound_bundle_incomplete));
}

TEST_CASE("validate_feature_bundles accepts the complete compound bundle", "[request][REQ-SEQ-003]") {
    FeatureSet f;
    f.compound            = true;
    f.compound_wait       = true;
    f.clear_non_safestate = true;
    f.sequencer_count     = kMinCompoundSequencers;
    REQUIRE_FALSE(validate_feature_bundles(f));
}

TEST_CASE("implemented_options_bits reports kOptConditionalRequests only when something is enabled",
          "[request][REQ-SEQ-003]") {
    FeatureSet none;
    REQUIRE(implemented_options_bits(none) == 0);

    FeatureSet triggered_only;
    triggered_only.triggered = true;
    REQUIRE(implemented_options_bits(triggered_only) == rcp::regmap::kOptConditionalRequests);
}

// ── Sequencer-state registers ─────────────────────────────────────────────────

TEST_CASE("SequencerTable::ensure_size fills new slots with kDefaultState (1), not 0",
          "[request][REQ-SEQ-004]") {
    std::vector<rcp::regmap::SequencerState> states;
    SequencerTable table(states);
    table.ensure_size(4);
    REQUIRE(table.size() == 4);
    for (size_t i = 0; i < 4; ++i) {
        rcp::regmap::SequencerState s = 0;
        REQUIRE_FALSE(table.state_of(i, s));
        REQUIRE(s == SequencerTable::kDefaultState);
    }
}

TEST_CASE("SequencerTable::ensure_size never shrinks existing storage", "[request][REQ-SEQ-004]") {
    std::vector<rcp::regmap::SequencerState> states{5, 5, 5};
    SequencerTable table(states);
    table.ensure_size(1);
    REQUIRE(table.size() == 3);
}

TEST_CASE("SequencerTable::set_state overwrites a valid index", "[request][REQ-SEQ-010]") {
    std::vector<rcp::regmap::SequencerState> states{1};
    SequencerTable table(states);
    REQUIRE_FALSE(table.set_state(0, 9));
    rcp::regmap::SequencerState s = 0;
    REQUIRE_FALSE(table.state_of(0, s));
    REQUIRE(s == 9);
}

TEST_CASE("SequencerTable::set_state rejects an invalid index without changing the table",
          "[request][REQ-SEQ-011]") {
    std::vector<rcp::regmap::SequencerState> states{1};
    SequencerTable table(states);
    REQUIRE(table.set_state(5, 9) == make_error_code(RequestErrc::index_out_of_range));
    rcp::regmap::SequencerState s = 0;
    REQUIRE_FALSE(table.state_of(0, s));
    REQUIRE(s == 1);
}

TEST_CASE("SequencerTable reports index_out_of_range for an out-of-bounds index", "[request][REQ-SEQ-007]") {
    std::vector<rcp::regmap::SequencerState> states{1};
    SequencerTable table(states);
    rcp::regmap::SequencerState s = 0;
    REQUIRE(table.state_of(5, s) == make_error_code(RequestErrc::index_out_of_range));
}

TEST_CASE("SequencerTable can be constructed directly over a RegisterMap's sequencer_states",
          "[request][REQ-SEQ-004]") {
    rcp::regmap::RegisterMap regs;
    SequencerTable table(regs);
    table.ensure_size(4);
    REQUIRE(regs.sequencer_states.size() == 4);
    REQUIRE(regs.sequencer_states[0] == SequencerTable::kDefaultState);
}

// ── advance_guard / start_condition_met (delta #4 — c-RCP's two DIFFERENT
// predicates, replacing the pre-pass unconditional-+1 try_advance) ──────────

TEST_CASE("advance_guard is true when the sequencer is in start_state", "[request][REQ-CMP-019]") {
    std::vector<rcp::regmap::SequencerState> states{3};
    SequencerTable table(states);
    CompoundStep step;
    step.start_state    = 3;
    step.sequencer_index = 0;
    REQUIRE(table.advance_guard(step));
}

TEST_CASE("advance_guard is false when the sequencer is not in start_state", "[request][REQ-CMP-019]") {
    std::vector<rcp::regmap::SequencerState> states{3};
    SequencerTable table(states);
    CompoundStep step;
    step.start_state    = 4;
    step.sequencer_index = 0;
    REQUIRE_FALSE(table.advance_guard(step));
}

TEST_CASE("advance_guard is false for an invalid sequencer index", "[request][REQ-CMP-019]") {
    std::vector<rcp::regmap::SequencerState> states{3};
    SequencerTable table(states);
    CompoundStep step;
    step.start_state    = 3;
    step.sequencer_index = 5;
    REQUIRE_FALSE(table.advance_guard(step));
}

TEST_CASE("advance_guard is false when the sequencer is disabled (state 0), even if start_state is 0",
          "[request][REQ-SEQ-012]") {
    std::vector<rcp::regmap::SequencerState> states{0};
    SequencerTable table(states);
    CompoundStep step;
    step.start_state    = 0;
    step.sequencer_index = 0;
    REQUIRE_FALSE(table.advance_guard(step));
}

TEST_CASE("start_condition_met: start_state==0 matches any enabled state", "[request][REQ-CMP-025]") {
    std::vector<rcp::regmap::SequencerState> states{42};
    SequencerTable table(states);
    CompoundStep step;
    step.start_state    = 0;
    step.sequencer_index = 0;
    REQUIRE(table.start_condition_met(step));
}

TEST_CASE("start_condition_met: nonzero start_state requires an exact match", "[request][REQ-CMP-025]") {
    std::vector<rcp::regmap::SequencerState> states{5};
    SequencerTable table(states);
    CompoundStep step;
    step.start_state    = 6;
    step.sequencer_index = 0;
    REQUIRE_FALSE(table.start_condition_met(step));

    step.start_state = 5;
    REQUIRE(table.start_condition_met(step));
}

TEST_CASE("start_condition_met is false for an unknown sequencer", "[request][REQ-CMP-025]") {
    std::vector<rcp::regmap::SequencerState> states{5};
    SequencerTable table(states);
    CompoundStep step;
    step.sequencer_index = 9;
    REQUIRE_FALSE(table.start_condition_met(step));
}

TEST_CASE("start_condition_met is false when disabled, even with the start_state==0 wildcard",
          "[request][REQ-SEQ-012]") {
    std::vector<rcp::regmap::SequencerState> states{0};
    SequencerTable table(states);
    CompoundStep step;
    step.start_state    = 0;
    step.sequencer_index = 0;
    REQUIRE_FALSE(table.start_condition_met(step));
}

TEST_CASE("SequencerTable::exec_delay_elapsed compares elapsed against exec_delay", "[request][REQ-CMP-020]") {
    CompoundStep step;
    step.exec_delay = 100;
    REQUIRE_FALSE(SequencerTable::exec_delay_elapsed(step, 99));
    REQUIRE(SequencerTable::exec_delay_elapsed(step, 100));
    REQUIRE(SequencerTable::exec_delay_elapsed(step, 101));
}

TEST_CASE("SequencerTable::tick advances only once both the delay has elapsed and the guard holds",
          "[request][REQ-CMP-021]") {
    std::vector<rcp::regmap::SequencerState> states{1};
    SequencerTable table(states);
    CompoundStep step;
    step.start_state    = 1;
    step.next_state      = 2;
    step.sequencer_index = 0;
    step.exec_delay       = 10;

    REQUIRE_FALSE(table.tick(step, 5)); // delay not elapsed yet
    rcp::regmap::SequencerState s = 0;
    REQUIRE_FALSE(table.state_of(0, s));
    REQUIRE(s == 1);

    REQUIRE(table.tick(step, 10));
    REQUIRE_FALSE(table.state_of(0, s));
    REQUIRE(s == 2);
}

TEST_CASE("SequencerTable::tick never advances a sequencer that has already left start_state, "
          "even after its delay elapses",
          "[request][REQ-CMP-022]") {
    std::vector<rcp::regmap::SequencerState> states{9}; // not start_state
    SequencerTable table(states);
    CompoundStep step;
    step.start_state    = 1;
    step.next_state      = 2;
    step.sequencer_index = 0;
    step.exec_delay       = 0;

    REQUIRE_FALSE(table.tick(step, 100));
    rcp::regmap::SequencerState s = 0;
    REQUIRE_FALSE(table.state_of(0, s));
    REQUIRE(s == 9);
}

TEST_CASE("SequencerTable::wait_tick advances only on condition_met and guard both holding",
          "[request][REQ-CMP-023]") {
    std::vector<rcp::regmap::SequencerState> states{1};
    SequencerTable table(states);
    CompoundStep step;
    step.start_state    = 1;
    step.next_state      = 2;
    step.sequencer_index = 0;

    REQUIRE_FALSE(table.wait_tick(step, /*condition_met=*/false));
    rcp::regmap::SequencerState s = 0;
    REQUIRE_FALSE(table.state_of(0, s));
    REQUIRE(s == 1);

    REQUIRE(table.wait_tick(step, /*condition_met=*/true));
    REQUIRE_FALSE(table.state_of(0, s));
    REQUIRE(s == 2);
}

TEST_CASE("SequencerTable::wait_tick never advances a sequencer that has already left start_state, "
          "even on a condition match",
          "[request][REQ-CMP-024]") {
    std::vector<rcp::regmap::SequencerState> states{9};
    SequencerTable table(states);
    CompoundStep step;
    step.start_state    = 1;
    step.next_state      = 2;
    step.sequencer_index = 0;

    REQUIRE_FALSE(table.wait_tick(step, /*condition_met=*/true));
    rcp::regmap::SequencerState s = 0;
    REQUIRE_FALSE(table.state_of(0, s));
    REQUIRE(s == 9);
}

TEST_CASE("SequencerTable::tick with next_state==0 leaves the sequencer unchanged but still "
          "reports success",
          "[request][REQ-CMP-021]") {
    std::vector<rcp::regmap::SequencerState> states{1};
    SequencerTable table(states);
    CompoundStep step;
    step.start_state    = 1;
    step.next_state      = 0; // "remain in the current state" sentinel
    step.sequencer_index = 0;

    REQUIRE(table.tick(step, 0));
    rcp::regmap::SequencerState s = 0;
    REQUIRE_FALSE(table.state_of(0, s));
    REQUIRE(s == 1); // unchanged, NOT driven to 0
}

TEST_CASE("SequencerTable::wait_tick with next_state==0 leaves the sequencer unchanged but still "
          "reports success",
          "[request][REQ-CMP-023]") {
    std::vector<rcp::regmap::SequencerState> states{7};
    SequencerTable table(states);
    CompoundStep step;
    step.start_state    = 7;
    step.next_state      = 0;
    step.sequencer_index = 0;

    REQUIRE(table.wait_tick(step, true));
    rcp::regmap::SequencerState s = 0;
    REQUIRE_FALSE(table.state_of(0, s));
    REQUIRE(s == 7);
}

// ── Triggered (0x0E/0x8E) ──────────────────────────────────────────────────────

TEST_CASE("encode_triggered_step_params/decode_triggered_step_params round-trip", "[request][REQ-TRIG-004]") {
    TriggeredStep step;
    step.trigger_source_ep  = 4;
    step.trigger_signal_nr  = 2;
    step.trigger_threshold  = 1;
    step.exec_delay          = 0x0102;
    step.repeat_count        = kTriggeredRepeatInfinite;

    auto params = encode_triggered_step_params(step);
    REQUIRE(params[0] == 4);
    REQUIRE(params[1] == 2);
    REQUIRE(params[2] == 1);
    REQUIRE(params[3] == 0x01);
    REQUIRE(params[4] == 0x02);
    REQUIRE(params[5] == 0xFF);
    REQUIRE(params[6] == 0xFF);

    auto decoded = decode_triggered_step_params(params);
    REQUIRE(decoded.trigger_source_ep == step.trigger_source_ep);
    REQUIRE(decoded.trigger_signal_nr == step.trigger_signal_nr);
    REQUIRE(decoded.trigger_threshold == step.trigger_threshold);
    REQUIRE(decoded.exec_delay == step.exec_delay);
    REQUIRE(decoded.repeat_count == step.repeat_count);
}

TEST_CASE("triggered request round-trips through encode_triggered_request/decode_triggered_request",
          "[request][REQ-TRIG-005]") {
    TriggeredStep step;
    step.trigger_source_ep = 1;
    step.trigger_signal_nr = 3;
    step.trigger_threshold = 0;
    step.exec_delay          = 5;
    std::vector<uint8_t> payload{0x01};

    auto encoded = encode_triggered_request(RequestTypeOpcode::Triggered, 2, step, 11, payload);
    TriggeredRequest out;
    REQUIRE_FALSE(decode_triggered_request(encoded.data(), encoded.size(), out));
    REQUIRE(out.type == RequestTypeOpcode::Triggered);
    REQUIRE(out.byte_bus_id == 2);
    REQUIRE(out.step.trigger_source_ep == 1);
    REQUIRE(out.step.trigger_signal_nr == 3);
    REQUIRE(out.transaction_num == 11);
    REQUIRE(out.payload == payload);
}

TEST_CASE("triggered safety opcode (0x8E) round-trips", "[request][REQ-TRIG-005]") {
    TriggeredStep step;
    auto encoded = encode_triggered_request(RequestTypeOpcode::TriggeredSafety, 1, step, 1);
    REQUIRE(static_cast<uint8_t>(RequestTypeOpcode::TriggeredSafety) == 0x8E);

    TriggeredRequest out;
    REQUIRE_FALSE(decode_triggered_request(encoded.data(), encoded.size(), out));
    REQUIRE(out.type == RequestTypeOpcode::TriggeredSafety);
}

TEST_CASE("decode_triggered_request rejects a request_type it does not recognize", "[request][REQ-TRIG-007]") {
    auto encoded = encode_chained_member(1, 0, false, 1);
    TriggeredRequest out;
    auto ec = decode_triggered_request(encoded.data(), encoded.size(), out);
    REQUIRE(ec == make_error_code(RequestErrc::unknown_request_type));
}

TEST_CASE("decode_triggered_request rejects a non-repurposed message_timestamp", "[request][REQ-TRIG-006]") {
    rcp::acf::AcfMessageInfo info;
    info.mtv = true;
    auto encoded = rcp::acf::encode_acf_gbb(info, 0x0Eull << 56, {});
    TriggeredRequest out;
    auto ec = decode_triggered_request(encoded.data(), encoded.size(), out);
    REQUIRE(ec == make_error_code(RequestErrc::timestamp_not_repurposed));
}

TEST_CASE("triggered_enter_started resets the occurrence counter", "[request][REQ-TRIG-008]") {
    TriggeredRuntime rt;
    rt.occurrence_count = 9;
    rt.started            = false;
    triggered_enter_started(rt);
    REQUIRE(rt.occurrence_count == 0);
    REQUIRE(rt.started);
}

TEST_CASE("triggered_record_occurrence requires started", "[request][REQ-TRIG-009]") {
    TriggeredRuntime rt;
    TriggeredStep step;
    step.trigger_source_ep = 1;
    step.trigger_signal_nr = 2;
    REQUIRE_FALSE(triggered_record_occurrence(rt, step, 1, 2));
    REQUIRE(rt.occurrence_count == 0);
}

TEST_CASE("triggered_record_occurrence only counts the selected trigger", "[request][REQ-TRIG-009]") {
    TriggeredRuntime rt;
    triggered_enter_started(rt);
    TriggeredStep step;
    step.trigger_source_ep = 1;
    step.trigger_signal_nr = 2;

    REQUIRE_FALSE(triggered_record_occurrence(rt, step, /*source_ep=*/9, /*signal_nr=*/2));
    REQUIRE_FALSE(triggered_record_occurrence(rt, step, /*source_ep=*/1, /*signal_nr=*/9));
    REQUIRE(rt.occurrence_count == 0);

    REQUIRE(triggered_record_occurrence(rt, step, 1, 2));
    REQUIRE(rt.occurrence_count == 1);
}

TEST_CASE("triggered_threshold_reached counts occurrences that must precede execution",
          "[request][REQ-TRIG-010]") {
    TriggeredStep step;
    step.trigger_threshold = 1; // fires on the 2nd occurrence
    TriggeredRuntime rt;
    rt.occurrence_count = 1;
    REQUIRE_FALSE(triggered_threshold_reached(step, rt));
    rt.occurrence_count = 2;
    REQUIRE(triggered_threshold_reached(step, rt));

    step.trigger_threshold = 0; // fires on the 1st occurrence
    rt.occurrence_count      = 1;
    REQUIRE(triggered_threshold_reached(step, rt));
}

TEST_CASE("triggered_exec_delay_elapsed compares elapsed against exec_delay", "[request][REQ-TRIG-011]") {
    TriggeredStep step;
    step.exec_delay = 50;
    REQUIRE_FALSE(triggered_exec_delay_elapsed(step, 49));
    REQUIRE(triggered_exec_delay_elapsed(step, 50));
}

TEST_CASE("triggered_tick requires started and the threshold reached", "[request][REQ-TRIG-012]") {
    TriggeredStep step;
    step.trigger_threshold = 0;
    TriggeredRuntime rt;
    REQUIRE_FALSE(triggered_tick(step, rt, 0, true)); // not started

    triggered_enter_started(rt);
    REQUIRE_FALSE(triggered_tick(step, rt, 0, true)); // threshold not reached (0 occurrences)
}

TEST_CASE("triggered_tick fires only when the endpoint is idle", "[request][REQ-TRIG-012]") {
    TriggeredStep step;
    step.trigger_threshold = 0;
    TriggeredRuntime rt;
    triggered_enter_started(rt);
    triggered_record_occurrence(rt, step, 0, 0);

    REQUIRE_FALSE(triggered_tick(step, rt, 0, /*endpoint_idle=*/false));
    REQUIRE(rt.started); // unchanged

    REQUIRE(triggered_tick(step, rt, 0, /*endpoint_idle=*/true));
}

TEST_CASE("triggered_tick is blocked until exec_delay elapses", "[request][REQ-TRIG-012]") {
    TriggeredStep step;
    step.trigger_threshold = 0;
    step.exec_delay          = 100;
    TriggeredRuntime rt;
    triggered_enter_started(rt);
    triggered_record_occurrence(rt, step, 0, 0);

    REQUIRE_FALSE(triggered_tick(step, rt, 50, true));
    REQUIRE(triggered_tick(step, rt, 100, true));
}

TEST_CASE("triggered_tick resets the runtime on a successful fire", "[request][REQ-TRIG-013]") {
    TriggeredStep step;
    step.trigger_threshold = 0;
    TriggeredRuntime rt;
    triggered_enter_started(rt);
    triggered_record_occurrence(rt, step, 0, 0);

    REQUIRE(triggered_tick(step, rt, 0, true));
    REQUIRE(rt.occurrence_count == 0);
    REQUIRE_FALSE(rt.started);
}

TEST_CASE("the occurrence counter free-runs independent of endpoint_idle", "[request][REQ-TRIG-009]") {
    TriggeredStep step;
    TriggeredRuntime rt;
    triggered_enter_started(rt);
    // record_occurrence has no endpoint_idle parameter at all — occurrences
    // accumulate regardless of what a fire attempt's own idle flag would be.
    REQUIRE(triggered_record_occurrence(rt, step, 0, 0));
    REQUIRE(triggered_record_occurrence(rt, step, 0, 0));
    REQUIRE(rt.occurrence_count == 2);
}

// ── Chained (0x01) ─────────────────────────────────────────────────────────────

TEST_CASE("chained member round-trips through encode_chained_member/decode_chained_member",
          "[request][REQ-CHAIN-002]") {
    std::vector<uint8_t> payload{0x01, 0x02};
    auto encoded = encode_chained_member(/*byte_bus_id=*/4, /*chain_exec_delay=*/0x0102,
                                          /*cs=*/true, /*transaction_num=*/6, payload);

    ChainedMember out;
    REQUIRE_FALSE(decode_chained_member(encoded.data(), encoded.size(), out));
    REQUIRE(out.byte_bus_id == 4);
    REQUIRE(out.chain_exec_delay == 0x0102);
    REQUIRE(out.cs);
    REQUIRE(out.transaction_num == 6);
    REQUIRE(out.payload == payload);
}

TEST_CASE("chained member wire sub-field offsets: chain_exec_delay at octets 4..5, rest reserved",
          "[request][REQ-CHAIN-004]") {
    auto encoded = encode_chained_member(1, 0xBEEF, false, 1);
    // encoded[8] is the opcode byte (start of the repurposed region).
    REQUIRE(encoded[8] == 0x01);
    REQUIRE(encoded[9] == 0);
    REQUIRE(encoded[10] == 0);
    REQUIRE(encoded[11] == 0);
    REQUIRE(encoded[12] == 0xBE);
    REQUIRE(encoded[13] == 0xEF);
    REQUIRE(encoded[14] == 0);
    REQUIRE(encoded[15] == 0);
}

TEST_CASE("decode_chained_member rejects a non-chained opcode", "[request][REQ-CHAIN-007]") {
    auto encoded = encode_clear_all(1, 1);
    ChainedMember out;
    auto ec = decode_chained_member(encoded.data(), encoded.size(), out);
    REQUIRE(ec == make_error_code(RequestErrc::unknown_request_type));
}

TEST_CASE("decode_chained_member rejects a non-repurposed message_timestamp", "[request][REQ-CHAIN-006]") {
    rcp::acf::AcfMessageInfo info;
    info.mtv = true;
    auto encoded = rcp::acf::encode_acf_gbb(info, 0x01ull << 56, {});
    ChainedMember out;
    auto ec = decode_chained_member(encoded.data(), encoded.size(), out);
    REQUIRE(ec == make_error_code(RequestErrc::timestamp_not_repurposed));
}

TEST_CASE("decode_chained_member rejects a nonzero reserved sub-field octet", "[request][REQ-CHAIN-012]") {
    rcp::acf::AcfMessageInfo info;
    info.byte_bus_id = 1;
    const uint64_t ts = (uint64_t{0x01} << 56) | (uint64_t{1} << 8); // octet 6 set (reserved)
    auto encoded = rcp::acf::encode_acf_gbb(info, ts, {});
    ChainedMember out;
    auto ec = decode_chained_member(encoded.data(), encoded.size(), out);
    REQUIRE(ec == make_error_code(RequestErrc::reserved_field_nonzero));
}

TEST_CASE("chained_exec_delay_elapsed compares elapsed against chain_exec_delay", "[request][REQ-CHAIN-011]") {
    REQUIRE_FALSE(chained_exec_delay_elapsed(100, 99));
    REQUIRE(chained_exec_delay_elapsed(100, 100));
    REQUIRE(chained_exec_delay_elapsed(0, 0));
}

// ── Timed (0x0A) ───────────────────────────────────────────────────────────────

TEST_CASE("timed_feature_enabled requires kOptConditionalRequests", "[request][REQ-TIMED-002]") {
    REQUIRE_FALSE(timed_feature_enabled(0));
    REQUIRE(timed_feature_enabled(rcp::regmap::kOptConditionalRequests));
}

TEST_CASE("timed request round-trips through encode_timed_request/decode_timed_request",
          "[request][REQ-TIMED-003]") {
    std::vector<uint8_t> payload{0x01, 0x02, 0x03};
    auto encoded = encode_timed_request(/*byte_bus_id=*/2, /*presentation_time=*/0x0001020304050ull,
                                         /*transaction_num=*/8, payload);
    REQUIRE(encoded.has_value());

    TimedRequest out;
    REQUIRE_FALSE(decode_timed_request(encoded->data(), encoded->size(), out));
    REQUIRE(out.byte_bus_id == 2);
    REQUIRE(out.presentation_time == 0x0001020304050ull);
    REQUIRE(out.transaction_num == 8);
    REQUIRE(out.payload == payload);
}

TEST_CASE("timed request wire sub-field offsets: octet 1 reserved, octets 2..7 the 48-bit "
          "presentation_time",
          "[request][REQ-TIMED-003]") {
    auto encoded = encode_timed_request(1, kTimedPresentationTimeMax, 1);
    REQUIRE(encoded.has_value());
    REQUIRE((*encoded)[8] == 0x0A);
    REQUIRE((*encoded)[9] == 0); // reserved
    for (size_t i = 10; i < 16; ++i) REQUIRE((*encoded)[i] == 0xFF);
}

TEST_CASE("encode_timed_request rejects a presentation_time beyond the 48-bit max", "[request][REQ-TIMED-003]") {
    auto encoded = encode_timed_request(1, kTimedPresentationTimeMax + 1, 1);
    REQUIRE_FALSE(encoded.has_value());
}

TEST_CASE("decode_timed_request rejects a nonzero reserved octet", "[request][REQ-TIMED-009]") {
    rcp::acf::AcfMessageInfo info;
    info.byte_bus_id = 1;
    const uint64_t ts = (uint64_t{0x0A} << 56) | (uint64_t{1} << 48); // octet 1 (reserved) set
    auto encoded = rcp::acf::encode_acf_gbb(info, ts, {});
    TimedRequest out;
    auto ec = decode_timed_request(encoded.data(), encoded.size(), out);
    REQUIRE(ec == make_error_code(RequestErrc::reserved_field_nonzero));
}

TEST_CASE("decode_timed_request rejects hs or cs set", "[request][REQ-TIMED-010]") {
    rcp::acf::AcfMessageInfo info;
    info.byte_bus_id = 1;
    info.hs           = true;
    auto encoded1 = rcp::acf::encode_acf_gbb(info, uint64_t{0x0A} << 56, {});
    TimedRequest out;
    REQUIRE(decode_timed_request(encoded1.data(), encoded1.size(), out) ==
            make_error_code(RequestErrc::unsupported_cmd));

    rcp::acf::AcfMessageInfo info2;
    info2.byte_bus_id = 1;
    info2.cs           = true;
    auto encoded2 = rcp::acf::encode_acf_gbb(info2, uint64_t{0x0A} << 56, {});
    REQUIRE(decode_timed_request(encoded2.data(), encoded2.size(), out) ==
            make_error_code(RequestErrc::unsupported_cmd));
}

TEST_CASE("decode_timed_request rejects a non-timed opcode", "[request][REQ-TIMED-005]") {
    auto encoded = encode_clear_all(1, 1);
    TimedRequest out;
    auto ec = decode_timed_request(encoded.data(), encoded.size(), out);
    REQUIRE(ec == make_error_code(RequestErrc::unknown_request_type));
}

TEST_CASE("timed presentation_time round-trips beyond the 32-bit range", "[request][REQ-TIMED-003]") {
    const uint64_t big = 0x0000A0B0C0D0E0ull;
    auto encoded = encode_timed_request(1, big, 1);
    REQUIRE(encoded.has_value());
    TimedRequest out;
    REQUIRE_FALSE(decode_timed_request(encoded->data(), encoded->size(), out));
    REQUIRE(out.presentation_time == big);
}

TEST_CASE("timed_too_far rejects a presentation_time beyond the horizon", "[request][REQ-TIMED-006]") {
    REQUIRE(timed_too_far(/*presentation_time=*/1000, /*now=*/0, /*max_horizon=*/500));
    REQUIRE_FALSE(timed_too_far(500, 0, 500));
}

TEST_CASE("timed_too_far: a presentation_time in the past is never too far", "[request][REQ-TIMED-006]") {
    REQUIRE_FALSE(timed_too_far(/*presentation_time=*/0, /*now=*/1000, /*max_horizon=*/1));
}

TEST_CASE("timed_too_far is wraparound-safe in the 48-bit presentation-time domain",
          "[request][REQ-TIMED-006]") {
    // now near the top of the 48-bit domain, presentation_time just after
    // wraparound — forward delta should be small, not huge.
    const uint64_t now = kTimedPresentationTimeMax - 5;
    const uint64_t pt  = 10; // wraps to now+16
    REQUIRE_FALSE(timed_too_far(pt, now, 100));
    REQUIRE(timed_too_far(pt, now, 5));
}

TEST_CASE("timed_admit reports RejectGptpFail whenever gptp is not locked", "[request][REQ-TIMED-007]") {
    REQUIRE(timed_admit(/*gptp_locked=*/false, 1'000'000, 0, 1) == TimedAdmission::RejectGptpFail);
    // Even when the horizon would otherwise pass.
    REQUIRE(timed_admit(false, 0, 0, 1'000'000) == TimedAdmission::RejectGptpFail);
}

TEST_CASE("timed_admit reports RejectPresentationTimeTooFar or Accept once gPTP is locked",
          "[request][REQ-TIMED-008]") {
    REQUIRE(timed_admit(true, 1000, 0, 500) == TimedAdmission::RejectPresentationTimeTooFar);
    REQUIRE(timed_admit(true, 500, 0, 500) == TimedAdmission::Accept);
}

TEST_CASE("wire_error_for maps admission outcomes onto acf::WireErrorCode", "[request][REQ-TIMED-007]") {
    REQUIRE(wire_error_for(TimedAdmission::RejectGptpFail) == rcp::acf::WireErrorCode::GptpFail);
    REQUIRE(wire_error_for(TimedAdmission::RejectPresentationTimeTooFar) ==
            rcp::acf::WireErrorCode::PresentationTimeTooFar);
    REQUIRE_FALSE(wire_error_for(TimedAdmission::Accept).has_value());
}

TEST_CASE("timed_due reports whether a presentation_time has arrived", "[request][REQ-TIMED-011]") {
    REQUIRE_FALSE(timed_due(/*presentation_time=*/100, /*now=*/0));
    REQUIRE(timed_due(100, 100));
    REQUIRE(timed_due(100, 200)); // already past
}

// ── Cancellation: clear-all (0x05) and clear-single (0x07) ───────────────────

TEST_CASE("clear-all round-trips through encode_clear_all/decode_clear_all", "[request][REQ-CANCEL-002]") {
    auto encoded = encode_clear_all(/*byte_bus_id=*/3, /*transaction_num=*/4);
    ClearAllRequest out;
    REQUIRE_FALSE(decode_clear_all(encoded.data(), encoded.size(), out));
    REQUIRE(out.byte_bus_id == 3);
    REQUIRE(out.transaction_num == 4);
}

TEST_CASE("decode_clear_all rejects a clear-single opcode", "[request][REQ-CANCEL-004]") {
    auto encoded = encode_clear_single(1, 2, 3);
    ClearAllRequest out;
    auto ec = decode_clear_all(encoded.data(), encoded.size(), out);
    REQUIRE(ec == make_error_code(RequestErrc::unknown_request_type));
}

TEST_CASE("decode_clear_all rejects a short frame", "[request][REQ-CANCEL-003]") {
    auto full = encode_clear_all(1, 1);
    std::vector<uint8_t> too_short(full.begin(), full.begin() + 4);
    ClearAllRequest out;
    auto ec = decode_clear_all(too_short.data(), too_short.size(), out);
    REQUIRE(ec == rcp::avtp::make_error_code(rcp::avtp::AvtpErrc::short_buffer));
}

TEST_CASE("decode_clear_all rejects a nonzero reserved octet", "[request][REQ-CANCEL-013]") {
    rcp::acf::AcfMessageInfo info;
    info.byte_bus_id = 1;
    const uint64_t ts = (uint64_t{0x05} << 56) | 1;
    auto encoded = rcp::acf::encode_acf_gbb(info, ts, {});
    ClearAllRequest out;
    auto ec = decode_clear_all(encoded.data(), encoded.size(), out);
    REQUIRE(ec == make_error_code(RequestErrc::reserved_field_nonzero));
}

TEST_CASE("decode_clear_all rejects nonzero evt/hs/cs", "[request][REQ-CANCEL-014]") {
    rcp::acf::AcfMessageInfo info;
    info.byte_bus_id = 1;
    info.evt_op        = 1;
    auto encoded = rcp::acf::encode_acf_gbb(info, uint64_t{0x05} << 56, {});
    ClearAllRequest out;
    auto ec = decode_clear_all(encoded.data(), encoded.size(), out);
    REQUIRE(ec == make_error_code(RequestErrc::evt_hs_cs_nonzero));
}

TEST_CASE("clear-single round-trips through encode_clear_single/decode_clear_single", "[request][REQ-CANCEL-005]") {
    auto encoded = encode_clear_single(/*byte_bus_id=*/2, /*clear_transaction_num=*/42, /*transaction_num=*/9);
    ClearSingleRequest out;
    REQUIRE_FALSE(decode_clear_single(encoded.data(), encoded.size(), out));
    REQUIRE(out.byte_bus_id == 2);
    REQUIRE(out.clear_transaction_num == 42);
    REQUIRE(out.transaction_num == 9);
}

TEST_CASE("clear-single wire sub-field offsets: clear_transaction_num at octet 3, rest reserved",
          "[request][REQ-CANCEL-005]") {
    auto encoded = encode_clear_single(1, 0x77, 1);
    REQUIRE(encoded[8] == 0x07);
    REQUIRE(encoded[9] == 0);
    REQUIRE(encoded[10] == 0);
    REQUIRE(encoded[11] == 0x77);
    REQUIRE(encoded[12] == 0);
    REQUIRE(encoded[13] == 0);
    REQUIRE(encoded[14] == 0);
    REQUIRE(encoded[15] == 0);
}

TEST_CASE("decode_clear_single rejects a clear-all opcode", "[request][REQ-CANCEL-006]") {
    auto encoded = encode_clear_all(1, 1);
    ClearSingleRequest out;
    auto ec = decode_clear_single(encoded.data(), encoded.size(), out);
    REQUIRE(ec == make_error_code(RequestErrc::unknown_request_type));
}

TEST_CASE("decode_clear_single rejects a nonzero reserved octet", "[request][REQ-CANCEL-007]") {
    rcp::acf::AcfMessageInfo info;
    info.byte_bus_id = 1;
    const uint64_t ts = (uint64_t{0x07} << 56) | (uint64_t{1} << 40); // octet 2 (reserved) set
    auto encoded = rcp::acf::encode_acf_gbb(info, ts, {});
    ClearSingleRequest out;
    auto ec = decode_clear_single(encoded.data(), encoded.size(), out);
    REQUIRE(ec == make_error_code(RequestErrc::reserved_field_nonzero));
}

TEST_CASE("decode_clear_single rejects nonzero evt/hs/cs", "[request][REQ-CANCEL-015]") {
    rcp::acf::AcfMessageInfo info;
    info.byte_bus_id = 1;
    info.cs            = true;
    const uint64_t ts = uint64_t{0x07} << 56;
    auto encoded = rcp::acf::encode_acf_gbb(info, ts, {});
    ClearSingleRequest out;
    auto ec = decode_clear_single(encoded.data(), encoded.size(), out);
    REQUIRE(ec == make_error_code(RequestErrc::evt_hs_cs_nonzero));
}

// ── RequestErrc category sanity ─────────────────────────────────────────────

TEST_CASE("RequestErrc reports a non-empty message in its own category", "[request][REQ-SEQ-009]") {
    auto ec = make_error_code(RequestErrc::request_canceled);
    REQUIRE(ec.category() == request_category());
    REQUIRE_FALSE(ec.message().empty());
}

TEST_CASE("every RequestErrc value has a distinct, non-empty message", "[request][REQ-SEQ-009]") {
    const RequestErrc all[] = {
        RequestErrc::timestamp_not_repurposed,     RequestErrc::unknown_request_type,
        RequestErrc::index_out_of_range,           RequestErrc::unknown_transaction,
        RequestErrc::invalid_lifecycle_transition, RequestErrc::transaction_num_collision,
        RequestErrc::request_not_found,            RequestErrc::request_canceled,
        RequestErrc::compound_bundle_incomplete,   RequestErrc::request_not_cancellable,
        RequestErrc::reserved_field_nonzero,       RequestErrc::evt_hs_cs_nonzero,
        RequestErrc::unsupported_cmd,              RequestErrc::ledger_full,
    };
    std::vector<std::string> messages;
    for (auto e : all) {
        auto ec = make_error_code(e);
        REQUIRE_FALSE(ec.message().empty());
        messages.push_back(ec.message());
    }
    for (size_t i = 0; i < messages.size(); ++i)
        for (size_t j = i + 1; j < messages.size(); ++j)
            REQUIRE(messages[i] != messages[j]);
}

// ── Request lifecycle state machine ───────────────────────────────────────────

TEST_CASE("RequestLedger enforces the forward-only pending->started->under_execution->finalized sequence",
          "[request][REQ-SEQ-006]") {
    RequestLedger ledger;
    RequestRecord rec;
    rec.transaction_num = 1;
    REQUIRE_FALSE(ledger.submit(rec));
    REQUIRE(ledger.find(1)->state == RequestState::Pending);

    REQUIRE_FALSE(ledger.start(1));
    REQUIRE(ledger.find(1)->state == RequestState::Started);

    REQUIRE_FALSE(ledger.begin_execution(1));
    REQUIRE(ledger.find(1)->state == RequestState::UnderExecution);

    REQUIRE_FALSE(ledger.finalize(1, /*errored=*/false));
    REQUIRE(ledger.find(1)->state == RequestState::Finalized);
}

TEST_CASE("RequestLedger rejects skipping a lifecycle step", "[request][REQ-SEQ-006]") {
    RequestLedger ledger;
    RequestRecord rec;
    rec.transaction_num = 1;
    REQUIRE_FALSE(ledger.submit(rec));

    REQUIRE(ledger.begin_execution(1) == make_error_code(RequestErrc::invalid_lifecycle_transition));
    REQUIRE(ledger.finalize(1, false) == make_error_code(RequestErrc::invalid_lifecycle_transition));
}

TEST_CASE("RequestLedger::submit rejects a duplicate transaction_num", "[request][REQ-SEQ-006]") {
    RequestLedger ledger;
    RequestRecord rec;
    rec.transaction_num = 1;
    REQUIRE_FALSE(ledger.submit(rec));
    REQUIRE(ledger.submit(rec) == make_error_code(RequestErrc::transaction_num_collision));
}

TEST_CASE("RequestLedger reports unknown_transaction for an untracked id", "[request][REQ-SEQ-006]") {
    RequestLedger ledger;
    REQUIRE(ledger.start(99) == make_error_code(RequestErrc::unknown_transaction));
}

TEST_CASE("finalize advances a compound request's sequencer via its own next_state on successful "
          "finalization",
          "[request][REQ-SEQ-006]") {
    std::vector<rcp::regmap::SequencerState> states{SequencerTable::kDefaultState};
    SequencerTable table(states);

    RequestLedger ledger;
    RequestRecord rec;
    rec.transaction_num = 1;
    rec.request_type    = RequestTypeOpcode::Compound;
    CompoundStep step;
    step.start_state    = SequencerTable::kDefaultState;
    step.next_state      = SequencerTable::kDefaultState + 5; // NOT just +1
    step.sequencer_index = 0;
    rec.compound_step    = step;

    REQUIRE_FALSE(ledger.submit(rec));
    REQUIRE_FALSE(ledger.start(1));
    REQUIRE_FALSE(ledger.begin_execution(1));
    REQUIRE_FALSE(ledger.finalize(1, false, &table));

    rcp::regmap::SequencerState s = 0;
    REQUIRE_FALSE(table.state_of(0, s));
    REQUIRE(s == SequencerTable::kDefaultState + 5);
}

TEST_CASE("finalize with next_state==0 leaves the sequencer exactly where it is", "[request][REQ-SEQ-006]") {
    std::vector<rcp::regmap::SequencerState> states{SequencerTable::kDefaultState};
    SequencerTable table(states);

    RequestLedger ledger;
    RequestRecord rec;
    rec.transaction_num = 1;
    rec.request_type    = RequestTypeOpcode::CompoundWait;
    CompoundStep step;
    step.start_state    = SequencerTable::kDefaultState;
    step.next_state      = 0; // sentinel
    step.sequencer_index = 0;
    rec.compound_step    = step;

    REQUIRE_FALSE(ledger.submit(rec));
    REQUIRE_FALSE(ledger.start(1));
    REQUIRE_FALSE(ledger.begin_execution(1));
    REQUIRE_FALSE(ledger.finalize(1, false, &table));

    rcp::regmap::SequencerState s = 0;
    REQUIRE_FALSE(table.state_of(0, s));
    REQUIRE(s == SequencerTable::kDefaultState);
}

TEST_CASE("finalize does not advance the sequencer when its state no longer matches start_state",
          "[request][REQ-SEQ-006]") {
    std::vector<rcp::regmap::SequencerState> states{99}; // not the expected start state
    SequencerTable table(states);

    RequestLedger ledger;
    RequestRecord rec;
    rec.transaction_num = 1;
    rec.request_type    = RequestTypeOpcode::CompoundWait;
    CompoundStep step;
    step.start_state    = SequencerTable::kDefaultState;
    step.next_state      = SequencerTable::kDefaultState + 1;
    step.sequencer_index = 0;
    rec.compound_step    = step;

    REQUIRE_FALSE(ledger.submit(rec));
    REQUIRE_FALSE(ledger.start(1));
    REQUIRE_FALSE(ledger.begin_execution(1));
    REQUIRE_FALSE(ledger.finalize(1, false, &table));

    rcp::regmap::SequencerState s = 0;
    REQUIRE_FALSE(table.state_of(0, s));
    REQUIRE(s == 99); // untouched
}

// ── Cancellation kinds & shared cancellation semantics ────────────────────────

TEST_CASE("RequestLedger::cancel_single cancels a pending request", "[request][REQ-CANCEL-011]") {
    RequestLedger ledger;
    RequestRecord rec;
    rec.transaction_num = 1;
    REQUIRE_FALSE(ledger.submit(rec));

    REQUIRE_FALSE(ledger.cancel_single(1));
    REQUIRE(ledger.find(1)->state == RequestState::Canceled);

    std::error_code outcome;
    REQUIRE_FALSE(ledger.outcome_of(1, outcome));
    REQUIRE(outcome == make_error_code(RequestErrc::request_canceled));
}

TEST_CASE("RequestLedger::cancel_single reports request_not_found for a transaction_num never tracked "
          "at all",
          "[request][REQ-CANCEL-009]") {
    RequestLedger ledger;
    REQUIRE(ledger.cancel_single(77) == make_error_code(RequestErrc::request_not_found));
}

TEST_CASE("RequestLedger::cancel_single reports request_not_cancellable — not request_not_found — "
          "for a request that is tracked but already past cancellation (c-RCP delta #3)",
          "[request][REQ-CANCEL-010]") {
    RequestLedger ledger;
    RequestRecord rec;
    rec.transaction_num = 1;
    REQUIRE_FALSE(ledger.submit(rec));
    REQUIRE_FALSE(ledger.start(1));
    REQUIRE_FALSE(ledger.begin_execution(1));

    auto ec = ledger.cancel_single(1);
    REQUIRE(ec == make_error_code(RequestErrc::request_not_cancellable));
    REQUIRE_FALSE(ec == make_error_code(RequestErrc::request_not_found));
    REQUIRE(ledger.find(1)->state == RequestState::UnderExecution);
}

TEST_CASE("Cancelling a chained request cancels its successors", "[request][REQ-CANCEL-012]") {
    RequestLedger ledger;
    RequestRecord a;
    a.transaction_num = 1;
    a.chained_successors = {2};
    RequestRecord b;
    b.transaction_num       = 2;
    b.chained_predecessor   = uint8_t{1};
    b.chained_successors    = {3};
    RequestRecord c;
    c.transaction_num     = 3;
    c.chained_predecessor = uint8_t{2};

    REQUIRE_FALSE(ledger.submit(a));
    REQUIRE_FALSE(ledger.submit(b));
    REQUIRE_FALSE(ledger.submit(c));

    REQUIRE_FALSE(ledger.cancel_single(1));
    REQUIRE(ledger.find(1)->state == RequestState::Canceled);
    REQUIRE(ledger.find(2)->state == RequestState::Canceled);
    REQUIRE(ledger.find(3)->state == RequestState::Canceled);
}

TEST_CASE("cancel_all(clear-all) cancels every pending/started request", "[request][REQ-CANCEL-002]") {
    RequestLedger ledger;
    for (uint8_t i = 1; i <= 3; ++i) {
        RequestRecord rec;
        rec.transaction_num = i;
        REQUIRE_FALSE(ledger.submit(rec));
    }
    REQUIRE(ledger.cancel_all(/*non_safestate_only=*/false) == 3);
    for (uint8_t i = 1; i <= 3; ++i) REQUIRE(ledger.find(i)->state == RequestState::Canceled);
}

TEST_CASE("cancel_all(clear-non-safestate) skips is_safety records", "[request][REQ-CMP-016]") {
    RequestLedger ledger;
    RequestRecord normal;
    normal.transaction_num = 1;
    RequestRecord safety;
    safety.transaction_num = 2;
    safety.is_safety        = true;
    REQUIRE_FALSE(ledger.submit(normal));
    REQUIRE_FALSE(ledger.submit(safety));

    REQUIRE(ledger.cancel_all(/*non_safestate_only=*/true) == 1);
    REQUIRE(ledger.find(1)->state == RequestState::Canceled);
    REQUIRE(ledger.find(2)->state == RequestState::Pending);
}

TEST_CASE("cancel_all does not disturb an already-executing request", "[request][REQ-CANCEL-002]") {
    RequestLedger ledger;
    RequestRecord rec;
    rec.transaction_num = 1;
    REQUIRE_FALSE(ledger.submit(rec));
    REQUIRE_FALSE(ledger.start(1));
    REQUIRE_FALSE(ledger.begin_execution(1));

    REQUIRE(ledger.cancel_all(false) == 0);
    REQUIRE(ledger.find(1)->state == RequestState::UnderExecution);
}

// ── Chained-successor propagation on predecessor finalization ────────────────
// These tests exercise should_execute_chained() (issue #58) THROUGH the
// ledger's own finalize()/propagate_chain_completion() integration, so a
// regression of the polarity fix above is caught at both layers.

TEST_CASE("finalize with errored=true aborts a chained successor whose cs is abort-on-error",
          "[request][REQ-CHAIN-010]") {
    RequestLedger ledger;
    RequestRecord predecessor;
    predecessor.transaction_num    = 1;
    predecessor.chained_successors = {2};
    RequestRecord successor;
    successor.transaction_num     = 2;
    successor.chained_predecessor = uint8_t{1};
    successor.cs                   = true; // abort-on-error

    REQUIRE_FALSE(ledger.submit(predecessor));
    REQUIRE_FALSE(ledger.submit(successor));
    REQUIRE_FALSE(ledger.start(1));
    REQUIRE_FALSE(ledger.begin_execution(1));
    REQUIRE_FALSE(ledger.finalize(1, /*errored=*/true));

    REQUIRE(ledger.find(2)->state == RequestState::Canceled);
}

TEST_CASE("finalize with errored=true does not abort a chained successor with cs=continue-on-error",
          "[request][REQ-CHAIN-010]") {
    RequestLedger ledger;
    RequestRecord predecessor;
    predecessor.transaction_num    = 1;
    predecessor.chained_successors = {2};
    RequestRecord successor;
    successor.transaction_num     = 2;
    successor.chained_predecessor = uint8_t{1};
    successor.cs                   = false; // continue regardless

    REQUIRE_FALSE(ledger.submit(predecessor));
    REQUIRE_FALSE(ledger.submit(successor));
    REQUIRE_FALSE(ledger.start(1));
    REQUIRE_FALSE(ledger.begin_execution(1));
    REQUIRE_FALSE(ledger.finalize(1, /*errored=*/true));

    REQUIRE(ledger.find(2)->state == RequestState::Pending);
}

TEST_CASE("finalize with errored=false never aborts a chained successor regardless of its cs",
          "[request][REQ-CHAIN-010]") {
    RequestLedger ledger;
    RequestRecord predecessor;
    predecessor.transaction_num    = 1;
    predecessor.chained_successors = {2};
    RequestRecord successor;
    successor.transaction_num     = 2;
    successor.chained_predecessor = uint8_t{1};
    successor.cs                   = true;

    REQUIRE_FALSE(ledger.submit(predecessor));
    REQUIRE_FALSE(ledger.submit(successor));
    REQUIRE_FALSE(ledger.start(1));
    REQUIRE_FALSE(ledger.begin_execution(1));
    REQUIRE_FALSE(ledger.finalize(1, /*errored=*/false));

    REQUIRE(ledger.find(2)->state == RequestState::Pending);
}

TEST_CASE("a full 4-member chain sequence matches c-RCP's own worked example", "[request][REQ-CHAIN-010]") {
    // member 0 runs fine, then errors; member 1 (cs=abort-on-error) does
    // not run and is aborted; members 2 and 3 cascade-cancel regardless of
    // their own cs.
    RequestLedger ledger;
    RequestRecord m0;
    m0.transaction_num    = 1;
    m0.chained_successors = {2};
    RequestRecord m1;
    m1.transaction_num     = 2;
    m1.chained_predecessor = uint8_t{1};
    m1.chained_successors  = {3};
    m1.cs                   = true; // abort-on-error
    RequestRecord m2;
    m2.transaction_num     = 3;
    m2.chained_predecessor = uint8_t{2};
    m2.chained_successors  = {4};
    m2.cs                   = false;
    RequestRecord m3;
    m3.transaction_num     = 4;
    m3.chained_predecessor = uint8_t{3};
    m3.cs                   = false;

    REQUIRE_FALSE(ledger.submit(m0));
    REQUIRE_FALSE(ledger.submit(m1));
    REQUIRE_FALSE(ledger.submit(m2));
    REQUIRE_FALSE(ledger.submit(m3));

    REQUIRE_FALSE(ledger.start(1));
    REQUIRE_FALSE(ledger.begin_execution(1));
    REQUIRE_FALSE(ledger.finalize(1, /*errored=*/true));

    REQUIRE(ledger.find(1)->state == RequestState::Finalized);
    REQUIRE(ledger.find(2)->state == RequestState::Canceled);
    REQUIRE(ledger.find(3)->state == RequestState::Canceled);
    REQUIRE(ledger.find(4)->state == RequestState::Canceled);
}

// ── Safety-tagged (0x8x) request variants ─────────────────────────────────────

TEST_CASE("decode_request_type accepts the three 0x8x safety-tagged opcodes", "[request][REQ-CMP-001]") {
    std::array<uint8_t, 7> params{1, 2, 3, 4, 5, 6, 7};
    for (auto opcode : {RequestTypeOpcode::CompoundSafety, RequestTypeOpcode::CompoundWaitSafety,
                         RequestTypeOpcode::TriggeredSafety}) {
        const uint64_t ts = encode_request_type(opcode, params);
        RequestTypeOpcode type{};
        std::array<uint8_t, 7> out_params{};
        auto ec = decode_request_type(/*mtv=*/false, ts, type, out_params);
        REQUIRE_FALSE(ec);
        REQUIRE(type == opcode);
        REQUIRE(out_params == params);
    }
}

TEST_CASE("category_of maps each safety-tagged opcode to its base opcode's priority category",
          "[request][REQ-SCHED-002]") {
    REQUIRE(category_of(RequestTypeOpcode::CompoundSafety) == category_of(RequestTypeOpcode::Compound));
    REQUIRE(category_of(RequestTypeOpcode::CompoundWaitSafety) ==
            category_of(RequestTypeOpcode::CompoundWait));
    REQUIRE(category_of(RequestTypeOpcode::TriggeredSafety) == category_of(RequestTypeOpcode::Triggered));
}

TEST_CASE("request_record_for derives is_safety from the opcode automatically", "[request][REQ-CMP-001]") {
    auto normal = request_record_for(/*transaction_num=*/1, RequestTypeOpcode::Compound, /*cs=*/true);
    REQUIRE_FALSE(normal.is_safety);
    REQUIRE(normal.transaction_num == 1);
    REQUIRE(normal.request_type == RequestTypeOpcode::Compound);
    REQUIRE(normal.cs);

    auto safety = request_record_for(/*transaction_num=*/2, RequestTypeOpcode::CompoundSafety, /*cs=*/false);
    REQUIRE(safety.is_safety);

    auto standard = request_record_for(/*transaction_num=*/3, std::nullopt, /*cs=*/false);
    REQUIRE_FALSE(standard.is_safety);
    REQUIRE_FALSE(standard.request_type.has_value());
}

TEST_CASE("request_record_for-built safety records survive cancel_all(non_safestate_only=true)",
          "[request][REQ-CMP-016]") {
    RequestLedger ledger;
    REQUIRE_FALSE(ledger.submit(request_record_for(1, RequestTypeOpcode::Triggered, false)));
    REQUIRE_FALSE(ledger.submit(request_record_for(2, RequestTypeOpcode::TriggeredSafety, false)));

    size_t canceled = ledger.cancel_all(/*non_safestate_only=*/true);
    REQUIRE(canceled == 1);
    REQUIRE(ledger.find(1)->state == RequestState::Canceled);
    REQUIRE(ledger.find(2)->state == RequestState::Pending);
}

// ── Fixed-capacity RequestLedger (kMaxTrackedRequests) ───────────────────────
// RequestLedger::records_ is a detail::BoundedVector<RequestRecord,
// kMaxTrackedRequests> (256 — the exact size of transaction_num's own
// uint8_t domain) rather than an unbounded std::vector. This proves the
// bound is real, not merely documented: filling the ledger to capacity and
// submitting one more must fail with ledger_full rather than silently
// growing. (Mutation-tested during development: temporarily removing
// RequestLedger::submit's `if (records_.full()) return ...;` guard made
// this test fail — REQUIRE(ledger.submit(...) == ...ledger_full) instead
// observed a success outcome — confirming the test is load-bearing; the
// guard was restored immediately after.)

TEST_CASE("RequestLedger::capacity reports kMaxTrackedRequests", "[request][fixed-capacity]") {
    RequestLedger ledger;
    REQUIRE(RequestLedger::capacity() == kMaxTrackedRequests);
    REQUIRE(ledger.size() == 0);
}

TEST_CASE("RequestLedger::submit fills to capacity and then reports ledger_full — not "
          "transaction_num_collision — for a genuinely fresh, never-submitted id",
          "[request][fixed-capacity]") {
    RequestLedger ledger;
    for (size_t i = 0; i < RequestLedger::capacity(); ++i) {
        RequestRecord rec;
        rec.transaction_num = static_cast<uint8_t>(i);
        REQUIRE_FALSE(ledger.submit(rec));
    }
    REQUIRE(ledger.size() == RequestLedger::capacity());

    // kMaxTrackedRequests (64) is well below transaction_num's full uint8_t
    // range (256), so id 64 is guaranteed fresh — this exercises the
    // capacity check for real, not the collision check.
    RequestRecord fresh;
    fresh.transaction_num = static_cast<uint8_t>(RequestLedger::capacity());
    auto ec = ledger.submit(fresh);
    REQUIRE(ec == make_error_code(RequestErrc::ledger_full));
    REQUIRE_FALSE(ec == make_error_code(RequestErrc::transaction_num_collision));
    REQUIRE(ledger.size() == RequestLedger::capacity()); // unchanged — not silently grown
    REQUIRE(ledger.find(fresh.transaction_num) == nullptr); // truly never admitted
}

TEST_CASE("RequestLedger::submit still reports transaction_num_collision (not ledger_full) for a "
          "duplicate submitted before the ledger is full",
          "[request][fixed-capacity]") {
    RequestLedger ledger;
    RequestRecord rec;
    rec.transaction_num = 5;
    REQUIRE_FALSE(ledger.submit(rec));
    REQUIRE(ledger.submit(rec) == make_error_code(RequestErrc::transaction_num_collision));
}

TEST_CASE("detail::BoundedVector reports full() at capacity and rejects a push_back beyond it",
          "[request][fixed-capacity]") {
    rcp::request::detail::BoundedVector<int, 3> v;
    REQUIRE(v.empty());
    REQUIRE(v.push_back(1));
    REQUIRE(v.push_back(2));
    REQUIRE(v.push_back(3));
    REQUIRE(v.full());
    REQUIRE(v.size() == 3);

    REQUIRE_FALSE(v.push_back(4)); // rejected, not silently grown
    REQUIRE(v.size() == 3);        // unchanged
    REQUIRE(v[0] == 1);
    REQUIRE(v[2] == 3);
}
