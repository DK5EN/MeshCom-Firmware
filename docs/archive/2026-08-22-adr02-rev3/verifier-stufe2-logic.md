# Verifier: Stufe-2 Logic Claims (F3, F4, F5)

Adversarial check of `finder-storm-starvation.md` F3/F4/F5 against firmware v4.35p_prio
and `docs/adr-nc-importance-backoff.md` (Rev. 2). Code read on 2026-08-22.

Key code facts established (load-bearing for all three verdicts):

1. `updateMheard()` (`src/mheard_functions.cpp:214`) is called from the RX path at
   `src/lora_functions.cpp:687` for EVERY successfully decoded LoRa frame whose
   `msg_source_last` is not the own call — all payload types (TEXT, POS, HEY, ACK-carrying
   frames), and BEFORE any dedup gating (`is_new_packet` sits later in the flow). It sets
   `mheardEpoch[ipos] = getUnixClock()` (`mheard_functions.cpp:303`). So a neighbor's
   mheard entry is refreshed by ANY demodulated transmission from that neighbor — own
   beacons included, relays not required.
2. `getMheardCount()` (`mheard_functions.cpp:556`) counts DISTINCT table entries with
   epoch in the last hour. It is a presence count, not a transmission-volume count.
3. Default position cadence: `POSINFO_INTERVAL = 30*60` s (`configuration_global.h:142`);
   send trigger `esp32_main.cpp:3104` fires every `posinfo_interval` and additionally once
   at boot between millis 100–130 s (`bPosFirst`). `sendPosition()` returns without
   sending only when `lat==0.0 && lon==0.0` (`loop_functions.cpp:3836-3837`).
4. `mheardNCount[]` is populated from position `/N` (`lora_functions.cpp:645-681`) and
   from HEY `R<NC>;` via `updateHeyPath` (`mheard_functions.cpp:446`); 0 = unknown.
5. mheard/NCount flash persistence exists ONLY on T-Deck / T-Deck Plus with SD opt-in
   (`meshcom_settings.node_persist_to_sd`, `mheard_functions.cpp:151-180, 939-975`).
   On all other boards the table is empty at boot → uptime == data age.
6. Fleet proxy: `fleet.json` (1430 DB nodes): 98.2% have a nonzero lat/lon in the DB,
   i.e. have reported positions; 2 withheld. (DB position could in a few cases be
   operator-set — treated as an upper-bound proxy, see F4 open measurement.)

---

## F3 — sole-provider starvation. VERDICT: CONFIRMED (with 3 corrections)

**(a) Topology consistency: consistent, but the diagram is missing two edges.**
For R to hear H's and M's relays as duplicates, R must demodulate H and M directly —
so R–H and R–M RF links exist and H, M are in R's mheard. Recomputed importance:
imp_R = 1/NC_L + 1/NC_S + 1/NC_H + 1/NC_M = 1.0 + (three small terms). The claimed 1.2
requires NC ≈ 15 for S, H and M each; with a more typical NC_M ≈ 3–5 the realistic value
is **1.3–1.6**. Conclusion unchanged: at CAP=8, slot_start=(1−1.5/8)·7 ≈ 5.7 → slots
5..7, far behind H (imp 7 → slots 0..2). Ordering works, R still cancels on k=2
duplicates, L starves. Rev. 3 should draw the R–H/R–M edges and use imp ≈ 1.4.
The failure is also the ADR's own removed-commit field result (ADR:1177-1178: "Leaf
Nodes mit nur einer Gegenstelle haben oft keine Nachrichten erhalten") — Stufe 1 changes
who sends first, not who depends on whom. The mechanism survives correct ordering.

**(b) Is the sole-provider veto sufficient? NO — counter-case constructed.**
Pair-cancel case: leaf L2 hears exactly R1 and R2, so L2 reports NC=2 and contributes
0.5 to each. Hub H and node M relay first (duplicates 1 and 2 for both R1 and R2);
R1 and R2 cancel in the same round — deterministically, because they see the SAME
duplicates. Neither veto fires (no neighbor with NC_reported ≤ 1), L2 gets nothing.
The veto as specified only protects NC=1 dependents; NC=2 dependents are starved
whenever both providers are low-importance and co-hear the same hubs — which is the
typical geometry (both sit in the same shadowed valley).

General condition: node u may safely cancel relay of m only if every mheard neighbor n
of u has ≥1 OTHER provider that (i) received m and (ii) will not cancel. This is not
locally decidable: u knows n's NC (a count) but not the identity, coverage, or cancel
decision of n's other providers. A proof-of-reception rule ("cancel only if every
neighbor was itself heard transmitting m") fails on timing: a leaf's own relay of m
comes after u's slot, so the proof is unavailable at decision time. Every local rule is
therefore a heuristic. Rev. 3 formulation:

- veto if any active neighbor has NC_reported ≤ NC_VETO or ==0/unknown, with
  **NC_VETO = 2** (not 1) as default — covers the pair-cancel case at the cost of less
  suppression;
