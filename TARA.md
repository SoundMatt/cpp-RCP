# TARA — Threat Analysis and Risk Assessment (Milestone 42)
## ISO 21434 / IEC 62443 SL-2

**Document version**: 1.0.0
**ASIL level**: ASIL-B (ISO 26262) / SIL-2 (IEC 61508)
**Date**: 2026-06-16

---

## 1. Scope

This TARA covers the cpp-RCP in-vehicle communication library.  It identifies
cybersecurity threats to zone controller communications in a zonal HPC
architecture and assigns attack feasibility / impact ratings.

---

## 2. Assets

| Asset ID | Asset | Security Property |
|----------|-------|-------------------|
| A-01 | Zone command channel | Integrity, Authenticity |
| A-02 | Watchdog heartbeat | Availability, Integrity |
| A-03 | Firmware images in transit | Integrity, Authenticity |
| A-04 | E2E sequence counter | Integrity |
| A-05 | TLS session credentials | Confidentiality |
| A-06 | Zone health state | Integrity |
| A-07 | Access control policy | Integrity, Availability |

---

## 3. Threat Table

| Threat ID | Asset | Threat | STRIDE | Attack Vector |
|-----------|-------|--------|--------|---------------|
| T-01 | A-01 | Command injection (replay) | Spoofing | In-vehicle network |
| T-02 | A-01 | Command forgery (no auth) | Spoofing | Compromised HPC |
| T-03 | A-02 | Watchdog flood (DoS) | DoS | Local |
| T-04 | A-02 | Watchdog silence (block kick) | DoS | Kernel / driver |
| T-05 | A-03 | Corrupt firmware image | Tampering | Network |
| T-06 | A-03 | Rollback to vulnerable firmware | Repudiation | Local |
| T-07 | A-04 | Sequence counter overflow injection | Spoofing | In-vehicle |
| T-08 | A-05 | TLS credential theft | Information Disclosure | Physical |
| T-09 | A-06 | Health state spoofing | Tampering | Compromised process |
| T-10 | A-07 | Policy bypass via identity forgery | Elevation of Privilege | Network |

---

## 4. Risk Assessment

Ratings: Feasibility (1-5) × Impact (1-5) = Risk Score

| Threat | Feasibility | Impact | Risk | Mitigation |
|--------|-------------|--------|------|-----------|
| T-01 | 3 | 4 | 12 | E2E replay guard (`e2e::ReplayGuard`) |
| T-02 | 2 | 5 | 10 | mTLS + `authz::AuthController` |
| T-03 | 4 | 2 |  8 | Rate limiter (`ratelimit::Controller`) |
| T-04 | 2 | 5 | 10 | Watchdog health degradation state machine |
| T-05 | 2 | 5 | 10 | SHA-256 verify in `firmware::FirmwareSession` |
| T-06 | 2 | 4 |  8 | Anti-rollback counter in zone controller FW |
| T-07 | 1 | 3 |  3 | 32-bit counter with unsigned wrap-around; window rejects |
| T-08 | 1 | 5 |  5 | Hardware security module for key storage |
| T-09 | 1 | 4 |  4 | Health state stored in private memory; CRC protected |
| T-10 | 2 | 5 | 10 | Certificate CN verified via TLS chain |

Residual risks above threshold (score ≥ 10): T-01, T-02, T-04, T-05, T-10.
All are covered by implemented mitigations.  Residual risk is accepted at ASIL-B.

---

## 5. Requirements Traceability

| Threat | Requirement(s) |
|--------|----------------|
| T-01 | REQ-E2E-004, REQ-E2E-005 |
| T-02 | REQ-AUTH-001, REQ-AUTH-002, REQ-TLS-001 |
| T-03 | REQ-RL-003 |
| T-04 | REQ-WDG-003, REQ-WDG-004 |
| T-05 | REQ-FW-005, REQ-FW-006 |
| T-06 | REQ-FW-007 |
| T-07 | REQ-E2E-006 |
| T-08 | REQ-TLS-002 (HSM integration, external) |
| T-09 | REQ-WDG-008 |
| T-10 | REQ-AUTH-003, REQ-TLS-003 |

---

## 6. Conclusion

All identified threats have corresponding mitigations aligned with IEC 62443 SL-2
security level requirements.  The residual risk profile is acceptable for
ASIL-B / SIL-2 operation.  A re-assessment is required when:
- The threat landscape changes (new attack vectors identified)
- A new protocol bridge is enabled in production
- The TLS credential management policy changes
