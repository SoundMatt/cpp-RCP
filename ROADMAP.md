# cpp-RCP Roadmap

## Vision

cpp-RCP is a C++17-native Remote Control Protocol for automotive zonal architecture.

The project focuses on:

- Reliable command delivery from a central computer to distributed zone controllers
- Safety-first design with traceability to ISO 26262 ASIL-B requirements
- Modern C++ developer experience — header-only core, pure interfaces, swappable transports
- Deterministic latency suitable for hard real-time automotive contexts
- Observability by default — metrics, heartbeats, and watchdog support built in

---

## Guiding Principles

1. Pure C++17 first — no OS-specific headers in core interfaces
2. Safety as a first-class concern — requirements in `.fusa-reqs.json`, traced to tests
3. Simplicity over completeness — clean interfaces, not a protocol kitchen sink
4. Testability by default — mock backend ships with the library
5. Zonal architecture native — Zone is a first-class type, not an afterthought
6. Transport-agnostic — swap in-process mock for UDP or TLS without API changes

---

## Release Plan

| Phase | Version | Theme | Summary |
|---|---|---|---|
| **Foundation** | v0.1.0 | Foundation | Core interfaces, mock backend, CI, cpp-FuSa, Docker quickstart ✅ |
| **Foundation** | v0.2.0 | Requirements | 198 atomic SEOOC ASIL-B requirements, full cpp-FuSa coverage ✅ |
| **Safety groundwork** | v0.3.0 | Hardening | Mock correctness fixes, benchmarks, safety timing evidence ✅ |
| **Safety groundwork** | v0.4.0 | HARA expansion | Comprehensive hazard analysis H-001..H-010, SG-001..SG-010 ✅ |
| **Transport stack** | v0.5.0 | UDP transport | Pure-C++ UDP command/response transport with zone discovery ✅ |
| **Transport stack** | v0.6.0 | mDNS discovery | Zero-configuration zone controller discovery via mDNS/DNS-SD ✅ |
| **Transport stack** | v0.7.0 | TLS transport | Mutual TLS channel for zone-controller communication ✅ |
| **Transport stack** | v0.8.0 | Shared memory | Zero-copy intra-host command delivery via shared memory ✅ |
| **Transport stack** | v0.9.0 | Loaned samples | LoaningController interface extending zero-copy to all transports ✅ |
| **Transport stack** | v0.10.0 | TSN transport | IEEE 802.1Qbv-aware UDP transport for hard real-time Ethernet delivery ✅ |
| **Safety mechanisms** | v0.11.0 | Watchdog & heartbeat | CmdWatchdog scheduling, zone health state machine, liveness API ✅ |
| **Safety mechanisms** | v0.12.0 | Deadline monitoring | Zone-to-HPC liveness: alert when Status stops arriving within deadline ✅ |
| **Safety mechanisms** | v0.13.0 | Power state | CmdSleep/CmdWake, zone power state machine, bus-off recovery ✅ |
| **Safety mechanisms** | v0.14.0 | E2E protection | Sequence counter, CRC-16, replay guard on command frames ✅ |
| **Safety mechanisms** | v0.15.0 | Priority queuing | Per-zone priority queue honouring PriorityCritical/High/Normal ✅ |
| **Safety mechanisms** | v0.16.0 | Rate limiting | Per-zone token-bucket admission control against command flooding ✅ |
| **Verification** | v0.17.0 | Zone simulator | Timing-realistic zone controller simulator for SiL/HIL testing ✅ |
| **Verification** | v0.18.0 | Fault injection | Structured fault injection to validate watchdog, E2E, and replay-guard mechanisms ✅ |
| **Security** | v0.19.0 | Authorization | Command-level access control; ISO 21434 SL-2 policy enforcement |
| **Security** | v0.20.0 | Firmware update | CmdUpdate and `firmware/` module for zone controller OTA delivery |
| **Topology** | v0.21.0 | Zone groups | Atomic multi-zone command broadcast with typed zone group sets |
| **Topology** | v0.22.0 | Zone proxy | Transparent zone proxy for multi-hop zonal topologies |
| **Topology** | v0.23.0 | Redundancy | Hot-standby Registry and HPC failover for ASIL-B fault tolerance |
| **Topology** | v0.24.0 | Multi-HPC federation | Multi-HPC active coordination over shared zone bus |
| **Tooling** | v0.25.0 | Observability | OpenTelemetry traces and Prometheus metrics adapter |
| **Tooling** | v0.26.0 | Admin API | HTTP admin interface for runtime registry inspection and control |
| **Tooling** | v0.27.0 | Record & replay | Record command/response/status streams to disk; replay for regression and forensics |
| **Tooling** | v0.28.0 | Config | YAML/JSON zone registry configuration |
| **Tooling** | v0.29.0 | Code generation | Zone manifest → typed C++ controller stubs and fusa-annotated requirements |
| **Tooling** | v0.30.0 | Dynamic data | Runtime schema registry and typed payload codec for schema-less command payloads |
| **Remote access** | v0.31.0 | gRPC bridge | gRPC transport for cloud-connected zone controllers and remote diagnostics |
| **Remote access** | v0.32.0 | REST bridge | HTTP/SSE bridge for browser tooling and cloud integration |
| **Protocol bridges** | v0.33.0 | SOME/IP bridge | Bridge RCP commands to SOME/IP service methods |
| **Protocol bridges** | v0.34.0 | CAN bridge | Bridge RCP commands to CAN frames via cpp-CAN |
| **Protocol bridges** | v0.35.0 | DDS bridge | Bridge RCP Status to DDS topics and DDS samples to RCP commands via cpp-DDS |
| **Protocol bridges** | v0.36.0 | MQTT bridge | Bridge RCP Status to MQTT topics for cloud/telematics integration via cpp-mqtt |
| **Protocol bridges** | v0.37.0 | LIN bridge | Bridge RCP commands to LIN frames for low-bandwidth zone actuators via cpp-LIN |
| **Protocol bridges** | v0.38.0 | UDS bridge | Bridge RCP commands to ISO 14229 UDS service calls for zone controller diagnostics |
| **Protocol bridges** | v0.39.0 | DoIP bridge | Bridge zone controller diagnostics over ISO 13400 Diagnostics over IP |
| **Platform** | v0.40.0 | RTOS / bare-metal | Zone controller client for Zephyr, FreeRTOS, and NuttX RTOS targets |
| **Certification** | v0.41.0 | Formal verification | TLA+ specification and model-checked proofs of health and watchdog state machines |
| **Certification** | v0.42.0 | ISO 21434 | Cybersecurity assurance case, TARA evidence, SL-2 gap report |
| **Certification** | v0.43.0 | Certification | ASIL-D gap analysis, structural coverage report, audit pack |

