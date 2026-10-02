# Finder: Source-Code Claim Verification — adr-nc-importance-backoff.md (Rev. 2)

Scope: every file:line citation, constant, and "this behavior/code does (not) exist" claim in
the ADR, checked against the actual tree on branch `v4.35p_prio`
(`/Users/martinwerner/WebDev/MeshCom-Firmware-DEV-Main`).

---

## F1: "NC persistiert in Flash / ueberlebt Reboots" is board-specific (T-Deck/T-Deck+ only, SD card, gated by a setting) — ADR presents it as general firmware behavior

**ADR locations:**

- Line 203 (Datenqualitaet-Tabelle): "Persistenz | `mheardNCount[]` wird in Flash geschrieben und ueberlebt Reboots — nach Standortwechsel wird mit NC-Werten vom alten QTH gerechnet | `mheard_functions.cpp:178`"
- Line 845 (Kap. 8.2 Tabelle): "Persistenz | Flash (`mheard_functions.cpp:205-208`)"
- Lines 998-1007 (Risiko-Analyse): "**Risiko: Stale NC-Werte aus dem Flash nach Standortwechsel** — `mheardNCount[]` und die Pfadtabelle werden persistiert (`mheard_functions.cpp:178`, `:205-208`). Nach einem Reboot an einem anderen QTH rechnet der Node mit der Nachbarschaft des alten Standorts weiter, bis die Eintraege altern (1 h bzw. 12 h)."

**Claim:** implies this is a general firmware property applicable to all/most boards in the fleet
(the risk section frames it as a rollout-wide concern for "der Node" generically, no board
qualifier).

**Actual code:** `saveMHeardPersistence()` (`src/mheard_functions.cpp:151-181`, the write at
line 178 cited) and `savePathPersistence()` (`:183-211`, the writes at lines 205-208 cited) are
each wrapped in:

```cpp
#if defined(BOARD_T_DECK) || defined(BOARD_T_DECK_PLUS)
```

and additionally gated at runtime by `meshcom_settings.node_persist_to_sd`. They write to an
**SD card** (`SD.open("/mheard.dat", ...)`), not internal flash. The corresponding
`loadMHeardPersistence()` / `loadPathPersistence()` (`:939-972`, `:974-...`) are equally
`BOARD_T_DECK`-gated, and the **only caller of either load function in the entire tree** is
`src/t-deck/tdeck_main.cpp:158-159` (`loadMHeardPersistence(); loadPathPersistence();`) —
confirmed via `grep -rn "loadMHeardPersistence\|loadPathPersistence" src/`.

On every other board (ESP32 classic, Heltec V3, T-Beam classic/1262/Supreme, RAK4631/nRF52,
E22, etc. — per the evidence pack's own hardware breakdown: TLORA 32%, HELTEC V3 20%,
TBEAM V1.2 16%, TBEAM V1.1 9%, i.e. the large majority of the fleet), `mheardNCount[]` and the
path table are **held only in RAM** and are reset to zero by `initMheard()` on every boot —
there is no persistence path for them at all. `grep -rln "mheardCalls\|mheardNCount\|mheardEpoch" src/`
turns up no other writer.

**Severity:** high — the "stale NC after relocation" risk as written applies only to T-Deck /
T-Deck Plus with the SD-persist setting enabled, not to the fleet in general. This changes the
practical relevance of that risk entry materially (it's a niche/optional-feature concern, not a
firmware-wide rollout risk), and the offene-Punkt "Gegenmassnahme: offen" implicitly treats it as
broadly applicable.

**Correction for Rev. 3:** qualify all three citations as T-Deck/T-Deck-Plus-only
(`#if defined(BOARD_T_DECK) || defined(BOARD_T_DECK_PLUS)`, additionally gated by
`meshcom_settings.node_persist_to_sd`), note it is SD-card storage not flash, and either narrow
the risk section to that board class or drop it if out of scope for Stufe 1's target boards.

---

## F2: "/N Kappung bei 99" citation points at the wrong code block (CO2 payload, not the NC cap)

