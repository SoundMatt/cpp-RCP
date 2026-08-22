# Safety Plan — cpp-RCP

## Scope

cpp-RCP is the C++ implementation of the Remote Control Protocol for automotive zonal architecture targeting ISO 26262 ASIL-C / IEC 61508 SIL-2 (corrected from a previously-stated ASIL-B; see `HARA.md`'s H-001 rationale — S3/E4/C2 maps to ASIL-C, not ASIL-B, and no decomposition is claimed).

## Safety standard

| Standard | Target level |
|---|---|
| ISO 26262 | ASIL-C |
| IEC 61508 | SIL-2 |
| IEC 62443 | SL-2 |

## Safety goals

Descriptions and ASIL levels below are kept in sync with `.fusa-hara.json`
(the authoritative machine-readable source) and `HARA.md`. The
pre-replacement Zone/Command/Controller terminology this table previously
used ("Commands", "CmdWatchdog", "Dead zone") has been retired along with
that model — see `HARA.md`'s own note on the milestone-62 supersession.

| ID | Description | ASIL |
|---|---|---|
| SG-001 | Requests to safety-critical endpoints shall be delivered within the configured watchdog period or a fault shall be signalled. | ASIL-C |
| SG-002 | Requests shall only be dispatched to the endpoint they are addressed to (stream_id + byte_bus_id); misaddressed requests shall be rejected. | ASIL-B |
| SG-003 | A per-stream watchdog kick shall be recorded for every accepted inbound request, regardless of request kind or safety tag. | ASIL-C |
| SG-004 | A request stream configured with rx_enforce_seq shall reject any sequence number that is not strictly greater than the last accepted one. | ASIL-B |
| SG-007 | An RC Server that stops responding shall be detected as unreachable within the configured liveness deadline. | ASIL-C |

## Safety mechanisms

The pre-replacement Zone/Command/Controller model (`REQ-CTRL-*`,
`REQ-ERR-011`) this table previously cited was retired at the milestone-62
Certification Refresh (see `CYBERSECURITY.md`/`TARA-ANALYSIS.md`) and no
longer exists in `.fusa-reqs.json` or `include/`. Rows below are repointed
at the real, currently-existing requirements and mechanisms that replaced
it, verified directly against `.fusa-reqs.json` and the relevant headers.

| Mechanism | Requirement | Description |
|---|---|---|
| Endpoint addressing validation | REQ-MOCK-010 | `mock::Server::dispatch` rejects a request whose byte_bus_id addresses no known endpoint, returning `invalid_parameter` — the current stream/endpoint addressing model; replaces the retired zone/Controller lookup, which has no direct analog in a byte_bus_id-addressed architecture |
| Watchdog kick | REQ-WDG-002 | `watchdog::StreamWatchdog::kick_from_request` resets that stream's watchdog on every accepted inbound request, regardless of request kind or safety tag |
| Sequence guard | REQ-E2E-007 | RxSequenceGuard rejects a non-strictly-increasing sequence number when rx_enforce_seq is set |
| Deadline monitor | REQ-DL-002 | `deadline::Monitor` detects an RC Server going silent within the configured liveness deadline (heartbeat or lifecycle-change signal) |
| Execution-priority ordering | REQ-SEQ-002 | Cancellation and triggered requests dispatch ahead of standard/compound requests queued earlier on the same stream |
| Payload copy-on-send *(retired)* | — | No direct analog. This was the pre-replacement in-process `Controller`'s defensive deep-copy before invoking a registered handler; the current stream/endpoint/register-map wire-protocol architecture has no equivalent client-side handler-invocation step to protect |
| Payload copy-on-publish *(retired)* | — | No direct analog, for the same reason — the pre-replacement pub/sub `Controller::Publish`/subscriber model is gone. `admin.hpp`'s SSE/event broadcast (REQ-ADMIN-002) is a different, non-safety-relevant surface (in-process observability, not request payload delivery) and is not offered as a substitute |
| Context / deadline propagation *(retired)* | — | No direct analog. The pre-replacement per-call context-cancellation ("expired context terminates Send without invoking the handler") has no current equivalent; the closest current mechanisms are client-side request timeout (REQ-UDP-010) and the watchdog/deadline-monitor rows above, neither of which reproduces "suppress handler invocation on an already-expired call" |

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
