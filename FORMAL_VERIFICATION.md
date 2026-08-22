# Formal Verification — cpp-RCP (Milestone 62)

## Overview

TLA+ specifications are located in [`tla/`](tla/). They cover the three
independent primitives `rcp/e2e.hpp` and `rcp/watchdog.hpp` provide for the
safety-critical subsystems identified in the HARA (see [`HARA.md`](HARA.md)),
re-derived at the Certification Refresh milestone (v2.18.0) against the
actual v2.6.0/v2.10.0 mechanism rather than the pre-replacement,
Zone/Command-shaped model the previous revision of this document described.

| Spec | Module | Safety Property |
|------|--------|----------------|
| `RxSequenceGuard.tla`   | `e2e::RxSequenceGuard` monotonic sequence check | SP1: high-water mark never decreases; SP2: no stale acceptance |
| `CrcSafeStateLatch.tla` | `e2e::RxStreamGuard` E2E CRC drop-vs-latch rule | SP1: no latch without `rx_enforce_e2e`; SP2: latch only cleared by an explicit reset |
| `WatchdogSafeState.tla` | `e2e::RxWatchdog`/`watchdog::StreamWatchdog` timeout + safe-state latch | SP1: no latch without `rx_wd_safestate_enable`; SP2: latch only cleared by an explicit reset |

`tla/AntiReplayGuard.tla`, `tla/HealthStateMachine.tla`, and
`tla/WatchdogProtocol.tla` are retired as of this milestone: the first
modelled the pre-replacement 32-entry sliding-window bitmap guard removed
at v2.6.0, and the latter two both modelled the same three-state
Healthy/Degraded/Faulted watchdog machine, driven by a client-side
periodic `CommandType::Watchdog` kick, discarded outright at v2.10.0. The
current watchdog mechanism has no Degraded intermediate state and is kicked
by any accepted inbound request, not a dedicated command.

## Verification Method

Specs are verified with the TLC model checker (`tla2tools.jar`, TLA+
Toolbox ≥ 1.7). Each spec's like-named `.cfg` file in `tla/` supplies the
constants, `INIT`/`NEXT`, and the invariants/properties TLC checks — TLC
loads it automatically when invoked without `-config`:

```bash
java -jar tla2tools.jar -workers 4 tla/RxSequenceGuard.tla
java -jar tla2tools.jar -workers 4 tla/CrcSafeStateLatch.tla
java -jar tla2tools.jar -workers 4 tla/WatchdogSafeState.tla
```

Expected output: `Model checking completed. No error has been found.`

