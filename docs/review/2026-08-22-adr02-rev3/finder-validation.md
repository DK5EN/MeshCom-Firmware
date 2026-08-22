# Finder: Falsifiability Audit — ADR NC-Importance Backoff (Rev. 2)

Angle: can every "Feld-Validierung"/"Tuning" claim actually be measured, by whom, at what
cost, and does the target value mean the same thing before and after? Read-only audit against
`docs/adr-nc-importance-backoff.md`, the evidence pack, and the current firmware tree
(v4.35p_prio).

Severity scale: **Blocking** (metric as written cannot detect success/failure at all) /
**Major** (measurable only with an undocumented cost or ambiguity that will produce a false
verdict) / **Minor** (measurable, but the target number itself is unjustified or the protocol
is underspecified).

---

## 1. DUP/NEW < 0.5 (Offene Punkte, "Metriken fuer Vergleich")

**Claim:** target DUP/NEW-Ratio < 0.5, presented alongside the BergLog baseline of 1.57.

**Measurability:** Node-side only. `RX_DEDUP_DUP`/`RX_DEDUP_NEW` (`src/lora_functions.cpp:1440,1453`)
are per-event `printfdeb` lines gated by `bLORADEBUG` (default `false`,
`src/loop_functions.cpp:95`) — there is **no aggregate counter**, no running ratio, no periodic
summary line anywhere in the firmware (checked `[MC-STAT]`/`[MC-PRIO]`/`[MC-HWM]` blocks in
`src/esp32/esp32_main.cpp:2047-2100`; none of them touch dedup counts). The BergLog 1.57 number
was necessarily produced by grepping 856,000 raw serial lines offline — i.e. the measurement
method is "enable verbose debug logging for hours, capture serial, grep afterwards," and the
ADR never states this.

**Gap — units mismatch (Blocking as stated):** BergLog's 1.57 is **RF-side**: DUP and NEW are
both counted from packets a single node actually _received over the air_, including every
duplicate copy relayed by every neighbor. The evidence-pack feed number, 0.70, is **internet
feed-side**: computed from `msg_id` recurrence in `heys.jsonl` — a stream that already lost most
duplication before it ever reached the analysis, because (a) 63.8% of feed frames are gateway
self-uploads with no RF hop at all, (b) the interlink/gateway race means at most one report per
transmitted RF frame reaches the server, and (c) `S1`-style suppression stops repeat gateway
uploads of the same message. These are not the same quantity, not even the same order of
measurement (RF receptions vs. server-side upload survivors), and the ADR juxtaposes 1.57 vs 0.70
without ever saying so. A Rev. 3 reader could easily conclude "the feed's 0.70 is already below
the 0.5 target, so field validation is basically done" — which would be a false pass;the feed
number is not a valid comparator for the target at all.

**Concrete protocol for Rev. 3:**

1. State explicitly that DUP/NEW is **RF-side, node-serial-log only** — retire the feed number
   from this comparison or clearly relabel it "upload-survivor ratio, not comparable."
2. Define counting precisely: is it `RX_DEDUP_DUP / RX_DEDUP_NEW` over a fixed clock window
   (hours, per 4.7b), or over a fixed count of NEW messages? BergLog's ratio was over ~16h; state
   the same window for before/after.
3. Same node set, same time-of-day, same season/propagation regime as the before-measurement
   (BergLog is March 2026 on `MAX_MHEARD=20` firmware — topology and buffer sizes have since
   changed; a same-nodes re-run is not automatically comparable even if labelled "same nodes").
