---- MODULE HealthStateMachine ----
(*
 * Formal specification of the Watchdog HealthState machine (ISO 26262 ASIL-B).
 *
 * States:   Healthy, Degraded, Faulted
 * Events:   kick (zone kicks watchdog), miss (watchdog interval elapsed, no kick)
 *
 * Safety property (SP1): A zone can only transition Healthy→Faulted via Degraded.
 * Liveness property (LP1): If a zone continuously kicks, it stays Healthy.
 *)

EXTENDS Naturals, TLC

CONSTANTS Zones          \* set of zone identifiers
VARIABLES state,         \* function: Zone -> HealthState
          miss_count      \* function: Zone -> Nat (consecutive miss count)

Healthy  == "Healthy"
Degraded == "Degraded"
Faulted  == "Faulted"

HealthStates == {Healthy, Degraded, Faulted}

MaxMiss == 2  \* misses before Degraded→Faulted

TypeOK ==
    /\ state      \in [Zones -> HealthStates]
    /\ miss_count \in [Zones -> Nat]

Init ==
    /\ state      = [z \in Zones |-> Healthy]
    /\ miss_count = [z \in Zones |-> 0]

Kick(z) ==
    /\ state'      = [state EXCEPT ![z] = Healthy]
    /\ miss_count' = [miss_count EXCEPT ![z] = 0]

Miss(z) ==
    LET mc == miss_count[z]
    IN
    /\ miss_count' = [miss_count EXCEPT ![z] = mc + 1]
    /\ IF state[z] = Healthy
       THEN state' = [state EXCEPT ![z] = Degraded]
       ELSE IF state[z] = Degraded /\ mc + 1 >= MaxMiss
            THEN state' = [state EXCEPT ![z] = Faulted]
            ELSE state' = state

Next ==
    \E z \in Zones : Kick(z) \/ Miss(z)

Spec == Init /\ [][Next]_<<state, miss_count>>

(* Safety property SP1: No Healthy→Faulted direct transition *)
NoDirectFault ==
    [][\A z \in Zones :
        state[z] = Healthy => state'[z] # Faulted]_<<state, miss_count>>

THEOREM Spec => TypeOK /\ NoDirectFault

====
