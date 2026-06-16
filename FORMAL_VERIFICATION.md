# Formal Verification — cpp-RCP (Milestone 41)

## Overview

TLA+ specifications are located in [`tla/`](tla/).  They cover three safety-
critical subsystems identified in the HARA (see [`HARA.md`](HARA.md)):

| Spec | Module | Safety Property |
|------|--------|----------------|
| `HealthStateMachine.tla` | Watchdog health state machine | SP1: No Healthy→Faulted direct transition |
| `AntiReplayGuard.tla`    | E2E sequence-number replay guard | SP1: No double-acceptance; SP2: Old sequences rejected |
| `WatchdogProtocol.tla`   | Watchdog heartbeat protocol | SP1: No Healthy→Faulted direct transition |

## Verification Method

Specs are verified with the TLC model checker (TLA+ Toolbox ≥ 1.7):

```bash
# Install TLA+ Toolbox or tlc2 JAR
java -jar tla2tools.jar -workers 4 tla/HealthStateMachine.tla
java -jar tla2tools.jar -workers 4 tla/AntiReplayGuard.tla
java -jar tla2tools.jar -workers 4 tla/WatchdogProtocol.tla
```

Expected output: `Model checking completed. No error has been found.`

## Safety Properties Verified

### SP1 — No Direct Health Transition (HealthStateMachine, WatchdogProtocol)

A zone controller in `Healthy` state may never transition directly to `Faulted`.
It must pass through `Degraded`.  This prevents a single missed heartbeat from
triggering an emergency shutdown.

**ASIL tracing**: H-002 (watchdog miss), SG-002 (watchdog recovery), REQ-WDG-003.

### SP2 — Anti-Replay Double-Acceptance (AntiReplayGuard)

A sequence number that has been accepted by the E2E guard may never be accepted
again within the replay window.  Sequence numbers older than `ReplayWindowSize`
ticks are unconditionally rejected.

**ASIL tracing**: H-008 (unauthorized injection), SG-006 (mTLS + E2E), REQ-E2E-004.

## Assumptions and Abstractions

- The TLA+ models use natural numbers as simulated clocks; overflow is not
  modelled (production uses 32-bit wrap-around with correct comparison).
- The `AntiReplayGuard` model does not model counter rollover; the C++
  implementation handles rollover via unsigned arithmetic.
- The `WatchdogProtocol` model assumes synchronous ticks; the C++
  implementation uses a background `std::thread` with `std::condition_variable`.

## Mapping to C++ Implementation

| TLA+ Variable | C++ Location |
|---------------|--------------|
| `state[z]` | `watchdog::Keeper::health_[z]` |
| `accepted` | `e2e::ReplayGuard::bitmap_` + `high_water_` |
| `last_kick[z]` | `watchdog::Keeper::last_kick_[z]` |
