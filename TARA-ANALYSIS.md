# TARA — Threat Analysis and Risk Assessment (Milestone 62)
## ISO 21434 / IEC 62443 SL-2

**Document version**: 2.0.0
**ASIL level**: ASIL-B (ISO 26262) / SIL-2 (IEC 61508)
**Date**: 2026-07-28

---

## 1. Scope

This TARA covers the cpp-RCP in-vehicle communication library as of the
Certification Refresh (v2.18.0). It identifies cybersecurity threats to
RC Client / RC Server communication under the OPEN Alliance TC18 Remote
Control Protocol Specification v0.5.1_RC's stream/endpoint/register-map
model and assigns attack feasibility / impact ratings. This revision
supersedes the pre-replacement, Zone/Command-shaped analysis authored at
milestone 42 — every asset, threat, and mitigation below is re-derived
against the current architecture rather than carried forward unchanged.

---

## 2. Assets

| Asset ID | Asset | Security Property |
|----------|-------|-------------------|
| A-01 | Request/response channel (addressed by stream_id + byte_bus_id) | Integrity, Authenticity |
| A-02 | Per-stream watchdog state (`e2e::RxWatchdog`, `watchdog::Manager`) | Availability, Integrity |
| A-03 | E2E sequence counter (`e2e::RxSequenceGuard`) | Integrity |
| A-04 | E2E CRC trailer / request-response payload integrity (`e2e::verify_crc`) | Integrity |
| A-05 | TLS session credentials (UDP/IP transport variant) | Confidentiality |
| A-06 | RC Server lifecycle/liveness state (`lifecycle::ServerLifecycle`, `deadline::Monitor`) | Integrity |
| A-07 | Access control policy (`authz::AccessPolicy`) | Integrity, Availability |
| A-08 | Register map / EP0 configuration (`regmap::Ep0`) | Integrity, Availability |
| A-09 | Discovery stream claim (`discovery::DiscoveryClaim`) | Availability, Integrity |

---

## 3. Threat Table

| Threat ID | Asset | Threat | STRIDE | Attack Vector |
|-----------|-------|--------|--------|---------------|
| T-01 | A-01, A-03 | Request replay or out-of-order re-injection | Spoofing | In-vehicle network |
| T-02 | A-01, A-07 | Request forgery without authentication | Spoofing | Compromised HPC |
| T-03 | A-02 | Request flood degrading watchdog-adjacent stream throughput | DoS | Local |
| T-04 | A-02 | Watchdog silence — attacker blocks a stream's inbound requests to force a spurious safe-state latch | DoS | Kernel / driver |
| T-05 | A-04 | Payload tampering that bypasses the E2E CRC check | Tampering | Network |
| T-06 | A-04 | Unauthorized `reset_latch()` clearing a legitimately-latched safe state | Tampering | Compromised process |
| T-07 | A-03 | Sequence-counter overflow/wrap injection | Spoofing | In-vehicle |
| T-08 | A-05 | TLS credential theft | Information Disclosure | Physical |
| T-09 | A-09 | Discovery spoofing — a rogue device answers a discovery request as if it were the intended RC Server | Spoofing | In-vehicle network |
| T-10 | A-08 | Register-map write by a non-root client | Elevation of Privilege | Network |
| T-11 | A-07 | Policy bypass via identity forgery | Elevation of Privilege | Network |

---

## 4. Risk Assessment

Ratings: Feasibility (1-5) × Impact (1-5) = Risk Score

| Threat | Feasibility | Impact | Risk | Mitigation |
|--------|-------------|--------|------|-----------|
| T-01 | 3 | 4 | 12 | Strictly-increasing sequence check (`e2e::RxSequenceGuard::check`) |
| T-02 | 2 | 5 | 10 | mTLS (`tls::SecureClient`/`SecureServer`) + `authz::AccessPolicy` |
| T-03 | 4 | 2 |  8 | Token-bucket rate limiter (`ratelimit::Manager`) |
| T-04 | 2 | 5 | 10 | Per-stream `watchdog::StreamWatchdog` + `deadline::Monitor` liveness cross-check |
| T-05 | 2 | 5 | 10 | 32-bit E2E CRC verification (`e2e::verify_crc`, polynomial `0xF4ACFB13`) |
| T-06 | 1 | 4 |  4 | `reset_latch()` is an explicit, application-gated call, not reachable from wire input |
| T-07 | 1 | 3 |  3 | Unsigned 32-bit sequence comparison; first-seen bootstraps rather than wraps silently |
| T-08 | 1 | 5 |  5 | Hardware security module for key storage (external) |
| T-09 | 2 | 4 |  8 | Discovery-stream claim (`discovery::DiscoveryClaim`) scoped to a single active claimant |
| T-10 | 1 | 5 |  5 | `regmap::Ep0::check_write_access` restricts whole-map writes to the root client |
| T-11 | 2 | 5 | 10 | Certificate identity verified against `authz::AccessPolicy`'s `PolicyEntry` table |

Residual risks above threshold (score ≥ 10): T-01, T-02, T-04, T-05, T-11.
All are covered by implemented mitigations. Residual risk is accepted at
ASIL-B.

---

## 5. Requirements Traceability

| Threat | Requirement(s) |
|--------|----------------|
| T-01 | REQ-E2E-007 |
| T-02 | REQ-AUTH-001, REQ-AUTH-003, REQ-TLS-001 |
| T-03 | REQ-RL-003, REQ-RL-004 |
| T-04 | REQ-WDG-001, REQ-WDG-003 |
| T-05 | REQ-E2E-001, REQ-E2E-004 |
| T-06 | REQ-E2E-006 |
| T-07 | REQ-E2E-007 |
| T-08 | REQ-TLS-002 (HSM integration, external) |
| T-09 | REQ-DISC-003, REQ-DISC-006 |
| T-10 | REQ-REGMAP-003 |
| T-11 | REQ-AUTH-003, REQ-TLS-001 |

---

## 6. Conclusion

All identified threats have corresponding mitigations aligned with IEC
62443 SL-2 security level requirements. The residual risk profile is
acceptable for ASIL-B / SIL-2 operation. A re-assessment is required when:
- The threat landscape changes (new attack vectors identified)
- A new protocol bridge (`mqttbr.hpp`/`ddsbr.hpp`/`someipbr.hpp`/
  `restbridge.hpp`/`grpcbridge.hpp`/`udsbr.hpp`/`doipbr.hpp`) moves past its
  current unimplemented-stub state into production behavior
- The TLS credential management policy changes, or MACsec (802.1AE)
  link-layer security is adopted for native Ethernet deployments