**ADR location:** line 197 (Datenqualitaet-Tabelle): "Kappung im Positions-Payload | `/N` wird
bei **99** gekappt | `loop_functions.cpp:3753-3759`"

**Claim substance is correct** (verified — see below), but the cited range is wrong.

**Actual code:** lines 3751-3758 are the **CO2 (`/C=`) telemetry block**:

```cpp
3751        if(co2 > 0 && bMCU811ON)
3752        {
3753            snprintf(cversion, sizeof(cversion),  "%s", "/V=2");
3754
3755            snprintf(cco2, sizeof(cco2), "/C=%.0f", co2);
3756            if(memcmp(cpress, "/C=nan", 6) == 0)
3757                return "";
3758        }
```

The actual `/N` cap is at lines 3760-3767:

```cpp
3760        int incnt = getMheardCount();
3761        if(incnt > 0)
3762        {
3763            if(incnt > 99)
3764                incnt=99;
3765
3766            snprintf(cncnt, sizeof(cncnt), "/N%i", incnt);
3767        }
```

**Severity:** medium (the cited lines are ~7-14 lines off and land on unrelated CO2 code, not
just trivial drift within the same logical block).

**Correction for Rev. 3:** change citation to `loop_functions.cpp:3760-3767` (cap logic at
3763-3764, emission at 3766).

---

## F3: "85-124 H00 nodes observed" comment is misattributed to MAX_MHPATH/line 181 — it's actually the MAX_MHEARD sizing comment on line 180; MAX_MHPATH=100 (S3/nRF52) is not clearly "kleiner als 85-124"

**ADR locations:**

- Line 922 (Kap. 8.7): "**Saettigung auch hier.** `MAX_MHPATH` ist auf allen Boards kleiner als
  die 85–124 beobachteten H00-Knoten (Kommentar in `configuration_global.h:181`)."
- Line 1119 (Alternative 7): "Dazu kommt Saettigung: `MAX_MHPATH` ist auf jedem Board kleiner als
  die 85–124 beobachteten H00-Knoten (`configuration_global.h:181`)."

**Actual code:**

```
180:#define MAX_MHEARD 80   // max count of messages in mheard ringbuffer (was 20, 85-124 H00 nodes observed)
181:#define MAX_MHPATH 100  // max count of messages in mhpath ringbuffer (was 30, multiple paths per node)
```

The "85-124 H00 nodes observed" text is the sizing rationale for **MAX_MHEARD** (line 180), not
MAX_MHPATH (line 181) — MAX_MHPATH's own comment gives a different rationale ("multiple paths
per node") and does not cite the 85-124 figure at all.

Additionally, the substantive claim is imprecise for the largest board class: MAX_MHPATH=100
(ESP32-S3/nRF52840) is **not** "kleiner als die 85–124 beobachteten H00-Knoten" if the observed
count is at the low end of that range (85) — 100 > 85. The claim only clearly holds for the
XML/SBUFFER (50), ESP32-classic (40), and T-Beam (10) branches.

**Severity:** low-medium (wrong line for the cited comment, plus a substantive overstatement for
the S3/nRF52 branch).

