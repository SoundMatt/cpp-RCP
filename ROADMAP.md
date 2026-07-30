# cpp-RCP Roadmap

## Status: Full Protocol Replacement — Breaking Changes Ahead

A conformance gap analysis against the real industry specification this
project is named for — the OPEN Alliance TC18 Remote Control Protocol
("RCP") — found that cpp-RCP's existing implementation shares **nothing at
the wire level** with that specification. What exists today is a
self-consistent but entirely home-grown protocol: a `Zone` enum addressed
`Command`/`Response`/`Status` model over a bespoke 16-byte header, plus
roughly forty satellite packages (discovery, power-state management,
watchdog, an ad-hoc E2E CRC wrapper, priority queueing, and protocol bridges
to CAN/LIN/DoIP/UDS/SOME-IP/MQTT/DDS/gRPC/REST) built on top of it.

This roadmap plans a **full replacement**, not an incremental gap-patch: the
goal is for cpp-RCP's `RCP` to eventually **be** the OPEN Alliance TC18
Remote Control Protocol, wire-compatible with other conformant
implementations, rather than a look-alike with a similar name. That is a
deliberate, explicit decision — spelled out here so it is not a surprise
later.

**This breaks every current consumer of the library.** `rcp::Zone`,
`rcp::Command`, `rcp::CommandType`, `rcp::Response`, `rcp::Status`, the
16-byte wire frame in `rcp/wire.hpp`, and the `Controller`/`Registry`
interfaces in `rcp/rcp.hpp` do not exist in the target protocol and will be
removed, not deprecated-and-kept. Anything — application code, the CLI, the
protocol bridges, the RELAY adapter — that sends a `Command` to a `Zone` and
gets back a `Response` will need to be rewritten against the new
request/endpoint model described below. There is no incremental migration
path at the API level because the two protocols do not share a wire format,
an addressing scheme, or a data model to migrate incrementally *between*.

**No compatibility shim is planned, and that is a deliberate call, not an
oversight.** A shim mapping `Zone`/`Command`/`Response` onto TC18's
`stream_id`/`byte_bus_id`-addressed endpoint requests would have to either
(a) silently drop protocol semantics that have no equivalent on the other
side — dangerous in a codebase that targets ISO 26262 ASIL-B — or (b)
become a second protocol implementation that has to be maintained forever
alongside the real one, which defeats the entire point of this replacement.
cpp-RCP has no external fleet of already-deployed zone controllers speaking
the old protocol that this repository is aware of; this is a greenfield
rewrite relative to the real spec, not a live migration. If that changes,
a shim should be reconsidered explicitly at that time — it is not being
ruled out forever, just not built speculatively now.

Traceability note: detailed field names, register layouts, and behavior
described below come from an internal structured extraction of the OPEN
Alliance TC18 Remote Control Protocol Specification v0.5.1_RC prepared for
this effort. Section numbers cited in parentheses (e.g. "extraction §3.2")
refer to that internal document's own section numbering for traceability
during implementation review, not to page/section numbers in the OPEN
Alliance document itself.

---

## Vision

cpp-RCP is a C++17-native implementation of the OPEN Alliance TC18 Remote
Control Protocol.

The project focuses on:

- Wire-level conformance with the real TC18 specification — an RC Client
  built on this library and an RC Server built on any other conformant
  implementation must interoperate
- Safety-first design with traceability to ISO 26262 ASIL-B requirements,
  carried forward from the pre-replacement codebase and re-scoped to the
  new requirement set as each phase lands
- Modern C++ developer experience — header-only core, pure interfaces,
  swappable transports (native IEEE 1722 over Ethernet, IEEE 1722 over
  UDP/IP, or CAN(FD/XL) as the underlying network)
- Deterministic latency suitable for hard real-time automotive contexts
- Observability and safety mechanisms by default, rebuilt on the
  spec's own primitives (per-stream watchdog, E2E CRC safe points, sequencer
  state) rather than bespoke ones

## Guiding Principles

1. Conformance first — every core-protocol phase below is scoped directly
   from the specification, not from what the old implementation happened
   to already do
2. Pure C++17 first — no OS-specific headers in core interfaces
3. Safety as a first-class concern — requirements in `.fusa-reqs.json`,
   traced to tests; re-derived against the new protocol surface rather than
   patched onto the old one
4. Dependency-ordered delivery — nothing in a later phase is built before
   the phase it depends on is usable (see sequencing rationale below)
5. Explicit about what's optional — the specification itself marks several
   features optional for "RCP version 1.0" (fragmentation; most conditional
   request kinds; enhanced cancellation); this roadmap makes an explicit,
   justified go/no-go call on each rather than silently deferring them
6. Every satellite package gets an individual decision — no package is
   carried forward, dropped, or rewritten by default; see the disposition
   table near the end of this document

## Sequencing Rationale

The phase order below follows the conformance-priority guidance produced
during the gap-analysis extraction (extraction §9): the three highest-impact
structural changes are the RC Server lifecycle state machine, the discovery
mechanism, and the E2E/safe-state mechanism, because nearly everything else
in the specification — register locking, request validation, error
handling — assumes those three already exist. Endpoint types are then
sequenced simplest-first (GPIO/SPI before UART/ADC's multi-field payloads,
before LIN/CAN/ISELED's raw-bus-passthrough model), and the register-map
reorganization (generic vs. functional config split) is treated as a
mechanical but pervasive refactor that has to land before *any* endpoint
work, since every endpoint type's functional config block depends on it.

---

## Release Plan

| Phase | Version | Theme | Summary |
|---|---|---|---|
| **Phase 13** | v2.0.0 | Wire format core | IEEE 1722 AVTPDU/NTSCF/TSCF framing, ACF_ABB/ACF_GBB, `byte_message_info` header, stream/byte_bus_id addressing |
| **Phase 13** | v2.1.0 | RC Server lifecycle | 3-state lifecycle machine, register-map model, generic/functional config split, EP0 |
| **Phase 13** | v2.2.0 | Discovery | Discovery request/response, discovery-stream claiming, `Discovery_TimeOut` |
| **Phase 13** | v2.3.0 | Basic endpoints I | GPIO and SPI — simplest request/response shapes |
| **Phase 13** | v2.4.0 | Basic endpoints II | I²C, UART, ADC, PWM_OUT, PWM_IN |
| **Phase 13** | v2.5.0 | Conditional requests | Compound / compound-wait / triggered / chained / timed requests, sequencers |
| **Phase 13** | v2.6.0 | E2E safe points | CRC32 safe-point mechanism, safety-request (`0x8x`) variants, per-stream watchdog/safe-state config |
| **Phase 13** | v2.7.0 | Remaining endpoints | LIN, CAN (incl. CAN XL), ISELED, MDIO, Wakeup control; DAC explicitly out of scope |
| **Phase 13** | v2.8.0 | Fragmentation decision | Explicit go/no-go call — **no-go for this cycle** (see rationale) |
| **Phase 14** | v2.9.0 | Power management rebuild | Normal/StandBy/Sleep/Unpowered model, cold/hot start, replaces `powerstate.hpp` |
| **Phase 14** | v2.10.0 | Watchdog & liveness rebuild | Per-stream watchdog config replaces the `CommandType::Watchdog` client-kick model |
| **Phase 14** | v2.11.0 | Authz & admission rebind | `authz.hpp`, `ratelimit.hpp` rebound to endpoint/stream addressing |
| **Phase 14** | v2.12.0 | Test & simulation rebuild | `mock.hpp`, `sim.hpp` rebuilt as RC Server simulators |
| **Phase 15** | v2.13.0 | Native transport rebuild | `udp.hpp` / `wire.hpp` rebuilt to carry real AVTPDU frames (IEEE1722-over-UDP/IP, Annex J) |
| **Phase 15** | v2.14.0 | Auxiliary transport rebind | `mdns.hpp`, `tls.hpp`, `tsn.hpp`, `shmem.hpp`, `loan.hpp`, `record.hpp`, `observe.hpp` |
| **Phase 15** | v2.15.0 | App-layer bridge rebind | `mqttbr`, `ddsbr`, `someipbr`, `restbridge`, `grpcbridge`, `udsbr`, `doipbr` |
| **Phase 15** | v2.16.0 | C ABI & CLI rebuild | `capi.h`/`capi_impl.hpp`, `cli.hpp` rebuilt against the new request model |
| **Phase 16** | v2.17.0 | Deprecation sweep | Remove packages with no TC18 analog (see disposition table) |
| **Phase 16** | v2.18.0 | Certification refresh | HARA/TARA/FMEA/formal verification/audit pack regenerated against the new requirement set |
| **Phase 16** | v3.0.0 | **TC18 RCP — General Availability** | First release where cpp-RCP *is* the OPEN Alliance TC18 Remote Control Protocol |

---

## Milestones

---
### Phase 13 — TC18 Wire Format & Server Core
---

This phase is one continuous breaking rewrite of the protocol core; the
version numbers below mark internal milestones within it, not independently
stable public releases — nothing in Phase 13 is wire-conformant until v2.6.0
lands (the point at which the mandatory baseline plus safe-points exist),
and full endpoint coverage is not reached until v2.7.0.

### 44. Wire Format Core (v2.0.0)

**Done (v2.0.0):** `rcp/wire.hpp` is now the IEEE 1722 AVTPDU/ACF codec
described below — NTSCF/TSCF headers, ACF_ABB/ACF_GBB messages, the shared
`AcfMessageInfo` ("byte_message_info") fields, `StreamId`/`byte_bus_id`
addressing with an enforced echo-back rule, the mandatory standard request
kind, the four response semantic types, and `avtp_timestamp`/
`message_timestamp` fallback handling via `effective_timestamp`. It has no
dependency on `rcp.hpp`'s Zone/Command/Controller/Registry model or on any
later-phase lifecycle/discovery/endpoint behavior, and makes no assumption
about the underlying transport. The old 16-byte frame codec this file used
to define is preserved unchanged, under `rcp/legacy_wire.hpp`, purely so
`rcp/udp.hpp` keeps building and working until it is rebuilt against real
AVTPDU framing at v2.13.0 — see the disposition table entry for `wire.hpp`
and the header comment in `legacy_wire.hpp`. New coverage lives in
`tests/test_wire.cpp` (REQ-WIRE-001..014); the prior 16-byte-frame tests
moved to `tests/test_legacy_wire.cpp` unchanged (REQ-UDP-001..012). Full
bit-for-bit wire conformance is not claimed at this milestone — that lands
incrementally through v2.6.0 per the Phase 13 introduction above.

- Replace the bespoke 16-byte `wire.hpp` header entirely with real IEEE 1722
  framing: NTSCF (non-time-synchronous, server-only-outbound) and TSCF
  (time-synchronous, client-only) AVTPDU headers (extraction §2.2)
- Implement the two ACF message types RCP defines on top of IEEE 1722:
  ACF_ABB (`0x0E`, no timestamp field) and ACF_GBB (`0x0D`, 64-bit
  `message_timestamp` field) (extraction §2.3)
- Implement the shared `byte_message_info` header fields common to both
  message types: `acf_msg_type`, `acf_msg_length`, `pad`, `mtv`, `rsv`,
  `byte_bus_id`, `evt` (ack flag + 3-bit sub-opcode), `hs`, `cs`,
  `transaction_num`, `op`, `rsp`, `err`, `ms`, and the dual-purpose
  `read_size`/`segment_num` field (extraction §2.4)
- Implement `stream_id` (sender MAC + locally-assigned suffix) and
  `byte_bus_id` addressing, including the rule that `byte_bus_id` is only
  unique within a stream, not globally, and must be echoed back unchanged
  on responses/acks (extraction §2.1)
- Implement the mandatory "standard request" kind (best-effort,
  unconditional, ACF_ABB) and the four response semantic types
  (acknowledge / write response / read response / error response) mapped
  onto the shared header fields (extraction §2.7, §2.8)
- Implement `avtp_timestamp` (32-bit, TSCF only) and `message_timestamp`
  (64-bit, ACF_GBB) semantics, including the timestamp-validity fallback
  rules (extraction §2.6, §2.9)
- Explicitly support running the transport over Ethernet directly,
  IEEE1722-over-UDP/IP, **or** CAN(FD/XL) as the underlying network — do not
  hard-assume a pure-Ethernet transport anywhere in this layer, since the
  specification calls out CAN-as-transport explicitly (extraction §2.1,
  §1.4)
- No dependency on any endpoint type, lifecycle state, or discovery
  behavior — this milestone is pure wire codec

### 45. RC Server Lifecycle & Register-Map Model (v2.1.0)

**Done (v2.1.0):** `rcp/lifecycle.hpp` implements the 3-state
`ServerState` machine (`HW_UNCONFIGURED`/`HW_CONFIGURED`/`RCP_CONFIGURED`)
via `ServerLifecycle`, with forward-only single-step `advance()`
transitions, the `HW_CFG_INCONSISTENT`/`RCP_CFG_INCONSISTENT` plausibility
checks gating each transition, an explicit `deconfigure()` backward path,
and independent generic/functional config-block locking queries.
`rcp/regmap.hpp` implements the register-map data model — the
generic/functional endpoint config split (`EndpointGenericConfig` /
`EndpointFunctionalConfig`), the general bootstrap fields (magic,
protocol version, vendor/device ID, endpoint count, stream/queue capacity,
`svr_implemented_options`, and the five table pointer/capacity fields), HW
pin-map config, request-stream config (including the inert `rx_wd_*`/
`rx_safety_measure` fields deferred to v2.6.0), the EP-ID/`byte_bus_id`
mapping table (with its client-ordering risk flagged in comments,
deliberately not server-enforced), response/ack queue config, and
persistent 8-bit sequencer-state storage — plus `Ep0`, the RC Server
pseudo-endpoint implementing whole-register-map read (unrestricted) and
write (root-client-only via `claim_root_client`/`svr_root_client_index`),
and per-endpoint write restriction for every other client. The four
mandatory error codes (`UNAUTHORIZED_ACCESS`, `LOCKED_MEM_ACCESS`,
`REQUEST_REJECTED`, `INVALID_PARAMETER`) are `rcp::regmap::RegMapErrc`. New
coverage lives in `tests/test_lifecycle.cpp` (REQ-LIFECYCLE-001..006) and
`tests/test_regmap.cpp` (REQ-REGMAP-001..014). `rcp/rcp.hpp`'s
pre-replacement Zone/Command/Controller/Registry model is left in place,
unmodified in behavior — its header comment now points at this milestone's
replacement headers — since roughly three dozen other headers still build
against it and are not rebound until their own later milestones (v2.9.0
onward); see the header comment in `rcp.hpp` and the disposition table.

- Implement the 3-state lifecycle machine: `HW_UNCONFIGURED` (`0x00`),
  `HW_CONFIGURED` (`0x55`), `RCP_CONFIGURED` (`0xAA`), with their documented
  transition guards, register-locking behavior, and the
  `HW_CFG_INCONSISTENT` / `RCP_CFG_INCONSISTENT` plausibility checks on
  state-advance requests (extraction §3.2)
- Implement the split between an endpoint's **generic** (server-owned,
  pin-mapping/queue-size) config block and its **functional** config block
  — this split is itself new relative to the pre-replacement design and
  every subsequent endpoint milestone depends on it existing first
  (extraction §4.2 vs. §4.4/§5.x, §6 item 5)
- Implement EP0 (RC Server as a pseudo-endpoint): whole-register-map
  read/write, the root-client concept (`svr_root_client_index`, one stream
  with full-register-map write access), and per-endpoint write restriction
  for every other client (extraction §5.1, §4.1)
- Implement the general register map fields needed to bootstrap everything
  else: magic number, protocol version, vendor/device ID, endpoint count,
  stream/queue capacity registers, `svr_implemented_options` bitmask,
  and the pointer/capacity fields for the HW pin-map, request-stream,
  response-stream, EP-ID mapping, and functional-config tables (extraction
  §3.6)
- Implement HW pin-mapping config (extraction §3.7), request-stream config
  including the `rx_*` fields needed later for watchdog/safe-state
  (extraction §3.8 — full behavioral wiring of the safety-relevant `rx_wd_*`
  /`rx_safety_measure` fields is deferred to v2.6.0, but the fields must
  exist in the register model now), the EP-ID/`byte_bus_id` mapping table
  (extraction §3.9, including the documented risk that ordering is
  client-guaranteed, not server-enforced — flag this explicitly in code
  comments rather than silently assuming correct ordering), and
  response/ack queue config (extraction §3.10)
