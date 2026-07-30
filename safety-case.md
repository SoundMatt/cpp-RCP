# Safety Case — cpp-RCP

**Standard:** iso26262  
**Generated:** 2026-07-30T19:16:23Z  

## Goals

| ID | Type | Status | Description |
|----|------|--------|-------------|
| G1 | goal | undeveloped | cpp-RCP produces conformant, non-fabricated safety evidence for iso26262 ASIL-B |
| G2 | goal | undeveloped | cpp-RCP's own development process satisfies iso26262's tool-confidence-level requirements (ISO 26262-8 Clause 11) |
| G3 | goal | supported | Every generated evidence artifact (fmea/tara/hara/safety-case/sas) passes the §1.6 content-quality baseline (no placeholder text, no blanket qualitative fallback) |
| G4 | goal | undeveloped | Every requirement in .fusa-reqs.json is implemented and independently verified |
| G5 | goal | undeveloped | Static analysis (check/lint/analyze/cyber) reports no unmitigated ERROR findings |
| G6 | goal | supported | cpp-RCP itself is qualified as a verification tool per ISO 26262-8 Clause 11 |
| St1 | strategy |  | Argument by direct inspection of generated evidence artifacts |
| St2 | strategy |  | Argument over independent verification and qualification records |
| Sn1 | solution |  | docs/tool-safety-manual.md — documented development process, scope, and evidence-generation plan |
| Sn2 | solution |  | .fusa.json — declares the standard/ASIL this project is held to |
| Sn3 | solution |  | .fusa-reqs.json — requirement registry with req/test traceability |
| Sn4 | solution |  | qualify-report.json — tool qualification cases and pass/fail record |
| Sn5 | solution |  | .fusa-evidence.json — collected test execution evidence |
| Sn6 | solution |  | check-report.json — the aggregated finding report `check` produced |
| Sn7 | solution |  | fmea.json / tara.json / .fusa-hara.json — pass the §1.6 quality baseline |
| C1 | context |  | Project: cpp-RCP, standard: iso26262 ASIL-B |
| A1 | assumption |  | The compiler toolchain used to build cpp-RCP is itself qualified or independently trusted for its intended use |
| J1 | justification |  | §1.6.1's FUSA-STUB001/002 heuristics are an automatable proxy for content quality, not a substitute for a human reviewer's judgement — hence §1.6.2's attestation mechanism rather than a purely mechanical gate |

## Evidence (13 files)

- `.fusa.json`
- `.fusa-reqs.json`
- `.fusa-hara.json`
- `.fusa-problems.json`
- `fmea.json`
- `fmea.csv`
- `safety-case.json`
- `safety-case.md`
- `safety-case.mermaid`
- `sbom.json`
- `provenance.json`
- `artifact-manifest.json`
- `qualify-report.json`
