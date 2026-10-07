# Finder: Wire-Protocol Correctness / Mixed-Firmware Interop

Angle: does the ADR's description of the NC wire channels match what the code actually
parses, stores, and ages — and are there interop hazards a mixed 4.35n/4.35p.06.11/older
fleet creates that the ADR misses.

All line numbers verified 2026-08-22 against `v4.35p_prio` working tree.

---

## F1 — Two NC channels have different firmware-version floors, not one

**ADR claim:** Kontext/"Datenqualitaet des NC" table, row "Kanal 1: Position" and "Kanal
2: HEY" (adr-nc-importance-backoff.md:198-199) list both channels side by side with no
version distinction; the evidence pack (evidence-pack.md:28-29, :44) attributes both to
"firmware >= 4.35n" and reports one blended fleet number ("~81%").

**Code reality:**

- HEY's `R<NC>;` has existed since 4.35n (evidence pack's own dating, via `sendHey()`,
  `loop_functions.cpp:4273`) — unconditional, no version guard in current code.
- The position `/N<nn>` channel is guarded by an explicit, much later version comment at
  **both** ingest sites:
  `src/lora_functions.cpp:644`, `:647`, `:666`:
  `// ab version v4.35p.06.11 kommt das als /N99 mit der Position auch mit`
  — i.e. a sub-patch of 4.35p, strictly newer than the 4.35n floor used for HEY.

**Consequence:** the fleet population that can supply `/N` is a **strict subset** of the
population that can supply `R<NC>;`. The ADR's "~81% >= 4.35n" figure (and the evidence
pack's "15.3% nct=0 / 19% old firmware" framing) implicitly treats the two channels as
fleet-equivalent. They are not — a meaningful slice of the "new enough for HEY" fleet
(everything between 4.35n and 4.35p.06.11) can report NC via HEY but never via position.
This matters for the ADR's own quality-gate discussion (3.1): `known_ratio` is computed
from `mheardNCount[i] > 0` regardless of which channel set it, so the gate itself is fine,
but any argument in the ADR that leans on "the position channel is a second, largely
redundant path" undercounts how much of the fleet the position channel actually reaches.

**Severity:** Medium (data-quality precision, not a correctness bug in the design).

**Rev. 3 correction:** In "Datenqualitaet des NC", split the version floor per channel:
HEY `R<NC>;` >= 4.35n, Position `/N<nn>` >= 4.35p.06.11 (cite `lora_functions.cpp:644,647,666`).
Do not present one blended "~81%" number for "NC-capable" without noting it's a HEY-only
figure; the position-capable subset is smaller and unquantified in the evidence pack.

---

## F2 — `mheardEpoch[]` is a general last-heard timestamp, not an NC-freshness timestamp

**ADR claim:** Konsequenz 1 (adr-nc-importance-backoff.md:209-211) and the `getNetImportance()`
draft (5.2, :710-727) gate contribution on `(mheardEpoch[i] + 60*60) > now`, explicitly to
keep the neighbor-population for Importance in sync with `getMheardCount()`'s 1h window.
The ADR frames this purely as a window-_width_ fix (1h vs. the old 12h draft).

**Code reality:** `mheardEpoch[ipos]` is written unconditionally by `updateMheard()`
(`mheard_functions.cpp:303`) for **every** packet type received directly from that
neighbor (text, ACK, ping/pong, position, HEY — anything reaching the shared processing
path around `lora_functions.cpp:687`), not specifically when `mheardNCount[]` changes.
`mheardNCount[]` itself is only touched by the two NC channels (F1) — which, per the
evidence pack's own trickle numbers ("1 own HEY per 57-170 min" steady state,
docs/hey-supp.md), can go stale for **longer** than the 1h active-window.

**Consequence:** a chatty-but-rarely-HEYing neighbor stays "active" (epoch refreshed by
routine traffic every few minutes) for the full 1h window while its `mheardNCount[]` value
can be up to ~170 minutes old — older than the window that is supposed to bound it. The
"one time window for both values" fix (Konsequenz 1) synchronizes `getMheardCount()`'s
population window with the _activity_ gate, but does not actually bound the _staleness of
the NC value itself_, because the two are tracked by the same timestamp for unrelated
reasons.

