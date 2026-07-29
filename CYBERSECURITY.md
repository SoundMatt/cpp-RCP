# Cybersecurity Architecture — cpp-RCP (Milestone 62)
## IEC 62443 SL-2 / ISO 21434

**Document version**: 2.0.0
**Date**: 2026-07-28

This revision (ROADMAP.md milestone 62, "Certification Refresh", v2.18.0)
supersedes the pre-replacement, Zone/Command-shaped architecture authored
at milestone 42. Every layer below is re-derived against the current
stream/endpoint/register-map architecture built up across Phase 13-16
(v2.0.0-v2.17.0); the pre-replacement `authz::AuthController`/
`ratelimit::Controller`/`firmware::FirmwareSession` surfaces this document
used to cite are gone or superseded, per ROADMAP.md's Satellite Package
Disposition table.

---

## 1. Security Layers

### Layer 1 — Transport Security (TLS 1.2+, UDP/IP variant)

`include/rcp/tls.hpp` provides `SecureClient`/`SecureServer`, the
integration surface for mutual TLS on the UDP/IP transport variant
(`rcp/udp.hpp`, v2.13.0). The actual TLS implementation (OpenSSL / wolfSSL)
is plugged in at the application layer; neither class reports itself
usable, nor sends/accepts a request, without a real backend attached
(REQ-TLS-005, REQ-TLS-006).

- Mutual certificate authentication, verified by CA (`Config::verify_peer`
  defaults to `true`)
- TLS 1.2+ enforced; weak cipher suites disabled (REQ-TLS-007, REQ-TLS-008)
- For native Ethernet deployments, MACsec (802.1AE) at layer 2 is the
  specification's own preferred link-security mechanism; this package does
  not implement it and flags it as the longer-term preferred approach
  (`tls.hpp`'s own header note)

REQ-TLS-001..REQ-TLS-010

### Layer 2 — Endpoint Authorization (`authz::AccessPolicy`)

Each request is checked against an `AccessPolicy` table keyed by
`(identity, stream, endpoint, request kind)`. An identity with zero policy
entries, or one not covered by any `PolicyEntry`, is denied by default
(REQ-AUTH-003, REQ-AUTH-007). `check()` returns `ErrForbidden` without
forwarding the request.

REQ-AUTH-001..REQ-AUTH-008

### Layer 3 — E2E Integrity & Anti-Replay (`e2e.hpp`)

Three independent, per-stream primitives cover this layer:

- `e2e::verify_crc` — a 32-bit CRC (polynomial `0xF4ACFB13`) computed over
  the stream_id, AVTP timestamp, ACF header, and payload; a mismatch is
  reported as `E2eErrc::crc_error` (REQ-E2E-001, REQ-E2E-004).
- `e2e::RxStreamGuard` — implements the per-stream `rx_enforce_e2e`
  drop-vs-latch rule: with the flag clear, a CRC failure is reported for
  that request alone; with it set, the first failure latches the whole
  stream until `reset_latch()` is called explicitly (REQ-E2E-006).
- `e2e::RxSequenceGuard` — enforces a strictly-increasing sequence number
  per stream when `rx_enforce_seq` is set, independent of the CRC and
  watchdog checks (REQ-E2E-007).

REQ-E2E-001..REQ-E2E-014

### Layer 4 — Rate Limiting (`ratelimit::Manager`)

A token-bucket admission-control layer, keyed per `(stream, endpoint)`
domain, prevents DoS via request flooding. Safety-tagged requests may be
configured exempt from the bucket to preserve safety-function availability
(REQ-RL-004); when not exempted, they are throttled like any other request
(REQ-RL-005).

REQ-RL-001..REQ-RL-008

### Layer 5 — Discovery Integrity (`discovery::DiscoveryClaim`)

Discovery requests are NTSCF-only, addressed at `byte_bus_id` 0 and
register-map address 0 (REQ-DISC-001, REQ-DISC-002). The first discovery
request claims the discovery stream; only the active claim holder may
issue a configuration request, and the claim lapses after
`Discovery_TimeOut` (REQ-DISC-003, REQ-DISC-004, REQ-DISC-006), bounding
how long a single client can monopolize configuration access.

REQ-DISC-001..REQ-DISC-009

### Layer 6 — Register-Map Write Protection (`regmap::Ep0`)

`Ep0`'s whole-register-map read is unrestricted, but a whole-map write
requires the root client — the client that currently holds the exclusive
root-client claim (REQ-REGMAP-003, REQ-REGMAP-004). Per-endpoint write
restrictions and independent config-block locks apply to non-root clients
on top of that gate (REQ-REGMAP-005, REQ-REGMAP-006).

REQ-REGMAP-001..REQ-REGMAP-015

---

## 2. IEC 62443 SL-2 Gap Analysis

| Requirement | Status | Notes |
|------------|--------|-------|
| FR1 Identification & Authentication | Implemented | mTLS (UDP/IP variant) + `authz::AccessPolicy` |
| FR2 Use Control | Implemented | `AccessPolicy` per (identity, stream, endpoint, request kind) |
| FR3 System Integrity | Implemented | E2E CRC32 + `rx_enforce_e2e` latch + `rx_enforce_seq` monotonic check |
| FR4 Data Confidentiality | Partial | TLS integration surface present; concrete backend and HSM key storage are external/application-supplied |
| FR5 Restricted Data Flow | Implemented | Endpoint isolation via `byte_bus_id`-scoped dispatch and per-endpoint `AccessPolicy` entries |
| FR6 Timely Response | Implemented | `deadline::Monitor` liveness tracking + `watchdog::Manager` per-stream watchdog |
| FR7 Resource Availability | Implemented | `ratelimit::Manager` token bucket; safety-tagged requests configurably exempt |

---

## 3. Penetration Test Scope

The following attack vectors are in scope for penetration testing:

1. Replay or out-of-order re-injection on a stream with `rx_enforce_seq` set (REQ-E2E-007)
2. Unauthorized request injection without a valid TLS session or policy entry (REQ-AUTH-001, REQ-TLS-001)
3. Watchdog-silence DoS against a safety-tagged stream (REQ-WDG-003)
4. Discovery spoofing / discovery-claim hijack attempt (REQ-DISC-003, REQ-DISC-006)
5. Non-root register-map write attempt (REQ-REGMAP-003)
6. TLS downgrade (covered by min-version enforcement, REQ-TLS-007)

---

## 4. Incident Response

Security vulnerabilities in cpp-RCP should be reported via the secure
disclosure process defined in [`SECURITY.md`](SECURITY.md); operational
incident handling is defined in [`INCIDENT-RESPONSE.md`](INCIDENT-RESPONSE.md).