- additionally randomize: cancel with probability p < 1 (e.g. 0.75) so that correlated
  same-round cancels of a shared dependent's few providers are never deterministic;
- keep k ≥ 2 and the per-suppression log line as specified.

**(c) Cheap to compute: YES.** One O(MAX_MHEARD ≤ 80) pass over `mheardNCount[]` +
`mheardEpoch[]` (both exist, see facts 2/4) — same cost as `getMheardCount()`.
Two honest caveats for Rev. 3: (i) unknown NC (=0) is reported by ~15% of origins and
by ALL pre-4.35n firmware (~17–19% of fleet) → the veto fires for any node hearing one
old-firmware neighbor → suppression will be largely inert in mixed-firmware regions.
Fail-safe direction, but the ADR must not promise Stufe-2 airtime savings before fleet
penetration is high. (ii) Receive-only stations never enter anyone's mheard — dependents
that never transmit are invisible to any veto. State as a known limit.

**(d) Does the ADR equate low importance with redundancy? YES — verbatim.**
ADR line 809: "Mit Importance-Backoff senden Hubs zuerst → Suppression storniert
korrekt die redundanten Blaetter." And ADR:1192-1194: "Erst mit aktivem
Importance-Backoff ist Suppression sicher. Der Hub sendet zuerst (Stufe 1). Die
Blaetter und redundanten Nodes senden spaeter und hoeren dabei den Hub als Duplikat →
Suppression storniert ihre Relays → korrekte Richtung." Both places treat "sends later
(= low importance)" as "safe to cancel". The ADR even contains the seed counterexample
itself (ADR:1151-1153, the leaf behind the Berg-Hub) but applies it only to the hub
being wrongly cancelled, never to a low-importance sole provider being rightly-ordered
and still wrongly cancelled. F3's reading of the ADR is accurate.

---

## F4 — rich-get-richer loop. VERDICT: REFUTED as a general loop; contingent residual

**The premise fails against the code.** F4 asserts "NC measures observed transmissions,
not link existence" and that suppressed relays "vanish as last hop" from tables within
1 h. Both halves are wrong for the fleet majority:

- `getMheardCount()` counts distinct present neighbors, not transmission volume
  (fact 2). Relaying more does not raise a hub's contribution to neighbors' NC, and
  relaying less does not lower a suppressed node's — presence is binary per window.
- Presence is refreshed by ANY demodulated own frame (fact 1): positions, HEYs, ACKs,
  own texts — not only relays. A suppressed relay keeps beaconing.
- Default position cadence is 30 min (fact 3) < the 60-min window → a
  position-enabled node is visible **100%** of the time (2 refreshes per window).
  Visibility fraction for own-frame cadence X min is min(1, 60/X): X=30 → 100%;
  HEY-only at trickle steady state X=57–170 min → 105%..35% → flicker.
- Therefore the loop's first link (suppression → table dropout → NC decay) is severed
  for every node that sends positions. DB proxy: 98.2% of 1430 nodes have reported
  positions (fact 6). The loop premise holds only for the residual class:
  position-disabled (lat/lon unset) or position-withheld nodes living off trickle HEYs
  alone — order 2% by the proxy. **Open measurement** (feed-side, cheap): count distinct
  origins in the pos feed over 24 h (~18.5k pos/4.75 h ≈ 3.9k/h ≈ 2.7 per node-hour is
  consistent with ~most of 1430 nodes at the 30-min default, but distinct-origin count
  was not extracted; heys.jsonl is HEY-only and cannot answer it).
- Corollary: F4's proposed "data-side fix" ("mheard refresh on any demodulated frame
  from the neighbor, not only on frames it originates/relays") **already describes the
  shipped behavior** — `updateMheard` keys on `msg_source_last` of every decoded frame,
  duplicates included. As written it is a no-op recommendation; Rev. 3 must not adopt
  it as a change. The only real data-side gap is the HEY-only minority (beacon floor
  for position-less nodes, or count them as "assume NC=1 dependents" — which the F3
  veto then covers).

**What survives of F4 (and should go into Rev. 3 differently):**

- Second-order coupling exists on marginal links: a node transmitting more often has
  more demodulation chances through collisions/fading, so volume weakly influences
  presence at the RF edge. Slow drift at the margins, not a runaway.
- The symmetric claim also falls: F4's "Stufe 1 self-damps" mechanism (hub relays more
  → enters more tables → its 1/NC contribution falls) relies on the same volume→NC
  coupling and is equally near-zero for already-visible nodes. The no-runaway
  conclusion stands, but by inertness, not by negative feedback.
- The end-state concerns (hub SPOF, hub battery drain, serial relay capacity ceiling)
  are real but STATIC consequences of concentrating relays on hubs — they need no
  feedback loop and should be stated as such.
