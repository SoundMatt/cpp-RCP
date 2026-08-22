// fusa:req REQ-SEQ-001
// fusa:req REQ-SEQ-002
// fusa:req REQ-SEQ-003
// fusa:req REQ-SEQ-004
// fusa:req REQ-SEQ-005
// fusa:req REQ-SEQ-006
// fusa:req REQ-SEQ-007
// fusa:req REQ-SEQ-008
// fusa:req REQ-SEQ-009
// fusa:req REQ-SEQ-010
// fusa:req REQ-SEQ-011
// fusa:req REQ-SEQ-012
// fusa:req REQ-CMP-001
// fusa:req REQ-CMP-002
// fusa:req REQ-CMP-003
// fusa:req REQ-CMP-010
// fusa:req REQ-CMP-011
// fusa:req REQ-CMP-012
// fusa:req REQ-CMP-013
// fusa:req REQ-CMP-014
// fusa:req REQ-CMP-015
// fusa:req REQ-CMP-016
// fusa:req REQ-CMP-017
// fusa:req REQ-CMP-018
// fusa:req REQ-CMP-019
// fusa:req REQ-CMP-020
// fusa:req REQ-CMP-021
// fusa:req REQ-CMP-022
// fusa:req REQ-CMP-023
// fusa:req REQ-CMP-024
// fusa:req REQ-CMP-025
// fusa:req REQ-CMP-026
// fusa:req REQ-CMP-027
// fusa:req REQ-CMP-028
// fusa:req REQ-CMP-029
// fusa:req REQ-TRIG-001
// fusa:req REQ-TRIG-004
// fusa:req REQ-TRIG-005
// fusa:req REQ-TRIG-006
// fusa:req REQ-TRIG-007
// fusa:req REQ-TRIG-008
// fusa:req REQ-TRIG-009
// fusa:req REQ-TRIG-010
// fusa:req REQ-TRIG-011
// fusa:req REQ-TRIG-012
// fusa:req REQ-TRIG-013
// fusa:req REQ-CHAIN-002
// fusa:req REQ-CHAIN-004
// fusa:req REQ-CHAIN-005
// fusa:req REQ-CHAIN-006
// fusa:req REQ-CHAIN-007
// fusa:req REQ-CHAIN-010
// fusa:req REQ-CHAIN-011
// fusa:req REQ-CHAIN-012
// fusa:req REQ-TIMED-002
// fusa:req REQ-TIMED-003
// fusa:req REQ-TIMED-004
// fusa:req REQ-TIMED-005
// fusa:req REQ-TIMED-006
// fusa:req REQ-TIMED-007
// fusa:req REQ-TIMED-008
// fusa:req REQ-TIMED-009
// fusa:req REQ-TIMED-010
// fusa:req REQ-TIMED-011
// fusa:req REQ-CANCEL-002
// fusa:req REQ-CANCEL-003
// fusa:req REQ-CANCEL-004
// fusa:req REQ-CANCEL-005
// fusa:req REQ-CANCEL-006
// fusa:req REQ-CANCEL-007
// fusa:req REQ-CANCEL-008
// fusa:req REQ-CANCEL-009
// fusa:req REQ-CANCEL-010
// fusa:req REQ-CANCEL-011
// fusa:req REQ-CANCEL-012
// fusa:req REQ-CANCEL-013
// fusa:req REQ-CANCEL-014
// fusa:req REQ-CANCEL-015
// fusa:req REQ-SCHED-002
// fusa:req REQ-SCHED-003
// fusa:req REQ-SCHED-007
// fusa:req REQ-SCHED-008

