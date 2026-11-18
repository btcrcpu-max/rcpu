# Upstream Bitcoin Core Security Patch Tracker

Track security patches from Bitcoin Core after v27.0 (RCPU's fork base).
Review quarterly. Update status as patches are backported or assessed.

**Last review**: 2026-11-18
**Next review due**: 2026-Q1 (2026-02)

## Status Legend

- **Backported**: patch applied to RCPU with reference to commit
- **Not applicable**: does not affect RCPU (e.g., feature not present)
- **Pending**: not yet evaluated or backported
- **Won't fix**: evaluated and deliberately excluded (with rationale)

## Patch Tracker

### CVE-2024-52911 — Block validation crash (txdata lifetime)

| Field | Value |
|---|---|
| **Upstream** | Bitcoin Core 27.1 |
| **Severity** | High (DoS via crafted block) |
| **Description** | Use-after-free in block validation: `txsdata` destroyed before `CCheckQueueControl` on failure path, causing script checks to read freed memory |
| **RCPU status** | **Backported** |
| **RCPU location** | `src/validation.cpp:2440-2453` — `txsdata` declared before `CCheckQueueControl` for reverse-order destruction |
| **Notes** | Correct reverse-order destruction pattern confirmed in DuMate audit 2026-11-17 |

### CVE-2022-20707 — Coinbase output validation

| Field | Value |
|---|---|
| **Upstream** | Bitcoin Core 23.0 (pre-fork) |
| **Severity** | High (consensus) |
| **Description** | Early return in CheckBlock before coinbase amount check allows oversized coinbase |
| **RCPU status** | **Backported** |
| **RCPU location** | `src/validation.cpp:2541-2560` — `CheckCoinbaseOutputsExplicit` runs before `GetValueOut() > blockReward`, no early return |
| **Notes** | RCPU strengthened: explicit-output check added as additional layer |

### Pending evaluation

| CVE / Patch | Upstream version | Description | RCPU status |
|---|---|---|---|
| _(to be populated)_ | 27.1-27.4, 28.x | — | Pending |

## Review process

1. Check https://bitcoincore.org/en/security-advisories/ for new advisories
2. Check https://github.com/bitcoin/bitcoin/releases for patch notes
3. For each relevant patch:
   - Determine if the affected code path exists in RCPU
   - If yes, backport and test
   - Update this document with status and commit reference
4. Commit the updated tracker

## References

- [Bitcoin Core security advisories](https://bitcoincore.org/en/security-advisories/)
- [Bitcoin Core releases](https://github.com/bitcoin/bitcoin/releases)
- DuMate audit report 2026-11-17 (P2-d finding)
