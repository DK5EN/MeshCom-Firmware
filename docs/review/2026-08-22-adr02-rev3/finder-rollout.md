# Finder: Rollout/Deployment Engineering Review — ADR NC-Importance-Backoff (Rev. 2)

Angle: staged-rollout safety for a volunteer, no-central-control amateur mesh network.
Read-only review. Firmware baseline: v4.35p_prio, spot-checked 2026-08-22.

**Correction to the task's own framing (affects Finding 5 severity):** the claim
"T-Beam (MAX_DEDUP_RING=10, MAX_MHEARD=10, 25% of fleet)" is not supported by the
current build matrix. `MAX_MHEARD=10`/`MAX_DEDUP_RING=10` in
`configuration_global.h:186-191` is gated by `#elif defined(ENABLE_TBEAM)`, annotated
in-code as "very smal version only for developer tests" — and no `platformio.ini`
env (top-level or any `variants/*/platformio.ini`, checked: `ttgo_tbeam`,
`ttgo_tbeam_SX1262`, `ttgo_tbeam_SX1268`, `ttgo_tbeam_supreme`, `LilyGo_T-Beam-1W`)
defines `ENABLE_TBEAM`; all real T-Beam variants build with `${esp32.build_flags}`
and fall through to the `#else` branch: **`MAX_MHEARD=30`, `MAX_DEDUP_RING=70`**,
identical to "ESP32 klassisch" (same bucket as TLORA V2.1.6). The ADR's own
Datenqualitaet table (line "Saettigung | ... 10 (T-Beam)") inherits the same error
and should be corrected in Rev. 3. This changes who the real 30-cap underclass is —
see Finding 5.

---

## Finding 1 — No Stufe 0 (observation-only) stage exists

**Gap:** The ADR jumps straight from "Entscheidung" (behavior-changing Stufe 1) to
field validation _after_ deployment. There is no stage where `getNetImportance()`,
the resulting slot, and `getNetImportanceKnownPct()` are computed and logged on
production hardware **without** touching `csma_compute_timeout_prio()`'s return
value. The only log line in the current draft (5.3) is inside the live code path
that already changed the backoff (`if(bDisplayInfo || bLORADEBUG) Serial.printf(...)`)
— by the time anyone can read `[MC-IMP]`, the behavior change is already active.

**Why it matters:** This is a volunteer network with no central rollout control,
78%/17-19% split between 4.35p and older firmware, and the ADR's own worst-case
risk ("gemischte Flotte kollabiert die Differenzierung", line 974) is a live-traffic
failure mode. There is currently no way to validate the importance formula, the
`known_pct` gate threshold (50%, explicitly "Schaetzung"), or `IMP_CAP` (8.0, also
"Schaetzung") against real topology before the gate can flip live traffic ordering.
The evidence pack's own importance simulation (508 observed nodes, `heys_analysis.txt`)
had to be built entirely from mcmap's externally-observed HEY feed — precisely
because the firmware itself produces no comparable telemetry today.