---

## Milestones

---
### Phase 1 — Foundation
---

### 1. Foundation (v0.1.0) ✅

- Core `include/rcp/rcp.hpp` interfaces: `Controller`, `Registry`, `Command`, `Response`, `Status`
- `include/rcp/mock.hpp` in-process backend
- CI: unit tests (cross-platform), clang-tidy, cpp-FuSa, DCO
- Release workflow: safety artifact regeneration on tag
- Safety artifacts: `.fusa.json`, `.fusa-reqs.json`, `.fusa-hara.json`, `.fusa-iec62443.json`
- Docs: README, SAFETY_PLAN, SECURITY, INCIDENT-RESPONSE, CONTRIBUTING

### 2. Requirements (v0.2.0) ✅

- 198 atomic SEOOC requirements across 24 groups (REQ-ZONE, REQ-PRI, REQ-CMD, REQ-STATUS, REQ-ERR, REQ-CMDSTRUCT, REQ-RESP, REQ-STAT, REQ-CTRL, REQ-REG, REQ-UDP, REQ-E2E, REQ-WDG, REQ-DL, REQ-PWR, REQ-PQ, REQ-RL, REQ-SIM, REQ-FI, REQ-LOAN, REQ-TLS, REQ-SHMEM, REQ-TSN, REQ-MDNS)
- Full cpp-FuSa v0.30.0+ trace and check compliance

---
### Phase 2 — Safety Groundwork
---

### 3. Hardening (v0.3.0) ✅

**Mock correctness fixes** (already in v0.1.0 mock)
- `Registry::lookup` returns `ErrClosed` after `close()`
- `Controller::send` returns `ErrZoneMismatch` when `cmd.zone != controller.zone()`
- Payload copy-on-send prevents cross-zone aliasing

**Benchmarks** (`tests/bench_mock.cpp`) ✅
- `Send_RoundTrip`, `Send_RoundTrip_WithPayload`, `Send_Concurrent` (8 threads), `Publish_FanOut`, `Registry_Lookup`