4. Since there is no built-in counter, either (a) add one (two `uint32_t` accumulators reset on
   the existing 5‑min/30‑min stat timers, cost is trivial) so this becomes a live, low-cost
   metric instead of an hours-long `bLORADEBUG` capture, or (b) accept the debug-log cost
   explicitly (see item 3 below on `bLORADEBUG`'s own side effects) and say how long a capture
   session needs to be to get a stable ratio.

---

## 2. CAD-Busy < 30%

**Claim:** target CAD-Busy-Rate < 30%, vs. BergLog's 66%.

**Measurability:** Node-side only, and **the firmware exposes two different metrics that both
could be called "CAD-Busy" but measure different things — the ADR never disambiguates them**:

- **Per-attempt CAD outcome** (`CAD_BUSY`/`CAD_FREE`/`CAD_BUSY_1`/`CAD_FALSE_POSITIVE`,
  `src/esp32/esp32_main.cpp:2451-2538`, `src/nrf52/nrf52_main.cpp:1407-1462`): one `printfdeb`
  line per CAD scan. This is what BergLog's "66% of TX attempts find the channel busy" almost
  certainly is — a count of `CAD_BUSY` events over `CAD_BUSY + CAD_FREE` events. No aggregate
  counter exists for it either; same offline-grep-of-serial-log problem as item 1.
- **Airtime occupancy** (`CHANNEL_UTIL`, `src/esp32/esp32_main.cpp:2028-2038`,
  `src/nrf52/nrf52_main.cpp:1307-1320`): `(rx_ms+tx_ms)*100/window`, logged every 10s, also gated
  by `bLORADEBUG`. This is a time-fraction-of-airtime number, not an attempt-outcome ratio — a
  channel can be 30% busy by airtime and still show 90% of CAD _attempts_ landing on a busy scan,
  if TX attempts cluster right after RX bursts (which is exactly the CSMA contention scenario this
  ADR is about). These numbers are not interchangeable and will not converge to the same percentage.

**Gap (Major):** the ADR states "CAD-Busy < 30%" without saying which of these two firmware
signals it means. Whoever runs the field test could legitimately pick either one, get a passing
number from one and a failing number from the other, and the ADR gives no way to adjudicate.

**Baseline gap (Major):** no Stufe-0 baseline exists for _today's_ network under either
definition. BergLog's 66% is from March 2026, 5 nodes, pre-`MAX_DEDUP_RING` increase, pre this
ADR's own code-baseline corrections (Rev. 2's own "Codestand-Abgleich" section shows the network
and firmware have moved). Comparing a post-change measurement to a 5-month-old baseline on
different firmware conflates "did importance-backoff help" with "did the network change since
March." As the ADR's own dependency chain notes (Stufe 3, dedup ring already partially resized),
some of the causal inputs to the 66% number have already changed independent of this ADR.

**Concrete protocol for Rev. 3:**

1. Pick one definition and name it explicitly: recommend the per-attempt `CAD_BUSY`/`CAD_FREE`
   ratio, since that is the quantity the 66%/30% narrative in the ADR (Kap. "Warum 210ms...")
   is actually reasoning about (CAD outcome, not airtime).
2. **Add a Stufe-0 requirement**: capture a `bLORADEBUG` session on today's firmware, today's
   network, before any importance-backoff code lands, and publish that number as the actual
   baseline the 30% target is measured against — not the March BergLog figure.
3. State the averaging window (hours, per 4.7b) and how many CAD attempts constitute a stable
   sample (BergLog's 66% came from an unstated attempt count over ~16h on 5 nodes).
4. Flag the instrumentation cost explicitly: `bLORADEBUG` drives per-line `Serial.printf`/malloc
   traffic; this project's own findings elsewhere in this codebase (see
   `printf malloc starves NimBLE` — heap churn from per-log-line printf on BLE-enabled boards can
   itself starve BLE connection establishment) mean that turning on verbose debug logging for a
   multi-hour field capture is not a neutral observation — it can perturb the very channel
   contention being measured on nRF52/BLE-class boards. The protocol must say whether the
   capture boards run without an active BLE central connected, or accept the confound.

---

## 3. "Keine Zombies" (Offene Punkte target, Kap. Zombie-Nachrichten)

**Claim:** target "keine Zombies," alongside a "Zombie-Nachricht-Zaehler" metric — named but
never operationally defined anywhere in the ADR.

**Measurability:** Neither side can count zombies as the ADR currently describes them.

