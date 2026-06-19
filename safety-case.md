# Safety Case — cpp-RCP

**Standard:** iso26262  
**Generated:** 2026-06-19T14:08:36Z  

## Goals

| ID | Type | Status | Description |
|----|------|--------|-------------|
| G1 | Goal | undeveloped | cpp-RCP is acceptably safe for iso26262 ASIL-ASIL-B |
| G2 | Goal | supported | Software development process meets iso26262 requirements |
| G3 | Goal | undeveloped | No unacceptable residual risks remain |
| G4 | Goal | undeveloped | All requirements are implemented and verified |
| G5 | Goal | undeveloped | Static analysis reports no unmitigated errors |
| G6 | Goal | supported | Tool is qualified per ISO 26262 Part 8 |
| S1 | Strategy |  | Argument over safety process evidence |
| S2 | Strategy |  | Argument over verification evidence |
| Sn1 | Solution |  | SAFETY_PLAN.md — documented safety plan |
| Sn2 | Solution |  | .fusa.json — project safety configuration |
| Sn3 | Solution |  | .fusa-reqs.json — requirements register |
| Sn4 | Solution |  | qualify-report.json — tool qualification evidence |
| Sn5 | Solution |  | .fusa-evidence.json — test execution evidence |
| Sn6 | Solution |  | check-report.json — safety check report |
| C1 | Context |  | Project: cpp-RCP standard: iso26262 ASIL-ASIL-B |
| A1 | Assumption |  | Compiler toolchain is itself qualified |

## Evidence (13 files)

- `.fusa.json`
- `.fusa-reqs.json`
- `fmea.json`
- `fmea.csv`
- `safety-case.json`
- `safety-case.md`
- `safety-case.mermaid`
- `sbom.json`
- `provenance.json`
- `artifact-manifest.json`
- `qualify-report.json`
- `tara.json`
- `tara.md`
