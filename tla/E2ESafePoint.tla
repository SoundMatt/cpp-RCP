---- MODULE E2ESafePoint ----
(*
 * Formal specification of the per-request-stream watchdog and safety-tagged-
 * request execution gate rcp::e2e.hpp provides (rcp::e2e::RxWatchdog,
 * apply_watchdog_overflow(), endpoint_in_configured_safe_state(),
 * may_execute_now()).
 *
 * Ported from c-RCP's tla/E2ESafePoint.tla (Phase 7, cpp-RCP issue #129)
 * against these same primitives, confirmed by direct comparison against
 * c-RCP's rcp_e2e_wd_evaluate()/rcp_e2e_watchdog_purge_should_keep()/
 * rcp_e2e_endpoint_in_safe_state() (src/e2e.c) during this port to be
 * behaviorally identical for the two properties this spec establishes: a
 * watchdog-overflow purge never discards a pending safety-tagged request,
 * and a safety-tagged request only ever executes once its endpoint has
 * reached its configured safe state. rcp/e2e.hpp additionally exposes
 * apply_queue_overflow() (a request-queue-overrun trigger, distinct from
 * watchdog expiry, gated on rx_ovrflw_safestate_enable) that this spec does
 * not separately model, matching c-RCP's own spec's scope -- both triggers
 * share the identical purge-normal/retain-safety consequence Miss(s) below
 * already captures once its own safe-state-enable input is set, so a second,
 * differently-named copy of the identical action would add no new coverage.
 *
 * This models one request stream's rx_wd_enable/rx_wd_timeout_interval/
 * rx_wd_safestate_enable watchdog (RxWatchdog::overflowed(),
 * apply_watchdog_overflow()), its pending-request queue split into a
 * safety-tagged and a normal component (request::RequestRecord::is_safety,
 * request::RequestLedger::cancel_all(non_safestate_only)), and the
 * safety-tagged execution admission rule (may_execute_now()) against a
 * polled endpoint_in_safe_state measurement
 * (endpoint_in_configured_safe_state()). CRC32 well-formedness itself
 * (wrap()/unwrap()) is a pure per-frame computation with no interesting
 * state-transition behavior to model checking, and is covered by
 * tests/test_e2e.cpp instead -- same scope split as the c-RCP original.
 * The watchdog's own timeout-vs-latch mechanics (kick()/overflowed()'s own
 * elapsed-time behavior) are separately covered by tla/WatchdogSafeState.tla
 * -- this spec's own Kick/Miss actions below model only the timing-
 * independent, purge-relevant consequence of an overflow, abstracting away
 * the elapsed-time comparison WatchdogSafeState.tla already verifies in
 * full, so the two specs are complementary, not duplicates.
 *
 * Safety property (SP1): a watchdog-overflow purge with
 * rx_wd_safestate_enable set never discards a pending safety-tagged
 * request -- only normal-tagged requests are purged.
 * Safety property (SP2): a safety-tagged request only ever transitions
 * from pending to executed while its endpoint reports it has reached
 * the configured safe state.
 *
 * Liveness property (LP1): a safety-tagged request that becomes pending
 * eventually executes, given per-stream fair scheduling of ExecuteSafety and
 * an endpoint safe-state signal that eventually settles and stays true. The
 * naive stronger wording -- "a pending safety-tagged request is eventually
 * either executed or purged, never stuck pending forever" -- is false by
 * construction against this spec's own SP1 above: Miss(s) (the only purge
 * event) never touches safety_pending by design, so "purged" is never a
 * live alternative for a safety-tagged request, and a property requiring
 * "executed or purged" is unprovable as stated (TLC finds a trivial
 * submit-and-never-purge counterexample). LP1 below is the corrected,
 * narrower claim this spec can actually make and TLC confirms holds. TLC's
 * default no-successor-state deadlock check passes (every state in the
 * model has at least one enabled successor). LP1 is a related but strictly
 * stronger claim: per-stream progress/livelock-freedom under fairness, not
 * mere deadlock-freedom. The fairness-minimality result (WF suffices, SF
 * buys nothing extra) was re-confirmed against real TLC runs during this
 * port and carries over unchanged from c-RCP's own original derivation --
 * the underlying action structure is unchanged.
 *)

