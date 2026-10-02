# Neighbor-count time series concept — Fable Verdict

Concept: `~/Desktop/Nachbarzaehler-Zeitreihe.html` (v1, 2026-09-24). Six finders (attribution,
firmware, storage, altitude, numbers, logic), every load-bearing claim re-checked by the
orchestrator against code, tags or data. Finder detail: `review/f-*.md`.

## Finding 1: The count window is not 1 h for about half the fleet

- **Where:** concept §2 "Zählfenster 1 h", §4.4, §1 BLUF
- **Severity:** high
- **Evidence:** `git show v4.35p_20260321:src/mheard_functions.cpp:512` counts
  `mheardEpoch+60*60*12` (12 h, wall clock) under a "last hour" comment. The 1 h `millis()` window
  arrived 2026-08-31. All these builds report `4.35p`; 690 of ~1,440 nodes in the snapshot run
  `4.35p`, including IW5EIA-12, IZ5FSA-12, IQ5BL-12, DB0KH-11, DB0HOB-12, the stations cited as
  pinned at 30.
- **Failure scenario:** a 4.35p node's 12 h count is stored and ranked as a 1 h count, so the
  top list is biased toward stale 4.35p nodes. The earlier chat claim "30-37 direct neighbors in
  one hour" is not established for them.
- **Fix:** show the firmware string with every value; treat `4.35p` as window-unknown and keep it
  out of peak and ranking statements.

## Finding 2: The cap depends on hardware and firmware version together

- **Where:** §2, §4.4, §8 open point 1
- **Severity:** high
- **Evidence:** `v4.35p_20260321:src/configuration_global.h:79` has `MAX_MHEARD 120` on S3/RAK;
  4.35s/t and DEV use 50/80/30. The E22_XML build reports the same hardware as the classic build
  (cap 50 or 30). Pre-MH-02 firmware evicts in fixed slot order, not oldest first, so a saturated
  node can sit a little below the cap (fits the 28/29 medians).
- **Fix:** derive the cap from (hardware, firmware); flag "near cap" rather than "== cap"; accept
  values up to 120 in the new series (`sanitizeNct` stops at 99, which comes from the position
  `/N` tag).

## Finding 3: Existing bug — old firmware's `nct` belongs to the first receiver, not the origin

- **Where:** production today, `proxy/src/interlink/ingest.ts:1085` → `nodes.nct` / NBCNT; would
  carry into the planned "self" series
- **Severity:** high (existing data defect, independent of the concept)
- **Evidence:** in frames whose first group has two values (pre-4.35n origin), `nct` matches
  path[1]'s own count within ±1 in 12,349 of 12,382 frames (99.7 %), against 18 % for
  new-format frames; `nct` varies across copies of one msg_id in 2,085 of 11,151 cases.
- **Failure scenario:** about 150 old-firmware stations show their neighbor's count as their own
  NBCNT on the public map.
- **Fix:** store `nct` only when the first group is not two-valued. The value can then be credited
  to path[1] instead, which recovers about 18,000 relay readings a day.

## Finding 4: Headline numbers wrong

- **Where:** §1, §2 Mengengerüst, §4.1, §5, §7 step 1 acceptance criterion
- **Severity:** medium
- **Evidence:** attributable relay counts are 59,849 (not ~68,200), unattributable 54,318 (not
  ~45,900); reproduced by two finders and by the orchestrator. Of these, only ~31,400 are
  distinct measurements (gateway re-uploads repeat them), so "reports per relay per day"
  (p90 365, median 29) is inflated about 2x. The "59 stations with no counter otherwise" is about
  35 net (24 already have `nodes.nct > 0`).
- **Fix:** correct the numbers; state that the parser runs behind `frameDedup`.

## Finding 5: The "graph is 3-4x thinner" comparison is apples to oranges

- **Where:** §4.6, first chat answer
- **Severity:** medium
- **Evidence:** "11" was only the latest hourly bucket. The hourly maximum over 7 days is 20 heard
  (rx) and 54 undirected. The self-reported maximum 37 is from a different firmware
  generation mix (Finding 1).
- **Fix:** compare per-hour rx degree against the same-hour count for the same station, 4.35s/t
  only.

