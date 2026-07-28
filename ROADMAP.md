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

---
### Phase 15 — Transport & Ecosystem Bridge Migration
---

### 57. Native Transport Rebuild — UDP/IP (Annex J) (v2.13.0)

- Rebuild `udp.hpp` to encapsulate real AVTPDU frames per IEEE 1722's own
  Annex J UDP/IP encapsulation, instead of the bespoke `R`/`C`-magic 16-byte
  header it uses today — `wire.hpp` is retired outright, not adapted, since
  it *is* the old wire format this whole roadmap replaces
- Depends on: Wire format core (v2.0.0)

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
