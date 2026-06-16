# Incident Response — cpp-RCP

## Severity levels

| Level | Criteria | SLA |
|---|---|---|
| Critical | Exploitable vulnerability or safety regression in production | 24 h |
| High | Security flaw or ASIL-B requirement violation in released code | 72 h |
| Medium | Functional defect with safety impact in unreleased code | 7 days |
| Low | Documentation or test gap | Next release |

## Response process

1. **Triage** — Confirm severity and affected versions within 24 h of report.
2. **Contain** — Disable the affected feature or publish a workaround if severity is Critical or High.
3. **Root cause** — Identify the failing requirement and write a regression test.
4. **Fix** — Implement and review the fix on a `fix/<id>` branch.
5. **Release** — Tag a patch release; update CHANGELOG and advisories.
6. **Post-mortem** — Update `.fusa-problems.json` and HARA if safety impact was found.

## Contacts

Report vulnerabilities via GitHub Security Advisory (see SECURITY.md).
