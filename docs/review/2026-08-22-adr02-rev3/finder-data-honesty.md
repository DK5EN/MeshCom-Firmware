# Data-Honesty Review: NC-Importance Evidence Pack (Rev. 3 input)

Scope: `analyze_heys.py`, `analyze_deep.py`, `analyze_links.py`, `harvest.py`, and every headline
number in `evidence-pack.md`. All numbers below were independently recomputed from `heys.jsonl`
with fresh Python, not by re-running the audited scripts.

## Findings (bugs / overclaims)

### F1 — rssi-segment→hop positional alignment breaks in ~1.2% of multi-hop frames (Medium)

`analyze_deep.py:24-33` assumes `segs[i]` always corresponds to hop `path[i]→path[i+1]`
(`relay_mh.append((path[i+1], mh))`). Recomputed on the deduped 34,810 frames with ≥1 hop:

- 405 frames (1.16%) have `len(segs) != len(path)-1` (23 with a missing segment, 382 with a surplus
  segment), containing **1,013 of 39,149 3-value segments (2.59%)** whose positional attribution is
  unverified.
- Concrete example of the "missing segment" case (predicted by the task brief — old-firmware
  relay drops a whole segment, not just a value): `OE1FFS-10,OE3ALB-2,OE3XIA-12,OE3GHB-1` (3 hops)
  has `rssi="110,-2;3,100,-9;"` — only 2 segments for 3 hops. The code reads `segs[1]="3,100,-9"`
  as hop `path[1]→path[2]` (attributing mheard=3 to `OE3XIA-12`), but with one hop's segment
  entirely absent, segment 1 may actually belong to the _last_ hop, not the middle one.
- Concrete example of the "surplus segment" case (majority: 382/405): `DL2JA-2,DK5EN-91` (1 hop,
  direct) repeatedly shows **two** 3-value segments, e.g. `"2,114,-8;3,12,6;"`. The code takes only
  `segs[0]` (mheard=2) for `DK5EN-91` and silently drops `segs[1]` via the `i+1<len(path)` bound —
  it never considers that `segs[1]`, not `segs[0]`, might be the authoritative one.
- Effect: the "Relay mheard (3-value segments): 38,767 segments from 397 relays; median 5, p90 14,
  max 35; 96 relays report ≥9" figures in the evidence pack rest on a positional assumption that is
  demonstrably violated for ~2.6% of the input segments. This is not large enough to overturn the
  headline distribution, but could shift a handful of relays into/out of the "saturation candidate"
  bucket (96 relays at MAX_MHEARD=10-1). **Recommend Rev. 3 caveat this table as "feed-sichtbar,
  ~97% positionally verified" rather than presenting it as exact.**

### F2 — "63.8% self-uploads = no RF measurement" is not exactly true (Low)

Evidence pack: _"a 1-element path with empty rssi is a gateway SELF-UPLOAD (no RF measurement)"_.
Recompute: of 61,264 length-1-path frames, **641 (1.05%) carry a non-empty 3-value rssi segment**
(e.g. `{"path":"DK5EN-91","rssi":"3,11,7;","nct":1,...}`). This does not corrupt the "hears" graph
(zip over a 1-element list yields no edges either way, so `hears{}`/`imp{}` are unaffected), but the
blanket "(no RF measurement)" claim needs a caveat — a small minority of self-upload frames do carry
a 3-value segment whose meaning (self-noise-floor sample? own mheard snapshot?) isn't established by
the evidence pack and shouldn't be asserted as absent.

### F3 — the 508-node "importance population" is receiver-biased; the 98.6%/60.4% headline figures do not generalize to the network (High — biggest interpretive risk)

`hears{}` (both scripts) only gets an entry for a node `b` when `b` appears as a **receiver**
(second+ element of some path). Recompute: **889 of 1,323 origins (67.2%) never appear as a
receiver in the 24h window** — pure leaves, structurally invisible to the importance/known_ratio/
slot-mapping simulation. The 508-node population is therefore relays + gateways that were actually
used as a hop or last-hop in this window, not a sample of the network.

- All cited percentiles (median 0.95, p90 3.11, max 9.24), the known_ratio gate-pass rate (98.6%),
  and the slot distributions (60.4% in slots 6..8, etc.) describe **only** this 508-node subset.