- **Feed-side:** `msg_id` reappearing after a gap is the only observable, but `msg_id`'s low 10
  bits are a **per-node counter that wraps at 999**
  (`src/loop_functions.cpp:3119-3132`: `msg_counter = ((_GW_ID & 0x3FFFFF) << 10) | (node_msgid &
0x3FF)`, `node_msgid` reset when `> 999`). A prolific origin reissuing >1000 messages inside the
  observation window will produce two **genuinely different** messages sharing one `msg_id` — the
  evidence pack's own analysis flags exactly this: 867 of 96,074 feed `msg_id`s span >1h and are
  "likely counter wrap, NOT zombies" (`heys_analysis.txt`/`deep_analysis.txt`), vs. 1,368 that
  span 10–60 min and are the actual zombie-candidate band. The ADR's Offene-Punkte line just says
  "Zombie-Nachricht-Zaehler" with no threshold, no wrap-exclusion rule — as written this metric
  will silently count wraps as zombies (or vice versa) depending on who implements it.
- **Node-side:** the dedup ring only stores raw `msg_id` bytes (`ringBufferLoraRX`,
  `is_new_packet()` at `src/lora_functions.cpp:1427`); there is no record of hop-count-consumed
  or first-seen-time attached to a ring slot, so "reappeared after the hop budget was already
  spent" is not directly observable from the ring itself — it has to be reconstructed from
  `RX_DEDUP_NEW`/`RX_DEDUP_DUP` log timestamps plus the packet's hop field, again only via
  `bLORADEBUG` capture.

**Operational definition to put in Rev. 3** (feed-side, since that's the only side with volume):
a `msg_id` is a **zombie candidate**, not a wrap, only if **all** of:

1. Reappearance gap is inside a bounded window that excludes wrap risk — e.g. 2–60 min (the
   evidence pack's own 10–60 min band is a reasonable start, but the lower bound should be set
   above the longest observed legitimate multi-hop relay time, ~13 min per BergLog, so really the
   danger band for false zombies is 2–15 min, and 15–60 min is safer zombie territory);
2. The reappearing copy's **hop-count-remaining is lower** than at first sighting (i.e. the same
   physical message bouncing further, not a fresh injection);
3. The origin's message rate in that window is low enough that wrap is implausible — origin sent
   fewer than ~1000 messages in the gap (cross-check against `nct`/HEY frequency for that origin;
   the storm case in the evidence pack, IU4KCH-26 at 5,912 frames/hour, shows wrap is a real risk
   for high-rate origins and must be excluded per-origin, not with one global rate assumption).
   Without at least (1)+(2), "keine Zombies" cannot be falsified — a wrap-heavy origin could make
   the after-measurement look identical to the before-measurement's zombie rate while the underlying
   mechanism (dedup ring rotation) is unchanged.

**Severity: Blocking** — as written, the metric has no operational definition at all; it is a
name, not a measurement.

---

## 4. Slot-ordering effectiveness (the core mechanism — "Hub sendet zuerst")

**Claim (Kap. 4.5, 4.7b, Stufe 2 rationale):** the entire deployment order (Stufe 1 must precede
Stufe 2) rests on the empirical claim that importance-ranked slots make higher-importance relays
start CAD/TX before lower-importance ones, most of the time, for co-hearing nodes.

**Direct observable, and who can see it:** node-side only, and only pairwise/locally. The
observable that actually proves the mechanism is: for messages heard by two or more relays at
the same time, does the higher-importance relay's `TX_GATE_ENTER`/`CAD_FREE`/`RADIO_TX` timestamp
precede the lower-importance relay's? That requires **synchronized serial captures from multiple
co-located nodes**, because the ADR's own 4.7b admits the backoff timer has no global time
reference (`iReceiveTimeOutTime` resets on every RX-end, independently per node) — so a
single-node log cannot establish "who went first" relative to a neighbor; you need both nodes'
logs correlated by the shared received-message event.

