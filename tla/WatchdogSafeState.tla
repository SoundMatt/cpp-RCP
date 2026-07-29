---- MODULE WatchdogSafeState ----
(*
 * Formal specification of rcp::e2e::RxWatchdog / rcp::watchdog::
 * StreamWatchdog (rcp/e2e.hpp, rcp/watchdog.hpp, v2.6.0/v2.10.0), the
 * per-request-stream watchdog and safe-state latch.
 *
 * This module replaces tla/HealthStateMachine.tla and
 * tla/WatchdogProtocol.tla, which both modelled a three-state
 * Healthy/Degraded/Faulted machine keyed by "Zone" and driven by a
 * client-side periodic CommandType::Watchdog kick — the pre-replacement
 * design discarded outright at v2.10.0 per ROADMAP.md's Satellite Package
 * Disposition table entry for `watchdog.hpp`. The current mechanism has
 * no Degraded intermediate state: a stream is armed (not latched) or
 * latched into safe state, and it is kicked by any accepted inbound
 * request — not a dedicated watchdog command — regardless of request kind
 * or safety tag.
 *
 * Behavior modelled (own description of this implementation, not spec
 * text): kick(now) records the current time as the last-seen-activity
 * clock. overflowed(now) reports TRUE once more than TimeoutInterval time
 * units have elapsed since the last kick, but only once the watchdog has
 * been kicked at least once and is not already latched — an unkicked or
 * already-latched watchdog does not re-fire. When a stream is configured
 * with SafestateEnabled, an observed overflow latches safe state; when it
 * is not, the overflow is reported but nothing latches. clear_safe_state
 * is the only way to unlatch, mirroring rcp/e2e.hpp's/rcp/watchdog.hpp's
 * explicit-unlatch convention used throughout this codebase since v2.6.0.
 *
 * Safety property (SP1): the watchdog never latches while
 *                        SafestateEnabled = FALSE.
 * Safety property (SP2): once latched, only an explicit ClearSafeState
 *                        step (identifiable by every other state
 *                        component staying unchanged) ever unlatches it —
 *                        Kick and Tick, by construction, never set
 *                        latched' = FALSE while latched = TRUE.
 *)

EXTENDS Naturals, TLC

CONSTANTS TimeoutInterval,      \* rx_wd_timeout_interval, a positive constant
          SafestateEnabled      \* BOOLEAN: this stream's rx_wd_safestate_enable configuration

VARIABLES kicked,               \* BOOLEAN: has kick() ever been called
          last_kick,            \* simulated clock value at the last kick()
          clock,                \* simulated clock, advanced by Tick
          latched                \* BOOLEAN: whether safe state is currently latched

TypeOK ==
    /\ kicked  \in BOOLEAN
    /\ last_kick \in Nat
    /\ clock   \in Nat
    /\ latched \in BOOLEAN
    /\ TimeoutInterval \in Nat \ {0}

Init ==
    /\ kicked    = FALSE
    /\ last_kick = 0
    /\ clock     = 0
    /\ latched   = FALSE

(* Kick mirrors RxWatchdog::kick(now_ms) — does not itself clear a latch. *)
Kick ==
    /\ kicked'    = TRUE
    /\ last_kick' = clock
    /\ UNCHANGED <<clock, latched>>

(* Tick advances the simulated clock by one unit and, if the watchdog is
   armed (kicked, not latched) and the elapsed time now exceeds
   TimeoutInterval, applies the overflow rule: latch iff SafestateEnabled,
   mirroring StreamWatchdog::check + e2e::apply_watchdog_overflow. *)
Tick ==
    /\ clock' = clock + 1
    /\ IF kicked /\ ~latched /\ (clock' - last_kick) > TimeoutInterval
       THEN latched' = SafestateEnabled
       ELSE latched' = latched
    /\ UNCHANGED <<kicked, last_kick>>

ClearSafeState ==
    /\ latched' = FALSE
    /\ UNCHANGED <<kicked, last_kick, clock>>

Next == Kick \/ Tick \/ ClearSafeState

Spec == Init /\ [][Next]_<<kicked, last_kick, clock, latched>>

(* SP1: No latch without SafestateEnabled *)
NoLatchWithoutEnable ==
    ~SafestateEnabled => [](~latched)

(* SP2: A latch is only cleared by a step that leaves every other state
   component unchanged — the fingerprint of ClearSafeState, since Kick
   always changes <<kicked, last_kick>> and Tick always changes clock. *)
LatchIsSticky ==
    [][latched /\ latched' = FALSE => UNCHANGED <<kicked, last_kick, clock>>]_
        <<kicked, last_kick, clock, latched>>

THEOREM Spec => TypeOK /\ NoLatchWithoutEnable /\ LatchIsSticky

BoundedClock == clock < 10 \* TLC state-space bound only; not part of the modelled system

====
