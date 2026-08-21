// fusa:test REQ-SRV-001
// fusa:test REQ-SRV-002
// fusa:test REQ-SRV-003
// fusa:test REQ-SRV-004
// fusa:test REQ-SRV-005
// fusa:test REQ-SRV-006
// fusa:test REQ-SRV-007
// fusa:test REQ-SRV-008
// fusa:test REQ-SRV-009
// fusa:test REQ-SRV-010
// fusa:test REQ-SRV-011
// fusa:test REQ-SRV-012
// fusa:test REQ-SRV-013
// fusa:test REQ-SRV-014
// fusa:test REQ-SRV-015
// fusa:test REQ-SRV-016
// fusa:test REQ-SRV-018
// fusa:test REQ-SRV-021
// fusa:test REQ-SRV-023
// fusa:test REQ-SRV-035
// fusa:test REQ-SRV-036
// fusa:test REQ-SRV-040
// fusa:test REQ-SRV-041
// fusa:test REQ-PWRMODE-028
// fusa:test REQ-CANCEL-012
// fusa:test REQ-ACF-021
// fusa:test REQ-TIMED-012

// Tests for rcp/server.hpp — the per-endpoint ep_enable admission queue and
// conditional-request scheduler (ROADMAP.md Phase 17/"Phase 4", cpp-RCP
// issue #129), ported from c-RCP's tests/test_server.c (the ep_enable
// queue) and the server-specific slice of tests/test_tc18_gaps_server.c
// (admission_suspended, config-write fast path, ack emission, the Table 37
// gPTP trigger tracker, TSCF gating). The priority-ordering/repeat-count/
// cancellation/chain-cascade/watchdog-purge tests below are this file's own
// direct-API equivalents of c-RCP's tests/test_conditional_dispatch.c
// scenarios — that file exercises the same server.c logic exclusively
// through mock.c's rcp_mock_server_dispatch()/_tick(), which cpp-RCP has not
// wired up yet (mock.hpp wiring is a later Phase 4 batch); the scenarios
// themselves (priority order, FIFO tie-break, repeat_count, clear-all/
// clear-single/clear-non-safestate, chained cascade, watchdog purge) are
// genuinely server.hpp's own behavior, so they are re-expressed here calling
// rcp::server::Endpoint directly instead.

#include <catch2/catch_test_macros.hpp>
#include <rcp/server.hpp>

#include <vector>

using namespace rcp::server;
namespace request = rcp::request;
namespace acf      = rcp::acf;
namespace regmap   = rcp::regmap;

namespace {

std::vector<uint8_t> standard_abb(rcp::avtp::ByteBusId bus_id, uint8_t transaction_num,
                                   uint8_t evt = 0) {
    acf::AcfMessageInfo info;
    info.byte_bus_id     = bus_id;
    info.transaction_num = transaction_num;
    info.evt_ack         = (evt & 0x08u) != 0;
    info.evt_op          = static_cast<uint8_t>(evt & 0x07u);
    return acf::encode_acf_abb(info, {});
}

} // namespace

// ── ep_enable: pre-load-then-drain-on-enable (ported from test_server.c) ─────

TEST_CASE("disabled endpoint queues submitted requests", "[server][REQ-SRV-001]") {
    Endpoint              ep(false);
    uint8_t               body[] = {1, 2, 3};
    std::vector<uint8_t>  ack;

    REQUIRE_FALSE(ep.submit(body, sizeof(body), &ack));
    REQUIRE(ep.queue_len() == 1);
}

TEST_CASE("enabled endpoint reports immediate execution", "[server][REQ-SRV-002]") {
    Endpoint             ep(true);
    uint8_t              body[] = {9};
    std::vector<uint8_t> ack;

    REQUIRE(ep.submit(body, sizeof(body), &ack));
    REQUIRE(ep.queue_len() == 0);
}

TEST_CASE("drain_one refuses while disabled", "[server][REQ-SRV-023]") {
    Endpoint             ep(false);
    uint8_t              body[] = {1};
    std::vector<uint8_t> out;

    (void)ep.submit(body, sizeof(body), nullptr);

    REQUIRE_FALSE(ep.drain_one(out));
    REQUIRE(ep.queue_len() == 1);
}

TEST_CASE("reenable drains the queue in FIFO order", "[server][REQ-SRV-003]") {
    Endpoint             ep(false);
    uint8_t              first[]  = {0xAA};
    uint8_t              second[] = {0xBB};
    uint8_t              third[]  = {0xCC};
    std::vector<uint8_t> out;

    (void)ep.submit(first, sizeof(first), nullptr);
    (void)ep.submit(second, sizeof(second), nullptr);
    (void)ep.submit(third, sizeof(third), nullptr);
    REQUIRE(ep.queue_len() == 3);

    ep.set_enable(true);

    REQUIRE(ep.drain_one(out));
    REQUIRE(out[0] == 0xAA);
    REQUIRE(ep.drain_one(out));
    REQUIRE(out[0] == 0xBB);
    REQUIRE(ep.drain_one(out));
    REQUIRE(out[0] == 0xCC);
    REQUIRE_FALSE(ep.drain_one(out));
    REQUIRE(ep.queue_len() == 0);
}

TEST_CASE("submit on a queue at capacity still reports queued, not executed",
          "[server][REQ-SRV-001]") {
    // kMaxQueuedFrames is this port's own fixed bound (c-RCP's own queue is
    // unbounded) — filling it exercises the same "queued but not actually
    // stored" contract c-RCP documents for a realloc() failure.
    Endpoint ep(false);
    uint8_t  body[] = {1};

    for (size_t i = 0; i < kMaxQueuedFrames; ++i) {
        REQUIRE_FALSE(ep.submit(body, sizeof(body), nullptr));
    }
    REQUIRE(ep.queue_len() == kMaxQueuedFrames);

    REQUIRE_FALSE(ep.submit(body, sizeof(body), nullptr));
    REQUIRE(ep.queue_len() == kMaxQueuedFrames); // unchanged: the push was refused
}

