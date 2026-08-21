// fusa:req REQ-SRV-001
// fusa:req REQ-SRV-002
// fusa:req REQ-SRV-003
// fusa:req REQ-SRV-004
// fusa:req REQ-SRV-005
// fusa:req REQ-SRV-006
// fusa:req REQ-SRV-007
// fusa:req REQ-SRV-008
// fusa:req REQ-SRV-009
// fusa:req REQ-SRV-010
// fusa:req REQ-SRV-011
// fusa:req REQ-SRV-012
// fusa:req REQ-SRV-013
// fusa:req REQ-SRV-014
// fusa:req REQ-SRV-015
// fusa:req REQ-SRV-016
// fusa:req REQ-SRV-017
// fusa:req REQ-SRV-018
// fusa:req REQ-SRV-019
// fusa:req REQ-SRV-020
// fusa:req REQ-SRV-021
// fusa:req REQ-SRV-022
// fusa:req REQ-SRV-023
// fusa:req REQ-SRV-024
// fusa:req REQ-SRV-025
// fusa:req REQ-SRV-026
// fusa:req REQ-SRV-027
// fusa:req REQ-SRV-028
// fusa:req REQ-SRV-029
// fusa:req REQ-SRV-030
// fusa:req REQ-SRV-031
// fusa:req REQ-SRV-032
// fusa:req REQ-SRV-033
// fusa:req REQ-SRV-034
// fusa:req REQ-SRV-035
// fusa:req REQ-SRV-036
// fusa:req REQ-SRV-037
// fusa:req REQ-SRV-038
// fusa:req REQ-SRV-039
// fusa:req REQ-SRV-040
// fusa:req REQ-SRV-041
// fusa:req REQ-SRV-042
// fusa:req REQ-PWRMODE-028
// fusa:req REQ-CANCEL-012
// fusa:req REQ-ACF-021
// fusa:req REQ-ACF-032
// fusa:req REQ-TIMED-012

// Per-endpoint admission queue and conditional-request scheduler for the
// OPEN Alliance TC18 Remote Control Protocol Specification v0.5.1_RC server
// (ROADMAP.md Phase 17/"Phase 4", cpp-RCP issue #129) — the request-queue/
// admission/scheduling surface c-RCP keeps in server.h/server.c, ported
// fresh into this rewrite.
//
// ── Where this fits relative to rcp/endpoint.hpp and rcp/request.hpp ────────
//
// rcp/endpoint.hpp is a DIFFERENT, narrower thing: shared endpoint-*type*
// registration/dispatch scaffolding (evt[2:0] write-semantics decode,
// saturating arithmetic, a generic trigger-signal enable/pending table).
// Nothing in it models a request queue, an admission decision, or a
// scheduling tick — this file does not touch it or duplicate it.
//
// rcp/request.hpp is c-RCP's request.c + request_sequencer.c + scheduler.c
// deliberately unified into one module, already ported: the conditional-
// request wire taxonomy (Compound/CompoundWait/Triggered/Timed/Chained, plus
// the three cancellation kinds), their encode/decode pairs,
// TriggeredRuntime, SequencerTable, RequestCategory/priority_rank/
// select_next_due, and RequestLedger (a lifecycle-state-machine request
// tracker that is itself a cpp-RCP-original data structure with no c-RCP
// counterpart). This file BUILDS ON that content rather than re-deriving
// it — every wire decode, every priority rule, every sequencer/trigger
// primitive below is a call into rcp::request, never a second definition of
// the same thing.
//
// What is genuinely still missing, and what this file adds, is c-RCP
// server.c's own two data structures and the operations that manage them:
//
//   1. The ep_enable pre-load-then-drain queue (rcp_server_endpoint_submit/
//      _set_enable/_drain_one/_queue_len in c-RCP) — a disabled endpoint
//      still accepts (queues) an incoming standard request without
//      executing it; queued requests drain out, FIFO, once re-enabled. No
//      counterpart exists anywhere in this codebase yet: RequestLedger
//      tracks lifecycle state for requests it is TOLD about, but nothing
//      upstream of it ever decided "queue this raw frame instead of
//      executing it" in the first place.
//
//   2. The per-endpoint conditional-request STORE (rcp_server_pending_t /
//      RCP_SERVER_MAX_PENDING in c-RCP) — fixed-capacity slots holding an
//      admitted conditional request's own decoded condition (CompoundStep,
//      TriggeredStep+TriggeredRuntime, presentation_time, chain_exec_delay)
//      from admission until it becomes due, plus the admission routing
//      (admit()/admit_with_ack()), the tick evaluation (select_due()), the
//      post-execution repetition rule (complete()), trigger-occurrence
//      delivery (notify_trigger()), the Table 37 gPTP-lock-edge trigger
//      tracker, chained-predecessor-done bookkeeping, and cancellation
//      (cancel_all/cancel_single/cancel_non_safestate/cancel_chain_from/
//      watchdog_purge). RequestLedger is NOT this: it is a request
//      *lifecycle* tracker (Pending -> Started -> UnderExecution ->
//      Finalized/Canceled) with no notion of "is this due right now" at
//      all — that question is this file's own SequencerTable/
//      TriggeredRuntime/e2e-safe-state/gPTP-presentation-time evaluation,
//      answered fresh on every tick, exactly mirroring server.c's own
//      is_due(). The two coexist by design, the same way c-RCP's own
//      server.c coexists with request.c/request_sequencer.c/scheduler.c:
//      this file decodes and stores; rcp::request supplies the taxonomy,
//      the per-kind predicates, and the priority ordering it evaluates
//      against.
//
// One place where this file's own model deliberately does NOT reuse
// RequestLedger's cascade machinery: chained-cancellation cascade
// (cancel_chain_from() below). c-RCP's server.c tracks a chain
// POSITIONALLY (chain_group/chain_position, assigned by the caller —
// mock.c's own dispatch_frame() — once it knows a frame's member layout,
// since this store has no way to derive that itself at admission time) and
// cascades via a pure chain_group/chain_position comparison
// (cancel_chain_should_cascade() below, ported from c-RCP's
// rcp_cancel_chain_should_cascade()). request.hpp's RequestLedger solves
// the SAME problem differently — as a real predecessor/successor graph it
// walks — which request.hpp's own file header calls out as "strictly more
// capable" and deliberately keeps instead of c-RCP's positional model
// *for RequestLedger's own callers*. This file's pending store is not a
// RequestLedger, has no graph of its own, and (matching c-RCP) needs the
// caller to assign chain_group/chain_position after admission — see
// Endpoint::pending()'s own doc comment below.
//
// ── Integration surface (for the mock.hpp dispatch-layer batch that lands
// after regmap/server/discovery, per this phase's own scope) ───────────────
//
// A future mock.hpp/dispatch loop is expected to, per endpoint:
//   - call admit_with_ack() (or admit()) for every inbound ACF frame, and
//     act on the returned AdmitOutcome (execute now / nothing further to
//     do; apply a queued/pending request's acknowledge if one was built;
//     decode+apply a cancellation via decode_clear_all/_single/
//     _non_safestate from rcp/request.hpp using the reported request_type);
//   - after admitting a Chained member, set its chain_group/chain_position
//     via pending() (see that method's own doc comment) — this file cannot
//     derive either from a lone frame;
//   - call select_due()/complete() once per scheduling tick, running the
//     selected slot's own frame between the two calls (this file executes
//     nothing itself, mirroring c-RCP's identical split);
//   - call notify_trigger() when a trigger signal actually occurs, and
//     drive gptp_trigger_evaluate()+notify_trigger() together on every
//     newly observed gPTP lock state (Table 37, §13.7.1.3);
//   - call chain_predecessor_done() once a chain member's predecessor
//     finalizes;
//   - call set_admission_suspended(true) at the start of a §13.7.2.3
//     SleepCMD drain (rcp/powerstate.hpp's own entry-gate check) and
//     set_admission_suspended(false) again if entry is ultimately refused;
//   - call watchdog_purge() from whatever drives rcp/watchdog.hpp's own
//     overflow callback for this endpoint's stream, and cancel_all()/
//     cancel_single()/cancel_non_safestate() from the matching cancellation
//     request kinds once decoded.
// None of that wiring happens in this file — matching this phase's own
// explicit scope, "you do NOT need to wire it into mock.hpp's dispatch yet".
//
// Field names, opcode values, and behavior below implement TC18's
// *behavior* as re-derived from c-RCP's current (RC5-conformant)
// src/server.c + include/rcp/server.h — no text from the confidential
// specification is reproduced here, same disclaimer as every other header
// in this codebase.
#pragma once