**Safety timing evidence** (`tests/command_latency_test.cpp`) ✅
- 30-second workload; P99 < 1 ms and Max < 10 ms gates enforced
- Writes `COMMAND_LATENCY.md` as FuSa audit evidence

### 4. HARA Expansion (v0.4.0) ✅

`.fusa-hara.json` expanded to H-001..H-010 and SG-001..SG-010:

- **H-001..H-003**: Original hazards (delivery loss, misrouting, watchdog failure)
- **H-004**: Replay of stale commands → SG-004 (E2E replay guard)
- **H-005**: Zone falsely reported alive → SG-007 (deadline monitor)
- **H-006**: Priority inversion → SG-005 (priority queue Critical bypass)
- **H-007**: Rate limiter blocks watchdog → SG-003, SG-005
- **H-008**: Unauthorized command injection → SG-006 (mTLS)
- **H-009**: Power-state management failure → SG-001, SG-008
- **H-010**: Fault injection persists across power cycles → SG-009
- `HARA.md` documents ASIL decomposition rationale

---
### Phase 3 — Transport Stack
---

### 5. UDP Transport (v0.5.0) ✅

- Length-framed binary command/response protocol over UDP (SOME/IP-aligned framing: message ID, session ID, length prefix)
- `include/rcp/udp.hpp`: `udp::Controller`, `udp::ZoneServer`, `udp::Registry`, frame codec
- Static unicast zone discovery; optional multicast announcement
- Integration tests with loopback interface
- `rcptool` gains `--transport udp --addr <host:port>` flag

### 6. mDNS Discovery (v0.6.0) ✅

- `include/rcp/mdns.hpp`: zero-configuration zone controller discovery via mDNS (RFC 6762) and DNS-SD (RFC 6763); Avahi-compatible
- Zone controllers self-announce as `_rcp._udp.local` service records carrying zone ID, address, and port
- `Discoverer` abstract base: `discover(ctx, events_channel)` with add/remove events
- `Registry::auto_register(ctx, discoverer)` wires discovered controllers into the registry automatically
- Configurable service-instance name format: `<zone-id>.<hostname>._rcp._udp.local`

### 7. TLS Transport (v0.7.0) ✅

- `include/rcp/tls.hpp`: mutual TLS transport via system TLS (OpenSSL or platform API)
- Certificate pinning for zone controller identity verification
- Addresses SG-006: zone identity authenticated via certificate before command acceptance

### 8. Shared Memory Transport (v0.8.0) ✅

- `include/rcp/shmem.hpp`: zero-copy intra-host command delivery via POSIX shared memory (`shm_open`/`mmap`) for zone controllers co-located on the same ECU
- `shmem::Controller` implements the `Controller` interface; swappable with UDP/TLS without API change
- Initial `LoaningController` implementation: `loan()` returns a pre-allocated `Command` buffer from the shared region; `send_loaned()` delivers it without copying
- Linux only; falls back to UDP transport gracefully on other platforms via `auto::new_controller`

### 9. Loaned Samples (v0.9.0) ✅

- `LoaningController` interface extending `Controller` with `loan(int size, std::unique_ptr<Loan>&)` and `send_loaned(ctx, loan, response)`
- `LoaningRegistry` wraps any registry; `lookup_loaning(zone)` returns a `LoaningController` if the underlying transport supports it
- Implementations across all transports:
  - `shmem`: full zero-copy into the shared memory region (no allocation, no copy)
  - `mock`: pre-allocated pool; `BenchmarkSend_Loaned` must report 0 allocs/op
  - UDP/TLS: pool-backed `Command` buffers eliminate per-call allocation; one copy to the socket send buffer remains unavoidable
- Guarantee: `LoaningController::send_loaned` on the shmem and mock paths must not allocate — enforced by benchmark gate in CI
- `auto::new_loaning_controller` selects shmem if available, falls back to pool-backed UDP

### 10. TSN Transport (v0.10.0) ✅

- `include/rcp/tsn.hpp`: IEEE 802.1Qbv (Time-Aware Shaper) aware UDP transport for hard real-time Ethernet delivery
- Credit-Based Shaper (CBS, 802.1Qav) integration for bandwidth reservation per zone stream
- Frame preemption (802.1Qbu) support to protect `Priority::Critical` commands from frame bursts
- Deployment guide: required `SO_PRIORITY` socket options, VLAN tagging, and NIC configuration on Linux (Nvidia Orin / Renesas R-Car H3)
- Timing evidence: `tests/tsn_latency_test.cpp` — loopback measurements with TSN shaper active, demonstrating bounded worst-case delivery latency