// ── §12.3.1.3: requests arriving at a disabled endpoint ──────────────────────

TEST_CASE("disabled endpoint executes ABB configuration requests immediately",
          "[server][REQ-SRV-015]") {
    Endpoint ep(false);
    auto     frame = standard_abb(5, 0x42, /*evt=*/0x07); // evt[2:0] = 111b

    REQUIRE(ep.submit(frame.data(), frame.size(), nullptr));
    REQUIRE(ep.queue_len() == 0);
}

TEST_CASE("disabled endpoint executes a non-CompoundWait GBB configuration request immediately",
          "[server][REQ-SRV-015]") {
    Endpoint             ep(false);
    request::CompoundStep step;
    auto frame = request::encode_compound_request(request::RequestTypeOpcode::Compound, 5, step,
                                                    /*evt_op=*/0x07, 0x43);

    REQUIRE(ep.submit(frame.data(), frame.size(), nullptr));
    REQUIRE(ep.queue_len() == 0);
}

TEST_CASE("disabled endpoint still queues an operational ABB request", "[server][REQ-SRV-015]") {
    Endpoint ep(false);
    auto     frame = standard_abb(5, 0x42, /*evt=*/0x00);

    REQUIRE_FALSE(ep.submit(frame.data(), frame.size(), nullptr));
    REQUIRE(ep.queue_len() == 1);

    std::optional<request::RequestTypeOpcode> rt;
    size_t                                     idx = 0;
    REQUIRE(ep.admit(frame.data(), frame.size(), 0, false, 0, 0, rt, &idx, nullptr) ==
            AdmitOutcome::Queued);
    REQUIRE_FALSE(rt.has_value());
    REQUIRE(ep.queue_len() == 2);
}

TEST_CASE("disabled endpoint still queues a CompoundWait request even with evt[2:0]=111b "
          "(REQ-SRV-015 deviation pin)",
          "[server][REQ-SRV-015]") {
    Endpoint             ep(false);
    request::CompoundStep step;
    auto frame = request::encode_compound_request(request::RequestTypeOpcode::CompoundWait, 5, step,
                                                    /*evt_op=*/0x07, 0x44);

    REQUIRE_FALSE(ep.submit(frame.data(), frame.size(), nullptr));
    REQUIRE(ep.queue_len() == 1);
}

TEST_CASE("disabled endpoint queuing emits the requested acknowledge", "[server][REQ-SRV-016]") {
    Endpoint ep(false);

    auto frame_wants_ack = standard_abb(5, 0x42, /*evt=*/0x08);
    std::vector<uint8_t> ack;
    REQUIRE_FALSE(ep.submit(frame_wants_ack.data(), frame_wants_ack.size(), &ack));
    REQUIRE(ep.queue_len() == 1);
    REQUIRE_FALSE(ack.empty());

    acf::AcfMessageInfo ack_hdr;
    acf::decode_acf_message_info(ack.data(), ack_hdr);
    REQUIRE(acf::response_kind_of(ack_hdr) == acf::ResponseKind::Acknowledge);
    REQUIRE(ack_hdr.byte_bus_id == 5);
    REQUIRE(ack_hdr.transaction_num == 0x42);

    auto frame_no_ack = standard_abb(5, 0x43, /*evt=*/0x00);
    ack.clear();
    REQUIRE_FALSE(ep.submit(frame_no_ack.data(), frame_no_ack.size(), &ack));
    REQUIRE(ep.queue_len() == 2);
    REQUIRE(ack.empty());
}

TEST_CASE("a truncated GBB frame too short to peek request_type is conservatively queued",
          "[server][REQ-SRV-015]") {
    // A GBB frame truncated to exactly the 8-octet header, no request_type
    // octet at all: frame_len >= 8 (submit()'s own outer gate still admits
    // it) but < 9 (peek_gbb_request_type() must bail before ever reading
    // frame[8]).
    Endpoint            ep(false);
    acf::AcfMessageInfo hdr;
    hdr.acf_msg_type     = acf::kAcfMsgTypeGbb;
    hdr.byte_bus_id      = 5;
    hdr.transaction_num  = 0x44;
    hdr.evt_op           = 0x07; // would be config-write, IF classifiable
    uint8_t frame[8];
    acf::encode_acf_message_info(hdr, frame);

    REQUIRE_FALSE(ep.submit(frame, sizeof(frame), nullptr));
    REQUIRE(ep.queue_len() == 1);
}

// ── REQ-PWRMODE-028: admission suspended during a sleep drain ───────────────

TEST_CASE("admission is suspended during a sleep drain", "[server][REQ-PWRMODE-028]") {
    Endpoint ep(true);
    auto     frame = standard_abb(5, 1);

    std::optional<request::RequestTypeOpcode> rt;
    REQUIRE(ep.admit(frame.data(), frame.size(), 0, false, 0, 0, rt, nullptr, nullptr) ==
            AdmitOutcome::ExecuteNow);

    ep.set_admission_suspended(true);
    REQUIRE(ep.admit(frame.data(), frame.size(), 0, false, 0, 0, rt, nullptr, nullptr) ==
            AdmitOutcome::Suspended);
    REQUIRE(ep.queue_len() == 0);

    ep.set_admission_suspended(false);
    REQUIRE(ep.admit(frame.data(), frame.size(), 0, false, 0, 0, rt, nullptr, nullptr) ==
            AdmitOutcome::ExecuteNow);
}

// ── REQ-ACF-021: a response frame is never admitted as a request ────────────