#include <rcp/request.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace rcp {
namespace server {

// ── Fixed-capacity bounds ─────────────────────────────────────────────────────

// kMaxPending mirrors c-RCP's own RCP_SERVER_MAX_PENDING (server.h) exactly:
// how many conditional/TSCF-gated requests one endpoint's store can hold at
// once.
constexpr size_t kMaxPending = 32;

// kMaxQueuedFrames bounds the ep_enable pre-load queue. c-RCP's own queue
// (server.c's ep->queue) is genuinely UNBOUNDED — it grows by doubling via
// realloc() with no capacity constant at all — so this bound is this port's
// own engineering decision, not a literal c-RCP value, made to satisfy this
// rewrite's fixed-capacity/no-dynamic-growth discipline for admission-path
// storage. c-RCP's own submit() already documents graceful degradation for
// this exact scenario: "Returns false (still meaning 'queued') without
// actually growing the queue if the internal reallocation fails — callers
// relying on eventual delivery under allocation failure must check
// rcp_server_endpoint_queue_len() themselves." A full fixed-capacity queue
// here reports the identical outcome (the push fails, submit() still
// returns false) through the identical caller contract — no behavior
// change, just a deterministic, inspectable capacity in place of a
// probabilistic allocator-failure one. 32 matches kMaxPending, the other
// fixed bound this same endpoint already carries.
constexpr size_t kMaxQueuedFrames = 32;

// ── Admission outcome ─────────────────────────────────────────────────────────
// Mirrors c-RCP's rcp_server_admit_t one-for-one.

enum class AdmitOutcome : uint8_t {
    // Standard request, endpoint enabled: caller must execute it now.
    ExecuteNow = 0,
    // Standard request, endpoint disabled: queued for drain_one().
    Queued = 1,
    // Conditional (or TSCF-gated standard/cancellation) request: stored;
    // surfaces from select_due() once due.
    Pending = 2,
    // Cancellation request (clear-all/clear-single/clear-non-safestate),
    // NOT TSCF-gated: not stored — caller applies it immediately via the
    // matching cancel_*() below, using the reported request_type to know
    // which of the three it is.
    Cancellation = 3,
    // Did not decode as its own opcode claims, or the store is full.
    Rejected = 4,
    // REQ-PWRMODE-028: admission_suspended was true. Nothing was inspected.
    Suspended = 5,
};

// ── The per-endpoint conditional-request store ────────────────────────────────
// One conditional (or TSCF-gate-postponed standard/cancellation) request
// held from admission until its condition is met (and, for a repeating
// request, until its repetitions are exhausted). Only the sub-object
// matching `category` is meaningful — ported from c-RCP's
// rcp_server_pending_t, restructured onto rcp::request's own decoded types
// (CompoundStep/TriggeredStep+TriggeredRuntime) instead of duplicating
// their fields.
struct PendingRequest {
    bool                                        in_use   = false;
    request::RequestCategory                    category = request::RequestCategory::Standard;
    // nullopt for a Standard request (including one postponed purely by a
    // TSCF presentation gate) — mirrors admit()'s own out_request_type
    // "0 for a standard request" convention, just spelled as an optional.
    std::optional<request::RequestTypeOpcode>   request_type;
    uint8_t                                      transaction_num = 0;
    uint64_t                                      sequence         = 0; // arrival order, FIFO tie-break
    std::vector<uint8_t>                          frame;               // owned copy of the whole ACF message

    // The decoded execution condition. Which is live is determined by
    // `category`.
    request::CompoundStep       compound;              // Compound / CompoundWait
    uint8_t                       compound_wait_evt    = 0; // CompoundWait only: its own evt[2:0]
    std::vector<uint8_t>          compound_wait_target;     // CompoundWait only: its own comparison target
    request::TriggeredStep      triggered;             // Triggered
    request::TriggeredRuntime   triggered_runtime;      // Triggered occurrence counter
    uint64_t                      presentation_time    = 0; // Timed
    uint16_t                      chain_exec_delay      = 0; // Chained
    bool                           chain_cs              = false; // Chained abort-on-error selector (cs)

    // REQ-CANCEL-012: this entry's own position within a chain, and which
    // chain it belongs to. Both are properties of the enclosing frame a
    // chain member was admitted from — this store has no way to derive
    // either itself at admission time (it sees one frame at a time, not a
    // whole multi-member AVTPDU); a caller that understands frame structure
    // sets both via pending() after admission succeeds. chain_group == 0 is
    // the "not part of a chain" sentinel every non-chain-grouped entry
    // carries by default; a real chain (including its own anchor member) is
    // assigned a chain_group != 0, unique among chains concurrently pending
    // on this endpoint. chain_position is 0 for a chain's own anchor member
    // and increases by one per successive chained follower.
    uint32_t chain_group    = 0;
    uint8_t  chain_position = 0;

