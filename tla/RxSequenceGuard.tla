---- MODULE RxSequenceGuard ----
(*
 * Formal specification of rcp::e2e::RxSequenceGuard (rcp/e2e.hpp, v2.6.0),
 * the strictly-increasing per-stream sequence check that fires when a
 * request stream's rx_enforce_seq register bit is set.
 *
 * This module supersedes tla/AntiReplayGuard.tla, which modelled the
 * pre-replacement 32-entry sliding-window bitmap guard removed at v2.6.0 —
 * the current mechanism has no window to exhaust: it is a single
 * comparison against the last accepted sequence number.
 *
 * Behavior modelled (own description of this implementation, not spec
 * text): the first observed sequence number on a stream is always
 * accepted and becomes the new high-water mark. Every subsequent
 * sequence number is accepted iff it is strictly greater than the
 * current high-water mark; otherwise it is rejected and state is
 * unchanged.
 *
 * Safety property (SP1): the high-water mark (last_seq) never decreases.
 * Safety property (SP2): once n has been accepted, no m <= n is ever
 *                        accepted afterward — a direct consequence of SP1
 *                        combined with the Accept guard n > last_seq.
 *)

EXTENDS Naturals, TLC

CONSTANTS MaxSeq              \* bounds the state space for TLC

VARIABLES has_last,           \* whether any sequence number has been accepted yet
          last_seq,           \* last accepted sequence number (meaningless while ~has_last)
          accepted            \* set of all sequence numbers ever accepted

TypeOK ==
    /\ has_last \in BOOLEAN
    /\ last_seq \in 0..MaxSeq
    /\ accepted \subseteq (0..MaxSeq)

Init ==
    /\ has_last = FALSE
    /\ last_seq = 0
    /\ accepted = {}

(* Accept mirrors RxSequenceGuard::check with cfg.rx_enforce_seq = TRUE. *)
Accept(n) ==
    /\ n \in 0..MaxSeq
    /\ \/ ~has_last
       \/ n > last_seq
    /\ has_last' = TRUE
    /\ last_seq' = n
    /\ accepted' = accepted \cup {n}

Reject(n) == \* Stutter-step: n is rejected, state unchanged
    /\ n \in 0..MaxSeq
    /\ has_last
    /\ n <= last_seq
    /\ UNCHANGED <<has_last, last_seq, accepted>>

Next == \E n \in 0..MaxSeq : Accept(n) \/ Reject(n)

Spec == Init /\ [][Next]_<<has_last, last_seq, accepted>>

(* SP1: Monotonicity — the high-water mark never decreases *)
Monotonic ==
    [][has_last /\ has_last' => last_seq' >= last_seq]_<<has_last, last_seq>>

(* SP2: A number no greater than the current high-water mark is never
   subsequently added to `accepted` — the set only ever gains numbers
   strictly above every number already in it. *)
NoStaleAcceptance ==
    [][accepted' # accepted =>
        \A n \in accepted : (accepted' \ accepted) \subseteq {m \in 0..MaxSeq : m > n}]_accepted

THEOREM Spec => TypeOK /\ Monotonic /\ NoStaleAcceptance

====