// Conditional-request taxonomy and sequencer-state primitives — the
// message_timestamp-repurposing decode, the five conditional request kinds
// (plus the three cancellation kinds) and their optional-feature bundling,
// sequencer-state advance/reset, cancellation semantics, and the request
// lifecycle state machine an OPEN Alliance TC18 Remote Control Protocol
// Specification v0.5.1_RC server needs once it goes beyond the mandatory
// "standard" request kind.
//
// ── Phase 1 rewrite (cpp-RCP issue #129, ROADMAP.md "Phase 17") ─────────────
// This is a from-c-RCP content re-derivation, not a restructuring: c-RCP
// (this project's RC5-spec-conformant reference) models the same taxonomy
// as three modules — request.h/request.c (compound/compound-wait/triggered/
// chained/timed/cancellation), request_sequencer.h/request_sequencer.c (the
// shared sequencer-state bank), and scheduler.h/scheduler.c (request-kind
// classification and execution-priority ordering). This file keeps cpp-RCP's
// own, already-established unification of all three into one module — that
// shape predates this rewrite and stays; what changed is the *content*,
// re-derived from c-RCP's current, RC5-correct behavior. c-RCP's own
// multi-request-per-frame splitting (scheduler.c's
// rcp_sched_split_frame_members) is deliberately NOT re-implemented here —
// rcp/acf.hpp's decode_acf_messages() already does that at the ACF-framing
// layer cpp-RCP settled on, so nothing in this file needs to walk raw ACF
// message boundaries itself.
//
// Real content deltas found while re-deriving this file from c-RCP and
// fixed as part of this pass:
//
//  1. cpp-RCP issue #58 — cs-bit polarity inverted in
//     should_execute_chained(): the old body was
//     `return cs || !predecessor_errored;`, meaning cs=true (this codebase's
//     prior, backwards reading of "execute regardless") never aborted a
//     successor no matter what its predecessor did. c-RCP's own
//     rcp_chained_advance() (src/request.c) and its test
//     test_advance_abort_on_error_stops_the_chain (tests/
//     test_request_chained.c) pin the opposite, TC18-correct polarity: cs=0
//     (RCP_CHAINED_CS_CONTINUE_ON_ERROR) executes regardless of a
//     predecessor's error, cs=1 (RCP_CHAINED_CS_ABORT_ON_ERROR) aborts this
//     member (and, transitively, the rest of the chain) when the
//     predecessor errored. Fixed below to
//     `return !cs || !predecessor_errored;`.
//
//  2. compound_wait_check_of()/CompoundWaitCheck — this cpp-RCP-original
//     concept (cs selects "check immediately" vs "check only after the
//     monitored value changes" for a compound-wait request) has NO
//     counterpart in c-RCP: c-RCP's rcp_compound_encode_request() hard-codes
//     cs=0 for every compound/compound-wait request it builds, and
//     rcp_compound_decode_request() does not surface a decoded cs at all —
//     acf.h's own comment that "compound-wait... assign[s] cs a meaning of
//     its own" (TC18 §11.2.2.3 Table 8) is round-tripped but never actually
//     interpreted anywhere in c-RCP's implementation (same "round-trip now,
//     activate later" precedent request.h's own file header documents for
//     the 0x8x safety-tagged opcodes). What compound-wait's condition
//     *comparison mode* actually uses, per c-RCP and per this project's own
//     acf.hpp, is the ACF header's evt[2:0] field — acf::
//     compound_wait_evt_valid()/compound_wait_match() (added in the
//     just-merged acf/avtp port) — never cs. compound_wait_check_of() and
//     CompoundWaitCheck are removed below (not ported: there is nothing
//     correct to port); this header's compound/compound-wait request codec
//     takes an evt_op parameter and callers use acf::compound_wait_evt_
//     valid()/compound_wait_match() directly, exactly like every other
//     endpoint header in this codebase already does for its own compare-
//     mode dispatch. cs keeps exactly one meaning in this file now: the
//     chained abort-on-error selector above.
//
//  3. RequestLedger::cancel_single() collapsed two different c-RCP outcomes
//     into one error: c-RCP's rcp_cancel_attempt() distinguishes
//     RCP_CANCEL_RESULT_NOT_FOUND (the target transaction_num was never
//     tracked at all) from RCP_CANCEL_RESULT_NOT_CANCELLABLE (found, but
//     already past the queued/executing window). cpp-RCP reported
//     request_not_found for both. Fixed below: a target that exists but has
//     moved to UnderExecution/Finalized/Canceled now reports the new
//     RequestErrc::request_not_cancellable; only a genuinely untracked
//     transaction_num reports request_not_found.
//
//  4. SequencerTable::try_advance() modeled compound/compound-wait
//     finalization as "advance by exactly one, wrapping, iff still at the
//     expected start state" — this codebase's own invention, and wrong
//     relative to c-RCP: c-RCP's rcp_compound_step_t carries an explicit
//     next_state sub-field (0 is the "leave the sequencer exactly where it
//     is" sentinel, not "advance to state 0"), and rcp_compound_advance_
//     guard()/rcp_compound_start_condition_met() are two DIFFERENT
//     predicates — the guard requires the sequencer literally sitting in
//     start_state (and never true for a disabled, state==0 sequencer, REQ-
//     SEQ-012); the start condition additionally treats start_state==0 as
//     an "any state" wildcard for whether the request may *begin* at all.
//     Replaced below with CompoundStep (carrying next_state) plus
//     SequencerTable::advance_guard()/start_condition_met()/
//     apply_next_state()/tick()/wait_tick(), ported directly from c-RCP's
//     rcp_compound_advance_guard()/_start_condition_met()/apply_next_state()
//     (static helper)/rcp_compound_tick()/rcp_compound_wait_tick().
//
// Genuinely new behavior ported from c-RCP that cpp-RCP never had at all
// (not a correction — c-RCP has this, cpp-RCP simply lacked it):
//   - Structured wire sub-fields for every conditional-request kind
//     (CompoundStep, TriggeredStep, chained's chain_exec_delay, timed's
//     presentation_time, clear-single's clear_transaction_num) plus their
//     encode/decode pairs over acf::encode_acf_gbb/decode_acf_gbb, reserved-
//     octet rejection, and (for the three cancellation kinds plus clear-non-
//     safestate) the evt[2:0]/hs/cs-must-be-zero wire validation.
//   - TriggeredRuntime and its occurrence-counter/threshold/fire-tick
//     primitives (ported from rcp_triggered_runtime_t and rcp_triggered_
//     runtime_enter_started()/_record_occurrence()/rcp_triggered_threshold_
//     reached()/rcp_triggered_tick()) — a triggered request has no sequencer
//     of its own; this state is independent of SequencerTable entirely.
//   - Timed-request admission: timed_too_far()/timed_admit()/timed_due(),
//     ported from rcp_timed_too_far()/_admit()/_due(), plus timed_feature_
//     enabled() and wire_error_for() mapping an admission outcome onto
//     acf::WireErrorCode::GptpFail/PresentationTimeTooFar (this project's
//     own numbered-wire-error-code enum, already covering both values —
//     no new error table needed). cpp-RCP's svr_implemented_options bitmask
//     (regmap::kOptConditionalRequests) is coarser than c-RCP's own four
//     independent per-feature bits (compound-wait/trigger/chained/time-sync)
//     — timed_feature_enabled() below gates on kOptConditionalRequests,
//     the one bit this codebase already uses for "any conditional-request
//     kind is implemented", rather than inventing a cpp-RCP-only time-sync
//     bit no other module in this tree would ever set.
//
// Deliberately NOT ported (a "genuinely better shape" call, not an
// oversight — see the file's own established precedent for documenting such
// calls):
//   - c-RCP's rcp_chained_advance()/rcp_cancel_chain_should_cascade() model
//     a chain positionally (has_predecessor bool, 0-based chain_position)
//     because c-RCP's request.c is a pure-function library with no request
//     store of its own. cpp-RCP's RequestLedger already tracks chained_
//     predecessor/chained_successors as a real graph and cascades
//     cancellation/abort by walking it (cascade_cancel/
//     propagate_chain_completion) — strictly more capable than a caller
//     re-deriving position-based cascade logic itself, so it stays as the
//     one chained-sequencing model in this file. should_execute_chained()
//     below is still ported 1:1 (fixed for polarity, see delta #1 above) as
//     the pure per-successor predicate that graph walk consults.
//
// TODO(phase1-followup): c-RCP's REQ-SEQ-013/014 (request_sequencer.h) give
// each sequencer its own owner (Request_stream_index) and a fail-closed
// rcp_sequencer_access_permitted()/access_check() gate — "a sequencer with
// no owner yet configured permits no client at all". SequencerTable below
// has no owner storage at all (it remains a thin behavior layer over
// regmap::RegisterMap::sequencer_states, which itself has no parallel
// owner-per-sequencer vector) — adding it means extending regmap.hpp's
// storage, which is out of this pass's stated scope (request.hpp content
// only). Left for a follow-up that touches regmap.hpp deliberately.
//
// TODO(phase1-followup): c-RCP's rcp_compound_peek_request_type() /
// rcp_timed_encode_request_tscf() are thin convenience wrappers (peek the
// opcode byte without a full decode; build a TSCF-headed ACF_ABB timed
// request instead of the NTSCF/repurposed-timestamp path) this file does
// not reproduce — a caller already has acf::peek_acf_msg_type() plus this
// file's own decode_* functions for the first, and acf::encode_acf_abb() +
// avtp::encode_tscf() (composed exactly as c-RCP's own thin wrapper does)
// for the second; neither is core protocol behavior, just a named
// convenience c-RCP happens to also offer.
//
// TODO(phase6-followup): Phase 6 (requirement-catalog re-derivation, cpp-RCP
// issue #129 batch 3/13) confirmed the following real, currently-unfixed
// gaps against c-RCP while re-deriving .fusa-reqs.json entries for this
// file's own already-orphan-cited ids — filed to .fusa-reqs-pending.json
// (REQ-CMP-008/009, REQ-TRIG-003, REQ-CHAIN-003, REQ-CHAIN-008, REQ-TIMED-
// 012/013), not fixed here (out of this cataloging pass's scope):
//   - encode_compound_request()/encode_triggered_request()/encode_chained_
//     member() never validate their own `type`/payload-size inputs before
//     encoding (REQ-CMP-008/009, REQ-TRIG-003, REQ-CHAIN-003): each forwards
//     straight to acf::encode_acf_gbb(), which by its own documented design
//     (acf.hpp's "always returns bytes, never an error code" contract) never
//     rejects an oversized payload or unrecognized opcode, and each of these
//     three functions returns a plain std::vector<uint8_t> (not
//     std::optional, unlike encode_timed_request()'s own std::nullopt-on-
//     invalid-input convention just below), so there is structurally no
//     channel to signal rejection even if the check were added without a
//     signature change.
//   - RequestLedger::submit() never rejects a Chained-opcode record whose
//     chained_predecessor is unset (REQ-CHAIN-008): TC18 §11.2.2.6 requires
//     a chain request with no predecessor (e.g. the first request in an
//     AVTPDU) to be rejected and its whole chain ignored; this file has no
//     CHAIN_ERROR-equivalent RequestErrc value and no detection for this
//     case at all. Distinct from the "already-aborted chain" case
//     (REQ-CHAIN-009), which cascade_cancel()/propagate_chain_completion()
//     genuinely do handle — see delta #4 above.
//   - No dispatch/admission entry point anywhere in cpp-RCP threads a real
//     TSCF header's avtp_timestamp into timed_admit()/timed_due()
//     (REQ-TIMED-012/013): this file provides every primitive the gate
//     needs, but per this file's own design note two paragraphs below, it
//     does not itself run a dispatch loop, and no other module in this tree
//     supplies one either.
//
// Field names and behavior below implement TC18's *behavior* as described in
// an internal structured extraction of the specification; no text from that
// document is reproduced here. The concrete opcode-to-byte mapping, the
// param-byte layout within the repurposed timestamp slot, and the request-
// ledger data model chosen in this file are this implementation's own
// encoding of that behavior, same as the equivalent disclaimers in
// rcp/avtp.hpp, rcp/acf.hpp, and rcp/regmap.hpp. This header models the
// taxonomy, the bundling rules, and the state machine's transitions and
// effects — it does not implement a running scheduler thread; wiring
// select_next_due()'s output into an actual dispatch loop is left to the
// embedding application, same as every other endpoint header in this
// codebase.
#pragma once

