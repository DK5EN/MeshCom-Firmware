# Finder: Worst-Case Network Dynamics — Storm, Starvation, Congestion Collapse

Review target: `docs/adr-nc-importance-backoff.md` (Rev. 2). Angle: does the ADR fail safe
under storms, starvation, and congestion collapse? Evidence: `evidence-pack.md`,
`heys_analysis.txt`, `deep_analysis.txt` (24h production feed, 96,074 HEY frames), firmware
v4.35p_prio spot-read (`src/lora_functions.cpp`, `src/esp32/esp32_main.cpp`,
`src/txring_functions.cpp`, `src/configuration_global.h`).

Shared collision model (used throughout, assumptions explicit):

- Co-hearing group of n relay candidates, all anchored on the end of the same message M
  (best case for ordering; per ADR 4.7b anchors often diverge — that degrades ordering and
  collision synchronization proportionally, so the synchronized case is the worst case for
  slot collisions and the best case for the ADR's ordering claim).
- Two nodes collide iff the earliest occupied 35 ms slot is chosen by ≥2 nodes (a CAD scan
  cannot detect a transmission that has not started; once one TX starts, packets of
  300–1000 ms ≫ 35 ms slots mean every later CAD sees busy). Adjacent-slot marginal races
  ignored — consistently for both schemes.
- Exact P(shared minimum) for n uniform draws from S positions:

  | n   | S=11 (today, slots 0..10) | S=3 (importance window) |
  | --- | ------------------------- | ----------------------- |
  | 2   | 9.1%                      | 33.3%                   |
  | 3   | 13.2%                     | 44.4%                   |
  | 5   | 21.4%                     | 65.0%                   |
  | 8   | 32.5%                     | —                       |
  | 10  | 39.3%                     | —                       |

---

## F1. Front-slot crowding: IMP_CAP has no safe lower bound in the ADR

**Scenario.** The open point "IMP_CAP festlegen" (ADR, Parameter-Tuning) is resolved
downward because concordance data tempts it: at CAP=8 only 8/20 top real relays land in
slots 0..2, at CAP=3 it is 17/20 (`deep_analysis.txt`). Someone lowers CAP to 3–4 to "make
the ordering match reality".

**Trigger conditions.** IMP_CAP ≤ 4 shipped fleet-wide; no fleet-percentile check.

**Quantitative sketch.**

- Fleet share with slot_start ≤ 2: CAP=8 → 0.8%, CAP=4 → 7.5%, CAP=3 → 14.2% (measured on
  508 observed nodes).
- Co-hearing group sizes from the data: relay mheard median 5, p90 14; origin nct median 3,
  p75 7, p90 11. Take g ≈ 10 for a loaded region.
- Expected co-front contenders k ≈ f·g·c, where f = front-window fleet share and c ≥ 1 is
  the spatial clustering factor of hubs. Hubs cluster hard: the CAP=3 front window contains
  17 of the top-20 real relays, and those co-hear each other (they appear in each other's
  paths — e.g. the Tuscany cluster IQ5ARI-13/-11, IZ5TRQ-11, IW4CVV-24, DB0FTS-1 all in
  slots 0..2 at CAP=3). c ≈ 2–3 in hub regions is realistic.
- CAP=8: k ≈ 0.008·10·2 ≈ 0.2 → almost always a single front-runner → first-relay collision
  ≈ 0, strictly better than today's 39% (n=10, S=11). CAP=8 fails safe.
- CAP=3: k ≈ 0.142·10·2 ≈ 3 → collision among co-front hubs 44% (k=3) to 65% (k=5) in a
  3-position window — worse than today's 21–39% for the whole group, and it is precisely
  the traffic-carrying hub groups that pay. Two colliding hubs lose both transmissions;
  under Stufe 1 the leaves still relay later (coverage survives, airtime is wasted twice);
  under Stufe 2 the leaves are suppressed by the retry copies → regional message loss.
- **Tipping point:** keep E[k] ≲ 1.5 ⇒ f ≲ 1.5/(g·c) ≈ 7.5% for g=10, c=2 ⇒ **IMP_CAP ≥ 4
  is the hard floor on today's importance distribution**; CAP=3 is over the line.

**Severity:** High (latent — Rev. 2 is safe at CAP=8, but the ADR gives no lower bound and
the concordance data actively invites lowering it).

**Rev. 3 must add:** (a) an explicit constraint "IMP_CAP darf nur so weit gesenkt werden,
dass der Fleet-Anteil mit slot_start ≤ 2 unter ~5–7% bleibt" — calibrated from the measured
importance percentiles, re-checked per release; (b) if more front-band resolution is wanted,
widen RELAY_TOTAL_SLOTS for the relay band (ADR 4.7a) instead of lowering CAP; (c) field
metric: share of relay transmissions starting in slots 0..2 plus relay-band collision proxy
(same-second duplicate first-copies in the feed).

---

## F2. Single-origin storm: no Stufe addresses it, and rapid-fire amplifies it

**Scenario.** IU4KCH-26, 2026-08-22 04:00 UTC: 5912 HEY frames in one hour = 1.64/s
(9 the hour before) — a real, measured defect node, not a hypothesis.

**What Stufe 1 does to it: nothing, twice over.**

1. Importance slots apply only to `MSG_PRIO_NORMAL` (ADR 5.3 patches only that case). But
   relayed HEYs are enqueued as `MSG_PRIO_BACKGROUND` and relayed positions as
   `MSG_PRIO_LOW` (`getMessagePriority()`, `src/txring_functions.cpp:46-58` — type HEY →
   BACKGROUND, type POSITION → LOW; only TEXT with RING_STATUS_DONE → NORMAL). The entire
   measured flood class (HEY = 100% of the harvested feed) is out of scope of Stufe 1.
   Any expectation that Stufe 1 improves the measured DUP/NEW of beacon traffic is
   unfounded — the ADR never states this limitation.
2. By design "die Gesamtzahl der Relays sinkt noch nicht" (Stufe 1) — relay amplification
   of a storm (each frame relayed by every neighbor, path ≥ 2 in 36% of feed frames) is
   untouched until Stufe 2, which is unimplemented (no cancel path exists in the tree).

**Regional starvation math (today's firmware, unchanged by the ADR).**

- The backoff anchor `iReceiveTimeOutTime` is reset at EVERY RX end
  (`lora_functions.cpp:1338-1339`, ACK path `:452-453`) and `csma_timeout` is recomputed at
  the _unchanged_ `cad_attempt`. `cad_attempt` increments only after a CAD scan finds busy
  (`esp32_main.cpp:2532`), and a CAD scan only happens after the timeout expires without
  another RX.
- At 1.64 frames/s with ~0.3 s airtime, the mean quiet gap is ~0.3 s. A node at attempt 0
  needs a ≥4500 ms gap to even reach its first CAD. P(gap ≥ 4.5 s) under Poisson(1.64/s)
  = e^(−7.4) ≈ 0.06%. **Every node in earshot of the storm is completely TX-starved —
  relays, own messages, and ACKs (base 3000 ms) alike — for the storm's duration, and
  `cad_attempt` cannot even escalate because escalation requires the timeout to fire.**
  "Es geht gar nichts mehr" is literally the firmware's steady state here.
- Nodes that were already at attempt ≥ 3 before saturation behave oppositely and worse:
  `csma_compute_timeout_prio()` returns `CSMA_RAPID_RX_MS = 100` before looking at priority
  or importance (`lora_functions.cpp:2154-2155`) — **zero jitter, anchored to the same
  RX-end event on every co-hearing node**. When the channel finally frees, all backlogged
  rapid-fire nodes CAD at the same +100 ms instant and transmit into each other: a
  synchronized collision convoy. Rapid-fire is a storm amplifier under sustained busy, and
  it is exactly the regime where importance slots are bypassed entirely.
- Queue behavior is the one existing mitigation: priority-aware overflow drops
  lowest-priority oldest entries (`addTxRingEntry()`), so HEY relays die first when
  MAX_RING (20; 10 on T-Beam) fills. That caps memory, not airtime.

**Severity:** Critical — this is the operator's stated top risk, it is measured in
production, and the ADR's three Stufen collectively do not touch it.

**Rev. 3 must add:**

- **Per-origin relay rate limit** (token bucket keyed on origin call): e.g. min 2 s spacing
  per origin for HEY/POS relays, N per minute for text relays. Dedup cannot do this (every
  storm frame has a fresh msg_id); this is the only mechanism that de-amplifies a broken
  beacon at the first relay ring.
- **Jitter on rapid-fire**: `CSMA_RAPID_RX_MS + random(0, 3..5) * CSMA_SLOT_SIZE`, to break
  the synchronized convoy. Cheap, independent of importance.
- **Age/TTL drop for queued relays** (ringEnqueueTime already exists for stats): a relay
  older than ~30 s is stale traffic amplification, not service.
- Metric: per-origin relay counter + "storm origin" log line (origin frames/min over
  threshold) so the network can locate the IU4KCH-26s.

---

## F3. Stufe 2 starvation: hub-first ordering does not protect a sole-provider relay

**Scenario (concrete topology).**

```
        S ── H (imp≈7, slots 0..2, hub)
        │
        ├── M (imp≈2, slots 5..7)
        │
        └── R (imp≈1.2, slots 5..7) ── L (leaf, hears ONLY R; hidden from H, M)
```

R's importance is ~1.2 (L contributes 1.0 as NC_reported=1 or unknown, plus small terms) —
correctly _below_ H's 7. Ordering works exactly as designed: H first, then M, then R. With
Stufe 2 cancel-on-k-duplicates (k=2): R hears H's relay (dup 1) and M's relay (dup 2) →
R cancels → **L never receives the message although the ordering was "correct"**.
Importance ranks global weight; suppression needs _sole-provider_ status, which is a
different predicate. The ADR's safety argument ("Hubs senden zuerst → Suppression storniert
korrekt die redundanten Blaetter", Kap. 6 / Stufe 2) silently equates "low importance" with
"redundant" — false for every relay whose importance is low _in total_ but contains a 1.0
dependency term. This is not hypothetical: commit 60ea7d8's field test ("Leaf Nodes mit nur
einer Gegenstelle haben oft keine Nachrichten erhalten") is precisely this failure, and
Stufe 1 does **not** remove it — it only changes who sends first, not who depends on whom.

**Aggravators.**

- **Timer anchor (ADR 4.7b, admitted):** R's backoff restarts on every unrelated RX; in a
  66% CAD-busy channel R systematically drifts later than its slot suggests → it reliably
  accumulates ≥k duplicates before its own attempt → suppression hits sole providers _more_
  often than the slot table implies, not less.
- **Asymmetric/hidden links:** L's existence is known only to R (L's HEY reaches only R).
  No global mechanism can see the dependency; only R's own mheardNCount row for L can.
- **Trickle vs. mheard window:** steady-state own-HEY interval is 57–170 min
  (docs/hey-supp.md) but the mheard/NC window is 1 h — L can age out of R's table between
  L's HEYs, at which point even the sole-provider check would go blind (see F4).

**Severity:** Critical for Stufe 2 (already field-proven as a starvation mechanism).

**Rev. 3 must demand as a Stufe-2 precondition (guardrail, in the ADR text, not tuning):**
suppression may cancel a queued relay only if ALL of:

1. ≥ k duplicates heard (k ≥ 2, never 1);
2. own importance below a threshold;
3. **sole-provider veto:** no active mheard neighbor with NC_reported == 1 _or_ unknown
   (i.e. the node's own importance sum contains no 1.0 term). A node that is anyone's only
   path never cancels, regardless of duplicates.
   Plus: every suppression logs callsign + reason (RELAY_SUPPRESS was 0 events in BergLog —
   the counter must exist and be part of the field gate before Stufe 2 ships network-wide).

---## F4. Feedback loops: Stufe 1 self-damps, Stufe 1+2 closes a rich-get-richer loop

**Stufe 1 alone — negative feedback (good).** A hub winning front slots relays more → it is
the `msg_source_last` for more frames → it enters more nodes' mheard tables → its
neighbors' NC_self rises → their _reported_ NC rises → 1/NC contributions fall → the hub's
importance falls → it drifts back. 1/nct self-damps. No runaway in Stufe 1.

**Stufe 1 + Stufe 2 — positive feedback (the runaway).** NC measures _observed
transmissions_, not link existence. Suppression silences low-importance relays → they stop
appearing as last hop → within the 1 h mheard window they vanish from other nodes' tables →
dependent nodes' NC_self falls → their reported NC falls → surviving hubs' importance
_rises_ → hubs suppress the periphery even harder. **Suppression corrupts the very metric
that licenses it.** Compounding: trickle's steady-state own-HEY interval (57–170 min) is
longer than the 1 h mheard window, so a suppressed-quiet node flickers out of the NC data
entirely between beacons. End state: relaying concentrates on a few hubs — single points of
failure, hub battery drain, and a hub airtime ceiling that becomes the new network-wide
bottleneck (a hub can only relay serially; today's parallel redundancy, however wasteful,
is also capacity).

**Severity:** High for Stufe 2 (structural, slow, invisible per-message).

**Rev. 3 must add:**

- Data-side fix: the NC pipeline must count heard-but-silent neighbors — at minimum, a
  suppressed relay decision must still refresh the suppressing node's beacon liveness
  (e.g. a HEY floor immune to trickle suppression, or mheard refresh on any demodulated
  frame from the neighbor, not only on frames it originates/relays).
- Monitoring metrics (both derivable from the existing feed):
  1. **Relay concentration:** top-decile share of mid-path appearances (today: top-20 of
     397 relays carry ~35% — trend up = loop closing);
  2. **NC drift:** median reported nct at constant active-node count (today median 3 —
     trend down while node count is flat = alternatives disappearing from tables);
  3. per-hub relay duty cycle (battery/SPOF early warning).

---

## F5. Cold start after regional power outage: the gate opens into inflated importance

**Scenario.** Hundreds of nodes reboot together after a regional outage; all trickle at
Imin=30 s; measured startup burst 6–17 HEYs/node in the first hour (docs/hey-supp.md).

**Walkthrough, first 60 minutes.**

- t=0: `mheardNCount[]` reloads from flash, but with no _active_ (last-hour) entries
  `getNetImportanceKnownPct()` returns 0 → gate closed → old full-jitter path. Not worse
  than today. So far the ADR is right.
- t≈2–10 min: HEY burst fills mheard tables _with_ NC_reported (every ≥4.35n HEY carries
  `R<NC>;`) → known_ratio jumps toward 100% → **gate opens fast**. But everyone's reported
  NC is transiently tiny (their own 1 h windows are also restarting): the network-wide NC
  snapshot is 1–3 for tens of minutes.
- Consequence: importance ≈ Σ(1/small) ≈ 0.4–1.0 per neighbor → a node with 6 neighbors
  computes imp ≈ 3–4. With CAP=8: slot_start ≈ 4 (slots 4..6) — tolerable. **With CAP=3–4
  (see F1): a large fraction of ordinary nodes computes imp ≥ CAP → slot_start 0, 3-slot
  jitter — front-slot crowding at the exact hour of maximum channel load.** The gate
  cannot catch this: it tests _knownness_, and the data is known — just systematically,
  transiently wrong. The ADR's Kaltstart risk section only analyzes the unknown-NC case
  (importance = NC_self) and calls it acceptable; the known-but-deflated-NC case is worse
  and unexamined.
- Flash persistence cuts both ways: if `mheardEpoch[]` is persisted stale, entries stay
  inactive (safe); if epochs are re-based on boot, months-old NC from possibly another QTH
  drives the gate and the slots (see also F6e).

**Does the ADR make a restart storm worse than today?** At CAP=8: no (mild compression,
minutes-scale). At CAP≤4: yes, during minutes ~10–60 — compressed 3-slot jitter for many
nodes on a saturated channel, i.e. exactly the 3.1 failure mode the gate was built to
exclude, entered through the front door with the gate open.

**Severity:** Medium (self-limiting after ~1 h as windows fill) — but it lands at the worst
possible moment and couples with F1.

**Rev. 3 must add:** a **boot hold-down**: use the old full-jitter path for the first
~45–60 min after reboot (or until the mheard window has ≥1 h of history), independent of
known_ratio — i.e. gate on _data age_, not only on knownness. One line of spec, removes the
whole scenario.

---

## F6. Additional findings

**a) Attempt-2 compression into the ACK band floor.** Today, relays at attempt 2 are
uniform over 3000..3350 ms (11 slots). With importance, the _most active_ relays (hubs,
slots 0..2) land deterministically at 3000..3070 — the floor of the ACK/DM/broadcast band
(CSMA_PRIO_BASE_1/2 = 3000, first slots). ADR 4.4 calls this "bestehendes Verhalten"; the
compression of high-duty relays onto the band floor is new. In a loaded network with many
attempt-2 relays this systematically races fresh ACKs. Safeguard: clamp attempt-2 relay
base to ≥3150 ms, or exclude slots <2 at attempt ≥2; measure ACK latency (stat exists) as
the field gate.

**b) T-Beam class (~28% of fleet) is the weak flank of all three Stufen.** MAX_MHEARD=10 and
MAX_DEDUP_RING=10 (`configuration_global.h:189-190`). (i) The 3.1 gate does NOT "greift
praktisch immer" as the ADR claims: known_ratio can be 100% on a saturated 10-entry table —
the gate tests knownness, not saturation. A T-Beam hub computes importance over ≤10 of its
real neighbors and mis-slots itself. (ii) Dedup ring 10 rotates in ~2.5 min at 4 msg/min —
a zombie factory that Stufe 3 explicitly cannot fix on these boards. (iii) The two
most-loaded relays in the entire 24 h feed are T-Beams: DB0HOB-12 (1164 relay appearances,
TBEAM V1.2) and OE3XIA-12 (803, TBEAM V1.1) — the biggest amplifiers run the smallest
protection. Note a data inconsistency worth resolving: DB0HOB-12 reports nct=26 despite the
TBEAM V1.2 label — either the hardware DB is wrong or the T-Beam MAX_MHEARD=10 define does
not cover that build; the gate's blind spot analysis depends on the answer. Safeguard:
gate must also close when `getMheardCount() == MAX_MHEARD` (saturation = data error, as the
ADR itself says in "Datenqualitaet" — but the gate code in 5.2 never checks it); boards
whose dedup dwell time is below the observed 13-min multi-hop delay should be excluded from
front slots (they re-relay zombies with priority service otherwise).