    // Runtime bookkeeping. armed becomes true the moment this request's own
    // start condition first holds; armed_at records the tick count at that
    // instant, and the exec_delay timer runs from there.
    bool     armed    = false;
    uint32_t armed_at = 0;
    // Chained only: set once this member's predecessor has finalized.
    bool     predecessor_done = false;

    // REQ-TIMED-012: "postponed until [the TSCF header's own
    // avtp_timestamp] presentation time" applies to a request of ANY kind
    // carried under a TSCF header, independent of (ANDed with) that kind's
    // own existing execution condition above — an envelope-level gate, not
    // a per-kind one. false for a request admitted under an NTSCF header
    // (or a TSCF header with tv unset); presentation_gate_ns is meaningless
    // while false. When true, it holds the 48-bit-domain instant
    // avtp::extend_timestamp() reconstructed from the TSCF header's own
    // 32-bit avtp_timestamp at admission time (resolved ONCE, not re-
    // derived per tick), compared against TickContext::gptp_now the same
    // way Timed's own presentation_time already is.
    bool     has_presentation_gate = false;
    uint64_t presentation_gate_ns  = 0;
};

// ── The execution-condition tick ─────────────────────────────────────────────
// Everything select_due() needs to evaluate a stored request's execution
// condition. The caller owns every field; this module reads no clock and
// holds no sequencer table of its own. Ported from c-RCP's
// rcp_server_tick_ctx_t.
struct TickContext {
    // Current tick count, in the endpoint's own ep_delay_time unit — the
    // same unit every exec_delay sub-field is expressed in.
    uint32_t now = 0;
    // Current gPTP time, nanoseconds modulo 2^48, for timed requests.
    uint64_t gptp_now = 0;
    // Whether a gPTP time base is locked. Timed requests (and any request
    // under a TSCF presentation gate) never become due while false.
    bool gptp_locked = false;
    // The sequencer-state table Compound/CompoundWait requests read and
    // advance. nullptr means no Compound/CompoundWait request ever becomes
    // due.
    request::SequencerTable* sequencers = nullptr;
    // Whether the endpoint is idle right now. A Compound/Triggered/Chained
    // request never becomes due while the endpoint is busy.
    bool endpoint_idle = false;
    // Whether the endpoint has reached its configured safe state. Gates
    // every safety-tagged (0x8x) request — see rcp/e2e.hpp's
    // endpoint_in_configured_safe_state() for how a caller derives this.
    bool in_safe_state = false;
    // The endpoint's own current status bytes, for evaluating any pending
    // CompoundWait requests' comparison via acf::compound_wait_match().
    // May be {nullptr, 0} if the endpoint has no status representation, in
    // which case no CompoundWait request whose target is nonempty ever
    // becomes due.
    const uint8_t* current_status     = nullptr;
    size_t          current_status_len = 0;
};

// ── §13.7.1.3 Table 37: the RC Server's own PTP time-synch trigger signals ──
// Table 37 defines two trigger signals the RC Server itself may issue:
// signal 0 fires when gPTP time-synch becomes established, signal 1 fires
// when it is lost. gPTP lock state is already modeled elsewhere
// (TickContext::gptp_locked above) but nothing derives an EDGE from its
// transitions on its own — this tracker closes that gap. It sends no wire
// traffic and owns no transport: a caller drives every evaluate() call
// itself on each newly observed gptp_locked value and, when a signal fires,
// delivers it via notify_trigger() below using whichever source_ep this
// deployment's own convention assigns to the RC Server itself. Ported from
// c-RCP's rcp_server_gptp_trigger_state_t/_evaluate().
constexpr uint8_t kGptpTriggerEstablished = 0; // Table 37 signal 0
constexpr uint8_t kGptpTriggerLost        = 1; // Table 37 signal 1

struct GptpTriggerState {
    bool has_previous    = false; // false until the first evaluate() call
    bool previous_locked = false; // meaningless while has_previous is false
};

// gptp_trigger_evaluate evaluates one newly observed gPTP lock state
// against `state`'s own previously observed state and updates it for the
// next call. Returns the fired signal (kGptpTriggerEstablished/_Lost) iff
// this call observed a genuine transition (an edge, not a level); returns
// std::nullopt if `locked` is unchanged from the previous call, or this is
// the very first call (no edge exists to detect yet).
inline std::optional<uint8_t> gptp_trigger_evaluate(GptpTriggerState& state, bool locked) noexcept {
    std::optional<uint8_t> fired;
    if (state.has_previous && locked != state.previous_locked) {
        fired = locked ? kGptpTriggerEstablished : kGptpTriggerLost;
    }
    state.has_previous    = true;
    state.previous_locked = locked;
    return fired;
}

// ── Cancellation lifecycle (caller-supplied — this store tracks no
// "currently executing" state of its own; select_due() does not remove a
// slot, only complete() does) ─────────────────────────────────────────────────
// Ported from c-RCP's rcp_cancel_lifecycle_t/rcp_cancel_result_t/
// rcp_cancel_attempt().

enum class CancelLifecycle : uint8_t {
    Queued    = 0,
    Executing = 1,
    Done      = 2,
};

constexpr bool cancel_is_cancellable(CancelLifecycle state) noexcept {
    return state == CancelLifecycle::Queued;
}

enum class CancelResult : uint8_t {
    Canceled       = 0, // REQUEST_CANCELED
    NotFound       = 1, // REQUEST_NOT_FOUND
    NotCancellable = 2, // found, but past the queued/executing window
};

// cancel_attempt: found is whether the target request was located at all;
// state is that request's own lifecycle state if found is true (ignored
// otherwise).
constexpr CancelResult cancel_attempt(bool found, CancelLifecycle state) noexcept {
    if (!found) return CancelResult::NotFound;
    if (!cancel_is_cancellable(state)) return CancelResult::NotCancellable;
    return CancelResult::Canceled;
}

// REQ-CANCEL-012, TC18 §11.2.3's cascade rule: true iff a chain member at
// member_position must also be canceled as part of cascading a cancellation
// targeted at canceled_position within the same chain — i.e.
// member_position is at or after canceled_position. A member strictly
// before canceled_position has already executed by the time a chain member
// is canceled (chained execution is sequential) and is therefore never
// cascaded to. Ported from c-RCP's rcp_cancel_chain_should_cascade().
constexpr bool cancel_chain_should_cascade(uint8_t member_position, uint8_t canceled_position) noexcept {
    return member_position >= canceled_position;
}

// ── The endpoint: ep_enable queue + conditional-request store ─────────────────
class Endpoint {
public:
    explicit Endpoint(bool ep_enable = true) noexcept : ep_enable_(ep_enable) {}

    // ── ep_enable: pre-load-then-drain-on-enable ──────────────────────────────

    bool ep_enable() const noexcept { return ep_enable_; }