#include <rcp/acf.hpp>
#include <rcp/regmap.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace rcp {
namespace request {

// ── Errors ────────────────────────────────────────────────────────────────────

enum class RequestErrc : int {
    timestamp_not_repurposed     = 1, // mtv=1: message_timestamp is a real timestamp, not a request_type opcode
    unknown_request_type         = 2, // byte 0 of the repurposed slot is not one of the defined opcodes
    index_out_of_range           = 3, // SequencerTable index >= table size
    unknown_transaction          = 4, // RequestLedger lookup by transaction_num failed
    invalid_lifecycle_transition = 5, // requested state transition is not the single next step
    transaction_num_collision    = 6, // submit() with a transaction_num already tracked
    request_not_found            = 7, // REQUEST_NOT_FOUND — cancel_single target unknown
    request_canceled             = 8, // REQUEST_CANCELED — recorded outcome of a canceled request
    compound_bundle_incomplete   = 9, // compound/compound-wait claimed without every required companion capability
    request_not_cancellable      = 10, // found, but past the queued/executing window (c-RCP RCP_CANCEL_RESULT_NOT_CANCELLABLE)
    reserved_field_nonzero       = 11, // a reserved wire sub-field octet carries a set bit
    evt_hs_cs_nonzero            = 12, // evt[2:0]/hs/cs must be zero for this request kind and are not
    unsupported_cmd              = 13, // hs and/or cs set on a Timed request
    ledger_full                  = 14, // RequestLedger has reached its fixed capacity (kMaxTrackedRequests)
};

inline const std::error_category& request_category() noexcept {
    struct Cat : std::error_category {
        const char* name() const noexcept override { return "rcp.request"; }
        std::string message(int ev) const override {
            switch (static_cast<RequestErrc>(ev)) {
            case RequestErrc::timestamp_not_repurposed:
                return "rcp/request: mtv=1 — message_timestamp is not a repurposed request_type slot";
            case RequestErrc::unknown_request_type:
                return "rcp/request: unrecognized request_type opcode";
            case RequestErrc::index_out_of_range:
                return "rcp/request: sequencer index out of range";
            case RequestErrc::unknown_transaction:
                return "rcp/request: transaction_num is not tracked by this ledger";
            case RequestErrc::invalid_lifecycle_transition:
                return "rcp/request: requested lifecycle transition is not the next state in sequence";
            case RequestErrc::transaction_num_collision:
                return "rcp/request: transaction_num is already tracked by this ledger";
            case RequestErrc::request_not_found:
                return "rcp/request: REQUEST_NOT_FOUND";
            case RequestErrc::request_canceled:
                return "rcp/request: REQUEST_CANCELED";
            case RequestErrc::compound_bundle_incomplete:
                return "rcp/request: compound support requires compound-wait, clear-non-safestate, "
                       "and >=4 sequencers together, not compound alone";
            case RequestErrc::request_not_cancellable:
                return "rcp/request: found, but past the queued/executing cancellable window";
            case RequestErrc::reserved_field_nonzero:
                return "rcp/request: a reserved wire sub-field octet is not zero";
            case RequestErrc::evt_hs_cs_nonzero:
                return "rcp/request: evt[2:0], hs, or cs must be zero for this request kind";
            case RequestErrc::unsupported_cmd:
                return "rcp/request: hs/cs must be clear on a Timed request";
            case RequestErrc::ledger_full:
                return "rcp/request: RequestLedger has reached its fixed capacity";
            default:
                return "rcp/request: unknown error";
            }
        }
    };
    static Cat instance;
    return instance;
}

inline std::error_code make_error_code(RequestErrc e) noexcept {
    return {static_cast<int>(e), request_category()};
}

// ── request_type opcode — the message_timestamp-repurposing trick ────────────
// When an ACF_GBB message's `mtv` bit is clear, the 64-bit message_timestamp
// slot it would otherwise carry (and which ACF_GBB always reserves space for
// regardless of `mtv`, per rcp/acf.hpp) is repurposed: its first byte
// becomes this opcode, and the remaining 7 bytes carry opcode-specific
// parameters. The eleven opcodes below are every kind this module defines:
// five conditional request kinds, three cancellation kinds, and the three
// MSB-set (0x8x) safety-tagged variants of compound/compound-wait/triggered.
// The mandatory "standard" request kind (rcp::acf::RequestKind) has no
// opcode here at all — it is always carried as ACF_ABB, which has no
// message_timestamp slot to repurpose in the first place.
//
// A safety-tagged request only actually executes once its endpoint is in its
// configured safe state — see rcp/e2e.hpp's endpoint_in_configured_safe_
// state()/may_execute_now(), which this header does not itself evaluate.

enum class RequestTypeOpcode : uint8_t {
    Chained            = 0x01,
    ClearAll           = 0x05,
    ClearNonSafestate  = 0x06,
    ClearSingle        = 0x07,
    Timed              = 0x0A,
    CompoundWait       = 0x0B,
    Triggered          = 0x0E,
    Compound           = 0x0F,
    CompoundWaitSafety = 0x8B,
    TriggeredSafety    = 0x8E,
    CompoundSafety     = 0x8F,
};

constexpr bool is_valid_request_type(uint8_t byte) noexcept {
    switch (static_cast<RequestTypeOpcode>(byte)) {
    case RequestTypeOpcode::Chained:
    case RequestTypeOpcode::ClearAll:
    case RequestTypeOpcode::ClearNonSafestate:
    case RequestTypeOpcode::ClearSingle:
    case RequestTypeOpcode::Timed:
    case RequestTypeOpcode::CompoundWait:
    case RequestTypeOpcode::Triggered:
    case RequestTypeOpcode::Compound:
    case RequestTypeOpcode::CompoundWaitSafety:
    case RequestTypeOpcode::TriggeredSafety:
    case RequestTypeOpcode::CompoundSafety:
        return true;
    default:
        return false;
    }
}

// is_safety_variant reports whether `type` is one of the three MSB-set
// (`0x8x`) safety-tagged opcodes above — the single source of truth
// RequestRecord::is_safety is derived from (see request_record_for()).
constexpr bool is_safety_variant(RequestTypeOpcode type) noexcept {
    switch (type) {
    case RequestTypeOpcode::CompoundWaitSafety:
    case RequestTypeOpcode::TriggeredSafety:
    case RequestTypeOpcode::CompoundSafety:
        return true;
    default:
        return false;
    }
}

// is_compound/is_compound_wait/is_triggered — ported from c-RCP's
// rcp_request_type_is_compound()/_is_compound_wait()/_is_triggered(): each
// recognizes its own opcode AND its safety-tagged counterpart.
constexpr bool is_compound(RequestTypeOpcode type) noexcept {
    return type == RequestTypeOpcode::Compound || type == RequestTypeOpcode::CompoundSafety;
}
constexpr bool is_compound_wait(RequestTypeOpcode type) noexcept {
    return type == RequestTypeOpcode::CompoundWait || type == RequestTypeOpcode::CompoundWaitSafety;
}
constexpr bool is_triggered(RequestTypeOpcode type) noexcept {
    return type == RequestTypeOpcode::Triggered || type == RequestTypeOpcode::TriggeredSafety;
}

// encode_request_type packs `type` into the top byte and `params` into the
// remaining 7 bytes of a 64-bit value, most-significant byte first.
constexpr uint64_t encode_request_type(RequestTypeOpcode type, const std::array<uint8_t, 7>& params) noexcept {
    uint64_t v = static_cast<uint64_t>(static_cast<uint8_t>(type)) << 56;
    for (size_t i = 0; i < 7; ++i)
        v |= static_cast<uint64_t>(params[i]) << (48 - 8 * i);
    return v;
}

// decode_request_type is the inverse of encode_request_type, gated on `mtv`
// being clear — a caller that has not first checked AcfMessageInfo::mtv gets
// a decode failure rather than silently misreading a real timestamp's high
// byte as an opcode.
inline std::error_code decode_request_type(bool mtv, uint64_t message_timestamp,
                                            RequestTypeOpcode& out_type,
                                            std::array<uint8_t, 7>& out_params) noexcept {
    if (mtv) return make_error_code(RequestErrc::timestamp_not_repurposed);
    const uint8_t byte0 = static_cast<uint8_t>((message_timestamp >> 56) & 0xFF);
    if (!is_valid_request_type(byte0)) return make_error_code(RequestErrc::unknown_request_type);
    out_type = static_cast<RequestTypeOpcode>(byte0);
    for (size_t i = 0; i < 7; ++i)
        out_params[i] = static_cast<uint8_t>((message_timestamp >> (48 - 8 * i)) & 0xFF);
    return {};
}

// make_conditional_request builds the AcfMessageInfo header for a
// conditional/cancellation request: always ACF_GBB, always mtv=false (the
// repurposing trick above requires it), with `cs` set per the caller's
// request-kind-specific meaning — after this pass's delta #2 above, the only
// kind that gives cs real meaning is chained (should_execute_chained).
inline acf::AcfMessageInfo make_conditional_request(avtp::ByteBusId bus_id, uint8_t transaction_num,
                                                      bool cs) noexcept {
    acf::AcfMessageInfo info;
    info.acf_msg_type    = acf::kAcfMsgTypeGbb;
    info.mtv              = false;
    info.byte_bus_id      = bus_id;
    info.transaction_num  = transaction_num;
    info.cs                = cs;
    return info;
}

namespace detail {

// decode_repurposed is the shared first stage every kind-specific decode_*
// function below runs: decode the ACF_GBB envelope, then reinterpret its
// message_timestamp as a request_type opcode + 7 parameter bytes. Kind-
// specific decoders take it from here (validate the opcode is one they
// recognize, unpack their own sub-fields, apply their own reserved/evt/hs/cs
// rules).
inline std::error_code decode_repurposed(const uint8_t* b, size_t len, acf::AcfMessageInfo& out_info,
                                          RequestTypeOpcode& out_type, std::array<uint8_t, 7>& out_params,
                                          std::vector<uint8_t>& out_payload) noexcept {
    uint64_t ts = 0;
    auto ec = acf::decode_acf_gbb(b, len, out_info, ts, out_payload);
    if (ec) return ec;
    return decode_request_type(out_info.mtv, ts, out_type, out_params);
}

// reserved_zero/reserved_nonzero_error: small shared helpers so each kind's
// own reserved-octet check below is a one-liner instead of a hand-rolled
// loop repeated five times.
constexpr bool all_zero(const std::array<uint8_t, 7>& p, std::initializer_list<size_t> indices) noexcept {
    for (size_t i : indices)
        if (p[i] != 0) return false;
    return true;
}

inline std::error_code check_evt_hs_cs_zero(const acf::AcfMessageInfo& info) noexcept {
    if (info.evt_op != 0 || info.hs || info.cs) return make_error_code(RequestErrc::evt_hs_cs_nonzero);
    return {};
}

} // namespace detail

// ── Compound / compound-wait (0x0F/0x8F, 0x0B/0x8B) ───────────────────────────
// Compound and compound-wait share one on-wire sub-field shape — the
// specification defines both kinds with identical sub-field widths and
// offsets (only the field-name prefix differs) — so one struct, CompoundStep,
// models both, ported from c-RCP's rcp_compound_step_t.

constexpr uint16_t kCompoundRepeatInfinite = 0xFFFF;

struct CompoundStep {
    regmap::SequencerState start_state     = 0; // the sequencer state this step requires
    regmap::SequencerState next_state      = 0; // the state this step advances the sequencer to; 0 = "leave it where it is"
    uint8_t                sequencer_index = 0; // which of a table's sequencers this step targets
    uint16_t                exec_delay      = 0; // counted in multiples of the endpoint's own ep_delay_time, NOT ms
    uint16_t                repeat_count    = 0; // remaining repetitions; kCompoundRepeatInfinite = never decrement
};

// Octet offsets within the repurposed 8-byte message_timestamp region:
//   0 request_type | 1 start_state | 2 next_state | 3 sequencer_index |
//   4..5 exec_delay (BE) | 6..7 repeat_count (BE)
constexpr std::array<uint8_t, 7> encode_compound_step_params(const CompoundStep& step) noexcept {
    return {
        step.start_state,
        step.next_state,
        step.sequencer_index,
        static_cast<uint8_t>((step.exec_delay >> 8) & 0xFF),
        static_cast<uint8_t>(step.exec_delay & 0xFF),
        static_cast<uint8_t>((step.repeat_count >> 8) & 0xFF),
        static_cast<uint8_t>(step.repeat_count & 0xFF),
    };
}

constexpr CompoundStep decode_compound_step_params(const std::array<uint8_t, 7>& p) noexcept {
    CompoundStep step;
    step.start_state     = p[0];
    step.next_state       = p[1];
    step.sequencer_index  = p[2];
    step.exec_delay        = static_cast<uint16_t>((static_cast<uint16_t>(p[3]) << 8) | p[4]);
    step.repeat_count      = static_cast<uint16_t>((static_cast<uint16_t>(p[5]) << 8) | p[6]);
    return step;
}

struct CompoundRequest {
    RequestTypeOpcode     type{};
    avtp::ByteBusId        byte_bus_id = 0;
    CompoundStep           step;
    uint8_t                 evt_op       = 0; // ACF header evt[2:0] — compound-wait's own comparison-mode
                                                // selector, see acf::compound_wait_evt_valid()/_match()
    uint8_t                 transaction_num = 0;
    std::vector<uint8_t>   payload;
};

// encode_compound_request builds an ACF_GBB-framed compound or compound-wait
// request. `type` must be one of Compound[Safety]/CompoundWait[Safety]; `cs`
// is always forced false (delta #2 above — cs has no meaning here in
// c-RCP's own implementation). evt_op is packed into the ACF header's own
// evt[2:0] field, never into one of `step`'s repurposed sub-field bytes.
inline std::vector<uint8_t> encode_compound_request(RequestTypeOpcode type, avtp::ByteBusId byte_bus_id,
                                                       const CompoundStep& step, uint8_t evt_op,
                                                       uint8_t transaction_num,
                                                       const std::vector<uint8_t>& payload = {}) {
    acf::AcfMessageInfo info;
    info.byte_bus_id     = byte_bus_id;
    info.evt_op            = static_cast<uint8_t>(evt_op & 0x7);
    info.cs                = false;
    info.transaction_num  = transaction_num;
    const uint64_t ts = encode_request_type(type, encode_compound_step_params(step));
    return acf::encode_acf_gbb(info, ts, payload);
}

inline std::error_code decode_compound_request(const uint8_t* b, size_t len, CompoundRequest& out) noexcept {
    acf::AcfMessageInfo info;
    RequestTypeOpcode type{};
    std::array<uint8_t, 7> params{};
    auto ec = detail::decode_repurposed(b, len, info, type, params, out.payload);
    if (ec) return ec;
    if (!is_compound(type) && !is_compound_wait(type)) return make_error_code(RequestErrc::unknown_request_type);

    out.type            = type;
    out.byte_bus_id       = info.byte_bus_id;
    out.step              = decode_compound_step_params(params);
    out.evt_op            = info.evt_op;
    out.transaction_num  = info.transaction_num;
    return {};
}

// ── clear-non-safestate (0x06) ────────────────────────────────────────────────
// Part of the compound bundle (see FeatureSet below): cancels every pending/
// started non-safety-tagged request. Carries no sub-field of its own beyond
// the opcode byte; every trailing octet, and evt[2:0]/hs/cs, must be zero.

struct ClearNonSafestateRequest {
    avtp::ByteBusId byte_bus_id     = 0;
    uint8_t          transaction_num = 0;
};

inline std::vector<uint8_t> encode_clear_non_safestate(avtp::ByteBusId byte_bus_id, uint8_t transaction_num) {
    acf::AcfMessageInfo info;
    info.byte_bus_id     = byte_bus_id;
    info.transaction_num  = transaction_num;
    const uint64_t ts = encode_request_type(RequestTypeOpcode::ClearNonSafestate, {});
    return acf::encode_acf_gbb(info, ts, {});
}

inline std::error_code decode_clear_non_safestate(const uint8_t* b, size_t len,
                                                    ClearNonSafestateRequest& out) noexcept {
    acf::AcfMessageInfo info;
    RequestTypeOpcode type{};
    std::array<uint8_t, 7> params{};
    std::vector<uint8_t> payload;
    auto ec = detail::decode_repurposed(b, len, info, type, params, payload);
    if (ec) return ec;
    if (type != RequestTypeOpcode::ClearNonSafestate) return make_error_code(RequestErrc::unknown_request_type);
    if (!detail::all_zero(params, {0, 1, 2, 3, 4, 5, 6})) return make_error_code(RequestErrc::reserved_field_nonzero);
    ec = detail::check_evt_hs_cs_zero(info);
    if (ec) return ec;

    out.byte_bus_id     = info.byte_bus_id;
    out.transaction_num = info.transaction_num;
    return {};
}

// ── Request categories & execution-priority ordering ──────────────────────────

enum class RequestCategory : uint8_t {
    Cancellation = 0,
    Triggered    = 1,
    Timed        = 2,
    Compound     = 3,
    CompoundWait = 4,
    Chained      = 5,
    Standard     = 6,
};

// category_of maps a decoded request_type opcode onto its priority category;
// `std::nullopt` denotes the mandatory standard request kind (no opcode). A
// safety-tagged (0x8x) opcode shares its base opcode's category.
constexpr RequestCategory category_of(std::optional<RequestTypeOpcode> kind) noexcept {
    if (!kind.has_value()) return RequestCategory::Standard;
    switch (*kind) {
    case RequestTypeOpcode::ClearAll:
    case RequestTypeOpcode::ClearNonSafestate:
    case RequestTypeOpcode::ClearSingle:
        return RequestCategory::Cancellation;
    case RequestTypeOpcode::Triggered:
    case RequestTypeOpcode::TriggeredSafety:
        return RequestCategory::Triggered;
    case RequestTypeOpcode::Timed:
        return RequestCategory::Timed;
    case RequestTypeOpcode::Compound:
    case RequestTypeOpcode::CompoundSafety:
        return RequestCategory::Compound;
    case RequestTypeOpcode::CompoundWait:
    case RequestTypeOpcode::CompoundWaitSafety:
        return RequestCategory::CompoundWait;
    case RequestTypeOpcode::Chained:
        return RequestCategory::Chained;
    default:
        return RequestCategory::Standard; // unreachable given a validated opcode
    }
}

constexpr uint8_t priority_rank(RequestCategory c) noexcept { return static_cast<uint8_t>(c); }

struct DueCandidate {
    RequestCategory category    = RequestCategory::Standard;
    size_t          arrival_seq = 0;
};

// select_next_due picks the winning index out of `due` by lowest
// priority_rank, breaking ties by lowest arrival_seq (FIFO). Returns
// std::nullopt for an empty input rather than an out-of-range index.
inline std::optional<size_t> select_next_due(const std::vector<DueCandidate>& due) noexcept {
    if (due.empty()) return std::nullopt;
    size_t best = 0;
    for (size_t i = 1; i < due.size(); ++i) {
        const auto& cur  = due[i];
        const auto& best_c = due[best];
        if (priority_rank(cur.category) < priority_rank(best_c.category) ||
            (priority_rank(cur.category) == priority_rank(best_c.category) &&
             cur.arrival_seq < best_c.arrival_seq)) {
            best = i;
        }
    }
    return best;
}

// frame_timing_consistent — ported from c-RCP's rcp_sched_frame_timing_
// consistent(): a TSCF-headed AVTPDU's single avtp_timestamp applies
// uniformly to every ACF member packed inside it, so a frame mixing a Timed
// (0x0A) member with any non-Timed member is never well-formed — either
// every member in a TSCF frame is itself Timed, or none are. NTSCF frames
// carry no shared presentation time and are exempt entirely.
// member_is_timed.empty() is trivially consistent (true), matching c-RCP's
// own count==0 case.
inline bool frame_timing_consistent(bool is_tscf, const std::vector<bool>& member_is_timed) noexcept {
    if (!is_tscf || member_is_timed.empty()) return true;
    for (size_t i = 1; i < member_is_timed.size(); ++i)
        if (member_is_timed[i] != member_is_timed[0]) return false;
    return true;
}

// ── The `cs` field's one remaining meaning: chained abort-on-error ───────────
// AcfMessageInfo::cs (rcp/acf.hpp) is a single wire bit. After this pass's
// delta #2 above, chained is the only request kind in this file that gives
// it real meaning: cs is read off the member about to run, about its
// predecessor's outcome.

// should_execute_chained answers, for one chained successor: given its own
// `cs` bit and whether its predecessor finished in error, should this
// successor still run? cs=false (RCP_CHAINED_CS_CONTINUE_ON_ERROR) means
// "execute regardless"; cs=true (RCP_CHAINED_CS_ABORT_ON_ERROR) means "abort
// if the predecessor errored". A predecessor that did *not* error always
// permits the successor to run, independent of cs. (cpp-RCP issue #58: this
// polarity was inverted before this pass — see delta #1 above.)
constexpr bool should_execute_chained(bool cs, bool predecessor_errored) noexcept {
    return !cs || !predecessor_errored;
}

// ── Chained (0x01) ─────────────────────────────────────────────────────────────
// A chain member carries exactly one sub-field of its own, chain_exec_delay,
// at octets 4..5; octets 1..3 and 6..7 are reserved (all-zero). cs (the
// member's own abort/continue selector, see should_execute_chained above)
// rides the ACF header's own cs bit, not one of the repurposed sub-fields.

struct ChainedMember {
    avtp::ByteBusId byte_bus_id      = 0;
    uint16_t         chain_exec_delay = 0; // measured from the predecessor's own finalization
    bool             cs                = false;
    uint8_t          transaction_num  = 0;
    std::vector<uint8_t> payload;
};

inline std::vector<uint8_t> encode_chained_member(avtp::ByteBusId byte_bus_id, uint16_t chain_exec_delay,
                                                     bool cs, uint8_t transaction_num,
                                                     const std::vector<uint8_t>& payload = {}) {
    acf::AcfMessageInfo info;
    info.byte_bus_id     = byte_bus_id;
    info.cs                = cs;
    info.transaction_num  = transaction_num;
    const std::array<uint8_t, 7> params = {
        0, 0, 0,
        static_cast<uint8_t>((chain_exec_delay >> 8) & 0xFF),
        static_cast<uint8_t>(chain_exec_delay & 0xFF),
        0, 0,
    };
    const uint64_t ts = encode_request_type(RequestTypeOpcode::Chained, params);
    return acf::encode_acf_gbb(info, ts, payload);
}

inline std::error_code decode_chained_member(const uint8_t* b, size_t len, ChainedMember& out) noexcept {
    acf::AcfMessageInfo info;
    RequestTypeOpcode type{};
    std::array<uint8_t, 7> params{};
    auto ec = detail::decode_repurposed(b, len, info, type, params, out.payload);
    if (ec) return ec;
    if (type != RequestTypeOpcode::Chained) return make_error_code(RequestErrc::unknown_request_type);
    if (!detail::all_zero(params, {0, 1, 2, 5, 6})) return make_error_code(RequestErrc::reserved_field_nonzero);

    out.byte_bus_id       = info.byte_bus_id;
    out.chain_exec_delay  = static_cast<uint16_t>((static_cast<uint16_t>(params[3]) << 8) | params[4]);
    out.cs                = info.cs;
    out.transaction_num  = info.transaction_num;
    return {};
}

inline bool chained_exec_delay_elapsed(uint16_t chain_exec_delay, uint32_t elapsed) noexcept {
    return elapsed >= static_cast<uint32_t>(chain_exec_delay);
}

// ── Optional-feature bundling ──────────────────────────────────────────────────
// Compound support cannot be claimed piecemeal: a repo implementing compound
// (0x0F) must also implement compound-wait (0x0B), clear-non-safestate
// cancellation (0x06), and at least 4 sequencer slots — all four together,
// or none of them advertised as supported. Triggered, chained, and timed are
// each independently flaggable; clear-single is its own "enhanced
// cancellation" capability with no further dependency modeled here.

constexpr size_t kMinCompoundSequencers = 4;

struct FeatureSet {
    bool compound             = false;
    bool compound_wait        = false;
    bool triggered            = false;
    bool chained              = false;
    bool timed                = false;
    bool clear_non_safestate  = false;
    bool clear_single         = false;
    size_t sequencer_count    = 0;
};

inline std::error_code validate_feature_bundles(const FeatureSet& f) noexcept {
    const bool claims_compound_support = f.compound || f.compound_wait;
    if (!claims_compound_support) return {};
    const bool complete = f.compound && f.compound_wait && f.clear_non_safestate &&
                           f.sequencer_count >= kMinCompoundSequencers;
    return complete ? std::error_code{} : make_error_code(RequestErrc::compound_bundle_incomplete);
}

// implemented_options_bits reports the svr_implemented_options bit(s)
// (rcp/regmap.hpp) a register map should advertise for a validated feature
// set. Returns 0 for a feature set with nothing conditional enabled at all.
inline uint32_t implemented_options_bits(const FeatureSet& f) noexcept {
    const bool any_conditional = f.compound || f.compound_wait || f.triggered || f.chained || f.timed;
    return any_conditional ? regmap::kOptConditionalRequests : 0;
}

// ── Sequencer-state registers ──────────────────────────────────────────────────
// SequencerTable is the behavior layer over rcp/regmap.hpp's already-
// allocated `RegisterMap::sequencer_states` storage: every sequencer
// defaults to kDefaultState (RCP_SEQUENCER_POWER_ON_STATE, 1) rather than the
// vector's own zero default, and a sequencer with state == 0 is DISABLED
// (REQ-SEQ-012, TC18 Table 28) — no compound/compound-wait step may ever
// start from or advance through state 0, including a step whose own
// start_state is the "any state" wildcard below.
class SequencerTable {
public:
    static constexpr regmap::SequencerState kDefaultState = 1;

