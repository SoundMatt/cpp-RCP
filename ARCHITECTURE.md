# Architecture

cpp-RCP's architecture follows the canonical cross-repo design at
[RELAY's `docs/RCP-ARCHITECTURE.md`](https://github.com/SoundMatt/RELAY/blob/main/docs/RCP-ARCHITECTURE.md),
shared with go-RCP, c-RCP, and rust-RCP.

## File-path mapping

| Lexicon term | This repo |
|---|---|
| wire / ACF layer | `include/rcp/acf.hpp` |
| framing / AVTP layer | `include/rcp/avtp.hpp` |
| response classification | `acf::response_kind_of()` in `include/rcp/acf.hpp` |
| conditional-request layer | `include/rcp/request.hpp` |
| Table 30 / evt[2:0] write semantics | `endpoint::WriteSemantics` / `write_semantics_of()` / `apply_bitmask_write()` in `include/rcp/endpoint.hpp` |
| endpoint-type modules | `gpio.hpp`, `spi.hpp`, `pwm.hpp`, `adc.hpp`, `i2c.hpp`, `lin.hpp`, `can.hpp`, `uart.hpp`, `iseled.hpp`, `mdio.hpp`, `wakeup.hpp` |
| dispatch/routing | `MockServer::dispatch()` in `include/rcp/mock.hpp` |

## Conformance status against the canonical architecture

| Canonical choice | Status |
|---|---|
| Response classification (evt-first) | **conformant** (this repo is the cross-repo reference implementation) |
| Table 30 centralization | conformant — `endpoint.hpp`'s `WriteSemantics` is shared correctly (e.g. `gpio.hpp`'s `apply_gpio_write` calls into it) |
| Conditional-request module unification | **conformant** (this repo is a reference shape, alongside rust-RCP) |
| Per-function requirement tagging | **not conformant** — tags are collected at file level (top of each `.hpp`), not per-function |
| `.fusa-reqs.json` schema (`tc18`/`tc18_master_id`/`status`) | **partial** — no citation or status fields exist yet; the tool (`cpfusa`) also expects an HLR/LLR two-tier hierarchy this repo's schema doesn't populate (`WARN: HLR ... has no LLR children` on every entry today) — needs its own investigation before schema unification lands here |
| Conditional-request req-id grouping | not yet resolved — needs re-inventory once schema unification starts |

## Note on tag comment syntax

This repo's `cpfusa` tool (confirmed at the CI-pinned v0.18.0) parses
`// fusa:req` **with a space**, unlike go-RCP/c-RCP/rust-RCP's no-space
convention. That's tool-enforced, not a repo choice — RELAY's canonical
doc does not ask this repo to change it.