---
### Phase 4 — Safety Mechanisms
---

### 11. Watchdog & Heartbeat (v0.11.0) ✅

- `include/rcp/watchdog.hpp`: periodic `CommandType::Watchdog` scheduling with configurable interval per zone
- Zone health state machine: Healthy → Degraded → Faulted with configurable thresholds
- `Registry::watch_health()` returns a `StatusChannel`-like stream for health state change events
- New requirements: REQ-WD-001..REQ-WD-00N (ASIL-B) — addresses SG-003, SG-007

### 12. Deadline Monitoring (v0.12.0) ✅

- `include/rcp/deadline.hpp`: zone-to-HPC direction: alert when `Status` updates from a zone controller stop arriving within a configured deadline
- `DeadlineMonitor` wraps any `Controller`; calls a `missed_deadline_fn` callback if no `Status` is received within the deadline window
- Integrates with `Registry::watch_health()`: a deadline miss transitions the zone to Degraded after one miss, Faulted after N consecutive misses (configurable)
- Complements the watchdog (HPC→zone) to give full bidirectional liveness
- New requirements: REQ-DL-001..REQ-DL-00N (ASIL-B) — addresses SG-001, SG-004

### 13. Power State (v0.13.0) ✅

- `include/rcp/powerstate.hpp`: `CommandType::Sleep` and `CommandType::Wake` already in `CommandType`
- Zone power state machine: Active → Sleeping → WakePending → Active; transitions driven by RCP commands and watchdog timeouts
- `Controller::power_state()` returns the current zone power state
- `Registry::watch_power()` stream for zone power state change events
- Bus-off recovery: automatic `CommandType::Wake` retry with configurable backoff when a zone transitions from Sleeping unexpectedly
- New requirements: REQ-PWR-001..REQ-PWR-00N (ASIL-B)

### 14. E2E Protection (v0.14.0) ✅

- `include/rcp/e2e.hpp`: 32-bit sequence counter per zone controller; rejects out-of-window frames
- CRC-16 frame check on command and response payload
- Anti-replay guard with configurable window size
- New requirements: REQ-E2E-001..REQ-E2E-00N (ASIL-B) — addresses SG-005, SG-010

### 15. Priority Queuing (v0.15.0) ✅

- `include/rcp/prioqueue.hpp`: per-zone send queue with three priority levels
- `Priority::Critical` bypasses normal queue backpressure
- Backpressure metrics exposed via OpenTelemetry counter
- Queue depth and drop rate added to `Status` telemetry
- New requirements: REQ-PQ-001..REQ-PQ-00N (ASIL-B) — addresses SG-008

### 16. Rate Limiting (v0.16.0) ✅

- `include/rcp/ratelimit.hpp`: per-zone token-bucket admission control on the HPC send path
- Configurable burst and sustained rate limits per priority level (`Priority::Critical` exempt by default)
- `ErrBusy` returned immediately when bucket is exhausted; no blocking
- Rate limit state exposed in `Status` telemetry and Prometheus metrics
- New requirements: REQ-RL-001..REQ-RL-00N (ASIL-B) — addresses SG-009

---
### Phase 5 — Verification
---

### 17. Zone Simulator (v0.17.0) ✅

- `include/rcp/sim.hpp`: timing-realistic zone controller simulator for SiL/HIL testing without physical ECUs; implements the full `Controller` interface
- Configurable response latency distribution (constant, normal, or jitter model) and processing load
- Zone health and power state machines driven by injected fault schedules: Healthy → Degraded → Faulted → Recovering
- Watchdog miss detection: simulator transitions to Faulted if `CommandType::Watchdog` is not received within the configured deadline
- Deadline monitoring simulation: publishes `Status` at a configured rate; stops publishing on fault injection to trigger `DeadlineMonitor`
- Composable with the fault injection harness (v0.18.0)
- Ships alongside `include/rcp/mock.hpp`

### 18. Fault Injection (v0.18.0) ✅

