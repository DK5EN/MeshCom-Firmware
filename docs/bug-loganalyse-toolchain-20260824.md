# TOOL-01…06 — `tools/loganalyse.sh` / `logauswertung` skill: 4 bugs + 1 enhancement + 1 doc fix

**Status:** FIXED on `v4.35p_prio` (2026-08-24). TOOL-01…05 landed as six commits (see Resolution);
each carries a regression test that was red before and green after. TOOL-06 (skill doc) is applied to
the local, git-ignored `SKILL.md`. Original report verified against `6157e3fe` and the source lines
that emit each log token.
**Class:** analysis-tooling defects (not firmware). No runtime/firmware behaviour is affected — these
corrupt the **analysis output**, i.e. the conclusions drawn from a log.
**Discovered:** 2026-08-24, while running `/logauswertung` on a 3-node MeshCom mesh test from DG0OPK
(DG0OPK-11/-12/-13, ~29.5 h each, ~105 MB / 1.4 M lines total).
**Tool:** `tools/loganalyse.sh` (2359 lines) + `.claude/skills/logauswertung/SKILL.md`.
**Affected outputs:** `STATE_MACHINE`, `PRIORITY_DISTRIBUTION`, `HOP_DISTRIBUTION`, and every awk-based
section when the input contains non-ASCII bytes.

> **Scope note for the implementer.** Every file:line below was read against `tools/loganalyse.sh` at
> `6157e3fe`. Re-verify before editing after any rebase. All six changes land in a small number of
> disjoint files — see §8 for the `/orchestrate-waves` split.

---

## Summary

| ID          | Type        | Severity | File / location                                  | One-liner                                                                   |
| ----------- | ----------- | -------- | ------------------------------------------------ | --------------------------------------------------------------------------- |
| **TOOL-01** | BUG         | Medium   | `tools/loganalyse.sh:401`                        | Counts normal CSMA backoff (`rc=-1`) as State-Machine errors → false FEHLER |
| **TOOL-02** | BUG         | Medium   | `tools/loganalyse.sh:1006`                       | Priority-drop breakdown double-counts via `replaced_by_prio=` substring     |
| **TOOL-03** | BUG         | Low      | `tools/loganalyse.sh:182,185,188`                | Hop regex `H[0-9]{2}` matches telemetry payload (`H19/B=…`) → bogus hops    |
| **TOOL-04** | BUG         | High     | `tools/loganalyse.sh` (all awk sections)         | One corrupt byte aborts awk mid-file → every awk section silently truncated |
| **TOOL-05** | ENHANCEMENT | Medium   | `tools/loganalyse.sh` (arg handling)             | Auto-detect + convert raw firmware bracket timestamp format                 |
| **TOOL-06** | DOC         | Low      | `.claude/skills/logauswertung/SKILL.md` §12, §15 | Doc says "any rc!=0 is a bug" — wrong; align with TOOL-01/02                |

**Why this matters:** the DG0OPK run initially produced a **FEHLER verdict on the State Machine** (561/864/851
"errors") and a **misleading priority-drop breakdown** ("106 prio=1 drops") — both false. A naive reading
would have reported firmware bugs that do not exist. TOOL-04 additionally truncated the _first_ run at ~71 %
of the file without any error to stdout.

## Resolution (2026-08-24)

Implemented via `/orchestrate-waves` — Wave 1 (TOOL-01/02/03 + harness) with three Sonnet writers,
Wave 2 (TOOL-04/05) orchestrator-direct (gnarly pure-awk calendar math + one tightly-coupled function).

