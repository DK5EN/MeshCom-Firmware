# Finder B: Auto-Via (4.8, Abb. 6/7, Fehlerszenarien/Rollout rows)

Repo `feature-neighbour-matrix` @ 082c2412, upstream/dev @ 472304e8. Field capture:
rpizero `~/meshlog/dk5en-98/2026-09-2[34].log` (read-only). Fleet: mcmap `nodes_query` (read-only;
`fleet_firmware` was refused by the permission classifier, not retried).

Paper's measurement reproduced exactly (no time window): 65 own POS, 56 with echo;
DL2JA-2 12 first / 13 second-only, DL2UD-1 18/1, DB0ED-99 25/1, DK5EN-1 44/0.

---

## B-1 (HIGH) "4.35" is not one behaviour: pre-June-2026 nodes read the FIRST via token as the destination

**Claim (4.8 Prämisse, 2. Ausgangslage "Via beim Empfänger/Relais"):** "Ein Empfänger, dessen
Zielpfad Rufzeichen nennt, wiederholt nur, wenn er selbst genannt ist, und das gilt für 4.35 wie
für den Fork. Jedes Relais setzt den Zielpfad vor dem Senden auf das Ziel zurück …"

**Hole:** Holds only for builds after 2026-06-10. Before that:

- `decodeAPRS` set `msg_destination_call` = FIRST token of the destination path
  (`git show 2c96f11b^:src/aprs_functions.cpp` 320-331, cConcat3 up to first comma). It became
  the LAST token only in 6781171c (2026-06-10, "v4.35p routing, optical").
- `checkMesh()` was `return bMESH;` (no via check), `git show 2c96f11b^:src/via_functions.cpp`;
  "relay only when named" arrived in 2c96f11b (2026-06-09).
- The relay never reset the destination path (`2c96f11b^:src/lora_functions.cpp` 1027-1075): the
  originator's via is carried forward unchanged.
- The relay gate is `strcmp(destination_call, node_call) != 0` (same file :1027), and text display
  is `destination_call == "*" || CheckOwnGroup(destination_call)` (:820), DM handling
  `destination_call == node_call` (:709).

"4.35p" spans April-August 2026, so the version string cannot tell the two apart.

**Proof that the Abb. 6 feeder is such a node:** DL2UD-1 (mcmap: 4.35p) relays via frames that
do NOT name it, and keeps the via:
`DB2GS-1,DK5EN-98,DL2UD-1>F1ZKT-12,*` (2026-09-23 20:04:41, x11B75386) and
`DB2GS-1,DL2JA-2,DL2UD-1>F4AVI-12,20` (09:31:47, x11B75351). A post-June node returns `nomesh`
there and would reset the path to `*`. Other pre-via builds near DK5EN-98: DL2JA-3 4.35d,
DO9EJ-1 4.35e, DG3MNF-4 4.35k (nbcnt 16).

**Failure scenarios for my frame `DK5EN-98>DL2JA-2,DL2UD-1,*`:**

1. If the super node (named FIRST by rule "Super-Node vorn") runs an old build, it sees
   destination_call == own call. It never relays, and it shows my text to `*` as a DM to itself.
   The design fails exactly at the node it relies on.
2. Every old node that receives a `*` text or a group text sent with a via never displays it
   (destination_call = "DL2JA-2"). A DM `DL2JA-2,OE1KBC-7` sent to an old OE1KBC-7 is not
   recognised as addressed to it. A text `:ack` back to an old sender (SendAckMessage
   loop_functions.cpp:5025 runs checkVia) is not matched either.
3. Old relays carry my via further. 4.35p+/fork nodes two or more hops away then refuse to relay,
   because they are not named. The paper's "flutet ab dort" does not happen.

**Severity:** high. Needs a fleet census by build date (not by version letter) and a Fehlerszenario
row. Stufe 4 is only safe where every node within 2 hops is on a build from 2026-06-10 or later.

## B-2 (HIGH) The via leaks network-wide through the server

**Claim (4.8 Regeln):** "Gateways rechnen die Menge wie jeder andere Knoten." Rollout step 6 checks
only that "eigene Positionen erreichen die Gateways laut mcmap".

**Hole:**

- Gateways upload the raw received frame, including the originator's via:
  lora_functions.cpp:1754 `addNodeData(RcvBuffer…)`, which runs before the relay rewrite at :1825.
- Own frames are uploaded after checkVia: loop_functions.cpp 3446 then 4318 (text), 4887 then 4911
  (POS).