    // Toggling does not itself execute or discard anything queued; call
    // drain_one() afterward to pull queued requests back out once
    // re-enabled.
    void set_enable(bool enable) noexcept { ep_enable_ = enable; }

    // REQ-PWRMODE-028 (TC18 §13.7.2.3 step 1): "on receipt of a sleep
    // request the server shall stop entering incoming requests into
    // endpoint queues" while the drain proceeds. Toggling this does not
    // itself execute, queue, or discard anything — it only changes what
    // admit()/admit_with_ack() do with the NEXT arriving request.
    void set_admission_suspended(bool suspended) noexcept { admission_suspended_ = suspended; }
    bool admission_suspended() const noexcept { return admission_suspended_; }

    // submit() is the lower-level queue primitive admit()/admit_with_ack()
    // are built on: it does NOT consult admission_suspended_ — a caller
    // wanting REQ-PWRMODE-028 semantics for standard requests must route
    // them through admit()/admit_with_ack(), not call submit() directly.
    //
    // If ep_enable_ is true, this is a no-op on the queue and returns true,
    // meaning the caller must execute the request now. If ep_enable_ is
    // false:
    //   REQ-SRV-015 (TC18 §12.3.1.3): "as long as EPs are not enabled...
    //   they will only execute config requests. Operational requests will
    //   be stored in the EP's queue." A configuration-write request
    //   (evt[2:0] == 111b) still executes immediately even while disabled
    //   — for an ACF_ABB request AND for every ACF_GBB conditional request
    //   kind EXCEPT CompoundWait, whose own evt[2:0] means an 8-way
    //   comparison-operator selector (§13.5.1), never a configuration-write
    //   signal (REQ-ACF-032's peek_gbb_request_type() is what lets this
    //   distinction be made without a full kind-specific decode). A GBB
    //   frame whose request_type cannot even be peeked (too short, or not
    //   one of the defined opcodes) is conservatively queued, the fail-safe
    //   default this function already applies to a too-short ABB frame.
    //
    //   Otherwise the request is appended to the queue and this returns
    //   false, meaning it has been queued rather than executed. Returns
    //   false (still "queued") without actually storing the frame if the
    //   queue has reached kMaxQueuedFrames — see that constant's own doc
    //   comment for why this mirrors c-RCP's identical allocation-failure
    //   contract exactly.
    //
    // REQ-SRV-016 (TC18 §12.3.1.3): "if requested an acknowledge is sent
    // after storing the request." out_ack may be nullptr. When the request
    // is queued and its own evt[3] requested an acknowledge
    // (acf::evt_requests_acknowledge(), here read directly off the decoded
    // AcfMessageInfo::evt_ack), *out_ack is set to a genuine Acknowledge
    // response (acf::build_acknowledge_response()) addressed to the
    // request's own byte_bus_id/transaction_num. Left empty otherwise
    // (executed now, evt[3] wasn't set, or frame is shorter than the fixed
    // ACF header and its evt[3] cannot be read at all).
    bool submit(const uint8_t* frame, size_t frame_len, std::vector<uint8_t>* out_ack) {
        if (out_ack) out_ack->clear();
        if (ep_enable_) return true; // caller must execute this now

        if (frame_len >= acf::kAcfCommonHeaderLen) {
            acf::AcfMessageInfo hdr;
            acf::decode_acf_message_info(frame, hdr);
            if (hdr.evt_op == 0x07u) {
                if (hdr.acf_msg_type == acf::kAcfMsgTypeAbb) {
                    return true; // configuration request: execute now
                }
                if (hdr.acf_msg_type == acf::kAcfMsgTypeGbb) {
                    uint8_t request_type = 0;
                    if (acf::peek_gbb_request_type(frame, frame_len, request_type) &&
                        !is_compound_wait_byte(request_type)) {
                        return true; // configuration request: execute now
                    }
                }
            }
        }

        if (queue_len_ >= kMaxQueuedFrames) {
            return false; // full: still "queued": nothing to execute now
        }
        queue_[queue_len_++].assign(frame, frame + frame_len);

        if (out_ack && frame_len >= acf::kAcfCommonHeaderLen) {
            acf::AcfMessageInfo hdr;
            acf::decode_acf_message_info(frame, hdr);
            if (hdr.evt_ack) *out_ack = acf::build_acknowledge_response(hdr.byte_bus_id, hdr.transaction_num);
        }
        return false;
    }

    // If ep_enable_ is true and the queue is non-empty, dequeues the oldest
    // queued request into out_frame and returns true. Otherwise returns
    // false and leaves out_frame untouched — including while ep_enable_ is
    // false, so a disabled endpoint's queue can never be silently drained
    // out from under it.
    bool drain_one(std::vector<uint8_t>& out_frame) {
        if (!ep_enable_ || queue_len_ == 0) return false;
        out_frame = std::move(queue_[0]);
        for (size_t i = 1; i < queue_len_; ++i) queue_[i - 1] = std::move(queue_[i]);
        --queue_len_;
        return true;
    }

    size_t queue_len() const noexcept { return queue_len_; }

    // ── Admission: request_type-aware routing ─────────────────────────────────

    // admit() is admit_with_ack() with out_ack forced to nullptr.
    AdmitOutcome admit(const uint8_t* frame, size_t frame_len, uint32_t now, bool tv,
                        uint32_t avtp_timestamp, uint64_t gptp_reference_now,
                        std::optional<request::RequestTypeOpcode>& out_request_type, size_t* out_index,
                        std::optional<acf::WireErrorCode>* out_error) {
        return admit_with_ack(frame, frame_len, now, tv, avtp_timestamp, gptp_reference_now,
                               out_request_type, out_index, out_error, nullptr);
    }

