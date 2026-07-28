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

// Tests for rcp/sequencer.hpp — conditional-request taxonomy and
// sequencer-state primitives (ROADMAP.md milestone 49,
// "Conditional-Request Taxonomy & Sequencers", v2.5.0).

#include <catch2/catch_test_macros.hpp>
#include <rcp/sequencer.hpp>

using namespace rcp::sequencer;

// ── message_timestamp-repurposing trick (mtv=0) ──────────────────────────────

TEST_CASE("encode_request_type/decode_request_type round-trip", "[sequencer][REQ-SEQ-001]") {
    std::array<uint8_t, 7> params{0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77};
    const uint64_t ts = encode_request_type(RequestTypeOpcode::Compound, params);

    RequestTypeOpcode type{};
    std::array<uint8_t, 7> out_params{};
    auto ec = decode_request_type(/*mtv=*/false, ts, type, out_params);
    REQUIRE_FALSE(ec);
    REQUIRE(type == RequestTypeOpcode::Compound);
    REQUIRE(out_params == params);
}

TEST_CASE("decode_request_type rejects mtv=1 as not a repurposed slot", "[sequencer][REQ-SEQ-001]") {
    RequestTypeOpcode type{};
    std::array<uint8_t, 7> out_params{};
    auto ec = decode_request_type(/*mtv=*/true, 0xFFFFFFFFFFFFFFFFull, type, out_params);
    REQUIRE(ec == make_error_code(SequencerErrc::timestamp_not_repurposed));
}

TEST_CASE("decode_request_type rejects an unrecognized opcode byte", "[sequencer][REQ-SEQ-001]") {
    RequestTypeOpcode type{};
    std::array<uint8_t, 7> out_params{};
    // 0x02 is not one of the 8 defined opcodes.
    const uint64_t ts = uint64_t{0x02} << 56;
    auto ec = decode_request_type(/*mtv=*/false, ts, type, out_params);
    REQUIRE(ec == make_error_code(SequencerErrc::unknown_request_type));
}

TEST_CASE("make_conditional_request always sets ACF_GBB with mtv clear", "[sequencer][REQ-SEQ-001]") {
    auto info = make_conditional_request(/*bus_id=*/7, /*transaction_num=*/9, /*cs=*/true);
    REQUIRE(info.acf_msg_type == rcp::wire::kAcfMsgTypeGbb);
    REQUIRE_FALSE(info.mtv);
    REQUIRE(info.byte_bus_id == 7);
    REQUIRE(info.transaction_num == 9);
    REQUIRE(info.cs);
}

TEST_CASE("make_conditional_request round-trips through wire::encode_acf_gbb/decode_acf_gbb",
          "[sequencer][REQ-SEQ-001]") {
    std::array<uint8_t, 7> params{1, 2, 3, 4, 5, 6, 7};
    auto info = make_conditional_request(3, 42, false);
    auto ts   = encode_request_type(RequestTypeOpcode::Triggered, params);
    auto buf  = rcp::wire::encode_acf_gbb(info, ts, {});

    rcp::wire::AcfMessageInfo decoded_info;
    uint64_t decoded_ts = 0;
    std::vector<uint8_t> decoded_payload;
    auto ec = rcp::wire::decode_acf_gbb(buf.data(), buf.size(), decoded_info, decoded_ts, decoded_payload);
    REQUIRE_FALSE(ec);
    REQUIRE_FALSE(decoded_info.mtv);

    RequestTypeOpcode type{};
    std::array<uint8_t, 7> out_params{};
    REQUIRE_FALSE(decode_request_type(decoded_info.mtv, decoded_ts, type, out_params));
    REQUIRE(type == RequestTypeOpcode::Triggered);
    REQUIRE(out_params == params);
}

// ── Request categories & execution-priority ordering ─────────────────────────

