# Finder A -- Factual claims about today's code

Repo: /Users/martinwerner/WebDev/MeshCom-Firmware-DEV-Main, branch feature-neighbour-matrix,
HEAD 082c24128d250b9195509c86b470fbb6a182dd06 (src identical to d9fc5c5a, the commit the paper
cites). All line numbers below are against this HEAD unless marked "upstream".

## Candidates

### 1. MHeard-Eintrag arithmetic: paper says 55 B, the cited fields sum to 54 B (medium)

Paper, section 2 table, row "MHeard-Eintrag":

> "55 B: Datensatz 20, Rufzeichen 10, lat, lon, alt, Epoche, millis, NCNT je 4; dazu 6 B
> Verwaltung"

Code (src/mheard_functions.cpp:54-96):

- `MheardRecord mheardRecords[MAX_MHEARD]` -- struct is 20 B (comment at
  src/mheard_record.h:34: "20 Byte je Eintrag (4-Byte-ausgerichtet)").
- `char mheardCalls[MAX_MHEARD][10]` -- 10 B.
- `float mheardLat[MAX_MHEARD]` -- 4 B
- `float mheardLon[MAX_MHEARD]` -- 4 B
- `int mheardAlt[MAX_MHEARD]` -- 4 B
- `unsigned long mheardEpoch[MAX_MHEARD]` -- 4 B (32-bit on both ESP32 and nRF52)
- `uint32_t mheardMillis[MAX_MHEARD]` -- 4 B
- `int mheardNCount[MAX_MHEARD]` -- 4 B

That is exactly the six "je 4" fields the paper itself lists. Sum: 20 + 10 + 6*4 = **54 B**, not
55 B. The paper's own field list is correct and complete; only the total is off by one byte.

Why it matters: this 55 B figure is carried forward verbatim into the "Mengengerüst" table
("Bytes je direktem Nachbarn | 55 MHeard + Matrixzeile") and into the comparison basis for the
new 12-byte direct-neighbour slot (4.3), i.e. it's used as a factual "today" baseline for a
savings argument. The nm-measured totals in 4.7 are independent (measured, not computed from this
formula), so this does not change the headline RAM numbers -- but the per-entry breakdown itself
is arithmetically wrong as printed.

### 2. Citation line-number drift, three places (low)

- Paper section 2: "MHeard-Schlüssel ... src/lora_functions.cpp:1076, :1197". Line 1076 is a
  comment line ("// (aprs_structures.h), mcSet() also nur eine Kopie, keine"); the actual
  `mcSet(mheardLine.mh_callsign, ..., aprsmsg.msg_source_last)` assignment that establishes
  msg_source_last as the MHeard key is at **line 1078**. (:1197, the updateMheard() call site, is
  exact.)
- Paper section 2: "Matrix ... nbrBit() liefert ab Index 32 still 0 | src/nbr_matrix.h 229 bis
  245, src/nbr_matrix.cpp:1109". `static uint32_t nbrBit(int idx)` is defined at
  **src/nbr_matrix.cpp:1107**, not 1109 (1109 is the closing brace's return statement line, off
  by 2).
- Paper section 2, "Via beim Empfänger" row: "Quelle Via: ... Aufrufer src/lora_functions.cpp:1827
  und src/loop_functions.cpp 750, 3446, 3715, 4839, 5025". Line 1827 is the **checkVia()** call
  site (`checkVia(aprsmsg);`), which is correct for checkVia(). But the citation is presented as
  covering "checkMesh() und checkVia()" jointly, and **checkMesh()'s actual call site in
  lora_functions.cpp is line 1777** (`if(!checkMesh(aprsmsg))`), which is never cited anywhere in
  the paper. A reader following the citation to find where checkMesh() is invoked will land on
  the checkVia() call instead.

None of these change any conclusion, but a verifier spot-checking citations will hit near-misses
here.

## Checked and found correct (no issue)

- MHeard key is msg_source_last, and updateMheard() is only reached when
  `!is_equ(msg_source_last, node_call)` (src/lora_functions.cpp:1057) -- confirmed.
- "Jeder Rahmentyp außer Binär-ACK und HN-Bericht" reaches updateMheard(): ACK (0x41) returns
  early from decodeAPRS() with msg_source_call left empty, so isUnconfiguredCall() drops it before
  the MHeard block (src/aprs_functions.cpp:150-151, src/lora_functions.cpp:958,
  src/configuration_global.h:48-57); HN-Bericht (payload '@', dest "HN") is intercepted and
  `return`s out of OnRxDone before the generic block, feeding only nbrNoteFrame/nbrNoteReport
  (src/lora_functions.cpp:773-810). Both exclusions confirmed, for the reasons implied.
- Pfadtabelle 71 B/Eintrag: mheardPathCalls[10] + mheardPathBuffer1[52] + mheardPathEpoch(4) +
  mheardPathMillis(4) + mheardPathLen(1) = 71 B exactly (src/mheard_functions.cpp:106-116).
- MAX_MHEARD 30/50/80/80 and MAX_MHPATH 40/50/100/100 across classic/E22_XML/S3/nRF52 --
  confirmed (src/configuration_global.h:301-334).
- getMheardCount() 1 h window (MHEARD_AGE_WINDOW_MS) -- confirmed, src/mheard_functions.cpp:691-703.
- Uhr-Schranke "ohne Datum ab 2025 kein Eintrag" -- confirmed exactly at
  src/mheard_functions.cpp:303 (`if(mcSliceToLong(mheardLine.mh_date, 0, 4) < 2025) return;`).
