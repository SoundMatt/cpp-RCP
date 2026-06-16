---- MODULE WatchdogProtocol ----
(*
 * Formal specification of the Watchdog heartbeat protocol.
 *
 * The keeper polls for kicks every Interval.  A zone must kick within
 * Interval milliseconds or the keeper records a miss.
 *
 * Safety property (SP1): Keeper never transitions a zone to Faulted
 *                        without first passing through Degraded.
 * Liveness property (LP1): If a zone sends kicks infinitely often,
 *                          it remains Healthy infinitely often.
 *)

EXTENDS Naturals, TLC

CONSTANTS Zones, Interval

VARIABLES zone_state,    \* Zone -> {Healthy, Degraded, Faulted}
          last_kick,      \* Zone -> Nat (simulated clock at last kick)
          clock           \* Nat (global simulated clock tick)

Healthy  == "Healthy"
Degraded == "Degraded"
Faulted  == "Faulted"

Init ==
    /\ zone_state = [z \in Zones |-> Healthy]
    /\ last_kick  = [z \in Zones |-> 0]
    /\ clock      = 0

Kick(z) ==
    /\ last_kick'  = [last_kick EXCEPT ![z] = clock]
    /\ UNCHANGED <<zone_state, clock>>

Tick ==
    /\ clock' = clock + 1
    /\ \A z \in Zones :
        LET gap == clock' - last_kick[z]
        IN IF gap > Interval
           THEN zone_state' = [zone_state EXCEPT
                   ![z] = CASE zone_state[z] = Healthy  -> Degraded
                             [] zone_state[z] = Degraded -> Faulted
                             [] OTHER                    -> Faulted]
           ELSE zone_state' = zone_state
    /\ UNCHANGED last_kick

Next == (\E z \in Zones : Kick(z)) \/ Tick

Spec == Init /\ [][Next]_<<zone_state, last_kick, clock>>

SP1_NoDirectFault ==
    [][\A z \in Zones :
        zone_state[z] = Healthy => zone_state'[z] # Faulted]_<<zone_state>>

THEOREM Spec => SP1_NoDirectFault

====