**Severity:** High — this is the single biggest gap for a "Stufen, um unerwuenschten
Nebenwirkungen entgegenzutreten" rollout story; without it, Stufe 1 field validation
(the ADR's own "Feld-Validierung" checklist) has no pre-deployment baseline to
compare against, and the first live signal from a change that alters TX ordering
is post-hoc.

**Concrete Rev. 3 proposal — Stufe 0:**

- Compute `getNetImportance()`, `getNetImportanceKnownPct()`, and the slot the node
  _would_ get, on every relay decision, but feed the **existing** unmodified return
  value (full 0..9 jitter) into `csma_compute_timeout_prio()`. Guard with a compile
  or flash flag (see Finding 2) so this is a distinct, revertible build/config from
  Stufe 1.
- Log format, one line per relay decision, both when `bDisplayInfo`/`bLORADEBUG` is
  active (not gated behind it exclusively — see Finding 2 on visibility):
  `[MC-IMP] mode=obs imp=%.2f known_pct=%d slot_would=%d sat=%d nc_self=%d`
  where `sat` = 1 iff `getMheardCount() == MAX_MHEARD` (own-node saturation flag,
  per the ADR's own "Saettigung als Datenfehler" rule in Kap. "Datenqualitaet").
  Route through `printfdeb()`, not raw `Serial.printf()` — see Finding 1a.
- **1a — visibility bug in the current draft:** `src/mheard_functions.cpp` /
  `src/printfdeb_functions.cpp` show `printfdeb()` is the wrapper that mirrors
  output to `net_console.cpp` (port 2323, `#ifdef ESP32`) — the mechanism this
  project already uses to make debug output remotely inspectable. The ADR's 5.3
  code sample calls raw `Serial.printf("[MC-IMP] ...")`, which **never reaches the
  net console** and is invisible to anyone not physically attached via USB serial.
  For a fleet where nodes sit on remote mountaintops, this makes Stufe 0/1
  observation impossible for the majority of the network. Rev. 3 must route
  `[MC-IMP]` through `printfdeb()`.
- No web UI surface for mheard/importance was found (`grep` across `data/`, `*.html`
  turned up nothing beyond `safeboot/ota.html`) — Stufe 0 observability is
  serial + net-console only unless a web UI page is added; note this as scope,
  don't silently assume web visibility exists.
- Minimum observation window before promoting to Stufe 1: propose >=2 weeks across
  a mixed-firmware neighborhood so `known_pct` samples span the trickle cycle
  (30s-15min) and diurnal traffic patterns (the evidence pack's storm hour,
  IU4KCH-26, shows 2.4x normal traffic in a single hour — Stufe 0 data must include
  at least one such anomaly window).

---

## Finding 2 — No runtime kill switch / tuning; ADR explicitly rejects the pattern the codebase already has

**Gap:** `src/command_functions.cpp:564-588` shows a directly analogous precedent:
`--shortpath on`/`--shortpath off` toggles `bSHORTPATH`, persists a bit in
`meshcom_settings.node_sset` (`src/esp32/esp32_flash.h:48`, `int node_sset = 0x0004`,
plus siblings `node_sset2/3/4` — all have spare bits), and calls `save_settings()`.
This is the established "per-node runtime tunable, survives reboot, no recompile"
pattern in this codebase. The ADR (line 468) explicitly chooses **not** to use it:
"schaltet die Differenzierung von selbst zu — ohne Feature-Flag, ohne koordinierten
Stichtag." All new constants (`RELAY_IMP_CAP`, `RELAY_TOTAL_SLOTS`,
`RELAY_JITTER_WIDTH`, `RELAY_IMP_MIN_KNOWN_PCT`) are `#define`d in
`configuration_global.h` — compile-time only.

**Why it matters:** The ADR's "Offene Punkte" section itself lists `IMP_CAP`,
`RELAY_JITTER_WIDTH`, `RELAY_IMP_MIN_KNOWN_PCT`, and `RELAY_TOTAL_SLOTS` as
unresolved tuning parameters ("Schaetzung", "Feldvalidierung"). Tuning a compile-time
constant on a fleet with no central rollout control means every parameter change
requires convincing individual operators to reflash — for a change whose own risk
section identifies a live-traffic collapse mode ("gemischte Flotte kollabiert die
Differenzierung", "Stufe 1 schlechter... als der Status quo"). If that mode is
observed in the field (e.g. a specific dense cluster misbehaves), there is currently
no way to disable importance-slotting on the affected nodes without a firmware
rebuild + reflash cycle, which for a volunteer amateur network can take weeks
(consistent with the stated 4.35a-4.35p fleet spread over months).
The self-gate (3.1, `known_pct < 50%` -> old behavior) is a good automatic safety net
for the "not enough data" case, but it is not a substitute for an operator-controlled
switch — it cannot be used to abort a _correctly-gated_ rollout that is nonetheless
causing field problems (e.g. the risk noted in 4.7a: thin CAD-scan margin, or an
IMP_CAP mis-tuning that the gate cannot detect because it only measures data
availability, not ordering correctness).

**Severity:** High for Stufe 2 (given Stufe 2's own prior history: "Feldtest zeigte:
Leaf Nodes mit nur einer Gegenstelle haben oft keine Nachrichten erhalten" — this
already happened once with a compile-time-only mechanism and had to be reverted by
git revert, not a field switch). Medium for Stufe 1, softened by the self-gate.

**Concrete Rev. 3 proposal:**

- Add `--impslot on/off/auto` mirroring the `--shortpath` pattern exactly: persist
  in a spare bit of `node_sset`/`node_sset2`, `save_settings()`, printed via
  `--info`/status output the same way `shortpath` state is surfaced. `auto`
  (default) = today's self-gating behavior (3.1); `off` = hard-forces old
  full-jitter path regardless of `known_pct` (the true kill switch, for the case
  where the gate itself is passing but the outcome is still bad); `on` = force
  importance-slotting even below the gate threshold (useful for controlled beta
  clusters — see Finding 3 — where the operator _wants_ to test with sparse data).
- Make `RELAY_IMP_CAP`, `RELAY_IMP_MIN_KNOWN_PCT`, and `RELAY_JITTER_WIDTH` flash
  settings with compile-time defaults (same pattern as other tunables already in
  `meshcom_settings`), not hard `#define`s — the ADR's own Offene-Punkte list
  concedes these values are unvalidated guesses; hard-coding them forces a
  reflash-fleet-wide cycle for every tuning iteration during exactly the field
  validation phase the ADR calls for.
- Defaults: compile-time constant is fine for `RELAY_TOTAL_SLOTS`/`RELAY_JITTER_WIDTH`
  (touches CSMA timing tables other code may assume are fixed) but should still be
  overridable via the flash-setting mechanism for the beta cluster, gated behind
  `--impslot on` so it never silently activates on nodes that haven't opted in.
- **Incremental-deployability question the ADR does not analyze:** the ADR
  simulates the _gate_ (does `known_pct` reach 50%?) against today's 81%-adoption
  topology (`heys_analysis.txt`: 98.6% of 508 nodes already pass the gate) but never
  simulates _ordering correctness_ at intermediate adoption ratios (e.g. 20%, 30%,
  50% of a co-hearing group running Stufe-1 firmware while the rest run old
  firmware). Because `known_pct` is evaluated per-node against that node's own
  neighbor set, a node in a dense early-adopter pocket can pass the gate and start
  reordering while most of its two-hop neighborhood is still unpatched — the ADR's
  own topology validation (Kap. 2) only checks correctness assuming uniform
  adoption. Rev. 3 should add a synthetic-mixture simulation (e.g. randomly flip
  20/30/50/70% of the observed 508-node graph to "reporting", holding the rest at
  nct=0) and check whether early-adopter nodes end up systematically favored or
  penalized relative to their real importance — this is exactly the kind of check
  a Stufe 0 observation window (Finding 1) would also surface empirically.

---

## Finding 3 — No beta-cluster stage; no concrete selection criteria or per-cluster metrics

**Gap:** The ADR has no explicit beta/pilot-cluster stage between "Stufe 0 doesn't
exist" and "flip the self-gate fleet-wide." Deployment is: build a release, let
operators reflash on their own schedule, and let the self-gate decide per-node when
enough neighbors report NC. There is no proposal to seed a geographically coherent,
densely-instrumented cluster first and measure it in isolation.

**Why it matters:** The evidence pack's leaderboards show at least two dense,
well-instrumented regional clusters already visible in the 24h feed:

- **OE3 (Austria)** — home region of the mcmap instance (meshmap.oevsv.at, OEVSV =
  Austrian amateur radio society); appears repeatedly in both relay and gateway
  leaderboards (`OE3XIA-12` #1 by simulated importance, 803 relay appearances;
  `OE3MIF-12` 341 relays; `OE3GHB-1`, `OE3XHU-22`, `DB0RVB-99` among top
  gateways/reporters).
- **Tuscany/Italy (IU5/IZ5/IR5/IW5/IQ5ARI prefixes, ARI-affiliated)** — extremely
  dense in the same leaderboards: `DB0IBH-90`, `IR5UDV-10`, `IQ5ARI-13`,
  `IQ5ARI-11`, `IZ5TRQ-11`, `IU5RCZ-12`, `IU5SNJ-12`, `IW0UTD-11`, `IW5EIV-11`,
  `IZ5IOM-13`, `IK5ZXH-98`, `IR5ZYQ-10`, `IZ5YYF-12`, `IZ5RWI-12` all place in the
  top-25 relay list — a single regional cluster contributing roughly half the
  top-25 relay load network-wide.

Both clusters are already firmware-homogeneous enough (mostly 4.35p per the
leaderboard's `fw=` column) and dense enough that a `known_pct` gate would open
quickly if a coordinated subset reflashed, giving a fast, bounded feedback loop
instead of waiting for organic fleet-wide adoption.

**Severity:** Medium — the self-gate provides passive safety, but without a
deliberately chosen cluster + explicit metrics, "Feld-Validierung" (the ADR's own
open item) has no concrete protocol and will happen ad hoc, by whichever operators
reflash first, with no coordinated before/after measurement.

**Concrete Rev. 3 proposal — Stufe "Beta":**

- Selection criteria: (a) geographically/topologically coherent (mostly resolves
  within 2 hops of a gateway with good mcmap visibility), (b) already
  firmware-homogeneous on 4.35p or later (>=81% `>=4.35n` per fleet data, so
  `known_pct` opens fast), (c) has an identifiable operator/maintainer group willing
  to coordinate a synchronized reflash window and watch metrics — OE3 and the
  Tuscany/ARI cluster both qualify per the leaderboard evidence above; either is a
  reasonable Rev. 3 candidate, final pick should be an operator-consensus call
  (relationship with local maintainers), not a firmware decision.
- Before/after metrics mcmap can actually compute per cluster (all backed by
  `link_load_overview`/`messages_query`-class tools already used to build the
  evidence pack, scoped to the cluster's node/gateway set):
  - `link_load` traversals and distinct origins per directed segment, filtered to
    edges inside the cluster — expect traversal count per unique message to drop
    if Stufe 1+2 reduce redundant relays (compare to this review's fleet-wide
    baseline DUP/NEW=0.70).
  - Per-message DUP/NEW ratio and unique-msg_id count restricted to cluster
    gateways' feed reports — the ADR's own target is `DUP/NEW < 0.5` fleet-wide;
    track it per-cluster before/after so a cluster-local regression doesn't hide
    in a network-wide average.
  - Path-length distribution shift for messages originating in-cluster (today:
    63.8%/19.8%/9.8%/4.5%/2.1% for 1-5 hops network-wide) — a working
    Stufe-1+2 should show more messages resolved in fewer hops from hub-adjacent
    origins, not a wholesale shift toward `MAX_HOP` exhaustion (today 20.7%
    network-wide per BergLog).
  - Zombie-candidate count (msg_id reappearing 10-60min after first sight, 1368
    fleet-wide today) restricted to cluster-local msg_ids, as a Stufe-3-independent
    sanity check.
  - CAD-busy proxy: since mcmap has no direct channel-occupancy telemetry (feed
    traffic "is NOT channel occupancy/airtime" per the evidence pack's stated blind
    spot), this metric needs an on-node counter exposed via `[MC-IMP]`/net console,
    not mcmap — flag explicitly that mcmap cannot validate the ADR's own
    `CAD-Busy < 30%` target metric; only firmware-side instrumentation can.

---

## Finding 4 — No concrete, numeric abort thresholds; no named owner

**Gap:** The ADR's "Feld-Validierung" checklist lists target metrics ("Zielwerte:
DUP/NEW < 0.5, CAD-Busy < 30%, keine Zombies") but these are **success** criteria
for declaring a stage validated, not **abort/rollback** criteria for reverting a
stage that is actively making things worse. Nothing in "Offene Punkte" or the risk
section assigns an owner, a monitoring cadence, or a decision rule distinguishing
"not yet converged, keep watching" from "actively regressing, revert now."

**Why it matters:** In a network with no central rollout control, a stage that goes
live silently (self-gating, no kill switch per Finding 2) can degrade service for a
subset of nodes (as Stufe 2 already did once: "Leaf Nodes mit nur einer Gegenstelle
haben oft keine Nachrichten erhalten") for an unbounded time before anyone with
write access notices and ships a revert.

**Severity:** High, specifically because Finding 2 shows there is no fast individual
kill switch — the only current remedy for a bad rollout is a new firmware build and
another multi-week reflash cycle, which makes early, precise detection much more
important than in a system that could flip a flag back.

**Concrete Rev. 3 proposal:**

- Numeric abort thresholds, evaluated against the _beta cluster's own pre-stage
  baseline_ (not the fleet-wide historic average, since clusters vary — Tuscany's
  and OE3's baseline DUP/NEW will differ from the 0.70 fleet figure):
  - Stufe 1: abort/revert-to-`--impslot off`-broadcast-request if cluster DUP/NEW
    rises >20% over its own pre-stage 7-day baseline, sustained over 48h (guards
    against the ADR's own "gemischte Flotte kollabiert" failure mode slipping past
    the `known_pct` gate due to Finding 2's untested mixture-ratio gap).
  - Stufe 2 (once reimplemented): abort if any single-neighbor leaf node (nct=1
    population, 20.0% of fleet per the nct distribution) in the beta cluster shows
    a >5% drop in successful delivery rate (measurable via `messages_query`
    ack/no-ack outcomes for that node) over the prior baseline — this is the exact
    failure mode that killed the original Stufe 2 implementation and must have an
    explicit, monitored guard this time, not just a design argument that Stufe 1
    fixes it.
  - Stufe 3: abort/reduce if RAM headroom on any affected board class drops below
    the project's existing safety margin after `MAX_DEDUP_RING` enlargement (this
    is a `ram-snapshot`-checkable, pre-deployment gate, not a field-abort one —
    catch it before the beta window opens).
- Owner: since there is no central authority, assign the cluster's own
  maintainer(s) (identifiable from top-gateway leaderboard — e.g. whoever runs
  `DB0HWR-12`/`OE3GHB-1` for OE3, or the ARI-affiliated gateway operators for
  Tuscany) as first responders, with the ADR author as second-line for
  cross-cluster comparison; watch cadence: daily during the first 2 weeks of a
  beta window, weekly after.

---

## Finding 5 — Stage sequencing: Stufe 3 (dedup enlargement) has no dependency on Stufe 1/2 and should plausibly ship first

**Gap:** The ADR mandates 1 -> 2 -> 3 and justifies it entirely by Stufe 2's
dependency on Stufe 1 ("Suppression ohne Importance-Backoff... storniert die
falschen Nodes"). But re-reading the ADR's own "Zusammenfassung der Abhaengigkeiten"
diagram: the arrow into Stufe 3 is "wird unterlaufen ohne" (Stufe 2 is undermined
_without_ Stufe 3 also being present), not "Stufe 3 requires Stufe 1/2 first."
Stufe 3 (`MAX_DEDUP_RING` 60->200-256) has no logical dependency on the importance
formula or on relay-suppression at all — it is a standalone ring-size bump.

**Why it matters:** The evidence pack shows the zombie problem Stufe 3 fixes is
happening **today**, independent of Stufe 1/2 ever shipping: 1368 msg_ids
(zombie candidates, 10-60min reappearance span) out of 18,252 msg_ids seen more than
once in the 24h window; the BergLog worst case (`32312D47`, 46x relayed/52x accepted
as "new") predates any importance/suppression work. Sequencing it last, gated behind
two mechanisms (Stufe 2 in particular, which the ADR itself calls "eine
Neuimplementierung" requiring new cancel-safe TX-ring logic) that are materially
harder and riskier to ship, delays a fix for a real, currently-active, low-risk
problem by however long Stufe 1+2 field validation takes (the ADR's own Stufe 1
validation checklist alone implies weeks-to-months).

**Severity:** Medium — this is a sequencing/prioritization issue, not a correctness
bug in the ADR's dependency logic (which is internally consistent for 1->2). But it
directly affects the operator's stated goal ("Eskalationsstufen... um unerwuenschten
Nebenwirkungen entgegenzutreten") — the safest, most independently-verifiable win
available today is being held back behind the riskiest one.

**Concrete Rev. 3 proposal:**

- Relabel: ship dedup-ring enlargement as **Stufe 0.5** or fold it into the
  observation stage (Finding 1) — it requires no new formula, no CSMA change, and
  is trivially A/B-testable per board class via `ram-snapshot` for headroom and
  the existing zombie-count metric (msg_id reappearance 10-60min) as its own
  before/after signal, fully independent of whether Stufe 1/2 ever land.
  Re-verify the "kostet ~1 KB RAM" claim per board with the `ram-snapshot` skill
  before shipping, since headroom differs by board class (30 vs 80-slot
  `MAX_MHEARD` boards, per corrected numbers above, have different RAM budgets).
- Keep the explicit 1->2 dependency (Importance before Suppression) as-is — that
  logic is sound and should stay a hard blocking order.

**T-Beam / underclass consequence per stage, using corrected numbers (see header
correction):** real T-Beam hardware (V1.1, V1.2, 1W — 25.4% of fleet by hardware
distribution: 16.2% + 9.2%, plus 1W's smaller share) shares the **30-slot**
`MAX_MHEARD`/**70-slot** `MAX_DEDUP_RING` bucket with TLORA V2.1.6 (32.0% of fleet)
— together roughly **57%** of the fleet, not a small T-Beam-only minority, sits on
the smallest actively-used buffer class. This is the real underclass to plan around:

- Stufe 0.5/3 (dedup): these boards get the smallest `MAX_DEDUP_RING` uplift target
  in absolute terms if RAM-constrained; confirm with `ram-snapshot` whether 200-256
  is reachable on classic ESP32 (160KB DRAM) before committing that number
  fleet-wide, or whether this class needs a lower board-specific target than
  S3/nRF52.
- Stufe 1 (importance): `deep_analysis.txt`'s "saturation candidates" figure ({10:
  96, 30: 6}) was computed against the _wrong_ cap (10) per the header correction —
  redo this analysis against the _real_ cap (30) for classic-ESP32 boards. Notably,
  the single highest-load relay in the entire 24h dataset, `DB0HOB-12` (TBEAM V1.2,
  1164 relay appearances, `nct_own=26` — consistent with a 30-cap, not a 10-cap),
  sits close to its board's real saturation ceiling; a corrected saturation analysis
  at cap=30 is needed before trusting the ADR's importance simulation for this
  57%-of-fleet class, since the current simulation doesn't distinguish per-board
  `MAX_MHEARD` when computing `nc_reported` inputs.
- No board class is permanently locked out of Stufe 1 by the self-gate (3.1) purely
  due to `MAX_MHEARD`, since the gate measures `known_pct` of neighbor-reported NC,
  not own-node saturation — own-node saturation instead produces an _understated_
  importance value for hub nodes on 30-cap boards (undercounts total dependents),
  which is a quieter, harder-to-detect bias than an outright gate block. Flag this
  distinction explicitly in Rev. 3's risk section — it currently reads as if
  saturation and gate-blocking were the same failure mode; they are not.

---

## Finding 6 — Upstream PR-ability not assessed; no split of firmware-PRable vs. out-of-scope work

**Gap:** The ADR's "Betroffener Code" section (Kap. 5) lists file-level changes but
never addresses the project's own PR workflow constraint (per this repo's
`CLAUDE.md`: minimal, targeted PRs against upstream `icssw-org` DEV, each with a
German rationale). Nothing in the ADR flags which pieces are self-contained enough
to land as an independent, reviewable upstream PR today versus which require
operator/community consensus before any PR is worth submitting.

**Why it matters:** Rev. 3's rollout plan is only actionable if it maps onto how
changes actually reach the fleet — via upstream-merged PRs, not via this fork's
branch. A monolithic "Stufe 1" PR that bundles the formula, the slot mechanic, the
quality gate, and new `#define`s is reviewable in isolation (touches
`configuration_global.h`, `mheard_functions.cpp`, `lora_functions.cpp` only, no
protocol/wire-format change) and matches the "minimal changes only" constraint.
But several ideas surfaced in this review and in the ADR's own text are not
firmware-PRable at all, or need community sign-off first, and conflating them with
the core PR risks stalling the whole thing in upstream review:

- **Out-of-scope-but-recommended, not firmware:** server-side rate limiting of
  storm origins. The evidence pack's spike-hour data (`IU4KCH-26`, 5912 HEY frames
  in one hour, 1.6/s) is a single misbehaving origin degrading the shared channel
  for everyone; nothing in this ADR (or a plausible firmware fix) throttles a
  single origin's _rate_, only relay ordering downstream of it. This belongs in
  mcmap/gateway-side ingestion policy, not a firmware PR — flag explicitly in Rev. 3
  so it isn't silently dropped for being "someone else's problem."
- **Needs operator consensus before any PR:** the beta-cluster stage (Finding 3)
  and the kill-switch default posture (Finding 2, `--impslot auto` vs `on`/`off`
  default) are deployment/community decisions, not code decisions — worth a
  paragraph in the ADR distinguishing "ready to PR once Stufe 0 data exists" from
  "needs the OE3/Tuscany maintainers to agree to a coordinated window first."
- **PR-able now, independent of the rest (per Finding 5):** the dedup-ring
  enlargement alone is the smallest, least controversial upstream PR available —
  a single-line-per-board-class constant change with a currently-observable,
  quantifiable problem (1368 zombie candidates) as its justification, no formula,
  no CSMA change. Recommend Rev. 3 explicitly sequence it as the first PR
  submitted, separate from the importance-backoff PR, so it isn't held hostage to
  the (correctly) slower and more contentious Stufe 1/2 review.
- **Importance v2 (Kap. 8):** already correctly marked "Ausbaustufe, nicht Teil von
  Stufe 1" in the ADR — consistent with minimal-PR discipline; no change needed
  here, just noting it's the one section that already gets this right.

**Severity:** Medium — doesn't block Stufe 1 technically, but affects how fast any
of this actually reaches the 22% of the fleet not yet on `>=4.35n`, given the
project's explicit minimal-PR-only upstream constraint.

**Concrete Rev. 3 proposal:** Add a short "Deployment-Kanaele" subsection listing,
per stage: (a) firmware change scope and PR-readiness, (b) whether it needs
operator/cluster-maintainer consensus before submission, (c) explicit non-firmware
recommendations (storm-origin rate limiting) marked out-of-scope with a pointer to
where that discussion should happen (mcmap/gateway operators, not this ADR).