TEST_CASE("category_of maps every opcode to its documented category", "[sequencer][REQ-SEQ-002]") {
    REQUIRE(category_of(std::nullopt) == RequestCategory::Standard);
    REQUIRE(category_of(RequestTypeOpcode::ClearAll) == RequestCategory::Cancellation);
    REQUIRE(category_of(RequestTypeOpcode::ClearNonSafestate) == RequestCategory::Cancellation);
    REQUIRE(category_of(RequestTypeOpcode::ClearSingle) == RequestCategory::Cancellation);
    REQUIRE(category_of(RequestTypeOpcode::Triggered) == RequestCategory::Triggered);
    REQUIRE(category_of(RequestTypeOpcode::Timed) == RequestCategory::Timed);
    REQUIRE(category_of(RequestTypeOpcode::Compound) == RequestCategory::Compound);
    REQUIRE(category_of(RequestTypeOpcode::CompoundWait) == RequestCategory::CompoundWait);
    REQUIRE(category_of(RequestTypeOpcode::Chained) == RequestCategory::Chained);
    // Safety-tagged (0x8x) variants share their base opcode's category.
    REQUIRE(category_of(RequestTypeOpcode::CompoundSafety) == RequestCategory::Compound);
    REQUIRE(category_of(RequestTypeOpcode::CompoundWaitSafety) == RequestCategory::CompoundWait);
    REQUIRE(category_of(RequestTypeOpcode::TriggeredSafety) == RequestCategory::Triggered);
}

TEST_CASE("priority_rank orders categories cancellation..standard", "[sequencer][REQ-SEQ-002]") {
    REQUIRE(priority_rank(RequestCategory::Cancellation) < priority_rank(RequestCategory::Triggered));
    REQUIRE(priority_rank(RequestCategory::Triggered) < priority_rank(RequestCategory::Timed));
    REQUIRE(priority_rank(RequestCategory::Timed) < priority_rank(RequestCategory::Compound));
    REQUIRE(priority_rank(RequestCategory::Compound) < priority_rank(RequestCategory::CompoundWait));
    REQUIRE(priority_rank(RequestCategory::CompoundWait) < priority_rank(RequestCategory::Chained));
    REQUIRE(priority_rank(RequestCategory::Chained) < priority_rank(RequestCategory::Standard));
}

TEST_CASE("select_next_due picks the highest-priority category regardless of arrival order",
          "[sequencer][REQ-SEQ-002]") {
    std::vector<DueCandidate> due{
        {RequestCategory::Standard, 0},
        {RequestCategory::Chained, 1},
        {RequestCategory::Cancellation, 2}, // arrived last, but highest priority
    };
    auto winner = select_next_due(due);
    REQUIRE(winner.has_value());
    REQUIRE(*winner == 2);
}

TEST_CASE("select_next_due breaks ties within a category by FIFO arrival order", "[sequencer][REQ-SEQ-002]") {
    std::vector<DueCandidate> due{
        {RequestCategory::Timed, 5},
        {RequestCategory::Timed, 2},
        {RequestCategory::Timed, 9},
    };
    auto winner = select_next_due(due);
    REQUIRE(winner.has_value());
    REQUIRE(*winner == 1); // arrival_seq 2 is earliest
}

TEST_CASE("select_next_due returns nullopt for an empty candidate set", "[sequencer][REQ-SEQ-002]") {
    REQUIRE_FALSE(select_next_due({}).has_value());
}

// ── Optional-feature bundling ─────────────────────────────────────────────────

TEST_CASE("validate_feature_bundles accepts a feature set with nothing conditional enabled",
          "[sequencer][REQ-SEQ-003]") {
    FeatureSet f;
    REQUIRE_FALSE(validate_feature_bundles(f));
}

TEST_CASE("validate_feature_bundles accepts triggered/chained/timed independently of compound",
          "[sequencer][REQ-SEQ-003]") {
    FeatureSet f;
    f.triggered = true;
    f.chained   = true;
    f.timed     = true;
    REQUIRE_FALSE(validate_feature_bundles(f));
}