    explicit SequencerTable(std::vector<regmap::SequencerState>& states) noexcept : states_(states) {}
    explicit SequencerTable(regmap::RegisterMap& regs) noexcept : states_(regs.sequencer_states) {}

    // ensure_size grows (never shrinks) the underlying storage to at least
    // `count` entries, filling newly added slots with kDefaultState.
    void ensure_size(size_t count) {
        if (states_.size() < count) states_.resize(count, kDefaultState);
    }

    size_t size() const noexcept { return states_.size(); }

    std::error_code state_of(size_t index, regmap::SequencerState& out) const noexcept {
        if (index >= states_.size()) return make_error_code(RequestErrc::index_out_of_range);
        out = states_[index];
        return {};
    }

    std::error_code set_state(size_t index, regmap::SequencerState state) noexcept {
        if (index >= states_.size()) return make_error_code(RequestErrc::index_out_of_range);
        states_[index] = state;
        return {};
    }

    void reset_all_to_default() noexcept { std::fill(states_.begin(), states_.end(), kDefaultState); }

    // advance_guard — ported from c-RCP's rcp_compound_advance_guard():
    // true iff step.sequencer_index is currently sitting in step.start_state
    // AND that state is not 0 (a disabled sequencer can never satisfy this,
    // even if start_state itself happens to be written as 0 — REQ-SEQ-012).
    bool advance_guard(const CompoundStep& step) const noexcept {
        regmap::SequencerState cur = 0;
        if (state_of(step.sequencer_index, cur)) return false;
        if (cur == 0) return false;
        return cur == step.start_state;
    }

