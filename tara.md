# TARA — cpp-RCP

Standard: ISO 21434:2021 Ch.9  
Generated: 2026-06-17T13:33:03Z

| ID | Asset | Threat | Feasibility | Impact | Risk | Level | Treatment |
|---|---|---|---|---|---|---|---|
| TS-001 | cpp-RCP binary | Tampering with build artifacts | 2 | 4 | 8 | medium | mitigate |
| TS-002 | Configuration (.fusa.json) | Spoofing of safety configuration | 2 | 3 | 6 | medium | mitigate |
| TS-003 | cpp-RCP CLI interface | Information disclosure via verbose output | 1 | 2 | 2 | low | accept |
| TS-004 | Third-party dependencies (FetchContent) | Supply chain compromise | 2 | 4 | 8 | medium | mitigate |
| TS-005 | qualify-report.json | Integrity violation of qualification evidence | 1 | 4 | 4 | low | mitigate |
| TS-006 | .fusa-evidence.json | Replay of stale test evidence | 2 | 3 | 6 | medium | mitigate |
| TS-007 | CI pipeline | Denial of service to safety check gate | 2 | 4 | 8 | medium | mitigate |
| TS-008 | Source code annotations (//fusa:req) | Repudiation of requirement traceability | 1 | 3 | 3 | low | mitigate |

## Cyber Goals

- **TS-001**: Build integrity: sign all release artifacts
- **TS-002**: Config integrity: store config in VCS and verify hash
- **TS-003**: Confidentiality: restrict CLI output in CI logs
- **TS-004**: Integrity: pin dependency hashes in FetchDeps.cmake
- **TS-005**: Integrity: SHA-256 hash field in qualify-report.json
- **TS-006**: Freshness: timestamp + VCS revision in evidence bundle
- **TS-007**: Availability: branch protection requires CI passing
- **TS-008**: Traceability: trace coverage gate enforced in CI