| ID      | Commit                                            | Verification                                                                             |
| ------- | ------------------------------------------------- | ---------------------------------------------------------------------------------------- |
| TOOL-01 | `893ba656`                                        | `MC_SM_ERRORS`/`MC_SM_CSMA_BACKOFF` split; DG0OPK: 0 / 561                               |
| TOOL-02 | `a67bf6cb`                                        | drop breakdown by dropped `prio+type`; DG0OPK: 751 `prio=5 type=40` + 4 `prio=4 type=21` |
| TOOL-03 | `9226b65c`                                        | ` S`-anchored hop; DG0OPK hops H00–H05 only, no H19                                      |
| tests   | `b7a54f2f`                                        | `tools/mock/test_loganalyse.py` + fixture (TOOL-01/02/03), red→green                     |
| TOOL-04 | `5f51627e`                                        | `normalize_log()` byte-sanitize under `LC_ALL=C`; 0xA0 stripped, no `towc`, full output  |
| TOOL-05 | `4bdf27a6`                                        | raw `[EPOCHDAY.HHh…]` → `YYYY-MM-DD` (pure-awk Gregorian); real raw log 2026-08-23..24   |
| TOOL-06 | local (`.claude/…SKILL.md`, git-ignored — no SHA) | §12/§Status reworded; drop-breakdown + hop notes added                                   |

Test suite: `python3 -m unittest discover -s tools/mock -p 'test_loganalyse.py'` → 5/5.

**Deviations from the plan (all approved constraints or forced by the environment):**

- **6 commits, one per TOOL-id** as requested — but the shared regression harness could not be split
  per-id without interactive staging (`git add -p` is disabled here), so it is one commit
  (`b7a54f2f`) after the three fixes it validates. TOOL-04/05 each still carry their own test.
- **TOOL-06 is not a git commit**: `.claude/` is git-ignored (`.gitignore:11` rule `.*`). The edit is
  applied on disk (the skill is now correct); traceability lives here and in `BACKLOG.md` §3.8.
- **CI wiring deferred** (user choice): the test runs locally
  (`python3 -m unittest discover -s tools/mock -p 'test_loganalyse.py'`), not in `ci-build.yml`.
- **TOOL-04 red-before is macOS-specific**: the awk mid-file abort reproduces on macOS `awk`; on
  Linux `gawk` the stray byte passes through instead. The test asserts the byte is stripped from
  output, which is red pre-fix and green post-fix on both platforms.

---

## TOOL-01 — CSMA backoff counted as State-Machine errors (Medium)

**Location:** `tools/loganalyse.sh:398-403` (`section "STATE_MACHINE"`).

**Current code:**

```bash
echo "MC_SM_TOTAL: $(grep -c 'MC-SM' "$LOGFILE" 2>/dev/null; true)"
echo "MC_SM_ERRORS: $(grep 'MC-SM' "$LOGFILE" | grep -vc 'rc=0' 2>/dev/null; true)"
grep "MC-SM" "$LOGFILE" | grep -v "rc=0" | head -10 || true
```

**Symptom:** `MC_SM_ERRORS` reported 561 (DG0OPK-11), 864 (-12), 851 (-13). The skill (SKILL.md §12) says
"All should be rc=0. Any rc!=0 is a bug", which turns this into a false FEHLER verdict.

**Root cause:** every one of those lines is the single transition `TX_PREPARE -> IDLE rc=-1`. Per the
firmware, that is the **normal CSMA backoff** taken when CAD finds the channel busy:

`src/esp32/esp32_main.cpp:2529-2540` (identical logic in `src/nrf52/nrf52_main.cpp:1461`):

```cpp
else {   // channel busy confirmed — backoff
    cad_attempt++;
    csma_timeout = csma_compute_timeout(cad_attempt);
    printfdeb("[MC-SM] TX_PREPARE -> IDLE rc=-1\n");
    printfdeb("[MC-DBG] CAD_BUSY attempt=%d next_timeout=%lu\n", cad_attempt, csma_timeout);
```

`rc=-1` here means "did not transmit this cycle, channel busy", not an error.

**Evidence (traceable):** the count equals the CAD-busy count exactly on all three nodes:

```
node11: CAD_BUSY events=561   rc=-1 count=561
node12: CAD_BUSY events=864   rc=-1 count=864
node13: CAD_BUSY events=851   rc=-1 count=851
```

and matches the CAD attempt distribution (node11: attempt=1 → 527, attempt=2 → 34, sum = 561).

**Fix:** separate the benign backoff from real errors.

