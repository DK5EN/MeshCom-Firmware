# Verifier V3: finder G, candidates G1 to G4 (paper 4.3/4.4)

Code base: feature-neighbour-matrix at 082c2412 (paper cites d9fc5c5a; nbrNoteFrame unchanged between them).
Field data: `combined_window.log` (DK5EN-98, 2026-09-23 00:12 to 2026-09-24 08:46, 32.5 h), `MH-LoRa` lines
(printed for every frame before dedup, same point where nbrNoteFrame runs). Scripts: `v3_hz.py`, `v3_hz12.py`,
`v3_g3.py` in this directory.

| ID  | Verdict                        | Severity after check                                                                |
| --- | ------------------------------ | ----------------------------------------------------------------------------------- |
| G1  | PARTLY                         | low (latent, documented residual risk, 0 field cases)                               |
| G2  | PARTLY                         | low (one overstated sentence, missing rollout item; the "contradiction" is refuted) |
| G3  | CONFIRMED, broader than stated | medium-low (display only, but common)                                               |
| G4  | CONFIRMED, broader than stated | medium (path view duplicates, horizon sizing, entry row = own row)                  |

## G1: server-injected POS/HEY seeding the horizon — PARTLY

Path, established in code:

- The injecting gateway does NOT feed its own matrix. `src/esp32/udp_frame_esp32.cpp:132-400` (and the mirror in
  `src/nrf52/udp_frame_nrf52.cpp:132-393`) accepts `:`, `!`, `@` from UDP, appends its own call to the source path
  (`:242-243`, nRF52 `:182`), sets `msg_server`, and radiates via `addTxRingEntry` when the msg_id is new and not own
  (`!` vetoed only by `bGATEWAY_NOPOS`). No `nbrNote*` call in either UDP file.
- Its RF neighbours receive it through `OnRxDone` and call `nbrNoteFrame()` (`src/lora_functions.cpp:984-1012`) like
  any RF frame; the hook runs before dedup, so even a duplicate of a frame they already heard feeds it.
- The uploading gateway sends the frame as received (`addNodeData(RcvBuffer…)`, `lora_functions.cpp:1729-1755`, no
  own call appended). The radiated path is therefore `S,…,R_last,GW_inject`: every pair is real RF except
  `(R_last, GW_inject)`, which is an IP hop. Under the 4.4 rule the entry row would be `R_last`, a station at the far
  site, and the hop count would be the far-site hop count plus one.

Why only PARTLY:

- Not new with the horizon. Today's 2-hop window already creates a row for `R_last` and a fake edge
  `R_last -> GW_inject` for any server `!`/`@`; `src/nbr_matrix.h:229-236` and `docs/nbr-stage2-campaign.md:93`
  record this as accepted residual risk. The paper's 4.4 rule copies the code's premise exactly.
- Zero occurrences measured: 511 server-to-gateway frames in 34 h and 238 in 11 h, all Text
  (`nbr_matrix.h:226-228`, `nbr-stage2-campaign.md:86`).
- What survives: the paper does not repeat the residual risk, section 5 has no row for it, and the horizon widens
  the exposure a little. Today's path table reads HEY only; the horizon also reads POS. "HIGH" is overstated.

## G2: "ME step already runs on every frame" and the section 5 contradiction — PARTLY

Confirmed part:

- `nbrNoteFrame()` (`src/nbr_matrix.cpp:519-535`) returns on DROP TOK (any invalid token anywhere in the path, or more
  than 8 tokens) and on DROP LOOP before the ME hit (`:672-682`). The 4.3 lead sentence ("läuft heute schon bei jedem
  Rahmen … genau der Schlüssel von MHeard") is therefore false for those frames. The 4.3 rule is a code change, but
  neither step 2 of the Rollout nor the `native_nbr_views` acceptance names it.
- Second parity gap, not in the finder: HN reports reach `nbrNoteFrame()` including the ME step
  (`lora_functions.cpp:773-795`), yet MHeard skips them (early return; paper line 53 says the same). The topology's
  MHeard would list a neighbour heard only through an HN report.
- Field frequency: 0 DROP TOK and 2 DROP LOOP in 32.5 h. Both loops ended in DB0ED-99, a direct neighbour.

Refuted part:

- The 4.3 rule sits under "Regeln" and is worded as target behaviour ("verliert wie heute seine Kanten, zählt aber
  als Direktempfang"). The "aber" marks the change. It is not a claim about today.
- Section 5 ("Rufzeichen außerhalb der Zeichenregel … Station fehlt in MHeard") does not contradict it. Under 4.3 the
  ME step still checks the last-hop token, so a station whose own call breaks the rule gets no row. 4.1 line 120 says
  the same. The case exists: `checkRegexCall()` (`src/regex_functions.cpp:9`, the `decodeAPRS` gate for source and
  last hop) allows an unbounded digit run, so a 10-character call such as `OE12ABC-12` passes decode but fails
  `nbrValidToken` (3 to 9 characters, `nbr_matrix.cpp:304`).
- A missing DROP LOOP row is consistent with the design: under 4.3 a loop costs only edges, as today, and nothing in
  MHeard.

## G3: stale minimum hop count — CONFIRMED, root is wider than eviction

- The layout allows exactly one hop count per sender. The entry is 19 or 27 B = 8 B call + 8 or 16 B mask + 3 B meta,
  and the meta holds last_min, hop and the G bit. It stores no minute for the minimum and no hop count per entry row.
- (a) Per-entry-row misreport, even without eviction. The path construction says "Hop-Zahl bis zur Eintrittszeile A
  aus hzEntry(S)", but one minimum is shared by every bit in the mask. Field: 28 of 49 truly far senders arrive
  through entry tokens at different minimum hop counts. Example: DL2RN-13 comes in at 3 via DB0HOB-12 and at 4 via
  DB0ED-99, so the path view would print 3 on both. The finder's eviction case is one instance of this: once the
  entry row that set the minimum is evicted, the number belongs to no remaining way.
- (b) Time staleness. "kleinste im Fenster gesehene" cannot be built this way. last_min moves with every sighting, so
  the minimum can never rise while S keeps being heard. Today's path table does bound it: `updateHeyPath()` returns
  without touching the timestamp when a longer path arrives (`src/mheard_functions.cpp:620-624`) and prunes 12 h
  after the shortest was last written (`:583-586`). Field: 0 cases in 32.5 h; the minimum was seen again within 12 h
  for every far sender. The flaw is real in principle, rare in practice.

## G4: horizon entry coexisting with a row for the same call — CONFIRMED, larger than stated

- The 4.4 rule builds the entry from the frame ("POS/HEY mit mindestens drei Pfad-Token") and never asks whether the
  sender already has a row. The heading means senders at 3 or more hops; the rule does not implement that.
- Field, DK5EN-98, three 12-h windows: 68 to 73 senders would get horizon entries. 26 to 28 of them are direct
  (all 5 direct neighbours) or 2-hop senders, heard directly and also through two relays. Far-only is 42 to 45 per
  12 h.
- Consequences:
  1. The path view shows a direct neighbour a second time as "3 Hops über X".
  2. The sizing premise (22 of 29 from a 2.5-h path-table sample) undercounts. On classic ESP32, 40 slots are already
     short for far-only senders at this node, and near senders take another ~27.
  3. Not in the finder: 987 of 3,832 horizon-creating frames (26 %) are echoes of my own relays
     (`…,DK5EN-98,X`). The rule "Eintrittszeile ist das vorletzte Token" then yields row 0, and the path construction
     "B aus heardBy(A) ∩ Direkt, dann ich" produces a way that loops back through me.
- Nothing in 4.4, section 5 or the Rollout covers this. Step 3 of the Rollout tests only "Zeile oder Horizont", not
  "not both".
- Fix direction: create or refresh a horizon entry only when S has no fresh row and the entry token is not the own
  call; free the entry when S gets a row.