TEST_CASE("admit rejects a frame whose rsp bit is set", "[server][REQ-ACF-021]") {
    Endpoint            ep(true);
    acf::AcfMessageInfo info;
    info.byte_bus_id     = 5;
    info.transaction_num = 1;
    info.rsp             = true;
    auto frame = acf::encode_acf_abb(info, {});

    std::optional<request::RequestTypeOpcode> rt;
    std::optional<acf::WireErrorCode>         err;
    REQUIRE(ep.admit(frame.data(), frame.size(), 0, false, 0, 0, rt, nullptr, &err) ==
            AdmitOutcome::Rejected);
    REQUIRE(err == acf::WireErrorCode::InvalidParameter);
}

// ── admit(): standard requests are unaffected by conditional routing ────────

TEST_CASE("standard NTSCF request executes immediately when enabled", "[server][REQ-SRV-004]") {
    Endpoint ep(true);
    auto     frame = standard_abb(5, 1);

    std::optional<request::RequestTypeOpcode> rt;
    REQUIRE(ep.admit(frame.data(), frame.size(), 0, false, 0, 0, rt, nullptr, nullptr) ==
            AdmitOutcome::ExecuteNow);
    REQUIRE_FALSE(rt.has_value());
}

TEST_CASE("admit reports a cancellation with its opcode, unstored", "[server][REQ-SRV-004]") {
    Endpoint ep(true);
    auto     frame = request::encode_clear_all(5, 1);

    std::optional<request::RequestTypeOpcode> rt;
    REQUIRE(ep.admit(frame.data(), frame.size(), 0, false, 0, 0, rt, nullptr, nullptr) ==
            AdmitOutcome::Cancellation);
    REQUIRE(rt == request::RequestTypeOpcode::ClearAll);
    REQUIRE(ep.pending_count() == 0);
}

TEST_CASE("a request store full of pending entries rejects a new conditional admission",
          "[server][REQ-SRV-004]") {
    Endpoint              ep(true);
    request::TriggeredStep step;

    for (uint8_t i = 0; i < kMaxPending; ++i) {
        auto frame = request::encode_triggered_request(request::RequestTypeOpcode::Triggered, 5, step, i);
        std::optional<request::RequestTypeOpcode> rt;
        REQUIRE(ep.admit(frame.data(), frame.size(), 0, false, 0, 0, rt, nullptr, nullptr) ==
                AdmitOutcome::Pending);
    }
    REQUIRE(ep.pending_count() == kMaxPending);

    auto frame = request::encode_triggered_request(request::RequestTypeOpcode::Triggered, 5, step,
                                                     static_cast<uint8_t>(kMaxPending));
    std::optional<request::RequestTypeOpcode> rt;
    std::optional<acf::WireErrorCode>         err;
    REQUIRE(ep.admit(frame.data(), frame.size(), 0, false, 0, 0, rt, nullptr, &err) ==
            AdmitOutcome::Rejected);
    REQUIRE(err == acf::WireErrorCode::ReqStorageOverflow);
}

TEST_CASE("CompoundWait admission rejects the reserved evt[2:0]=011b comparison mode",
          "[server][REQ-CMP-029]") {
    Endpoint             ep(true);
    request::CompoundStep step;
    auto frame = request::encode_compound_request(request::RequestTypeOpcode::CompoundWait, 5, step,
                                                    /*evt_op=*/0x03, 1);

    std::optional<request::RequestTypeOpcode> rt;
    std::optional<acf::WireErrorCode>         err;
    REQUIRE(ep.admit(frame.data(), frame.size(), 0, false, 0, 0, rt, nullptr, &err) ==
            AdmitOutcome::Rejected);
    REQUIRE(err == acf::WireErrorCode::UnsupportedCmd);
    REQUIRE(ep.pending_count() == 0);
}

TEST_CASE("an undecodable conditional request is rejected, not admitted", "[server][REQ-SRV-004]") {
    Endpoint ep(true);
    // A well-formed Timed request (opcode 0x0A, category Timed) whose own
    // mandatorily-zero reserved octet (frame offset 9, immediately after
    // the repurposed opcode byte at offset 8) is then corrupted by hand:
    // the message still peeks as request_type 0x0A/category Timed, so
    // admit() still routes it into decode_timed_request() — which now
    // rejects it (reserved_field_nonzero) — rather than silently admitting
    // or executing it.
    auto frame = *request::encode_timed_request(5, 0, 1);
    frame[acf::kAcfCommonHeaderLen + 1] = 0xFF;

    std::optional<request::RequestTypeOpcode> rt;
    size_t                                     idx = 0;
    REQUIRE(ep.admit(frame.data(), frame.size(), 0, false, 0, 0, rt, &idx, nullptr) ==
            AdmitOutcome::Rejected);
    REQUIRE(ep.pending_count() == 0);
}

// ── The execution-condition tick: select_due()/complete() ───────────────────

TEST_CASE("select_due reports nothing due with an empty store", "[server][REQ-SRV-006]") {
    Endpoint    ep(true);
    TickContext ctx;
    ctx.endpoint_idle = true;
    ctx.gptp_locked   = true;

    size_t out_index = 12345;
    REQUIRE_FALSE(ep.select_due(ctx, &out_index));
    REQUIRE(out_index == 12345); // left untouched
}