```bash
CSMA_BACKOFF=$(grep -c 'TX_PREPARE -> IDLE rc=-1' "$LOGFILE")
REAL_ERR=$(grep 'MC-SM' "$LOGFILE" | grep -v 'rc=0' | grep -vc 'TX_PREPARE -> IDLE rc=-1')
echo "MC_SM_ERRORS: $REAL_ERR"
echo "MC_SM_CSMA_BACKOFF: $CSMA_BACKOFF"
grep "MC-SM" "$LOGFILE" | grep -v "rc=0" | grep -v 'TX_PREPARE -> IDLE rc=-1' | head -10 || true
```

**Verification target (DG0OPK):** `MC_SM_ERRORS` = 0/0/0, `MC_SM_CSMA_BACKOFF` = 561/864/851.

---

## TOOL-02 — Priority-drop breakdown double-counts (Medium)

**Location:** `tools/loganalyse.sh:1004-1006` (`section "PRIORITY_DISTRIBUTION"`, "Priority drops").

**Current code:**

```bash
echo "--- Priority drops ---"
grep 'RING_DROP_PRIO\|RING_DROP_NEW' "$LOGFILE" | wc -l | awk '{printf "  Total priority drops: %d\n", $1}'
(grep 'RING_DROP_PRIO' "$LOGFILE" | grep -oE 'prio=[0-9]' | sort | uniq -c | sort -rn) || true
```

**Symptom:** the per-priority breakdown for DG0OPK-11 read `440 prio=5, 193 prio=4, 143 prio=3,
106 prio=1, 4 prio=2` — implying 106 high-priority (`prio=1`) packets were dropped.

**Root cause:** two defects in the last line.

1. `grep -oE 'prio=[0-9]'` also matches the substring `prio=N` inside `replaced_by_prio=N`, so each
   `RING_DROP_PRIO` line contributes **both** the dropped packet's `prio=5` _and_ the incoming winner's
   `replaced_by_prio=N`. The low-priority counts are the **incoming winners**, not dropped packets.
2. It only greps `RING_DROP_PRIO`, ignoring `RING_DROP_NEW` (312 of 755 drops on node11).

A `RING_DROP_PRIO` line looks like:
`RING_DROP_PRIO slot=18 prio=5 type=40 msg_id=… replaced_by_prio=1 src=rx_ack_fwd`
— the dropped packet is `prio=5 type=40`; `replaced_by_prio=1` is the packet that displaced it.

**Evidence (traceable):** extracting the _dropped_ packet's own `prio`+`type`:

```
node11: 751 prio=5 type=40  +  4 prio=4 type=21   (= 755 total)
node12: 173 prio=5 type=40                         (= 173 total)
node13: 110 prio=5 type=40                         (= 110 total)
```

i.e. ~100 % of dropped packets are `type=40` heartbeats (prio 5). **No text/ACK dropped.** Type codes
confirmed in `src/aprs_functions.cpp:154` (`0x3A` text, `0x21` position, `0x40` heartbeat).

**Fix:** count the dropped packet's `prio`+`type` for both drop kinds.

```bash
echo "--- Dropped packets by prio+type ---"
grep -E 'RING_DROP_(PRIO|NEW)' "$LOGFILE" | grep -oE 'prio=[0-9]+ type=[0-9A-Fa-f]+' | sort | uniq -c | sort -rn || true
```

**Verification target (DG0OPK):** node11 → `751 prio=5 type=40`, `4 prio=4 type=21`; node12 → `173 prio=5 type=40`;
node13 → `110 prio=5 type=40`.

---

## TOOL-03 — Hop regex matches telemetry payload (Low)

**Location:** `tools/loganalyse.sh:182,185,188` (`section "HOP_DISTRIBUTION"`).

**Current code:**

```bash
grep "MH-LoRa:" "$LOGFILE" | grep -oE 'H[0-9]{2}' | sort | uniq -c | sort -rn || true   # + RX-LoRa2:, TX-LoRa:
```

**Symptom:** hop tables contained bogus rows `125 H19` and `1 H26` (node11 MH). Valid MeshCom hops are
H00–H05.