- `include/rcp/faultinject.hpp`: structured fault injection harness for validating safety mechanisms introduced in v0.11.0–v0.16.0
- Fault types: missed watchdog kick, missed Status deadline, corrupted CRC frame, replayed sequence number, late response (> timeout budget), dropped response, zone-mismatch command, admission-control exhaustion, spurious sleep transition
- Each fault is a typed value injected via a `FaultSchedule` applied to a `sim::Controller` or live UDP transport
- Regression suite: `tests/fault_injection_test.cpp` — for each fault type, assert the correct sentinel error is returned and the health/power state machine transitions correctly
- Writes `FAULT_INJECTION.md` — FuSa evidence cross-referencing HARA hazards H-001..H-010

---
### Phase 6 — Security
---

### 19. Authorization (v0.19.0)

- `include/rcp/authz.hpp`: command-level access control: a signed `AccessPolicy` declares which HPC identities may send which `CommandType` values to which zones
- `AuthController` wraps any `Controller`; verifies the caller's certificate against the access policy before forwarding commands
- Policy format: YAML/JSON, signed with the zone controller's TLS private key — policies are unforgeable without the zone's key
- `ErrForbidden` sentinel error returned on policy violation; logged to audit trail
- Aligns with IEC 62443 SL-2 target in `.fusa-iec62443.json`: authenticated identity + command-level authorisation
- New requirements: REQ-AUTH-001..REQ-AUTH-00N (ASIL-B / IEC 62443 SL-2) — addresses SG-006

### 20. Firmware Update / OTA (v0.20.0)

- `include/rcp/firmware.hpp`: `CommandType::Update` added to `CommandType`
- Chunked firmware delivery over RCP with integrity check (SHA-256) and rollback support
- `FirmwareSession` manages the multi-command exchange: Initiate → Transfer (N chunks) → Verify → Activate → Reset
- Zone controller authentication required before any `CommandType::Update` is accepted (depends on v0.19.0 Authorization)
- Delta update support: binary diff to minimise transfer size over constrained links
- `rcptool update <zone> <firmware.bin>` subcommand

---
### Phase 7 — Topology & Scalability
---

### 21. Zone Groups (v0.21.0)

- `include/rcp/zonegroup.hpp`: `ZoneGroup` is a typed set of `Zone` values with named constants (e.g. `GroupRearPassenger`, `GroupAllZones`)
- `Registry::send_group(ctx, group, cmd)` dispatches a command atomically to all zones in the group and collects responses
- Partial-failure semantics: returns a `GroupResponse` carrying individual per-zone `Response` and `std::error_code` values; caller decides whether to treat partial success as failure
- `Priority::Critical` group commands are dispatched concurrently with a single shared deadline context

### 22. Zone Proxy (v0.22.0)

- `include/rcp/proxy.hpp`: transparent proxy for cascaded zonal topologies (HPC → proxy → zone MCU)
- Command routing table: zone → upstream proxy address
- Latency budget enforcement at proxy boundary; budget violation → `ErrTimeout`

### 23. Redundancy (v0.23.0)

- `include/rcp/redundancy.hpp`: `RedundantRegistry` wraps a primary and hot-standby `Registry`; promotes standby automatically on health-state change
- Heartbeat-based HPC liveness detection: standby activates if primary HPC misses N consecutive heartbeats
- Configurable promotion policy: automatic (zero-touch) or operator-confirmed
- State synchronisation: in-flight commands at failover are retried against the new primary with deduplication via `Command::id`
- New requirements: REQ-RED-001..REQ-RED-00N (ASIL-B)

### 24. Multi-HPC Federation (v0.24.0)

- `include/rcp/federation.hpp`: multiple active HPCs each owning disjoint zone subsets on the same zone bus
- `FederatedRegistry` coordinates zone ownership: each HPC registers a lease on the zones it owns; a lease server arbitrates conflicts
- Cross-HPC command forwarding: HPC-A can send a command to a zone owned by HPC-B via the federation layer; transparent to the caller
- Ownership transfer: zones can be migrated between HPCs at runtime (e.g. powertrain HPC hands off body zones during shutdown)

---
### Phase 8 — Tooling
---

### 25. Observability (v0.25.0)

- `include/rcp/observe.hpp`: OpenTelemetry trace spans for every `send` and `subscribe` call
- Prometheus-compatible metrics: command latency histogram, error rate, zone health gauge, power state distribution, deadline miss counter
- `monitor` subcommand in `rcptool` for live zone status dashboard

### 26. Admin API (v0.26.0)

