# Changelog

All notable changes to cpp-RCP are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/).

This changelog starts from the ground-up rewrite tracked on
`rewrite/v3-from-c-rcp` (cpp-RCP issue #129, ROADMAP.md Phase 17) and does not
attempt to reconstruct the project's full pre-rewrite history — see git log
for that. Entries below are one line per merged PR/batch, in the order
merged.

## [3.0.0] — 2026-08-22 — v3.0.0 rewrite (`rewrite/v3-from-c-rcp`)

First release where cpp-RCP *is* the OPEN Alliance TC18 Remote Control
Protocol (ROADMAP.md Phase 17). Full ground-up rewrite, ported from
c-RCP's current RC5-conformant, atomicity-audited, ASIL-D-hardened
implementation. Merged to `main` via #172.

### Phase 7 — release hardening, formal verification, coverage

- Release-pipeline hardening: `version-sources-agree` CI job, real `cpfusa
  analyze`/`cpfusa cyber` gating, `.fusa-dispositions.json`, IEC 62443 release
  gap report, shipped-artifact version verification (batch 3)
- rewrite(phase7): batch 2 — add real MC/DC coverage ratchet gate (LLVM) (#168)
- rewrite(phase7): batch 1 — port LifecycleStateMachine/E2ESafePoint TLA+
  specs, add TLC CI job (#167)

### Phase 6 — requirement catalog re-derivation (complete)

- rewrite(phase6): batch 13 — MOCK/PWRMODE catalog re-derivation (Phase 6
  complete) (#166)
- rewrite(phase6): batch 12 — DISC/RELAY catalog re-derivation (#165)
- rewrite(phase6): batch 11 — REQ-REGMAP-\*→REQ-RMAP-\* rename + RMAP/SRV
  catalog (#164)
- rewrite(phase6): batch 10 — SPI/UART/WAKEUP catalog re-derivation (#163)
- rewrite(phase6): batch 9 — MDIO/PWM catalog re-derivation (#162)
- rewrite(phase6): batch 8 — I2C/ADC/GPIO catalog re-derivation (#161)
- rewrite(phase6): batch 7 — CANEP/LINEP/ISELED catalog re-derivation (#160)
- rewrite(phase6): batch 6 — E2E/LIFECYCLE catalog re-derivation (#159)
- rewrite(phase6): batch 5 — watchdog: one traceability gap found, no catalog
  change (#158)
- rewrite(phase6): batch 3 — conditional-request cluster catalog
  re-derivation (#157)
- rewrite(phase6): batch 2 — FRAG/RESPQUEUE-slice/LOAN catalog re-derivation
  (#156)
- rewrite(phase6): batch 1 — ACF/AVTP/WIREERR catalog re-derivation (#155)

### Phase 5 — admin/shmem and transport dispatch wiring

- rewrite(phase5): admin.hpp — fixed-capacity subscriber/counter bounds, port
  deadlock fix (#154)
- rewrite(phase5): shmem.hpp — rebuild Channel around a real bounded
  byte-level buffer (#153)
- rewrite(phase5): l2.hpp — add FrameHandler wired to Phase 4 frame-level
  dispatch (#152)
- rewrite(phase5): udp.hpp — wire Server::Handler to Phase 4 frame-level
  dispatch (#151)

### Phase 4 — mock dispatch, discovery, register map, server admission

- rewrite(phase4): mock.hpp batch D2 — AVTPDU frame-level dispatch, closes
  out Phase 4 (#150)
- rewrite(phase4): mock.hpp batch D1 — wire fragment.hpp/respqueue.hpp for
  E2E fragmented dispatch (#149)
- rewrite(phase4): mock.hpp batch C — wire RxSequenceGuard,
  StreamFaultTracker, RxWatchdog (#148)
- rewrite(phase4): mock.hpp batch B — Table 24 response suppression +
  regmap/discovery wiring (#147)
- rewrite(phase4): mock.hpp batch A — wire server::Endpoint admission (#146)
- rewrite(phase4): fix adapt.hpp's missing read_size_or_segment_num field,
  add test_adapt.cpp (#145)
- rewrite(phase4): port discovery from c-RCP, fix claim-release and
  validation gaps (#144)
- rewrite(phase4): port regmap batch B from c-RCP — HW pins, streams, EP-ID
  map, optional subsystems (#143)
- rewrite(phase4): port server.c admission/scheduling into new server.hpp
  (#141)
- rewrite(phase4): port regmap batch A from c-RCP — general map, EP0,
  generic/functional split (#142)

### Phase 3 — remaining endpoint types

- rewrite(phase3): port spi and uart from c-RCP, RC5 nr_cs/deassert_cs_pause
  fix (#139)
- rewrite(phase3): port pwm and wakeup from c-RCP, fix PWM_OUT Subtract
  operand order (#140)
- rewrite(phase3): port mdio from c-RCP, revert an earlier session's own
  regression (#138)
- rewrite(phase3): port adc/gpio from c-RCP, fix a critical ADC averaging
  regression (#137)
- rewrite(phase3): port can/lin from c-RCP, wire CAN XL fragmentation via
  fragment.hpp (#136)
- rewrite(phase3): port iseled/i2c from c-RCP; fix ROADMAP.md's Phase 17
  missing i2c entry (#135)

### Phase 2 — E2E/lifecycle/watchdog

- rewrite(phase2): port e2e/lifecycle from c-RCP, correct HARA.md's
  overstated H-004 claim (#134)
- rewrite(phase2): port watchdog + build allocation fault-injection seam,
  ported from c-RCP (#133)

### Phase 1 — core wire format foundation

- rewrite(phase1): port request/sequencer/scheduler from c-RCP's
  RC5-conformant reference (#132)
- rewrite(phase1): add fragment/respqueue, convert loan to fixed-capacity,
  ported from c-RCP (#131)
- rewrite(phase1): port acf.hpp/avtp.hpp from c-RCP's RC5-conformant
  reference (#130)

### Rewrite kickoff

- ci: run CI/DCO on the rewrite/v3-from-c-rcp branch too
- docs(roadmap): v3.0.0 becomes a full rewrite ported from c-RCP, not Phase
  16's organic conclusion
