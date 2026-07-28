// fusa:req REQ-SEQ-001
// fusa:req REQ-SEQ-002
// fusa:req REQ-SEQ-003
// fusa:req REQ-SEQ-004
// fusa:req REQ-SEQ-005
// fusa:req REQ-SEQ-006
// fusa:req REQ-SEQ-007
// fusa:req REQ-SEQ-008
// fusa:req REQ-SEQ-009

// Conditional-request taxonomy and sequencer-state primitives — the
// message_timestamp-repurposing decode, the five conditional request kinds
// (plus the three cancellation kinds) and their optional-feature bundling,
// sequencer-state advance/reset, cancellation semantics, and the request
// lifecycle state machine an OPEN Alliance TC18 Remote Control Protocol
// Specification v0.5.1_RC server needs once it goes beyond the mandatory
// "standard" request kind (extraction §2.4, §2.7, §3.1, §3.11, §3.14).
//
// ROADMAP.md milestone 49, "Conditional-Request Taxonomy & Sequencers
// (v2.5.0)": this header rides on top of rcp/wire.hpp's existing ACF_GBB
// support (v2.0.0) and rcp/regmap.hpp's existing `sequencer_states` storage
// (v2.1.0) without changing either — the mtv=0 repurposing trick is a new
// decode path over already-implemented fields, and sequencer *behavior*
// (default value, advance-on-finalization) is built on top of regmap.hpp's
// already-allocated storage rather than duplicating it. It also consumes
// rcp/spi.hpp's compound_wait_matches and rcp/i2c.hpp's
// compound_wait_matches_bits as the per-endpoint-type condition-comparison
// rules a real compound-wait dispatch loop would call once it decides a
// compound-wait request is due; this header does not re-implement either.
//
// Field names and behavior below implement TC18's *behavior* as described in
// an internal structured extraction of the specification named above; no
// text from that document is reproduced here. The concrete opcode-to-byte
// mapping, the param-byte layout within the repurposed timestamp slot, and
// the request-ledger data model chosen in this file are this
// implementation's own encoding of that behavior, same as the equivalent
// disclaimers in rcp/wire.hpp, rcp/regmap.hpp, rcp/endpoint.hpp,
// rcp/spi.hpp, and rcp/i2c.hpp. This header models the taxonomy, the
// bundling rules, and the state machine's transitions and effects — it does
// not implement a running scheduler thread; wiring select_next_due()'s
// output into an actual dispatch loop is left to the embedding application,
// same as every other endpoint header in this codebase.
#pragma once