TEST_CASE("compound never becomes due without a sequencer table", "[server][REQ-SRV-006]") {
    Endpoint              ep(true);
    request::CompoundStep step;
    step.start_state = 0; // "any state" wildcard
    auto frame = request::encode_compound_request(request::RequestTypeOpcode::Compound, 1, step, 0, 1);

    std::optional<request::RequestTypeOpcode> rt;
    size_t                                     idx = 0;
    REQUIRE(ep.admit(frame.data(), frame.size(), 0, false, 0, 0, rt, &idx, nullptr) ==
            AdmitOutcome::Pending);

    TickContext ctx;
    ctx.endpoint_idle = true;
    ctx.sequencers    = nullptr;
    size_t due = 0;
    REQUIRE_FALSE(ep.select_due(ctx, &due));

    std::vector<regmap::SequencerState> states;
    request::SequencerTable              seqs(states);
    seqs.ensure_size(1);
    ctx.sequencers = &seqs;
    REQUIRE(ep.select_due(ctx, &due));
    REQUIRE(due == idx);
}

TEST_CASE("due requests execute in priority order regardless of arrival order",
          "[server][REQ-SRV-007][REQ-SRV-008]") {
    Endpoint ep(true);
    std::vector<regmap::SequencerState> states;
    request::SequencerTable              seqs(states);
    seqs.ensure_size(1);

    request::CompoundStep compound_step;
    compound_step.start_state = request::SequencerTable::kDefaultState;
    auto compound = request::encode_compound_request(request::RequestTypeOpcode::Compound, 1,
                                                       compound_step, 0, 51);
    auto timed    = *request::encode_timed_request(1, 0, 52);
    request::TriggeredStep triggered_step;
    auto triggered =
        request::encode_triggered_request(request::RequestTypeOpcode::Triggered, 1, triggered_step, 53);

    // Arrival order is deliberately the reverse of priority order.
    std::optional<request::RequestTypeOpcode> rt;
    size_t idx_c = 0, idx_t = 0, idx_r = 0;
    REQUIRE(ep.admit(compound.data(), compound.size(), 0, false, 0, 0, rt, &idx_c, nullptr) ==
            AdmitOutcome::Pending);
    REQUIRE(ep.admit(timed.data(), timed.size(), 0, false, 0, 0, rt, &idx_t, nullptr) ==
            AdmitOutcome::Pending);
    REQUIRE(ep.admit(triggered.data(), triggered.size(), 0, false, 0, 0, rt, &idx_r, nullptr) ==
            AdmitOutcome::Pending);
    REQUIRE(ep.pending_count() == 3);
    REQUIRE(ep.notify_trigger(0, 0) == 1);

    TickContext ctx;
    ctx.endpoint_idle = true;
    ctx.gptp_locked   = true;
    ctx.gptp_now      = 0;
    ctx.sequencers    = &seqs;

    size_t due = 0;
    REQUIRE(ep.select_due(ctx, &due));
    REQUIRE(due == idx_r); // Triggered: rank 1
    REQUIRE_FALSE(ep.complete(due, ctx));

    REQUIRE(ep.select_due(ctx, &due));
    REQUIRE(due == idx_t); // Timed: rank 2
    REQUIRE_FALSE(ep.complete(due, ctx));

    REQUIRE(ep.select_due(ctx, &due));
    REQUIRE(due == idx_c); // Compound: rank 3
    REQUIRE_FALSE(ep.complete(due, ctx));

    REQUIRE_FALSE(ep.select_due(ctx, &due));
}

TEST_CASE("equal-rank requests execute in arrival order", "[server][REQ-SCHED-003]") {
    Endpoint ep(true);
    std::vector<regmap::SequencerState> states;
    request::SequencerTable              seqs(states);
    seqs.ensure_size(1);

    request::CompoundStep step;
    step.start_state = request::SequencerTable::kDefaultState;
    auto first  = request::encode_compound_request(request::RequestTypeOpcode::Compound, 1, step, 0, 61);
    auto second = request::encode_compound_request(request::RequestTypeOpcode::Compound, 1, step, 0, 62);

    std::optional<request::RequestTypeOpcode> rt;
    size_t idx1 = 0, idx2 = 0;
    ep.admit(first.data(), first.size(), 0, false, 0, 0, rt, &idx1, nullptr);
    ep.admit(second.data(), second.size(), 0, false, 0, 0, rt, &idx2, nullptr);

    TickContext ctx;
    ctx.endpoint_idle = true;
    ctx.sequencers    = &seqs;

    size_t due = 0;
    REQUIRE(ep.select_due(ctx, &due));
    REQUIRE(due == idx1);
    REQUIRE(ep.pending(due)->transaction_num == 61);
    ep.complete(due, ctx);

    REQUIRE(ep.select_due(ctx, &due));
    REQUIRE(due == idx2);
    REQUIRE(ep.pending(due)->transaction_num == 62);
}

// ── Repetitions ──────────────────────────────────────────────────────────────

TEST_CASE("repeat_count controls how often a request runs", "[server][REQ-SRV-010][REQ-SRV-036]") {
    Endpoint ep(true);
    std::vector<regmap::SequencerState> states;
    request::SequencerTable              seqs(states);
    seqs.ensure_size(1);

    request::CompoundStep step;
    step.start_state  = request::SequencerTable::kDefaultState;
    step.repeat_count = 2; // this execution plus two more, then removed
    auto frame = request::encode_compound_request(request::RequestTypeOpcode::Compound, 1, step, 0, 71);

    std::optional<request::RequestTypeOpcode> rt;
    size_t idx = 0;
    ep.admit(frame.data(), frame.size(), 0, false, 0, 0, rt, &idx, nullptr);

    TickContext ctx;
    ctx.endpoint_idle = true;
    ctx.sequencers    = &seqs;

    size_t due = 0;
    REQUIRE(ep.select_due(ctx, &due));
    REQUIRE(ep.complete(due, ctx));
    REQUIRE(ep.pending_count() == 1);

    REQUIRE(ep.select_due(ctx, &due));
    REQUIRE(ep.complete(due, ctx));
    REQUIRE(ep.pending_count() == 1);

    REQUIRE(ep.select_due(ctx, &due));
    REQUIRE_FALSE(ep.complete(due, ctx));
    REQUIRE(ep.pending_count() == 0);

    REQUIRE_FALSE(ep.select_due(ctx, &due));
}