    // start_condition_met — ported from c-RCP's rcp_compound_start_
    // condition_met(): true iff this step's *start* condition is satisfied,
    // i.e. the request may begin at all. Deliberately NOT the same predicate
    // as advance_guard(): for a start_state of zero, the request starts in
    // any state (the "any state" wildcard), but advance_guard() above still
    // only reports true when the sequencer happens to already be sitting in
    // literal state 0 — which itself can never hold, since 0 means disabled.
    // A disabled (state==0) sequencer never satisfies this either, checked
    // before the wildcard so "any state" cannot itself paper over disabled.
    bool start_condition_met(const CompoundStep& step) const noexcept {
        regmap::SequencerState cur = 0;
        if (state_of(step.sequencer_index, cur)) return false;
        if (cur == 0) return false;
        if (step.start_state == 0) return true; // start in any (enabled) state
        return cur == step.start_state;
    }

    // apply_next_state honours the "remain in the current state" sentinel: a
    // next_state of zero leaves the sequencer exactly where it is (still
    // validating sequencer_index) rather than driving it to state zero
    // (which would disable it — never this function's job to do implicitly).
    std::error_code apply_next_state(const CompoundStep& step) noexcept {
        if (step.next_state == 0) {
            regmap::SequencerState dummy = 0;
            return state_of(step.sequencer_index, dummy); // validates the index only
        }
        return set_state(step.sequencer_index, step.next_state);
    }

    static bool exec_delay_elapsed(const CompoundStep& step, uint32_t elapsed) noexcept {
        return elapsed >= static_cast<uint32_t>(step.exec_delay);
    }

    // tick — compound's own: advances iff both exec_delay_elapsed and
    // advance_guard hold; otherwise the table is left entirely unchanged.
    bool tick(const CompoundStep& step, uint32_t elapsed) noexcept {
        if (!exec_delay_elapsed(step, elapsed)) return false;
        if (!advance_guard(step)) return false;
        return !apply_next_state(step);
    }

