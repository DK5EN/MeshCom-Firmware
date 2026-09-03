---
description: Audit source files against docs/codequality-rules.md and docs/code-quality-2.0.md, write docs/code-audit-YYYYMMDD.md
allowed-tools: [Bash, Read, Write]
---

Run a two-phase code audit: mechanical grep scan first, then contextual analysis of each finding.

## Arguments

- No args → audit only files changed vs `upstream/dev` (PR scope)
- `--full` → audit all of `src/`
- `--only <file>[,<file>]` → audit specific files (relative to project root)
- `--delta` → compare against most recent previous audit and highlight new/resolved findings

## Phase 1: Mechanical scan

```bash
python3 tools/code_audit_scan.py [--full | --only <files>]
```

The script runs regex checks for the most common rule violations and emits JSON:
```json
{
  "mode": "changed|full|specific",
  "files_scanned": N,
  "scanned_files": [...],
  "total": N,
  "summary": {"CRITICAL": N, "HIGH": N, "MEDIUM": N},
  "findings": [
    {"rule": "BND-01", "severity": "CRITICAL", "category": "Buffer Safety",
     "description": "sprintf() — use snprintf",
     "file": "src/esp32/esp32_main.cpp", "line": 42, "text": "sprintf(buf, ..."}
  ]
}
```

Parse the JSON. Group findings by file.

## Phase 2: Contextual analysis

For each file in `scanned_files`:

1. Read the file: `Read src/...`
2. For each mechanical finding in that file: verify it is a real violation (not a false positive from a comment or test stub), add context (function name, severity justification).
3. Additionally, read the file looking for violations that grep cannot catch:
   - Thread safety: shared globals accessed without mutex (`RACE-01..08`)
   - ISR safety: `IRAM_ATTR` functions calling non-IRAM functions (`ISR-01..04`)
   - Watchdog: tasks that never yield (`STAB-01..05`)
   - Stack overflow: large local arrays > 512 B, deep recursion (`STK-01..04`)
   - Protocol correctness: missing bounds checks on `rx_len` before array index (`BND-02`)
   - State machine integrity: states modified outside their FSM (`Section 16`)

Read `docs/codequality-rules.md` to apply the full rule set.

4. Walk the review checklist in `docs/code-quality-2.0.md` **Part D**:
   - Tier 1 (ten checks) for every scanned file.
   - Tier 2 when the file touches settings, rings, state machines or an nRF52 path.
   - For every `CQ2-*` mechanical finding, read the matching pattern (`C01`..`C28` in Part A)
     for the "why it slipped" and the consuming-limit question before judging it.
   - Paired-file rule (C06): if a scanned file is one half of an ESP32/nRF52 pair, open the twin
     function and report drift even when the twin was not in scope.

## Phase 3: Delta (if `--delta`)

```bash
ls docs/code-audit-*.md 2>/dev/null | sort | tail -1
```

Read the previous audit. Compare rule IDs + file + line range to classify each current finding as:
- `NEW` — not in previous audit
- `EXISTING` — was already flagged
- `RESOLVED` — was in previous audit, no longer present

## Phase 4: Write the document

Write to `docs/code-audit-YYYYMMDD.md` (today's date).

### Document format

```markdown
# MeshCom Firmware Code Audit

**Date:** YYYY-MM-DD  
**Branch:** fork-main  
**HEAD:** <short hash>  
**Scope:** changed files | full src/ | specific: <list>  
**Rules:** docs/codequality-rules.md, docs/code-quality-2.0.md (Part D checklist)  
**Previous audit:** docs/code-audit-<prev date>.md (or "—")  
**Auditor:** Claude Code (automated)

---

## Audit Summary

| Category | Rule IDs | Critical | High | Medium | Low |
|----------|----------|----------|------|--------|-----|
| Buffer Safety     | BND-01..05 | N | N | N | N |
| Memory Safety     | MEM-01..05 | N | N | N | N |
| Thread Safety     | RACE-01..08| N | N | N | N |
| ISR Safety        | ISR-01..04 | N | N | N | N |
| Stack Safety      | STK-01..04 | N | N | N | N |
| Watchdog          | STAB-01..05| N | N | N | N |
| Security          | SEC-01..   | N | N | N | N |
| Protocol          | Section 15 | N | N | N | N |
| Code Quality 2.0  | CQ2-C01..C28 | N | N | N | N |

**Total: N findings (N CRITICAL, N HIGH, N MEDIUM, N LOW)**

---

## Findings

### [FILENAME]

#### [SEVERITY] [RULE-ID] — [Description]

**Line N:** `code snippet`

**Context:** Which function, what the variable/buffer represents.

**Violation:** Exact rule text from codequality-rules.md that is broken.

**Fix:** Concrete 1-2 line recommendation.

---

## Delta vs. Previous Audit  *(omit if no --delta)*

| Status | Count | Notes |
|--------|-------|-------|
| New findings    | N | ... |
| Resolved        | N | ... |
| Existing (unchanged) | N | ... |

## Fazit

One paragraph: overall code quality trend, most urgent items, any improvements since last audit.
```

### Rules for severity assignment

- **CRITICAL**: Can cause memory corruption, remote code execution, or build failure
- **HIGH**: Likely crash or undefined behavior under realistic conditions
- **MEDIUM**: Latent bug or violation that degrades reliability
- **LOW**: Style / best-practice violation with no immediate impact

## Phase 5: Report

Print the summary table to the terminal.
State path of the written doc.
If CRITICAL findings exist, list them explicitly.
