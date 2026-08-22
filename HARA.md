# Hazard Analysis and Risk Assessment (HARA)

**Standard:** ISO 26262:2018 Part 3
**System:** cpp-RCP — C++ implementation of the OPEN Alliance TC18 Remote Control Protocol
**Target ASIL:** ASIL-C (derived)
**Source:** `.fusa-hara.json` (machine-readable authoritative source)

This revision (ROADMAP.md milestone 62, "Certification Refresh", v2.18.0)
supersedes the pre-replacement, Zone/Command/Controller-shaped analysis
authored at milestone 41. Hazard/safety-goal IDs are carried forward
unchanged where the underlying concern still applies to the current
stream/endpoint/register-map architecture; new hazards introduced by
Phase 13-16 work are appended as H-011/SG-011.

---

## Operational Situations

| ID | Description |
|----|-------------|
| OS-001 | Normal operation — the RC Client and every discovered RC Server are reachable |
| OS-002 | Partial network fault — one or more RC Servers unreachable or a request stream lost |
| OS-003 | Safety-critical manoeuvre — a request is in flight to a safety-relevant endpoint (e.g. a GPIO/CAN endpoint driving a braking-adjacent actuator) |
| OS-004 | RC Client software fault — runaway process, crash, or OOM condition on the HPC |
| OS-005 | Elevated network latency — congestion, EMI, or hardware degradation on the AVB/TSN segment |
| OS-006 | Adversarial access — attacker present on the Ethernet segment |
| OS-007 | RC Server power-state transition — cold/hot start or a StandBy/Sleep to Normal wake sequence in progress |

---

## Hazard Table

| ID | Hazard | Severity | Exposure | Controllability | ASIL | Safety Goals |
|----|--------|----------|----------|-----------------|------|--------------|
| H-001 | Loss of request delivery to a safety-critical endpoint (e.g. a braking-adjacent actuator) | S3 | E4 | C2 | ASIL-C | SG-001 |
| H-002 | Request misaddressed to the wrong endpoint via a stale or corrupted stream_id/byte_bus_id pairing | S2 | E3 | C2 | ASIL-B | SG-002 |
| H-003 | Per-stream watchdog not kicked, leading to an unintended safe-state entry or endpoint reset | S2 | E4 | C2 | ASIL-B | SG-003 |
| H-004 | Replay or out-of-order re-delivery of a stale request from a previous session | S2 | E3 | C2 | ASIL-B | SG-004 |
| H-005 | RC Server falsely reported reachable/ready when actually unresponsive | S2 | E3 | C2 | ASIL-B | SG-007 |
| H-006 | Execution-priority inversion — a lower-priority request burst starves a safety-tagged or cancellation request | S2 | E4 | C2 | ASIL-B | SG-001, SG-005 |
| H-007 | Rate limiter blocks a safety-relevant request during a high-traffic burst, causing a spurious watchdog trip | S2 | E3 | C2 | ASIL-B | SG-003, SG-005 |
| H-008 | Unauthorized request injection or register-map modification via an unsecured transport, spoofed discovery response, or non-root EP0 write | S3 | E2 | C2 | ASIL-B | SG-006 |
| H-009 | RC Server not properly woken from StandBy/Sleep, causing loss of actuation during a safety manoeuvre | S3 | E3 | C2 | ASIL-B | SG-001, SG-008 |
| H-010 | Fault injection state persists across process/power cycles, masking real faults in production | S2 | E2 | C3 | ASIL-A | SG-009 |
| H-011 | Undetected payload corruption bypasses the E2E CRC check, or a stream configured to latch on CRC failure fails to enter or hold safe state | S3 | E3 | C2 | ASIL-B | SG-011 |

---

## Safety Goals