    // wait_tick — compound-wait's own: advances iff both condition_met (the
    // caller's own already-evaluated acf::compound_wait_match() result) and
    // advance_guard hold. Unlike tick(), elapsing exec_delay alone is never
    // sufficient here.
    bool wait_tick(const CompoundStep& step, bool condition_met) noexcept {
        if (!condition_met) return false;
        if (!advance_guard(step)) return false;
        return !apply_next_state(step);
    }

private:
    std::vector<regmap::SequencerState>& states_;
};

// ── Triggered (0x0E/0x8E) ──────────────────────────────────────────────────────
// A triggered request's execution condition is "a named trigger signal,
// emitted by a named endpoint, has occurred at least a named number of
// times". A triggered request has no sequencer of its own and no start/next
// state — it has no dependency on SequencerTable at all.

constexpr uint16_t kTriggeredRepeatInfinite = 0xFFFF;

struct TriggeredStep {
    uint8_t  trigger_source_ep = 0; // the endpoint whose trigger signal this request waits on
    uint8_t  trigger_signal_nr = 0; // which of that endpoint's trigger signals
    uint8_t  trigger_threshold = 0; // occurrences that must precede execution: 0 fires on the first, N on the (N+1)th
    uint16_t exec_delay         = 0;
    uint16_t repeat_count       = 0; // kTriggeredRepeatInfinite = never decrement
};

// Octet offsets: 0 request_type | 1 trigger_source_ep | 2 trigger_signal_nr |
// 3 trigger_threshold | 4..5 exec_delay (BE) | 6..7 repeat_count (BE)
constexpr std::array<uint8_t, 7> encode_triggered_step_params(const TriggeredStep& step) noexcept {
    return {
        step.trigger_source_ep,
        step.trigger_signal_nr,
        step.trigger_threshold,
        static_cast<uint8_t>((step.exec_delay >> 8) & 0xFF),
        static_cast<uint8_t>(step.exec_delay & 0xFF),
        static_cast<uint8_t>((step.repeat_count >> 8) & 0xFF),
        static_cast<uint8_t>(step.repeat_count & 0xFF),
    };
}

constexpr TriggeredStep decode_triggered_step_params(const std::array<uint8_t, 7>& p) noexcept {
    TriggeredStep step;
    step.trigger_source_ep  = p[0];
    step.trigger_signal_nr  = p[1];
    step.trigger_threshold  = p[2];
    step.exec_delay          = static_cast<uint16_t>((static_cast<uint16_t>(p[3]) << 8) | p[4]);
    step.repeat_count        = static_cast<uint16_t>((static_cast<uint16_t>(p[5]) << 8) | p[6]);
    return step;
}

struct TriggeredRequest {
    RequestTypeOpcode     type{};
    avtp::ByteBusId        byte_bus_id = 0;
    TriggeredStep           step;
    uint8_t                 transaction_num = 0;
    std::vector<uint8_t>   payload;
};

inline std::vector<uint8_t> encode_triggered_request(RequestTypeOpcode type, avtp::ByteBusId byte_bus_id,
                                                        const TriggeredStep& step, uint8_t transaction_num,
                                                        const std::vector<uint8_t>& payload = {}) {
    acf::AcfMessageInfo info;
    info.byte_bus_id     = byte_bus_id;
    info.transaction_num  = transaction_num;
    const uint64_t ts = encode_request_type(type, encode_triggered_step_params(step));
    return acf::encode_acf_gbb(info, ts, payload);
}

inline std::error_code decode_triggered_request(const uint8_t* b, size_t len, TriggeredRequest& out) noexcept {
    acf::AcfMessageInfo info;
    RequestTypeOpcode type{};
    std::array<uint8_t, 7> params{};
    auto ec = detail::decode_repurposed(b, len, info, type, params, out.payload);
    if (ec) return ec;
    if (!is_triggered(type)) return make_error_code(RequestErrc::unknown_request_type);

    out.type            = type;
    out.byte_bus_id       = info.byte_bus_id;
    out.step              = decode_triggered_step_params(params);
    out.transaction_num  = info.transaction_num;
    return {};
}

// TriggeredRuntime is one triggered request's own runtime (not wire-carried)
// state: how many matching trigger occurrences have been observed since
// entering "started". Ported from c-RCP's rcp_triggered_runtime_t.
struct TriggeredRuntime {
    uint32_t occurrence_count = 0;
    bool     started           = false;
};

inline void triggered_enter_started(TriggeredRuntime& rt) noexcept {
    rt.occurrence_count = 0;
    rt.started           = true;
}

// triggered_record_occurrence increments rt.occurrence_count, and returns
// true, iff rt.started AND the occurrence matches this request's own
// selection. A non-matching occurrence, or one arriving while rt has not
// entered "started", leaves rt entirely unchanged. Independent of any
// endpoint idle/busy status.
inline bool triggered_record_occurrence(TriggeredRuntime& rt, const TriggeredStep& step, uint8_t source_ep,
                                          uint8_t signal_nr) noexcept {
    if (!rt.started) return false;
    if (source_ep != step.trigger_source_ep || signal_nr != step.trigger_signal_nr) return false;
    ++rt.occurrence_count;
    return true;
}

inline bool triggered_threshold_reached(const TriggeredStep& step, const TriggeredRuntime& rt) noexcept {
    return rt.occurrence_count > static_cast<uint32_t>(step.trigger_threshold);
}

inline bool triggered_exec_delay_elapsed(const TriggeredStep& step, uint32_t elapsed) noexcept {
    return elapsed >= static_cast<uint32_t>(step.exec_delay);
}

// triggered_tick is the fire transition: resets rt (occurrence_count=0,
// started=false) and returns true iff ALL of started, threshold_reached,
// exec_delay_elapsed, and endpoint_idle hold. Otherwise rt is left entirely
// unchanged. endpoint_idle gates only the fire transition — the occurrence
// counter itself is deliberately not gated on it. Advances no sequencer: a
// triggered request has none.
inline bool triggered_tick(const TriggeredStep& step, TriggeredRuntime& rt, uint32_t elapsed,
                             bool endpoint_idle) noexcept {
    if (!rt.started) return false;
    if (!triggered_threshold_reached(step, rt)) return false;
    if (!triggered_exec_delay_elapsed(step, elapsed)) return false;
    if (!endpoint_idle) return false;
    rt.occurrence_count = 0;
    rt.started           = false;
    return true;
}

// ── Timed (0x0A) ───────────────────────────────────────────────────────────────
// A per-request alternative to a TSCF header: presentation_time is a 48-bit
// gPTP-domain instant in nanoseconds, reduced modulo 2^48 (rolls over every
// few days), packed directly into the repurposed message_timestamp region
// (octets 2..7; octet 1 is reserved) instead of a TSCF header's shared
// avtp_timestamp.

constexpr uint64_t kTimedPresentationTimeMax       = 0x0000FFFFFFFFFFFFull;
constexpr uint64_t kTimedPresentationTimeModulus   = 0x0001000000000000ull;

struct TimedRequest {
    avtp::ByteBusId        byte_bus_id       = 0;
    uint64_t                 presentation_time = 0; // in [0, kTimedPresentationTimeMax]
    uint8_t                  transaction_num   = 0;
    std::vector<uint8_t>   payload;
};

// timed_feature_enabled — ported from c-RCP's rcp_timed_feature_enabled(),
// which requires c-RCP's own RCP_REGMAP_OPT_TIME_SYNC bit. cpp-RCP's
// svr_implemented_options is a coarser bitmask than c-RCP's four independent
// per-feature bits; kOptConditionalRequests ("compound/triggered/timed/
// chained requests") is the one bit this codebase already uses to mean "some
// conditional-request kind, including Timed, is implemented".
inline bool timed_feature_enabled(uint32_t options) noexcept {
    return (options & regmap::kOptConditionalRequests) != 0;
}

// encode_timed_request returns std::nullopt if presentation_time exceeds
// kTimedPresentationTimeMax — never silently truncated, matching c-RCP's own
// "zeroed rcp_bytes_t on invalid input" convention for this one validated
// case (rcp_timed_encode_request()).
inline std::optional<std::vector<uint8_t>> encode_timed_request(avtp::ByteBusId byte_bus_id,
                                                                    uint64_t presentation_time,
                                                                    uint8_t transaction_num,
                                                                    const std::vector<uint8_t>& payload = {}) {
    if (presentation_time > kTimedPresentationTimeMax) return std::nullopt;
    acf::AcfMessageInfo info;
    info.byte_bus_id     = byte_bus_id;
    info.transaction_num  = transaction_num;
    const std::array<uint8_t, 7> params = {
        0, // reserved (octet 1)
        static_cast<uint8_t>((presentation_time >> 40) & 0xFF),
        static_cast<uint8_t>((presentation_time >> 32) & 0xFF),
        static_cast<uint8_t>((presentation_time >> 24) & 0xFF),
        static_cast<uint8_t>((presentation_time >> 16) & 0xFF),
        static_cast<uint8_t>((presentation_time >> 8) & 0xFF),
        static_cast<uint8_t>(presentation_time & 0xFF),
    };
    const uint64_t ts = encode_request_type(RequestTypeOpcode::Timed, params);
    return acf::encode_acf_gbb(info, ts, payload);
}

inline std::error_code decode_timed_request(const uint8_t* b, size_t len, TimedRequest& out) noexcept {
    acf::AcfMessageInfo info;
    RequestTypeOpcode type{};
    std::array<uint8_t, 7> params{};
    auto ec = detail::decode_repurposed(b, len, info, type, params, out.payload);
    if (ec) return ec;
    if (type != RequestTypeOpcode::Timed) return make_error_code(RequestErrc::unknown_request_type);
    if (params[0] != 0) return make_error_code(RequestErrc::reserved_field_nonzero);
    if (info.hs || info.cs) return make_error_code(RequestErrc::unsupported_cmd);

    uint64_t pt = 0;
    for (size_t i = 1; i < 7; ++i) pt = (pt << 8) | params[i];
    out.presentation_time = pt;
    out.byte_bus_id         = info.byte_bus_id;
    out.transaction_num    = info.transaction_num;
    return {};
}

enum class TimedAdmission : uint8_t {
    Accept,
    RejectGptpFail,
    RejectPresentationTimeTooFar,
};

namespace detail {
inline uint64_t timed_forward_delta(uint64_t presentation_time, uint64_t now) noexcept {
    return (presentation_time - now) & kTimedPresentationTimeMax;
}
inline bool timed_in_the_past(uint64_t delta) noexcept {
    return delta > (kTimedPresentationTimeModulus / 2);
}
} // namespace detail

// timed_too_far — true iff presentation_time sits strictly in the future of
// `now` (wraparound-safe, modulo the 48-bit rollover period) by more than
// max_horizon. A presentation_time at or before now is never "too far".
inline bool timed_too_far(uint64_t presentation_time, uint64_t now, uint64_t max_horizon) noexcept {
    const uint64_t delta = detail::timed_forward_delta(presentation_time, now);
    if (detail::timed_in_the_past(delta)) return false;
    return delta > max_horizon;
}

inline bool timed_due(uint64_t presentation_time, uint64_t now) noexcept {
    const uint64_t delta = detail::timed_forward_delta(presentation_time, now);
    return delta == 0 || detail::timed_in_the_past(delta);
}

// timed_admit is the combined admission decision: RejectGptpFail if
// !gptp_locked (takes priority — presentation_time cannot be trusted at all
// without a locked time base), else RejectPresentationTimeTooFar if
// timed_too_far(), else Accept.
inline TimedAdmission timed_admit(bool gptp_locked, uint64_t presentation_time, uint64_t now,
                                    uint64_t max_horizon) noexcept {
    if (!gptp_locked) return TimedAdmission::RejectGptpFail;
    if (timed_too_far(presentation_time, now, max_horizon)) return TimedAdmission::RejectPresentationTimeTooFar;
    return TimedAdmission::Accept;
}

// wire_error_for maps a TimedAdmission onto this project's own numbered wire
// error code (acf::WireErrorCode, extraction Table 27) for a caller
// populating an Error Response frame. std::nullopt for Accept — nothing to
// report.
inline std::optional<acf::WireErrorCode> wire_error_for(TimedAdmission a) noexcept {
    switch (a) {
    case TimedAdmission::RejectGptpFail:                return acf::WireErrorCode::GptpFail;
    case TimedAdmission::RejectPresentationTimeTooFar:  return acf::WireErrorCode::PresentationTimeTooFar;
    default:                                              return std::nullopt;
    }
}

// ── Cancellation: clear-all (0x05) and clear-single (0x07) ───────────────────
// clear-non-safestate (0x06) lives in the compound section above (it is part
// of the compound bundle, not independently flaggable). clear-all and
// clear-single both carry no payload of their own beyond their opcode byte
// (and, for clear-single, the one clear_transaction_num sub-field at octet
// 3) — the remaining sub-field bytes are always reserved-zero, and
// evt[2:0]/hs/cs must be zero for both, same rule as clear-non-safestate.

struct ClearAllRequest {
    avtp::ByteBusId byte_bus_id     = 0;
    uint8_t          transaction_num = 0;
};

inline std::vector<uint8_t> encode_clear_all(avtp::ByteBusId byte_bus_id, uint8_t transaction_num) {
    acf::AcfMessageInfo info;
    info.byte_bus_id     = byte_bus_id;
    info.transaction_num  = transaction_num;
    const uint64_t ts = encode_request_type(RequestTypeOpcode::ClearAll, {});
    return acf::encode_acf_gbb(info, ts, {});
}

inline std::error_code decode_clear_all(const uint8_t* b, size_t len, ClearAllRequest& out) noexcept {
    acf::AcfMessageInfo info;
    RequestTypeOpcode type{};
    std::array<uint8_t, 7> params{};
    std::vector<uint8_t> payload;
    auto ec = detail::decode_repurposed(b, len, info, type, params, payload);
    if (ec) return ec;
    if (type != RequestTypeOpcode::ClearAll) return make_error_code(RequestErrc::unknown_request_type);
    if (!detail::all_zero(params, {0, 1, 2, 3, 4, 5, 6})) return make_error_code(RequestErrc::reserved_field_nonzero);
    ec = detail::check_evt_hs_cs_zero(info);
    if (ec) return ec;

    out.byte_bus_id     = info.byte_bus_id;
    out.transaction_num = info.transaction_num;
    return {};
}

struct ClearSingleRequest {
    avtp::ByteBusId byte_bus_id             = 0;
    uint8_t          clear_transaction_num   = 0; // the previously-queued request this cancels
    uint8_t          transaction_num          = 0;
};

// Octet offsets: 0 request_type | 1..2 reserved | 3 clear_transaction_num | 4..7 reserved
inline std::vector<uint8_t> encode_clear_single(avtp::ByteBusId byte_bus_id, uint8_t clear_transaction_num,
                                                   uint8_t transaction_num) {
    acf::AcfMessageInfo info;
    info.byte_bus_id     = byte_bus_id;
    info.transaction_num  = transaction_num;
    const std::array<uint8_t, 7> params = {0, 0, clear_transaction_num, 0, 0, 0, 0};
    const uint64_t ts = encode_request_type(RequestTypeOpcode::ClearSingle, params);
    return acf::encode_acf_gbb(info, ts, {});
}

inline std::error_code decode_clear_single(const uint8_t* b, size_t len, ClearSingleRequest& out) noexcept {
    acf::AcfMessageInfo info;
    RequestTypeOpcode type{};
    std::array<uint8_t, 7> params{};
    std::vector<uint8_t> payload;
    auto ec = detail::decode_repurposed(b, len, info, type, params, payload);
    if (ec) return ec;
    if (type != RequestTypeOpcode::ClearSingle) return make_error_code(RequestErrc::unknown_request_type);
    if (!detail::all_zero(params, {0, 1, 3, 4, 5, 6})) return make_error_code(RequestErrc::reserved_field_nonzero);
    ec = detail::check_evt_hs_cs_zero(info);
    if (ec) return ec;

    out.clear_transaction_num = params[2];
    out.byte_bus_id             = info.byte_bus_id;
    out.transaction_num          = info.transaction_num;
    return {};
}

// ── Fixed-capacity bounded storage ─────────────────────────────────────────────
// c-RCP's own request.c/request_sequencer.c have no ledger-shaped storage to
// port a bound from (request.c is a pure-function library; request_
// sequencer.c's table is still heap-allocated via rcp_malloc(count), sized
// from svr_sequencers_max at runtime — deliberately NOT yet converted to
// fixed-capacity as of this pass, per c-RCP's own CHANGELOG.md, issue #521
// "[c-RCP-17]"). RequestLedger below is a cpp-RCP-original data structure
// (c-RCP has no equivalent), so this bound is this port's own engineering
// decision, not a literal c-RCP value.
//
// transaction_num's own domain is a full uint8_t (256 distinct values), but
// kMaxTrackedRequests is deliberately chosen well below that: RequestLedger
// tracks every submitted transaction_num for the ledger's entire lifetime
// (finalized/canceled records are kept, never evicted — see
// RequestLedger::submit's own doc comment), and submit()'s collision check
// runs BEFORE its capacity check — so a bound of exactly 256 would make the
// full-ledger path unreachable through the public API: once every uint8_t
// value is tracked, every possible resubmission is by definition a
// collision, and the capacity check could never independently fire (dead
// code, undetectable by any test). 64 — the same order of magnitude as
// other c-RCP-17 fixed-capacity conversions elsewhere in this project's
// history (e.g. RCP_POWERSTATE_MAX_ENDPOINTS=64) — is comfortably below
// transaction_num's full range, so a genuinely fresh, never-submitted
// transaction_num can always still exist once the ledger is full, making
// the ledger_full outcome a real, reachable, testable scenario rather than
// unreachable defensive code.
constexpr size_t kMaxTrackedRequests = 64;

namespace detail {

// BoundedVector<T, Capacity> — a small, fixed-capacity, std::array-backed
// container with std::vector-shaped ergonomics (push_back, size, iteration)
// for the one thing RequestLedger needs: push_back that reports "full"
// instead of growing without bound.
template <typename T, std::size_t Capacity>
class BoundedVector {
public:
    bool push_back(T value) noexcept {
        if (size_ >= Capacity) return false;
        data_[size_] = std::move(value);
        ++size_;
        return true;
    }

