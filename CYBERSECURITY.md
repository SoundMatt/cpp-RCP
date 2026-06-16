# Cybersecurity Architecture — cpp-RCP (Milestone 42)
## IEC 62443 SL-2 / ISO 21434

**Document version**: 1.0.0
**Date**: 2026-06-16

---

## 1. Security Layers

### Layer 1 — Transport Security (TLS 1.3)

`include/rcp/tls.hpp` provides the integration surface for mTLS.  The actual
TLS implementation (OpenSSL / wolfSSL) is plugged in at the application layer.

- Mutual certificate authentication (both HPC and zone controller present certs)
- Certificate CN is extracted and passed to the `authz::AuthController`
- Cipher suites: TLS_AES_128_GCM_SHA256, TLS_AES_256_GCM_SHA384 (mandatory)

### Layer 2 — Command Authorization (`authz::AuthController`)

Each send() is checked against an `AccessPolicy` table.  The policy is loaded
from a signed manifest at boot.  ErrForbidden is returned without forwarding
the command.

REQ-AUTH-001..REQ-AUTH-008

### Layer 3 — E2E Anti-Replay (`e2e::ReplayGuard`)

A 32-entry sliding-window bitmap detects replayed sequence numbers.  Sequence
numbers more than 32 behind the high-water mark are unconditionally rejected.

REQ-E2E-004, REQ-E2E-005, REQ-E2E-006

### Layer 4 — Rate Limiting (`ratelimit::Controller`)

Token-bucket rate limiter prevents DoS via command flooding.  Priority::Critical
commands are exempt to preserve safety function availability.

REQ-RL-003, REQ-RL-004

### Layer 5 — Firmware Integrity (`firmware::FirmwareSession`)

SHA-256 hash of the transferred image is verified by the zone controller before
activation.  Rollback requires authentication.

REQ-FW-005..REQ-FW-007

---

## 2. IEC 62443 SL-2 Gap Analysis

| Requirement | Status | Notes |
|------------|--------|-------|
| FR1 Identification & Authentication | Implemented | mTLS + authz |
| FR2 Use Control | Implemented | AccessPolicy per zone/command-type |
| FR3 System Integrity | Implemented | E2E CRC-16 + anti-replay |
| FR4 Data Confidentiality | Partial | TLS stub; HSM key storage external |
| FR5 Restricted Data Flow | Implemented | Zone isolation in routing |
| FR6 Timely Response | Implemented | Deadline monitor + watchdog |
| FR7 Resource Availability | Implemented | Rate limiter; Critical exempt |

---

## 3. Penetration Test Scope

The following attack vectors are in scope for penetration testing:

1. Replay attack on the RCP wire protocol (REQ-E2E-004)
2. Unauthorized command injection (REQ-AUTH-001)
3. Watchdog silence attack (REQ-WDG-003)
4. Firmware rollback to known-vulnerable version (REQ-FW-007)
5. TLS downgrade (covered by min-version enforcement)

---

## 4. Incident Response

Security vulnerabilities in cpp-RCP should be reported via the secure disclosure
process defined in [`SECURITY.md`](SECURITY.md) (not yet authored; tracked as
open action item).