**Can mcmap's feed measure this? No, and the ADR should say so.** Per the evidence pack's feed
semantics: at most one report reaches the server per transmitted RF frame (gateway race), and a
suppression flag (`S1`) stops repeat uploads of the same message once one report lands. This
means the feed structurally **cannot** show "relay A's copy vs. relay B's copy of the same
message, and which arrived first" for the common case — by the time a message is visible in the
feed, competing relay copies have already been collapsed to (usually) one surviving path. The
only thing the feed can give is an indirect, aggregate, weak proxy over many messages:

```
For each origin O with >=2 known relays R1 (high importance rank) and R2 (low rank)
that both appear as "have heard O directly" in the mheard/link-load graph:
  count, over N days, how often O's *reported feed path* uses R1 as the relay hop
  vs. R2, when both were in a position to relay.
```

This tests "does the higher-importance relay end up as the surviving/reported path more often,"
which is a downstream correlate of the ordering mechanism (if R1 wins CAD first, R2 gets
suppressed and never becomes a reported path) — but it cannot distinguish "R1 won because of
slot-ordering" from "R1 won because it simply has better propagation to the gateway" (the same
confound the ADR itself raises for `IU5CZN-10`, evidence pack: relay-load rank 9 by traffic but
importance rank 304/508 — a workhorse the formula would push to back slots; whether it keeps
winning under the new scheme or gets crowded out is exactly the ambiguous case this proxy cannot
resolve on its own).

**Severity: Major.** The ADR's Feld-Validierung list never proposes any direct test of the core
mechanism at all — only downstream aggregates (DUP/NEW, CAD-Busy). Rev. 3 should add an explicit
"does the ordering happen" test using synchronized multi-node `bLORADEBUG` capture on a small
co-hearing group (the same 5-node BergLog set would do), correlating `TX_GATE_ENTER`/`RADIO_TX`
timestamps across nodes for shared message IDs — this is the only way to falsify the mechanism
itself rather than its second-order effects.

---

## 5. Offene Punkte checkboxes — resolved vs. still unfalsifiable

Checked against the evidence pack and current code (`src/configuration_global.h`,
`src/lora_functions.cpp`):