## Finding 6: `NBR_MAX_ROWS` is 13 on classic ESP32, not 21

- **Where:** §4.6
- **Severity:** low
- **Evidence:** DEV `src/configuration_global.h:352`.
- **Fix:** state 21 (S3/RAK) and 13 (classic).

## Finding 7: The firmware's new HN report is the better source for the 2-hop question

- **Where:** §4.6, §1 Stufe 3
- **Severity:** medium (design alternative)
- **Evidence:** DEV commit `ec636e3f` (2026-09-23) sends an `HN` frame listing up to 8 heard
  callsigns with SNR (`R<heard>;N<k>[+];CALL,snr;...`), by default only from nodes with mesh
  off and gateway off, i.e. exactly the leaves. It is LoRa-only (`max_hop 0`) and receivers
  intercept it before upload. Released firmware drops the frame entirely
  (`docs/nbr-wichtigkeit-konzept.md:361`).
- **Failure scenario:** Stufe 3 builds a sum-of-counts upper bound, while a firmware change the
  owner controls (a gateway uploads received HN reports) would deliver named leaf-neighbor sets.
- **Fix:** add the option "gateway uploads HN" as an alternative Stufe 3 in §1.

## Finding 8: Scope and logic issues

- **Severity:** medium
- **Details:**
  - §1 promises the basis for the supernode/relay decision, but §4.6 hands the decision itself to
    the firmware. The limit belongs in the BLUF.
  - "Region" from the owner's question is never operationalized.
  - §4.2 argues 30 days of retention are needed for peaks, yet the Stufe-3 decision is scheduled
    after 14 days.
  - The reason for waiting (the cap) is already known after one day.
  - Missing risks: self-reported, unauthenticated counts; peak values driven by mobile or contest
    traffic.
  - §5 calls "small relays sparse" a positive.
- **Fix:** rewrite §1 and §5 accordingly.

## Finding 9: Operational and interface details

- **Severity:** low to medium
- **Details:**
  - WSS is off by default since 2026-08-14 (`wss/config.ts:10`). "Both ingest paths" means
    INTERLINK only in production.
  - 30-day retention has no precedent. Comparable hourly tables are owner-set to 7 days
    (`unifiedDatabase.ts:1366`).
  - Extra per-HEY writes land on the single-core host already under MC-218 load. Measure stall
    lines before and after.
  - Route shape: every v2 route uses `?call=`, so use `/api/v2/neighbor-count/series?call=`.
  - Reuse `frontend/src/components/links/LinkSignalChart.vue` (hourly min/max/last).
  - Parser must trim space-padded groups (225 per day, e.g. `7, 0,6`).
  - The §6 drift detector (more groups than path elements) cannot fire. Use relay-vs-own-count
    agreement (~98 % today) as the canary.
  - §2 labels `lora_functions.cpp:1736` as a relay append; it is the gateway's pre-upload
    append.

## Refuted claims (do not re-investigate)

- "`relay_counts_1h` (MC-047) already stores this" — refuted: it counts relayed transmissions per
  relay per hour (load), not the relay's heard-station count (`unifiedDatabase.ts:1365-1375`). It
  is a structural precedent (additive table, no `SCHEMA_VERSION` bump), not a duplicate.
- "The self count is derivable from `link_signal_1h`" — refuted: that is the gateway-bound graph
  degree, measured at 20 heard per hour against self-reports up to 37. It is a different
  quantity.
- "The firmware builds no HN frame and none is planned" (altitude finder, citing
  `nbr-wichtigkeit-konzept.md`) — refuted: `sendNbrReport()` exists
  (`loop_functions.cpp:5142`, commit `ec636e3f`, 2026-09-23). The concept doc it cites predates
  the commit.
- "Group-to-callsign attribution may be shifted by one in the (n,n) shape" — refuted: relay value
  vs the same call's own count agrees 97.9-98.6 % under the chosen rule, 28-35 % under the
  alternative.
- "Gateways count internet-received stations" — refuted: the only `updateMheard` caller is
  `lora_functions.cpp:1197` on the LoRa receive path.
- "The dedup key conflates multi-gateway receptions" — refuted: 18,814 of 24,381 same-id/ts/path
  groups differ in rssi and stay distinct.