- **Monitoring:** median reported nct (F4's metric 2) is the WRONG early-warning
  signal — it will not move while beacons keep tables full. The right signals:
  (1) relay concentration (top-decile share of mid-path appearances; today top-20/397
  ≈ 35%) — measures the loop's claimed output directly; (2) count of active nodes
  whose only frames in 24 h are HEYs (the actually-vulnerable class, directly
  extractable from the feed).

---

## F5 — cold start. VERDICT: NUANCED (direction right, timeline wrong ~3x, fix simplifiable)

**Gate-opens-into-deflated-NC: confirmed.** The 3.1 gate tests knownness; early HEYs
carry R1/R2 within minutes, so known_ratio rises fast while values are transiently
tiny. With CAP ≤ 4 the inflated Σ(1/small) puts many ordinary nodes at slot_start 0
during the restart burst. Couples with F1 exactly as claimed. At CAP=8 tolerable.

**Timeline arithmetic: the "~60 min linear ramp" premise is wrong.** The 1-h window
bounds expiry, not accumulation. A neighbor enters the table on its FIRST demodulated
frame after boot, and after a synchronized reboot every node transmits early:
trickle HEYs from Imin=30 s (doubling; measured burst 6–17 HEYs/node in the first hour,
14 in the first 28 min per docs/hey-supp.md), plus a first position at millis 100–130 s
(`bPosFirst`, fact 3). Absent losses, tables — and hence `getMheardCount()`, carried
outbound in every subsequent `R<NC>;` HEY and `/N` position — approach steady state in
**~5–15 min**, not 60. Congestion losses during the restart storm stretch this
(collisions delay first demodulation), so a realistic deflation window is **~10–20 min**
— which is also the peak-load window. So the hazard is real but 3x shorter than F5's
walkthrough implies; its severity call ("Medium, self-limiting") stands.

Also correct in Rev. 3: F5's "mheardNCount[] reloads from flash" branch applies ONLY to
T-Deck/T-Deck Plus with SD persistence opted in (fact 5). Every other board boots with
an empty table, so the stale-NC-drives-the-gate arm of F5 is a T-Deck corner case, and
F6e's relocation concern likewise.

**Hold-down vs. uptime gate: effectively equivalent — prefer the uptime gate.**
Because the table is boot-empty on all non-T-Deck boards, "mheard window has ≥1 h of
history" is exactly "uptime ≥ 1 h". Rev. 3 formulation: gate additionally on
`millis() >= 60*60*1000UL` (uint32 subtraction-safe; wrap at 49.7 days is irrelevant
since the condition is only consulted to OPEN the gate once). 45–60 min is generous
against the ~10–20 min deflation window, but the cost is zero — with the gate closed,
behavior is today's full-jitter path — so 60 min is fine and simpler to specify than a
per-entry data-age scan. **Code support exists today:** no dedicated uptime variable,
but millis() is boot-relative on both ESP32 and nRF52 and the codebase already gates on
raw millis at boot (`bPosFirst` check `millis() > 100000 && millis() < 130000`,
`esp32_main.cpp:3104`). One added condition in the 3.1 gate function; on T-Deck with
persistence it is strictly more conservative than data age — acceptable.

---

## Summary for Rev. 3

| Claim                                 | Verdict                | Correction to carry into Rev. 3                                                                                                                                                                                                                                                   |
| ------------------------------------- | ---------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| F3 topology + starvation mechanism    | CONFIRMED              | Add R–H/R–M edges to diagram; imp_R ≈ 1.4 not 1.2                                                                                                                                                                                                                                 |
| F3 sole-provider veto (NC==1/unknown) | INSUFFICIENT           | Pair-cancel counter-case (NC=2, both providers cancel same round); use NC_VETO=2 + randomized cancel p<1; note old-firmware NC=0 makes suppression inert in mixed regions; receive-only stations unprotectable                                                                    |
| F3 veto cost                          | CONFIRMED              | O(MAX_MHEARD) over existing `mheardNCount[]`/`mheardEpoch[]`                                                                                                                                                                                                                      |
| F3 ADR equates low imp = redundant    | CONFIRMED              | Quotes at ADR:809 and ADR:1192-1194                                                                                                                                                                                                                                               |
| F4 rich-get-richer loop               | REFUTED (general case) | `updateMheard` refreshes on ANY own frame; 30-min default positions keep nodes 100% visible in the 1-h count; loop contingent on the HEY-only minority (~2% by DB proxy; exact share = open measurement). Keep SPOF/battery/capacity as static concerns; drop the runaway framing |
| F4 proposed mheard-refresh fix        | REFUTED                | Already shipped behavior — no-op                                                                                                                                                                                                                                                  |
| F4 monitoring metric (median nct)     | REFUTED                | Use relay concentration + HEY-only-node count instead                                                                                                                                                                                                                             |
| F5 gate opens into deflated NC        | CONFIRMED              | Hazard window ~10–20 min, not ~60; only material at CAP ≤ 4                                                                                                                                                                                                                       |
| F5 flash-persistence arm              | NUANCED                | T-Deck SD opt-in only; other boards boot empty                                                                                                                                                                                                                                    |
| F5 boot hold-down 45–60 min           | CONFIRMED (simplify)   | Specify as uptime gate `millis() >= 1h` in the 3.1 gate — equivalent on non-persistent boards, zero-cost, precedent exists (`bPosFirst`)                                                                                                                                          |