    // Inspects frame[0..frame_len)'s request_type and routes it.
    //
    // If admission_suspended_ is true, returns Suspended immediately
    // (REQ-PWRMODE-028) without inspecting frame at all.
    //
    // REQ-ACF-021: TC18's own rsp field description states rsp=1 identifies
    // a response — a frame carrying one must never be admitted as a
    // request. Reported as Rejected with InvalidParameter.
    //
    // A message that is not a repurposed-timestamp ACF_GBB at all (an
    // ordinary ACF_ABB, a GBB with mtv set, or a frame too short to even
    // carry the opcode byte) is a standard request and takes the original
    // submit() path unchanged. Under a TSCF header (tv), REQ-TIMED-012
    // postpones it via the request store instead.
    //
    // A recognized conditional opcode is decoded through its own kind's
    // rcp::request decode_*_request() and stored, with its exec_delay timer
    // left unarmed. An unrecognized GBB opcode byte is treated exactly like
    // a standard request — never over-privileged, matching
    // rcp::request::category_of()'s own "unknown -> not a category" shape
    // composed with is_valid_request_type() below.
    //
    // out_request_type is always written: the repurposed opcode for a
    // conditional or cancellation request, or std::nullopt for a standard
    // one. out_index, when non-null, receives the store index a Pending
    // request was placed at.
    //
    // out_error, when non-null, is always written on Rejected and left
    // untouched otherwise. Most rejection paths reject before the fields
    // needed to build a real error response are known, so out_error stays
    // std::nullopt for those. Only two paths set a real code: the request
    // store being full (WireErrorCode::ReqStorageOverflow) and a
    // CompoundWait request whose evt[2:0] is the reserved value
    // (WireErrorCode::UnsupportedCmd, TC18 §13.5.1).
    //
    // REQ-TIMED-012: tv/avtp_timestamp are the enclosing AVTPDU's own TSCF
    // header fields. tv false means frame carries no presentation time at
    // all, and this call behaves exactly as it would with no TSCF header.
    // tv true means the request — of ANY kind, standard, conditional, or
    // cancel — is postponed until avtp_timestamp's own reconstructed
    // 48-bit-domain instant (avtp::extend_timestamp(), resolved once here
    // against gptp_reference_now and stored as the new slot's own
    // presentation_gate_ns).
    AdmitOutcome admit_with_ack(const uint8_t* frame, size_t frame_len, uint32_t now, bool tv,
                                 uint32_t avtp_timestamp, uint64_t gptp_reference_now,
                                 std::optional<request::RequestTypeOpcode>& out_request_type,
                                 size_t* out_index, std::optional<acf::WireErrorCode>* out_error,
                                 std::vector<uint8_t>* out_ack) {
        out_request_type = std::nullopt;
        if (out_error) *out_error = std::nullopt;
        if (out_ack) out_ack->clear();

        if (admission_suspended_) return AdmitOutcome::Suspended;

        const uint64_t presentation_gate_ns =
            tv ? avtp::extend_timestamp(avtp_timestamp, gptp_reference_now) : 0;

        // REQ-ACF-021
        if (frame_len >= acf::kAcfCommonHeaderLen) {
            acf::AcfMessageInfo hdr;
            acf::decode_acf_message_info(frame, hdr);
            if (!acf::header_is_request(hdr)) {
                if (out_error) *out_error = acf::WireErrorCode::InvalidParameter;
                return AdmitOutcome::Rejected;
            }
        }

        const std::optional<request::RequestTypeOpcode> peeked = peek_request_type(frame, frame_len);

        if (!peeked) {
            // Not a repurposed-timestamp ACF_GBB at all (or an unrecognized
            // opcode byte on one): a standard request.
            if (tv) {
                return admit_under_tscf_gate(frame, frame_len, now, presentation_gate_ns,
                                              request::RequestCategory::Standard, std::nullopt, out_index,
                                              out_ack);
            }
            return submit(frame, frame_len, out_ack) ? AdmitOutcome::ExecuteNow : AdmitOutcome::Queued;
        }

        const request::RequestCategory category = request::category_of(peeked);
        out_request_type                        = peeked;

        if (category == request::RequestCategory::Cancellation) {
            // REQ-TIMED-012/013: "If received under TSCF header all of them
            // [Standard, Conditional, AND Cancel] shall be executed
            // earliest at the given presentation time."
            if (tv) {
                return admit_under_tscf_gate(frame, frame_len, now, presentation_gate_ns, category, peeked,
                                              out_index, out_ack);
            }
            return AdmitOutcome::Cancellation;
        }

        PendingRequest* slot = claim_slot();
        if (!slot) {
            if (out_error) *out_error = acf::WireErrorCode::ReqStorageOverflow;
            return AdmitOutcome::Rejected;
        }
        slot->category     = category;
        slot->request_type = peeked;

        switch (category) {
        case request::RequestCategory::Compound:
        case request::RequestCategory::CompoundWait: {
            request::CompoundRequest cr;
            if (request::decode_compound_request(frame, frame_len, cr)) {
                release_slot(*slot);
                return AdmitOutcome::Rejected;
            }
            if (category == request::RequestCategory::CompoundWait) {
                // TC18 §13.5.1: evt[2:0] = 011b is reserved for a
                // compound-wait request — "request shall be ignored and an
                // err-response with error code = UNSUPPORTED_CMD shall be
                // sent".
                if (!acf::compound_wait_evt_valid(cr.evt_op)) {
                    release_slot(*slot);
                    if (out_error) *out_error = acf::WireErrorCode::UnsupportedCmd;
                    return AdmitOutcome::Rejected;
                }
                slot->compound_wait_evt    = cr.evt_op;
                slot->compound_wait_target = std::move(cr.payload);
            }
            slot->compound        = cr.step;
            slot->transaction_num = cr.transaction_num;
            break;
        }
        case request::RequestCategory::Triggered: {
            request::TriggeredRequest tr;
            if (request::decode_triggered_request(frame, frame_len, tr)) {
                release_slot(*slot);
                return AdmitOutcome::Rejected;
            }
            slot->triggered       = tr.step;
            slot->transaction_num = tr.transaction_num;
            // A triggered request begins counting occurrences the moment it
            // is admitted; its exec_delay runs from the moment its
            // threshold is reached — see select_due().
            request::triggered_enter_started(slot->triggered_runtime);
            break;
        }
        case request::RequestCategory::Timed: {
            request::TimedRequest tmr;
            if (request::decode_timed_request(frame, frame_len, tmr)) {
                release_slot(*slot);
                return AdmitOutcome::Rejected;
            }
            slot->presentation_time = tmr.presentation_time;
            slot->transaction_num   = tmr.transaction_num;
            break;
        }
        case request::RequestCategory::Chained: {
            request::ChainedMember cm;
            if (request::decode_chained_member(frame, frame_len, cm)) {
                release_slot(*slot);
                return AdmitOutcome::Rejected;
            }
            slot->chain_exec_delay = cm.chain_exec_delay;
            slot->chain_cs         = cm.cs;
            slot->transaction_num  = cm.transaction_num;
            break;
        }
        default:
            release_slot(*slot); // unreachable: category_of() never returns
                                  // Standard/Cancellation here
            return AdmitOutcome::Rejected;
        }

        // REQ-TIMED-012: ALSO gated by the envelope-level presentation
        // time, on top of (not instead of) the kind-specific condition just
        // decoded above.
        slot->has_presentation_gate = tv;
        slot->presentation_gate_ns  = presentation_gate_ns;
        slot->armed_at              = now;
        slot->frame.assign(frame, frame + frame_len);

        if (out_index) *out_index = index_of(*slot);
        build_store_ack(frame, frame_len, out_ack);
        return AdmitOutcome::Pending;
    }

    // ── The execution-condition tick ──────────────────────────────────────────

