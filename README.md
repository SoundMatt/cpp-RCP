# cpp-RCP

A C++17 library implementing the Remote Control Protocol (RCP) for zonal control in automotive systems.

RCP connects a high-performance central computer to distributed Ethernet-based zone controllers, keeping application logic centralised while remote zones provide access to local I/O, sensors, CAN/LIN gateways, and actuators.

Feature and API equivalent of [go-RCP](https://github.com/SoundMatt/go-RCP).

[![CI](https://github.com/SoundMatt/cpp-RCP/actions/workflows/ci.yml/badge.svg)](https://github.com/SoundMatt/cpp-RCP/actions/workflows/ci.yml)
[![DCO](https://github.com/SoundMatt/cpp-RCP/actions/workflows/dco.yml/badge.svg)](https://github.com/SoundMatt/cpp-RCP/actions/workflows/dco.yml)

## Headers

cpp-RCP ships ~40 public headers under `include/rcp/` and `include/relay/`,
grouped below by concern (matching the RELAY spec §13.7.2 module-name
registry categories where applicable). Every header is self-contained and
documented with a header-comment; this table is an index, not a full API
reference.

### Core / RELAY integration

| Header | Description |
|---|---|
| `<rcp/rcp.hpp>` | Core interfaces: `Controller`, `Registry`, `Command`, `Response`, `Status`, `Zone`, `Context`, `StatusChannel` |
| `<rcp/adapt.hpp>` | `Adapt()` — the RELAY §10.3 entry point, wraps a `Controller` as a `relay::Caller`; `ToMessage`/`FromMessage` conversions |
| `<relay/relay.hpp>` | `relay::` namespace types (§18.2): `Protocol`, `Message`, `Errc` sentinels, `Channel<T>`, `Context`, `Node`, `Caller` |
| `<rcp/mock.hpp>` | In-process TC18 RC Server simulator (lifecycle + register map + GPIO/SPI) — zero I/O, default for unit tests (ROADMAP.md v2.12.0) |
| `<rcp/legacy_mock.hpp>` | Pre-replacement in-process `Controller`/`Registry`, kept only for the old-model dependents not yet rebound to the new request model (`capi_impl.hpp`/`cli.hpp`/`config.hpp`, v2.16.0) |
| `<rcp/cli.hpp>` | RELAY-conformant CLI (§11/§12): `version`/`capabilities`/`status`/`send`; `cli/main.cpp` is a thin wrapper around it |
| `<rcp/version.hpp>` | Binary version string — single source of truth for the CLI |
| `<rcp/capi.h>` / `<rcp/capi_impl.hpp>` | C ABI / FFI surface for RTOS/bare-metal targets (Zephyr/FreeRTOS) |

### Protocol bridges

| Header | Description |
|---|---|
| `<rcp/canbr.hpp>` | CAN / CAN-FD bridge — maps RCP commands to CAN frames via SocketCAN |
| `<rcp/linbr.hpp>` | LIN bridge — maps RCP commands to LIN master-frame requests |
| `<rcp/ddsbr.hpp>` | DDS bridge — publishes RCP commands as DDS typed topics |
| `<rcp/mqttbr.hpp>` | MQTT bridge — publishes commands to MQTT topics |
| `<rcp/someipbr.hpp>` | SOME/IP bridge — routes commands over SOME/IP service discovery |
| `<rcp/restbridge.hpp>` | REST/HTTP bridge — maps commands to `POST /zones/{zone}/command` |
| `<rcp/grpcbridge.hpp>` | gRPC bridge — translates RCP wire frames to gRPC unary/streaming RPCs |
| `<rcp/doipbr.hpp>` | DoIP (ISO 13400) bridge — encapsulates UDS requests over TCP/IP |
| `<rcp/udsbr.hpp>` | UDS (ISO 14229) bridge — wraps RCP commands as UDS service requests |

### RCP control-plane concerns

| Header | Description |
|---|---|
| `<rcp/authz.hpp>` | Command-level access control (ISO 21434 / IEC 62443 SL-2) |
| `<rcp/e2e.hpp>` | End-to-end communication protection (ISO 26262 Part 7 E2E profile) |
| `<rcp/deadline.hpp>` | Liveness deadline monitor for zone controller Status streams |
| `<rcp/watchdog.hpp>` | Watchdog keeper — periodic `CommandType::Watchdog` kicks (ASIL-B) |
| `<rcp/federation.hpp>` | Multi-HPC federation: cross-HPC zone forwarding with lease-based ownership |
| `<rcp/redundancy.hpp>` | Hot-standby registry and HPC failover for ASIL-B fault tolerance |
| `<rcp/firmware.hpp>` | Zone controller OTA firmware update session |
| `<rcp/faultinject.hpp>` | Structured fault injection for validating safety mechanisms |
| `<rcp/config.hpp>` | Zone registry loader from JSON/YAML configuration files |
| `<rcp/admin.hpp>` | In-process Admin API: zone listing, SSE events, Prometheus metrics |
| `<rcp/ratelimit.hpp>` | Per-zone token-bucket admission control against command flooding |
| `<rcp/powerstate.hpp>` | Zone controller power state manager (Sleep/Wake) |
| `<rcp/prioqueue.hpp>` | Per-zone priority queue honouring `Priority::Critical` > `High` > `Normal` |
| `<rcp/proxy.hpp>` | Transparent zone proxy for cascaded zonal topologies |
| `<rcp/zonegroup.hpp>` | Atomic multi-zone command broadcast with typed zone group sets |
| `<rcp/dyndata.hpp>` | Runtime schema registry and dynamic payload encoding |
| `<rcp/loan.hpp>` | `LoaningController` — zero-copy payload loaning via a pre-allocated pool |
| `<rcp/record.hpp>` | Binary record and replay of RCP traffic |
| `<rcp/observe.hpp>` | OpenTelemetry-style observability: spans, gauges, counters |
| `<rcp/mdns.hpp>` | mDNS/DNS-SD zone controller discovery (RFC 6762/6763) |

### Transports

| Header | Description |
|---|---|
| `<rcp/udp.hpp>` | Native IEEE 1722-over-UDP/IP transport (Annex J): `Server`/`Client` carry AVTPDU (NTSCF/TSCF) + ACF_ABB/ACF_GBB frames over UDP sockets (ROADMAP.md v2.13.0) |
| `<rcp/tls.hpp>` | Mutual TLS transport for zone controller communication |
| `<rcp/shmem.hpp>` | Zero-copy intra-host command delivery via shared in-process memory |
| `<rcp/tsn.hpp>` | IEEE 802.1Qbv-aware UDP transport adapter for hard real-time Ethernet |
| `<rcp/avtp.hpp>` | TC18 wire codec, framing half — IEEE 1722 AVTPDU (NTSCF/TSCF) header framing (ROADMAP.md v2.0.0) |
| `<rcp/acf.hpp>` | TC18 wire codec, message half — ACF_ABB/ACF_GBB message format (ROADMAP.md v2.0.0) |
| `<rcp/sim.hpp>` | Timing-realistic RC Server simulator for SiL/HIL testing — wraps `<rcp/mock.hpp>` with latency/jitter and Fault/Recover controls, watchdog wired via `<rcp/watchdog.hpp>` (ROADMAP.md v2.12.0) |

## Build

Requires CMake 3.21+ and a C++17 compiler.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## Quick start

The example below uses `<rcp/legacy_mock.hpp>`'s pre-replacement
`Controller`/`Registry` pair, since `rcp/rcp.hpp`'s `Zone`/`Command`/
`Response` model is itself still pre-replacement (see ROADMAP.md's
Satellite Package Disposition table). For a TC18-shaped in-process server,
see `<rcp/mock.hpp>`'s `mock::Server` instead.

```cpp
#include <rcp/rcp.hpp>
#include <rcp/legacy_mock.hpp>
#include <cassert>

int main() {
    auto reg = rcp::legacy_mock::new_registry();

    std::shared_ptr<rcp::Controller> ctrl;
    reg->lookup(rcp::Zone::FrontLeft, ctrl);

    rcp::Command cmd;
    cmd.id       = 1;
    cmd.zone     = rcp::Zone::FrontLeft;
    cmd.type     = rcp::CommandType::Set;
    cmd.priority = rcp::Priority::Normal;
    cmd.payload  = {0x01, 0x02};

    rcp::Response resp;
    auto ec = ctrl->send(rcp::Context::background(), cmd, resp);
    assert(!ec);
    assert(resp.status == rcp::ResponseStatus::OK);

    reg->close();
}
```

## Zones

| Constant | Value | Description |
|---|---|---|
| `Zone::Unknown` | 0 | Zero value / uninitialized |
| `Zone::FrontLeft` | 1 | Front-left zone controller |
| `Zone::FrontRight` | 2 | Front-right zone controller |
| `Zone::RearLeft` | 3 | Rear-left zone controller |
| `Zone::RearRight` | 4 | Rear-right zone controller |
| `Zone::Central` | 5 | Central zone controller |

## Command types

| Constant | Value | Description |
|---|---|---|
| `CommandType::Noop` | 0 | No-op / keepalive |
| `CommandType::Set` | 1 | Set an output or actuator state |
| `CommandType::Get` | 2 | Query current state |
| `CommandType::Reset` | 3 | Reset zone controller |
| `CommandType::Watchdog` | 4 | Watchdog kick |
| `CommandType::Sleep` | 5 | Request zone controller to enter low-power sleep |
| `CommandType::Wake` | 6 | Request zone controller to exit sleep |

## Error codes

Errors are returned as `std::error_code` values in the `rcp` category.

| Sentinel | Description |
|---|---|
| `rcp::ErrClosed` | Controller or registry is closed |
| `rcp::ErrNotFound` | Zone not found in registry |
| `rcp::ErrAlreadyExists` | Zone already registered |
| `rcp::ErrTimeout` | Command timed out or context expired |
| `rcp::ErrBusy` | Zone controller busy (rate limit hit) |
| `rcp::ErrZoneMismatch` | Command addressed to wrong zone |

## Safety

cpp-RCP targets deployment in automotive safety-critical environments.

- Safety standard: ISO 26262 ASIL-B / IEC 61508 SIL-2
- Security standard: IEC 62443 SL-2
- cpp-FuSa static analysis runs in CI on every PR
- All requirements are traced to tests in `.fusa-reqs.json`
- HARA, FMEA, safety case, and SBOM are regenerated on every release

See [SAFETY_PLAN.md](SAFETY_PLAN.md), [SECURITY.md](SECURITY.md), and [INCIDENT-RESPONSE.md](INCIDENT-RESPONSE.md).

## License

[Mozilla Public License v2.0](LICENSE). Copyright © Matt Jones.
