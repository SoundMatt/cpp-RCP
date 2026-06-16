# Hazard Analysis and Risk Assessment (HARA)

**Standard:** ISO 26262:2018 Part 3  
**System:** cpp-RCP — Remote Control Protocol for automotive zonal architecture  
**Target ASIL:** ASIL-B (derived)  
**Source:** `.fusa-hara.json` (machine-readable authoritative source)

---

## Operational Situations

| ID | Description |
|----|-------------|
| OS-001 | Normal vehicle operation — all zone controllers reachable |
| OS-002 | Partial network fault — one or more zone controllers unreachable |
| OS-003 | Safety-critical manoeuvre — emergency braking or collision avoidance active |
| OS-004 | HPC software fault — runaway process, crash, or OOM condition |
| OS-005 | Elevated network latency — congestion, EMI, or hardware degradation |
| OS-006 | Adversarial access — attacker present on the zone Ethernet bus |

---

## Hazard Table

| ID | Hazard | Severity | Exposure | Controllability | ASIL | Safety Goals |
|----|--------|----------|----------|-----------------|------|--------------|
| H-001 | Loss of command delivery to safety-critical zone (e.g. braking actuator) | S3 | E4 | C2 | ASIL-B | SG-001 |
| H-002 | Spurious command sent to wrong zone controller | S2 | E3 | C2 | ASIL-B | SG-002 |
| H-003 | Zone controller watchdog not kicked, leading to unintended reset | S2 | E4 | C2 | ASIL-B | SG-003 |
| H-004 | Replay of stale commands from a previous drive cycle | S2 | E3 | C2 | ASIL-B | SG-004 |
| H-005 | Zone controller falsely reported as alive when unresponsive | S2 | E3 | C2 | ASIL-B | SG-007 |
| H-006 | Priority inversion — low-priority burst starves safety-critical CmdWatchdog | S2 | E4 | C2 | ASIL-B | SG-001, SG-005 |
| H-007 | Rate limiter blocks watchdog kick during high command burst | S2 | E3 | C2 | ASIL-B | SG-003, SG-005 |
| H-008 | Unauthorized command injection via unsecured transport | S3 | E2 | C2 | ASIL-B | SG-006 |
| H-009 | Power state management failure — zone not properly woken from sleep | S3 | E3 | C2 | ASIL-B | SG-001, SG-008 |
| H-010 | Fault injection state persists across vehicle power cycles | S2 | E2 | C3 | ASIL-A | SG-009 |

---

## Safety Goals

| ID | Safety Goal | ASIL | Addressed By |
|----|-------------|------|--------------|
| SG-001 | Commands to safety-critical zones shall be delivered within the watchdog period or a fault shall be signalled. | ASIL-B | `watchdog::Keeper`, `deadline::Monitor` |
| SG-002 | Commands shall only be processed by the zone they are addressed to; misrouted commands shall be rejected. | ASIL-B | `Controller::send()` ErrZoneMismatch check |
| SG-003 | The watchdog kick command (CmdWatchdog) shall always be deliverable at the configured priority. | ASIL-B | `ratelimit::Controller` exempt_critical, `prioqueue::Controller` Critical bypass |
| SG-004 | Replayed or duplicated commands from prior sessions shall be detected and rejected. | ASIL-B | `e2e::ReplayGuard` bitmap sliding window |
| SG-005 | Priority::Critical commands shall never be delayed by Priority::Normal or Priority::High commands queued earlier. | ASIL-B | `prioqueue::Controller` priority ordering |
| SG-006 | Transport authentication (mTLS or equivalent) shall be enforced on all external zone controller connections. | ASIL-B | `tls::Controller` (interface), mTLS Config |
| SG-007 | A zone controller that stops publishing Status shall be detected as dead within the configured deadline. | ASIL-B | `deadline::Monitor` |
| SG-008 | A zone controller shall only be declared operational after a successful Wake command response. | ASIL-B | `powerstate::Manager` Active transition gate |
| SG-009 | Fault injection rules shall not persist beyond the lifetime of the injecting process. | ASIL-A | `faultinject::Controller` in-process state only |
| SG-010 | Zone controller health state transitions shall be deterministically derivable from observable send() outcomes alone. | ASIL-B | `watchdog::Keeper` deterministic state machine |

---

## ASIL Decomposition Rationale

### H-001 (ASIL-B): Loss of command delivery

The HPC sends commands to zone controllers over an automotive Ethernet network. Network faults, ECU resets, or HPC process crashes can prevent command delivery. The required ASIL is B because:

- **S3**: Loss of braking actuation during emergency deceleration could be life-threatening.
- **E4**: Zone controllers are continuously addressable during normal driving.
- **C2**: The driver may not be able to react in time if actuation is silently lost.

**Decomposition**: ASIL-B is achieved via:
1. `watchdog::Keeper` (ASIL-B): detects non-responsive zones within `fault_after × interval`.
2. `deadline::Monitor` (ASIL-B): detects zones that stop sending Status telemetry.
3. No decomposition into lower ASIL-A + ASIL-A elements is claimed; the above mechanisms together satisfy ASIL-B.

### H-002 (ASIL-B): Zone misrouting

Each `Controller::send()` checks `cmd.zone == this->zone()` before processing. This check is a single-point protection; ASIL-B is met because the check is simple and directly observable (returns ErrZoneMismatch).

### H-003 (ASIL-B): Watchdog failure

SG-003 is addressed by the combination of:
1. `prioqueue::Controller` — Critical-priority CmdWatchdog bypasses queue backpressure.
2. `ratelimit::Controller` — `exempt_critical=true` bypasses the token bucket for Critical commands.

Both mechanisms are independently verifiable and together meet ASIL-B by preventing starvation.

### H-004 (ASIL-B): Command replay

`e2e::ReplayGuard` implements a 32-entry bitmap sliding window. Frames are rejected if:
- The sequence number matches a previously-accepted number within the window, or
- The sequence number is more than `ReplayWindowSize` (32) behind the current high-water mark.

The CRC-16/CCITT-FALSE also protects against accidentally-valid replays.

### H-006 (ASIL-B): Priority inversion

`prioqueue::Controller` uses `std::priority_queue` with Priority::Critical as the highest key. FIFO ordering within the same priority level preserves fairness. The dispatch thread always dispatches the highest-priority entry in the queue.

### H-008 (ASIL-B): Unauthorized injection

The `tls::Controller` interface requires mutual TLS. The stub returns `errc::function_not_supported` to ensure integrators cannot accidentally connect without authentication. Full OpenSSL backend is scoped for v0.7.0 acceptance testing.

---

## Residual Risks

| Risk | Likelihood | Mitigation | Status |
|------|-----------|------------|--------|
| ReplayGuard window exhausted by rapid legitimate traffic | Low | 32-entry window handles 32 in-flight commands per zone; watchdog period is at least 10 ms | Accepted |
| TLS stub returns `function_not_supported` on non-Linux CI | Low | CI explicitly tests the shmem/mock transport; TLS tested on Linux only | Accepted |
| mDNS StaticDiscoverer not sufficient for dynamic topology | Medium | Full mDNS backend deferred to v0.6.0 platform integration; static config covers initial SiL testing | Accepted |
