# cpp-RCP

A C++17 library implementing the Remote Control Protocol (RCP) for zonal control in automotive systems.

RCP connects a high-performance central computer to distributed Ethernet-based zone controllers, keeping application logic centralised while remote zones provide access to local I/O, sensors, CAN/LIN gateways, and actuators.

Feature and API equivalent of [go-RCP](https://github.com/SoundMatt/go-RCP).

[![CI](https://github.com/SoundMatt/cpp-RCP/actions/workflows/ci.yml/badge.svg)](https://github.com/SoundMatt/cpp-RCP/actions/workflows/ci.yml)
[![DCO](https://github.com/SoundMatt/cpp-RCP/actions/workflows/dco.yml/badge.svg)](https://github.com/SoundMatt/cpp-RCP/actions/workflows/dco.yml)

## Headers

| Header | Description |
|---|---|
| `<rcp/rcp.hpp>` | Core interfaces: `Controller`, `Registry`, `Command`, `Response`, `Status`, `Zone`, `Context`, `StatusChannel` |
| `<rcp/mock.hpp>` | In-process mock controller and registry — zero I/O, default for unit tests |

## Build

Requires CMake 3.21+ and a C++17 compiler.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## Quick start

```cpp
#include <rcp/rcp.hpp>
#include <rcp/mock.hpp>
#include <cassert>

int main() {
    auto reg = rcp::mock::new_registry();

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
