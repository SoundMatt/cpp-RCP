# TARA — cpp-RCP

Standard: iso21434  
Generated: 2026-07-29T00:57:06Z  
Coverage: 8/8 assets (100.0%)

| ID | Asset | Threat | Feasibility | Risk | Treatment |
|---|---|---|---|---|---|
| TARA-001 | cpp-RCP release binary | Attacker replaces a signed release binary with a trojaned build that suppresses safety findings before it reaches a downstream project's CI. | low | medium | mitigate |
| TARA-002 | .fusa.json / .fusa-hara.json project config | Attacker (or careless commit) weakens the configured ASIL or replaces genuine hazard analysis with a stale/placeholder template, silently lowering the safety bar a downstream project believes it is held to. | medium | high | mitigate |
| TARA-003 | Generated evidence artifacts (fmea.json, tara.json, safety-case.json, check-report.json) | Insider or compromised CI step edits a generated evidence artifact after generation to hide a known defect from a certification reviewer. | low | medium | mitigate |
| TARA-004 | Third-party CMake dependencies (FetchContent: CLI11, nlohmann/json, Catch2) | A compromised upstream dependency introduces malicious code that runs inside cpfusa's own analysis process, potentially tampering with the findings it produces. | very-low | medium | mitigate |
| TARA-005 | qualify-report.json tool-qualification evidence | Attacker modifies qualify-report.json to hide a failing qualification case, making an unqualified tool appear qualified for its intended safety use (ISO 26262-8 Clause 11). | low | medium | mitigate |
| TARA-006 | CI pipeline (ci.yml / release.yml) | Attacker disables or bypasses the `check` gate step in CI, allowing a change with open ERROR findings to merge and release. | medium | high | mitigate |
| TARA-007 | Source requirement annotations (//fusa:req, //fusa:test) | Developer removes or mistypes an annotation to hide a requirement-traceability gap from `trace`'s coverage gate. | medium | medium | mitigate |
| TARA-008 | .fusa-dispositions.json waiver log | Attacker (or an over-broad legitimate waiver) adds a rule-level disposition that silently suppresses a real, currently-open safety finding project-wide. | medium | high | mitigate |

## Impact (SFOP) & Mitigations

- **TARA-001** — safety=major financial=moderate operational=moderate privacy=negligible
  - sign — HMAC-SHA256 artifact signing
  - SLSA provenance verification (slsa command)
  - Release workflow requires branch-protected, reviewed PRs before tagging
- **TARA-002** — safety=major financial=moderate operational=negligible privacy=negligible
  - FUSA-STUB001 deny-list scan flags untouched hazard/goal templates (§1.6.1)
  - Code review required on any change to .fusa.json / .fusa-hara.json
  - hara --format json's completeness block surfaces missing ASIL/fssrRefs
- **TARA-003** — safety=major financial=moderate operational=moderate privacy=negligible
  - audit-pack — hashed manifest over every evidence artifact
  - sign --verify re-checks HMAC signatures before submission
  - sci — Software Configuration Index records a per-file sha256 at release time
- **TARA-004** — safety=major financial=moderate operational=moderate privacy=negligible
  - vuln — scans CMake dependency manifests for known vulnerabilities
  - Dependency versions pinned to tagged releases in cmake/FetchDeps.cmake
- **TARA-005** — safety=major financial=moderate operational=negligible privacy=negligible
  - qualify.hash — RFC 8785 canonical integrity hash over the report content
  - audit-pack bundles qualify-report.json under the same hashed manifest as every other artifact
- **TARA-006** — safety=moderate financial=negligible operational=major privacy=negligible
  - Branch protection requires the check/lint/test CI jobs to pass before merge
  - dco.yml enforces signed-off commits, raising the bar for an anonymous malicious push
- **TARA-007** — safety=moderate financial=negligible operational=moderate privacy=negligible
  - trace --req-coverage / --func-coverage gates CI on annotation density
  - trace flags a dangling //fusa:test reference to a nonexistent requirement id
- **TARA-008** — safety=major financial=negligible operational=moderate privacy=negligible
  - disposition add requires --reviewer and --rationale (no anonymous waivers)
  - Code review required on any change to .fusa-dispositions.json
  - FUSA-STUB001 is disposition-suppressible only per-finding, never blanket (§1.6.1)