- Evidence pack line 61-62: _"The ADR 3.1 gate would be OPEN nearly everywhere today"_ — this
  generalizes beyond what was measured. For the 889 invisible leaf origins, `known_ratio` is
  **undefined**, not "unknown-but-probably-fine" — there is zero feed evidence either way.
- **Required correction for Rev. 3**: reword to "unter den 508 im Fenster als Relay/Gateway
  beobachteten Knoten" (or similar), not "nahezu überall im Netz". This is the single claim most
  likely to be over-read by anyone skimming the ADR.

### F4 — importance topology is a 24h union, weighted by a single end-of-window nct snapshot (Medium, needs wording downgrade)

`hears{}` accumulates edges from the **entire 24h window**, but `nct_latest[x]` (used identically
for every edge involving neighbor `x`, regardless of when that edge was recorded) is a single
snapshot — the latest frame per origin, which in practice is the value as of the _end_ of the
harvest window. This is a defensible choice for "what would the algorithm compute right now", but
it silently treats a 24h-aggregated graph as if it reflects one consistent point in time. Concretely:
edges recorded during the 04:00 storm hour (driven by `IU4KCH-26`'s anomalous 5,912-frame burst) are
weighted using end-of-window nct values that may bear no relation to conditions during the storm.
Rev. 3 should call this out explicitly as "eine eingefrorene Momentaufnahme auf Basis eines 24h-
Topologie-Unions", not "gemessene 24h-Importance".
Checked and **ruled out** as a live concern: tie-breaking in `max(frames, key=lambda f:
f["timestamp"])` — verified across all 1,323 origins, zero cases where the max timestamp is shared
by frames with differing `nct` values, so the arbitrary-tie-break risk does not actually bite in
this dataset.

### F5 — "feed DUP/NEW = 0.70 (lower bound on RF duplication)" is not a lower bound; direction of bias is unknown (High)

Of the 39,400 "extra" msg_id copies (`len(heys)-uniq_msgids`):

- **12,865 (32.6%) come from frames sharing the identical path string** — i.e., the _same_
  physical reception reported more than once (different gateway, or with/without a trailing
  rssi segment), not an independent RF retransmission. **3,242** of these differ _only_ in the
  `gw` flag (e.g. `DK6GC-3` reported twice at the same second, once `gw:0` once `gw:1`).
- This directly **contradicts** the evidence pack's own blind-spot claim ("at most one report per
  transmitted frame network-wide — gateway internet race"): 689 distinct `(msg_id, path,
timestamp)` keys have ≥2 textually-different raw records, proving multiple gateway reports of one
  transmission _do_ reach the log.
- Net effect: the 0.70 ratio is inflated by same-path server-side re-reporting (which is not RF
  duplication at all) while simultaneously deflated by the internet-race blind spot and by
  msg_id-wrap contamination (867 msg_ids span >1h; excluding those drops the ratio only marginally,
  0.70→0.678, so wrap is a minor 4% effect — **not** the dominant issue). Because the two biases
  push in opposite directions and neither is quantifiable precisely, **0.70 cannot honestly be
  labeled "lower bound on RF duplication"**, and comparing it directly against BergLog's RF-side
  1.57 as if commensurable is not supportable. Recommend Rev. 3 present it as "feed-sichtbarer
  msg_id-Wiederholungsquotient, Richtung des Bias gegenüber echter RF-Duplikation unbekannt".

### F6 — reported "min/h=1340" is a partial trailing-hour artifact, not a real minimum (Low/cosmetic)

`heys_analysis.txt`: _"hours covered: 25, min/h=1340, max/h=9377, avg/h=3842"_. The harvest window
in the evidence pack is stated as ending "14:12 UTC", but the actual max timestamp in `heys.jsonl`
is **14:20:39 UTC** (checked directly), and the last hour bucket (`08-22 14:00`) only covers ~20 of
60 minutes, yielding 1,340 frames — that's why it reads as the "minimum". The real quietest _full_
hour is 17:00 (1,903, already correctly attributed to the INTERLINK outage in the evidence pack).
`avg/h=3842` is also computed over 25 buckets including two partial hours (start 14:00 and end
14:00), which slightly deflates the true steady-state average. Fix: exclude partial edge buckets
from min/max/avg before citing them, and correct the stated window end time.

## Independent recomputes (task item 4) — all match, no arithmetic bugs found

Recomputed directly from `heys.jsonl` with fresh code (not the audited scripts):

| Metric                               | Evidence pack          | Recomputed                                                                                                                                |
| ------------------------------------ | ---------------------- | ----------------------------------------------------------------------------------------------------------------------------------------- |
| HEY frames (24h, deduped)            | 96,074                 | 96,074 ✓                                                                                                                                  |
| Path length shares 1..5              | 63.8/19.8/9.8/4.5/2.1% | 63.8/19.8/9.8/4.5/2.1% ✓                                                                                                                  |
| nct (n=1121): p25/median/p75/p90/max | 2/3/7/11/36            | 2/3/7/11/36 ✓                                                                                                                             |
| nct==0 share                         | 15.3%                  | 15.3% ✓                                                                                                                                   |
| Top relay                            | DB0HOB-12 (1164)       | DB0HOB-12 (1164) ✓                                                                                                                        |
| Spike-hour top origin                | IU4KCH-26, 5912        | IU4KCH-26, 5912 ✓                                                                                                                         |
| known_ratio gate pass                | 501/508 (98.6%)        | 501/508 (98.6%) ✓                                                                                                                         |
| Fleet fw ≥4.35n                      | ≈81%                   | 81.0% (78.2+2.2+0.6) ✓                                                                                                                    |
| Line dedup (134,576→96,074)          | matches                | matches; timestamps essentially monotonic (1 backward jump of 169s in 134,576 lines); 0 whitespace-in-path-segment cases anywhere in file |

## Feed-vs-RF validity — required "gemessen" → caveat downgrades for Rev. 3

- **DUP/NEW = 0.70**: not a validated RF-duplication proxy (F5) — label "feed-sichtbar", drop the
  direct comparison to BergLog's RF-side 1.57 or caveat it heavily.
- **Relay mheard / saturation candidates**: feed-visible self-reported neighbor counts, ~97%
  positionally reliable (F1) — label "feed-sichtbar (Selbstangabe der Relays)".
- **known_ratio 98.6% / "gate OPEN nearly everywhere"**: applies only to the 508-node
  observed-receiver population, not the network (F3) — must not be stated as a network-wide claim.
- **Frame counts as activity/load proxy** (e.g. "9377 frames in spike hour"): these are HEY
  _reports_, not airtime — already correctly caveated once in the evidence pack ("feed traffic is
  NOT channel occupancy/airtime"), but that caveat needs to travel with every frame-count number
  cited in Rev. 3's backoff/collision argument, not just live in the blind-spots section once.
- **True NC_self of the 202 nct==0 origins and the 889 receiver-invisible leaves**: feed cannot
  distinguish "genuinely low-connectivity node" from "node whose R<NC> HEY simply wasn't captured/
  relayed in this 24h window" — state as a coverage gap, not as measured NC_self=0.

## Verified sound (failed to refute)

- `slot_start` formula in both scripts, `(1.0 - r) * 7`, matches ADR §4.2 pseudocode and the actual
  firmware constants (`RELAY_TOTAL_SLOTS=10`, `RELAY_JITTER_WIDTH=3` at
  `docs/adr-nc-importance-backoff.md:686-784`) exactly, including `int()` truncation semantics.
- Line-based dedup (`if l not in seen_lines`) is adequate: reproduces 96,074/134,576 exactly, no
  evidence of missed near-duplicates from re-serialization drift.
- `spike hour = 1787371200` correctly decodes to 2026-08-22 04:00:00 UTC.
- IU5CZN-10 outlier (high relay load, low importance, rank 304/508) is correctly computed, not a
  bug — it's the expected signature of a high-throughput single-upstream "Bridge" node, which the
  ADR's own §4.3 table already names as a distinct category. This is a design question for the ADR
  authors (does the formula under-value bridge throughput?), not a data-validity defect.
- Latest-nct tie-breaking (`max(..., key=timestamp)`): checked all 1,323 origins for same-timestamp
  frames with differing nct — zero occurrences, so the theoretical non-determinism does not affect
  this dataset.
- `analyze_heys.py` and `analyze_deep.py` compute `imp{}` independently (slightly different code
  paths) and agree exactly (median 0.95, p90 3.11 in both) — a real cross-check that passed.
- IMP_CAP alternative tables (4.0/3.0/2.0) and the top-20-relay/slot table in `deep_analysis.txt`
  reproduce exactly under independent recomputation.