TEST_CASE("an infinite repeat_count is never exhausted", "[server][REQ-SRV-035]") {
    Endpoint ep(true);
    std::vector<regmap::SequencerState> states;
    request::SequencerTable              seqs(states);
    seqs.ensure_size(1);

    request::CompoundStep step;
    step.start_state  = request::SequencerTable::kDefaultState;
    step.repeat_count = request::kCompoundRepeatInfinite;
    auto frame = request::encode_compound_request(request::RequestTypeOpcode::Compound, 1, step, 0, 72);

    std::optional<request::RequestTypeOpcode> rt;
    size_t idx = 0;
    ep.admit(frame.data(), frame.size(), 0, false, 0, 0, rt, &idx, nullptr);

    TickContext ctx;
    ctx.endpoint_idle = true;
    ctx.sequencers    = &seqs;

    for (int i = 0; i < 5; ++i) {
        size_t due = 0;
        REQUIRE(ep.select_due(ctx, &due));
        REQUIRE(ep.complete(due, ctx));
        REQUIRE(ep.pending_count() == 1);
    }
}

// ── Cancellation ─────────────────────────────────────────────────────────────

TEST_CASE("clear-all empties the request store", "[server][REQ-SRV-013]") {
    Endpoint               ep(true);
    request::TriggeredStep step;
    auto f1 = request::encode_triggered_request(request::RequestTypeOpcode::Triggered, 1, step, 1);
    auto f2 = request::encode_triggered_request(request::RequestTypeOpcode::Triggered, 1, step, 2);

    std::optional<request::RequestTypeOpcode> rt;
    ep.admit(f1.data(), f1.size(), 0, false, 0, 0, rt, nullptr, nullptr);
    ep.admit(f2.data(), f2.size(), 0, false, 0, 0, rt, nullptr, nullptr);
    REQUIRE(ep.pending_count() == 2);

    REQUIRE(ep.cancel_all() == 2);
    REQUIRE(ep.pending_count() == 0);
}

TEST_CASE("clear-single removes only its own target", "[server][REQ-SRV-040]") {
    Endpoint               ep(true);
    request::TriggeredStep step;
    auto f1 = request::encode_triggered_request(request::RequestTypeOpcode::Triggered, 1, step, 10);
    auto f2 = request::encode_triggered_request(request::RequestTypeOpcode::Triggered, 1, step, 11);

    std::optional<request::RequestTypeOpcode> rt;
    size_t idx2 = 0;
    ep.admit(f1.data(), f1.size(), 0, false, 0, 0, rt, nullptr, nullptr);
    ep.admit(f2.data(), f2.size(), 0, false, 0, 0, rt, &idx2, nullptr);

    REQUIRE(ep.cancel_single(10, CancelLifecycle::Queued) == CancelResult::Canceled);
    REQUIRE(ep.pending_count() == 1);
    REQUIRE(ep.pending(idx2) != nullptr);
    REQUIRE(ep.pending(idx2)->transaction_num == 11);
}

TEST_CASE("clear-single reports NotFound for an untracked transaction_num",
          "[server][REQ-SRV-040]") {
    Endpoint ep(true);
    REQUIRE(ep.cancel_single(1, CancelLifecycle::Queued) == CancelResult::NotFound);
}

TEST_CASE("clear-single reports NotCancellable for a request past the queued window",
          "[server][REQ-SRV-040]") {
    Endpoint               ep(true);
    request::TriggeredStep step;
    auto f = request::encode_triggered_request(request::RequestTypeOpcode::Triggered, 1, step, 10);
    std::optional<request::RequestTypeOpcode> rt;
    ep.admit(f.data(), f.size(), 0, false, 0, 0, rt, nullptr, nullptr);

    REQUIRE(ep.cancel_single(10, CancelLifecycle::Executing) == CancelResult::NotCancellable);
    REQUIRE(ep.pending_count() == 1); // not removed
}

TEST_CASE("clear-non-safestate keeps safety-tagged requests, removes the rest",
          "[server][REQ-SRV-041]") {
    Endpoint               ep(true);
    request::TriggeredStep step;
    auto normal = request::encode_triggered_request(request::RequestTypeOpcode::Triggered, 1, step, 1);
    auto safety =
        request::encode_triggered_request(request::RequestTypeOpcode::TriggeredSafety, 1, step, 2);

    std::optional<request::RequestTypeOpcode> rt;
    size_t idx_safety = 0;
    ep.admit(normal.data(), normal.size(), 0, false, 0, 0, rt, nullptr, nullptr);
    ep.admit(safety.data(), safety.size(), 0, false, 0, 0, rt, &idx_safety, nullptr);

    REQUIRE(ep.cancel_non_safestate() == 1);
    REQUIRE(ep.pending_count() == 1);
    REQUIRE(ep.pending(idx_safety) != nullptr);
}

TEST_CASE("watchdog_purge keeps only the safety sequence", "[server][REQ-SRV-014]") {
    Endpoint               ep(true);
    request::TriggeredStep step;
    auto normal = request::encode_triggered_request(request::RequestTypeOpcode::Triggered, 1, step, 1);
    auto safety =
        request::encode_triggered_request(request::RequestTypeOpcode::TriggeredSafety, 1, step, 2);

    std::optional<request::RequestTypeOpcode> rt;
    size_t idx_safety = 0;
    ep.admit(normal.data(), normal.size(), 0, false, 0, 0, rt, nullptr, nullptr);
    ep.admit(safety.data(), safety.size(), 0, false, 0, 0, rt, &idx_safety, nullptr);

    REQUIRE(ep.watchdog_purge() == 1);
    REQUIRE(ep.pending_count() == 1);
    REQUIRE(ep.pending(idx_safety) != nullptr);
}