    size_t size() const noexcept { return size_; }
    static constexpr size_t capacity() noexcept { return Capacity; }
    bool full() const noexcept { return size_ >= Capacity; }
    bool empty() const noexcept { return size_ == 0; }

    T&       operator[](size_t i) noexcept { return data_[i]; }
    const T& operator[](size_t i) const noexcept { return data_[i]; }

    T*       begin() noexcept { return data_.data(); }
    T*       end() noexcept { return data_.data() + size_; }
    const T* begin() const noexcept { return data_.data(); }
    const T* end() const noexcept { return data_.data() + size_; }

private:
    std::array<T, Capacity> data_{};
    size_t                    size_ = 0;
};

} // namespace detail

// ── Request lifecycle state machine ────────────────────────────────────────────
// pending -> started -> under_execution -> finalized, forward-only and
// single-step, mirroring rcp/lifecycle.hpp's own forward-only ServerState
// progression. `canceled` is a separate terminal state reachable only from
// pending/started — already-executing requests finish, never cancel here.
// c-RCP has no equivalent state machine at all (request.c is a pure-function
// library with no request-store concept of its own); RequestState/
// RequestRecord/RequestLedger below remain this codebase's own original
// design, unchanged in shape by this pass except for the two content fixes
// (deltas #3 and #4 in the file header above).

enum class RequestState : uint8_t {
    Pending,
    Started,
    UnderExecution,
    Finalized,
    Canceled,
};

// RequestRecord is one tracked conditional/cancellation/standard request.
// `request_type` is std::nullopt for a standard request; chained-successor
// bookkeeping (`chained_predecessor`, `chained_successors`) is populated by
// the caller when it submits a chained request.
struct RequestRecord {
    uint8_t                          transaction_num = 0;
    std::optional<RequestTypeOpcode> request_type;
    RequestState                     state = RequestState::Pending;
    bool                              cs    = false;
    bool                              is_safety = false; // true for a decoded 0x8x opcode — see request_record_for()
    size_t                            arrival_seq = 0;    // assigned by RequestLedger::submit, FIFO tie-break key