**Severity:** Medium — doesn't break the conservative-fallback safety property (a stale
positive NC still gets used as `1/NC`, same as a fresh one; no crash/negative risk), but
weakens the "Zeitfenster-Konsistenz" argument the ADR uses to justify Konsequenz 1 as a
complete fix.

**Rev. 3 correction:** Note explicitly that `mheardEpoch[]` bounds _neighbor activity_, not
_NC-value freshness_; the two coincide only as often as the neighbor's own HEY/position
cadence (Trickle-dependent, up to ~170 min per hey-supp.md). If this matters for Stufe 1's
correctness claims, it needs its own field measurement, not just window alignment.

---

## F3 — Relay 3-value signal-report segments are feed-only, confirmed by parser structure (verifies ADR, not a contradiction)

**Task question:** does the RELAY `mheard,rssi,snr` segment (appended per hop) update
anything on receivers, or is it feed-only?

**Code reality:** `appendHeySignalReport()` (`src/aprs_functions.cpp:1124-1131`) is a
**write-only** helper — called from the gateway-upload path and the mesh-relay path
(`lora_functions.cpp:1186-1191`, `:1260-1261`) to append the _relaying_ node's own
`getMheardCount()`/rssi/snr onto the outgoing `@` payload before re-transmission/upload.
On ingest, `updateHeyPath()` only ever looks at the text **up to the first `;`**
(`mheard_functions.cpp:416-420`, `ipos = payload.indexOf(";")`), which is always the
_origin's_ own `R<NC>` segment. Every segment appended by intermediate relays afterward is
carried through in `mheardPathBuffer1[]`/re-transmitted verbatim but is never re-parsed
into `mheardNCount[]` for any of those relaying stations.

**Consequence:** confirms the ADR's Begriffe/Datenqualitaet channel enumeration (only
Position `/N` and HEY `R<NC>;` feed `mheardNCount[]`) is complete and correct — the
38,767 relay-segment records in the evidence pack (deep_analysis.txt) are purely a
feed/server-side artifact (used by mcmap for RF path reconstruction), not a firmware-side
NC source today. Worth stating explicitly in Kap. 8, since a reader could otherwise assume
the "richer" per-hop segment data is already flowing into `mheardNCount[]` and wonder why
v2 needs the path table instead.

**Severity:** Low (documentation clarity only — code matches ADR).

**Rev. 3 correction:** Add one sentence to the Begriffe or 8.1 section: "Die per-Hop
Signal-Report-Segmente (`NCT,RSSI,SNR;`) werden von `updateHeyPath()` nicht geparst
(`mheard_functions.cpp:416-420` liest nur bis zum ersten `;`) — sie sind reine
Feed-/Server-Daten, keine Firmware-NC-Quelle."

---

## F4 — "R0;" is real and reachable; the ADR's own §3 claim that it "does not exist" is wrong (though the chosen fallback is still correct)

**ADR claim (adr-nc-importance-backoff.md:399-401):**

> "Wir koennen nicht unterscheiden zwischen 'NC_reported=0 weil unbekannt' und einem
> hypothetischen 'NC_reported=0 weil keine Nachbarn' (**der Fall existiert nicht** — wenn
> wir ihn hoeren, hat er mindestens NC_self=1)."

**Code reality:** `sendHey()` (`loop_functions.cpp:4257-4273`) has exactly one guard —
`if(node_call[0] != 0x00 && node_pingtime > 0) return;` (a not-yet-configured / anti-spam
check) — and otherwise unconditionally sends
`aprsmsg.msg_payload = "R" + String(getMheardCount()) + ";";`
There is **no** guard requiring `getMheardCount() > 0`. A node fresh off boot, or one that
for whatever reason (asymmetric link, RF-quiet location) genuinely hears nobody, sends a
literal `"R0;"`. `updateHeyPath()` parses this with no floor (`mheard_functions.cpp:445-446`,
no `if(value>0)` check before the assignment, unlike the position channel which is
explicitly gated `if(aprspos.ncnt > 0)` at `lora_functions.cpp:645`/`:667`). So a receiving
neighbor's `mheardNCount[i]` genuinely gets set to `0` for a genuinely-isolated (or
just-booted) neighbor — the case the ADR calls impossible does happen, and the code treats
it asymmetrically across the two channels (HEY can write a real 0; position never can,
since the writer at `loop_functions.cpp:3757` omits `/N` entirely when `incnt==0`, and the
parser at `aprs_functions.cpp:915` additionally requires the first digit after `N` to be
`1-9`, structurally rejecting `/N0`).