- `include/rcp/admin.hpp`: HTTP admin interface
- `GET /zones` — list all registered zones with health, power state, and last-seen timestamp
- `GET /zones/{zone}` — single-zone detail: health history, command rate, deadline miss count
- `POST /zones/{zone}/send` — send a command via JSON body; returns response JSON
- `GET /events` — SSE stream of all health, power, and deadline-miss events
- `GET /metrics` — Prometheus scrape endpoint
- Bearer auth enforced on all write endpoints (depends on v0.19.0 Authorization)

### 27. Record & Replay (v0.27.0)

- `include/rcp/record.hpp`: records all `Command`, `Response`, and `Status` streams to a structured binary log on disk
- Ring-buffer mode for always-on black-box recording with configurable retention window
- Replay: feed a recorded log back through any `Controller`/`Registry` implementation for regression testing against a new version
- `rcptool record` and `rcptool replay` subcommands
- Log format is append-only and checksummed — suitable as FuSa incident forensics evidence

### 28. Config (v0.28.0)

- `include/rcp/config.hpp`: YAML/JSON zone registry configuration (zone ID, transport, address, certificates)
- Hot-reload of zone addresses without restart via filesystem watcher

### 29. Code Generation (v0.29.0)

- Zone manifest schema (YAML/JSON): declares zone IDs, supported command types, payload schemas, and ASIL levels
- `rcptool gen <manifest.yaml>` generates typed C++ controller stubs with `// fusa:req` annotations pre-populated
- Generated stubs implement the `Controller` interface; the generator emits matching `_test.cpp` skeletons and `.fusa-reqs.json` entries
- Eliminates hand-written boilerplate when adding a new zone type; keeps requirements, code, and tests in sync from declaration

### 30. Dynamic Data (v0.30.0)

- `include/rcp/dyndata.hpp`: runtime payload schema registry: named types (e.g. `"braking.BrakeCommand"`) registered with a C++ struct and a codec at startup
- `DynamicPayload` carries a schema name alongside raw bytes; `decode<T>(p)` reconstructs the typed value without compile-time knowledge of all payload types
- Admin API and `rcptool monitor` display decoded payload fields when a matching schema is registered; fall back to hex for unregistered types
- Code generation (v0.29.0) emits `register_schema` calls for each declared payload type, wiring the two features together
- Useful for cloud tools and dashboards that connect after deployment and need to interpret payloads without a recompile

---
### Phase 9 — Remote Access
---

### 31. gRPC Bridge (v0.31.0)

- `include/rcp/grpcbridge.hpp`: gRPC transport for cloud-connected zone controllers and remote HPC diagnostic access
- `Subscribe` server-streaming RPC: cloud consumer receives `Status` updates in real time
- `Send` unary RPC: remote caller dispatches a `Command` and receives the `Response`
- Bearer auth interceptors; filter and transform hooks; YAML config
- Enables remote diagnostic tools and cloud dashboards to interact with zone controllers without a local HPC connection

### 32. REST Bridge (v0.32.0)

- `include/rcp/restbridge.hpp`: HTTP/SSE bridge for browser-based tooling and cloud integration
- `POST /zones/{zone}/commands` — dispatch a `Command` as JSON; returns `Response` JSON
- `GET /zones/{zone}/status` — SSE stream of `Status` updates for a single zone
- `GET /zones` — SSE stream of all zone health and power-state change events
- Bearer auth on all write endpoints; CORS support for browser clients
- Complements the gRPC bridge: REST/SSE for interactive dashboards and scripts; gRPC for high-throughput cloud consumers

---
### Phase 10 — Automotive Protocol Bridges
---

### 33. SOME/IP Bridge (v0.33.0)

- `include/rcp/someipbr.hpp`: bridge `CommandType::Set`/`CommandType::Get` to SOME/IP service method calls via cpp-SOMEIP
- Bidirectional: SOME/IP events → RCP `Status` updates

### 34. CAN Bridge (v0.34.0)

- `include/rcp/canbr.hpp`: bridge `CommandType::Set`/`CommandType::Get` to CAN frames via cpp-CAN
- Configurable CAN ID mapping per zone and command type

### 35. DDS Bridge (v0.35.0)

- `include/rcp/ddsbr.hpp`: bridge RCP `Status` updates to DDS topics via cpp-DDS (sensor-fusion consumers receive zone telemetry as typed DDS samples)
- Bridge DDS samples → RCP `CommandType::Set`/`CommandType::Get` for ADAS pipeline → zone actuator control
- Bidirectional QoS mapping: DDS Reliability/Durability → RCP `Priority`