EXTENDS TLC

CONSTANTS Streams,             \* set of request-stream identifiers
          SafestateEnabled,    \* streams with rx_wd_safestate_enable set
          WatchdogEnabled      \* streams with rx_wd_enable set

ASSUME SafestateEnabled \subseteq Streams
ASSUME WatchdogEnabled  \subseteq Streams

VARIABLES overflowed,           \* Stream -> BOOLEAN: RxWatchdog::overflowed() verdict
          endpoint_in_safe_state, \* Stream -> BOOLEAN: polled safe-state measurement
                                   \* (endpoint_in_configured_safe_state())
          safety_pending,       \* Stream -> BOOLEAN: a safety-tagged request is queued
          normal_pending        \* Stream -> BOOLEAN: a normal-tagged request is queued

vars == <<overflowed, endpoint_in_safe_state, safety_pending, normal_pending>>

TypeOK ==
    /\ overflowed             \in [Streams -> BOOLEAN]
    /\ endpoint_in_safe_state \in [Streams -> BOOLEAN]
    /\ safety_pending         \in [Streams -> BOOLEAN]
    /\ normal_pending         \in [Streams -> BOOLEAN]

Init ==
    /\ overflowed             = [s \in Streams |-> FALSE]
    /\ endpoint_in_safe_state \in [Streams -> BOOLEAN]
    /\ safety_pending         = [s \in Streams |-> FALSE]
    /\ normal_pending         = [s \in Streams |-> FALSE]

(* RxWatchdog::kick()-equivalent: resets a stream's elapsed-since-last-kick
 * clock, clearing any overflow verdict. *)
Kick(s) ==
    /\ overflowed' = [overflowed EXCEPT ![s] = FALSE]
    /\ UNCHANGED <<endpoint_in_safe_state, safety_pending, normal_pending>>

(* RxWatchdog::overflowed() reporting TRUE once elapsed exceeds
 * rx_wd_timeout_interval -- only possible while the watchdog is enabled for
 * this stream (a disabled watchdog never overflows, RxWatchdog::overflowed()'s
 * own !cfg.rx_wd_enable guard). When rx_wd_safestate_enable is also set,
 * this is exactly apply_watchdog_overflow()'s own purge event: every
 * pending normal-tagged request is discarded
 * (request::RequestLedger::cancel_all(/*non_safestate_only=*/true)), but a
 * pending safety-tagged request survives untouched. *)
Miss(s) ==
    /\ s \in WatchdogEnabled
    /\ overflowed[s] = FALSE
    /\ overflowed' = [overflowed EXCEPT ![s] = TRUE]
    /\ IF s \in SafestateEnabled
       THEN normal_pending' = [normal_pending EXCEPT ![s] = FALSE]
       ELSE UNCHANGED normal_pending
    /\ UNCHANGED <<endpoint_in_safe_state, safety_pending>>

(* A caller submits a safety-tagged (RequestRecord::is_safety, wire 0x8x)
 * request; may_execute_now() only ever governs *execution*, not admission
 * into the queue, so submission itself is unconditional. *)
SubmitSafety(s) ==
    /\ safety_pending' = [safety_pending EXCEPT ![s] = TRUE]
    /\ UNCHANGED <<overflowed, endpoint_in_safe_state, normal_pending>>

SubmitNormal(s) ==
    /\ normal_pending' = [normal_pending EXCEPT ![s] = TRUE]
    /\ UNCHANGED <<overflowed, endpoint_in_safe_state, safety_pending>>

(* may_execute_now(): a safety-tagged request executes only once the
 * endpoint reports it has reached its configured safe state
 * (endpoint_in_configured_safe_state()). *)
ExecuteSafety(s) ==
    /\ safety_pending[s]
    /\ endpoint_in_safe_state[s]
    /\ safety_pending' = [safety_pending EXCEPT ![s] = FALSE]
    /\ UNCHANGED <<overflowed, endpoint_in_safe_state, normal_pending>>