- The server hands the destination path back unchanged.
- The injecting gateway calls `checkVia()` WITHOUT first resetting the path to destination_call
  (udp_frame_esp32.cpp:252, udp_frame_nrf52.cpp:197). With bVIA off or no via set, the
  originator's via goes out on LoRa in a foreign region.

**Field proof (today, with fixed-via senders):** `RX-UDP … DB2GS-1>F5JFA-12,*` was injected by
DK5EN-98, DK5EN-1 and DL2JA-2 (x11B75375, x11B75386). No 4.35t node relayed it; only the old
DL2UD-1 did. Ordinary injected frames without a via ARE relayed by DK5EN-1 and DB0ED-99
(x2EFB01BD `…DK5EN-98,DK5EN-1>20`, `…DL2JA-2,DB0ED-99>20`; x92055106).

**Failure scenario:** With Stufe 4, every `*`/group/DM text from a MeshCom-5 node goes out in
every remote region with `DL2JA-2,DL2UD-1,*`. There it reaches only the injecting gateway's direct
neighbours: 4.35p+ nodes do not relay, and old nodes do not display it (B-1). The same applies to
the injecting super node's own copy after it missed the RF original. The rollout acceptance
criterion (uplink to mcmap) cannot see this.

**Severity:** high. Minimum fix in v5: reset the path before checkVia in both udp_frame files.
The 4.35 gateways of the fleet keep leaking regardless.

## B-3 (HIGH) Echo control is built on the wrong base rate; the named set carried only ~half of the frames

**Claim (4.8 Prämissen der Echo-Kontrolle; Rollout 6):**

- "In der Flut kam für 56 von 65 eigenen Positionen ein Echo zurück, 86 % … Zwei Fehlschläge
  hintereinander treffen einen gesunden Via dann in rund 2 %."
- "300 Sekunden lassen dem Altknoten DL2JA-2 … fast das Dreifache."
- Acceptance: "Echo-Quote mit Via mindestens 86 %".

**Hole:** The 86 % counts echoes from ANY neighbour. DK5EN-1 alone gave 44/65, and by design it no
longer relays. The rule counts only echoes from named nodes. Same capture, set {DL2JA-2, DL2UD-1}:

- echo anywhere in the path: 34/65 = 52 % without a window, 28/65 = 43 % within 300 s;
- named-only paths: 24/65;
- two misses in a row: 27 of 64 consecutive pairs (42 %), not 2 %. With any-neighbour echoes it is
  2 pairs.
- The 300 s window cuts 7 of DL2JA-2's 26 echoes (332, 351, 355, 411, 465, 489, 519 s).
  Delay from MY transmission (not "nach der ersten Kopie"): first-hand median 122 s, p90 355 s.
  With the window, any-echo is 52/65, not 56.
- The check cannot tell "S did not relay" from "I did not hear S's relay". DK5EN-98 hears
  DL2JA-2 at RSSI -119..-123, SNR -7..-14 (log lines above). The super-node link is weak in both
  directions.

**Failure scenario:** On the paper's own data, SPERRE (60 min flood) triggers every few frames,
and the node oscillates. "Jede SPERRE einem echten Ausfall zuordenbar" and "≥ 86 %" are
unattainable by construction. Script: `scratchpad/fable/finderB_echo.txt` plus the inline python
in this session.

**Severity:** high (a parameter is wrong and measurable today, and it gates the whole state machine).

## B-4 (MEDIUM-HIGH) Delivery to the super node through the feeder is not guaranteed

**Claim (4.8):** "Zubringer … den der Super-Node gut hört". Fehlerszenario "Super-Node hört mich
schlecht → Zubringer … keine Lücke, solange ein sicherer Nachbar ihn erreicht".

**Hole:** S relays F's copy only if that copy names S, or carries no via at all.