    // Re-evaluates every stored request's start condition against ctx
    // (arming exec_delay timers that have just become armable) and returns
    // the single highest-priority request that is due to execute right now
    // (request::priority_rank(): Cancellation > Triggered > Timed >
    // Compound > CompoundWait > Chained > Standard, FIFO within a rank).
    // Safety-tagged requests are held back until ctx.in_safe_state.
    //
    // REQ-SRV-015/016 extension: a Compound/CompoundWait/Triggered/Timed/
    // Chained request stored on a currently-disabled endpoint is never
    // reported due, however long its own kind-specific condition has held —
    // the same "operational requests stay queued, never executed, while
    // disabled" rule submit() already enforces for a Standard request. A
    // Standard or Cancellation request postponed purely by the
    // REQ-TIMED-012 TSCF gate is NOT covered by this rule — it has no
    // kind-specific operational semantics of its own to misclassify, and
    // becomes due purely once the presentation gate opens, on a disabled
    // endpoint exactly as on an enabled one.
    //
    // This function never executes anything and never advances a
    // sequencer — the caller runs the selected slot's own frame
    // (pending(*out_index)->frame) and then reports the outcome back
    // through complete().
    //
    // Deliberately does NOT call rcp::request::select_next_due() (which
    // takes a std::vector<DueCandidate>): building that vector each tick
    // would mean a heap allocation on this safety-relevant scheduling path
    // for every call. The identical rank-then-FIFO comparison
    // select_next_due() implements is inlined below instead, evaluated
    // directly over the fixed-size pending_ array.
    bool select_due(const TickContext& ctx, size_t* out_index) {
        bool                     found = false;
        size_t                   best  = 0;
        request::DueCandidate    best_candidate{};

        for (size_t i = 0; i < kMaxPending; ++i) {
            PendingRequest& slot = pending_[i];
            if (!slot.in_use) continue;
            if (!is_due(slot, ctx)) continue;

            const request::DueCandidate candidate{slot.category, static_cast<size_t>(slot.sequence)};
            if (!found ||
                request::priority_rank(candidate.category) < request::priority_rank(best_candidate.category) ||
                (request::priority_rank(candidate.category) == request::priority_rank(best_candidate.category) &&
                 candidate.arrival_seq < best_candidate.arrival_seq)) {
                found         = true;
                best          = i;
                best_candidate = candidate;
            }
        }

        if (found && out_index) *out_index = best;
        return found;
    }

    // Finalizes the request at index after the caller has executed it.
    // Applies that request kind's own completion action — Compound
    // advances its sequencer through SequencerTable::tick(), CompoundWait
    // through SequencerTable::wait_tick() (re-evaluating the caller-owned
    // current_status), Triggered through request::triggered_tick() — and
    // then the repetition rule: an infinite repeat_count
    // (kCompoundRepeatInfinite/kTriggeredRepeatInfinite, both 0xFFFF) is
    // left untouched and the request re-arms; zero removes the request from
    // the store; any other value is decremented and the request re-arms.
    // Timed, Chained, Standard, and Cancellation carry no repetition
    // sub-field and are always removed. Returns true iff the request
    // remains in the store afterwards (it will repeat).
    bool complete(size_t index, const TickContext& ctx) {
        if (index >= kMaxPending) return false;
        PendingRequest& slot = pending_[index];
        if (!slot.in_use) return false;

        const uint32_t elapsed = ctx.now - slot.armed_at;
        uint16_t*      repeat  = nullptr;

        switch (slot.category) {
        case request::RequestCategory::Compound:
            if (ctx.sequencers) ctx.sequencers->tick(slot.compound, elapsed);
            repeat = &slot.compound.repeat_count;
            break;
        case request::RequestCategory::CompoundWait:
            if (ctx.sequencers) {
                const bool matched =
                    acf::compound_wait_match(slot.compound_wait_evt, slot.compound_wait_target.data(),
                                              slot.compound_wait_target.size(), ctx.current_status,
                                              ctx.current_status_len);
                ctx.sequencers->wait_tick(slot.compound, matched);
            }
            repeat = &slot.compound.repeat_count;
            break;
        case request::RequestCategory::Triggered:
            (void)request::triggered_tick(slot.triggered, slot.triggered_runtime, elapsed, true);
            repeat = &slot.triggered.repeat_count;
            break;
        default:
            // Timed, Chained, Standard, Cancellation: no repetition
            // sub-field of their own — always released after one execution.
            release_slot(slot);
            return false;
        }

        if (*repeat == request::kCompoundRepeatInfinite) {
            // never decremented, never removed
        } else if (*repeat == 0u) {
            release_slot(slot);
            return false;
        } else {
            --(*repeat);
        }

        // Re-arm for the next repetition: the start condition has to be
        // satisfied again from scratch.
        slot.armed    = false;
        slot.armed_at = ctx.now;
        if (slot.category == request::RequestCategory::Triggered) {
            request::triggered_enter_started(slot.triggered_runtime);
        }
        return true;
    }

    // Records one observed trigger occurrence, emitted by endpoint
    // source_ep as its trigger signal number signal_nr, against every
    // stored Triggered request whose own trigger_source_ep/
    // trigger_signal_nr selection matches. Returns how many stored
    // requests counted it.
    size_t notify_trigger(uint8_t source_ep, uint8_t signal_nr) {
        size_t matched = 0;
        for (size_t i = 0; i < kMaxPending; ++i) {
            PendingRequest& slot = pending_[i];
            if (!slot.in_use || slot.category != request::RequestCategory::Triggered) continue;
            if (request::triggered_record_occurrence(slot.triggered_runtime, slot.triggered, source_ep,
                                                       signal_nr)) {
                ++matched;
            }
        }
        return matched;
    }

    // Marks the stored Chained request at index as having had its
    // predecessor finalize, at tick count now: its chain_exec_delay timer
    // starts running from there and it becomes due once that delay
    // elapses. Returns false, changing nothing, if index does not name a
    // stored Chained request.
    bool chain_predecessor_done(size_t index, uint32_t now) {
        if (index >= kMaxPending) return false;
        PendingRequest& slot = pending_[index];
        if (!slot.in_use || slot.category != request::RequestCategory::Chained) return false;
        slot.predecessor_done = true;
        slot.armed_at         = now;
        return true;
    }

    // pending() gives a caller (a future mock.hpp dispatch layer) direct
    // access to a stored slot — needed for exactly one thing this store
    // cannot do itself: assigning chain_group/chain_position once the
    // caller has decoded a whole multi-member AVTPDU and knows a Chained
    // member's own position within it (see PendingRequest's own doc
    // comment). Returns nullptr for an out-of-range or unused index.
    PendingRequest*       pending(size_t index) noexcept {
        if (index >= kMaxPending || !pending_[index].in_use) return nullptr;
        return &pending_[index];
    }
    const PendingRequest* pending(size_t index) const noexcept {
        if (index >= kMaxPending || !pending_[index].in_use) return nullptr;
        return &pending_[index];
    }

