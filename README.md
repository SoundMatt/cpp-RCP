# cpp-RCP

A C++17 library implementing the OPEN Alliance TC18 Remote Control Protocol (RCP) Specification v0.5.1_RC for zonal control in automotive systems.

RCP connects a high-performance central computer (an RC Client) to distributed Ethernet-based RC Servers over IEEE 1722 AVTPDU/ACF framing, keeping application logic centralised while remote Endpoints provide access to local I/O, sensors, CAN/LIN gateways, and actuators.

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
| `<rcp/avtp.hpp>` | TC18 wire codec, framing half — IEEE 1722 AVTPDU (NTSCF/TSCF) header framing, `StreamId`, `ByteBusId` |
| `<rcp/acf.hpp>` | TC18 wire codec, message half — ACF_ABB/ACF_GBB message format, `AcfMessageInfo`. ACF_ABB Message Info is 8 contiguous octets; ACF_GBB is 16, with the 64-bit `message_timestamp` spliced between the header's two quadlets (octets 0–3 quadlet 0, 4–11 timestamp, 12–15 quadlet 1) — corrected in v2.22.0, see ROADMAP.md milestone 66 |
| `<rcp/regmap.hpp>` | RC Server register-map model (generic + functional config, EP0). The two config blocks differ in who may write them: the generic block is the RC Server's own and is root-client-only, while a non-root client may write the functional block of an endpoint allocated to it (§13.1/§13.2) — corrected in v2.23.0, see ROADMAP.md milestone 67 |
| `<rcp/lifecycle.hpp>` | RC Server lifecycle state machine |
| `<rcp/request.hpp>` | Conditional-request taxonomy (compound, triggered, timed, chained, ...) and sequencers |
| `<rcp/rcp.hpp>` | Shared primitives used codebase-wide: the `Errc` sentinel error category, `Context` (a `relay::Context` alias), and `Loan` (a generic RAII buffer-loan holder) |
| `<rcp/adapt.hpp>` | `Adapt()` — the RELAY §10.3 entry point, wraps a `RequestFn` (a client-side send-equivalent call) as a `relay::Caller`; `response_to_message`/`message_to_request` conversions, addressed by the endpoint's `byte_bus_id` (RELAY spec §15.7.5) |
| `<relay/relay.hpp>` | `relay::` namespace types (§18.2): `Protocol`, `Message`, `Errc` sentinels, `Channel<T>`, `Context`, `Node`, `Caller` |
| `<rcp/mock.hpp>` | In-process TC18 RC Server simulator (lifecycle + register map + GPIO/SPI) — zero I/O, default for unit tests (ROADMAP.md v2.12.0) |
| `<rcp/cli.hpp>` | RELAY-conformant CLI (§11/§12): `version`/`capabilities`/`status`/`send`; `send` addresses an RC Server endpoint via `--server`/`--endpoint` against the `<rcp/mock.hpp>` demo backend; `cli/main.cpp` is a thin wrapper around it (ROADMAP.md v2.16.0) |
| `<rcp/version.hpp>` | Binary version string — single source of truth for the CLI |
| `<rcp/capi.h>` / `<rcp/capi_impl.hpp>` | C ABI / FFI surface for RTOS/bare-metal targets (Zephyr/FreeRTOS): server+endpoint addressing, a caller-supplied `rcp_request_fn_t` callback, no heap allocation (ROADMAP.md v2.16.0) |

### Protocol bridges

| Header | Description |
|---|---|
| `<rcp/ddsbr.hpp>` | DDS bridge — publishes RCP requests as DDS typed topics |
| `<rcp/mqttbr.hpp>` | MQTT bridge — publishes requests to MQTT topics |
| `<rcp/someipbr.hpp>` | SOME/IP bridge — routes requests over SOME/IP service discovery |
| `<rcp/restbridge.hpp>` | REST/HTTP bridge — maps requests to `POST /endpoints/{byte_bus_id}/request` |
| `<rcp/grpcbridge.hpp>` | gRPC bridge — translates RCP wire frames to gRPC unary/streaming RPCs |
| `<rcp/doipbr.hpp>` | DoIP (ISO 13400) bridge — encapsulates UDS requests over TCP/IP |
| `<rcp/udsbr.hpp>` | UDS (ISO 14229) bridge — wraps RCP requests as UDS service requests |

CAN and LIN are native TC18 Endpoint types (see `<rcp/can.hpp>`/`<rcp/lin.hpp>`), not bridge targets, so this package carries no `canbr.hpp`/`linbr.hpp`.

### RCP control-plane concerns

| Header | Description |
|---|---|
| `<rcp/authz.hpp>` | Request-level access control (ISO 21434 / IEC 62443 SL-2) |
| `<rcp/e2e.hpp>` | End-to-end communication protection (ISO 26262 Part 7 E2E profile) |
| `<rcp/deadline.hpp>` | Liveness deadline monitor for RC Server connections |
| `<rcp/watchdog.hpp>` | Watchdog keeper — periodic per-stream kicks (ASIL-B) |
| `<rcp/redundancy.hpp>` | Hot-standby primary/standby failover for ASIL-B fault tolerance, over a pair of `RequestFn`s |
| `<rcp/faultinject.hpp>` | Structured fault injection for validating safety mechanisms, wrapping a `RequestFn` |
| `<rcp/config.hpp>` | RC Server/endpoint topology manifest loader from JSON/YAML configuration files, bootstrapping an `rcp::shmem::Registry` |
| `<rcp/admin.hpp>` | In-process Admin API: stream listing, SSE events, Prometheus metrics, over an `rcp::shmem::Registry` |
| `<rcp/ratelimit.hpp>` | Per-target token-bucket admission control against request flooding |
| `<rcp/powerstate.hpp>` | RC Server power state manager (Sleep/Wake) |
| `<rcp/dyndata.hpp>` | Runtime schema registry and dynamic payload encoding |
| `<rcp/loan.hpp>` | `loan::BufferPool` — zero-copy payload loaning via a pre-allocated pool, for AVTPDU/ACF request-building (ROADMAP.md v2.14.0) |
| `<rcp/record.hpp>` | Binary record and replay of RC-Client-level request/response traffic (ROADMAP.md v2.14.0) |
| `<rcp/observe.hpp>` | OpenTelemetry-style observability: spans and counters around a client-side send-equivalent call (ROADMAP.md v2.14.0) |
| `<rcp/mdns.hpp>` | mDNS/DNS-SD host:port discovery (RFC 6762/6763), scoped to the UDP/IP transport variant (ROADMAP.md v2.14.0) |

