# TC18 SHOULD/MAY clauses with no corresponding requirement

Companion to `.fusa-reqs.json`'s MUST/SHALL requirement catalog. Every
SHOULD/MAY occurrence in the OPEN Alliance TC18 Remote Control Protocol
Specification is accounted for in one of two places: an existing `REQ-*`
entry's `tc18` citation (search `.fusa-reqs.json` for the citation text
below), or this document, when the clause is either non-testable prose or
— honestly flagged below — a capability this repo does not yet distinctly
track.

No spec text is reproduced verbatim (confidentiality restriction); each
line is paraphrased and cited by section/`pdftotext`-extraction line
reference.

**Depth note:** this pass mirrors c-RCP's SHOULD/MAY audit methodology
(see that repo's own `docs/TC18-NON-NORMATIVE-CLAUSES.md` and ROADMAP.md
milestone 116) but was done at a lighter verification depth given this
repo's overall lower existing citation density (4/376 requirements had a
`tc18` citation before this pass, vs. c-RCP's 212+). Four MAY clauses
below are flagged as **gaps this pass found but did not fully resolve**
(marked ⚠) — they need their own dedicated investigation, not a citation,
because the underlying capability doesn't appear to be distinctly
implemented in this codebase yet. A fuller MUST-clause citation backfill
(matching c-RCP's ~975-entry, multi-session "requirements-corpus
completeness pass") remains separate, larger, future work — see this
project's master checklist memory.

## SHOULD (10, all non-testable — same disposition as every TC18 implementation, this is about the standard's own text, not this codebase)

| § | Citation | Paraphrase | Why no REQ |
|---|---|---|---|
| §2 (intro) | TC18.txt L773, L790, L795, L798, L803, L805 | Six design-goal statements about the standard itself (interchangeability, unambiguous behavior, version compatibility, etc.) | Non-normative prose about the spec's own philosophy, not a testable protocol clause. |
| §11.2.2.1 | TC18.txt L1213 | Repetitive `cmp_start_state=0` compound requests should be last-sent or use a delay | Client request-composition advice; nothing for an RC Server to enforce. |
| §11.2.2.2 | TC18.txt L1312 | Same advice for compound-wait | Same. |
| §12.9.1 | TC18.txt L2988 | Endpoints sharing a `byte_bus_id` in one stream should be the same type | Client/config-authoring guidance, not server-enforceable. |
| §13.7.12.2 | TC18.txt L5525 | `iseled_clk_divider` should be nominal 2MHz | Hardware calibration guideline for a register value, not a software behavior — this repo does not yet implement an ISELED endpoint at all; N/A either way. |

## MAY — implemented and already cited (10, for reference; see the actual `.fusa-reqs.json` entries for full text)

`REQ-E2E-011` (Safety_Measure, L2935), `REQ-PWR-001` (power modes, L2268), `REQ-LIFECYCLE-001` (three lifecycle states, L2063), `REQ-SPI-001` (SPI 6 channels, L4192), `REQ-ADC-002` (samples-per-interval R/W choice, L5040), `REQ-L2-007` (multi-request-per-frame, L3220).

## MAY — genuinely uncertain / not yet distinctly tracked in this codebase (4, ⚠ needs follow-up, not just a citation)

| § | Citation | Paraphrase | Finding |
|---|---|---|---|
| §11.2.2.5 | TC18.txt L1649 | RC Server may reject a request whose `presentation_time` is too far in the future | ⚠ **Not found**: `request.hpp` has no `presentation_time`/gPTP-domain admission logic at all (grepped for `presentation_time`, `Gptp`, `TooFar` — zero hits). c-RCP's equivalent (`rcp_timed_admit()`/`rcp_timed_too_far()`) is a good reference implementation. Needs its own investigation + fix, not a citation. |
| §13.2 | TC18.txt L3502 | An endpoint may be used or not used in a specific RC Server instantiation (EP_USED bit) | ⚠ **Not found**: no `ep_used`/`EpUsed` concept anywhere in `include/rcp/*.hpp` (grepped, zero hits). Whether this is a real gap or handled implicitly by a different mechanism (e.g. the mock server's endpoint-registration model) wasn't determined this pass. |
| §11.2.2.3 | TC18.txt L1413 | Each endpoint may be configured to start a request upon a trigger signal | ⚠ **Partially found**: `RequestTypeOpcode::Triggered`/`RequestCategory::Triggered` exist in `request.hpp`'s enums, but there is no distinct `REQ-TRIG-*` requirement cluster the way there is for Compound/CompoundWait — unclear whether Triggered's own field-level encode/decode (trigger_source_ep/trigger_signal_nr/trigger_threshold/trigger_exec_delay/trigger_repetitions, TC18 §11.2.2.3 Table 8) is actually implemented anywhere or just the opcode/priority-classification shell. Needs its own read-through. |
| §13.7.13.1 | TC18.txt L5631 | An RC Server with an integrated PHY may allow access to it via the MDIO EP | ⚠ **Not addressed**: unlike c-RCP's `ep_mdio.h` (which explicitly documents this deployment mode as in-scope by design, needing no special-case code), cpp-RCP's `mdio.hpp` header has no equivalent discussion. Likely fine by the same reasoning (register-map access doesn't inherently require a physical pin mapping to be validated), but not independently confirmed this pass. |

## MAY — descriptive/out-of-scope (27, same disposition as c-RCP's equivalent audit)

L640 (Edge Node PTP/MACsec, L1/L2 topology concern), L909, L1024, L1025, L1588, L1943 (gPTP sync generally — implemented via this repo's own clock-sync handling but no single citable requirement), L2060, L2062, L2244, L2289, L2355, L2385, L2405, L2565, L2668, L2984, L2986, L2989, L3197, L3206, L3227, L3252, L4323, L5035, L5164, L5358 — each is either descriptive prose restating an architectural fact from a different angle, a client-side or hardware-deployment choice outside library scope, or a non-closed-list permission. See c-RCP's own `docs/TC18-NON-NORMATIVE-CLAUSES.md` for the identical per-line paraphrase and reasoning — the spec text and its non-normative character don't change per implementation.
