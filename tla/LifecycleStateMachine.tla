---- MODULE LifecycleStateMachine ----
(*
 * Formal specification of rcp::lifecycle::ServerLifecycle (rcp/lifecycle.hpp),
 * the 3-state HW_UNCONFIGURED / HW_CONFIGURED / RCP_CONFIGURED progression an
 * OPEN Alliance TC18 Remote Control Protocol Specification v0.5.1_RC server
 * advances through as it gets configured, plus the plausibility checks and
 * register-locking behavior tied to that progression.
 *
 * Ported from c-RCP's tla/LifecycleStateMachine.tla (Phase 7, cpp-RCP issue
 * #129) against ServerLifecycle::transition()/check_hw_cfg()/check_rcp_cfg()
 * — confirmed, by direct comparison against c-RCP's rcp_lifecycle_transition()
 * (src/lifecycle.c) during this port, to be a field-for-field match: same
 * from/target state pairs, same plausibility guards, same writer-
 * authorization structure. This spec deliberately abstracts writer
 * authorization and idleness-gating out of scope, same as the c-RCP original
 * it is ported from — every promote/demote action below is modelled as
 * enabled whenever its plausibility guard passes, without a `writer`/
 * `all_other_eps_idle` input, since neither this spec's safety properties
 * (state/field-lock shape) nor its liveness property depend on who initiated
 * a transition or whether other endpoints were idle at the time. See "Known
 * abstraction gap" below for what this simplification leaves out of scope,
 * and rcp/lifecycle.hpp's own ServerLifecycle::transition() doc comment for
 * the full writer/idle-gated topology this spec does not model in full.
 *
 * ServerLifecycle also exposes a second, simpler entry point — advance()/
 * deconfigure() — predating transition() and kept alongside it for its own
 * existing callers (see ServerLifecycle's class doc comment, "Why advance()
 * and transition() both exist"). advance()'s forward steps and
 * transition()'s guarded promotions enforce the identical hw_cfg/rcp_cfg
 * plausibility gates on the identical state pairs, so PromoteToHwConfigured/
 * PromoteToRcpConfigured below model both entry points at once; this spec
 * does not separately model advance()'s "repeat of the current state is
 * invalid_transition" rule, since it manifests no differently, from this
 * spec's own state/field-lock/liveness properties' point of view, than
 * transition()'s "repeat of the current state is a no-op" rule — both leave
 * every variable this spec tracks unchanged.
 *
 * Known abstraction gap (present already in c-RCP's own spec, not introduced
 * by this port): transition() additionally supports a partial demotion,
 * RcpConfigured -> HwConfigured (guarded by a narrower writer authorization
 * plus idleness, TC18 Figure 17's own explicit arrow) — a transition neither
 * this spec nor c-RCP's original models at all. field_writable()'s locking
 * rules (FieldKind::FunctionalWStar in particular) are, in the real
 * implementation, a PURE FUNCTION of the current ServerState — "permanently
 * locked once RcpConfigured is reached" literally means "locked while
 * state = RcpConfigured", re-evaluated on every query, not a persistent
 * latch bit — so demoting via that unmodelled transition would, in the real
 * implementation, make a FunctionalWStar field writable again immediately,
 * without going all the way back through HwUnconfigured. `field_lock` below
 * instead models locking as a separate, sticky variable that only clears via
 * FullReset (mirroring c-RCP's own choice), which is faithful to the real
 * implementation only as long as this unmodelled demotion is never taken —
 * true within this spec's own Next relation (it is not one of the actions
 * below), but not a claim about ServerLifecycle::transition()'s full
 * behavior. Left as-is, matching c-RCP's own spec's scope, rather than
 * silently narrowing what SP2 below actually establishes; re-deriving this
 * spec to add that demotion and re-model field_lock as the real state-
 * function it is would be a distinct formal-modeling task, not a port.
 *
 * Safety property (SP1): the server can never reach RcpConfigured
 * directly from HwUnconfigured -- every upward transition passes through
 * HwConfigured.
 * Safety property (SP2): once a field class is locked while the server
 * is RcpConfigured, it never becomes unlocked again while the server
 * remains RcpConfigured (a lock can only be cleared by demoting all the
 * way back to HwUnconfigured -- a full reset).
 *
 * Liveness property (LP1): given HW/RCP configuration inputs that eventually
 * settle and stay consistent, the server eventually reaches RcpConfigured
 * under fair scheduling -- i.e. this is not merely a state the server *can*
 * reach (SP1/SP2 already establish what happens if it does), it is a state
 * fair scheduling *guarantees* it reaches. TLC's default no-successor-state
 * deadlock check passes (every state in the model has at least one enabled
 * successor). LP1 is a related but strictly stronger claim: progress/
 * livelock-freedom under fairness, not mere deadlock-freedom. The fairness
 * conditions below were re-derived and confirmed against real TLC runs
 * during this port (WF vs. SF, both re-run), matching c-RCP's own original
 * derivation exactly -- the fairness-minimality result carries over
 * unchanged because the underlying action structure is unchanged.
 *)

EXTENDS TLC

HwUnconfigured  == "HwUnconfigured"
HwConfigured    == "HwConfigured"
RcpConfigured   == "RcpConfigured"

LifecycleStates == {HwUnconfigured, HwConfigured, RcpConfigured}

Unlocked == "Unlocked"
Locked   == "Locked"
LockStates == {Unlocked, Locked}

VARIABLES state,              \* current lifecycle state (ServerLifecycle::state_)
          hw_cfg_consistent,  \* check_hw_cfg()'s current verdict
          rcp_cfg_consistent, \* check_rcp_cfg()'s current verdict
          field_lock          \* FieldKind::FunctionalWStar field lock state (see "Known
                               \* abstraction gap" above)

vars == <<state, hw_cfg_consistent, rcp_cfg_consistent, field_lock>>

TypeOK ==
    /\ state              \in LifecycleStates
    /\ hw_cfg_consistent   \in BOOLEAN
    /\ rcp_cfg_consistent  \in BOOLEAN
    /\ field_lock          \in LockStates

Init ==
    /\ state              = HwUnconfigured
    /\ hw_cfg_consistent   \in BOOLEAN
    /\ rcp_cfg_consistent  \in BOOLEAN
    /\ field_lock          = Unlocked

(* The PlausibilitySnapshot check_hw_cfg()/check_rcp_cfg() are evaluated
 * against may change between transition attempts -- their verdicts are
 * re-evaluated each time advance()/transition() runs, not cached once and
 * for all. *)
ReviseHwConsistency ==
    /\ hw_cfg_consistent' \in BOOLEAN
    /\ UNCHANGED <<state, rcp_cfg_consistent, field_lock>>

ReviseRcpConsistency ==
    /\ rcp_cfg_consistent' \in BOOLEAN
    /\ UNCHANGED <<state, hw_cfg_consistent, field_lock>>

(* HW_UNCONFIGURED -> HW_CONFIGURED, gated by check_hw_cfg(). *)
PromoteToHwConfigured ==
    /\ state = HwUnconfigured
    /\ hw_cfg_consistent
    /\ state' = HwConfigured
    /\ UNCHANGED <<hw_cfg_consistent, rcp_cfg_consistent, field_lock>>

(* HW_CONFIGURED -> RCP_CONFIGURED, gated by check_rcp_cfg(). HwGeneric
 * fields become read-only and FunctionalWStar fields become permanently
 * locked (see field_writable()) for the remainder of this configured
 * session on this same transition. *)
PromoteToRcpConfigured ==
    /\ state = HwConfigured
    /\ rcp_cfg_consistent
    /\ state' = RcpConfigured
    /\ field_lock' = Locked
    /\ UNCHANGED <<hw_cfg_consistent, rcp_cfg_consistent>>

(* HW_CONFIGURED -> HW_UNCONFIGURED demotion, modelled unconditional (writer
 * authorization/idleness abstracted out of scope; see this spec's own
 * top-of-file note). *)
DemoteToHwUnconfigured ==
    /\ state = HwConfigured
    /\ state' = HwUnconfigured
    /\ UNCHANGED <<hw_cfg_consistent, rcp_cfg_consistent, field_lock>>

(* RCP_CONFIGURED -> HW_UNCONFIGURED full-reset demotion, modelled
 * unconditional (writer authorization/idleness abstracted out of scope) and
 * the only way this spec clears a field lock -- a full reset re-opens
 * hardware configuration from scratch, so whatever FunctionalWStar fields
 * were locked for the just-ended configured session no longer apply to the
 * next one. This is a modelling assumption, not a literal reading of any
 * single rcp/lifecycle.hpp doc comment; see this spec's own "Known
 * abstraction gap" note above. *)
FullReset ==
    /\ state = RcpConfigured
    /\ state' = HwUnconfigured
    /\ field_lock' = Unlocked
    /\ UNCHANGED <<hw_cfg_consistent, rcp_cfg_consistent>>

Next ==
    \/ ReviseHwConsistency
    \/ ReviseRcpConsistency
    \/ PromoteToHwConfigured
    \/ PromoteToRcpConfigured
    \/ DemoteToHwUnconfigured
    \/ FullReset

Spec == Init /\ [][Next]_<<state, hw_cfg_consistent, rcp_cfg_consistent, field_lock>>

(* FairSpec adds the minimum fairness each promote action genuinely needs to
 * guarantee LP1, re-confirmed against real TLC runs during this port:
 *
 * - WF_vars(PromoteToHwConfigured): weak fairness suffices. Once
 *   hw_cfg_consistent holds continuously and state = HwUnconfigured holds
 *   continuously, nothing else can disable PromoteToHwConfigured before it
 *   fires -- no other action changes state away from HwUnconfigured. TLC
 *   confirms a WF-only variant already gets the server past HwUnconfigured
 *   on its own.
 *
 * - SF_vars(PromoteToRcpConfigured): strong fairness is required, and WF is
 *   provably insufficient here. DemoteToHwUnconfigured is unconditionally
 *   enabled at state = HwConfigured and can race PromoteToRcpConfigured back
 *   to HwUnconfigured every time before it fires, so PromoteToRcpConfigured
 *   is never *continuously* enabled -- only *infinitely often* enabled --
 *   which WF does not act on but SF does. TLC confirms a WF-only variant on
 *   this action finds a concrete Promote/Demote lasso counterexample that
 *   never leaves {HwUnconfigured, HwConfigured}.
 *)
FairSpec == Spec
    /\ WF_vars(PromoteToHwConfigured)
    /\ SF_vars(PromoteToRcpConfigured)

(* LP1's antecedent: check_hw_cfg()/check_rcp_cfg()'s inputs eventually
 * settle and stay consistent (become permanently TRUE) -- without this,
 * ReviseHwConsistency/ReviseRcpConsistency (deliberately left unfair,
 * matching their role as an unconstrained environment input) could keep an
 * input flapping forever and no fairness on the promote actions could
 * compensate, since neither promote action is ever enabled while its
 * gating input is FALSE. *)
InputsEventuallyConsistent == <>[](hw_cfg_consistent /\ rcp_cfg_consistent)

(* LP1: given eventually-consistent inputs and fair scheduling of the two
 * promote actions (at the minimum fairness level each genuinely needs,
 * above), the server eventually reaches RcpConfigured. *)
EventuallyRcpConfigured == InputsEventuallyConsistent => <>(state = RcpConfigured)

(* SP1: No skip-configuration transition -- RcpConfigured is only ever
 * reached from HwConfigured, never directly from HwUnconfigured. *)
NoSkipConfiguration ==
    [][state = HwUnconfigured => state' # RcpConfigured]_<<state>>

(* SP2: A field lock, once set while RcpConfigured, is never cleared except
 * by the full-reset transition back to HwUnconfigured -- i.e. it never
 * silently reverts to Unlocked while the server remains RcpConfigured
 * (subject to this spec's own "Known abstraction gap" note above). *)
FieldLockMonotonicWhileConfigured ==
    [][ (state = RcpConfigured /\ field_lock = Locked /\ state' = RcpConfigured)
            => field_lock' = Locked ]_<<state, field_lock>>

THEOREM Spec => TypeOK /\ NoSkipConfiguration /\ FieldLockMonotonicWhileConfigured
THEOREM FairSpec => EventuallyRcpConfigured

====