(* A non-safety-tagged request is never gated by endpoint_in_safe_state
 * (may_execute_now()'s own !rec.is_safety short-circuit). *)
ExecuteNormal(s) ==
    /\ normal_pending[s]
    /\ normal_pending' = [normal_pending EXCEPT ![s] = FALSE]
    /\ UNCHANGED <<overflowed, endpoint_in_safe_state, safety_pending>>

(* endpoint_in_configured_safe_state()'s own polled measurement changing --
 * e.g. the ForceHighImpedance strategy's external boolean flipping, or the
 * RunSafeSequencer strategy's tracked sequencer state reaching (or leaving)
 * cfg.rx_safe_sequencer_state as the physical endpoint moves. *)
ObserveSafeState(s) ==
    /\ endpoint_in_safe_state' \in [Streams -> BOOLEAN]
    /\ UNCHANGED <<overflowed, safety_pending, normal_pending>>

Next ==
    \E s \in Streams :
        \/ Kick(s)
        \/ Miss(s)
        \/ SubmitSafety(s)
        \/ SubmitNormal(s)
        \/ ExecuteSafety(s)
        \/ ExecuteNormal(s)
        \/ ObserveSafeState(s)

Spec == Init /\ [][Next]_<<overflowed, endpoint_in_safe_state, safety_pending, normal_pending>>

(* FairSpec adds weak fairness, per stream, on ExecuteSafety(s) -- and only
 * weak fairness. Re-confirmed against real TLC runs during this port:
 * nothing in this spec can clear safety_pending[s] except ExecuteSafety(s)
 * itself (Miss(s) leaves it untouched by SP1), so once safety_pending[s]
 * holds and endpoint_in_safe_state[s] holds continuously, ExecuteSafety(s)
 * stays continuously enabled until it is taken -- exactly the condition WF
 * acts on. Strong fairness buys nothing extra here; TLC confirms a WF-only
 * variant already suffices once LP1's antecedent below holds. *)
FairSpec == Spec /\ (\A s \in Streams : WF_vars(ExecuteSafety(s)))

(* LP1's antecedent, per stream: the endpoint's polled safe-state signal
 * eventually settles and stays TRUE. Without this, ObserveSafeState
 * (deliberately left unfair, matching its role as an unconstrained polled
 * environment measurement) could keep flipping endpoint_in_safe_state[s]
 * forever, which would repeatedly disable ExecuteSafety(s) right as it
 * becomes enabled -- WF only acts on an action that is *continuously*
 * enabled, and TLC confirms dropping this antecedent produces exactly that
 * flapping-endpoint counterexample. *)
EndpointEventuallyStable(s) == <>[](endpoint_in_safe_state[s])

(* LP1: for every stream, given its endpoint signal eventually settling true
 * and fair (WF) scheduling of that stream's ExecuteSafety, a safety-tagged
 * request that becomes pending on that stream eventually executes. *)
EventuallySafetyExecutes ==
    \A s \in Streams :
        EndpointEventuallyStable(s) => [](safety_pending[s] => <>~safety_pending[s])

(* SP1: a watchdog-overflow purge (safestate-enabled Miss) never clears a
 * pending safety-tagged request -- only Miss can purge, and Miss leaves
 * safety_pending entirely unchanged by construction; this property confirms
 * that guarantee holds for every reachable step, not just by inspection of
 * the action definition. *)
SafetyRequestsSurvivePurge ==
    [][\A s \in Streams :
        (overflowed[s] = FALSE /\ overflowed'[s] = TRUE /\ s \in SafestateEnabled /\ safety_pending[s])
            => safety_pending'[s]]_<<overflowed, safety_pending>>

(* SP2: a safety-tagged request only ever transitions from pending to
 * not-pending while its endpoint was reporting safe state -- i.e. the only
 * way safety_pending[s] goes TRUE -> FALSE is ExecuteSafety(s), whose own
 * guard requires endpoint_in_safe_state[s]. *)
NoUnsafeSafetyExecution ==
    [][\A s \in Streams :
        (safety_pending[s] /\ ~safety_pending'[s]) => endpoint_in_safe_state[s]]_<<safety_pending, endpoint_in_safe_state>>

THEOREM Spec => TypeOK /\ SafetyRequestsSurvivePurge /\ NoUnsafeSafetyExecution
THEOREM FairSpec => EventuallySafetyExecutes

====