- A MeshCom-5 F resets the path and applies ITS OWN via (lora_functions.cpp:1825-1827, 4.8 "für
  Relais gleichermaßen"). F's super node need not be S.
- The dedup entry is written BEFORE the relay decision (lora_functions.cpp:1302 addLoraRxBuffer vs.
  :1777 checkMesh). So an unnamed first copy locks S out of every later copy.
- In Abb. 6 the feeder works only by accident: DL2UD-1 is pre-June (B-1) and forwards my via
  unchanged. A 4.35p+ F would flood, and a v5 F with its own set might not name S.

**Failure scenario:** S misses the original (13 to 40 of 65 frames in the capture). F (v5) relays
with `F-super,*`, and S drops the copy as unnamed and marks it seen. S's 8 exclusive stations
lose the frame, and the echo check still passes on F's echo (B-8).

**Severity:** medium-high. It grows with the MeshCom-5 share, which is the paper's own goal.

## B-5 (MEDIUM) The OLSR analogy fails at MeshCom's first-copy-wins dedup

**Claim (4.8 Prämisse):** "…macht das, was OLSR als Multipoint Relay beschreibt, RFC 3626 8.3.1".

**Hole:**

- OLSR's forwarding rule (RFC 3626 §3.4.1, duplicate set with D_retransmitted) still forwards a
  message received earlier from a non-selector, once it arrives from an MPR selector.
- MeshCom marks the msg_id seen before the relay decision (lora_functions.cpp:1302). The copy a
  node decodes FIRST decides alone. A node named by the slower relay but not by the faster one
  never relays.

**Failure scenario, already with today's fleet:** DL2UD-1 relays fast (first-hand median 27 s from
my TX) and carries my via. DL2JA-2 relays slowly (median 122 s) and floods. A 4.35p+ station in N2
that hears both decodes DL2UD-1's copy first: it is not named, so it drops the frame and marks it
seen. It then ignores DL2JA-2's flood copy. Onward propagation to N3 around the feeder is lost.
The flood would have relayed the first copy.

**Severity:** medium.

## B-6 (MEDIUM) The coverage test uses edges in the reverse direction

**Claim (4.8):** "N2 = OR hears(m)…; Pflicht = m ist für ein x in N2 der einzige Hörer in K".
Fehlerszenarien: "Asymmetrische Strecke … keine Entscheidung hängt daran, Auto-Via nutzt nur
Beweise". Options row: OLSR rejected because it "setzt symmetrische … Strecken voraus".

**Hole:**

- hears(m) = stations m has heard. The edge comes from path "…,x,m" (4.4: B ∈ heardBy(X) means
  B heard X). Delivery of my frame needs the opposite edge, x hears m.
- Stufe 2 does this correctly: nbrCoverMask uses the hearers of the relayer,
  `cell[row][Y]` = "Y hat row gehoert" (nbr_matrix.h, nbrHearersMask).
- For 2-hop x, "x hears m" is almost never observable: x's relays of m arrive only as 3-token
  paths, and x's HN report has max_hop 0. The coverage test is therefore a silent symmetry
  assumption.

**Failure scenario:** A PA-equipped or hilltop x is heard by the mountain node S, but x cannot hear
S. x counts as covered by S, S is named alone, and x loses frames that a flood relay by another
neighbour would have delivered.

**Severity:** medium.

## B-7 (MEDIUM) Hop budget: the feeder path eats both position hops

**Claim (4.8):** "keine bekannte Station abgeschnitten" (options table). The criterion is N2 only.

**Hole:** `max_hop_pos` is forced to 2 at every boot (esp32_main.cpp:1044, nrf52_main.cpp:660,
configuration_global.h:362).

- me (H2) → F (H1) → S (H0): N2 receives at H0 and nobody relays further.
- In the flood, S's second-hand relays were also at H0, but the first-hand relays of DB0ED-99 and
  DK5EN-1 (H1) carried the frame into their N3.
- Text (max_hop_text 4) reaches N5 in the flood, yet the via criterion guarantees only N2.
- The gateway gwcap (lora_functions.cpp ~1705) does not block F→S (path count 2 < 3). Fine.

**Failure scenario:** In the 20-50 % of frames where S misses the original, stations 3 hops out
lose my position.

**Severity:** medium. The paper should state N3/N5 loss, or require S first-hand for "gültig".

## B-8 (MEDIUM) Echo "by any named node" masks a failing member, and detection takes far longer than stated