**Root cause:** `H[0-9]{2}` also matches hop-shaped substrings inside telemetry payloads, e.g.
`…empfangen auf DB0IBH#OV H19/B=100/A=001978/R=20`. `H19` there is a sensor/locator field, not a hop.

**Evidence (traceable):**

```
$ grep -oE '.{20}H19.{20}' node11.log | head -1
ommen auf DB0IBH#OV H19/B=100/A=001978/R=20
```

**Fix:** anchor on the ` S<digit>` signal field that always follows the real hop count in MH/RX/TX-LoRa
lines (`… x1320F184 H01 S1 T0 M01 …`). Confirmed present for all three line types; on a payload line the
real hop `H01 S1` is captured and `H19/B=` is excluded.

```bash
grep "MH-LoRa:" "$LOGFILE" | grep -oE 'H[0-9]{2} S[0-9]' | grep -oE 'H[0-9]{2}' | sort | uniq -c | sort -rn || true
```

Apply identically to the `RX-LoRa2:` and `TX-LoRa:` lines. (Fallback if the anchor ever fails to hold:
restrict to `H0[0-9]`.)

**Verification target (DG0OPK):** hop tables contain only H00–H05; no H19/H26.

---

## TOOL-04 — One corrupt byte aborts awk and silently truncates every section (High)

**Location:** structural — all awk invocations in `tools/loganalyse.sh` run under the caller's UTF-8
locale.

**Symptom:** the first DG0OPK run emitted `awk: towc: multibyte conversion failure on: '… LH:A7'`
(one-true-awk / macOS) and **stopped processing that awk stream at input record 224787 of 314287
(~71 %)**. Exit code was still 0. Downstream sections (NODES, MESSAGE_TYPES, HOP, …) were computed on a
truncated stream with no indication in stdout. Only 8 lines per file were corrupt, yet they truncated
everything.

**Root cause:** serial captures contain occasional non-UTF-8 bytes (RF/UART corruption). Under a UTF-8
`LC_CTYPE`, awk's `towc()` conversion fails on such a byte and aborts the run.

**Evidence (traceable):**

```
$ LC_ALL=C grep -c '[^[:print:][:space:]]' lora_dg0opk_11_23aug2026.txt
8
# first run (UTF-8 locale): "multibyte conversion failure … input record number 224787"; UNIQUE_NODES truncated
# after LC_ALL=C + sanitize: node11 output grew 3955 -> 5011 lines (previously-lost sections restored)
```

**Fix:** in the normalization step (see TOOL-05), always pipe input through a byte filter and run the
script's awk under a byte-safe locale:

```bash
LC_ALL=C tr -cd '\11\12\40-\176' < "$SRC" > "$CLEAN"   # keep tab, newline, printable ASCII
```

Then use `$CLEAN` as the log. (This runs regardless of timestamp format — it protects serial_monitor
logs too.)

**Verification target:** a fixture containing a `\xNN` byte mid-line produces complete output; no
`towc`/`multibyte` message; the section that previously truncated now reports full counts.

---

## TOOL-05 — Auto-detect and convert the raw firmware bracket timestamp format (Enhancement, Medium)

**Location:** `tools/loganalyse.sh` — new `normalize_log()` after arg parsing (line ~20), applied to
`$LOGFILE` and `$LOGFILE2`.

**Motivation:** the script header states it expects serial_monitor.py format
(`2026-03-06 15:36:58.248  <text>`; time parsed as `$2`, `split($2,":")`, in 14 awk blocks). Raw node
captures instead use `[EPOCHDAY.HHh.MMm.SSs.mmm] <text>` (the DG0OPK logs). Fed raw, every time-bucketed
section breaks. Today this required a manual pre-conversion before the script could be used at all.

**Design (minimally invasive — does not touch the 14 awk blocks):**

1. **Detect** on the first ~50 non-empty lines: `^\[[0-9]+\.[0-9]+h\.[0-9]+m\.[0-9]+s\.[0-9]+\]` = raw
   firmware format; `^[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:` = serial_monitor (already fine).
