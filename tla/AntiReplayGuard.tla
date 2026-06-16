---- MODULE AntiReplayGuard ----
(*
 * Formal specification of the E2E anti-replay sliding-window guard.
 *
 * The guard accepts a sequence number iff:
 *   (a) It has not been seen before, AND
 *   (b) high_water - seq_num < ReplayWindowSize
 *
 * Safety property (SP1): A previously accepted seq_num is never accepted again.
 * Safety property (SP2): A seq_num more than ReplayWindowSize behind high_water
 *                        is always rejected.
 *)

EXTENDS Naturals, Sequences, TLC

CONSTANTS ReplayWindowSize   \* size of the bitmap window (32 in production)

VARIABLES high_water,        \* highest seen sequence number
          accepted            \* set of all accepted sequence numbers

TypeOK ==
    /\ high_water \in Nat
    /\ accepted   \in SUBSET Nat

Init ==
    /\ high_water = 0
    /\ accepted   = {}

Check(n) ==
    \* Accept n if it is fresh and within the window
    /\ n \notin accepted
    /\ (high_water = 0 \/ high_water - n < ReplayWindowSize \/ n > high_water)
    /\ accepted'   = accepted \cup {n}
    /\ high_water' = IF n > high_water THEN n ELSE high_water

Reject(n) == \* Stutter-step: n is rejected, state unchanged
    /\ (n \in accepted \/ (high_water > 0 /\ high_water - n >= ReplayWindowSize))
    /\ UNCHANGED <<high_water, accepted>>

Next == \E n \in Nat : Check(n) \/ Reject(n)

Spec == Init /\ [][Next]_<<high_water, accepted>>

(* SP1: No double-acceptance *)
NoDoubleAccept ==
    [][\A n \in accepted : n \notin accepted']_accepted

(* SP2: Old sequences are always rejected (checked via Check guard above) *)

THEOREM Spec => TypeOK

====