| Checkbox                                                                                      | ADR status                                                                | Verdict                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      |
| --------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Zeitfenster vereinheitlicht (5.2, 1h)                                                         | `[x]`                                                                     | **Correctly closed.** `getNetImportance()` draft in 5.2 uses `mheardEpoch[i] + 60*60`, matching `getMheardCount()`'s 1h window at `mheard_functions.cpp:556`. Verifiable by reading the code, not a field claim — fine as a checkbox.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        |
| Critical-/Background-Slots vereinheitlicht (4.6)                                              | `[x]` (both, "Critical-Slots erhoehen" and "Background-Slots reduzieren") | **Correctly closed** per `configuration_global.h:CSMA_PRIO_SLOTS_1..5` all =10, confirmed in the evidence pack's firmware baseline. Same caveat: code-verifiable, not empirical — fine.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      |
| Qualitaetsgate implementieren (3.1)                                                           | `[ ]` open                                                                | **Correctly still open** — it's not yet in the codebase (this ADR proposes the code in 5.3, not yet merged), and the 50% threshold is explicitly flagged "eine Schaetzung" belonging in Feld-Validierung, but the corresponding Feld-Validierung section (bottom of doc) never actually lists "validate the 50% threshold" as a task — only "IMP_CAP" and `RELAY_JITTER_WIDTH` tuning items mention it. **Gap:** `RELAY_IMP_MIN_KNOWN_PCT` is listed under "Parameter-Tuning" but has no proposed measurement protocol (what would make 50% wrong? What would the field observation look like?). As written it's a knob to be "tuned" with no falsification criterion — recommend Rev. 3 add: measure the false-negative rate (nodes that should differentiate but get gated off) and false-positive rate (nodes differentiating on <50% known data) directly from `getNetImportanceKnownPct()` telemetry, not from network-wide aggregates. |
| IMP_CAP=8.0 festlegen                                                                         | `[ ]` open, "Schaetzung"                                                  | **Legitimately open, but unfalsifiable as worded.** The evidence pack shows the sensitivity is enormous and directly measurable from data already collected: CAP=8 puts 60.4% of nodes in back slots vs. CAP=3's 28.9% (`heys_analysis.txt`), and top-20-by-real-relay-load nodes land in front slots 8/20 (CAP=8) vs. 17/20 (CAP=3) (`deep_analysis.txt`). The ADR's own open item doesn't reference this simulation or propose what field signal would confirm/reject a given CAP value (e.g., "top-N real relays should land in slots 0..2 at rate >X%"). Recommend making it falsifiable: define the target relay-load/slot-rank concordance rate explicitly and use the existing simulation as the Stufe-0 estimate.                                                                                                                                                                                                                    |
| `RELAY_TOTAL_SLOTS` 10 vs 16-20 (4.7a)                                                        | `[ ]` open                                                                | **Correctly flagged as a real trade-off**, but "als Tuning-Parameter im Feldtest zu messen" doesn't say what's measured to decide it — latency cost (+1.4s at 4 hops) is quantified, but the benefit side (does 16-20 slots measurably improve ordering-success vs. 10) has no proposed test. Same gap as item 4 above: needs the synchronized-capture ordering test, not just DUP/NEW-Ratio.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                |
| Position-Relay-Anwendung, Bidirektionalitaetsnachweis, Stale-NC-nach-Standortwechsel, Logging | all `[ ]` open                                                            | Correctly open (design decisions, not empirical claims) — no falsifiability issue, these are scope questions.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                |
| Importance v2 (Kap. 8)                                                                        | `[ ]` open, gated "erst nach Vermessung von Stufe 1"                      | Consistent — but see item 4: Stufe 1's own "Vermessung" as currently specified (DUP/NEW, CAD-Busy) does not actually test the mechanism v2 depends on (ordering), so the gate condition for starting v2 work is itself not well-defined by the current Feld-Validierung list.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                |

---

## 6. Averaging window / statistical framing (4.7b) — consistency check

The ADR is explicit and correct that ordering is "ein statistischer Vorteil ueber viele
Nachrichten, keine Garantie pro Einzelnachricht" and that field metrics "muessen ueber Stunden
mitteln." This part is well-framed. The gap is that **none of the four listed Offene-Punkte
Feld-Validierung metrics (DUP/NEW, CAD-Busy, Relay-TX-Anzahl pro Knoten, Zombie-Zaehler, ACK-
Laufzeit) states its averaging window, sample-size floor, or pass/fail decision rule** — "< 0.5"
and "< 30%" are point targets with no confidence interval or minimum-hours requirement attached,
so a single lucky/unlucky multi-hour window could produce a false pass or fail. Recommend Rev. 3
attach to every target: (a) minimum capture duration, (b) minimum event count for a stable
estimate, (c) explicit statement of which nodes/topology class the number applies to (dense
urban vs. mountain-hub — the ADR's own Berg-Hub-vs-Stadt-Node argument implies these metrics will
differ by topology, so one network-wide number conflates two regimes the ADR itself says are
different).

---

## Summary of severities

| #   | Item                                                                                                     | Severity    |
| --- | -------------------------------------------------------------------------------------------------------- | ----------- |
| 1   | DUP/NEW < 0.5 — RF-side vs. feed-side unit mismatch, no live counter                                     | Blocking    |
| 2   | CAD-Busy < 30% — two different firmware signals, no baseline for today's network                         | Major       |
| 3   | "Keine Zombies" — no operational definition, wrap vs. zombie ambiguity                                   | Blocking    |
| 4   | Slot-ordering mechanism itself — no direct test proposed anywhere in Feld-Validierung                    | Major       |
| 5   | IMP_CAP / RELAY_IMP_MIN_KNOWN_PCT tuning items — no falsification criterion, ignores existing simulation | Minor–Major |
| 6   | All four listed metrics lack averaging window / sample-size / topology-stratification                    | Major       |