### 36. MQTT Bridge (v0.36.0)

- `include/rcp/mqttbr.hpp`: bridge RCP `Status` to MQTT topics for cloud telemetry and fleet management via cpp-mqtt
- Bridge MQTT command messages → RCP `CommandType::Set` for remote zone actuation
- Configurable topic prefix per zone (e.g. `rcp/zone/front-left/status`)

### 37. LIN Bridge (v0.37.0)

- `include/rcp/linbr.hpp`: bridge `CommandType::Set`/`CommandType::Get` to LIN frames via cpp-LIN for low-bandwidth zone actuators (seat motors, mirror adjustment, window regulators)
- Configurable LIN frame ID and field mapping per zone and command type
- LIN schedule table management: RCP commands inserted as unconditional or event-triggered frames

### 38. UDS Bridge (v0.38.0)

- `include/rcp/udsbr.hpp`: bridge RCP commands to ISO 14229 UDS service calls for zone controller diagnostics
- `CommandType::Reset` → UDS ECUReset (0x11); `CommandType::Get` → ReadDataByIdentifier (0x22); `CommandType::Set` → WriteDataByIdentifier (0x2E)
- Configurable UDS addressing mode per zone (physical, functional, extended)
- UDS negative response codes surfaced as typed `ResponseStatus` values

### 39. DoIP Bridge (v0.39.0)

- `include/rcp/doipbr.hpp`: ISO 13400 Diagnostics over IP transport for workshop and EOL diagnostic access to zone controllers
- `DoIPController` implements the `Controller` interface; routes `CommandType::Get`/`CommandType::Reset` to UDS services over the DoIP wire protocol
- Logical address and routing activation management per zone
- Enables `rcptool` to act as a DoIP tester for factory-floor zone controller flashing and diagnostics

---
### Phase 11 — Platform
---

### 40. RTOS / Bare-Metal (v0.40.0)

- Zone controller client library targeting Zephyr, FreeRTOS, and NuttX RTOS
- Pure C API header (`include/rcp/capi.h`) with `extern "C"` linkage; no heap allocation on the RTOS side: all buffers statically allocated at compile time
- Implements the same UDP framing, E2E protection, and watchdog protocol as the C++ library
- Integration test: Zephyr zone controller on QEMU communicating with cpp-RCP HPC over loopback

---
### Phase 12 — Certification & Formal Methods
---

### 41. Formal Verification (v0.41.0)

- TLA+ specification of the zone health state machine (Healthy → Degraded → Faulted → Recovering)
  - Properties verified: no deadlock, no livelock; liveness (a zone that becomes healthy is eventually detected as Healthy); safety (a Faulted zone is detected within 2× the watchdog period)
- TLA+ specification of the watchdog protocol: HPC sends `CommandType::Watchdog` at interval T; zone resets if no kick arrives within deadline D
  - Properties verified: if kicks cease, zone reaches Faulted within D + network round-trip; a resumed kick stream returns zone to Healthy
- TLA+ specification of the anti-replay guard: sequence counter window W; frames outside the window are rejected
  - Properties verified: no valid in-window frame is ever rejected; a replayed frame is always rejected
- Model-checking results and counter-example traces published in `FORMAL_VERIFICATION.md` — ASIL-D evidence that the safety state machines are correct by construction
- `tla/` directory contains all `.tla` and `.cfg` files; reproducible via the TLC model checker

### 42. ISO 21434 / Cybersecurity (v0.42.0)

- Threat Analysis and Risk Assessment (TARA) covering command injection, replay attacks, rogue zone controller registration, OTA firmware tampering, and denial-of-service via command flooding
- Security requirements mapped to TARA findings; implemented controls (TLS, Authorization, E2E replay guard, rate limiting, mDNS authentication) traced as countermeasures
- IEC 62443 SL-2 gap report (`iec62443-gap-report.json`) — closes open items from `.fusa-iec62443.json`
- Penetration test evidence: structured attack scenarios against UDP, TLS, admin HTTP, gRPC, and REST endpoints
- `TARA.md` and `CYBERSECURITY.md` published alongside the safety case

### 43. Certification (v0.43.0)

- ASIL-D gap analysis report (decomposition paths from current ASIL-B)
- Structural coverage report: statement, branch, MC/DC
- Audit pack for customer qualification (requirements traceability matrix, FMEA, safety case, TARA cross-reference, formal verification summary)