| ID | Safety Goal | ASIL | Addressed By |
|----|-------------|------|--------------|
| SG-001 | Requests to safety-critical endpoints shall be delivered within the configured watchdog period or a fault shall be signalled. | ASIL-C | `watchdog::Manager`/`StreamWatchdog`, `deadline::Monitor` |
| SG-002 | Requests shall only be dispatched to the endpoint they are addressed to (stream_id + byte_bus_id); misaddressed requests shall be rejected. | ASIL-B | `acf::AcfMessageInfo` byte_bus_id decode, RC Server dispatch (e.g. `mock::Server::dispatch`) |
| SG-003 | A per-stream watchdog kick shall be recorded for every accepted inbound request, regardless of request kind or safety tag. | ASIL-C | `watchdog::StreamWatchdog::kick_from_request`, `e2e::RxWatchdog` |
| SG-004 | A request stream configured with rx_enforce_seq shall reject any sequence number that is not strictly greater than the last accepted one. | ASIL-B | `e2e::RxSequenceGuard::evaluate`/`check`, wired into `mock::Server::dispatch_e2e()`/`dispatch_frame_e2e()` via `seq_gate_admits()` (Phase 4/Phase 17 batch C, cpp-RCP issue #129, PR #148) — see H-004's corrected note below. |
| SG-005 | Cancellation and triggered requests shall never be delayed by a standard or compound request queued earlier on the same stream. | ASIL-B | `request::SequencerTable`/`RequestLedger` execution-priority ordering |
| SG-006 | Transport authentication (mTLS on the UDP/IP variant, or link-layer authentication on native Ethernet) and per-endpoint access policy shall be enforced on every external RC Server connection. | ASIL-B | `tls::SecureClient`/`SecureServer`, `authz::AccessPolicy`, `discovery::DiscoveryClaim` |
| SG-007 | An RC Server that stops responding shall be detected as unreachable within the configured liveness deadline. | ASIL-C | `deadline::Monitor`/`LivenessTracker` |
| SG-008 | An RC Server shall only be treated as operational after its lifecycle state machine reports RCP_CONFIGURED following a successful wake sequence. | ASIL-B | `lifecycle::ServerLifecycle`, `powerstate::PowerManager` |
| SG-009 | Fault injection rules shall not persist beyond the lifetime of the injecting process. | ASIL-A | `faultinject::Interceptor` in-process state only |
| SG-010 | A stream's watchdog/safe-state status shall be deterministically derivable from its own kick/overflow/latch history alone. | ASIL-B | `e2e::RxWatchdog`, `watchdog::StreamWatchdog` deterministic state |
| SG-011 | A request whose computed E2E CRC does not match its received trailer shall be rejected, and a stream configured with rx_enforce_e2e shall latch safe state on the first such failure until explicitly reset. | ASIL-B | `e2e::verify_crc`, `e2e::RxStreamGuard` |

---

## ASIL Decomposition Rationale

### H-001 (ASIL-C): Loss of request delivery

The RC Client sends requests to RC Servers over an automotive Ethernet
network. Network faults, ECU resets, or HPC process crashes can prevent
request delivery. The required ASIL is C because:

- **S3**: loss of actuation on a braking-adjacent endpoint during emergency
  deceleration could be life-threatening.
- **E4**: RC Servers are continuously addressable during normal driving.
- **C2**: the driver may not be able to react in time if actuation is
  silently lost.

Per ISO 26262-3's severity/exposure/controllability-to-ASIL determination
table, S3/E4/C2 maps to ASIL-C, not ASIL-B. This row previously stated
ASIL-B without claiming any decomposition to justify the reduction; it is
corrected here to ASIL-C, with the correction propagated to SG-001,
SG-003, and SG-007 (the request-delivery/watchdog/liveness chain that
implements this hazard's mitigation, per the decomposition below) and to
this document's stated target ASIL.

**Decomposition**: ASIL-C is achieved via:
1. `watchdog::Manager`/`StreamWatchdog` (ASIL-C): detects a stream whose
   `rx_wd_timeout_interval` has elapsed with no accepted request.
2. `deadline::Monitor` (ASIL-C): detects an RC Server whose liveness
   signal (response/ack-queue heartbeat or EP0 lifecycle-state-changed
   trigger) has gone silent past its configured deadline.
3. No decomposition into lower ASIL elements is claimed; the above
   mechanisms together satisfy ASIL-C directly.

### H-002 (ASIL-B): Endpoint misaddressing

Every request carries an explicit `(stream_id, byte_bus_id)` pair in its
AVTPDU/ACF framing (`rcp/avtp.hpp`, `rcp/acf.hpp`). An RC Server's dispatch
layer is responsible for routing a decoded request only to the endpoint
whose `byte_bus_id` it names — `mock::Server::dispatch` is this codebase's
own reference implementation of that check. This is a single-point
protection; ASIL-B is met because the check is simple, directly observable
in the decoded `acf::AcfMessageInfo`, and independent of request payload
content.

### H-003 (ASIL-B): Watchdog failure

SG-003 is addressed by `watchdog::StreamWatchdog::kick_from_request`,
called once per accepted inbound request regardless of request kind or
safety tag, wrapping `e2e::RxWatchdog`'s timeout/latch primitive
(`rx_wd_enable`/`rx_wd_timeout_interval`). A stream configured with
`rx_wd_safestate_enable` additionally latches safe state and purges queued
non-safety requests on overflow (`e2e::apply_watchdog_overflow`), leaving
safety-tagged (0x8x) requests untouched so they can still drive the
endpoint through its own safe-state sequence.

### H-004 (ASIL-B): Request replay / out-of-order delivery

**STATUS CORRECTED 2026-08-22 (cpp-RCP issue #129 / RELAY Phase 17 Phase 4
batch C pass):** the previous correction below (2026-08-21) stated that
`e2e::RxSequenceGuard` was implemented and content-correct but **never
instantiated anywhere outside its own unit test** — accurate at the time,
but superseded by Phase 4 batch C, "mock.hpp batch C — wire
RxSequenceGuard, StreamFaultTracker, RxWatchdog" (CHANGELOG.md, PR #148).
`rcp/mock.hpp`'s `Server` now holds a
`std::array<e2e::RxSequenceGuard, regmap::request_stream_cfg::kMaxEntries>
seq_trackers_` member, and both `Server::dispatch_e2e()` (single-member)
and `Server::dispatch_frame_e2e()` (multi-member, frame-level) call it —
via a shared `seq_gate_admits()` helper — on every dispatched
request/frame, before CRC unwrap, evaluating the real AVTPDU
`sequence_num` against `RxSequenceGuard::evaluate()`. This is real,
wired, integration-tested behavior: `tests/test_mock.cpp` includes
"dispatch_e2e's sequence gate (REQ-E2E-028/029) rejects a non-increasing
sequence_num" as a dispatch-level test, distinct from and in addition to
`RxSequenceGuard`'s own standalone `tests/test_e2e.cpp` unit test. See
`include/rcp/e2e.hpp`'s own file header ("UPDATE (Phase 4/Phase 17 batch
C...)") for the authoritative account of this wiring.

c-RCP resolved the identical ambiguity for itself earlier (issues
#601/#606): its own equivalent (`rcp_e2e_seq_evaluate()`/
`rcp_e2e_seq_tracker_t`) is wired into its own reference dispatch
(`mock.c`'s `frame_seq_gate_admits()`, called once per AVTPDU frame), and
documented as "Mitigated (opt-in)" with an explicit residual-risk list
(opt-in config bits, no cross-restart persistence, the RFC 1982 `[1,127]`
forward-window bound, per-frame not per-message granularity) — see
c-RCP's `HARA.md` H-004 section and `include/rcp/e2e.h`'s file header.
cpp-RCP's own `mock::Server` reference dispatch is now at that same
point, for both of its own dispatch paths (single-member and
frame-level).

`e2e::RxSequenceGuard::evaluate()`/`check()` remain the correct, content-
verified primitive established by the prior pass (an RFC 1982
forward-window comparison over the 8-bit AVTPDU `sequence_num` space —
accept iff the forward distance from the last accepted value lies in
`[1, 127]` when `rx_enforce_seq` is set, with a separate
`rx_seq_safestate_enable`-gated discontinuity signal for an
increase-by-more-than-one gap; see `e2e.hpp`'s own doc comment for the
full rationale). What has changed is that this primitive is no longer
merely implemented — it is now called against real inbound traffic by
this codebase's own reference dispatch.

One caveat remains, honestly stated: being wired into THIS codebase's own
`mock::Server` reference implementation does not by itself protect an
integrator who drives a transport `Server` (`rcp/udp.hpp`, `rcp/l2.hpp`)
with their own dispatch logic instead of `mock::Server::dispatch_frame()`/
`dispatch_frame_e2e()` as the `FrameHandler` — the same "primitives, not
a mandate" caveat every header in this codebase carries. `udp.hpp`'s and
`l2.hpp`'s own header comments recommend wiring `mock::Server`'s
frame-level dispatch as the `FrameHandler` precisely so this and other
frame-level behaviors are not silently bypassed.

H-004 is therefore **Mitigated (opt-in)**, matching c-RCP's own
disposition, for any integrator using this codebase's `mock::Server`
reference dispatch with `rx_enforce_seq` set on the relevant stream. This
does not change H-004's S2/E3/C2/ASIL-B classification — the hazard's
worst case (no sequence check evaluated at all) is now the state of a
stream with `rx_enforce_seq` clear, a deliberate per-endpoint
configuration choice documented as an accepted residual risk below, not
this library's reference-dispatch default behavior.

### H-006 (ASIL-B): Execution-priority inversion

`request::SequencerTable`/`RequestLedger` dispatch in the specification's
fixed execution-priority ordering — cancellation, then triggered, timed,
compound, compound-wait, chained, and finally standard requests — enforced
server-side per stream rather than as a client-side send-order wrapper.
A burst of standard requests queued ahead of a cancellation or triggered
request can never delay it once dispatch reaches that stream.

### H-008 (ASIL-B): Unauthorized injection / register-map modification

Three independent mechanisms cover this hazard's distinct attack
surfaces: `tls::SecureClient`/`SecureServer` require mutual TLS on the
UDP/IP transport variant (native Ethernet deployments should prefer
MACsec (802.1AE) at layer 2, flagged as the longer-term mechanism this
package does not itself implement); `authz::AccessPolicy` gates requests
by `(identity, server, endpoint, request kind)`; and
`regmap::Ep0::check_write_access` restricts a whole-register-map write to
the root client, rejecting a non-root client's attempt to reconfigure an
endpoint's stream/lifecycle behavior.

### H-011 (ASIL-B): E2E CRC integrity

This is the hazard the roadmap identifies as the most safety-relevant new
surface introduced across this whole roadmap. `e2e::verify_crc` recomputes
the 32-bit CRC (polynomial `0xF4ACFB13`) over the stream_id, AVTP
timestamp (or its documented zero-fill under NTSCF), ACF header, and
payload, and compares it against the received trailer; a mismatch is
reported as `E2eErrc::crc_error` for that request. `e2e::RxStreamGuard`
implements the per-stream opt-in latch: with `rx_enforce_e2e` clear, a
CRC failure is reported for that request alone; with it set, the first
failure additionally latches the whole stream so every subsequent request
also reports `crc_error` until `reset_latch()` is called explicitly (e.g.
on stream reconfiguration). ASIL-B is met because both the per-request
rejection and the whole-stream latch are deterministic functions of the
received bytes and the stream's own configuration — no timing-dependent
or best-effort behavior is involved.

---

## Residual Risks

| Risk | Likelihood | Mitigation | Status |
|------|-----------|------------|--------|
| An integrator driving a transport `Server` (`udp.hpp`/`l2.hpp`) with their own dispatch logic, instead of wiring `mock::Server::dispatch_frame()`/`dispatch_frame_e2e()` as the `FrameHandler`, bypasses the now-wired `RxSequenceGuard` gate (H-004) | Low | `udp.hpp`/`l2.hpp`'s own header comments document wiring `mock::Server`'s frame-level dispatch as the `FrameHandler` as the correct integration path; `RxSequenceGuard::evaluate()`/`check()` remain directly callable for a fully custom dispatch loop | **Mitigated (opt-in) for `mock::Server`'s own reference dispatch — wired via `seq_gate_admits()`/`dispatch_e2e()`/`dispatch_frame_e2e()`, Phase 4 batch C, cpp-RCP issue #129 (see H-004's corrected note)** |
| A stream with `rx_enforce_seq` clear accepts replayed/out-of-order requests by design | Low | Per-endpoint configuration choice; safety-relevant streams are expected to set `rx_enforce_seq` | Accepted |
| `tls::SecureClient`/`SecureServer` require an application-supplied OpenSSL/wolfSSL backend; the interface itself performs no cryptography | Low | Native Ethernet deployments should prefer MACsec (802.1AE) at layer 2 instead, per `tls.hpp`'s own header note | Accepted |
| `mdns.hpp`'s discovery-adjacent name service is rescoped to the UDP/IP transport variant only | Low | TC18's own wire-level discovery (`discovery.hpp`, v2.2.0) covers native Ethernet deployments without a name-service layer | Accepted |
| `rcp/config.hpp` and `rcp/faultinject.hpp` still build against the pre-replacement `rcp.hpp` Zone/Command/Controller model | Medium | Tracked as a separate, still-open rebind item (per milestone 60's closeout); neither is on a safety-relevant request-dispatch path introduced by Phase 13-16 | Accepted |