// A safety-tagged request stays in the store until the endpoint reaches its
// configured safe state.
TEST_CASE("safety-tagged request waits for safe state", "[server][REQ-SRV-006]") {
    Endpoint               ep(true);
    request::TriggeredStep step;
    auto frame =
        request::encode_triggered_request(request::RequestTypeOpcode::TriggeredSafety, 1, step, 1);
    std::optional<request::RequestTypeOpcode> rt;
    size_t                                     idx = 0;
    ep.admit(frame.data(), frame.size(), 0, false, 0, 0, rt, &idx, nullptr);
    REQUIRE(ep.notify_trigger(0, 0) == 1);

    TickContext ctx;
    ctx.endpoint_idle = true;
    ctx.in_safe_state = false;
    size_t due = 0;
    REQUIRE_FALSE(ep.select_due(ctx, &due));

    ctx.in_safe_state = true;
    REQUIRE(ep.select_due(ctx, &due));
    REQUIRE(due == idx);
}

TEST_CASE("REQ-CANCEL-012: cancelling a chain's first member cascades to its successor",
          "[server][REQ-CANCEL-012]") {
    Endpoint ep(true);
    auto m1 = request::encode_chained_member(1, 10, false, 201);
    auto m2 = request::encode_chained_member(1, 10, false, 202);

    std::optional<request::RequestTypeOpcode> rt;
    size_t idx1 = 0, idx2 = 0;
    ep.admit(m1.data(), m1.size(), 0, false, 0, 0, rt, &idx1, nullptr);
    ep.admit(m2.data(), m2.size(), 0, false, 0, 0, rt, &idx2, nullptr);
    REQUIRE(ep.pending_count() == 2);

    // Caller (a future mock.hpp) assigns chain_group/chain_position once it
    // knows the enclosing frame's own member layout — this store cannot
    // derive either itself from one admitted frame.
    ep.pending(idx1)->chain_group    = 7;
    ep.pending(idx1)->chain_position = 0;
    ep.pending(idx2)->chain_group    = 7;
    ep.pending(idx2)->chain_position = 1;

    REQUIRE(ep.cancel_chain_from(7, 0) == 2);
    REQUIRE(ep.pending_count() == 0);
}

TEST_CASE("REQ-CANCEL-012: cascade does not cross unrelated chains", "[server][REQ-CANCEL-012]") {
    Endpoint ep(true);
    auto m1 = request::encode_chained_member(1, 10, false, 211);
    auto m2 = request::encode_chained_member(1, 10, false, 221);

    std::optional<request::RequestTypeOpcode> rt;
    size_t idx1 = 0, idx2 = 0;
    ep.admit(m1.data(), m1.size(), 0, false, 0, 0, rt, &idx1, nullptr);
    ep.admit(m2.data(), m2.size(), 0, false, 0, 0, rt, &idx2, nullptr);

    ep.pending(idx1)->chain_group    = 1;
    ep.pending(idx1)->chain_position = 0;
    ep.pending(idx2)->chain_group    = 2; // a different chain entirely
    ep.pending(idx2)->chain_position = 0;

    REQUIRE(ep.cancel_chain_from(1, 0) == 1);
    REQUIRE(ep.pending_count() == 1);
    REQUIRE(ep.pending(idx2) != nullptr);
}

TEST_CASE("cancel_chain_from(0, ...) is a no-op: 0 is the not-part-of-a-chain sentinel",
          "[server][REQ-CANCEL-012]") {
    Endpoint ep(true);
    auto     m1 = request::encode_chained_member(1, 10, false, 1);
    std::optional<request::RequestTypeOpcode> rt;
    ep.admit(m1.data(), m1.size(), 0, false, 0, 0, rt, nullptr, nullptr);

    REQUIRE(ep.cancel_chain_from(0, 0) == 0);
    REQUIRE(ep.pending_count() == 1);
}

// ── Trigger occurrence delivery ──────────────────────────────────────────────

TEST_CASE("notify_trigger only matches a Triggered request's own selection",
          "[server][REQ-SRV-011]") {
    Endpoint               ep(true);
    request::TriggeredStep step;
    step.trigger_source_ep = 3;
    step.trigger_signal_nr = 4;
    auto frame = request::encode_triggered_request(request::RequestTypeOpcode::Triggered, 1, step, 1);
    std::optional<request::RequestTypeOpcode> rt;
    ep.admit(frame.data(), frame.size(), 0, false, 0, 0, rt, nullptr, nullptr);

    REQUIRE(ep.notify_trigger(3, 5) == 0); // wrong signal_nr
    REQUIRE(ep.notify_trigger(9, 4) == 0); // wrong source_ep
    REQUIRE(ep.notify_trigger(3, 4) == 1); // matches
}

// ── Chained: predecessor-done bookkeeping ────────────────────────────────────

TEST_CASE("chain_predecessor_done arms a chained request's exec_delay timer",
          "[server][REQ-SRV-012]") {
    Endpoint ep(true);
    auto     frame = request::encode_chained_member(1, 5, false, 1);
    std::optional<request::RequestTypeOpcode> rt;
    size_t                                     idx = 0;
    ep.admit(frame.data(), frame.size(), 0, false, 0, 0, rt, &idx, nullptr);

    TickContext ctx;
    ctx.endpoint_idle = true;
    ctx.now           = 100;

    size_t due = 0;
    REQUIRE_FALSE(ep.select_due(ctx, &due)); // predecessor not done yet

    REQUIRE(ep.chain_predecessor_done(idx, 100));
    ctx.now = 104; // 4 ticks elapsed, delay is 5: not yet
    REQUIRE_FALSE(ep.select_due(ctx, &due));
    ctx.now = 105;
    REQUIRE(ep.select_due(ctx, &due));
    REQUIRE(due == idx);
}