#include <rcp/regmap.hpp>
#include <rcp/wire.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace rcp {
namespace sequencer {

// ── Errors ────────────────────────────────────────────────────────────────────

enum class SequencerErrc : int {
    timestamp_not_repurposed     = 1, // mtv=1: message_timestamp is a real timestamp, not a request_type opcode
    unknown_request_type         = 2, // byte 0 of the repurposed slot is not one of the defined opcodes
    index_out_of_range           = 3, // SequencerTable index >= table size
    unknown_transaction          = 4, // RequestLedger lookup by transaction_num failed
    invalid_lifecycle_transition = 5, // requested state transition is not the single next step
    transaction_num_collision    = 6, // submit() with a transaction_num already tracked
    request_not_found            = 7, // REQUEST_NOT_FOUND — cancel_single target unknown or past cancellation
    request_canceled             = 8, // REQUEST_CANCELED — recorded outcome of a canceled request
    compound_bundle_incomplete   = 9, // compound/compound-wait claimed without every required companion capability
};

inline const std::error_category& sequencer_category() noexcept {
    struct Cat : std::error_category {
        const char* name() const noexcept override { return "rcp.sequencer"; }
        std::string message(int ev) const override {
            switch (static_cast<SequencerErrc>(ev)) {
            case SequencerErrc::timestamp_not_repurposed:
                return "rcp/sequencer: mtv=1 — message_timestamp is not a repurposed request_type slot";
            case SequencerErrc::unknown_request_type:
                return "rcp/sequencer: unrecognized request_type opcode";
            case SequencerErrc::index_out_of_range:
                return "rcp/sequencer: sequencer index out of range";
            case SequencerErrc::unknown_transaction:
                return "rcp/sequencer: transaction_num is not tracked by this ledger";
            case SequencerErrc::invalid_lifecycle_transition:
                return "rcp/sequencer: requested lifecycle transition is not the next state in sequence";
            case SequencerErrc::transaction_num_collision:
                return "rcp/sequencer: transaction_num is already tracked by this ledger";
            case SequencerErrc::request_not_found:
                return "rcp/sequencer: REQUEST_NOT_FOUND";
            case SequencerErrc::request_canceled:
                return "rcp/sequencer: REQUEST_CANCELED";
            case SequencerErrc::compound_bundle_incomplete:
                return "rcp/sequencer: compound support requires compound-wait, clear-non-safestate, "
                       "and >=4 sequencers together, not compound alone";
            default:
                return "rcp/sequencer: unknown error";
            }
        }
    };
    static Cat instance;
    return instance;
}

inline std::error_code make_error_code(SequencerErrc e) noexcept {
    return {static_cast<int>(e), sequencer_category()};
}

// ── request_type opcode — the message_timestamp-repurposing trick ────────────
// When an ACF_GBB message's `mtv` bit is clear, the 64-bit message_timestamp
// slot it would otherwise carry (and which ACF_GBB always reserves space for
// regardless of `mtv`, per rcp/wire.hpp) is repurposed: its first byte
// becomes this opcode, and the remaining 7 bytes carry opcode-specific
// parameters (extraction §2.7). The eight opcodes below are every kind this
// milestone defines: five conditional request kinds plus three cancellation
// kinds. The mandatory "standard" request kind (rcp::wire::RequestKind) has
// no opcode here at all — it is always carried as ACF_ABB, which has no
// message_timestamp slot to repurpose in the first place.

enum class RequestTypeOpcode : uint8_t {
    Chained           = 0x01,
    ClearAll          = 0x05,
    ClearNonSafestate = 0x06,
    ClearSingle       = 0x07,
    Timed             = 0x0A,
    CompoundWait      = 0x0B,
    Triggered         = 0x0E,
    Compound          = 0x0F,
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
        return true;
    default:
        return false;
    }
}

// encode_request_type packs `type` into the top byte and `params` into the
// remaining 7 bytes of a 64-bit value, most-significant byte first — the
// same big-endian convention rcp/wire.hpp's put_u64 uses elsewhere in this
// codebase, kept consistent here even though this file does not call that
// (internal, unexported) helper directly.
constexpr uint64_t encode_request_type(RequestTypeOpcode type, const std::array<uint8_t, 7>& params) noexcept {
    uint64_t v = static_cast<uint64_t>(static_cast<uint8_t>(type)) << 56;
    for (size_t i = 0; i < 7; ++i)
        v |= static_cast<uint64_t>(params[i]) << (48 - 8 * i);
    return v;
}

// decode_request_type is the inverse of encode_request_type, gated on `mtv`
// being clear — a caller that has not first checked AcfMessageInfo::mtv (or
// that is decoding a genuinely timestamped ACF_GBB message) gets a decode
// failure rather than silently misreading a real timestamp's high byte as an
// opcode, matching this codebase's established preference (see
// rcp/discovery.hpp's NTSCF-only enforcement) for modeling a precondition
// violation as a returned error rather than an implicit caller obligation.
inline std::error_code decode_request_type(bool mtv, uint64_t message_timestamp,
                                            RequestTypeOpcode& out_type,
                                            std::array<uint8_t, 7>& out_params) noexcept {
    if (mtv) return make_error_code(SequencerErrc::timestamp_not_repurposed);
    const uint8_t byte0 = static_cast<uint8_t>((message_timestamp >> 56) & 0xFF);
    if (!is_valid_request_type(byte0)) return make_error_code(SequencerErrc::unknown_request_type);
    out_type = static_cast<RequestTypeOpcode>(byte0);
    for (size_t i = 0; i < 7; ++i)
        out_params[i] = static_cast<uint8_t>((message_timestamp >> (48 - 8 * i)) & 0xFF);
    return {};
}