    // Compound / compound-wait finalization target (unused by other kinds).
    // Carries an explicit next_state (see delta #4 in the file header) —
    // replaces the pre-pass sequencer_index/expected_start_state pair, which
    // modeled finalization as an unconditional +1 with no way to express
    // "leave the sequencer where it is" or "jump to an arbitrary state".
    std::optional<CompoundStep> compound_step;

    // Chained-request linkage (unused by other kinds).
    std::optional<uint8_t>      chained_predecessor;
    std::vector<uint8_t>        chained_successors;

    std::optional<std::error_code> outcome; // set to request_canceled once state == Canceled
};

// request_record_for builds a RequestRecord with `is_safety` derived
// automatically from `type` via is_safety_variant(). Compound/compound-wait/
// chained-specific fields (compound_step, chained_predecessor,
// chained_successors) are left at their defaults; callers that need them
// still set them directly.
inline RequestRecord request_record_for(uint8_t transaction_num, std::optional<RequestTypeOpcode> type,
                                          bool cs) noexcept {
    RequestRecord rec;
    rec.transaction_num = transaction_num;
    rec.request_type    = type;
    rec.cs               = cs;
    rec.is_safety        = type.has_value() && is_safety_variant(*type);
    return rec;
}

// RequestLedger tracks every in-flight request's lifecycle state and
// implements the cancellation and chained-abort semantics that act across
// records. It does not itself decide *when* a request is due — that is
// select_next_due's job — this class only enforces what a valid transition
// sequence and a valid cancellation outcome look like once a decision has
// been made. Backed by a fixed-capacity BoundedVector (kMaxTrackedRequests,
// see above) rather than an unbounded std::vector.
class RequestLedger {
public:
    // submit registers a new record in the Pending state. Rejects a
    // transaction_num already tracked by this ledger (finalized/canceled
    // records remain tracked, not cleared, so a client cannot silently
    // resurrect a completed transaction_num while this ledger instance
    // lives — callers that want to reuse ids across sessions should
    // construct a fresh ledger) with transaction_num_collision, and reports
    // ledger_full if this ledger has already reached kMaxTrackedRequests
    // distinct tracked transactions (checked after the collision check, so
    // a duplicate submission is never misreported as "no room").
    std::error_code submit(RequestRecord rec) noexcept {
        if (find(rec.transaction_num) != nullptr)
            return make_error_code(RequestErrc::transaction_num_collision);
        if (records_.full())
            return make_error_code(RequestErrc::ledger_full);
        rec.state       = RequestState::Pending;
        rec.arrival_seq = next_arrival_seq_++;
        records_.push_back(std::move(rec));
        return {};
    }

    std::error_code start(uint8_t txn) noexcept { return transition(txn, RequestState::Pending, RequestState::Started); }

    std::error_code begin_execution(uint8_t txn) noexcept {
        return transition(txn, RequestState::Started, RequestState::UnderExecution);
    }

    // finalize completes a request that is currently UnderExecution. For a
    // Compound/CompoundWait request whose compound_step is set, it drives
    // SequencerTable::advance_guard()+apply_next_state() for that step as
    // the finalization side effect — advance-or-not is only ever a side
    // effect here, never a precondition for finalizing. `errored` records
    // whether this request's own execution failed, which
    // propagate_chain_completion (below) needs for its chained successors'
    // cs-gated abort rule.
    std::error_code finalize(uint8_t txn, bool errored, SequencerTable* sequencers = nullptr) noexcept {
        auto ec = transition(txn, RequestState::UnderExecution, RequestState::Finalized);
        if (ec) return ec;
        auto* rec = find_mut(txn);
        if (rec != nullptr && sequencers != nullptr && rec->compound_step.has_value() &&
            rec->request_type.has_value() &&
            (is_compound(*rec->request_type) || is_compound_wait(*rec->request_type))) {
            if (sequencers->advance_guard(*rec->compound_step)) {
                sequencers->apply_next_state(*rec->compound_step);
            }
        }
        propagate_chain_completion(txn, errored);
        return {};
    }

    // cancel_single implements clear-single (0x07): cancels `txn` and every
    // transitive chained successor if `txn` is still Pending/Started;
    // returns request_not_found if `txn` was never tracked by this ledger at
    // all, or request_not_cancellable if it is tracked but has already moved
    // past cancellation (UnderExecution/Finalized/Canceled — "already-
    // executing requests finish"). These are two different c-RCP outcomes
    // (RCP_CANCEL_RESULT_NOT_FOUND vs RCP_CANCEL_RESULT_NOT_CANCELLABLE, see
    // delta #3 in the file header) collapsed into one before this pass.
    std::error_code cancel_single(uint8_t txn) noexcept {
        auto* rec = find_mut(txn);
        if (rec == nullptr) return make_error_code(RequestErrc::request_not_found);
        if (rec->state == RequestState::UnderExecution || rec->state == RequestState::Finalized ||
            rec->state == RequestState::Canceled)
            return make_error_code(RequestErrc::request_not_cancellable);
        cascade_cancel(txn);
        return {};
    }

    // cancel_all implements clear-all (0x05, non_safestate_only=false, the
    // mandatory baseline) and clear-non-safestate (0x06, non_safestate_only
    // =true, part of the compound bundle): cancels every currently
    // Pending/Started record (and, for each, its chained successors),
    // optionally skipping records flagged is_safety. Returns the count of
    // top-level records this call transitioned to Canceled.
    size_t cancel_all(bool non_safestate_only) noexcept {
        size_t count = 0;
        for (auto& rec : records_) {
            if ((rec.state != RequestState::Pending && rec.state != RequestState::Started)) continue;
            if (non_safestate_only && rec.is_safety) continue;
            cascade_cancel(rec.transaction_num);
            ++count;
        }
        return count;
    }

    const RequestRecord* find(uint8_t txn) const noexcept {
        for (const auto& rec : records_)
            if (rec.transaction_num == txn) return &rec;
        return nullptr;
    }

    std::error_code outcome_of(uint8_t txn, std::error_code& out) const noexcept {
        const auto* rec = find(txn);
        if (rec == nullptr) return make_error_code(RequestErrc::unknown_transaction);
        out = rec->outcome.value_or(std::error_code{});
        return {};
    }

    size_t size() const noexcept { return records_.size(); }
    static constexpr size_t capacity() noexcept { return kMaxTrackedRequests; }

private:
    RequestRecord* find_mut(uint8_t txn) noexcept {
        for (auto& rec : records_)
            if (rec.transaction_num == txn) return &rec;
        return nullptr;
    }

    std::error_code transition(uint8_t txn, RequestState from, RequestState to) noexcept {
        auto* rec = find_mut(txn);
        if (rec == nullptr) return make_error_code(RequestErrc::unknown_transaction);
        if (rec->state != from) return make_error_code(RequestErrc::invalid_lifecycle_transition);
        rec->state = to;
        return {};
    }

    // cascade_cancel marks `txn` Canceled (idempotent, and a no-op for a
    // record that has moved past cancellation) and recurses into its
    // chained_successors.
    void cascade_cancel(uint8_t txn) noexcept {
        auto* rec = find_mut(txn);
        if (rec == nullptr) return;
        if (rec->state == RequestState::UnderExecution || rec->state == RequestState::Finalized ||
            rec->state == RequestState::Canceled)
            return;
        rec->state   = RequestState::Canceled;
        rec->outcome = make_error_code(RequestErrc::request_canceled);
        for (uint8_t succ : rec->chained_successors) cascade_cancel(succ);
    }

    // propagate_chain_completion is finalize()'s hook into the chained `cs`
    // rule (should_execute_chained above): every record whose
    // chained_predecessor is `predecessor_txn` is aborted (cascade_cancel)
    // iff its own cs does not permit executing after a predecessor error.
    void propagate_chain_completion(uint8_t predecessor_txn, bool predecessor_errored) noexcept {
        // Collect matching successors first: cascade_cancel can mutate
        // records_ only in-place (no insertion/removal), but iterating and
        // mutating by id first is simpler to reason about.
        std::array<uint8_t, kMaxTrackedRequests> successors{};
        size_t successor_count = 0;
        for (const auto& rec : records_) {
            if (rec.chained_predecessor.has_value() && *rec.chained_predecessor == predecessor_txn &&
                successor_count < successors.size()) {
                successors[successor_count++] = rec.transaction_num;
            }
        }
        for (size_t i = 0; i < successor_count; ++i) {
            const auto* rec = find(successors[i]);
            if (rec != nullptr && !should_execute_chained(rec->cs, predecessor_errored))
                cascade_cancel(successors[i]);
        }
    }

    detail::BoundedVector<RequestRecord, kMaxTrackedRequests> records_;
    size_t                                                       next_arrival_seq_ = 0;
};

} // namespace request
} // namespace rcp

// Enable std::error_code construction from rcp::request::RequestErrc.
namespace std {
template <>
struct is_error_code_enum<rcp::request::RequestErrc> : true_type {};
} // namespace std