TEST_CASE("chain_predecessor_done returns false for a non-chained or unused index",
          "[server][REQ-SRV-012]") {
    Endpoint ep(true);
    REQUIRE_FALSE(ep.chain_predecessor_done(0, 0));

    auto frame = standard_abb(1, 1);
    std::optional<request::RequestTypeOpcode> rt;
    size_t                                     idx = 0;
    ep.admit(frame.data(), frame.size(), 0, false, 0, 0, rt, &idx, nullptr);
    // The above is a Standard request executed immediately (enabled
    // endpoint), so nothing was actually stored — chain_predecessor_done()
    // on any index still reports false against an empty store.
    REQUIRE_FALSE(ep.chain_predecessor_done(0, 0));
}

// ── §13.7.1.3 Table 37: the RC Server's own PTP time-synch trigger signals ──

TEST_CASE("gptp_trigger_evaluate derives a signal and composes with notify_trigger",
          "[server][REQ-SRV-018]") {
    Endpoint                ep(true);
    request::TriggeredStep  step;
    step.trigger_source_ep = 0; // EP0 -- where Table 37's server signals originate
    step.trigger_signal_nr = kGptpTriggerEstablished;
    auto frame = request::encode_triggered_request(request::RequestTypeOpcode::Triggered, 5, step, 1);

    std::optional<request::RequestTypeOpcode> rt;
    size_t                                     idx = 0;
    REQUIRE(ep.admit(frame.data(), frame.size(), 0, false, 0, 0, rt, &idx, nullptr) ==
            AdmitOutcome::Pending);

    TickContext ctx;
    ctx.endpoint_idle = true;

    GptpTriggerState trig;

    // The very first observation is never itself a transition.
    REQUIRE_FALSE(gptp_trigger_evaluate(trig, false).has_value());
    size_t due = 0;
    REQUIRE_FALSE(ep.select_due(ctx, &due));

    // unlocked -> locked: signal 0 (Established). Observing it alone
    // changes nothing until the derived signal is actually delivered.
    ctx.gptp_locked = true;
    auto signal     = gptp_trigger_evaluate(trig, true);
    REQUIRE(signal == kGptpTriggerEstablished);
    REQUIRE_FALSE(ep.select_due(ctx, &due));

    REQUIRE(ep.notify_trigger(0, *signal) == 1);
    REQUIRE(ep.select_due(ctx, &due));
    REQUIRE(due == idx);

    // A repeated observation at the same lock state is not a transition.
    REQUIRE_FALSE(gptp_trigger_evaluate(trig, true).has_value());

    // locked -> unlocked: the symmetric edge, signal 1 (Lost).
    REQUIRE(gptp_trigger_evaluate(trig, false) == kGptpTriggerLost);
}

TEST_CASE("gptp_trigger_evaluate: a first-ever observation is never an edge",
          "[server][REQ-SRV-018]") {
    GptpTriggerState trig;
    REQUIRE_FALSE(gptp_trigger_evaluate(trig, true).has_value());
    REQUIRE_FALSE(gptp_trigger_evaluate(trig, true).has_value()); // staying at true: still no edge
}

// ── §11.2/§11.2.1: TSCF-carried requests (REQ-TIMED-012) ────────────────────

TEST_CASE("NTSCF standard request still executes immediately", "[server][REQ-TIMED-012]") {
    Endpoint ep(true);
    auto     frame = standard_abb(5, 1);
    std::optional<request::RequestTypeOpcode> rt;
    REQUIRE(ep.admit(frame.data(), frame.size(), 0, false, 0, 0, rt, nullptr, nullptr) ==
            AdmitOutcome::ExecuteNow);
    REQUIRE_FALSE(rt.has_value());
}

TEST_CASE("TSCF standard request is postponed until its presentation time",
          "[server][REQ-TIMED-012]") {
    Endpoint ep(true);
    auto     frame = standard_abb(5, 1);

    std::optional<request::RequestTypeOpcode> rt;
    size_t                                     idx = 0;
    REQUIRE(ep.admit(frame.data(), frame.size(), 0, true, 5000000u, 0, rt, &idx, nullptr) ==
            AdmitOutcome::Pending);
    REQUIRE_FALSE(rt.has_value());

    TickContext ctx;
    ctx.endpoint_idle = true;

    ctx.gptp_locked = true;
    ctx.gptp_now    = 4999999u;
    size_t due      = 0;
    REQUIRE_FALSE(ep.select_due(ctx, &due));

    ctx.gptp_locked = false; // reached the instant, but not locked: fail-closed
    ctx.gptp_now    = 5000000u;
    REQUIRE_FALSE(ep.select_due(ctx, &due));

    ctx.gptp_locked = true;
    REQUIRE(ep.select_due(ctx, &due));
    REQUIRE(due == idx);
}

TEST_CASE("a TSCF conditional request needs both its own condition and the envelope gate",
          "[server][REQ-TIMED-012]") {
    Endpoint                ep(true);
    request::TriggeredStep  step; // trigger_threshold 0: fires on the first occurrence
    step.trigger_source_ep = 0;
    step.trigger_signal_nr = 0;
    auto frame = request::encode_triggered_request(request::RequestTypeOpcode::Triggered, 5, step, 1);

    std::optional<request::RequestTypeOpcode> rt;
    size_t                                     idx = 0;
    REQUIRE(ep.admit(frame.data(), frame.size(), 0, true, 5000000u, 0, rt, &idx, nullptr) ==
            AdmitOutcome::Pending);

    TickContext ctx;
    ctx.endpoint_idle = true;
    ctx.gptp_locked   = true;
    ctx.gptp_now      = 5000000u; // presentation gate open

    size_t due = 0;
    REQUIRE_FALSE(ep.select_due(ctx, &due)); // trigger threshold not reached

    REQUIRE(ep.notify_trigger(0, 0) == 1);
    ctx.gptp_now = 0; // gate closed again
    REQUIRE_FALSE(ep.select_due(ctx, &due));

    ctx.gptp_now = 5000000u;
    REQUIRE(ep.select_due(ctx, &due));
    REQUIRE(due == idx);
}