// make_conditional_request builds the AcfMessageInfo header for a
// conditional/cancellation request: always ACF_GBB, always mtv=false (the
// repurposing trick above requires it), with `cs` set per the caller's
// request-kind-specific meaning (see the `cs` section below). Callers pass
// the result to rcp::wire::encode_acf_gbb alongside encode_request_type's
// output as the message_timestamp argument — no new wire encoding is
// introduced here, only this semantic layer over the existing one.
inline wire::AcfMessageInfo make_conditional_request(wire::ByteBusId bus_id, uint8_t transaction_num,
                                                       bool cs) noexcept {
    wire::AcfMessageInfo info;
    info.acf_msg_type    = wire::kAcfMsgTypeGbb;
    info.mtv              = false;
    info.byte_bus_id      = bus_id;
    info.transaction_num  = transaction_num;
    info.cs                = cs;
    return info;
}

// ── Request categories & execution-priority ordering ──────────────────────────
// The seven categories simultaneously-due requests are ordered across
// (extraction §3.14). Numeric values double as priority rank — lower value
// wins — so priority_rank below is a trivial cast rather than a lookup
// table that could drift out of sync with declaration order.

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
// `std::nullopt` denotes the mandatory standard request kind (no opcode, per
// the header comment above).
constexpr RequestCategory category_of(std::optional<RequestTypeOpcode> kind) noexcept {
    if (!kind.has_value()) return RequestCategory::Standard;
    switch (*kind) {
    case RequestTypeOpcode::ClearAll:
    case RequestTypeOpcode::ClearNonSafestate:
    case RequestTypeOpcode::ClearSingle:
        return RequestCategory::Cancellation;
    case RequestTypeOpcode::Triggered:    return RequestCategory::Triggered;
    case RequestTypeOpcode::Timed:        return RequestCategory::Timed;
    case RequestTypeOpcode::Compound:     return RequestCategory::Compound;
    case RequestTypeOpcode::CompoundWait: return RequestCategory::CompoundWait;
    case RequestTypeOpcode::Chained:      return RequestCategory::Chained;
    default:                              return RequestCategory::Standard; // unreachable given a validated opcode
    }
}

constexpr uint8_t priority_rank(RequestCategory c) noexcept { return static_cast<uint8_t>(c); }

// DueCandidate is the minimal shape select_next_due needs: which category a
// due request belongs to, and its arrival order for the FIFO tie-break. It
// deliberately does not reference RequestRecord below, so the priority
// algorithm stays testable independent of the ledger that owns real
// records.
struct DueCandidate {
    RequestCategory category    = RequestCategory::Standard;
    size_t          arrival_seq = 0;
};

// select_next_due picks the winning index out of `due` — every entry is
// assumed already due, this function only orders them — by lowest
// priority_rank, breaking ties by lowest arrival_seq (extraction §3.14:
// "ties resolved FIFO"). Returns std::nullopt for an empty input rather than
// an out-of-range index.
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

// ── The `cs` field's two request-kind-specific meanings ───────────────────────
// AcfMessageInfo::cs (rcp/wire.hpp) is a single wire bit whose meaning
// depends entirely on which conditional request kind it rides on (extraction
// §2.4): for compound-wait it selects when the wait condition is first
// checked; for chained it selects whether a successor cares about its
// predecessor's outcome at all.

enum class CompoundWaitCheck : uint8_t {
    Immediate       = 0, // evaluate the condition against current status right away
    AfterChangeOnly = 1, // only evaluate once the monitored value changes from its current one
};

constexpr CompoundWaitCheck compound_wait_check_of(bool cs) noexcept {
    return cs ? CompoundWaitCheck::Immediate : CompoundWaitCheck::AfterChangeOnly;
}