`CrcSafeStateLatch.cfg` and `WatchdogSafeState.cfg` each check their
respective spec's affirmative latch behavior under
`EnforceLatch`/`SafestateEnabled = TRUE`; the complementary
`NoLatchWithoutEnforce`/`NoLatchWithoutEnable` property is non-vacuous only
under the `FALSE` configuration — flip the relevant `CONSTANT` line and
re-run to check it directly (see each `.cfg` file's own comment).

## Safety Properties Verified

### SP1/SP2 — Sequence Monotonicity (`RxSequenceGuard`)

A stream's high-water mark (`last_seq`) never decreases, and a sequence
number no greater than the current high-water mark is never subsequently
accepted. Unlike the pre-replacement sliding-window bitmap this spec
supersedes, there is no window to exhaust — acceptance is a single
comparison against the high-water mark.

**STATUS CORRECTED 2026-08-21 (cpp-RCP issue #129 / RELAY Phase 17 Phase 2
pass) — two distinct corrections, both documentation-only:**

1. **Wiring gap, now closed for this codebase's own reference dispatch.**
   This section previously stated that `RxSequenceGuard` was **never
   instantiated anywhere outside its own unit test** — not in
   `mock::Server`'s dispatch, and not in any transport `Server`. That was
   accurate as of the 2026-08-21 pass that added this note, but is
   superseded by Phase 4 batch C, "mock.hpp batch C — wire
   RxSequenceGuard, StreamFaultTracker, RxWatchdog" (CHANGELOG.md, cpp-RCP
   issue #129, PR #148): `rcp/mock.hpp`'s `Server` now holds a
   `std::array<e2e::RxSequenceGuard, ...> seq_trackers_` member, and both
   `Server::dispatch_e2e()` and `Server::dispatch_frame_e2e()` call it
   (via a shared `seq_gate_admits()` helper) on every dispatched
   request/frame before CRC unwrap — integration-tested in
   `tests/test_mock.cpp` ("dispatch_e2e's sequence gate (REQ-E2E-028/029)
   rejects a non-increasing sequence_num"), not merely the standalone
   primitive's own `tests/test_e2e.cpp` unit test. See `HARA.md`'s own
   corrected H-004 section and `include/rcp/e2e.hpp`'s file header
   ("UPDATE (Phase 4/Phase 17 batch C...)") for the full account
   (mirroring c-RCP's own earlier resolution of the identical ambiguity,
   issues #601/#606). SP1/SP2 as verified here are therefore properties of
   a primitive that **is** now wired into this codebase's own reference
   dispatch — with the same "an integrator bypassing `mock::Server`'s own
   dispatch is on their own" caveat every primitive in this codebase
   carries (see `HARA.md`'s H-004 Residual Risks entry).
2. **This spec models the pre-Phase-2-pass algorithm.** The same Phase 2
   pass that produced this correction also content-corrected
   `e2e::RxSequenceGuard`'s actual comparison rule against c-RCP's
   `rcp_e2e_seq_evaluate()`: acceptance is now an RFC 1982 forward-window
   comparison over the 8-bit AVTPDU `sequence_num` space (forward distance
   in `[1, 127]`), not the plain non-wrapping `n > last_seq` this file's
   `tla/RxSequenceGuard.tla` still models (its own `Accept(n)` action). A
   non-wrapping model is a real behavioral divergence from the corrected
   C++ implementation, not merely an abstraction choice the "Assumptions
   and Abstractions" section below already accounts for (that section's
   "unsigned 32-bit wrap-around... is not modelled directly" note predates
   and does not cover this). Re-deriving `RxSequenceGuard.tla` (and its
   `.cfg`) against the RFC 1982 rule — including the independent
   `rx_seq_safestate_enable`-gated discontinuity signal this pass also
   added — is **not undertaken in this pass** (a distinct formal-modeling
   task, not a documentation fix); tracked as a follow-up. Until then,
   SP1/SP2 as stated here should be read as verified properties of the
   *prior* algorithm, not the current one.

**ASIL tracing**: H-004 (request replay/out-of-order delivery), SG-004,
REQ-E2E-007.

### SP1/SP2 — CRC Drop-vs-Latch Rule (`CrcSafeStateLatch`)

A stream configured with `rx_enforce_e2e = FALSE` never latches on a CRC
failure — only that request is rejected. A stream configured with
`rx_enforce_e2e = TRUE` latches on its first CRC failure, and — once
latched — only an explicit `reset_latch()` step (identifiable in the model
by every other tracked state component staying unchanged) ever clears it.

**ASIL tracing**: H-011 (E2E CRC integrity — the most safety-relevant new
surface introduced across this roadmap), SG-011, REQ-E2E-004, REQ-E2E-006.

### SP1/SP2 — Watchdog Timeout + Safe-State Latch (`WatchdogSafeState`)

A stream configured with `rx_wd_safestate_enable = FALSE` never latches on
a watchdog timeout — only the overflow itself is reported. A stream
configured with `rx_wd_safestate_enable = TRUE` latches on the first
observed timeout, and only an explicit `clear_safe_state()` step ever
clears it. `kick()` alone never causes a timeout, since overflow detection
requires an elapsed-time step (`Tick`) past `rx_wd_timeout_interval`.

**ASIL tracing**: H-003 (watchdog not kicked), H-009 (RC Server not woken),
SG-003, SG-008, SG-010, REQ-WDG-001, REQ-WDG-003, REQ-WDG-004.

## Assumptions and Abstractions

- The TLA+ models use natural numbers as simulated clocks; unsigned 32-bit
  wrap-around (the C++ implementation's actual sequence-number and
  watchdog-timer representation) is not modelled directly, since TLC model
  checking already bounds the state space independently via each spec's
  `.cfg` constants/constraints.
- `RxSequenceGuard.tla` and `CrcSafeStateLatch.tla`/`WatchdogSafeState.tla`
  are modelled independently, matching `rcp/e2e.hpp`'s own separation of
  `RxSequenceGuard`, `RxStreamGuard`, and `RxWatchdog` into three
  independently-configurable primitives — a stream can enable any
  combination of `rx_enforce_seq`, `rx_enforce_e2e`, and
  `rx_wd_safestate_enable` (or none) per the register map.
- `WatchdogSafeState.tla` assumes a synchronous `Tick` action for the
  elapsed-time check; the C++ implementation instead compares
  caller-supplied wall-clock milliseconds on each `check()`/`poll()` call,
  with no timer thread of its own (`RxWatchdog`/`StreamWatchdog` are
  primitives, not a scheduler — see `rcp/watchdog.hpp`'s own header
  comment).

## Mapping to C++ Implementation

| TLA+ Spec | TLA+ Variable | C++ Location |
|-----------|---------------|--------------|
| `RxSequenceGuard` | `last_seq`, `has_last` | `e2e::RxSequenceGuard::last_seq_`, `has_last_` |
| `CrcSafeStateLatch` | `latched` | `e2e::RxStreamGuard::latched_` |
| `WatchdogSafeState` | `kicked`, `last_kick`, `latched` | `e2e::RxWatchdog::kicked_`, `last_kick_ms_`, `in_safe_state_` |