Multi-HPC federation, a transparent zone-proxy, atomic multi-zone broadcast,
client-side priority queuing, and OTA firmware update were part of the
retired pre-TC18 model and have no TC18 analog (see ROADMAP.md's Satellite
Package Disposition table); their headers were removed rather than adapted.

### Transports

| Header | Description |
|---|---|
| `<rcp/udp.hpp>` | Native IEEE 1722-over-UDP/IP transport (Annex J): `Server`/`Client` carry AVTPDU (NTSCF/TSCF) + ACF_ABB/ACF_GBB frames over UDP sockets (ROADMAP.md v2.13.0) |
| `<rcp/tls.hpp>` | Secure-channel option for the UDP/IP transport variant (DTLS/application-layer, `SecureClient`/`SecureServer`); the specification's own preferred link security is MACsec (802.1AE) at layer 2, which this package does not address (ROADMAP.md v2.14.0) |
| `<rcp/shmem.hpp>` | Zero-copy in-process request `Channel`/`Registry`, keyed by opaque stream_key (ROADMAP.md v2.14.0) |
| `<rcp/tsn.hpp>` | IEEE 802.1p PCP-priority hint (`apply_priority`) keyed off `rcp::request::RequestCategory`'s execution-priority ordering; prefer genuine IEEE 1722 stream reservation where available (ROADMAP.md v2.14.0) |
| `<rcp/sim.hpp>` | Timing-realistic RC Server simulator for SiL/HIL testing — wraps `<rcp/mock.hpp>` with latency/jitter and Fault/Recover controls, watchdog wired via `<rcp/watchdog.hpp>` (ROADMAP.md v2.12.0) |

## Build

Requires CMake 3.21+ and a C++17 compiler.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## Quick start

```cpp
#include <rcp/acf.hpp>
#include <rcp/adapt.hpp>
#include <rcp/mock.hpp>
#include <cassert>

int main() {
    // rcp::mock::Server is an in-process TC18 RC Server simulator — no I/O,
    // suitable for tests and this example alike. A real deployment dials
    // rcp::udp::Client (or another transport) instead and wraps its own
    // send-equivalent call the same way.
    rcp::mock::Server server;
    auto ec = server.advance_to_rcp_configured();
    assert(!ec);

    // RequestFn is the "client-side send-equivalent call" every transport
    // and control-plane decorator in this library standardizes on.
    rcp::RequestFn request_fn = [&server](const rcp::Context&,
                                           const rcp::acf::AcfMessageInfo& req,
                                           const std::vector<uint8_t>& payload,
                                           rcp::acf::AcfMessageInfo& out,
                                           std::vector<uint8_t>& out_payload) {
        return server.dispatch(/*client=*/0, req, payload, out, out_payload);
    };

    // Adapt() wraps any RequestFn as a relay::Caller (RELAY spec §10.3), so
    // application code can address the RC Server through the
    // protocol-agnostic relay::Node/relay::Caller interface.
    auto caller = rcp::Adapt(request_fn);

    relay::Message req;
    req.protocol       = relay::Protocol::RCP;
    req.id             = rcp::endpoint_id_to_relay_id(rcp::mock::kGpioByteBusId);
    req.meta["rcp.op"] = "read";

    auto [resp, call_ec] = caller->call(relay::Context::with_timeout(std::chrono::seconds(1)), req);
    assert(!call_ec);
    assert(resp.protocol == relay::Protocol::RCP);
}
```

## Error codes

Errors are returned as `std::error_code` values in the `rcp` category.

| Sentinel | Description |
|---|---|
| `rcp::ErrClosed` | Connection or resource is closed |
| `rcp::ErrNotFound` | Requested resource (e.g. a registry entry) not found |
| `rcp::ErrAlreadyExists` | Resource already registered |
| `rcp::ErrTimeout` | Request timed out or context expired |
| `rcp::ErrBusy` | Resource busy (e.g. rate limit hit) |

## Safety

cpp-RCP targets deployment in automotive safety-critical environments.

- Safety standard: ISO 26262 ASIL-C / IEC 61508 SIL-2 (corrected from ASIL-B; see `HARA.md`'s H-001 rationale)
- Security standard: IEC 62443 SL-2
- cpp-FuSa static analysis runs in CI on every PR
- All requirements are traced to tests in `.fusa-reqs.json`
- HARA, FMEA, safety case, and SBOM are regenerated on every release

See [SAFETY_PLAN.md](SAFETY_PLAN.md), [SECURITY.md](SECURITY.md), and [INCIDENT-RESPONSE.md](INCIDENT-RESPONSE.md).

## License

[Mozilla Public License v2.0](LICENSE). Copyright © Matt Jones.