// should_execute_chained answers, for one chained successor: given its own
// `cs` bit and whether its predecessor finished in error, should this
// successor still run? cs=true means "execute regardless"; cs=false means
// "abort if the predecessor errored". A predecessor that did *not* error
// always permits the successor to run, independent of cs.
constexpr bool should_execute_chained(bool cs, bool predecessor_errored) noexcept {
    return cs || !predecessor_errored;
}

// ── Optional-feature bundling ──────────────────────────────────────────────────
// The roadmap is explicit that compound support cannot be claimed piecemeal:
// a repo implementing compound (0x0F) must also implement compound-wait
// (0x0B), clear-non-safestate cancellation (0x06), and at least 4 sequencer
// slots — all four together, or none of them advertised as supported.
// Triggered, chained, and timed are each independently flaggable; clear-
// single is its own "enhanced cancellation" capability with no further
// dependency modeled here (extraction §3.1, §2.7).

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

// validate_feature_bundles enforces the compound/compound-wait bundle rule
// above. It takes no position on triggered/chained/timed/clear_single, which
// this milestone's scope leaves independently flaggable.
inline std::error_code validate_feature_bundles(const FeatureSet& f) noexcept {
    const bool claims_compound_support = f.compound || f.compound_wait;
    if (!claims_compound_support) return {};
    const bool complete = f.compound && f.compound_wait && f.clear_non_safestate &&
                           f.sequencer_count >= kMinCompoundSequencers;
    return complete ? std::error_code{} : make_error_code(SequencerErrc::compound_bundle_incomplete);
}

// implemented_options_bits reports the svr_implemented_options bit(s)
// (rcp/regmap.hpp) a register map should advertise for a validated feature
// set. Returns 0 for a feature set with nothing conditional enabled at all —
// callers should not report kOptConditionalRequests unless at least one of
// the five conditional kinds is actually implemented.
inline uint32_t implemented_options_bits(const FeatureSet& f) noexcept {
    const bool any_conditional = f.compound || f.compound_wait || f.triggered || f.chained || f.timed;
    return any_conditional ? regmap::kOptConditionalRequests : 0;
}

// ── Sequencer-state registers ──────────────────────────────────────────────────
// SequencerTable is the behavior layer over rcp/regmap.hpp's already-
// allocated `RegisterMap::sequencer_states` storage (extraction §3.11,
// §3.14): every sequencer defaults to kDefaultState (not the vector's own
// zero-initialization default), and a sequencer only advances when a
// compound request finalizes while that sequencer is still holding its
// expected start value — a mismatch leaves the sequencer untouched rather
// than erroring, since "the sequencer moved on already" is an ordinary
// outcome of concurrent compound requests racing, not a fault.
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
        if (index >= states_.size()) return make_error_code(SequencerErrc::index_out_of_range);
        out = states_[index];
        return {};
    }

    // try_advance implements the compound-request finalization rule
    // directly: the sequencer at `index` advances by one (wrapping at the
    // 8-bit storage width, matching the "persistent 8-bit state" the roadmap
    // specifies) iff its current value equals `expected_start`; otherwise it
    // is left untouched. `out_advanced` reports which happened; the returned
    // std::error_code is reserved for the index-out-of-range case only, so
    // callers cannot mistake "didn't advance because the expected state did
    // not match" for a failure of the call itself.
    std::error_code try_advance(size_t index, regmap::SequencerState expected_start,
                                 bool& out_advanced) noexcept {
        if (index >= states_.size()) return make_error_code(SequencerErrc::index_out_of_range);
        if (states_[index] == expected_start) {
            states_[index] = static_cast<regmap::SequencerState>(states_[index] + 1);
            out_advanced = true;
        } else {
            out_advanced = false;
        }
        return {};
    }

    void reset_all_to_default() noexcept { std::fill(states_.begin(), states_.end(), kDefaultState); }

private:
    std::vector<regmap::SequencerState>& states_;
};