**c) Front slots accelerate zombies.** 1368 msg_ids reappear after 10–60 min (feed, lower
bound). A zombie re-accepted by a hub now gets first-in-line relay service — importance
ordering is zombie-blind and speeds up their redistribution. Consequence: **Stufe 3 (dedup
depth) is a prerequisite for Stufe 1's field rollout, not a follow-up** — the ADR's own
dependency chain (1→2→3) has the arrow backwards for this interaction.

**d) Loop paths.** 104 measured loops (e.g. `...DB0TVI-1,...,DB0TVI-1`) prove dedup is
already porous at today's ring sizes; loops that pass through hubs circulate faster once
hubs get front slots. Same remedy as (c) plus the per-origin limiter of F2.

**e) Persistence after QTH change.** `mheardNCount[]` and path table survive reboot in
flash. A relocated node computes importance — and holds the gate open — with the old site's
topology for up to 1 h (NC) / 12 h (paths); a former hub camps in front slots at a location
where it is a leaf. The ADR lists this as "Gegenmassnahme: offen"; for Stufe 1 it should be
closed cheaply: discard persisted NC on significant position delta, or apply the F5 boot
hold-down to relocations too.

**f) Honest expectation setting.** With CAP=8 and today's importance distribution, 60% of
nodes sit in slots 6..8 and 0.8% in 0..2 — Stufe 1 is nearly a no-op fleet-wide (which is
_why_ it fails safe). The measurable benefit will be small and confined to hub-dense
regions; the ADR's field-validation targets (DUP/NEW < 0.5, CAD-Busy < 30%) cannot be met
by Stufe 1 alone and should not be attached to it, or the field test will "fail" a
mechanism that worked exactly as specified.

---

## Verdict on fail-safety

| Stufe / knob                    | Fails safe?     | Condition                                                                                                                                                                       |
| ------------------------------- | --------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Stufe 1, CAP=8, gate as specced | Yes             | Nearly inert today; front-runner usually unique; strictly fewer first-relay collisions than status quo                                                                          |
| Stufe 1, CAP≤4                  | **No**          | Front-slot crowding in hub clusters (F1) + cold-start inflation (F5) — needs the CAP floor + boot hold-down                                                                     |
| Stufe 1 vs. the measured storm  | Not applicable  | Importance never touches HEY/POS relays; storm starvation is untouched at every Stufe (F2) — Rev. 3 needs the per-origin limiter and rapid-fire jitter as first-class decisions |
| Stufe 2 as currently argued     | **No**          | Sole-provider starvation survives correct ordering (F3, field-proven); NC-feedback loop (F4) — needs the three-condition guardrail + NC liveness fix before any reactivation    |
| Stufe 3 ordering                | Wrong direction | Dedup depth is a _prerequisite_ of Stufe 1 (F6c), impossible on T-Beam (F6b)                                                                                                    |