**Does this break the design?** No — by luck/design, treating a genuine `R0` the same as
"unknown" (contribution `1.0`) is still the _correct_ conservative choice (an isolated
neighbor really is maximally dependent on whoever can hear it). The design decision in Kap.
3 survives. But the **justification text** is factually wrong, and a future implementer
using the "impossible case" framing to simplify the code (e.g. treating `mheardNCount[i]==0`
as unconditionally safe to backfill from something else) could introduce a real bug.

**Severity:** Medium — wrong claim of fact backing a currently-correct decision; documentation/
correctness-audit risk, not a live bug.

**Rev. 3 correction:** Replace adr-nc-importance-backoff.md:399-401 wording. Correct
framing: "Ein Nachbar kann `R0;` senden (z.B. direkt nach dem eigenen Boot, `sendHey()`
hat keine `getMheardCount()>0`-Schranke, `loop_functions.cpp:4257`) — der Fall existiert
also sehr wohl. Wir behandeln ihn bewusst identisch zu 'unbekannt' (Beitrag 1.0), weil ein
echt isolierter Nachbar ebenfalls maximal von uns abhaengt — die Konsequenz aus Kap. 3
bleibt richtig, nur die Begruendung 'der Fall existiert nicht' ist falsch." Also note the
HEY-vs-position asymmetry (HEY can write 0, position structurally cannot — parser requires
`/N` first digit `1-9`, `aprs_functions.cpp:915`; writer omits `/N` at `incnt==0`,
`loop_functions.cpp:3756-3759`).

---

## F5 — Direct-neighbor HEY ingest is self-sufficient in a single packet pass (clarifies, does not contradict, ADR line ~199)

**Task question:** trace the ingest path for a direct neighbor's NC end-to-end; does the
"originator already in `mheardCalls[]`" precondition (ADR line 199, 8.2) actually gate
direct neighbors?

**Code reality, traced through `lora_functions.cpp` (region ~552-701):**

1. `mheardLine.mh_callsign = aprsmsg.msg_source_last` (`:568`) — keyed by **last hop**.
2. `mheardLine.mh_sourcecallsign = aprsmsg.msg_source_call` (`:570`) — the **origin**.
3. `updateMheard(mheardLine, ...)` runs unconditionally at `:687`, **before** anything
   HEY-specific — this upserts `mheardCalls[]`/`mheardEpoch[]` keyed by `mh_callsign`
   (= last hop) for every packet.
4. Only then, if `payload_type == '@'` (`:692`), `updateHeyPath(mheardLine)` runs
   (`:698`), which searches `mheardCalls[]` for `mh_sourcecallsign` (the origin,
   `mheard_functions.cpp:398`).

For a **direct** neighbor's own HEY (0 hops): `msg_source_call == msg_source_last`, so
step 3 just upserted the exact slot step 4 searches for, in the same processing pass. The
"originator already in `mheardCalls[]`" precondition is therefore **trivially and always
satisfied for direct neighbors** — it is not a real gate for the primary use case the
Importance formula depends on (immediate neighbors). It only becomes a genuine, non-trivial
gate for **relayed** HEYs, where `updateMheard()` (step 3) upserts the _relay's_ slot, not
the origin's — the origin's NC only lands if the origin is _separately_ already a direct
neighbor via some other reception. Same reasoning applies to the position `/N` channel's
"else" branch (`lora_functions.cpp:664-682`), which explicitly searches `mheardCalls[]` for
`msg_source_call` when it differs from `msg_source_last`.