2. **Always** byte-sanitize (TOOL-04).
3. **If raw:** build an epoch-day → date map (`date -u -r $((day*86400)) +%Y-%m-%d`; macOS awk has no
   `strftime`, so pass the map via `-v`), then awk-convert `[day.HHh.MMm.SSs.mmm] rest` →
   `YYYY-MM-DD HH:MM:SS.mmm  rest`. Continuation lines (no leading bracket) inherit the last stamp.
4. Write to `mktemp`; point `LOGFILE`/`LOGFILE2` at it; `trap 'rm -f …' EXIT` to clean up.

Reference implementation used for the DG0OPK run (validated): epoch day 20688 = 2026-08-23, bracket time
is UTC (embedded `HH:MM:SS` on some lines is local CEST +2). See the `logauswertung-raw-firmware-log-prep`
memory for the exact awk.

**Verification target:** a raw-format fixture auto-converts and yields the same core counts
(MH heard, message types, hop table, CAD distribution) as the equivalent serial_monitor-format fixture.

---

## TOOL-06 — SKILL.md guidance is wrong for the State Machine (Doc, Low)

**Location:** `.claude/skills/logauswertung/SKILL.md` §12 ("State Machine Health") and §15/priority-drops.

**Symptom:** §12 says "All should be rc=0. Any rc!=0 is a bug." — which drove the false FEHLER reading
in TOOL-01. §15's priority-drop guidance implies the (double-counted) per-prio breakdown is meaningful.

**Fix:** document that `TX_PREPARE -> IDLE rc=-1` is normal CSMA backoff (now reported separately as
`MC_SM_CSMA_BACKOFF`), and that the dropped-packet breakdown is by dropped `prio`+`type` (expected:
mostly `prio=5 type=40` heartbeats under congestion). Add a one-line note that the script auto-handles
raw firmware logs (TOOL-05).

---

## 8. Implementation plan (`/orchestrate-waves`)

Disjoint file ownership → three parallel writers, then a gate.

| Writer | Exclusive files                                | Items                       |
| ------ | ---------------------------------------------- | --------------------------- |
| A      | `tools/loganalyse.sh`                          | TOOL-01, -02, -03, -04, -05 |
| B      | `tools/testdata/*`, `tools/test_loganalyse.sh` | fixtures + assertions (§9)  |
| C      | `.claude/skills/logauswertung/SKILL.md`        | TOOL-06                     |

Note: TOOL-01…05 all live in one file (`loganalyse.sh`) and therefore run in **one** writer (A),
sequentially — no intra-file parallelism. The parallelism is A ∥ B ∥ C. Gate after the wave: run
Writer B's harness against Writer A's script.

## 9. Regression / test approach (Definition of Done, BACKLOG §2.5)

Two fixtures with **identical content** in both formats (raw bracket + serial_monitor), each containing:
one corrupt `\xNN` byte, one `H19/B=` telemetry payload line, one `RING_DROP_PRIO … replaced_by_prio=1`,
one `RING_DROP_NEW`, and one `TX_PREPARE -> IDLE rc=-1`. Assertions:

- `MC_SM_ERRORS` = 0 and `MC_SM_CSMA_BACKOFF` ≥ 1 (TOOL-01)
- dropped-packet breakdown shows only the seeded dropped priorities, no `replaced_by_prio` leakage (TOOL-02)
- hop table contains no `H19` (TOOL-03)
- full output produced despite the corrupt byte; no `towc` message (TOOL-04)
- the raw fixture auto-converts and matches the serial_monitor fixture's core counts (TOOL-05)

Must be **red before, green after**. No hardware needed.

## 10. Open decisions (confirm before implementing)

1. TOOL-01 output: add a separate `MC_SM_CSMA_BACKOFF` field (recommended) vs. silently subtract backoff
   from the error count.
2. Byte-sanitizing (TOOL-04): always apply, including to serial_monitor logs (recommended, as a safety
   net) vs. only for raw-format input.
3. Test harness location: new `tools/test_loganalyse.sh` + `tools/testdata/` vs. an existing convention.