- Implement sequencer-state registers as persistent 8-bit values (used
  starting in v2.5.0) (extraction §3.11, §3.16)
- Implement the mandatory error codes needed once register access exists:
  `UNAUTHORIZED_ACCESS`, `LOCKED_MEM_ACCESS`, `REQUEST_REJECTED`,
  `INVALID_PARAMETER` (extraction §3.15)

### 46. Discovery (v2.2.0)

**Done (v2.2.0):** `rcp/discovery.hpp` implements the discovery mechanism
described below on top of the three prior milestones without changing any
of them. `make_discovery_request`/`encode_discovery_request` build an
unconditional ACF_ABB read targeting `byte_bus_id 0` (`kDiscoveryByteBusId`,
matching `regmap::kEp0`) at register-map address 0 (`kDiscoveryRegisterAddress`
— the general bootstrap/magic-number field region `rcp/regmap.hpp` already
models), always wrapped in an NTSCF header, riding directly on `rcp/wire.hpp`'s
existing framing with no changes to that codec. `decode_discovery_request`
enforces the NTSCF-only rule by returning
`DiscoveryErrc::tscf_headed_request_dropped` — modeled as a decode failure,
not a flag a caller could forget to check — for any TSCF-headed frame.
`DiscoveryClaim` implements discovery-stream claiming: the first discovery
request accepted while `rcp::lifecycle::ServerLifecycle` reports
`HwUnconfigured`/`HwConfigured` reserves the stream for that client's
configuration writes (`ClaimOutcome::Claimed`); the reservation lapses after
a configurable `Discovery_TimeOut` (`kDefaultTimeout`, ~20 ms default) if no
configuration request follows (`on_configuration_request` returning `false`
once lapsed, freeing the stream for the next claimant); other clients'
concurrent discovery requests are reported as `HeldByOther`/`AlreadyHeld`
without ever touching whether their own reads get answered, since claiming
and read-answering are deliberately independent concerns in this
implementation; and a request in `RcpConfigured` is always `NotEligible`,
since the claiming mechanism itself is scoped to the two pre-RCP-configured
states. `should_answer_discovery` is a trivial always-true function over
`ServerState` — a deliberate, testable single call site documenting the
any-state answering invariant rather than an implicit assumption. New
coverage lives in `tests/test_discovery.cpp` (REQ-DISC-001..009).

- Implement the discovery request: a broadcastable ACF_ABB read addressed
  to `byte_bus_id 0`, NTSCF-only (a TSCF-headed discovery request is
  dropped), reading from register-map address 0 (extraction §3.5)
- Implement discovery-stream claiming: the first discovery request received
  in `HW_UNCONFIGURED`/`HW_CONFIGURED` reserves the discovery stream for
  configuration writes; the reservation lapses after a configurable
  `Discovery_TimeOut` (~20 ms default) if no configuration request follows;
  other clients can still read via discovery concurrently but cannot
  configure while a claim is active (extraction §3.5)
- Ensure a server answers discovery in **any** lifecycle state, per the
  mandatory baseline (extraction §3.1, §3.5)

### 47. Basic Endpoint Types I — GPIO & SPI (v2.3.0)

**Done (v2.3.0):** `rcp/endpoint.hpp` establishes the shared
endpoint-registration/request-dispatch scaffolding this milestone's own
description calls for: `ep_type` id constants (`kEndpointTypeGpio`,
`kEndpointTypeSpi`, with the remaining v2.4.0/v2.7.0 ids named in comments
rather than guessed at), `write_semantics_of` decoding
`AcfMessageInfo::evt_op` into the 8-way `WriteSemantics` enum, the
`saturating_add`/`saturating_subtract` templates implementing the
arithmetic-add/subtract clamping rule generically over the caller's
unsigned integer width, `apply_bitmask_write` covering the six
value-combining semantics (Replace/Or/And/Xor/Add/Subtract) with
Reserved rejected and Reconfigure deliberately left to each endpoint type,
and `TriggerRegistry`, a generic enable/notify/drain trigger-signal table.
`rcp/gpio.hpp` implements GPIO (`ep_type 0x02`): a 4-byte big-endian pin
bitmask payload (`encode_gpio_payload`/`decode_gpio_payload`),
`apply_gpio_write` completing the 8-way semantics by retargeting
Reconfigure at a separate pin-direction mask instead of the pin-value
bitmask, per-pin change/rising/falling trigger signals
(`evaluate_gpio_triggers` over `TriggerRegistry`), a functional-config
codec (`encode_gpio_functional_config`/`decode_gpio_functional_config`)
interpreting `regmap::EndpointFunctionalConfig::data`'s previously-opaque
blob, and `GpioEndpoint` tying all of the above into one
request-dispatch entry point. `rcp/spi.hpp` implements SPI (`ep_type
0x03`): `channel_of` decoding `evt[2:0]` as a 0-5 channel selector (SPI's
own repurposing of that field, distinct from GPIO's write-semantics use of
it), `SpiEndpoint::transfer` recording a raw full-duplex PICO-out/POCI-in
byte exchange per channel and firing that channel's CsAssert /
TransferComplete / CsDeassert trigger signals in observation order, and
`compound_wait_matches` implementing the compound-wait status-byte
truncation rule (comparing only the first 4 of up to 20 status bytes) as
scaffolding for the v2.5.0 compound-wait request kind itself. New coverage
lives in `tests/test_endpoint.cpp` (REQ-ENDPOINT-001..006),
`tests/test_gpio.cpp` (REQ-GPIO-001..008), and `tests/test_spi.cpp`
(REQ-SPI-001..005), traced in `.fusa-reqs.json`.

- GPIO (`ep_type 0x02`): 4-byte bitmask request/response, the 8
  write-semantics selected by `evt[2:0]` (replace / OR / AND / XOR /
  reserved / add / subtract / reconfigure), per-pin change/rising/falling
  trigger signals (extraction §5.3, §4.5 Group C)
- SPI (`ep_type 0x03`): up to 6 pre-configured channels selected by
  `evt[2:0]` values `0`-`5`, raw PICO-out/POCI-in byte transfer, the
  compound-wait truncation rule (compares only the first 4 of up to 20
  status bytes), transfer-complete and per-CS assert/de-assert triggers
  (extraction §5.4, §4.5 Group A)
- These two endpoint types establish the endpoint-registration and
  request-dispatch pattern (functional config block, `evt[2:0]` decoding,
  trigger-signal table) that every later endpoint type reuses — deliberately
  sequenced first because they are the simplest fully-specified payload
  shapes
- Implement the arithmetic-add/subtract saturation rule shared by GPIO and
  PWM_OUT now, since GPIO exercises it (extraction §4.5)

### 48. Basic Endpoint Types II — I²C, UART, ADC, PWM_OUT, PWM_IN (v2.4.0)