**Consequence:** the ADR's phrasing ("nur wenn der HEY-Originator bereits in `mheardCalls[]`
steht") is technically accurate but reads like a real limitation/precondition when, for the
formula's actual input population (1-hop neighbors, per `getNetImportance()`'s loop over
`mheardCalls[]`), it is unconditionally true by construction. This is worth stating
explicitly since it's the load-bearing correctness argument for Stufe 1: every direct
neighbor that ever sends its own HEY or position-with-`/N` **will** get a usable
`mheardNCount[]` entry, deterministically, in one packet.

**Severity:** Low/informational — strengthens rather than weakens the ADR, but the current
wording underclaims the guarantee.

**Rev. 3 correction:** Add to the Kanal 2 row or 8.2: "Fuer Direktnachbarn ist diese
Bedingung immer erfuellt, weil `updateMheard()` (`lora_functions.cpp:687`) den
`mheardCalls[]`-Eintrag noch im selben Verarbeitungsdurchlauf VOR `updateHeyPath()`
(`:698`) anlegt — ein einzelnes direktes HEY reicht aus. Die Bedingung ist nur fuer
relayte HEYs eine echte Einschraenkung (der Origin muss unabhaengig davon bereits als
Direktnachbar bekannt sein)."

---

## F6 — SHORTPATH corrupts the feed-based evidence itself, not just v2a (gap the ADR doesn't state)

**ADR claim (8.7):** "SHORTPATH zerstoert v2a" — correctly describes that a SHORTPATH relay
collapses the path to `Origin,letztesRelay`, breaking the Zwischenhop-verification v2a
needs, with fallback via 8.6.

**Code reality:** `bSHORTPATH` default `false` (`loop_functions.cpp:166`), toggled true via
`--shortpath` per node (`command_functions.cpp:568,581`). When true, the relay overwrites
`aprsmsg.msg_source_path` to just `Origin,MyCall` (`lora_functions.cpp:1246-1249`),
discarding every earlier hop from the wire path entirely — this happens on the **wire**,
not just in this node's local table. Any downstream node, gateway, or the mcmap feed
harvester sees a path that looks exactly like a genuine 1-hop relay.

**Consequence the ADR doesn't state:** this doesn't just break the _future_ v2a feature —
it silently pollutes the **evidence pack's own path-length distribution and importance
simulation** used throughout this ADR review (evidence-pack.md:49, "Path lengths: 1:63.8%
2:19.8% 3:9.8% 4:4.5% 5:2.1% 6: 9 frames" and the importance-simulation graph built from
"consecutive path pairs = hears"). A SHORTPATH-relayed message is indistinguishable in the
feed from a true 1-hop transmission — there's no way to tell fraction-SHORTPATH from the
feed alone. This means: (a) the path-length histogram likely undercounts true hop depth by
an unknown amount, (b) the importance-simulation's inferred "hears" edges can misattribute
a multi-hop relationship as a direct link whenever the relay used `--shortpath`. Neither the
ADR nor the evidence pack flags this as a source of uncertainty in the _current_ empirical
grounding (BergLog + feed data) — it's only discussed as a future problem for v2a.

**Severity:** Medium — doesn't invalidate the ADR's core decision (Stufe 1 doesn't depend on
path-table data at all), but weakens confidence in the feed-based numbers used to validate
Stufe 1's slot mapping (Kap 4.3, 4.7) and IMP_CAP choice, to an unquantified degree.

**Rev. 3 correction:** Add to 8.7 or the evidence-quality caveats: "SHORTPATH ist auf dem
Draht nicht von einem echten 1-Hop-Pfad unterscheidbar — betrifft nicht nur v2a, sondern
auch die Pfadlaengen-Verteilung und Importance-Simulation, die in diesem ADR als
Feld-Evidenz fuer Stufe 1 verwendet werden. Anteil SHORTPATH-Knoten im Feed ist unbekannt."

---

## F7 — Path length 6 in the feed is explained, not a hop-budget violation (verification, not a finding against the ADR)

**Task question:** HEY hop budget — does a 6-element path fit, or indicate a violation?

