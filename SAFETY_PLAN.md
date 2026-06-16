# Safety Plan — cpp-RCP

## Scope

cpp-RCP is the C++ implementation of the Remote Control Protocol for automotive zonal architecture targeting ISO 26262 ASIL-B / IEC 61508 SIL-2.

## Safety standard

| Standard | Target level |
|---|---|
| ISO 26262 | ASIL-B |
| IEC 61508 | SIL-2 |
| IEC 62443 | SL-2 |

## Safety goals

| ID | Description | ASIL |
|---|---|---|
| SG-001 | Commands delivered within watchdog period or fault signalled | ASIL-B |
| SG-002 | Misrouted commands rejected | ASIL-B |
| SG-003 | CmdWatchdog always deliverable | ASIL-B |
| SG-004 | Replayed commands detected and rejected | ASIL-B |
| SG-007 | Dead zone detected within configured deadline | ASIL-B |

## Safety mechanisms

| Mechanism | Requirement | Description |
|---|---|---|
| Zone mismatch detection | REQ-CTRL-025, REQ-ERR-011 | Controller rejects commands addressed to a different zone |
| Payload copy-on-send | REQ-CTRL-026 | Payload is deep-copied before handler invocation |
| Payload copy-on-publish | REQ-CTRL-027 | Published payload is deep-copied before delivery to subscribers |
| Context / deadline propagation | REQ-CTRL-004 | Expired context terminates Send without invoking the handler |
| Watchdog kick | REQ-CMD-005, REQ-WDG-007 | CmdWatchdog is a named constant schedulable at PriorityHigh |
| Replay guard | REQ-E2E-005 | ReplayGuard detects replayed sequence numbers within a sliding window |
| Deadline monitor | REQ-DL-002 | Monitor detects a zone going silent within Config.Deadline |
| Priority scheduling | REQ-PQ-001 | PriorityCritical dispatched before High and Normal |

## Verification approach

- All requirements in `.fusa-reqs.json` are annotated with `// fusa:req` and `// fusa:test` markers
- cpp-FuSa `check` enforces zero violations on every PR
- Traceability verified: every requirement must be traced to an implementation and a test
- Tests run on Ubuntu (clang, gcc) and macOS (clang) in CI

## Artifact locations

| Artifact | Path |
|---|---|
| Requirements | `.fusa-reqs.json` |
| HARA | `.fusa-hara.json` |
| IEC 62443 config | `.fusa-iec62443.json` |
| Check report | `check-report.json` (CI generated) |
| Incident response | `INCIDENT-RESPONSE.md` |
| Security policy | `SECURITY.md` |