- Web MHeard 3 h window, sub_page_mheard() and sub_page_path() (line 1543 exact) -- confirmed,
  src/web_functions/web_functions.cpp:1499-1500, 1543-1550.
- Three static struct mheardLine (584 B, per code comment) on nRF52 in sendMheard(),
  showMHeard(), sub_page_mheard() (`#if defined(NRF52_SERIES) static ... #else` in each), vs
  stack-allocated on ESP32 -- confirmed all three sites
  (src/mheard_functions.cpp:861, 956, src/web_functions/web_functions.cpp:1480).
- NCNT deckel /N capped at 99 (src/loop_functions.cpp:4546-4547 area) -- confirmed.
- HEY payload "R%d;" from getMheardCount() inside sendHey() (src/loop_functions.cpp:5095) --
  confirmed.
- Trickle reset on neighbour-count change (src/esp32/esp32_main.cpp:3480-3487) -- confirmed.
- LORA_SNR_STABLE_MIN_DB == -16 (src/configuration_default.h:98) -- confirmed.
- via_functions.cpp checkMesh(): fork uses token-exact `pathNamesCall()` (no substring match);
  upstream/dev's checkMesh() uses `msg_destination_path.indexOf(node_call)` (substring) and ends
  with an unconditional `return true;` (i.e. relays even with bMESH==false) -- confirmed by
  diffing fork vs `git show upstream/dev:src/via_functions.cpp`. Matches the paper's claim
  precisely, including that this is attributed to upstream, not the fork.
- Relay rewrites destination path to target then applies own via: `mcSet(...msg_destination_path,
...msg_destination_call)` immediately followed by `checkVia(aprsmsg)` at exactly
  src/lora_functions.cpp:1825 and :1827 -- confirmed exact line numbers.
- Auto-Via selection code commented out since 22.07.2026, identical between fork and upstream/dev
  (same dead block, same comment date) -- confirmed via diff of via_functions.cpp checkVia().
- checkVia() caller line numbers 750, 3446, 3715, 4839, 5025 in loop_functions.cpp -- all five
  confirmed exact.
- shortVERSION() reads chars 3-4 (1-indexed) of SOURCE_VERSION via `memcpy(cfw, SOURCE_VERSION+2,
2)` -- confirmed, src/aprs_functions.cpp:30-34. "5.00" -> "00" -> shortVERSION()==0 confirmed
  by the substitution.
- fw>13 foot-to-meter guard at src/lora_functions.cpp:1145 and src/loop_functions.cpp:3120, and
  the <35 decode-branch guard at src/aprs_functions.cpp:531 -- all three line numbers exact.
- ME-step / text-only-feeds-ME semantics (src/nbr_matrix.h:220-245) and 2-hop window (only last
  two path tokens get rows) -- confirmed via code comments describing the actual implemented
  behaviour, matches 4c85d... commit series (8487ea2a per git log).
- nbrBit() returns 0 for idx outside [0,32) (uint32_t masks) -- confirmed logic, only the cited
  line number is off by 2 (see Candidate 2).
- HN-Bericht max 8 entries: `NBR_REPORT_MAX_ENTRIES 8` (src/nbr_matrix.h:130-131) -- confirmed.
- T-Deck persistence: 30 s throttle via `lastsaveMHEARDPersistence`/`lastsavePATHPersistence`,
  gated on `node_persist_to_sd`, files mheard.dat/mhpath.dat -- confirmed,
  src/mheard_functions.cpp:230-298.
- time_functions.cpp:131 is exactly the `mheard_time = getLatestMHeardTimestamp();` line inside
  loadTimePersistence() -- confirmed, and the surrounding function does combine
  saved_time/mheard_time/msg_time via max() as implied.
- 245 B BLE command-frame clamp: `if (len > 245)` at exactly src/loop_functions.cpp:716; comment
  reference in src/ble_json_frame.h:21 -- confirmed.
- bleJsonFrameFailSoft() exists today (src/ble_json_frame.h:42-58) and does drop trailing
  non-"TYP" object keys first when re-measuring -- confirmed (paper describes this only as a
  Stufe-2/3 design decision for the future MH frame, not a claim about current MHeard traffic,
  which correctly still uses plain bleJsonFrame()).
- App MH-JSON interface has exactly 13 fields (TYP, CALL, DATE, TIME, PLT, HW, MOD, RSSI, SNR,
  DIST, PL, MESH, NCNT) -- confirmed, ~/WebDev/Meshcom-MobileApp/src/utils/AppInterfaces.ts:378-392.
- App parser drops CALL=="XX0XXX-00" and own node call, casts JSON to the Mheard interface --
  confirmed, MessageHandler.ts case "MH" block ~1188-1259.
- NBR_MAX_ROWS 13/21 (classic ESP32 vs E22_XML/S3/nRF52... actually E22_XML also 21, S3 21, RAK
  21, developer variant 11) -- confirmed src/configuration_global.h:309/320/330/352, matches
  paper's "13 oder 21 Zeilen".
- Rufzeichen character rule (3-9 chars from [A-Z0-9-], else whole frame dropped as "DROP TOK") --
  confirmed src/nbr_matrix.h:211-214, matches the 37-character/6-bit premise in 4.1.

## Summary

4 candidate findings written up (2 unique issue types: 1 arithmetic error affecting a
downstream comparison table, 3 instances of citation line-number drift). Everything else checked
-- roughly 30 distinct factual/citation claims across MHeard, path table, NCNT, via/checkMesh
semantics (including the upstream-vs-fork substring-match distinction), shortVERSION() version
gating, T-Deck persistence, BLE frame limits, and the mobile app's JSON contract -- came back
correct, several matching cited line numbers exactly.