TEST_CASE("validate_feature_bundles rejects compound claimed alone", "[sequencer][REQ-SEQ-003]") {
    FeatureSet f;
    f.compound = true; // missing compound_wait, clear_non_safestate, and sequencer_count
    REQUIRE(validate_feature_bundles(f) == make_error_code(SequencerErrc::compound_bundle_incomplete));
}

TEST_CASE("validate_feature_bundles rejects compound_wait claimed alone", "[sequencer][REQ-SEQ-003]") {
    FeatureSet f;
    f.compound_wait = true;
    REQUIRE(validate_feature_bundles(f) == make_error_code(SequencerErrc::compound_bundle_incomplete));
}

TEST_CASE("validate_feature_bundles rejects compound+compound_wait with fewer than 4 sequencers",
          "[sequencer][REQ-SEQ-003]") {
    FeatureSet f;
    f.compound            = true;
    f.compound_wait       = true;
    f.clear_non_safestate = true;
    f.sequencer_count     = kMinCompoundSequencers - 1;
    REQUIRE(validate_feature_bundles(f) == make_error_code(SequencerErrc::compound_bundle_incomplete));
}

TEST_CASE("validate_feature_bundles rejects compound+compound_wait without clear_non_safestate",
          "[sequencer][REQ-SEQ-003]") {
    FeatureSet f;
    f.compound        = true;
    f.compound_wait   = true;
    f.sequencer_count = kMinCompoundSequencers;
    REQUIRE(validate_feature_bundles(f) == make_error_code(SequencerErrc::compound_bundle_incomplete));
}

TEST_CASE("validate_feature_bundles accepts the complete compound bundle", "[sequencer][REQ-SEQ-003]") {
    FeatureSet f;
    f.compound            = true;
    f.compound_wait       = true;
    f.clear_non_safestate = true;
    f.sequencer_count     = kMinCompoundSequencers;
    REQUIRE_FALSE(validate_feature_bundles(f));
}

TEST_CASE("implemented_options_bits reports kOptConditionalRequests only when something is enabled",
          "[sequencer][REQ-SEQ-003]") {
    FeatureSet none;
    REQUIRE(implemented_options_bits(none) == 0);

    FeatureSet triggered_only;
    triggered_only.triggered = true;
    REQUIRE(implemented_options_bits(triggered_only) == rcp::regmap::kOptConditionalRequests);
}

// ── Sequencer-state registers ─────────────────────────────────────────────────