TEST_CASE("a TSCF-postponed Standard request executes once due even while the endpoint stays "
          "disabled",
          "[server][REQ-TIMED-012]") {
    Endpoint ep(false); // disabled the entire time
    auto     frame = standard_abb(5, 1);

    std::optional<request::RequestTypeOpcode> rt;
    size_t                                     idx = 0;
    REQUIRE(ep.admit(frame.data(), frame.size(), 0, true, 5000000u, 0, rt, &idx, nullptr) ==
            AdmitOutcome::Pending);
    REQUIRE_FALSE(rt.has_value());

    TickContext ctx;
    ctx.endpoint_idle = true;
    ctx.gptp_locked   = true;
    ctx.gptp_now      = 4999999u;

    size_t due = 0;
    REQUIRE_FALSE(ep.select_due(ctx, &due));

    ctx.gptp_now = 5000000u;
    REQUIRE(ep.select_due(ctx, &due));
    REQUIRE(due == idx);
}

TEST_CASE("a disabled endpoint's stored Compound request never becomes due, however long its "
          "own condition has held",
          "[server][REQ-SRV-015]") {
    Endpoint ep(false);
    std::vector<regmap::SequencerState> states;
    request::SequencerTable              seqs(states);
    seqs.ensure_size(1);

    request::CompoundStep step;
    step.start_state = request::SequencerTable::kDefaultState; // "any state" would fire
    auto frame = request::encode_compound_request(request::RequestTypeOpcode::Compound, 1, step, 0, 1);

    std::optional<request::RequestTypeOpcode> rt;
    size_t                                     idx = 0;
    REQUIRE(ep.admit(frame.data(), frame.size(), 0, false, 0, 0, rt, &idx, nullptr) ==
            AdmitOutcome::Pending);

    TickContext ctx;
    ctx.endpoint_idle = true;
    ctx.sequencers    = &seqs;

    size_t due = 0;
    REQUIRE_FALSE(ep.select_due(ctx, &due));

    ep.set_enable(true);
    REQUIRE(ep.select_due(ctx, &due));
    REQUIRE(due == idx);
}

// ── admit_with_ack(): TSCF-stored requests' own acknowledge (issue #463 analogue) ─

TEST_CASE("a TSCF-pending standard request emits its requested acknowledge at admission time",
          "[server][REQ-SRV-016]") {
    Endpoint ep(true);
    auto     frame = standard_abb(5, 0x42, /*evt=*/0x08);

    std::optional<request::RequestTypeOpcode> rt;
    size_t                                     idx = 0;
    std::vector<uint8_t>                       ack;
    REQUIRE(ep.admit_with_ack(frame.data(), frame.size(), 0, true, 5000000u, 0, rt, &idx, nullptr,
                               &ack) == AdmitOutcome::Pending);
    REQUIRE_FALSE(ack.empty());

    acf::AcfMessageInfo ack_hdr;
    acf::decode_acf_message_info(ack.data(), ack_hdr);
    REQUIRE(acf::response_kind_of(ack_hdr) == acf::ResponseKind::Acknowledge);
    REQUIRE_FALSE(ack_hdr.err);
    REQUIRE(ack_hdr.byte_bus_id == 5);
    REQUIRE(ack_hdr.transaction_num == 0x42);
}

TEST_CASE("a TSCF-pending standard request emits no acknowledge when evt[3] is clear",
          "[server][REQ-SRV-016]") {
    Endpoint ep(true);
    auto     frame = standard_abb(5, 0x43, /*evt=*/0x00);

    std::optional<request::RequestTypeOpcode> rt;
    size_t                                     idx = 0;
    std::vector<uint8_t>                       ack;
    REQUIRE(ep.admit_with_ack(frame.data(), frame.size(), 0, true, 5000000u, 0, rt, &idx, nullptr,
                               &ack) == AdmitOutcome::Pending);
    REQUIRE(ack.empty());
}

TEST_CASE("a pending conditional request emits its requested acknowledge", "[server][REQ-SRV-016]") {
    Endpoint                ep(true);
    request::TriggeredStep  step;
    auto frame = request::encode_triggered_request(request::RequestTypeOpcode::Triggered, 5, step, 0x50);

    // encode_triggered_request does not itself set evt[3]; build the frame
    // by hand instead so evt_ack is under this test's own control.
    acf::AcfMessageInfo hdr;
    acf::decode_acf_message_info(frame.data(), hdr);
    hdr.evt_ack = true;
    acf::encode_acf_message_info(hdr, frame.data());

    std::optional<request::RequestTypeOpcode> rt;
    size_t                                     idx = 0;
    std::vector<uint8_t>                       ack;
    REQUIRE(ep.admit_with_ack(frame.data(), frame.size(), 0, false, 0, 0, rt, &idx, nullptr, &ack) ==
            AdmitOutcome::Pending);
    REQUIRE_FALSE(ack.empty());

    acf::AcfMessageInfo ack_hdr;
    acf::decode_acf_message_info(ack.data(), ack_hdr);
    REQUIRE(acf::response_kind_of(ack_hdr) == acf::ResponseKind::Acknowledge);
    REQUIRE(ack_hdr.transaction_num == 0x50);
}
