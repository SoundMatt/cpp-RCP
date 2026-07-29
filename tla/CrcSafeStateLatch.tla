---- MODULE CrcSafeStateLatch ----
(*
 * Formal specification of rcp::e2e::RxStreamGuard (rcp/e2e.hpp, v2.6.0),
 * the per-stream E2E CRC drop-vs-latch rule gated on the rx_enforce_e2e
 * register bit.
 *
 * This is new formal-verification coverage added at the Certification
 * Refresh milestone (ROADMAP.md milestone 62, v2.18.0): the roadmap
 * identifies the E2E CRC / safe-state mechanism (v2.6.0) as the most
 * safety-relevant new surface introduced across this whole roadmap, and
 * the pre-replacement formal specs (tla/HealthStateMachine.tla,
 * tla/WatchdogProtocol.tla) never modelled it — both instead modelled a
 * three-state Healthy/Degraded/Faulted watchdog machine that predates the
 * current architecture (see tla/WatchdogSafeState.tla for that
 * mechanism's own replacement spec).
 *
 * Behavior modelled (own description of this implementation, not spec
 * text): record_crc_result(ok) is called once per checked request on a
 * stream. While the stream is not yet latched, a failing result (ok =
 * FALSE) is always reported as an error for that request; if the stream's
 * rx_enforce_e2e configuration is enabled, that first failure additionally
 * latches the stream. Once latched, every subsequent call reports an
 * error regardless of `ok`, until reset_latch() is called explicitly.
 *
 * Safety property (SP1): once latched, the guard never reports success
 *                        again without an intervening reset_latch().
 * Safety property (SP2): a stream configured with rx_enforce_e2e = FALSE
 *                        never latches on its own.
 *)

EXTENDS Naturals, TLC

CONSTANTS EnforceLatch      \* BOOLEAN: this stream's rx_enforce_e2e configuration

VARIABLES latched            \* BOOLEAN: whether the stream is currently latched

TypeOK ==
    /\ latched \in BOOLEAN
    /\ EnforceLatch \in BOOLEAN

Init ==
    /\ latched = FALSE

(* RecordResult mirrors RxStreamGuard::record_crc_result(cfg, ok). Returns
   are not modelled as a variable — CrcResult below tracks the outcome of
   the most recent call for the safety properties to reference. *)
VARIABLES last_result        \* "ok" | "error" | "unset" — outcome reported by the last call

RecordResult(ok) ==
    IF latched
    THEN /\ last_result' = "error"
         /\ UNCHANGED latched
    ELSE IF ok
         THEN /\ last_result' = "ok"
              /\ UNCHANGED latched
         ELSE /\ last_result' = "error"
              /\ latched' = EnforceLatch \/ latched  \* only sets latched when EnforceLatch

ResetLatch ==
    /\ latched' = FALSE
    /\ last_result' = last_result  \* reset_latch() does not itself report a CRC result

Next ==
    \/ \E ok \in BOOLEAN : RecordResult(ok)
    \/ ResetLatch

Init2 == Init /\ last_result = "unset"

Spec == Init2 /\ [][Next]_<<latched, last_result>>

(* SP1: Once latched, only an explicit ResetLatch step (identifiable in the
   trace by last_result staying unchanged) ever clears it — every
   RecordResult call, by construction, leaves `latched` unchanged while it
   is already TRUE. *)
LatchIsSticky ==
    [][latched /\ latched' = FALSE => last_result' = last_result]_<<latched, last_result>>

(* SP2: With EnforceLatch = FALSE, the stream never transitions to latched. *)
NoLatchWithoutEnforce ==
    ~EnforceLatch => [](~latched)

THEOREM Spec => TypeOK /\ NoLatchWithoutEnforce /\ LatchIsSticky

====
