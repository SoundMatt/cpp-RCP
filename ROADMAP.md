# cpp-RCP Roadmap

Feature-equivalent C++ port of go-RCP. Each milestone tracks one or more header modules under `include/rcp/`.

---

## Milestone 1 — Core + Mock (current)

> Foundation: core types, interfaces, and in-process mock.

- [x] `include/rcp/rcp.hpp` — Core types, interfaces, sentinel errors, Context, StatusChannel
- [x] `include/rcp/mock.hpp` — In-process mock Controller and Registry
- [x] `tests/test_rcp.cpp` — Tests for REQ-ZONE, REQ-PRI, REQ-CMD, REQ-STATUS, REQ-ERR, REQ-CMDSTRUCT, REQ-RESP, REQ-STAT
- [x] `tests/test_mock.cpp` — Tests for REQ-CTRL-001…027, REQ-REG-001…013, REQ-RESP-001,002, REQ-STAT-001…004
- [x] CMake build system (CMakeLists.txt + cmake/FetchDeps.cmake)
- [x] CI workflow (build-and-test, lint, DCO, cpp-FuSa)
- [x] Safety artifacts (.fusa.json, .fusa-reqs.json, .fusa-hara.json)
- [x] README, LICENSE (MPL-2.0), CONTRIBUTING, SECURITY, INCIDENT-RESPONSE, SAFETY_PLAN

---

## Milestone 2 — Controller Middleware

> Zero-copy, safety-critical command wrappers.

- [ ] `include/rcp/loan.hpp` — Zero-copy loaning controller (REQ-LOAN-001…006)
- [ ] `include/rcp/prioqueue.hpp` — Priority-queue controller wrapper (REQ-PQ-001…008)
- [ ] `include/rcp/ratelimit.hpp` — Token-bucket rate-limit wrapper (REQ-RL-001…008)
- [ ] `include/rcp/faultinject.hpp` — Fault injection wrapper (REQ-FI-001…008)
- [ ] `include/rcp/sim.hpp` — Simulated controller with latency/jitter/watchdog (REQ-SIM-001…008)
- [ ] Tests for all middleware modules
- [ ] Requirements added to .fusa-reqs.json

---

## Milestone 3 — Safety & Reliability

> Watchdog, deadline monitoring, power management, E2E protection.

- [ ] `include/rcp/watchdog.hpp` — Watchdog Keeper with health state machine (REQ-WDG-001…008)
- [ ] `include/rcp/deadline.hpp` — Liveness/deadline Monitor (REQ-DL-001…008)
- [ ] `include/rcp/powerstate.hpp` — Sleep/Wake power state Manager (REQ-PWR-001…008)
- [ ] `include/rcp/e2e.hpp` — End-to-end protection: CRC-16, seq counter, replay guard (REQ-E2E-001…008)
- [ ] Tests for all safety modules
- [ ] Requirements added to .fusa-reqs.json

---

## Milestone 4 — Transport Layer

> Shared-memory, UDP, and mTLS transports.

- [ ] `include/rcp/shmem.hpp` — Shared-memory bus transport (REQ-SHMEM-001…008)
- [ ] `include/rcp/udp.hpp` — UDP transport: Controller, ZoneServer, Registry, frame codec (REQ-UDP-001…012)
- [ ] `include/rcp/tls.hpp` — mTLS transport: Controller, ZoneServer, Registry (REQ-TLS-001…010)
- [ ] Tests for all transport modules
- [ ] Requirements added to .fusa-reqs.json

---

## Milestone 5 — Discovery & Networking

> mDNS service discovery, TSN priority mapping.

- [ ] `include/rcp/mdns.hpp` — mDNS-SD Announcer and Browser (REQ-MDNS-001…008)
- [ ] `include/rcp/tsn.hpp` — TSN/IEEE 802.1Q PCP priority mapping (REQ-TSN-001…006)
- [ ] Tests for discovery and TSN modules
- [ ] Requirements added to .fusa-reqs.json

---

## Milestone 6 — Protocol Bridges

> Bridges to CAN, LIN, SOME/IP, DDS, MQTT, DoIP, UDS, gRPC, REST.

- [ ] `include/rcp/canbr.hpp` — CAN bus bridge
- [ ] `include/rcp/linbr.hpp` — LIN bus bridge
- [ ] `include/rcp/someip.hpp` — SOME/IP bridge
- [ ] `include/rcp/ddsbr.hpp` — DDS bridge
- [ ] `include/rcp/mqttbr.hpp` — MQTT bridge
- [ ] `include/rcp/doipbr.hpp` — DoIP bridge
- [ ] `include/rcp/udsbr.hpp` — UDS bridge
- [ ] `include/rcp/grpcbridge.hpp` — gRPC bridge
- [ ] `include/rcp/restbridge.hpp` — REST bridge

---

## Milestone 7 — Advanced Features

> Federation, authorization, observability, dynamic data, proxy, record/replay, redundancy.

- [ ] `include/rcp/federation.hpp` — Multi-registry federation
- [ ] `include/rcp/authz.hpp` — Authorization / RBAC
- [ ] `include/rcp/observe.hpp` — Observability / OpenTelemetry traces
- [ ] `include/rcp/dyndata.hpp` — Dynamic data / metadata
- [ ] `include/rcp/proxy.hpp` — Proxy controller
- [ ] `include/rcp/record.hpp` — Record/replay
- [ ] `include/rcp/redundancy.hpp` — Redundancy/failover controller
- [ ] `include/rcp/zonegroup.hpp` — Zone group abstraction

---

## Milestone 8 — Tooling & DX

> CLI tool, C API shim, code generation, config helpers, formal verification stubs.

- [ ] `include/rcp/capi.h` — C API shim for `extern "C"` integration
- [ ] `include/rcp/config.hpp` — Configuration helpers
- [ ] `include/rcp/codegen.hpp` — Code generation utilities
- [ ] `include/rcp/wire.hpp` — Wire protocol helpers
- [ ] `include/rcp/firmware.hpp` — Firmware update support
- [ ] `include/rcp/formal.hpp` — Formal verification stubs
- [ ] `include/rcp/certgap.hpp` — Certificate/gap analysis
- [ ] `include/rcp/admin.hpp` — Admin/management interface
- [ ] `include/rcp/iso21434.hpp` — ISO 21434 cybersecurity
- [ ] `cmd/rcptool/` — CLI tool

---

## Milestone 9 — Release 1.0

- [ ] All requirements traced and tested (CI gate)
- [ ] HARA, FMEA, safety case regenerated
- [ ] SBOM and provenance generated
- [ ] IEC 62443 SL-2 gap report
- [ ] ISO 26262 ASIL-B gap report
- [ ] Changelog complete
- [ ] Version tag v1.0.0
- [ ] Docker quickstart image published