// ── Request lifecycle state machine ────────────────────────────────────────────
// pending -> started -> under_execution -> finalized, forward-only and
// single-step (extraction §3.14), mirroring rcp/lifecycle.hpp's own
// forward-only ServerState progression. `canceled` is a separate terminal
// state reachable only from pending/started — extraction §2.7's rule that
// "already-executing requests finish" means under_execution and finalized
// requests are never canceled by this ledger, only left to complete.

enum class RequestState : uint8_t {
    Pending,
    Started,
    UnderExecution,
    Finalized,
    Canceled,
};

// RequestRecord is one tracked conditional/cancellation/standard request.
// `request_type` is std::nullopt for a standard request (see category_of
// above); chained-successor bookkeeping (`chained_predecessor`,
// `chained_successors`) is populated by the caller when it submits a chained
// request, since only the caller knows the wire-level identifier a
// predecessor/successor pair share.
struct RequestRecord {
    uint8_t                          transaction_num = 0;
    std::optional<RequestTypeOpcode> request_type;
    RequestState                     state = RequestState::Pending;
    bool                              cs    = false;
    bool                              is_safety = false; // always false until v2.6.0's 0x8x safety variants exist
    size_t                            arrival_seq = 0;    // assigned by RequestLedger::submit, FIFO tie-break key

    // Compound / compound-wait finalization target (unused by other kinds).
    std::optional<size_t>       sequencer_index;
    regmap::SequencerState      expected_start_state = SequencerTable::kDefaultState;

    // Chained-request linkage (unused by other kinds).
    std::optional<uint8_t>      chained_predecessor;
    std::vector<uint8_t>        chained_successors;

    std::optional<std::error_code> outcome; // set to request_canceled once state == Canceled
};

// RequestLedger tracks every in-flight request's lifecycle state and
// implements the cancellation and chained-abort semantics that act across
// records (extraction §2.7, §3.14). It does not itself decide *when* a
// request is due — that is select_next_due's job, fed by the embedding
// application's own timing/trigger/compound-wait-condition evaluation — this
// class only enforces what a valid transition sequence and a valid
// cancellation outcome look like once a decision has been made.
class RequestLedger {
public:
    // submit registers a new record in the Pending state. Rejects a
    // transaction_num already tracked by this ledger (finalized/canceled
    // records remain tracked, not cleared, so a client cannot silently
    // resurrect a completed transaction_num while this ledger instance
    // lives — callers that want to reuse ids across sessions should
    // construct a fresh ledger).
    std::error_code submit(RequestRecord rec) noexcept {
        if (find(rec.transaction_num) != nullptr)
            return make_error_code(SequencerErrc::transaction_num_collision);
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
    // Compound/CompoundWait request whose sequencer_index is set, it drives
    // SequencerTable::try_advance for that sequencer as the finalization
    // side effect the roadmap calls out — advance-or-not is only ever a
    // side effect here, never a precondition for finalizing. `errored`
    // records whether this request's own execution failed, which
    // propagate_chain_completion (below) needs to evaluate its chained
    // successors' cs-gated abort rule.
    std::error_code finalize(uint8_t txn, bool errored, SequencerTable* sequencers = nullptr) noexcept {
        auto ec = transition(txn, RequestState::UnderExecution, RequestState::Finalized);
        if (ec) return ec;
        auto* rec = find_mut(txn);
        if (rec != nullptr && sequencers != nullptr && rec->sequencer_index.has_value() &&
            (rec->request_type == RequestTypeOpcode::Compound ||
             rec->request_type == RequestTypeOpcode::CompoundWait)) {
            bool advanced = false;
            sequencers->try_advance(*rec->sequencer_index, rec->expected_start_state, advanced);
        }
        propagate_chain_completion(txn, errored);
        return {};
    }

    // cancel_single implements clear-single (0x07): cancels `txn` and every
    // transitive chained successor if `txn` is still Pending/Started;
    // returns REQUEST_NOT_FOUND if `txn` is untracked, already Canceled, or
    // has moved past cancellation (UnderExecution/Finalized — "already-
    // executing requests finish", extraction §2.7).
    std::error_code cancel_single(uint8_t txn) noexcept {
        auto* rec = find_mut(txn);
        if (rec == nullptr) return make_error_code(SequencerErrc::request_not_found);
        if (rec->state == RequestState::UnderExecution || rec->state == RequestState::Finalized ||
            rec->state == RequestState::Canceled)
            return make_error_code(SequencerErrc::request_not_found);
        cascade_cancel(txn);
        return {};
    }

    // cancel_all implements clear-all (0x05, non_safestate_only=false, the
    // mandatory baseline) and clear-non-safestate (0x06, non_safestate_only
    // =true, part of the compound bundle): cancels every currently
    // Pending/Started record (and, for each, its chained successors),
    // optionally skipping records flagged is_safety. Returns the count of
    // top-level records this call transitioned to Canceled (successors
    // cascaded from them are not counted separately).
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
        if (rec == nullptr) return make_error_code(SequencerErrc::unknown_transaction);
        out = rec->outcome.value_or(std::error_code{});
        return {};
    }