    size_t pending_count() const noexcept { return pending_count_; }

    // ── Cancellation and watchdog purge ───────────────────────────────────────

    // Clear-all (0x05): removes every stored conditional request, returning
    // how many were removed.
    size_t cancel_all() {
        size_t removed = 0;
        for (size_t i = 0; i < kMaxPending; ++i) {
            if (!pending_[i].in_use) continue;
            release_slot(pending_[i]);
            ++removed;
        }
        return removed;
    }

    // Clear-single (0x07): removes the stored request whose own
    // transaction_num equals clear_transaction_num. Reports cancel_attempt()
    // above — NotFound when no stored request carries that transaction_num,
    // Canceled when found and removed. A request already past
    // cancel_lifecycle's own Queued state (caller-supplied — this store
    // tracks no "currently executing" flag of its own) is reported
    // NotCancellable without being removed.
    CancelResult cancel_single(uint8_t clear_transaction_num, CancelLifecycle state) {
        bool   found = false;
        size_t idx   = 0;
        for (size_t i = 0; i < kMaxPending; ++i) {
            if (pending_[i].in_use && pending_[i].transaction_num == clear_transaction_num) {
                found = true;
                idx   = i;
                break;
            }
        }
        const CancelResult result = cancel_attempt(found, state);
        if (result == CancelResult::Canceled) release_slot(pending_[idx]);
        return result;
    }

    // Clear-non-safestate (0x06): removes every stored request that is not
    // safety-tagged, leaving the 0x8x ones in place. Returns how many were
    // removed.
    size_t cancel_non_safestate() { return purge_non_safety(); }

    // The watchdog-overflow purge: removes every stored request that is not
    // safety-tagged, so that only the safety sequence survives to drive the
    // endpoint into its safe state. Returns how many were removed.
    // Identical in effect to cancel_non_safestate(), but reached by a
    // different event.
    size_t watchdog_purge() { return purge_non_safety(); }

    // REQ-CANCEL-012, TC18 §11.2.3's cascade rule: removes every stored
    // request whose own chain_group equals chain_group and whose own
    // chain_position satisfies cancel_chain_should_cascade(chain_position,
    // min_position) — i.e. every member at or after min_position within
    // that same chain, including min_position's own entry if it is itself
    // still stored. chain_group == 0 (the "not part of a chain" sentinel)
    // matches nothing, so calling this with a non-chain-grouped entry's own
    // chain_group is always a safe no-op. Returns the count actually
    // removed.
    size_t cancel_chain_from(uint32_t chain_group, uint8_t min_position) {
        if (chain_group == 0u) return 0;
        size_t removed = 0;
        for (size_t i = 0; i < kMaxPending; ++i) {
            if (!pending_[i].in_use) continue;
            if (pending_[i].chain_group != chain_group) continue;
            if (!cancel_chain_should_cascade(pending_[i].chain_position, min_position)) continue;
            release_slot(pending_[i]);
            ++removed;
        }
        return removed;
    }

private:
    // ── ep_enable queue helpers ────────────────────────────────────────────

    static bool is_compound_wait_byte(uint8_t byte) noexcept {
        return byte == static_cast<uint8_t>(request::RequestTypeOpcode::CompoundWait) ||
               byte == static_cast<uint8_t>(request::RequestTypeOpcode::CompoundWaitSafety);
    }

    // ── conditional-request store helpers ─────────────────────────────────

    PendingRequest* claim_slot() noexcept {
        for (size_t i = 0; i < kMaxPending; ++i) {
            if (!pending_[i].in_use) {
                pending_[i]             = PendingRequest{};
                pending_[i].in_use      = true;
                pending_[i].sequence    = next_sequence_++;
                ++pending_count_;
                return &pending_[i];
            }
        }
        return nullptr;
    }

    void release_slot(PendingRequest& slot) noexcept {
        slot = PendingRequest{};
        --pending_count_;
    }

    size_t index_of(const PendingRequest& slot) const noexcept {
        return static_cast<size_t>(&slot - &pending_[0]);
    }

    // peek_request_type: if frame is long enough to hold the full ACF_GBB
    // Message Info block (16 octets) and decodes as an untimed
    // (mtv-clear) ACF_GBB whose repurposed opcode byte is one of the
    // defined request_type values, returns it. Otherwise (ABB, a GBB with
    // mtv set, too short, or an unrecognized opcode byte) returns
    // std::nullopt — every one of those is treated as a standard request,
    // matching c-RCP's own rcp_compound_peek_request_type() +
    // rcp_sched_classify() fail-safe-to-standard composition.
    static std::optional<request::RequestTypeOpcode> peek_request_type(const uint8_t* frame,
                                                                          size_t frame_len) noexcept {
        if (frame_len < acf::kAcfGbbMessageInfoLen) return std::nullopt;
        acf::AcfMessageInfo hdr;
        acf::decode_acf_message_info(frame, hdr);
        if (hdr.acf_msg_type != acf::kAcfMsgTypeGbb || hdr.mtv) return std::nullopt;
        const uint8_t byte0 = frame[acf::kAcfCommonHeaderLen];
        if (!request::is_valid_request_type(byte0)) return std::nullopt;
        return static_cast<request::RequestTypeOpcode>(byte0);
    }

    // build_store_ack: shared by admit_with_ack()'s own store-success paths
    // — builds a genuine Acknowledge response into *out_ack when frame's
    // own evt[3] requests one, mirroring submit()'s own identical logic.
    static void build_store_ack(const uint8_t* frame, size_t frame_len, std::vector<uint8_t>* out_ack) {
        if (!out_ack) return;
        out_ack->clear();
        if (frame_len < acf::kAcfCommonHeaderLen) return;
        acf::AcfMessageInfo hdr;
        acf::decode_acf_message_info(frame, hdr);
        if (hdr.evt_ack) *out_ack = acf::build_acknowledge_response(hdr.byte_bus_id, hdr.transaction_num);
    }