TEST_CASE("SequencerTable::ensure_size fills new slots with kDefaultState (1), not 0",
          "[sequencer][REQ-SEQ-004]") {
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

TEST_CASE("SequencerTable::ensure_size never shrinks existing storage", "[sequencer][REQ-SEQ-004]") {
    std::vector<rcp::regmap::SequencerState> states{5, 5, 5};
    SequencerTable table(states);
    table.ensure_size(1);
    REQUIRE(table.size() == 3);
}

TEST_CASE("SequencerTable::try_advance advances only when current state matches expected_start",
          "[sequencer][REQ-SEQ-004]") {
    std::vector<rcp::regmap::SequencerState> states{SequencerTable::kDefaultState};
    SequencerTable table(states);

    bool advanced = false;
    auto ec = table.try_advance(0, SequencerTable::kDefaultState, advanced);
    REQUIRE_FALSE(ec);
    REQUIRE(advanced);

    rcp::regmap::SequencerState s = 0;
    REQUIRE_FALSE(table.state_of(0, s));
    REQUIRE(s == SequencerTable::kDefaultState + 1);
}

TEST_CASE("SequencerTable::try_advance leaves a mismatched sequencer untouched", "[sequencer][REQ-SEQ-004]") {
    std::vector<rcp::regmap::SequencerState> states{9};
    SequencerTable table(states);

    bool advanced = true;
    auto ec = table.try_advance(0, /*expected_start=*/1, advanced);
    REQUIRE_FALSE(ec); // not an error — just didn't advance
    REQUIRE_FALSE(advanced);

    rcp::regmap::SequencerState s = 0;
    REQUIRE_FALSE(table.state_of(0, s));
    REQUIRE(s == 9);
}

TEST_CASE("SequencerTable reports index_out_of_range for an out-of-bounds index", "[sequencer][REQ-SEQ-004]") {
    std::vector<rcp::regmap::SequencerState> states{1};
    SequencerTable table(states);
    rcp::regmap::SequencerState s = 0;
    REQUIRE(table.state_of(5, s) == make_error_code(SequencerErrc::index_out_of_range));
    bool advanced = false;
    REQUIRE(table.try_advance(5, 1, advanced) == make_error_code(SequencerErrc::index_out_of_range));
}

TEST_CASE("SequencerTable can be constructed directly over a RegisterMap's sequencer_states",
          "[sequencer][REQ-SEQ-004]") {
    rcp::regmap::RegisterMap regs;
    SequencerTable table(regs);
    table.ensure_size(4);
    REQUIRE(regs.sequencer_states.size() == 4);
    REQUIRE(regs.sequencer_states[0] == SequencerTable::kDefaultState);
}

// ── Cancellation kinds & shared cancellation semantics ────────────────────────

TEST_CASE("RequestLedger::cancel_single cancels a pending request", "[sequencer][REQ-SEQ-005]") {
    RequestLedger ledger;
    RequestRecord rec;
    rec.transaction_num = 1;
    REQUIRE_FALSE(ledger.submit(rec));

    REQUIRE_FALSE(ledger.cancel_single(1));
    REQUIRE(ledger.find(1)->state == RequestState::Canceled);

    std::error_code outcome;
    REQUIRE_FALSE(ledger.outcome_of(1, outcome));
    REQUIRE(outcome == make_error_code(SequencerErrc::request_canceled));
}

TEST_CASE("RequestLedger::cancel_single reports REQUEST_NOT_FOUND for an unknown transaction",
          "[sequencer][REQ-SEQ-005]") {
    RequestLedger ledger;
    REQUIRE(ledger.cancel_single(77) == make_error_code(SequencerErrc::request_not_found));
}

TEST_CASE("RequestLedger::cancel_single leaves an already-executing request to finish",
          "[sequencer][REQ-SEQ-005]") {
    RequestLedger ledger;
    RequestRecord rec;
    rec.transaction_num = 1;
    REQUIRE_FALSE(ledger.submit(rec));
    REQUIRE_FALSE(ledger.start(1));
    REQUIRE_FALSE(ledger.begin_execution(1));

    REQUIRE(ledger.cancel_single(1) == make_error_code(SequencerErrc::request_not_found));
    REQUIRE(ledger.find(1)->state == RequestState::UnderExecution);
}

TEST_CASE("Cancelling a chained request cancels its successors", "[sequencer][REQ-SEQ-005]") {
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

TEST_CASE("cancel_all(clear-all) cancels every pending/started request", "[sequencer][REQ-SEQ-005]") {
    RequestLedger ledger;
    for (uint8_t i = 1; i <= 3; ++i) {
        RequestRecord rec;
        rec.transaction_num = i;
        REQUIRE_FALSE(ledger.submit(rec));
    }
    REQUIRE(ledger.cancel_all(/*non_safestate_only=*/false) == 3);
    for (uint8_t i = 1; i <= 3; ++i) REQUIRE(ledger.find(i)->state == RequestState::Canceled);
}

TEST_CASE("cancel_all(clear-non-safestate) skips is_safety records", "[sequencer][REQ-SEQ-005]") {
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

TEST_CASE("cancel_all does not disturb an already-executing request", "[sequencer][REQ-SEQ-005]") {
    RequestLedger ledger;
    RequestRecord rec;
    rec.transaction_num = 1;
    REQUIRE_FALSE(ledger.submit(rec));
    REQUIRE_FALSE(ledger.start(1));
    REQUIRE_FALSE(ledger.begin_execution(1));

    REQUIRE(ledger.cancel_all(false) == 0);
    REQUIRE(ledger.find(1)->state == RequestState::UnderExecution);
}

// ── Request lifecycle state machine ───────────────────────────────────────────

TEST_CASE("RequestLedger enforces the forward-only pending->started->under_execution->finalized sequence",
          "[sequencer][REQ-SEQ-006]") {
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

TEST_CASE("RequestLedger rejects skipping a lifecycle step", "[sequencer][REQ-SEQ-006]") {
    RequestLedger ledger;
    RequestRecord rec;
    rec.transaction_num = 1;
    REQUIRE_FALSE(ledger.submit(rec));

    REQUIRE(ledger.begin_execution(1) == make_error_code(SequencerErrc::invalid_lifecycle_transition));
    REQUIRE(ledger.finalize(1, false) == make_error_code(SequencerErrc::invalid_lifecycle_transition));
}

TEST_CASE("RequestLedger::submit rejects a duplicate transaction_num", "[sequencer][REQ-SEQ-006]") {
    RequestLedger ledger;
    RequestRecord rec;
    rec.transaction_num = 1;
    REQUIRE_FALSE(ledger.submit(rec));
    REQUIRE(ledger.submit(rec) == make_error_code(SequencerErrc::transaction_num_collision));
}

TEST_CASE("RequestLedger reports unknown_transaction for an untracked id", "[sequencer][REQ-SEQ-006]") {
    RequestLedger ledger;
    REQUIRE(ledger.start(99) == make_error_code(SequencerErrc::unknown_transaction));
}

TEST_CASE("finalize advances a compound request's sequencer on successful finalization",
          "[sequencer][REQ-SEQ-006]") {
    std::vector<rcp::regmap::SequencerState> states{SequencerTable::kDefaultState};
    SequencerTable table(states);

    RequestLedger ledger;
    RequestRecord rec;
    rec.transaction_num       = 1;
    rec.request_type           = RequestTypeOpcode::Compound;
    rec.sequencer_index        = size_t{0};
    rec.expected_start_state   = SequencerTable::kDefaultState;
    REQUIRE_FALSE(ledger.submit(rec));
    REQUIRE_FALSE(ledger.start(1));
    REQUIRE_FALSE(ledger.begin_execution(1));
    REQUIRE_FALSE(ledger.finalize(1, false, &table));

    rcp::regmap::SequencerState s = 0;
    REQUIRE_FALSE(table.state_of(0, s));
    REQUIRE(s == SequencerTable::kDefaultState + 1);
}

TEST_CASE("finalize does not advance the sequencer when its state no longer matches expected_start",
          "[sequencer][REQ-SEQ-006]") {
    std::vector<rcp::regmap::SequencerState> states{99}; // not the expected start state
    SequencerTable table(states);

    RequestLedger ledger;
    RequestRecord rec;
    rec.transaction_num     = 1;
    rec.request_type         = RequestTypeOpcode::CompoundWait;
    rec.sequencer_index      = size_t{0};
    rec.expected_start_state = SequencerTable::kDefaultState;
    REQUIRE_FALSE(ledger.submit(rec));
    REQUIRE_FALSE(ledger.start(1));
    REQUIRE_FALSE(ledger.begin_execution(1));
    REQUIRE_FALSE(ledger.finalize(1, false, &table));

    rcp::regmap::SequencerState s = 0;
    REQUIRE_FALSE(table.state_of(0, s));
    REQUIRE(s == 99); // untouched
}

// ── Chained-successor propagation on predecessor finalization ────────────────

TEST_CASE("finalize with errored=true aborts a chained successor whose cs forbids executing anyway",
          "[sequencer][REQ-SEQ-007]") {
    RequestLedger ledger;
    RequestRecord predecessor;
    predecessor.transaction_num    = 1;
    predecessor.chained_successors = {2};
    RequestRecord successor;
    successor.transaction_num     = 2;
    successor.chained_predecessor = uint8_t{1};
    successor.cs                   = false; // abort-on-predecessor-error

    REQUIRE_FALSE(ledger.submit(predecessor));
    REQUIRE_FALSE(ledger.submit(successor));
    REQUIRE_FALSE(ledger.start(1));
    REQUIRE_FALSE(ledger.begin_execution(1));
    REQUIRE_FALSE(ledger.finalize(1, /*errored=*/true));

    REQUIRE(ledger.find(2)->state == RequestState::Canceled);
}

TEST_CASE("finalize with errored=true does not abort a chained successor with cs=execute-regardless",
          "[sequencer][REQ-SEQ-007]") {
    RequestLedger ledger;
    RequestRecord predecessor;
    predecessor.transaction_num    = 1;
    predecessor.chained_successors = {2};
    RequestRecord successor;
    successor.transaction_num     = 2;
    successor.chained_predecessor = uint8_t{1};
    successor.cs                   = true; // execute regardless

    REQUIRE_FALSE(ledger.submit(predecessor));
    REQUIRE_FALSE(ledger.submit(successor));
    REQUIRE_FALSE(ledger.start(1));
    REQUIRE_FALSE(ledger.begin_execution(1));
    REQUIRE_FALSE(ledger.finalize(1, /*errored=*/true));

    REQUIRE(ledger.find(2)->state == RequestState::Pending);
}

TEST_CASE("finalize with errored=false never aborts a chained successor regardless of its cs",
          "[sequencer][REQ-SEQ-007]") {
    RequestLedger ledger;
    RequestRecord predecessor;
    predecessor.transaction_num    = 1;
    predecessor.chained_successors = {2};
    RequestRecord successor;
    successor.transaction_num     = 2;
    successor.chained_predecessor = uint8_t{1};
    successor.cs                   = false;

    REQUIRE_FALSE(ledger.submit(predecessor));
    REQUIRE_FALSE(ledger.submit(successor));
    REQUIRE_FALSE(ledger.start(1));
    REQUIRE_FALSE(ledger.begin_execution(1));
    REQUIRE_FALSE(ledger.finalize(1, /*errored=*/false));

    REQUIRE(ledger.find(2)->state == RequestState::Pending);
}

// ── The `cs` field's dual meaning ─────────────────────────────────────────────

TEST_CASE("compound_wait_check_of maps cs to Immediate vs AfterChangeOnly", "[sequencer][REQ-SEQ-008]") {
    REQUIRE(compound_wait_check_of(true) == CompoundWaitCheck::Immediate);
    REQUIRE(compound_wait_check_of(false) == CompoundWaitCheck::AfterChangeOnly);
}

TEST_CASE("should_execute_chained: cs=true always executes regardless of predecessor outcome",
          "[sequencer][REQ-SEQ-008]") {
    REQUIRE(should_execute_chained(/*cs=*/true, /*predecessor_errored=*/true));
    REQUIRE(should_execute_chained(/*cs=*/true, /*predecessor_errored=*/false));
}

TEST_CASE("should_execute_chained: cs=false aborts only when the predecessor errored",
          "[sequencer][REQ-SEQ-008]") {
    REQUIRE_FALSE(should_execute_chained(/*cs=*/false, /*predecessor_errored=*/true));
    REQUIRE(should_execute_chained(/*cs=*/false, /*predecessor_errored=*/false));
}

// ── SequencerErrc category sanity ─────────────────────────────────────────────

TEST_CASE("SequencerErrc reports a non-empty message in its own category", "[sequencer][REQ-SEQ-009]") {
    auto ec = make_error_code(SequencerErrc::request_canceled);
    REQUIRE(ec.category() == sequencer_category());
    REQUIRE_FALSE(ec.message().empty());
}

TEST_CASE("every SequencerErrc value has a distinct, non-empty message", "[sequencer][REQ-SEQ-009]") {
    const SequencerErrc all[] = {
        SequencerErrc::timestamp_not_repurposed,     SequencerErrc::unknown_request_type,
        SequencerErrc::index_out_of_range,           SequencerErrc::unknown_transaction,
        SequencerErrc::invalid_lifecycle_transition, SequencerErrc::transaction_num_collision,
        SequencerErrc::request_not_found,            SequencerErrc::request_canceled,
        SequencerErrc::compound_bundle_incomplete,
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

// ── Safety-tagged (0x8x) request variants (ROADMAP.md milestone 50, v2.6.0) ──

TEST_CASE("decode_request_type accepts the three 0x8x safety-tagged opcodes", "[sequencer][REQ-SEQ-010]") {
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

TEST_CASE("decode_request_type rejects an MSB-set byte that is not one of the three defined "
          "safety opcodes",
          "[sequencer][REQ-SEQ-010]") {
    RequestTypeOpcode type{};
    std::array<uint8_t, 7> out_params{};
    // 0x80 has the MSB set but is not CompoundWaitSafety/TriggeredSafety/CompoundSafety.
    auto ec = decode_request_type(/*mtv=*/false, uint64_t{0x80} << 56, type, out_params);
    REQUIRE(ec == make_error_code(SequencerErrc::unknown_request_type));
}

TEST_CASE("is_safety_variant identifies exactly the three 0x8x opcodes", "[sequencer][REQ-SEQ-011]") {
    REQUIRE(is_safety_variant(RequestTypeOpcode::CompoundSafety));
    REQUIRE(is_safety_variant(RequestTypeOpcode::CompoundWaitSafety));
    REQUIRE(is_safety_variant(RequestTypeOpcode::TriggeredSafety));

    REQUIRE_FALSE(is_safety_variant(RequestTypeOpcode::Compound));
    REQUIRE_FALSE(is_safety_variant(RequestTypeOpcode::CompoundWait));
    REQUIRE_FALSE(is_safety_variant(RequestTypeOpcode::Triggered));
    REQUIRE_FALSE(is_safety_variant(RequestTypeOpcode::Chained));
    REQUIRE_FALSE(is_safety_variant(RequestTypeOpcode::ClearAll));
}

TEST_CASE("category_of maps each safety-tagged opcode to its base opcode's priority category",
          "[sequencer][REQ-SEQ-011]") {
    REQUIRE(category_of(RequestTypeOpcode::CompoundSafety) == category_of(RequestTypeOpcode::Compound));
    REQUIRE(category_of(RequestTypeOpcode::CompoundWaitSafety) ==
            category_of(RequestTypeOpcode::CompoundWait));
    REQUIRE(category_of(RequestTypeOpcode::TriggeredSafety) == category_of(RequestTypeOpcode::Triggered));
}

TEST_CASE("request_record_for derives is_safety from the opcode automatically", "[sequencer][REQ-SEQ-012]") {
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
          "[sequencer][REQ-SEQ-012]") {
    RequestLedger ledger;
    REQUIRE_FALSE(ledger.submit(request_record_for(1, RequestTypeOpcode::Triggered, false)));
    REQUIRE_FALSE(ledger.submit(request_record_for(2, RequestTypeOpcode::TriggeredSafety, false)));

    size_t canceled = ledger.cancel_all(/*non_safestate_only=*/true);
    REQUIRE(canceled == 1);
    REQUIRE(ledger.find(1)->state == RequestState::Canceled);
    REQUIRE(ledger.find(2)->state == RequestState::Pending);
}