**Correction for Rev. 3:** cite `configuration_global.h:180` (MAX_MHEARD's comment) as the source
of the 85-124 figure, and either soften the MAX_MHPATH claim to "on 3 of 4 board classes" or state
the S3/nRF52 case separately (100 vs. an observed range whose lower bound it exceeds).

---

## F4 (minor/low): `updateHeyPath()` span cited as `382-553`, actual closing brace is line 554

**ADR location:** line 835 (Kap. 8.2): "`updateHeyPath()`, `mheard_functions.cpp:382-553`"

**Actual code:** function opens at `382:void updateHeyPath(struct mheardLine &mheardLine)` and
closes at `554:}` (the `savePathPersistence();` call cited elsewhere is at line 553, one line
before the closing brace). One-line drift, no behavioral impact.

**Severity:** low.

**Correction for Rev. 3:** `382-554` (or leave as-is; immaterial).

---

## Verified as correct (no re-check needed)

- **CSMA base table** (Kontext, "Aktuelle CSMA-Parameter"): `CSMA_PRIO_BASE_1..5` =
  3000/3000/4500/5500/5500 and `CSMA_PRIO_SLOTS_1..5` = 10/10/10/10/10, all confirmed exactly at
  `configuration_global.h:266-277`. Base=4500/Slots=10 for Prio 3 (Relay) matches the Rev.2
  changelog's stated correction from a wrong "4000" in Rev.1.
- **`CSMA_SLOT_SIZE` = 35** confirmed at `configuration_global.h:221`, comment "28ms CAD + 2ms
  TX-Switch + 5ms Safety" matches verbatim.
- **Retry reduction**: `csma_compute_timeout_prio()` at `src/lora_functions.cpp:2153` confirmed
  (`unsigned long csma_compute_timeout_prio(int attempt, uint8_t priority) {`); body matches ADR
  description exactly — `attempt>=2 → base*2/3`, `attempt>=1 → base*5/6`,
  `attempt>=CSMA_MAX_ATTEMPTS(3) → CSMA_RAPID_RX_MS(100)` (both constants confirmed at
  `configuration_global.h:224-225`). Arithmetic 4500×5/6=3750 and 4500×2/3=3000 confirmed correct
  (integer division, exact).
- **`getMheardCount()`** at `mheard_functions.cpp:556` confirmed exactly, including the 1-hour
  window (`(mheardEpoch[iset]+(60*60)) > getUnixClock()`).
- **`sendHey()` payload** `"R" + String(getMheardCount()) + ";"` confirmed exactly at
  `loop_functions.cpp:4273` (evidence-pack citation, not directly quoted in ADR body but used to
  justify the R<NC>; format described in Kap. "Kanal 2: HEY").
- **NC_self key = `msg_source_last`**: confirmed — `mheardLine.mh_callsign = aprsmsg.msg_source_last;`
  at `lora_functions.cpp:568` exactly, and `updateMheard()` (`mheard_functions.cpp:214-...`) uses
  `mheardLine.mh_callsign` as the dedup/insert key for `mheardCalls[]`, confirming NC_self only
  counts direct (last-hop) neighbors, not multi-hop originators.
- **`MAX_MHEARD` values** 80 (S3/nRF52840, line 180), 50 (XML/SBUFFER, line 172), 30 (ESP32
  classic, line 195), 10 (T-Beam, line 187) — all confirmed exactly, table at
  `configuration_global.h:172ff` is accurate.
- **`MAX_DEDUP_RING` values** 100 (S3/nRF52840, line 183), 70 (ESP32 classic, line 198), 60
  (XML/SBUFFER, line 175), 10 (T-Beam, line 190) — all confirmed exactly against
  `configuration_global.h:175ff`.
- **`MAX_MHPATH` values** 100 (S3/nRF52840, line 181), 50 (XML/SBUFFER, line 173), 40 (ESP32
  classic, line 196), 10 (T-Beam, line 188) — all confirmed exactly (see also F3 re the comment
  misattribution).
- **`/N` capped at 99**: confirmed in substance — `if(incnt > 99) incnt=99;` at
  `loop_functions.cpp:3763-3764` (see F2 for the line-number correction).
- **Kanal 1 (Position → `mheardNCount[]`)**: `sscanf(decode_text, "%d", &aprspos.ncnt);` confirmed
  exactly at `aprs_functions.cpp:905`; `mheardNCount[ipos]=aprspos.ncnt;` confirmed exactly at
  `lora_functions.cpp:648`; `mheardNCount[iset]=aprspos.ncnt;` confirmed exactly at
  `lora_functions.cpp:676`.
- **Kanal 2 (HEY → `updateHeyPath()`, R<NC>; gated by `mheardCalls[]` membership)**: confirmed
  exactly at `mheard_functions.cpp:420-446` — the assignment `mheardNCount[imh] = mheardLine.mh_ncount;`
  (line 446) sits inside the `for(imh<MAX_MHEARD)` loop (line 394) that is itself gated by
  `is_equ(mheardCalls[imh], mheardLine.mh_sourcecallsign.c_str())` (line 398), i.e. the NC value
  is only recorded for originators already present in `mheardCalls[]`, exactly as claimed.
- **`updateHeyPath()` overall structure**: function body `382-554` (off by one from ADR's `382-553`,
  see F4); "nur relayte HEYs (`if(ips <= 0) return;`)" confirmed verbatim at
  `mheard_functions.cpp:525-526`; shortest-path-wins with `0x7F` sentinel confirmed at lines
  495 (init) and 509 (`if((mheardPathLen[ipos] & 0x7F) < mheardLine.mh_path_len) return;`);
  gateway bit `0x80` confirmed at line 543 (`mheardPathLen[ipos] = mheardLine.mh_path_len | 0x80;`,
  gated on `mh_destinationpath == "HG"`); 12h path aging confirmed at line 471
  (`(mheardPathEpoch[iset]+(60*60*12)) < getUnixClock())`).
- **Path-table persistence writes** at `mheard_functions.cpp:205-208` (four `file.write` calls)
  confirmed exactly as a citation for _what_ is written; see F1 for the board-scope correction.
- **`iReceiveTimeOutTime` reset on every RX end**: confirmed exactly at `lora_functions.cpp:1338`
  (`iReceiveTimeOutTime = millis();`), inside `OnRxDone`-style handling (also present at lines 452
  and 2079 for other reset paths, consistent with "kein globaler Nullpunkt").
- **`bSHORTPATH` default `false`**: confirmed exactly at `loop_functions.cpp:166`
  (`bool bSHORTPATH = false;`). **`--shortpath`/"shortpath on" toggle**: confirmed exactly at
  `command_functions.cpp:581` (`bSHORTPATH=true;` inside the `"shortpath on"` command handler;
  `"shortpath off"` sets it back to false at line 568). Behavioral claim in Kap. 8.7 ("dann bleibt
  nur Origin,letztesRelay") confirmed at `lora_functions.cpp:1246-1254`: when `bSHORTPATH` is
  true, `msg_source_path` is rewritten to exactly `msg_source_call + "," + own_call`, collapsing
  all intermediate hops — matches the claim precisely.
- **`is_new_packet()` filters only the RX path**: confirmed — function defined at
  `lora_functions.cpp:1427` exactly (`bool is_new_packet(uint8_t compBuffer[4])`), checks only
  against `ringBufferLoraRX[]` (the RX dedup ring). Its only two call sites in the tree (lines 259
  and 734 of `lora_functions.cpp`) are both in RX-side packet handling, not in `doTX()` or the TX
  ring. Confirmed no call from `txring_functions.cpp`.
- **`txring_functions.cpp` has no cancel path**: confirmed — `grep -ni "cancel"` over
  `txring_functions.cpp`/`.h` returns zero hits; the only `RING_STATUS_EXT_PENDING` references
  (lines 130-132, 301) are about skipping re-selection of a still-pending external-radio slot, not
  about cancelling a queued relay on duplicate receipt. Together with the `is_new_packet()` finding
  above, this fully supports "kein Suppressionspfad mehr" / "keine Reaktivierung eines Schalters".
- **Commit `60ea7d8`** ("NC-basierte Relay-Suppression") exists in repo history with exactly the
  described feature (`git show 60ea7d862b3b9fabd4c53b044902a564b829c883` — commit message: "feat:
  NC-basierte Relay-Suppression — reduziert Relay-Lawine bei dichter Vermaschung", touches
  `configuration_global.h` and adds `docs/design_relay_suppression.md`). No `RELAY_SUPPRESS`/
  `relay_suppress` symbol exists anywhere in current `src/` — consistent with "implementiert und
  wieder entfernt". Note: this commit is not an ancestor of current HEAD (likely superseded by a
  later rebase/squash of the branch it lived on) — this is a git-history mechanic, not a
  documentation error, and doesn't contradict the ADR's narrative.
- **`docs/hey-supp.md` trickle numbers**: "5 Knoten, 9.5h" confirmed (file header: "9.5h", "~21:21
  bis ~06:55 (ca. 9.5 Stunden)"); "mean 53%" confirmed ("Durchschnittliche Einsparung: 53%"); OE3XWJ
  75% confirmed exactly (table row "OE3XWJ-12 | ... | 75%").
- **Priority-band slot counts uniformly 10** (Kap 4.6): confirmed — all five
  `CSMA_PRIO_SLOTS_1..5` = 10 in `configuration_global.h:273-277`.
- **Proposed code (5.1-5.3) compiles conceptually against real declarations**:
  - `mheardCalls[MAX_MHEARD][10]` is `char[][]`; ADR's `mheardCalls[i][0] != 0x00` matches the
    real access pattern used throughout `mheard_functions.cpp` (e.g. line 396, 562).
  - `mheardEpoch[MAX_MHEARD]` is `unsigned long`; ADR's `unsigned long now = getUnixClock();` and
    `(mheardEpoch[i] + 60*60) > now` match `getUnixClock()`'s real return type
    (`unsigned long getUnixClock();`, `loop_functions.h:15`) and the real usage pattern at
    `mheard_functions.cpp:564`.
  - `mheardNCount[MAX_MHEARD]` is `int` (declared `mheard_functions.cpp:30`); ADR's
    `int nc_reported = mheardNCount[i];` matches.
  - `random(0, N)` Arduino-style two-arg call matches the real usage in
    `csma_compute_timeout_prio()` (`lora_functions.cpp:2173`: `random(0, slots + 1)`).
  - `bDisplayInfo` and `bLORADEBUG` both exist as `extern bool` (`loop_functions_extern.h:41,56`;
    defined `loop_functions.cpp:95,106`).
  - `MSG_PRIO_NORMAL` (=3) and the `switch(priority)` structure in `csma_compute_timeout_prio()`
    match exactly what section 5.3 proposes to edit (verified real code shown above under CSMA
    base table).
  - None of the proposed new symbols (`RELAY_IMP_CAP`, `RELAY_TOTAL_SLOTS`, `RELAY_JITTER_WIDTH`,
    `RELAY_IMP_MIN_KNOWN_PCT`, `getNetImportance`, `getNetImportanceKnownPct`) currently exist
    anywhere in `src/` — no naming collisions.
- **Slot-mapping and backoff arithmetic** in Kap. 4.3/4.4 (importance→slot_start→backoff-window
  for every row of both tables, and attempt-1/attempt-2 base reductions) recomputed independently
  and matches the ADR's stated numbers exactly in every row checked (8.0/7.2/5.0/2.0/1.0/0.5/0.2).

---

## Not independently re-verifiable from source (out of scope for this pass)

- BergLog field-data tables (Kap. "Felddaten: BergLog 2026-03-13/14") — no BergLog raw data present
  in this tree; taken as given per the ADR's own framing (historical field capture, not current
  code).
- mcmap/interlink production statistics (nct distribution, importance simulation percentiles,
  known_ratio, storm/duplicate figures) — these come from the evidence pack's separate MCP-sourced
  analysis, not from firmware source; not this finder's angle.

---

## Summary

- Findings: 4 total — 1 high (F1, persistence claim wrongly generalized from T-Deck-only to the
  whole fleet), 1 medium (F2, wrong line range for the /N=99 cap, points at CO2 code), 1 low-medium
  (F3, MAX_MHPATH saturation comment misattributed from MAX_MHEARD's line + imprecise for S3/nRF52),
  1 low (F4, one-line drift on `updateHeyPath()` span).
- ~30+ file:line citations and constants checked; the large majority (CSMA parameters, NC ingest
  channels, `updateHeyPath()` internals, SHORTPATH behavior, `is_new_packet()`/dedup-ring scope,
  suppression absence, trickle stats, proposed-code type/API compatibility) verified byte-exact
  correct.