**Claim (4.8 Echo-Kontrolle; Fehlerszenario "Via-Knoten fällt aus … bis zu zwei eigene Rahmen …
in bis zu 600 s").**

**Hole:**

- (a) One echo from F satisfies the check while S, which holds the exclusive stations, is silent.
- (b) Own positions come every ~30.5 min (NEW-POS 00:12:34 / 00:43:04 / 01:13:34), so two misses
  take about 60 min, not 600 s. Throughout that time every RELAYED frame also carries the broken
  via, and relays have no echo check at all.
- (c) K has no mesh filter. On the fork a named node with `--mesh off` returns bMESH=false
  (via_functions.cpp:139). HM can come from an HN report of such a node (nbrNoteReport, "self").
  NBR_FLAG_MESH exists but 4.8 does not use it.

**Severity:** medium.

## B-9 (MEDIUM-LOW) Text echoes can be server injections, not RF relays

**Claim (4.8):** "Nach jedem eigenen Text- oder Positionsrahmen … Wiederholung durch einen
genannten Knoten".

**Hole:** DK5EN-98 and DL2JA-2 are both gateways. When S misses my RF text, the server copy
reaches S. S injects it as `DK5EN-98,DL2JA-2` (udp_frame_esp32.cpp:243 appends its own call),
which looks identical to a first-hand relay. The matrix already refuses text edges for exactly
this reason (nbr_matrix.h text comment, commit 8487ea2a). The echo check would count it as a
success although S never heard me.

**Severity:** medium-low. Restrict the echo check to POS, or do not count echoes whose msg_id this
gateway injected.

## B-10 (MEDIUM-LOW) D60 is used as a stand-in for "hears me"

**Claim (4.8):** "N2 = … ohne D60 und ohne mich".

**Hole:** D60 means _I_ heard them. A D60 neighbour without HM (for example DL2JA-1, which "leitet
nie weiter") is dropped from the coverage requirement even if it cannot hear me. With via, it gets
my frame only if it happens to hear a named relay.

**Severity:** medium-low. Use D60 & HM (or & SYM) for the exclusion.

## B-11 (MEDIUM-LOW) The f/s metric cannot express "missed" and is starved by the via decision

**Claim (4.8):** "f und s zählen … letzten 15 eigenen … Rahmen"; "schwach = f+s<4 oder s>10 %";
Abb. 6: DL2UD-1 "hört mich sicher".

**Hole:**

- Frames without any echo count nowhere. DL2UD-1: 18 first-hand echoes out of 65 frames (28 %)
  still counts as "sicher". DL2JA-2: 40 of 65 frames without its echo.
- Two 4-bit counters with halving at 15 have no count of frames sent, so "letzten 15 Rahmen" is
  not implementable. Counters of neighbours that stop relaying freeze.
- s(S) can only be observed while somebody else relays first. Once a feeder is dropped (or a node
  starts without one), S's second-hand events disappear and s only halves toward 0. S then looks
  "strong" for good: a self-confirming decision.

**Severity:** medium-low.

## B-12 (MEDIUM, future) Stufe-2 overheard-cancel assumes flood semantics

**Hole:** The coverage-gated CANCEL (lora_functions.cpp ~889-944) drops my relay when an overheard
copy "covers" my dependents. Under via, receivers of that copy relay only if named in IT, and it
carries the other relay's set. Reception is covered; onward propagation is not. Fall A (alone)
still protects exclusive stations.

**Scenario:** A v5 S with --nbrrelay on cancels after a v5 F's relay. S's N2, which F's via does
not cover, is lost.

**Severity:** medium, once v5 relays run both features. Not discussed in the paper.

## B-13 (LOW) Small points

- HEY goes through checkVia today: sendHey → finalizeAndSendAPRS (loop_functions.cpp:5097 →
  :3446), and relays do too (:1827). The exemption must key on payload_type INSIDE checkVia so that
  relayed HEYs keep "HG". The matrix G bit keys on `destination_path == "HG"`
  (lora_functions.cpp:795, :1011) and already breaks today with any 4.35 fixed node_via relay;
  keying on destination_call would be robust. HN (max_hop 0) is harmless.
- Frame length: a via adds up to 30 B, and encodeAPRS clips the payload at 245 B
  (aprs_functions.cpp ~1088). The largest field frame is 227 B (2 of 14401 at ≥215 B), so long
  texts are rarely truncated.
- A 4.35 relay with --via on and a fixed node_via applies ITS via; it does not "flood ab dort".
- f/s first/second-hand assumes long-path relays; a bSHORTPATH relay (node_sset 0x0400) reports
  second-hand relays as first-hand.

---

## Checks that came out fine

- Post-2026-06-10 upstream and the fork: destination_call = last token, so `*`/group/DM semantics
  survive a via. The relay resets the path and applies its own via (fork lora_functions.cpp:1825-1827;
  upstream 1477-1479).
- The substring match in upstream checkMesh is correctly listed as a Fehlerszenario. Upstream
  named-with-mesh-off relaying vs fork `return bMESH` is correctly stated.
- Own echoes are observable: [LOG] and nbrNoteFrame run before dedup (lora_functions.cpp ~740-1011),
  so an echo table fed from that hook is implementable.
- No ping-pong: the own-call check in checkMesh plus loop detection on the source path. Mutual
  naming of the previous hop is harmless under symmetric links (it already transmitted).
- Binary ACKs (0x41) do not pass checkVia (paper correct). Text `:ack` DMs do (see B-1 item 2).
- gwcap does not block the F→S position path.
- Paper's echo counts reproduced exactly (no time window).