    // admit_under_tscf_gate: REQ-TIMED-012 — claims a fresh slot for a
    // request carried under a TSCF header that has no kind-specific
    // execution condition of its own (a Standard request that would
    // otherwise ExecuteNow/Queued, or a Cancellation request that would
    // otherwise apply immediately), now postponed purely by the
    // envelope-level presentation-time gate.
    AdmitOutcome admit_under_tscf_gate(const uint8_t* frame, size_t frame_len, uint32_t now,
                                        uint64_t presentation_gate_ns, request::RequestCategory category,
                                        std::optional<request::RequestTypeOpcode> request_type,
                                        size_t* out_index, std::vector<uint8_t>* out_ack) {
        PendingRequest* slot = claim_slot();
        if (!slot) return AdmitOutcome::Rejected;

        slot->category             = category;
        slot->request_type         = request_type;
        slot->has_presentation_gate = true;
        slot->presentation_gate_ns  = presentation_gate_ns;
        slot->armed_at              = now;
        slot->frame.assign(frame, frame + frame_len);

        if (out_index) *out_index = index_of(*slot);
        build_store_ack(frame, frame_len, out_ack);
        return AdmitOutcome::Pending;
    }

    // Whether slot's own kind is one of the conditional-request kinds
    // REQ-SRV-015/016's extension gates on ep_enable_ below. Standard and
    // Cancellation are deliberately excluded: those two also reach this
    // store, but only via admit_under_tscf_gate()'s own separate
    // REQ-TIMED-012 envelope-level gate, which bypasses submit()'s
    // config-vs-operational classification entirely.
    static bool kind_is_gated_by_ep_enable(request::RequestCategory category) noexcept {
        switch (category) {
        case request::RequestCategory::Compound:
        case request::RequestCategory::CompoundWait:
        case request::RequestCategory::Triggered:
        case request::RequestCategory::Timed:
        case request::RequestCategory::Chained:
            return true;
        default:
            return false;
        }
    }

    // Whether slot's own start condition — the thing that sets its
    // exec_delay timer running — holds right now.
    static bool start_condition_holds(const PendingRequest& slot, const TickContext& ctx) noexcept {
        switch (slot.category) {
        case request::RequestCategory::Compound:
        case request::RequestCategory::CompoundWait:
            return ctx.sequencers != nullptr && ctx.sequencers->start_condition_met(slot.compound);
        case request::RequestCategory::Triggered:
            return request::triggered_threshold_reached(slot.triggered, slot.triggered_runtime);
        case request::RequestCategory::Chained:
            return slot.predecessor_done;
        case request::RequestCategory::Timed:
        case request::RequestCategory::Standard:
        case request::RequestCategory::Cancellation:
            // No separate arming step of their own.
            return true;
        default:
            return false;
        }
    }

    // Arms slot's exec_delay timer at ctx.now if its own start condition
    // has just begun to hold. Returns whether slot is armed afterwards.
    static bool arm_if_startable(PendingRequest& slot, const TickContext& ctx) noexcept {
        if (slot.armed) return true;
        if (!start_condition_holds(slot, ctx)) return false;
        slot.armed = true;
        // A chained request's chain_exec_delay is measured from its
        // predecessor's finalization, already recorded in armed_at by
        // chain_predecessor_done() — restarting the timer here would
        // discard it.
        if (slot.category != request::RequestCategory::Chained) slot.armed_at = ctx.now;
        return true;
    }

    // The non-timer half of slot's condition: what must hold, besides its
    // exec_delay having elapsed, before it may execute.
    static bool auxiliary_condition_met(const PendingRequest& slot, const TickContext& ctx) noexcept {
        switch (slot.category) {
        case request::RequestCategory::CompoundWait:
            return acf::compound_wait_match(slot.compound_wait_evt, slot.compound_wait_target.data(),
                                             slot.compound_wait_target.size(), ctx.current_status,
                                             ctx.current_status_len);
        case request::RequestCategory::Compound:
        case request::RequestCategory::Triggered:
        case request::RequestCategory::Chained:
            return ctx.endpoint_idle;
        case request::RequestCategory::Timed:
            return ctx.gptp_locked;
        default:
            return true;
        }
    }

    // Whether slot's own delay/deadline has expired, given how long it has
    // been armed.
    static bool delay_expired(const PendingRequest& slot, const TickContext& ctx, uint32_t elapsed) noexcept {
        switch (slot.category) {
        case request::RequestCategory::Compound:
        case request::RequestCategory::CompoundWait:
            return request::SequencerTable::exec_delay_elapsed(slot.compound, elapsed);
        case request::RequestCategory::Triggered:
            return request::triggered_exec_delay_elapsed(slot.triggered, elapsed);
        case request::RequestCategory::Timed:
            return request::timed_due(slot.presentation_time, ctx.gptp_now);
        case request::RequestCategory::Chained:
            return request::chained_exec_delay_elapsed(slot.chain_exec_delay, elapsed);
        case request::RequestCategory::Standard:
        case request::RequestCategory::Cancellation:
            // No exec_delay of their own — is_due()'s own envelope-level
            // presentation gate (checked before this is ever reached) is
            // this kind's entire condition here.
            return true;
        default:
            return false;
        }
    }

    // Whether slot's execution condition is fully satisfied right now.
    bool is_due(PendingRequest& slot, const TickContext& ctx) const {
        // REQ-SRV-015/016 extension
        if (!ep_enable_ && kind_is_gated_by_ep_enable(slot.category)) return false;

        // Safety-tagged requests stay in the store until the endpoint has
        // actually reached its configured safe state.
        if (slot.request_type.has_value() && request::is_safety_variant(*slot.request_type) &&
            !ctx.in_safe_state) {
            return false;
        }

        // REQ-TIMED-012: envelope-level gate, independent of (ANDed with)
        // each kind's own existing condition below. Fail-closed without a
        // locked time base.
        if (slot.has_presentation_gate &&
            !(ctx.gptp_locked && request::timed_due(slot.presentation_gate_ns, ctx.gptp_now))) {
            return false;
        }

        // Arming is evaluated before the auxiliary gate on purpose: a
        // triggered request's exec_delay runs from the moment its
        // threshold was met, not from whenever the endpoint next happens
        // to be idle.
        if (!arm_if_startable(slot, ctx)) return false;
        if (!auxiliary_condition_met(slot, ctx)) return false;

        return delay_expired(slot, ctx, ctx.now - slot.armed_at);
    }

    // Shared body of clear-non-safestate and the watchdog purge: both
    // remove exactly the requests that are not safety-tagged.
    size_t purge_non_safety() {
        size_t removed = 0;
        for (size_t i = 0; i < kMaxPending; ++i) {
            PendingRequest& slot = pending_[i];
            if (!slot.in_use) continue;
            if (slot.request_type.has_value() && request::is_safety_variant(*slot.request_type)) continue;
            release_slot(slot);
            ++removed;
        }
        return removed;
    }

    bool ep_enable_          = true;
    bool admission_suspended_ = false;

    std::array<std::vector<uint8_t>, kMaxQueuedFrames> queue_;
    size_t                                                queue_len_ = 0;

    std::array<PendingRequest, kMaxPending> pending_{};
    size_t                                    pending_count_ = 0;
    uint64_t                                   next_sequence_ = 0;
};

} // namespace server
} // namespace rcp