    size_t size() const noexcept { return records_.size(); }

private:
    RequestRecord* find_mut(uint8_t txn) noexcept {
        for (auto& rec : records_)
            if (rec.transaction_num == txn) return &rec;
        return nullptr;
    }

    std::error_code transition(uint8_t txn, RequestState from, RequestState to) noexcept {
        auto* rec = find_mut(txn);
        if (rec == nullptr) return make_error_code(SequencerErrc::unknown_transaction);
        if (rec->state != from) return make_error_code(SequencerErrc::invalid_lifecycle_transition);
        rec->state = to;
        return {};
    }

    // cascade_cancel marks `txn` Canceled (idempotent, and a no-op for a
    // record that has moved past cancellation) and recurses into its
    // chained_successors — extraction §2.7's "cancelling a chained request
    // cancels its successors" rule.
    void cascade_cancel(uint8_t txn) noexcept {
        auto* rec = find_mut(txn);
        if (rec == nullptr) return;
        if (rec->state == RequestState::UnderExecution || rec->state == RequestState::Finalized ||
            rec->state == RequestState::Canceled)
            return;
        rec->state   = RequestState::Canceled;
        rec->outcome = make_error_code(SequencerErrc::request_canceled);
        for (uint8_t succ : rec->chained_successors) cascade_cancel(succ);
    }

    // propagate_chain_completion is finalize()'s hook into the chained `cs`
    // rule (should_execute_chained above): every record whose
    // chained_predecessor is `predecessor_txn` is aborted (cascade_cancel)
    // iff its own cs does not permit executing after a predecessor error.
    // A predecessor that finished without error never triggers an abort
    // here, regardless of any successor's cs.
    void propagate_chain_completion(uint8_t predecessor_txn, bool predecessor_errored) noexcept {
        // Collect matching successors first: cascade_cancel can mutate
        // records_ only in-place (no insertion/removal), but iterating and
        // mutating the same vector via index is simpler to reason about
        // than iterator invalidation rules, so gather ids first.
        std::vector<uint8_t> successors;
        for (const auto& rec : records_)
            if (rec.chained_predecessor.has_value() && *rec.chained_predecessor == predecessor_txn)
                successors.push_back(rec.transaction_num);
        for (uint8_t succ : successors) {
            const auto* rec = find(succ);
            if (rec != nullptr && !should_execute_chained(rec->cs, predecessor_errored))
                cascade_cancel(succ);
        }
    }

    std::vector<RequestRecord> records_;
    size_t                      next_arrival_seq_ = 0;
};

} // namespace sequencer
} // namespace rcp

// Enable std::error_code construction from rcp::sequencer::SequencerErrc.
namespace std {
template <>
struct is_error_code_enum<rcp::sequencer::SequencerErrc> : true_type {};
} // namespace std