**Done (v2.4.0):** `rcp/endpoint.hpp` gains real `ep_type` id constants for
all five endpoint types this milestone covers
(`kEndpointTypeI2c`/`kEndpointTypeUart`/`kEndpointTypePwmOut`/
`kEndpointTypePwmIn`/`kEndpointTypeAdc`), replacing the comment placeholder
milestone 47 left for them. `rcp/i2c.hpp` implements I²C (`ep_type 0x04`):
a controller-only `I2cEndpoint::transfer` recording a raw byte stream
(address bytes included, since I²C has no separate address field in this
milestone's scope), `compound_wait_matches_bits` implementing the
arbitrary-bit-sequence compound-wait match (distinct from SPI's fixed
4-of-20-byte truncation), and `i2c_mode_of` deliberately decoding only the
coarse `hs` high-speed-requested bit — the finer `i2c_mode` speed-grade
mapping is flagged as an open item in the header's own comments rather than
guessed at, pending a future spec errata pass. `rcp/uart.hpp` implements
UART (`ep_type 0x05`): independent TX/RX queues (`enqueue_tx`/`drain_tx` vs.
`rx_fill`/`handle_read`), `handle_read`'s read-size-or-timeout completion
rule, `handle_pure_read` for payload-less reads, and
`pack_frame_to_octet`/`unpack_frame_bits` for sub-octet bit-width padding;
`kMaxReadSize`/`kRxFifoCapacity`/`kTxQueueCapacity` bound both the RX FIFO
and configurable read size to a conservative single-AVTPDU ceiling,
documented explicitly as an accepted limitation tied to milestone 52's
already-final fragmentation no-go decision, not left implicit. `rcp/adc.hpp`
implements ADC (`ep_type 0x09`): `AdcEndpoint::request_reading` (SelfTimed
cadence) and `request_reading_from_trigger_queue` (ExternalTrigger cadence,
keyed to `rcp/pwm.hpp`'s PWM_IN mid-pulse signal) both drive the same
three-level `compute_average` combinator
(`adc_avg_intervals_per_request` → `adc_combine_avg_values`), request-driven
only — sampling never free-runs outside a call — with `AdcErrc::no_signal`
as the no-signal timeout/underrun path analogous to PWM_IN_NO_SIGNAL.
`rcp/pwm.hpp` implements both PWM_OUT (`ep_type 0x07`) and PWM_IN
(`ep_type 0x08`) sharing one `PwmValue{period, active_duration}` payload
struct: `PwmOutEndpoint::handle_write` reuses `rcp::endpoint::
apply_bitmask_write` directly (so GPIO's saturating add/subtract rule
applies to both fields without re-derivation) and leaves Reconfigure to that
function's own built-in rejection rather than inventing a PWM_OUT-specific
target; `PwmInEndpoint` is response-only (`handle_read`/
`record_measurement`), returns `PwmErrc::no_signal` (PWM_IN_NO_SIGNAL)
before any measurement or after `clear_signal()`, and fires the MidPulse
trigger signal `rcp/adc.hpp`'s ExternalTrigger cadence pattern is meant to
key off of. New coverage lives in `tests/test_i2c.cpp` (REQ-I2C-001..005),
`tests/test_uart.cpp` (REQ-UART-001..007), `tests/test_adc.cpp`
(REQ-ADC-001..006), and `tests/test_pwm.cpp` (REQ-PWM-001..007), traced in
`.fusa-reqs.json`.

- I²C (`ep_type 0x04`): controller-only, raw byte stream including address
  bytes, compound-wait against arbitrary received-bit-sequence match; flag
  the unresolved `i2c_mode` high-speed enum ambiguity in code comments as an
  open item pending a later spec errata pass rather than guessing at a
  mapping (extraction §5.7, §7)
- UART (`ep_type 0x05`): independent TX/RX queues, RX FIFO fill/drain
  semantics, read completion on either `read_size` reached or `uart_timeout`
  elapsed, payload-less "pure" read requests, sub-octet bit-width padding
  rules; because fragmentation is being deferred (v2.8.0), size the RX FIFO
  and bound configurable `read_size` so a single-AVTPDU response is always
  achievable — document this explicitly as the accepted limitation rather
  than leaving it implicit (extraction §5.8)
- ADC (`ep_type 0x09`): the three-level averaging model
  (`adc_sample_interval` → `adc_avg_intervals_per_request` →
  `adc_combine_avg_values`), request-driven sampling only, the two
  self-triggering cadence patterns, and the `PWM_IN_NO_SIGNAL`-style
  no-signal timeout handling equivalent for the request path (extraction
  §5.9)
- PWM_OUT (`ep_type 0x07`) and PWM_IN (`ep_type 0x08`): the shared
  period/active-duration two-field payload shape, PWM_OUT's 8-way
  write-semantics reuse from GPIO, PWM_IN's response-only read model and
  `PWM_IN_NO_SIGNAL` error path, and the mid-pulse trigger signal used later
  to key ADC sampling cadence (extraction §5.5, §5.6)

### 49. Conditional-Request Taxonomy & Sequencers (v2.5.0)

**Done (v2.5.0):** `rcp/sequencer.hpp` implements the taxonomy and state
machine below entirely as a new decode/behavior layer over already-existing
substrate — no change to `rcp/wire.hpp`'s ACF_GBB codec or
`rcp/regmap.hpp`'s `sequencer_states` storage was needed. `decode_request_type`
/`encode_request_type` implement the `mtv=0` message_timestamp-repurposing
trick (`RequestTypeOpcode`'s 8 values: the 5 conditional kinds plus the 3
cancellation kinds), gated so a genuinely timestamped ACF_GBB message
(`mtv=1`) is rejected rather than misread; `make_conditional_request` builds
the matching `AcfMessageInfo` for `rcp::wire::encode_acf_gbb` to carry
unchanged. `RequestCategory`/`category_of`/`priority_rank`/`select_next_due`
implement the seven-way execution-priority ordering (cancellation →
triggered → timed → compound → compound-wait → chained → standard) with FIFO
tie-break. `FeatureSet`/`validate_feature_bundles` enforce the compound
bundle rule exactly as the roadmap states it: compound and compound-wait can
only be claimed together, alongside clear-non-safestate cancellation and
`sequencer_count >= kMinCompoundSequencers` (4) — triggered, chained, and
timed remain independently flaggable. `SequencerTable` is the behavior layer
over `regmap::RegisterMap::sequencer_states`: `kDefaultState` (1) fills newly
grown slots (not the vector's own zero default), and `try_advance` advances a
sequencer only while it still holds its expected start value, otherwise
leaving it untouched without erroring. `RequestState`/`RequestRecord`/
`RequestLedger` implement the forward-only pending → started →
under_execution → finalized lifecycle (mirroring `rcp/lifecycle.hpp`'s own
forward-only style), the three cancellation kinds' shared semantics
(`cancel_single`/`cancel_all`, `REQUEST_CANCELED`/`REQUEST_NOT_FOUND` via
`SequencerErrc`, already-executing requests left to finish, cascade-cancel
into chained successors), and `finalize`'s two integration points: driving
`SequencerTable::try_advance` for a Compound/CompoundWait record, and
`propagate_chain_completion` evaluating each chained successor's `cs`-gated
abort-on-predecessor-error rule via `should_execute_chained`.
`compound_wait_check_of` and `should_execute_chained` implement the `cs`
field's two independent meanings (compound-wait's immediate-vs-after-change
check; chained's execute-regardless-vs-abort-on-error). This header
deliberately does not run a scheduler thread — `select_next_due`'s output and
`RequestLedger`'s transition methods are primitives for the embedding
application to drive, same as every other endpoint header in this codebase.
New coverage lives in `tests/test_sequencer.cpp` (REQ-SEQ-001..009), traced
in `.fusa-reqs.json`. Full wire-level conformance (the safety-tagged `0x8x`
variants and E2E CRC safe points this milestone's cancellation model already
carries an `is_safety` field in anticipation of) remains a v2.6.0 concern per
the Phase 13 introduction above.

- Implement the `message_timestamp`-field-repurposing trick: when
  `mtv=0` on an ACF_GBB message, the first byte of the otherwise-unused
  64-bit timestamp slot becomes a `request_type` opcode, with the
  remaining 7 bytes carrying kind-specific parameters (extraction §2.7)
- Implement all five conditional request kinds and their optional-feature
  bundling: compound (`0x0F`) and compound-wait (`0x0B`) as one bundle
  (requires ≥4 sequencers and clear-non-safestate cancellation to also be
  implemented — a repo cannot claim "compound support" without both);
  triggered (`0x0E`) and chained (`0x01`) as independently-flagged
  capabilities; timed (`0x0A`) as part of the time-sync bundle alongside
  TSCF and timestamped acks/responses (extraction §3.1, §2.7)
- Implement sequencer state registers as the supporting primitive for
  compound/compound-wait (persistent 8-bit state, default `1`, advanced
  only on compound-request finalization while the sequencer is still in its
  expected start state) (extraction §3.11, §3.14)
- Implement the three cancellation request kinds: clear-all (`0x05`,
  mandatory baseline), clear-non-safestate (`0x06`, part of the compound
  bundle), clear-single (`0x07`, part of the enhanced-cancellation bundle),
  and their shared cancellation semantics (already-executing requests
  finish; cancelling a chained request cancels its successors;
  `REQUEST_CANCELED`/`REQUEST_NOT_FOUND` error codes) (extraction §2.7)
- Implement the full request lifecycle state machine (pending → started →
  under execution → finalized) with the type-specific sub-behavior at each
  transition, and the execution-priority ordering across simultaneously-due
  requests: cancellation → triggered → timed → compound → compound-wait →
  chained → standard, ties resolved FIFO (extraction §3.14)
- Implement the `cs` (conditional-start) field's two meanings depending on
  request kind (compound-wait immediate-vs-after-change check; chained
  execute-regardless-vs-abort-on-predecessor-error) (extraction §2.4)

### 50. E2E CRC Safe Points & Safety-Request Variants (v2.6.0)

**Done (v2.6.0):** `rcp/e2e.hpp` is REPLACED in full, per the Satellite
Package Disposition table's entry for this file — the pre-replacement
CRC-16/CCITT-FALSE + sequence-counter + replay-window wrapper around
`rcp.hpp`'s `Controller` is discarded outright, not adapted; nothing else in
the tree depended on that old API, so no `legacy_e2e.hpp` split was needed
the way `rcp/legacy_wire.hpp` was at v2.0.0. `crc32`/`detail::crc32_update`
implement the specification's actual end-to-end CRC via the standard
table-less right-shifting construction for a RefIn=true/RefOut=true CRC
(polynomial `0xF4ACFB13`, init `0xFFFFFFFF`, final XOR `0xFFFFFFFF`).
`coverage_buffer`/`compute_crc` assemble and hash exactly `stream_id` +
`avtp_timestamp` (zero-filled — `std::nullopt` — under NTSCF) + the full ACF
shared header + payload; `apply_acf_length_adjustment` and
`apply_frame_length_adjustment` implement the +1 quadlet / +4 octet
pre-adjustment respectively, layered on top of `rcp/wire.hpp`'s existing ACF
codec without changing that codec's core framing. `crc_required` gates CRC
checking per message role off `regmap::EndpointGenericConfig`'s three new
`ep_req_crc_enable`/`ep_ack_crc_enable`/`ep_response_crc_enable` toggles, and
`E2eErrc::crc_error` is the `CRC_ERROR` failure path, following the
category/message pattern `RegMapErrc`/`LifecycleErrc`/`SequencerErrc`
already established. `RxStreamGuard` implements `rx_enforce_e2e`'s
per-request-drop-vs-whole-stream-latch choice; `RxSequenceGuard` implements
`rx_enforce_seq`'s monotonic check, independent of the watchdog; `RxWatchdog`
implements `rx_wd_enable`/`rx_wd_timeout_interval` overflow detection, the
safe-state latch, and `rx_wd_info_enable`'s repeating-notification flag;
`apply_watchdog_overflow`/`apply_queue_overflow` implement the
purge-normal/retain-safety queue rule for their respective triggers
(`rx_wd_safestate_enable`/`rx_ovrflw_safestate_enable`) by calling
`rcp::sequencer::RequestLedger::cancel_all(non_safestate_only=true)`
directly rather than reimplementing cancellation — that call already existed
from v2.5.0's clear-non-safestate support and needed no changes.
`endpoint_in_configured_safe_state` implements both `rx_safety_measure`
strategies (`RxSafetyMeasure::ForceHighImpedance`'s externally-asserted flag
vs. `RunSafeSequencer`'s `rx_safestate_sequencer`-reads-`rx_safe_sequencer_state`
check), and `may_execute_now` is the load-bearing safe-state gate: a
safety-tagged request is only ever eligible once that check reports true.
`rcp/regmap.hpp`'s `RequestStreamConfig` is expanded from its three
placeholder fields (`rx_wd_timeout_s`/`rx_wd_action`/`rx_safety_measure` as a
bare `uint8_t`) to the full eleven-field set the roadmap calls for, plus the
new `RxSafetyMeasure` enum; `EndpointGenericConfig` gains the three CRC
toggles above. `rcp/sequencer.hpp` gains the three MSB-set safety-tagged
opcodes — `RequestTypeOpcode::CompoundSafety` (`0x8F`), `CompoundWaitSafety`
(`0x8B`), `TriggeredSafety` (`0x8E`) — accepted by `is_valid_request_type`/
`decode_request_type` and mapped by `category_of` onto their base opcode's
existing priority category; `is_safety_variant` is the single source of
truth for which opcodes are safety-tagged, and the new `request_record_for`
factory derives `RequestRecord::is_safety` from it automatically rather than
leaving that assignment to each call site by hand. New coverage lives in
`tests/test_e2e.cpp` (REQ-E2E-001..014, entirely rewritten — the prior
REQ-E2E-001..008 described the discarded CRC-16 scheme and no longer
apply), `tests/test_sequencer.cpp` (REQ-SEQ-010..012, added), and
`tests/test_regmap.cpp` (REQ-REGMAP-009 rewritten for the expanded field
set, REQ-REGMAP-015 added), traced in `.fusa-reqs.json`. Full wire-level
conformance against other TC18 implementations is still not claimed — see
this file's own disclaimer pattern in `rcp/wire.hpp`/`rcp/regmap.hpp`/
`rcp/sequencer.hpp` — but this is the milestone the Phase 13 introduction
above names as "the point at which the mandatory baseline plus safe-points
exist."

- Implement the specification's actual end-to-end CRC: 32-bit, polynomial
  `0xF4ACFB13`, initial value `0xFFFFFFFF`, final XOR `0xFFFFFFFF`, both
  input and output reflection enabled — **not** the ad-hoc CRC-16/CCITT
  scheme the pre-replacement `e2e.hpp` used (extraction §4.7)
- Implement CRC coverage exactly as specified: `stream_id` +
  `avtp_timestamp` (zero-filled stand-in under NTSCF) + the full ACF header
  + payload, with the message-length pre-adjustment (+1 quadlet on
  `acf_msg_length`, +4 octets on the frame length field) needed to account
  for the trailing CRC bytes (extraction §4.7)
- Implement per-endpoint opt-in "safe mode" via `ep_req_crc_enable` /
  `ep_ack_crc_enable` / `ep_response_crc_enable`, and the `CRC_ERROR`
  failure path (extraction §4.4, §4.7, §3.15)
- Implement the safety-tagged (`0x8x`) MSB-set variants of compound
  (`0x8F`), compound-wait (`0x8B`), and triggered (`0x8E`) requests: these
  execute only once the endpoint is actually in its configured safe state,
  and on watchdog overflow the *normal* variants are purged from the
  endpoint's queue while the *safety* variants remain and drive the system
  through its safe state — this is the load-bearing safety mechanism new
  relative to the pre-replacement design and the reason this milestone is
  sequenced before the remaining endpoint types (extraction §2.7, §6 item 4)
- Implement the per-request-stream watchdog and safe-state config fields:
  `rx_wd_timeout_interval`, `rx_wd_enable`, `rx_wd_safestate_enable`,
  `rx_enforce_e2e` (per-request drop vs. whole-stream latch), `rx_enforce_seq`
  / `rx_seq_safestate_enable` (monotonic sequence check, distinct from and
  orthogonal to the watchdog), `rx_ovrflw_safestate_enable`,
  `rx_safety_measure` (force-high-impedance vs. run-a-sequencer-based
  safety-sequence), `rx_safestate_sequencer`, `rx_safe_sequencer_state`, and
  `rx_wd_info_enable` (repeating notification while in safe state)
  (extraction §3.8)
- This is the third of the three highest-priority structural changes
  identified during gap analysis (alongside the lifecycle state machine and
  discovery) — deliberately landed before the remaining, lower-priority
  endpoint types in v2.7.0

### 51. Remaining Endpoint Types — LIN, CAN (incl. CAN XL), ISELED, MDIO, Wakeup Control (v2.7.0)

**Done (v2.7.0):** `rcp/endpoint.hpp` gains the five remaining `ep_type` id
constants this milestone assigns (`kEndpointTypeWakeup`=`0x01`,
`kEndpointTypeLin`=`0x06`, `kEndpointTypeCan`=`0x0B`,
`kEndpointTypeIseled`=`0x0C`, `kEndpointTypeMdio`=`0x0D`), replacing the
comment placeholder milestone 48 left for them; `0x0A` (DAC) remains
deliberately unallocated per this milestone's explicit DAC-out-of-scope
call. `rcp/lin.hpp` implements the LIN commander (`ep_type 0x06`):
`LinEndpoint::transfer` is a pure raw-byte-pusher — it records exactly the
bytes passed to it in both directions and performs no checksum selection,
PID generation, or schedule-table lookup, deliberately breaking with any
frame-aware assumption the deprecated `linbr.hpp` bridge's era might imply
(that file remains untouched — DEPRECATE, not ADAPT, per the Satellite
Package Disposition table). `rcp/can.hpp` implements the CAN controller
(`ep_type 0x0B`): an explicit `FrameFormat` (Classical/Fd/Xl) selects each
format's own payload ceiling via `max_payload_for`/`validate_frame`, with no
remote-frame shape defined anywhere in the header; CAN XL's payload is
bounded by `kMaxXlPayloadSingleAvtpdu`, a conservative implementation-chosen
ceiling strictly below the specification's own 2054-byte
`kMaxXlPayloadSpec`, consistent with milestone 52's already-decided
fragmentation no-go — a payload between the two is reported via
`CanErrc::xl_payload_exceeds_single_avtpdu_bound` rather than silently
accepted or truncated; `CanBitTimingConfig` carries the three independent
per-phase register sets (arbitration/fd_data/xl_data); `CanEndpoint::receive`
matches CAN-XL frames against a separate `xl_receive_filters` bank distinct
from the general `acceptance_filters` bank; and `CanEndpoint` deliberately
exposes no `TriggerRegistry` at all — the one endpoint type in this codebase
without one, since the specification defines no trigger-signal table for
it. `rcp/iseled.hpp` implements ISELED (`ep_type 0x0C`):
`IseledEndpoint::transact` carries the Instruction/Address/Data request and
Address/Data/optional-native-CRC response shape, with `compute_native_crc8`/
`verify_native_crc` implementing that optional native CRC as a check fully
independent of — and with no dependency on — `rcp/e2e.hpp`'s general
RCP-level E2E CRC from v2.6.0, layered on top of it rather than replacing
any part of it. `rcp/mdio.hpp` implements MDIO (`ep_type 0x0D`):
`MdioRequest`/`MdioResponse` are Clause-22/Clause-45-mode-selected, with
`MdioEndpoint`'s internal `register_key` composition keeping Clause 22's
flat register address space and Clause 45's device+register address space
from ever colliding; this header defines no MDIO-specific functional-config
block, since the extraction calls for none beyond the common
generic/functional split. `rcp/wakeup.hpp` implements Wakeup control
(`ep_type 0x01`): `decode_sleep_cmd`/`WakeupEndpoint::handle_sleep_cmd`
decode the fixed `kSleepCmd` (`0xA5`) opcode via a single byte comparison
with no dependency on `rcp/sequencer.hpp`'s `RequestTypeOpcode` taxonomy;
`record_wake_source_event`/`wake_source_pins` track wake-source pin state;
and `wakeup_message_pending`/`acknowledge_wakeup` model the repeating
`WakeUp` message handshake milestone 53 (v2.9.0, Phase 14) depends on
directly. New coverage lives in `tests/test_lin.cpp` (REQ-LINEP-001..004),
`tests/test_can.cpp` (REQ-CANEP-001..007), `tests/test_iseled.cpp`
(REQ-ISELED-001..005), `tests/test_mdio.cpp` (REQ-MDIO-001..005), and
`tests/test_wakeup.cpp` (REQ-WAKEUP-001..005), traced in `.fusa-reqs.json`.
The `REQ-LINEP-*`/`REQ-CANEP-*` prefixes (rather than `REQ-LIN-*`/
`REQ-CAN-*`) are deliberate: those shorter ids are already in use by the
untouched, deprecated `linbr.hpp`/`canbr.hpp` bridge stubs' own requirements,
and this milestone's native endpoint types needed ids of their own rather
than colliding with them. DAC (`ep_type 0x0A`) has no header, no stub, and
no requirements — left entirely unbuilt per this milestone's explicit scope
call. Full wire-level conformance against other TC18 implementations is
still not claimed — see this file's own disclaimer pattern in
`rcp/wire.hpp`/`rcp/regmap.hpp`/`rcp/sequencer.hpp`/`rcp/e2e.hpp`.

- LIN commander (`ep_type 0x06`): raw-byte-pusher model with no
  frame-level concepts (no checksum selection, no PID generation, no
  schedule tables) — validate explicitly against any assumption in the
  deprecated `linbr.hpp` bridge that the endpoint understood LIN frame
  structure; it does not, per the specification, and client-side driver
  logic must absorb that responsibility instead (extraction §5.10, §6 item
  "significant behavioral-scope question")
- CAN controller (`ep_type 0x0B`): Classical/FD/XL frame formats via an
  explicit `FrameFormat` sub-field, data frames only (no remote-frame
  support), CAN XL's extra 6-byte header region and up to 2054-byte payload
  (necessarily spanning multiple AVTPDUs — see the fragmentation decision
  below), separate bit-timing register sets per phase (arbitration / FD data
  / XL data), and CAN-XL-specific acceptance/receive filters; note in code
  that the specification defines no trigger-signal table for this endpoint
  type at all, unlike every other device-facing endpoint (extraction §5.11,
  §7)
- ISELED (`ep_type 0x0C`): native ISELED daisy-chain framing
  (Instruction/Address/Data request shape, Address/Data/optional-native-CRC
  response shape), the endpoint's own native CRC as a mechanism additional
  to (not a replacement for) the general RCP-level E2E CRC from v2.6.0
  (extraction §5.12)
- MDIO (`ep_type 0x0D`): Clause-22/Clause-45-style mode-selected register
  access, essentially no type-specific functional config beyond the common
  block — note this endpoint type is fully specified but omitted from the
  specification's own informative "ten interfaces" scope list, so treat it
  as in-scope regardless of that omission (extraction §5.13, §1.2, §7)
- Wakeup control (`ep_type 0x01`): the fixed `SleepCMD` (`0xA5`) request
  distinct from the generic request taxonomy, wake-source pin monitoring,
  and the repeating `WakeUp` message handshake used during hot-start-from-
  Sleep — this is the endpoint type the Phase 14 power-management rebuild
  (v2.9.0) depends on directly (extraction §5.2, §3.3)
- **DAC is explicitly out of scope for this cycle.** The specification
  enumerates a DAC `ep_type` (`0x0A`) and a `DAC_OUT` pin signal, but no
  functional-config chapter exists anywhere in the source document — it is
  reserved-but-undefined in v0.5.1_RC. Do not build against it; revisit only
  if and when a later spec revision actually defines DAC's register model
  and request semantics (extraction §1.2, §7, §8)

### 52. Fragmentation — Go/No-Go Decision (v2.8.0)

**Done (v2.15.1):** this milestone's call was made in prose (below) before
any of its downstream consequences landed, and every one of those
consequences was already built and cross-referencing "ROADMAP.md milestone
52" as settled fact by the time this close-out PR was opened: the
single-AVTPDU UART RX-FIFO/`read_size` bound in `rcp/uart.hpp`, the
equivalent SPI framing note in `rcp/spi.hpp`, the CAN XL
`kMaxXlPayloadSingleAvtpdu` vs. `kMaxXlPayloadSpec` split in `rcp/can.hpp`,
the `ms` ("more segments") field comment in `rcp/acf.hpp` (`rcp/wire.hpp`
at the time this close-out was first drafted; split into `rcp/avtp.hpp`/
`rcp/acf.hpp` by the post-hoc naming reconciliation below, v2.7.1 — the
`ms` field itself lives in the ACF message-format half, `rcp/acf.hpp`), and
the `REQ-UART-006`/`REQ-CANEP-004` traceability entries in `.fusa-reqs.json`.
This milestone formally closes the decision itself: the
`kOptFragmentation` bit comment in `rcp/regmap.hpp` — the one remaining
place in the tree still describing the call as "pending" — now reads
consistently with the rest of the codebase (reserved, never set, per this
already-decided no-go). This milestone's designated slot, `v2.8.0`, is the
label used throughout the tree's forward references to this decision
(`rcp/uart.hpp`, `rcp/spi.hpp`, `rcp/can.hpp`, `rcp/acf.hpp`,
`rcp/avtp.hpp`, and this file's own milestone heading above) and is left
unchanged for that reason, but it was never tagged — this close-out PR sat
open while `main` moved through the v2.7.1 naming reconciliation and seven
more milestones to v2.15.0 — so `rcp::kVersion`/the CMake project version
move to `2.15.1` instead, an out-of-band patch bump on top of current
`main` rather than the unavailable `2.8.0` slot, the same convention
`v2.7.1` itself set for landing non-sequential, milestone-adjacent work.
No wire-format, endpoint, or safety-mechanism behavior changes in this
milestone — it is a documentation and version close-out only.

**Decision: no-go for this cycle.** Fragmentation (the `ms`/`segment_num`
mechanism for splitting one logical request/response across multiple
AVTPDUs) is explicitly called out in the specification itself as optional
"for RCP version 1.0" (extraction §2.5, §7) — even the specification's own
authors do not require it. Given that:

- it interacts non-trivially with the E2E CRC mechanism landed in v2.6.0
  (only the *last* segment of a fragmented message carries a CRC, computed
  across all segments combined, with its own length-accounting adjustment
  rule) — implementing it correctly means re-touching the safety-critical
  CRC code path a second time;
- every place it would matter (UART RX-FIFO sizing, CAN XL's up-to-2054-byte
  payloads, full register-map discovery reads) has a workable single-AVTPDU
  fallback that this roadmap already plans for (bounded UART `read_size`,
  documenting CAN XL frames larger than one AVTPDU as an accepted
  limitation, and chunked discovery reads); and
- cpp-RCP's certification posture (ASIL-B traceability, formal verification
  of safety state machines) benefits more from a smaller, fully-verified
  wire surface at the v3.0.0 GA milestone than from broader payload-size
  support that isn't required for conformance,

fragmentation is deferred to a dedicated future milestone, to be scoped
once the unfragmented core has shipped and been field-proven. This is
tracked as an explicit, documented limitation of the v2.x/v3.0.0 line, not
a silent omission — any endpoint whose natural payload can exceed one
AVTPDU (UART, CAN XL, full-register discovery) must have that bound
documented in its own functional-config comments once implemented.

---
### Phase 14 — Safety & Control-Plane Package Rebuild
---

These four milestones rebuild the satellite packages whose current
implementation is safety- or liveness-relevant and therefore cannot be left
running against the old Zone/Command model once Phase 13 lands — see the
disposition table below for the full reasoning per package.

### 53. Power Management Rebuild (v2.9.0)

- Replace `powerstate.hpp`'s ad-hoc `Active`/`Sleeping`/`BusOff` model
  entirely with the specification's actual `Normal`/`StandBy`/`Sleep`/
  `Unpowered` power modes, the `StandBy`-is-always-hot-start /
  `Sleep`-is-always-cold-start distinction, and the documented entry-refusal
  conditions (unacknowledged wake-up event, a non-idle endpoint, or a
  non-empty response/ack queue) (extraction §3.3, §3.4)
- Implement the hot-start-from-Sleep handshake: network-interface
  re-enablement, the repeating `WakeUp` message via the Wakeup endpoint
  (v2.7.0) until echoed or a repeat limit is hit, then re-enabling other
  response/ack queues (extraction §3.3)
- Depends on: Wakeup control endpoint (v2.7.0), RC Server lifecycle
  (v2.1.0)

**Done (v2.9.0):** `rcp/powerstate.hpp` is replaced in full, per the
Satellite Package Disposition table's entry for this file — the prior
ad-hoc `Active`/`Sleeping`/`BusOff` model built on `rcp.hpp`'s
`Zone`/`CommandType`/`Controller` is discarded, not adapted. `PowerMode`
gains the specification's actual four modes (`Normal`/`StandBy`/`Sleep`/
`Unpowered`); `start_kind_on_exit` fixes `StandBy` as always `Hot` and
`Sleep` as always `Cold`, exposed on `PowerManager` as
`pending_start_kind()`. `PowerManager::enter_standby`/`enter_sleep` apply
the three entry-refusal conditions in a fixed order — an unacknowledged
wake-up event (queried directly from `rcp::wakeup::WakeupEndpoint::
wakeup_message_pending()`), a non-idle endpoint, and a non-empty
response/ack queue — with the latter two exposed as caller-supplied
`Hooks::endpoints_idle`/`Hooks::response_ack_queues_empty` predicates
rather than a direct `rcp/regmap.hpp` dependency, mirroring
`rcp/lifecycle.hpp`'s `PlausibilityCheck` pattern. `resume_from_standby`
implements the hot-start rule directly (no handshake, straight back to
`Normal`); `begin_wake_from_sleep`/`note_wakeup_attempt_sent`/
`acknowledge_wakeup` implement the hot-start-from-Sleep handshake by
directly driving `WakeupEndpoint::wakeup_message_pending()`/
`acknowledge_wakeup()` in a repeat-until-echoed-or-limit loop
(`Config::wakeup_repeat_limit`, default 8, this implementation's own
choice), with `Hooks::reenable_network_interface`/
`Hooks::reenable_response_ack_queues` covering the handshake's step 1 and
step 3. `notify_power_removed`/`notify_power_restored` model `Unpowered`
as a hardware-driven, unconditional transition with no refusal path.
Nothing else in the tree depended on the old `powerstate::Manager` API
(only this file's own test did), so no legacy shim was needed, same as
`rcp/e2e.hpp`'s equivalent note at v2.6.0. New coverage lives in
`tests/test_powerstate.cpp` (`REQ-PWR-001..014`, entirely rewritten),
traced in `.fusa-reqs.json`. Full bit-for-bit conformance against other
TC18 implementations is not claimed — see this file's own disclaimer
pattern in `rcp/lifecycle.hpp`/`rcp/regmap.hpp`/`rcp/wakeup.hpp`.

### 54. Watchdog & Liveness Rebuild (v2.10.0)

- Replace `watchdog.hpp`'s client-driven periodic `CommandType::Watchdog`
  kick model — that command type does not exist in the target protocol —
  with the specification's per-request-stream watchdog: reset by *any*
  inbound request on that stream, tied directly into the safe-state
  mechanism from v2.6.0 rather than a separate client-side health poll
  (extraction §3.8)
- Rework `deadline.hpp`'s zone-liveness monitor to key off the
  response/ack queue's `Flush_time`-driven periodic heartbeat and/or the
  EP0 lifecycle-state-changed trigger signal, since there is no `Status`
  subscription concept to poll in the new model (extraction §3.10, §5.1)
- Depends on: RC Server lifecycle & register map (v2.1.0), E2E safe points
  (v2.6.0)

**Done (v2.10.0):** `rcp/watchdog.hpp` is replaced in full, per the
Satellite Package Disposition table's entry for this file — the prior
client-driven `CommandType::Watchdog` kick model built on `rcp.hpp`'s
`Zone`/`Command`/`Controller` is discarded, not adapted. The actual
per-stream watchdog timeout/latch primitive already existed as
`rcp::e2e::RxWatchdog` (v2.6.0); this milestone's real work is the driver
layer around it. `watchdog::StreamWatchdog::kick_from_request` resets a
stream's watchdog on every accepted inbound request regardless of kind or
safety tag, matching the "reset by *any* inbound request" rule directly;
`StreamWatchdog::check` reads a live `regmap::RequestStreamConfig` on every
poll, reports `HealthEvent::overflowed` on timeout, and — only when
`rx_wd_safestate_enable` is set — calls `rcp::e2e::apply_watchdog_overflow`
to latch safe state and purge normal (non-safety) requests from the
supplied `request::RequestLedger`, exactly the v2.6.0 purge-normal/
retain-safety rule reused unmodified. `watchdog::Manager` multiplexes
`StreamWatchdog` across every request stream the embedding application
registers, keyed by an opaque `uint64_t` (typically an
`avtp::StreamId::to_u64()`), and fans out `HealthEvent`s to subscribed
callbacks — this is the "driver/wiring layer" the roadmap called for, not
a reimplementation of watchdog detection itself.

`rcp/deadline.hpp` is likewise rebuilt in full per the disposition table's
"ADAPT" call for this file: the concept of "declare a target dead once its
liveness signal goes silent past a deadline, alive again once it resumes"
survives from the pre-replacement `Monitor`, but every concrete signal
source is rebound away from `Status`-subscription polling, which does not
exist in the target model. Two independent liveness signals feed the new
`deadline::Monitor`, matching the roadmap's own "and/or": `note_heartbeat`
for `regmap::ResponseQueueConfig::flush_time`'s periodic flush cadence, and
`note_lifecycle_change` for `lifecycle::ServerLifecycle`'s new
`subscribe_state_changed` trigger — either alone is sufficient evidence of
liveness. Both `ResponseQueueConfig::flush_time` and
`ServerLifecycle::subscribe_state_changed` are small, explicitly-scoped
additions landed at this milestone (see those headers' own v2.10.0 comments)
since neither existed in the tree before this rebuild needed them; both are
purely additive; every pre-existing field and transition rule in
`regmap.hpp`/`lifecycle.hpp` is unchanged. `deadline::Monitor::check`
evaluates every registered target independently, emitting a
`LivenessEvent` on each alive/dead transition (including the initial dead
report for a target that has never reported) and suppressing repeats,
carrying forward the pre-replacement `Monitor`'s alive/dead semantics
without its background-thread-per-zone machinery — this header, like every
other one touched since v2.6.0, provides primitives driven by the
embedding application's own clock/scheduler, not a running thread of its
own.

Nothing else in this tree depended on the old `watchdog::Keeper` or
`deadline::Monitor` APIs (only their own tests did; `rcp/redundancy.hpp`
mentions `watchdog::Keeper` only in a doc comment, with no code dependency
on it), so no legacy shim was needed, same as every other Phase 14 rebuild.
New/rewritten coverage lives in `tests/test_watchdog.cpp`
(`REQ-WDG-001..008`, entirely rewritten), `tests/test_deadline.cpp`
(`REQ-DL-001..008`, entirely rewritten), and `tests/test_lifecycle.cpp`
(`REQ-LIFECYCLE-007`, new), traced in `.fusa-reqs.json`. Full bit-for-bit
conformance against other TC18 implementations is not claimed — see this
file's own disclaimer pattern in `rcp/e2e.hpp`/`rcp/regmap.hpp`/
`rcp/lifecycle.hpp`/`rcp/powerstate.hpp`.

### 55. Authorization & Admission-Control Rebind (v2.11.0)

- Rebind `authz.hpp`'s access-policy check from
  (`Identity`, `Zone`, `CommandType`) to (`Identity`, target server, target
  endpoint, request kind) — the root-client/per-endpoint-client access
  model from v2.1.0 already provides half of this natively at the protocol
  level, so this package's job narrows to policy *on top of* that, not
  replacing it
- Rebind `ratelimit.hpp` from per-`Zone`/`Priority` token buckets to
  per-endpoint or per-stream admission control, protecting against a
  client overrunning an endpoint's finite request-queue capacity — the
  specification defines queue overflow as an unconditional drop with no
  server-side flow control, explicitly leaving overrun-avoidance as "the
  client's job" (extraction §3.14), which is exactly what this package
  already does one layer up
- Depends on: RC Server lifecycle & register map (v2.1.0)

**Done (v2.11.0):** Both `rcp/authz.hpp` and `rcp/ratelimit.hpp` are ADAPTed
in place per the Satellite Package Disposition table's entries for these
two files — the `AccessPolicy`/`PolicyEntry`/`AuthzErrc` and
`Config`/token-bucket shapes survive; only their keying axes change.
`authz.hpp`'s `PolicyEntry` and `AccessPolicy::permit` now key on
(`Identity`, target stream, target endpoint, request kind) instead of
(`Identity`, `Zone`, `CommandType`): a target stream is the same opaque
`uint64_t` per-connection key `rcp/regmap.hpp`'s `Ep0` and
`rcp/watchdog.hpp`'s `Manager` already use (typically an
`avtp::StreamId::to_u64()`), a target endpoint is an `avtp::ByteBusId`, and
a request kind is `rcp::request::RequestCategory` (v2.5.0) — already the
single taxonomy spanning the mandatory standard kind and every
conditional/cancellation kind. Per the roadmap's own framing, this package
does not reimplement `regmap.hpp`'s root-client/per-endpoint-owner access
model (v2.1.0) — it has no dependency on `regmap.hpp` at all — it adds an
independent policy gate a dispatch call site consults *in addition to*
that model. The pre-replacement `AuthController` wrapper is dropped
outright rather than rebound: there is no longer a single unified
`Controller::send()` chokepoint to wrap (that unification, if any, does
not land until the CLI/capi/adapt rebuilds at v2.16.0), so `permit`/`check`
are standalone primitives a dispatch call site invokes directly, the same
"primitives driven by the embedding application" pattern
`rcp/e2e.hpp`/`rcp/watchdog.hpp`/`rcp/request.hpp` already established.

`ratelimit.hpp`'s token bucket is now one per (target stream, target
endpoint) admission-control domain (`ratelimit::EndpointKey`), keyed the
same stream/`byte_bus_id` way, instead of one per `Controller`
instance/`Zone`; `ratelimit::Manager` multiplexes domains lazily, mirroring
`rcp/watchdog.hpp`'s `Manager` multiplexing one `StreamWatchdog` per
stream. The pre-replacement `Priority::Critical` bypass — `Priority` being
`rcp/prioqueue.hpp`'s whole client-side-priority-wrapper concept, marked
DEPRECATE outright per the disposition table, with no TC18 analog — is
replaced by an explicit `is_safety_tagged` argument callers derive from
`rcp::request::is_safety_variant` (v2.6.0): the traffic class that
ultimately drives an endpoint through its configured safe state once it
executes is the closest real analog to "must not be dropped by an
admission-control layer" here. Like every other Phase 14 primitive header,
`TokenBucket::take`/`Manager::admit` take an explicit `now_ms` rather than
reading a clock internally, the same convention `rcp/e2e.hpp`'s
`RxWatchdog` and `rcp/watchdog.hpp`'s `StreamWatchdog` already use.

Nothing else in this tree depended on the old `AuthController`/
`ratelimit::Controller` APIs (only their own tests did), so no legacy shim
was needed, same as every other Phase 14 rebuild. New/rewritten coverage
lives in `tests/test_authz.cpp` (`REQ-AUTH-001..008`, entirely rewritten)
and `tests/test_ratelimit.cpp` (`REQ-RL-001..008`, entirely rewritten),
traced in `.fusa-reqs.json`. Full bit-for-bit conformance against other
TC18 implementations is not claimed — see this file's own disclaimer
pattern in `rcp/regmap.hpp`/`rcp/request.hpp`/`rcp/e2e.hpp`/
`rcp/watchdog.hpp`.

### 56. Test & Simulation Harness Rebuild (v2.12.0)

- Rebuild `mock.hpp` as an in-process RC Server simulator implementing the
  lifecycle state machine, register map, and a representative endpoint set,
  replacing the old in-process `Controller`/`Registry` mock entirely
- Rebuild `sim.hpp` as a timing-realistic RC Server simulator for SiL/HIL
  testing, carrying forward the existing latency/jitter modeling concept
  but re-implemented against the new request/response shapes and the new
  watchdog model from v2.10.0
- Depends on: everything in Phase 13 through v2.6.0 at minimum, to have a
  representative register map and safe-state model to simulate

**Done (v2.12.0):** `rcp/mock.hpp` is replaced in full, per the Satellite
Package Disposition table's entry for this file — the prior in-process
`Controller`/`Registry` pair built on rcp.hpp's Zone/Command model is
discarded, not adapted, since zone-addressed request/response has no
analog once addressing moves to server+endpoint identifiers. `mock::Server`
holds a real `lifecycle::ServerLifecycle` (starting `HW_UNCONFIGURED`, per
v2.1.0), a real `regmap::RegisterMap` plus `regmap::Ep0` (including EP0's
unrestricted whole-map read and root-client-gated whole-map write, also
v2.1.0), and one instance each of `gpio::GpioEndpoint` and
`spi::SpiEndpoint` (v2.3.0) as its representative endpoint set — GPIO and
SPI were the natural choice, being the two endpoint types the roadmap
itself sequenced first specifically for having the simplest fully-built
request/response shapes (see milestone 47's own note). `Server::dispatch`
is the single request/response entry point: it decodes the mandatory
standard request kind's `evt[2:0]`/`op` fields (`rcp/acf.hpp`, v2.0.0),
answers EP0 reads with the register map's magic number (the one field a
`rcp/discovery.hpp`-shaped read needs by default — whole-map wire
serialization itself remains out of scope, per `regmap.hpp`'s own header
comment), rejects EP0 writes (reachable only through `ep0().write_whole_map`
directly), and gates GPIO/SPI operational traffic on the lifecycle being
`RCP_CONFIGURED` — this mock's own choice, not a rule reused from
`regmap::Ep0::check_write_access`'s config-block locking, which models a
different thing (whether an endpoint's *configuration* may still change,
not whether the endpoint may be *operated*).

The old `Controller`/`Registry` content is preserved unchanged, under
`rcp/legacy_mock.hpp`, purely so the still-untouched old-model dependents
that build against it keep working until each is rebound at its own later
milestone — `rcp/capi_impl.hpp`, `rcp/cli.hpp`, and `rcp/config.hpp` (all
v2.16.0 per the disposition table), plus every test file that used the old
mock as a generic in-process test double for its own not-yet-rebuilt
package (`firmware`/`prioqueue`/`proxy`/`record`/`redundancy`/`tsn`/
`zonegroup`/`federation`/`observe`/`faultinject`/`loan`/`admin`, most
themselves DEPRECATE/ADAPT candidates for later milestones). This is the
same file-split precedent `rcp/wire.hpp`'s `rcp/legacy_wire.hpp` established
at v2.0.0 — REQ-CTRL-*/REQ-REG-*/REQ-RESP-*/REQ-STAT-*/REQ-ERR-011 and
their `tests/test_mock.cpp` coverage move to `tests/test_legacy_mock.cpp`
unchanged, matching `REQ-UDP-*`'s equivalent move at that milestone.

`rcp/sim.hpp` is likewise replaced in full — the prior `sim::Controller`,
which implemented the full old `rcp::Controller` interface plus a
client-driven `CommandType::Watchdog` kick model, is discarded outright
(nothing else in this tree depended on it beyond its own test, so no
legacy shim was needed here, unlike `mock.hpp`'s split). `sim::Simulator`
wraps one `mock::Server` with the latency/jitter modeling concept carried
forward unchanged from the pre-replacement design (`LatencyModel::
Constant`/`Jitter`, `simulated_latency_ms()`) and the same Fault/Recover
scenario-testing concept, re-targeted at this module's
`std::error_code`/`AcfMessageInfo` response shape. Watchdog-miss detection
is wired through `rcp::watchdog::StreamWatchdog`/`Manager` (v2.10.0)
exactly as the roadmap called for: `Simulator::dispatch` kicks a
registered stream's watchdog via `Manager::on_request_received` on every
accepted request, and `poll_watchdog` forwards to `Manager::poll` for the
caller to drive on its own schedule. Per the same "primitives, not a
scheduler" convention every Phase 14 header has followed since v2.9.0/
v2.10.0, `Simulator` spawns no background thread of its own — the old
`sim::Controller`'s status/watchdog polling threads are not carried
forward; `simulated_latency_ms()` reports a delay for the caller to apply
however it sees fit rather than sleeping on the caller's behalf.

New/rewritten coverage lives in `tests/test_mock.cpp` (`REQ-MOCK-001..010`,
new) and `tests/test_sim.cpp` (`REQ-SIM-001..007`, entirely rewritten under
the same file/prefix identity as the discarded pre-replacement coverage),
traced in `.fusa-reqs.json`; the pre-replacement mock coverage lives on,
unchanged, as `tests/test_legacy_mock.cpp`. Full bit-for-bit conformance
against other TC18 implementations is not claimed, same as the equivalent
disclaimers in `rcp/regmap.hpp`, `rcp/lifecycle.hpp`, `rcp/gpio.hpp`, and
`rcp/spi.hpp`.

---
### Phase 15 — Transport & Ecosystem Bridge Migration
---

### 57. Native Transport Rebuild — UDP/IP (Annex J) (v2.13.0)

- Rebuild `udp.hpp` to encapsulate real AVTPDU frames per IEEE 1722's own
  Annex J UDP/IP encapsulation, instead of the bespoke `R`/`C`-magic 16-byte
  header it uses today — `wire.hpp` is retired outright, not adapted, since
  it *is* the old wire format this whole roadmap replaces
- Depends on: Wire format core (v2.0.0)

**Done (v2.13.0):** `rcp/udp.hpp` is replaced in full, per the Satellite
Package Disposition table's entry for this file — the bespoke `R`/`C`-magic
16-byte Zone/Command/Response/Status frame this file used to carry
(`rcp/legacy_wire.hpp`) is discarded outright, not adapted, matching the
roadmap's own call for this milestone. `udp::Frame`/`encode_frame`/
`decode_frame` compose `rcp/avtp.hpp`'s NTSCF/TSCF header codec with
`rcp/acf.hpp`'s ACF_ABB/ACF_GBB message codec into one AVTPDU, carried as
the UDP datagram payload unmodified — this implementation's own reading of
IEEE 1722 Annex J's UDP/IP encapsulation, consistent with `rcp/avtp.hpp`'s
own header comment that its framing is transport-agnostic by design; no
additional encapsulation header is layered on top. `udp::Server` binds a UDP
socket, decodes each inbound datagram as a `Frame`, dispatches the carried
ACF request to a caller-supplied `Handler` shaped to match
`rcp::mock::Server::dispatch`'s signature (v2.12.0) so that simulator can be
wired up as this transport's handler directly, and answers the sender under
the same NTSCF/TSCF header kind the request arrived under; distinct sender
addresses are assigned stable, distinct opaque client ids, first-seen order.
`udp::Client` connects to one server address and correlates each response by
the `(byte_bus_id, transaction_num)` echo rule `rcp/acf.hpp`'s
`make_response` documents, rather than a locally invented request id — the
new addressing model has no analog of the old `Command::id`. Neither class
builds on `rcp.hpp`'s `Zone`/`Command`/`Controller`/`Registry` model, per
that file's own header comment that nothing new should; `udp::Registry`'s
old Zone-keyed controller-collection role has no replacement here since
addressing moved to stream/byte_bus_id. Grepping the tree for consumers of
the old `udp::ZoneServer`/`udp::Controller`/`udp::Registry` API before this
change found none beyond this file's own (now-deleted) test and
`rcp/tsn.hpp`'s doc comment — `rcp/tsn.hpp` itself only ever depended on the
generic `rcp::Controller` interface plus a raw socket fd, never on `udp::`
types directly — so no legacy-shim split file was needed here, unlike
`rcp/mock.hpp`'s at v2.12.0; `rcp/legacy_wire.hpp` is deleted outright in the
same change. New coverage lives in `tests/test_udp.cpp` (`REQ-UDP-001..012`,
entirely rewritten under the same file/prefix identity as the discarded
pre-replacement coverage, the same convention `tests/test_sim.cpp`'s
`REQ-SIM-001..007` rewrite followed at v2.12.0), traced in `.fusa-reqs.json`.
Full bit-for-bit conformance against other TC18/Annex-J implementations is
not claimed, same as the equivalent disclaimers in `rcp/avtp.hpp`,
`rcp/acf.hpp`, and `rcp/discovery.hpp`.

### 58. Auxiliary Transport & Cross-Cutting Rebind (v2.14.0)

- `mdns.hpp`: narrow its scope to the UDP/IP transport variant specifically
  — TC18 defines its own wire-level discovery over an Ethernet-addressed
  stream (v2.2.0) that does not need a name service, so mDNS/DNS-SD only
  earns its keep when host:port discovery is separately needed for the
  IEEE1722-over-UDP/IP path
- `tls.hpp`: rebuild as a transport option specifically for the UDP/IP
  variant (as DTLS or an application-layer channel), and flag in the same
  change that the specification's own preferred link-security mechanism is
  MACsec (802.1AE) at layer 2 (extraction §3.12, §1.3), which this package
  does not currently address at all
- `tsn.hpp`: rebind its PCP-mapping from the old 3-level `Priority` enum
  (which is being removed) to the specification's own execution-priority
  ordering across request kinds (v2.5.0), and prefer genuine IEEE 1722
  stream reservation where available over socket-level `SO_PRIORITY` hints
- `shmem.hpp`: rebuild the zero-copy in-process channel against the new
  request/response shapes; the "avoid serialization overhead for
  co-located RC Client/RC Server" value proposition is unaffected by the
  protocol replacement
- `loan.hpp`: rebind zero-copy buffer loaning to whatever the new
  client-side request-building API turns out to be — likely to matter more
  here than before, since AVTPDU construction has more framing overhead to
  avoid copying than the old 16-byte header did
- `record.hpp`: rework the on-disk entry format to capture raw AVTPDU
  frames (or RC-Client-level request/response pairs) instead of
  `Command`/`Response`/`Status` structs
- `observe.hpp`: rebind span/counter recording to wrap the new client-side
  send-equivalent call; the OpenTelemetry-style approach itself is
  unaffected
- Depends on: Wire format core (v2.0.0); UDP/IP transport (v2.13.0) for the
  `mdns`/`tls` items specifically

**Done (v2.14.0):** All seven files are ADAPTed in place per the Satellite
Package Disposition table's entries — each concept survives, rebound onto
the new stream/`byte_bus_id` addressing model instead of the removed
`Zone`/`Command`/`Controller`/`Registry` types. `rcp/rcp.hpp`'s own
"nothing new should build on this" notice held throughout: none of the
seven include it for anything beyond `rcp::Context`/the generic
`ErrClosed`/`ErrNotFound`/`ErrAlreadyExists`/`ErrTimeout` error constants
(also not part of that pre-replacement model) and, for `loan.hpp`, the
already-generic `rcp::Loan` RAII buffer holder.

`mdns.hpp`'s `Discoverer`/`StaticDiscoverer`/`Announcer` interfaces are
unchanged in shape; `ZoneInfo` becomes `ServerInfo`, keyed by an opaque
`stream_key` (typically `avtp::StreamId::to_u64()`) instead of `Zone`.
`tls.hpp`'s `Config` (cert/key/ca/verify_peer) survives unchanged;
`Controller`/`ZoneServer`/`Registry` are replaced by `SecureClient`/
`SecureServer` wrapping `rcp/udp.hpp`'s `Server`/`Client` (v2.13.0), same
OpenSSL-gated-real-backend/`function_not_supported`-stub split as before.
The header now also states plainly, per this milestone's own instruction,
that the specification's preferred link-security mechanism is MACsec
(802.1AE) at layer 2 — this package addresses the UDP/IP path only and
does not implement MACsec itself.

`tsn.hpp`'s `PCPMap` now maps all seven `rcp::request::RequestCategory`
values (v2.5.0) instead of the removed `Priority` enum's three, preserving
the same cancellation > triggered > timed > compound > compound-wait >
chained > standard execution-priority ordering `rcp/request.hpp`'s
`priority_rank` already defines (extraction §3.14). The `Controller`
wrapper is dropped for a standalone `apply_priority(fd, cfg, category)`
free function — there is no unified client-side `send()` chokepoint left
to wrap (that unification, if any, does not land until the CLI/capi/adapt
rebuilds at v2.16.0, per `rcp/authz.hpp`'s equivalent v2.11.0 note) — and
its header comment now notes that genuine IEEE 1722 stream reservation
(802.1Qat SRP), where available, is preferable to this socket-level hint.

`shmem.hpp`'s `ZoneServer`/`Controller` pair becomes `Channel`/`Registry`,
keyed by `stream_key` the same way `rcp/watchdog.hpp`'s `Manager` already
is; `Channel::request()` still dispatches to a caller-supplied handler
in-process with no wire encode/decode step, the same zero-copy value
proposition as before, now shaped to match `udp::Server::Handler`/
`rcp::mock::Server::dispatch`'s signature.

`loan.hpp`'s `loan::Controller` (which wrapped the now-gone
`rcp::LoaningController`) becomes a standalone `BufferPool`, the same
"primitive, not a wrapped chokepoint" choice `tsn.hpp` makes at this
milestone — a caller draws a `rcp::Loan` from it immediately before
building an AVTPDU/ACF-framed request. Buffers are re-zeroed on reuse
(closing a small aliasing gap the pre-replacement pool didn't guarantee).

`record.hpp` and `observe.hpp` both now wrap a `RequestFn` — a
`std::function` shaped like `udp::Client::request`'s core signature (the
"new client-side send-equivalent call" this milestone calls for) — rather
than the removed `rcp::Controller`. `record::Entry` carries
`acf::AcfMessageInfo`/payload pairs for both the request and response
instead of `Command`/`Response`/`Status`; `Record::write_binary` now has a
matching `read_binary`, encoding each message via `rcp::acf::encode_acf_abb`/
`encode_acf_gbb` (the same codec the wire path itself uses) so the on-disk
format round-trips losslessly rather than only being asserted non-empty.
`observe::Span`/`Metric` swap `Zone`/`CommandType` for `byte_bus_id`/
`acf_msg_type` and an opaque `stream_key`; the OTel-style span/counter
approach itself (`MetricsSink`/`NoopSink`/`InMemorySink`) is unchanged.

Grepping the tree for consumers of each file's pre-v2.14.0 API beyond its
own (now-rewritten) test found none, so no legacy-shim split file was
needed for any of the seven, same as `rcp/udp.hpp`'s v2.13.0 rebuild.
New/rewritten coverage lives in `tests/test_mdns.cpp` (`REQ-MDNS-001..008`),
`tests/test_tls.cpp` (`REQ-TLS-001..010`), `tests/test_tsn.cpp`
(`REQ-TSN-001..006`), `tests/test_shmem.cpp` (`REQ-SHMEM-001..008`),
`tests/test_loan.cpp` (`REQ-LOAN-001..006`), `tests/test_record.cpp`
(`REQ-REC-001..008`), and `tests/test_observe.cpp` (`REQ-OBS-001..008`) —
all entirely rewritten under the same file/prefix identity as the
discarded pre-replacement coverage, the same convention `tests/test_udp.cpp`
followed at v2.13.0 — traced in `.fusa-reqs.json`. Full bit-for-bit
conformance against other TC18/Annex-J implementations is not claimed,
same as the equivalent disclaimers in `rcp/avtp.hpp`, `rcp/acf.hpp`,
`rcp/discovery.hpp`, and `rcp/udp.hpp`.

### 59. Application-Layer Protocol Bridge Rebind (v2.15.0)

- Rebind `mqttbr.hpp`, `ddsbr.hpp`, `someipbr.hpp`, `restbridge.hpp`,
  `grpcbridge.hpp`, `udsbr.hpp`, `doipbr.hpp` to the new client-side
  request/response types. These bridges translate between an RC Client's
  application-level view and another ecosystem (MQTT topics, DDS samples,
  SOME/IP methods, REST, gRPC, UDS services, DoIP) — that job is
  conceptually unaffected by which wire protocol RCP itself now speaks, so
  each of these is a mechanical interface update, not a redesign. All seven
  are currently unimplemented stubs (`function_not_supported`), so there is
  no working behavior to preserve, only an interface shape to update before
  a real implementation is ever built behind it
- `canbr.hpp` and `linbr.hpp` are **not** part of this rebind — see the
  disposition table: both are deprecated outright, because the target
  specification makes CAN and LIN native endpoint types of an RC Server
  (v2.7.0) rather than something external to bridge to from an RCP
  "Zone" — the bridge direction the old packages assumed is inverted by
  the real protocol
- Depends on: Wire format core (v2.0.0) at minimum; each bridge's own
  external dependency (an MQTT/DDS/SOME-IP/UDS library) is unchanged by
  this rework

**Done (v2.15.0):** All seven files are ADAPTed in place per the Satellite
Package Disposition table's entries — each pre-replacement
`<Name>Controller : public rcp::Controller` wrapper (keyed by the removed
`Zone` type) becomes a standalone `<Name>Bridge` class, the same
"primitives, not a wrapped chokepoint" choice `rcp/authz.hpp` (v2.11.0) and
`rcp/tsn.hpp`/`rcp/record.hpp`/`rcp/observe.hpp` (v2.14.0) already made —
there is no unified client-side `send()` chokepoint left to wrap. Addressing
moves from `Zone` to the opaque per-connection `stream_key` (typically
`avtp::StreamId::to_u64()`) plus `avtp::ByteBusId` endpoint pair every other
Phase 14/15 header keys on since v2.10.0. Each bridge's pre-replacement
`send()` becomes `request()`, shaped to match `rcp/record.hpp`'s/
`rcp/observe.hpp`'s `RequestFn` — the same "new client-side send-equivalent
call" shape as `rcp/udp.hpp`'s `Client::request` core signature — so a
caller can address a bridge exactly where it would otherwise address a
transport `Client`; the pre-replacement `subscribe()`/`StatusChannel`
method is dropped with no replacement, since it belonged to
`rcp::Controller`'s status-telemetry push model, which has no analog in the
target specification's request/response shape. Every method still returns
`errc::function_not_supported` (`close()` still succeeds) — no real
MQTT/DDS/SOME-IP/REST/gRPC/UDS/DoIP behavior is implemented here, exactly as
this milestone's own scope states; each protocol's `Config` struct is
unchanged. `canbr.hpp`/`linbr.hpp` are untouched, per this milestone's own
scope note — they remain a separate DEPRECATE item at milestone 61
(v2.17.0). Grepping the tree for consumers of each file's pre-v2.15.0 API
beyond its own (now-rewritten) test found none, so no legacy-shim split
file was needed for any of the seven, same as `rcp/udp.hpp`'s v2.13.0
rebuild and the seven files ADAPTed at v2.14.0. New coverage lives in
`tests/test_mqttbr.cpp`, `tests/test_ddsbr.cpp`, `tests/test_someipbr.cpp`,
`tests/test_restbridge.cpp`, `tests/test_grpcbridge.cpp`,
`tests/test_udsbr.cpp`, and `tests/test_doipbr.cpp` (`REQ-MQTT-001..004`,
`REQ-DDS-001..004`, `REQ-SOMEIP-001..004`, `REQ-REST-001..004`,
`REQ-GRPC-001..004`, `REQ-UDS-001..004`, `REQ-DOIP-001..004`), entirely
rewritten under the same file/prefix identity as the discarded
pre-replacement coverage, traced in `.fusa-reqs.json`.

### 60. C ABI & CLI Rebuild (v2.16.0)

- Rebuild `capi.h`/`capi_impl.hpp`: the current C structs
  (`rcp_command_t`, `rcp_response_t`, `rcp_zone_t`) are Zone/CommandType-
  specific and do not generalize by adaptation — this is a from-scratch
  redesign of the RTOS/bare-metal C surface against the new request model,
  landed last in this phase because it is the smallest-audience,
  highest-effort-per-user surface
- Rebuild `cli.hpp` (and the `cli/main.cpp` wrapper): the RELAY-conformant
  `version`/`capabilities`/`status` triad (RELAY spec §11/§12) carries
  forward unchanged in *shape*, but `send` needs a new addressing form
  (target server + endpoint instead of `--zone`), and `capabilities_json()`
  needs updated `transports`/`features` fields once the new transports and
  endpoint set exist
- Rework `rcp/adapt.hpp`'s `Adapt()`/`ToMessage()`/`FromMessage()` mappings
  so `relay::Message` continues to bridge to *something* meaningful on the
  RCP side once `Controller`/`Command`/`Zone` no longer exist — likely a
  per-endpoint request/response pair keyed by a server+endpoint identifier
  in `relay::Message.id` instead of a PascalCase zone name; `relay/relay.hpp`
  itself (the protocol-agnostic `Message`/`Channel`/`Node`/`Caller` types)
  needs no change
- Depends on: at least the basic endpoint types (v2.3.0/v2.4.0) to have
  something for `send` to address

**Done (v2.16.0):** `capi.h`/`capi_impl.hpp` are REPLACEd in full, per the
Satellite Package Disposition table's entry for this pair — the old
`rcp_zone_t`/`rcp_command_t`/`rcp_response_t`/`rcp_priority_t` types are
discarded outright, not adapted, since a from-scratch C ABI has no analog
of `Zone`/`Priority` to adapt from. The new surface addresses a request via
a caller-chosen `rcp_stream_key_t` plus an `rcp_byte_bus_id_t` (the same
pair every Phase 14/15 header has keyed on since v2.10.0); `rcp_acf_info_t`
mirrors `rcp::acf::AcfMessageInfo`'s shared-header fields (`rcp/acf.hpp`,
v2.0.0) one-for-one instead of the removed `cmd_type`/`priority` pair. An
`rcp_ctrl_h` now binds one caller-supplied `rcp_request_fn_t` C function
pointer (plus opaque userdata) to one endpoint — the C-linkage analog of
the "client-side send-equivalent call" shape `rcp/record.hpp`'s and
`rcp/observe.hpp`'s own `RequestFn` already standardize on (v2.14.0) — so
the same callback can be backed by `rcp::mock::Server::dispatch`
(v2.12.0), `rcp::udp::Client::request` (v2.13.0), or real hardware,
without `capi_impl.hpp` depending on any of them; `rcp_registry_s` becomes
a small fixed-capacity (16-entry) table of such handles keyed by
`(stream_key, byte_bus_id)` rather than growing on the heap, closing a gap
the pre-replacement registry's `std::unordered_map`-backed storage left in
its own "no heap allocation" claim. `rcp_response_t` gains a caller-
supplied `payload_cap` alongside `payload`/`payload_len`, so a response
payload is written into caller-owned memory rather than the pre-
replacement stub's `payload = nullptr` no-op; `rcp_send()` rejects a
callback that reports more bytes than `payload_cap` allowed
(`REQ-CAPI-009`, new). `rcp_subscribe`/`rcp_status_cb_t` have no
replacement, the same "no analog in the target specification's
request/response shape" call every ADAPTed Phase 14/15 bridge already
made for `Controller`'s status-telemetry push model. Grepping the tree for
consumers of the pre-v2.16.0 `capi_impl.hpp` API beyond its own (now-
rewritten) test found none, so no legacy-shim split file was needed here,
matching `rcp/udp.hpp`'s v2.13.0 precedent.

`cli.hpp` (and `cli/main.cpp`, unchanged) is ADAPTed in place: the
`version`/`capabilities`/`status` triad carries forward unchanged in
shape, as this milestone's own scope note called for. `send`'s protocol-
flags form moves from `--zone <name> --type <cmdtype>` to `--server
<stream_key> --endpoint <byte_bus_id> --op read|write [--evt-op <0-7>]`,
and both the protocol-flags and streaming NDJSON forms now dispatch
against `rcp::mock::Server` (v2.12.0) — the new RC Server simulator —
instead of `rcp/legacy_mock.hpp`'s Zone-keyed `Registry`, addressed by
`byte_bus_id` the same way that simulator's own representative endpoint
set already is. `capabilities_json()`'s `transports` becomes
`["udp","shmem","mock"]` (v2.13.0/v2.14.0/v2.12.0) and `features` becomes
the full v2.3.0/v2.4.0/v2.7.0 endpoint set plus `"loaning"`;
`optional_interfaces` drops `"LoaningController"`, which `Adapt()` no
longer wraps now that it takes a plain callable instead of a
`shared_ptr<Controller>`.

`rcp/adapt.hpp`'s `Adapt()` now takes a `RequestFn` — shaped identically
to `rcp/record.hpp`'s and `rcp/observe.hpp`'s own `RequestFn` (v2.14.0) —
rather than a `shared_ptr<Controller>`, the same "primitives, not a
wrapped chokepoint" choice every ADAPTed Phase 14/15 header already made;
there is no unified client-side `send()` chokepoint left to wrap.
`zone_to_relay_id`/`zone_from_relay_id`'s PascalCase-zone scheme in
`relay::Message.id` is replaced by `endpoint_id_to_relay_id`/
`relay_id_to_endpoint_id`, encoding a `(stream_key, byte_bus_id)` pair as
`"<16 lowercase hex digits>:<decimal>"` — this implementation's own
encoding, not something the RELAY spec mandates a shape for.
`status_to_message`/`message_to_command`/`response_to_message` are
replaced by `message_to_request`/`response_to_message`, mapping
`relay::Message.meta`'s `"rcp.op"`/`"rcp.evt_op"` keys onto
`acf::AcfMessageInfo`'s `op`/`evt_op` fields instead of the removed
`CommandType`/`Priority` pair. `subscribe()` now always reports
`std::errc::function_not_supported` — the same call `rcp/mqttbr.hpp`'s
(and its six ADAPTed siblings') dropped `subscribe()`/`StatusChannel`
method already made at v2.15.0, since the pre-replacement Status push
model this milestone retires has no analog in the target specification's
request/response shape; `close()` still succeeds unconditionally, same as
those seven bridges. `relay/relay.hpp` itself is unchanged, per this
milestone's own scope note.

`rcp.hpp` and `rcp/legacy_mock.hpp` are untouched: `rcp/config.hpp` is the
one remaining dependent of `rcp.hpp`'s pre-replacement Zone/Command/
Controller model and `legacy_mock.hpp`'s Registry, so neither file could
be deleted at this milestone; `rcp/config.hpp`'s own rebind is not part of
this milestone's stated scope and remains open. Grepping the tree for
consumers of `cli.hpp`'s/`adapt.hpp`'s pre-v2.16.0 APIs beyond their own
(now-rewritten) tests found none, so no legacy-shim split file was needed
for either. `version.hpp`/`CMakeLists.txt` are bumped to 2.16.0. Coverage
lives in `tests/test_capi.cpp` (`REQ-CAPI-001..009`, `009` new),
`tests/test_cli.cpp` (`REQ-CLI-001..005`), and `tests/test_relay.cpp`
(`REQ-RELAY-001..005`) — all rewritten in place under their existing
prefix identity, the same convention every prior REPLACE/ADAPT milestone
in Phase 13-15 followed — traced in `.fusa-reqs.json`.

---
### Phase 16 — Deprecation & Certification Refresh
---

### 61. Deprecation Sweep (v2.17.0)

Remove, rather than adapt, every package whose current abstraction has no
analog in the target specification and cannot be meaningfully repositioned
on top of it. See the disposition table for the individual justification
per package; this milestone is the mechanical removal once Phase 13-15 have
landed and nothing else in the tree still depends on these headers:
`canbr.hpp`, `linbr.hpp`, `federation.hpp`, `proxy.hpp`, `zonegroup.hpp`,
`prioqueue.hpp`, `firmware.hpp`.

**Done (v2.17.0):** All seven headers named in this milestone's scope —
`include/rcp/canbr.hpp`, `linbr.hpp`, `federation.hpp`, `proxy.hpp`,
`zonegroup.hpp`, `prioqueue.hpp`, `firmware.hpp` — are deleted outright,
along with their dedicated test files (`tests/test_canbr.cpp`,
`test_linbr.cpp`, `test_federation.cpp`, `test_proxy.cpp`,
`test_zonegroup.cpp`, `test_prioqueue.cpp`, `test_firmware.cpp`), matching
the DEPRECATE call and per-package justification already recorded in the
Satellite Package Disposition table. Before deleting, grepping the tree for
`#include` references to each of the seven confirmed zero remaining
consumers outside the header/test pair being removed; `linbr.hpp` and
`prioqueue.hpp` were each still named in passing inside doc comments in
`rcp/lin.hpp`, `rcp/ratelimit.hpp`, and `rcp/tsn.hpp` (explaining, by name,
what those ADAPTed packages replaced) and `rcp/legacy_mock.hpp` (a
historical list of this file's old dependents) — none of these are
`#include`s, so they were left as the same kind of point-in-time
archaeological record every prior REPLACE/ADAPT milestone's own comments
already are, rather than rewritten.

`tests/CMakeLists.txt` is updated to match: `canbr`/`linbr` are pulled out
of the shared bridge-stub `foreach` loop (the seven ADAPTed bridges sharing
that loop — `ddsbr`/`doipbr`/`grpcbridge`/`mqttbr`/`restbridge`/
`someipbr`/`udsbr` — are untouched), and the five standalone
`add_executable`/`target_link_libraries`/`add_test` blocks for
`federation`/`proxy`/`zonegroup`/`prioqueue`/`firmware` are deleted.

One real coupling had to be resolved rather than just deleted around:
`tests/test_config.cpp` pulled in `rcp/proxy.hpp` purely to get an
`rcp::Registry` implementation that starts empty (`legacy_mock::Registry`
always pre-populates all 5 zones, so it can't observe `config::load`
registering a zone from a clean slate) — no `ProxyController` latency-budget
behavior was ever under test there. Rather than drop the two `config::load`
test cases (and their `REQ-CFG-*` coverage) or reach back into `proxy.hpp`,
`test_config.cpp` now defines its own minimal `EmptyRegistry` test double
locally and uses that instead; `rcp/config.hpp` itself, and the fact that
its own rebind onto the new server/endpoint manifest schema remains a
separate, still-open item per the Satellite Package Disposition table's
`config.hpp` entry, are both otherwise untouched by this milestone.

`.fusa-reqs.json` is pruned of the 44 requirement entries that back the
deleted packages — `REQ-CANBR`/`REQ-LINBR` didn't end up being the real
prefixes canbr.hpp/linbr.hpp used (they were `REQ-CAN-001..004` and
`REQ-LIN-001..004`, distinct from the native CAN/LIN endpoints' own
`REQ-CANEP-*`/`REQ-LINEP-*`), plus `REQ-PROXY-001..006`,
`REQ-FED-001..008`, `REQ-ZG-001..006`, `REQ-PQ-001..008`, and
`REQ-FW-001..008`. This was necessary within this milestone rather than
deferred to #62: `cpfusa trace --req-coverage 100` (the same gate CI's
`cpfusa-trace` job enforces) treats any requirement id present in
`.fusa-reqs.json` with no corresponding `fusa:req`/`fusa:test` source
annotation as an uncovered gap, and removing the seven headers' annotations
without pruning their matching entries dropped measured coverage to 90.9%.
No other safety artifact (`.fusa-hara.json`, `.fusa-problems.json`,
`.fusa-iec62443.json`, `fmea.csv`/`fmea.json`) references any of these 44
ids, so none of them needed a corresponding edit. This is a narrowly-scoped
prune of exactly the entries this milestone's own deletions orphaned — it
is not the full regeneration against a new v2.x requirement set that
milestone #62 still owns, and does not touch the pre-replacement
`REQ-ZONE-*`/`REQ-CMD-*`/`REQ-STATUS-*` groups #62's own scope text calls
out for retirement.

`version.hpp`/`CMakeLists.txt` are bumped to 2.17.0. Full local build
(clang/gcc, C++17) and `ctest` (53/53 tests, down from 60 — the seven
removed suites) pass; `cpfusa check`/`lint`/`cyber`/`trace --req-coverage
100` all pass with 0 errors (only the same pre-existing warnings already
present before this change); RELAY `conform --strict` against the built
`cpp-rcp` CLI still passes unchanged, since this milestone touches no
wire-format or CLI-surface code. Milestone #62 (Certification Refresh,
v2.18.0) is next: it still owns the full `.fusa-*`/`fmea.*`/`HARA.md`/
`TARA-ANALYSIS.md`/`CYBERSECURITY.md`/`tla/`/`AUDIT_PACK.md` regeneration
against the complete v2.x requirement set, including retiring the older
pre-replacement `REQ-ZONE-*`/`REQ-CMD-*`/`REQ-STATUS-*` groups that this
milestone did not touch.

### 62. Certification Refresh (v2.18.0)

- Regenerate `.fusa-reqs.json`, `.fusa-hara.json`, `HARA.md`, `TARA-ANALYSIS.md`,
  `CYBERSECURITY.md`, `fmea.csv`/`fmea.json`, the formal-verification TLA+
  specs in `tla/`, and `AUDIT_PACK.md` against the v2.x requirement set —
  the old `REQ-ZONE-*`/`REQ-CMD-*`/`REQ-STATUS-*` requirement groups are
  retired alongside the API surface they trace to, and every phase above
  needs its own new `REQ-*` group
- Re-run the ASIL-B decomposition and structural-coverage analysis against
  the new endpoint and safe-state code paths specifically, since the E2E
  CRC / safe-state mechanism (v2.6.0) is the most safety-relevant new
  surface in this whole roadmap

**Done (v2.18.0):** `.fusa-reqs.json` is pruned of the 20 requirement
entries backing the pre-replacement `REQ-ZONE-*`/`REQ-CMD-*`/
`REQ-STATUS-*` groups (8/6/6), matching the file's own `rcp/rcp.hpp`
header-comment note added at this milestone explaining why the underlying
C++ types remain (per REPLACE, not a delete — `rcp/config.hpp` is still
their one remaining dependent, per milestone 60's closeout) while their
requirement coverage does not: carrying certified coverage for a type this
codebase has already recorded as having no TC18 analog would misrepresent
it as safety-relevant surface going forward. The matching `// fusa:req`/
`// fusa:test` annotation lines and stale Catch2 tags are removed from
`rcp/rcp.hpp` and `tests/test_rcp.cpp`; the `REQ-PRI-*`/`REQ-ERR-*`/
`REQ-CMDSTRUCT-*`/`REQ-RESP-*`/`REQ-STAT-*` groups this milestone's own
scope text does not name for retirement keep their existing coverage
unchanged. Every `REQ-*` prefix this milestone's scope text names as
needing its own group (`REQ-WIRE` through `REQ-RELAY`) was already present
per each phase's own closeout — this was verification, not bulk authoring,
confirming no gap-filling was needed. `.fusa-reqs.json` now carries 417
requirements across 55 groups (down from 437 across 58 groups pre-prune).

`HARA.md`/`.fusa-hara.json`, `TARA-ANALYSIS.md`, and `CYBERSECURITY.md`
are rewritten against the current stream/endpoint/register-map
architecture, keeping their existing OS-\*/H-\*/SG-\* and A-\*/T-\* ID
schemes stable (so `SAFETY_PLAN.md`'s existing safety-goal references stay
valid) while replacing every Zone/Command/Controller-era mechanism
reference with the real v2.x module it now maps to (`e2e::RxSequenceGuard`/
`RxStreamGuard`/`RxWatchdog`, `watchdog::Manager`, `lifecycle::
ServerLifecycle`, `regmap::Ep0`, `authz::AccessPolicy`, `discovery::
DiscoveryClaim`, `tls::SecureClient`/`SecureServer`), and removing threats/
mitigations tied to packages this roadmap has since deprecated
(`firmware.hpp`'s SHA-256 image verification, `prioqueue.hpp`'s
`Priority::Critical` bypass). A new hazard/safety-goal pair (H-011/SG-011)
and TARA threat (T-05, re-scoped) cover the E2E CRC mechanism specifically,
per this milestone's own call-out that it is the most safety-relevant new
surface in the roadmap; H-001/SG-001's ASIL-B decomposition rationale is
rewritten around the real two-mechanism combination
(`watchdog::Manager`/`StreamWatchdog` + `deadline::Monitor`) instead of the
removed `watchdog::Keeper`. `SAFETY_PLAN.md`'s two mechanism-table rows
that named now-retired ids (`REQ-CMD-005`, `REQ-PQ-001`) are repointed to
their real v2.x replacements (`REQ-WDG-002`, `REQ-SEQ-002`) as a minimal,
targeted fix — the rest of that file's zone-era language is unchanged,
being out of this milestone's own named scope.

The formal-verification TLA+ specs are replaced, not just re-described:
`tla/AntiReplayGuard.tla` modelled the pre-replacement 32-entry
sliding-window bitmap guard removed at v2.6.0, and `tla/
HealthStateMachine.tla`/`tla/WatchdogProtocol.tla` both modelled the same
three-state Healthy/Degraded/Faulted watchdog machine driven by a
client-side periodic `CommandType::Watchdog` kick, discarded outright at
v2.10.0 — none of the three matched any mechanism actually in this tree.
`tla/RxSequenceGuard.tla`, `tla/CrcSafeStateLatch.tla` (new: the E2E CRC
drop-vs-latch rule had no formal spec before this milestone), and `tla/
WatchdogSafeState.tla` model `rcp/e2e.hpp`'s/`rcp/watchdog.hpp`'s actual
three independently-configurable primitives instead, each with a
like-named `.cfg` TLC now loads automatically (the pre-replacement specs
had none, so `FORMAL_VERIFICATION.md`'s own documented `java -jar
tla2tools.jar tla/*.tla` invocation would have failed with a missing-config
error had anyone actually run it — a pre-existing gap this milestone
closes). All three new specs and their safety/liveness properties are
verified with TLC 2.19 (`tla2tools.jar`), not just asserted:
`RxSequenceGuard`'s `Monotonic`/`NoStaleAcceptance`, `CrcSafeStateLatch`'s
`LatchIsSticky`/`NoLatchWithoutEnforce`, and `WatchdogSafeState`'s
`LatchIsSticky` all report "Model checking completed. No error has been
found." `FORMAL_VERIFICATION.md` is rewritten to match, including the
ASIL tracing table pointing at the new H-011/SG-011 pair.

`AUDIT_PACK.md` is rewritten: the requirement count (198 across 24 groups
→ 417 across 55), the `cpfusa trace` coverage gate threshold (80% → 100%,
matching the gate milestone 60 actually tightened without this document
being updated to match until now), and the structural-coverage module list
(no more `rcp.hpp (core)`/`firmware.hpp` fixed numbers — the module table
now points at `coverage-report.json`, regenerated by `release.yml` on
every tag, rather than hand-copying numbers that go stale between
releases) are all corrected. Its own document version is bumped 1.0.0 →
2.0.0, alongside `TARA-ANALYSIS.md`'s and `CYBERSECURITY.md`'s — these are
each document's own independent revision counter, distinct from cpp-RCP's
own semver, and this is each document's first full-content rewrite since
its original milestone-42/43 authoring.

`fmea.csv`/`fmea.json`, `safety-case.*`, `tara.json`/`tara.md`, `sbom.json`,
`provenance.json`, `artifact-manifest.json`, the ISO 26262/IEC 61508/
DO-178C gap reports, `sas.*`, `sci.json`, `audit-pack.zip`,
`fusa-badge.svg`, `report.json`/`report.html`, and `qualify-report.json`
are deliberately left untouched by this PR: per `.github/workflows/
release.yml`, every one of them is mechanically regenerated by `cpfusa`
against the current `.fusa-reqs.json`/`.fusa-hara.json`/source-tree state
on the next tag push, so hand-editing them here would just be overwritten
— committing a hand-edited copy now would only create merge noise against
what `release.yml`'s `github-actions[bot]` commit produces once v2.18.0 is
tagged, the same regeneration step every prior milestone's tag already
triggered (see e.g. `e7a8178`, the v2.17.0 regeneration commit).

`version.hpp`/`CMakeLists.txt` are bumped to 2.18.0. Full local build
(clang, C++17) and `ctest` (53/53 tests, unchanged — no test file added,
removed, or renamed this milestone) pass; `cpfusa check`/`lint`/`cyber`
all pass with 0 errors (`lint`'s reported errors only appear when run
against a local `build/` tree that pulls in FetchContent'd Catch2 source
under `build/_deps`, which CI's `cpfusa-lint` job never has present, since
it runs `cpfusa lint` without a prior CMake configure/build step); `cpfusa
trace --req-coverage 100` reports `Total: 417  Annotated: 417 (100.0%)
Tested: 417 (100.0%)`, 0 dangling ids; RELAY `conform --strict` against
the built `cpp-rcp` CLI still passes, since this milestone touches no
wire-format or CLI-surface code. `README.md` is left unmodified: it
already predates milestones 44-61 (still documenting `rcp/firmware.hpp`
and other since-deleted packages) and bringing it in line with the current
architecture is a substantial, separately-scoped task this milestone's own
text does not name. Milestone #63 (TC18 RCP General Availability, v3.0.0)
is next and, per its own scope, is the final milestone in this roadmap.

### Retired-model residue cleanup (2026-07-30, v2.19.0 — audit findings cpp-RCP-FS-01..05, cpp-RCP-05, cpp-RCP-07)

Milestone 62's own closing note above named this exact gap and deferred it:
`rcp/config.hpp` was still the one live compiled dependent of `rcp.hpp`'s
pre-replacement `Zone`/`Command`/`Response`/`Status`/`Priority`/
`CommandType`/`Controller`/`LoaningController`/`Registry` model, which kept
that entire retired surface — plus `rcp/legacy_mock.hpp` and the three
other headers still built against it — alive as compiled, tested, public
API. This pass finishes that removal end to end, closing cpp-RCP-FS-01
through cpp-RCP-FS-05 and the two smaller cpp-RCP-05/cpp-RCP-07 findings
from the same audit register (cpp-RCP-FS-06 — a canonical `ControlFlags`/
`Message` type in `rcp/acf.hpp` — is intentionally left to the parallel
pass already rewriting that file's wire-format code, to avoid two PRs
touching the same header's bit-packing logic at once).

`rcp/config.hpp` is rebound first, per the dependency `rcp.hpp`'s own
header comment named: `ZoneManifestEntry`/`Manifest.zones`/`load(json,
rcp::Registry&)` become `EndpointManifestEntry`/`Manifest.endpoints`/
`load(json, shmem::Registry&)`, parsing a `stream_key`+`byte_bus_id`
manifest and bootstrapping one `shmem::Channel` per distinct `stream_key`
instead of constructing one `legacy_mock::Controller` per zone name — the
embedding application still owns wiring each `Channel`'s `Handler`, since
there is no more generic in-process `Controller` this loader can construct
on the caller's behalf. `tooling/zone_manifest_schema.json` (the retired
zone-manifest JSON schema) and `rcp/legacy_mock.hpp` itself are then
deleted outright, along with `tests/test_legacy_mock.cpp`, and `rcp.hpp` is
stripped down to the three primitives that never depended on the retired
model: the `Errc` sentinel category (minus `zone_mismatch`/
`ErrZoneMismatch`, which had no TC18 analog), the `Context` alias, and
`Loan` (a generic RAII buffer holder — `rcp/loan.hpp`'s `BufferPool` still
hands these out and is otherwise untouched). `rcp::StatusChannel`
(`relay::Channel<Status>`) has no meaning once `Status` is gone and is
removed with it; §18.2's own worked example still names it, which is a
RELAY-spec-side documentation lag this PR flags rather than tries to fix
from the cpp-RCP side.

`rcp/faultinject.hpp`, `rcp/redundancy.hpp`, and `rcp/admin.hpp` are
rebound rather than deleted, since each is a genuine mechanism with no
TC18-side replacement of its own: `faultinject::Controller` becomes
`faultinject::Interceptor`, wrapping an `rcp::RequestFn` (rcp/adapt.hpp's
client-side send-equivalent call, the same shape rcp/record.hpp and
rcp/observe.hpp already standardize on) instead of decorating
`rcp::Controller`; the injected "error" case now sets
`AcfMessageInfo::err` instead of returning the retired
`ResponseStatus::Error`. `redundancy::RedundantController` becomes
`redundancy::RedundantRequestFn`, holding a primary/standby pair of
`RequestFn`s instead of `rcp::Controller`s. `admin::AdminServer` moves
from reporting `Zone`-keyed `rcp::Registry` state to reporting
stream-keyed `rcp::shmem::Registry` state (`ZoneInfo`/`Event.zone` become
`StreamInfo`/`Event.stream_key`). None of the three preserve a `close()`
or `zone()`/`subscribe()` passthrough that has no analog on a plain
`RequestFn`; their test suites (`test_faultinject.cpp`,
`test_redundancy.cpp`, `test_admin.cpp`) are rewritten against
`rcp::mock::Server`-backed `RequestFn`s and `rcp::shmem::Registry` instead
of `legacy_mock`, and `test_config.cpp` similarly against the rebound
loader.

`tests/test_rcp.cpp` (cpp-RCP-FS-04) is purged of its
`Zone::FrontLeft==1`/`CommandType::Set==1`/`Priority` ordering
certification cases and now covers what `rcp.hpp` actually still defines
(`Errc` sentinels, `Context`, `Loan`'s release-exactly-once contract) plus
two supplementary TC18 conformance checks (`avtp::ByteBusId`'s single-byte
range, `AcfMessageInfo`'s zero-value defaults) that don't duplicate
`test_acf.cpp`/`test_avtp.cpp`'s own much larger suites.
`tests/bench_mock.cpp`/`tests/command_latency_test.cpp` (not named in the
audit but both compiled directly against `legacy_mock::Controller`) are
rebound to benchmark/time `rcp::mock::Server::dispatch()` instead — the
transport their own names already referred to.

`rcp/adapt.hpp` (cpp-RCP-FS-05): `endpoint_id_to_relay_id`/
`relay_id_to_endpoint_id`/`response_to_message`/`message_to_request` drop
the `stream_key` parameter entirely. RELAY spec v2.0 §15.7.5 defines
`relay.Message.ID` for RCP as just the decimal `ByteBusID` string (e.g.
`"9"`), with the `StreamID` carried by the `Caller`/connection object
itself (§8.5: one `StreamID` per `Caller` instance) rather than folded
into the ID — this file previously encoded
`"<stream_key as 16 lowercase hex digits>:<byte_bus_id>"`, which does not
round-trip through anything expecting the spec's plain decimal form.
`rcp/cli.hpp`'s `send` keeps its existing `--server`/`--endpoint` flags
unchanged for backward CLI compatibility, but `--server` is now validated
for shape only and no longer folded into the constructed `relay::Message`,
since this CLI's demo backend was already a single hardcoded
`mock::Server` regardless of `--server`'s value — the flag never actually
selected among multiple backends even before this fix, only its string
representation in `id` changes here.

`relay/relay.hpp`'s `kRelaySpecVersion` (cpp-RCP-05, #74) is bumped
`"1.11"` → `"2.0"`, the current stable RELAY spec version
(`spec/version.json`), which retired the placeholder RCP model this PR
finishes removing and renamed the RCP-facing client interface to
`Controller` in the process (RELAY spec §8.5 — a distinct, `StreamID`-
scoped request/response interface, not the deleted `rcp::Controller`;
introducing that interface itself is out of scope here, since none of the
findings this PR closes ask for it, and `rcp::RequestFn` already covers
the same "client-side send-equivalent call" role every decorator in this
tree already standardizes on). A full re-read of every in-repo RELAY-spec
`§`-citation against the current spec text (not just the two the finding
named) turned up only the version-string gap: `avtp.hpp`/`acf.hpp`'s
`§13.7.2` module-name-registry citations and `rcp.hpp`'s `§18.2`
Context/StatusChannel citations both still match section content actually
present in the current v2.0 document, so neither warranted a citation
fix — recorded here since the finding text itself flagged this as
unverified and asked for a fresh check.

`rcp/cli.hpp`'s `capabilities_json()` (cpp-RCP-07, #75) drops `"loaning"`
from its reported `features` array — no `LoaningController` (itself
deleted with the rest of the retired model) is wired into the CLI-exposed
`Adapt()` path, so the self-report was overclaiming a capability the
binary didn't actually expose.

`README.md` — deferred at milestone 62 as "a substantial, separately-
scoped task" — is rewritten: the Quick Start no longer uses
`rcp/legacy_mock.hpp`/`Zone`/`CommandType`/`Priority` and instead builds an
`rcp::mock::Server`, wraps its `dispatch()` as an `rcp::RequestFn`, and
calls it through `rcp::Adapt()`; the "## Zones" and "## Command types"
sections and the `legacy_mock.hpp`/`rcp.hpp`-as-Controller-and-Registry
core-table rows are gone; the error table drops `ErrZoneMismatch`; and the
protocol-bridge/control-plane module rows for `federation.hpp`,
`firmware.hpp`, `proxy.hpp`, `zonegroup.hpp`, `canbr.hpp`, `linbr.hpp`, and
`prioqueue.hpp` — headers already deleted at earlier milestones per the
Satellite Package Disposition table below, but still documented as if
present — are removed.

`HARA.md`'s SG-009 row is updated to name `faultinject::Interceptor`
instead of the now-renamed `faultinject::Controller`; no other hand-
authored safety/security document is touched, and `.fusa-hara.json`/
`fmea.*`/`tara.*`/`safety-case.*`/etc. are left for `release.yml`'s
existing tag-triggered regeneration, per every prior milestone's own
convention. `.fusa-reqs.json`: the `REQ-PRI-*`/`REQ-CMDSTRUCT-*`/
`REQ-RESP-*`/`REQ-STAT-*`/`REQ-ERR-011`/`REQ-CTRL-*`/`REQ-REG-*` families
(54 entries — the placeholder-model requirement groups milestone 62's own
scope note explicitly left untouched) are removed; `REQ-FI-*`/`REQ-RED-*`/
`REQ-ADMIN-*`/`REQ-CFG-*`/`REQ-RELAY-001/003/004/005` keep their existing
ids with text updated to match the rebound behavior; one new id,
`REQ-LOAN-007`, covers `Loan`'s release-exactly-once contract now that
`Loan` lives directly in `rcp.hpp`. Every id in the resulting 364-entry
file has exactly one `fusa:req` and one `fusa:test` tag somewhere in the
tree (checked by hand pending a real `cpfusa trace` run, since the
`cpfusa` binary itself isn't available in this environment).

`version.hpp`/`CMakeLists.txt` are bumped to 2.19.0. Full local build
(GCC 16, C++17, Ninja) and `ctest` (52/52 tests — one target,
`rcp_legacy_mock`, removed along with the file it built) pass, including
the 30-second `rcp_latency` safety-timing gate. `clang-tidy` could not be
run against this repo's own CI invocation as-is: there is no `.clang-tidy`
config committed, so `clang-tidy -p build --warnings-as-errors='*'` with
no explicit `--checks` reports "no checks enabled" and exits nonzero
before analyzing anything (CI's own step already masks this with
`|| true`, so it has not been gating merges). A representative
`--checks='clang-diagnostic-*,clang-analyzer-*,bugprone-*,performance-*'`
run against every file this PR touches found nothing new: the remaining
hits are Catch2's own `TEST_CASE` static-registration pattern (present
identically in every test file in the tree) and a couple of
`bugprone-branch-clone`/`bugprone-std-namespace-modification` hits on
switch/namespace-specialization shapes carried over unchanged from the
pre-existing code this PR migrated, not introduced by it. `cpfusa`
check/lint/trace and RELAY `conform --strict` were not run locally (the
`cpfusa` binary requires building from its own separate repo, as CI's own
`cpfusa-build` job does) — flagged for CI to confirm.

### 63. TC18 RCP — General Availability (v3.0.0)

- First release where cpp-RCP's `RCP` conforms to the OPEN Alliance TC18
  Remote Control Protocol Specification at the wire level: an RC Client
  built on this library and an RC Server built on any other conformant
  implementation (and vice versa) can interoperate
- Major version bump (v2.x → v3.0.0) marks this as the intended stable line
  going forward, distinct from the v2.x pre-GA milestones that got the
  implementation there
- Fragmentation support remains an explicitly documented future item (see
  v2.8.0), not a GA blocker

---

## Satellite Package Disposition

Every header under `include/rcp/` and `include/relay/` that exists
alongside the core protocol today gets an individual, justified call
below. "REPLACE" means the package's current implementation is discarded
and rebuilt from spec; "ADAPT" means the concept survives but the interface
is rebound to the new core types; "DEPRECATE" means the package is removed
with no direct successor; "KEEP AS-IS" means the package is genuinely
orthogonal to the protocol replacement.

| Package | Call | Reason |
|---|---|---|
| `rcp.hpp` | REPLACE | Core `Zone`/`Command`/`Response`/`Status`/`Controller`/`Registry` model has no TC18 analog; becomes the new stream/endpoint/register-map model (v2.0.0-v2.1.0) |
| `wire.hpp` | REPLACE | *Is* the old wire format; rebuilt as the AVTPDU/ACF codec (v2.0.0) |
| `udp.hpp` | REPLACE | Bespoke 16-byte-header UDP transport rebuilt to carry real AVTPDU frames (v2.13.0) |
| `mock.hpp` | REPLACE | In-process mock is entirely `Zone`/`Command`-shaped; rebuilt as an RC Server simulator (v2.12.0) |
| `sim.hpp` | REPLACE | Timing-realistic simulator implements the full old `Controller` interface; rebuilt against the new register/endpoint model (v2.12.0) |
| `e2e.hpp` | REPLACE | Ad-hoc CRC-16 + replay-window wrapper superseded by the spec's actual CRC32 (poly `0xF4ACFB13`) safe-point mechanism and `rx_enforce_seq` monotonic check (v2.6.0) |
| `powerstate.hpp` | REPLACE | Ad-hoc Active/Sleeping/BusOff model superseded by the spec's Normal/StandBy/Sleep/Unpowered model with cold/hot start (v2.9.0) |
| `watchdog.hpp` | REPLACE | Client-driven `CommandType::Watchdog` kick has no analog; superseded by per-stream watchdog config tied to safe-state (v2.10.0) |
| `capi.h` / `capi_impl.hpp` | REPLACE | C structs are `Zone`/`CommandType`-specific; RTOS/bare-metal surface rebuilt from scratch against the new request model (v2.16.0) |
| `adapt.hpp` | ADAPT | `Adapt()`/`ToMessage`/`FromMessage` rebound to the new request/response shapes; `relay::Message` addressing moves from zone name to server+endpoint identifier (v2.16.0) |
| `cli.hpp` (+ `cli/main.cpp`) | ADAPT | `version`/`capabilities`/`status` triad unaffected in shape; `send` gets new server/endpoint addressing (v2.16.0) |
| `config.hpp` | ADAPT | Zone-registry JSON manifest loader rebuilt around a server/endpoint manifest schema; still useful for bootstrapping known topologies alongside discovery |
| `admin.hpp` | ADAPT | Zone listing/SSE/Prometheus rebuilt around discovered RC Servers and their endpoints instead of registered zones |
| `authz.hpp` | ADAPT | Policy check rebound from (identity, zone, cmd type) to (identity, server, endpoint, request kind); protocol already provides root-client vs. per-endpoint access natively (v2.1.0), this package adds policy on top (v2.11.0) |
| `ratelimit.hpp` | ADAPT | Token-bucket rebound from per-zone/priority to per-endpoint/stream; the spec explicitly leaves queue-overrun avoidance to the client, which is this package's existing job one layer up (v2.11.0) |
| `deadline.hpp` | ADAPT | Zone-liveness-via-`Status`-subscription rebuilt around response-queue heartbeat cadence / EP0 lifecycle triggers, since `Status` subscription doesn't exist in the new model (v2.10.0) |
| `mdns.hpp` | ADAPT | Rescoped to the UDP/IP transport variant only — TC18's own wire-level discovery (v2.2.0) makes a name-service layer unnecessary for native Ethernet deployments (v2.14.0) |
| `tls.hpp` | ADAPT | Rebuilt for the UDP/IP transport variant; the spec's own preferred link security is MACsec (802.1AE) at layer 2, which this package doesn't address and should be flagged as the longer-term preferred mechanism (v2.14.0) |
| `tsn.hpp` | ADAPT | PCP mapping rebound from the removed `Priority` enum to the spec's request-kind execution-priority ordering (v2.5.0); prefer real IEEE 1722 stream reservation where available (v2.14.0) |
| `shmem.hpp` | ADAPT | Zero-copy in-process channel rebuilt against new request/response shapes; the co-location value proposition is unaffected (v2.14.0) |
| `loan.hpp` | ADAPT | Zero-copy payload loaning rebound to the new request-building API; arguably more valuable now given AVTPDU framing overhead (v2.14.0) |
| `record.hpp` | ADAPT | Record/replay format reworked to capture raw AVTPDU frames or RC-Client-level request/response pairs (v2.14.0) |
| `observe.hpp` | ADAPT | Span/counter wrapper rebound to the new send-equivalent call; OTel-style approach itself unaffected (v2.14.0) |
| `dyndata.hpp` | KEEP AS-IS | `SchemaRegistry`/`DynamicPayload` operate on raw bytes + a schema ID with no dependency on `Zone`/`Command`; genuinely orthogonal to the wire-protocol replacement |
| `mqttbr.hpp` | ADAPT | Unimplemented stub; interface rebound to new request/response types, no working behavior to preserve (v2.15.0) |
| `ddsbr.hpp` | ADAPT | Same as above (v2.15.0) |
| `someipbr.hpp` | ADAPT | Same as above (v2.15.0) |
| `restbridge.hpp` | ADAPT | Same as above (v2.15.0) |
| `grpcbridge.hpp` | ADAPT | Same as above (v2.15.0) |
| `udsbr.hpp` | ADAPT | Same as above (v2.15.0) |
| `doipbr.hpp` | ADAPT | Same as above (v2.15.0) |
| `canbr.hpp` | DEPRECATE | The spec makes CAN a native RC Server endpoint type (v2.7.0), not something bridged to from a `Zone`; the bridge *direction* this package assumes is inverted by the real protocol, and it is currently an empty stub with no behavior to preserve |
| `linbr.hpp` | DEPRECATE | Same reasoning as `canbr.hpp` — LIN is a native endpoint type (v2.7.0), not a bridge target |
| `federation.hpp` | DEPRECATE | Multi-HPC lease-based zone-ownership forwarding is a bespoke zonal-architecture concept with no TC18 analog; the spec's multi-client arbitration is the root-client/discovery-stream-claim mechanism (v2.1.0/v2.2.0), which is architecturally a different idea, not a superset |
| `proxy.hpp` | DEPRECATE | TC18 has no "proxy node" concept — an RC Client addresses an RC Server directly via `stream_id`/`byte_bus_id`; multi-hop is a network-layer (switching/routing) concern the spec explicitly leaves out of scope |
| `zonegroup.hpp` | DEPRECATE | No direct analog; the spec's own way to batch multiple targets is packing several ACF messages with different `byte_bus_id`s into one AVTPDU (extraction §3.14), which is a different mechanism (one frame, many targets) from this package's fan-out-to-N-independent-zones model |
| `prioqueue.hpp` | DEPRECATE | The `Priority::Critical/High/Normal` enum this package reorders by is removed; the spec's own priority concept is a fixed execution-priority ordering across request *kinds* (cancellation > triggered > timed > compound > compound-wait > chained > standard), enforced server-side, not a client-side send-order wrapper |
| `firmware.hpp` | DEPRECATE | `CommandType::Update` and the whole Initiate/Transfer/Verify/Activate session model have no TC18 analog — OTA is an application-layer concern the spec doesn't define; could be rebuilt later atop raw UART/SPI endpoint byte transfer if ever needed, but that is speculative and out of scope here |
| `version.hpp` | KEEP AS-IS | Single version-string constant; mechanically bumped per release, no design dependency on the protocol |
| `relay/relay.hpp` | KEEP AS-IS | Protocol-agnostic `Message`/`Channel`/`Node`/`Caller`/`Errc` types have no dependency on `rcp::` internals; only the RCP-side mapping in `adapt.hpp` needs rework |
| Safety/certification tooling (`.fusa*.json`, `fmea.*`, `HARA.md`, `TARA-ANALYSIS.md`, `SAFETY_PLAN.md`, `CYBERSECURITY.md`, `AUDIT_PACK.md`, `tla/`, `cmake/`, `tooling/`) | KEEP AS-IS | Genuinely orthogonal process/tooling scaffolding; regenerated with new requirement IDs at the Certification Refresh milestone (v2.18.0) rather than redesigned |

### Post-hoc naming reconciliation (RELAY spec §13.7.2, issue #45)

RELAY spec v1.14 expanded §13.7.2's standard module-name registry with
canonical names for the RCP protocol-core concerns this Phase 13 rewrite
builds, prompted by comparing this effort against the equivalent go/rust/c
RCP replacements. Two naming deltas from milestone 44/49 above were
corrected to match, without any behavior change:

- `rcp/wire.hpp` (milestone 44, v2.0.0) is split into `rcp/avtp.hpp`
  (AVTPDU/NTSCF/TSCF header framing) and `rcp/acf.hpp` (ACF_ABB/ACF_GBB
  message format) — the registry names these two concerns separately.
  Every table/paragraph above that refers to `rcp/wire.hpp` as the then-
  current file is describing the shape of the milestone 44/49/50/51 work as
  it shipped at the time; the file itself now lives under those two names.
  `rcp/legacy_wire.hpp` (the pre-replacement 16-byte codec) is unaffected
  and keeps its name.
- `rcp/sequencer.hpp` (milestone 49, v2.5.0) is renamed `rcp/request.hpp` to
  match the registry's `request` entry ("conditional-request taxonomy... and
  sequencers") — the file's contents already covered exactly that combined
  scope, so this was a rename, not a split; `SequencerTable` keeps its name
  as the internal sequencer-state sub-concept within the module.
- `rcp/lifecycle.hpp`, `rcp/regmap.hpp`, `rcp/discovery.hpp`, and
  `rcp/e2e.hpp` already matched the registry and were left unchanged.
- `rcp/fragment.hpp` is not created: milestone 52 (v2.8.0) above already
  decided fragmentation no-go for this cycle, so there is no fragmentation
  logic anywhere in this tree to extract into a module.
- `canbr.hpp`/`linbr.hpp`'s DEPRECATE call above (vs. go-RCP's ADAPT) is a
  real cross-repo architectural difference, not a naming issue, and is
  intentionally left unresolved here — see issue #45.

---
### Appendix A — Legacy Roadmap (v0.1.0–v1.11, superseded)
---

Everything below this line is the completed roadmap for the pre-replacement,
non-conformant `Zone`/`Command`/`Response` protocol. It is kept for
historical record only — none of it describes the OPEN Alliance TC18 Remote
Control Protocol work above, and no future work should be planned against
it. See the Breaking Change Notice at the top of this document.

### Vision (superseded)

cpp-RCP was, prior to this roadmap, a C++17-native Remote Control Protocol
for automotive zonal architecture, feature- and API-equivalent to
[go-RCP](https://github.com/SoundMatt/go-RCP). That framing is retired: the
project's name now refers to the real OPEN Alliance TC18 protocol, not a
same-named home-grown one.

### Legacy Milestones (v0.1.0 – v1.11, all shipped)

1. Foundation (v0.1.0) — core interfaces, mock backend, CI, cpp-FuSa, safety artifacts
2. Requirements (v0.2.0) — 198 atomic SEOOC ASIL-B requirements
3. Hardening (v0.3.0) — mock correctness fixes, benchmarks, safety timing evidence
4. HARA Expansion (v0.4.0) — H-001..H-010, SG-001..SG-010
5. UDP Transport (v0.5.0)
6. mDNS Discovery (v0.6.0)
7. TLS Transport (v0.7.0)
8. Shared Memory Transport (v0.8.0)
9. Loaned Samples (v0.9.0)
10. TSN Transport (v0.10.0)
11. Watchdog & Heartbeat (v0.11.0)
12. Deadline Monitoring (v0.12.0)
13. Power State (v0.13.0)
14. E2E Protection (v0.14.0)
15. Priority Queuing (v0.15.0)
16. Rate Limiting (v0.16.0)
17. Zone Simulator (v0.17.0)
18. Fault Injection (v0.18.0)
19. Authorization (v0.19.0)
20. Firmware Update / OTA (v0.20.0)
21. Zone Groups (v0.21.0)
22. Zone Proxy (v0.22.0)
23. Redundancy (v0.23.0)
24. Multi-HPC Federation (v0.24.0)
25. Observability (v0.25.0)
26. Admin API (v0.26.0)
27. Record & Replay (v0.27.0)
28. Config (v0.28.0)
29. Code Generation (v0.29.0)
30. Dynamic Data (v0.30.0)
31. gRPC Bridge (v0.31.0)
32. REST Bridge (v0.32.0)
33. SOME/IP Bridge (v0.33.0)
34. CAN Bridge (v0.34.0)
35. DDS Bridge (v0.35.0)
36. MQTT Bridge (v0.36.0)
37. LIN Bridge (v0.37.0)
38. UDS Bridge (v0.38.0)
39. DoIP Bridge (v0.39.0)
40. RTOS / Bare-Metal (v0.40.0)
41. Formal Verification (v0.41.0)
42. ISO 21434 / Cybersecurity (v0.42.0)
43. Certification (v0.43.0)
44. RELAY conformance uplift (v1.0.0 – v1.11) — protocol-agnostic `relay::` namespace, crossbar spoke CLI, cross-language module conventions

Full detail for each of these has been removed from this file to keep the
document focused on the active replacement plan; see git history for
`ROADMAP.md` prior to this revision if the original per-milestone
bullet points are needed.