**Code reality:** HEY reuses the TEXT hop budget (`initAPRS()`, `aprs_functions.cpp:99-100`:
`if(msgType==':' || msgType=='@') aprsmsg.max_hop = meshcom_settings.max_hop_text;`),
default `MAX_HOP_TEXT_DEFAULT = 4` (`configuration_global.h:209`). Each relay decrements
`max_hop` and only relays while `max_hop > 0` after decrement (`lora_functions.cpp:~3768`,
`aprsmsg.max_hop--;` guarded by `if(aprsmsg.max_hop > 0)`), so at most **4** mesh relay hops
occur → at most 5 path elements (Origin + 4 relays) on the wire/mesh side. The evidence
pack's own documented feed semantics (evidence-pack.md:26) state the **final** 2-value
segment in a HEY's `rssi` string is "the reporting gateway's own reception, server-appended
from UDP DATA header" — i.e. one more element added by the server, not the mesh. 4
mesh-hops + 1 server-appended gateway-reception segment = 6, matching the observed 9 frames
with path length 6. This is consistent, not a hop-budget violation.

**Severity:** N/A (verification only).

**Rev. 3 correction:** none needed on the ADR itself; worth a one-line footnote in the
evidence pack so this doesn't get mis-read as an H4 violation later: "Pfadlaenge 6 = 4
Mesh-Hops (H4-Budget, `configuration_global.h:209`) + 1 server-seitig angehaengtes
Gateway-Empfangssegment — kein Hop-Budget-Verstoss."

---

## Verified correct (no ADR change needed)

- **Position `/N` cap at 99**: writer omits `/N` at `incnt==0`, caps at 99 otherwise
  (`loop_functions.cpp:3756-3759`, exact citation match for ADR's `:3753-3759`). Cap is
  **unreachable in practice** — `MAX_MHEARD` ceiling on every board is <=80
  (`configuration_global.h:172ff`), so `/N` can never actually hit 99. ADR's "unkritisch,
  weil 1/99 gegen null geht" (Konsequenz 3) is correct and, if anything, understates it —
  the cap never fires at all today.
- **HEY R<NC> has no explicit cap** (`sendHey()`, `loop_functions.cpp:4272`) — none needed,
  same `MAX_MHEARD<=80` ceiling applies implicitly. No discrepancy between channels'
  effective max in practice. Evidence pack's max observed nct=36 is consistent with a
  non-saturated S3/nRF52/RAK board (cap 80) or a saturated ESP32-classic board (cap 30,
  but 36>30 rules that specific node out as saturated at exactly 30).
- **Old-format 2-value segment (`R99,99;`) is explicitly excluded** from NC parsing
  (`mheard_functions.cpp:437`, `icomma==1` is neither `0` nor `2`, falls through — no
  value assigned). The code's own comment documents this as intentional
  ("old R99,99;.... kein NCount"). No mis-parse risk from old-format ambiguity; the
  3-comma old variant (`R99,99,99;`) is deliberately treated as still-valid NC via its
  leading number.
- **updateHeyPath() citations**: function span `mheard_functions.cpp:382-553` (ADR 8.2)
  matches exactly; NC-parse block `:420-446` (ADR line 199) matches exactly; `ips<=0
return` at `:525-526` matches; path-table/NC-table non-overlap claim (Direktnachbarn in
  `mheardCalls[]`, Mehr-Hop in `mheardPathCalls[]`) is accurate — the NC-parse block (lines
  394-462) and the path-table-only `ips<=0` gate (lines 464-526) are genuinely separate
  code regions with separate acceptance criteria, correctly distinguished by the ADR.
- **`bSHORTPATH` default/toggle citations** (`loop_functions.cpp:166`,
  `command_functions.cpp:581`) match exactly.
- **CSMA base/slot constants** (`CSMA_SLOT_SIZE=35` at `:221`, `CSMA_PRIO_BASE_3=4500` at
  `:268`, `CSMA_PRIO_SLOTS_3=10` at `:275`) match the ADR's tables (Kontext, 4.7a).
- **`getMheardCount()` 1h window** (`mheard_functions.cpp:556-572`) matches ADR's "1 h"
  claims throughout.
