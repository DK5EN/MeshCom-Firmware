# Verifier V1b: paper section 4.8 (Auto-Via, Abb. 6), finders I and B

The inputs were read only. Scripts and data are in `scratchpad/v1b/` (`echo.py`, `b3.py`, `fixture.py`,
`raw.log` = rpizero `2026-09-23.log` + `2026-09-24.log` as fetched via ssh cat, `rfc3626.txt`).
Code references: upstream/dev 472304e8 and tag v4.35t (6edc7499), plus fork HEAD 082c2412.

Baseline reproduced: the window 2026-09-23 00:12 to 2026-09-24 08:46 has 65 own POS frames. 56 have an echo and 9 have none.
First-hand / second-hand-only counts: DK5EN-1 44/0, DB0ED-99 25/1, DL2UD-1 18/1, DL2JA-2 12/13. These match the paper exactly.
No neighbour ever appears first-hand and second-hand for the same msg_id (0 of 65).

Firmware of the neighbours, read from the FW field of frames they originated: DL2JA-2 35:t (463 frames, HW:43),
DB0ED-99 35:t, DK5EN-1 35:t, DL2JA-1 35:t, DL2UD-1 35:p. The super node therefore runs 4.35t, which is post-June code.

## Task 1: feeder rule on the fixture (finder I C1): CONFIRMED

The fixture is Anhang B of Kantenpool: 67 edges, and the cell (X,Y) means "Y hat X gehört". The semantics check out:
#N of every row equals the number of its column entries, and row 1 #N = 5 equals D.

- D60 = {2,3,4,5,11}. HM (edge 1→F) = {2,3,4,5}. That gives **K = {DL2JA-2, DK5EN-1, DL2UD-1, DB0ED-99}**.
  DL2JA-1 is not in K because no edge 1,11 exists. Finder I included it; this makes no difference to the result.
- The rule is "m hört F, meiste Treffer auf Kante (F, m)". For m = DL2JA-2 that is edge (F,2):
  **DB0ED-99 66**, DL2UD-1 15, DK5EN-1 0. DB0ED-99 is not weak (25/1, 4 %). **The rule selects DB0ED-99, not DL2UD-1.**
- The paper's "DL2JA-2 hat ihn 159-mal gehört" is edge 2,4,159, which means DL2UD-1 heard DL2JA-2. That is the reversed
  direction. Even with the reversed direction the margin over DB0ED-99 (2,5 = 138) is small.
- Field-log cross-check: DL2JA-2's 13 second-hand copies came via **DB0ED-99 7** and via **DL2UD-1 6**.
  The log therefore does not support DL2UD-1 as the feeder either.
- The Abb. 6 SVG draws "DL2JA-2,DL2UD-1,*", "DL2JA-2 hoert ihn" and "DB0ED-99 nicht genannt". The figure,
  the caption and the via string all need to change.

## Task 2: does "nur zweite Hand" imply that the direct copy was missed? (finder I C2.1-C2.3): REFUTED. The paper's inference holds.

The code that DL2JA-2 runs (upstream 4.35t; identical at tag v4.35t and upstream/dev):

- `lora_functions.cpp:654` `bool rx_is_new = is_new_packet(RcvBuffer+1);` This is a pure lookup in
  `ringBufferLoraRX` (`dedup_functions.cpp:23-52`).
- `:897` `else if(setlogCountDedup(rx_is_new))` is the only way into the relay block, so a duplicate never reaches it.
- `:962` `addLoraRxBuffer(aprsmsg.msg_id, aprsmsg.msg_server);` records the id **before** the relay decision at
  `:1429 if(!checkMesh(aprsmsg))`. The first decoded copy is therefore consumed whether or not it is relayed.
- `:1477` resets the path and appends the node's own call, both built from the **first** copy. At `:1523`
  `addTxRingEntry(RcvBuffer, size, RING_STATUS_DONE, "rx_relay", 0, true)` copies the finished frame into the slot
  (`txring_functions.cpp:442ff`, memcpy at enqueue time, no merge by msg_id). doTX sends it raw (`:1767 memcpy(lora_tx_buffer, ringBuffer[txSlot] + 2, …)`).
- There is no overhear-cancel for relays. The only release loop runs over slots with status `!= RING_STATUS_DONE && != RING_STATUS_READY`
  (`:596`), and relay slots carry DONE. Finder I read memory note "Overhear-Cancel überspringt Relays" backwards:
  relays are _excluded_ from the cancel. Even a cancel would not re-arm the node from the overheard copy, because that copy is a duplicate (see :897).

Alternatives the finders raised, and why none of them produces a false second-hand echo:

- TX-ring drop (`"full"`) or stale drop: the id is already recorded, so the result is no relay at all. It is not a second-hand relay.
- Hop limit: a copy that arrives first at H00 is recorded and not relayed. This also produces no relay.
- CSMA re-arm or backoff: only the send time moves. The frame content was fixed at enqueue.
- Server injection (DL2JA-2 is a gateway): it appends the node's own call to the path as uploaded ("DK5EN-98") and so can only
  look first-hand. Both observed feeders, DL2UD-1 and DB0ED-99, are G=N, which makes the 13 second-hand echoes pure RF relays.
- Dedup-ring eviction: HW:43 corresponds to 100 slots. DK5EN-98 sees about 150 distinct ids per hour. Evicting the id between my TX
  and the feeder copy (≤ 519 s) is negligible.
- Hop fields agree: DL2JA-2's first-hand relays are H01 (11), its second-hand relays H00 (13), and it never transmits twice per msg_id.

Remaining nuance, not a defect: "selbst nicht gehört" also covers "was transmitting itself" (half duplex on a busy
super node). "Mindestens 13 von 65" remains a valid lower bound. "Verschluckt" is only strictly true for the 7
DB0ED-99-fed cases. For the 6 DL2UD-1-fed cases it depends on whether DL2UD-1 (4.35p) relays when it is not named (finder B-1).

C2.4 (the 86 % proxy is one-sided): **CONFIRMED**. The numbers are under task 3.

## Task 3: B-3 recomputed: CONFIRMED (every number reproduces)

The window is measured from my TX. Named set as in the paper: {DL2JA-2, DL2UD-1}.

| Predicate                                        | no window  | ≤ 300 s    | miss pairs (of 64), ≤ 300 s | longest miss run |
| ------------------------------------------------ | ---------- | ---------- | --------------------------- | ---------------- |
| any echo (paper's base)                          | 56/65 86 % | 52/65 80 % | 2 (3 %)                     | 2                |
| named node anywhere in path                      | 34/65 52 % | 28/65 43 % | 22 (34 %)                   | 9                |
| path after me only named nodes (survives via)    | 28/65 43 % | 24/65 37 % | **27 (42 %)**               | 12               |
| rule-correct set {DL2JA-2, DB0ED-99}, only-named | 29/65 45 % | 24/65 37 % | 28 (44 %)                   | 16               |

- DL2JA-2 appears on 26 echo lines. The 300 s window cuts 7 of them (332, 351, 355, 411, 464, 489, 519 s), which is 6 of 25 frames.
  Its relay delay from my TX has a median of 122 s (first-hand) and 125 s (all), so 300 s is 2.5x the median, not "fast das Dreifache",
  and 24 % of its frames arrive late.
- The paper's arithmetic is right for its own base: (1-0.862)^2 = 1.9 %. The base is wrong. It includes DK5EN-1's 44 first-hand echoes
  (6 dB link), which disappear by design.
- **Base rate the check should use:** P(a relay by a named node is heard within 300 s), measured in the flood on paths that survive the via:
  about 37 % (up to 43 % if any named appearance counts). Assuming independence, two misses in a row give about 32-40 %; observed is 34-42 %.
  To reach 2 % with p ≈ 0.4 the rule would need about 7-9 consecutive misses (3.5-4.5 h at 30.5 min per frame), and even then
  the observed runs of 9-12 would trip it. Via mode may raise p (fewer collisions, no H00 pre-emption), but not from about 40 % to 86 %:
  DK5EN-98 hears DL2JA-2 at SNR -7..-14. The Rollout acceptance of "≥ 86 %" is unattainable for the named set.

## Task 4: medium items

- **Coverage uses reverse edges (B-6): CONFIRMED.** The paper defines `hears(x)` as the stations x heard (NB = popcount hears(x)) and builds
  N2 and Pflicht from `hears(m)`. Delivery needs "x hears m". In the fixture only 4 of the 15 N2 stations (7, 10, 15, 18) show any
  evidence of hearing a K member, and only 1 of DL2JA-2's 8 exclusive stations does (18, edge 2,18 = 1). This is a silent
  symmetry assumption. It contradicts "Auto-Via nutzt nur Beweise" and the paper's reason for rejecting OLSR.
- **max_hop_pos 2 means the feeder path loses N3 (B-7): CONFIRMED as an unstated cost.** The value is forced at boot
  (`esp32_main.cpp:1044`, `nrf52_main.cpp:660`, `MAX_HOP_POS_DEFAULT 2`, and the log shows H02). The relay guard is `:1448`/`:1454`.
  The path me(H2)→F(H1)→S(H0) ends at N2. In the data, S relayed at H00 in 13 of 25 frames. The N2 criterion ("keine bekannte Station")
  remains literally true.
- **Detection takes about 60 min, not 600 s; one named echo masks a failing member (B-8): CONFIRMED.**
  Own POS interval: median 30.5 min (range 7.4-59 min). SPERRE therefore fires about 35 min after the first lost frame, or up to
  about 65 min after the failure. The "600 s" is only the sum of two echo windows. Relays reuse the via without any echo check.
  The masking follows from the rule text ("durch einen genannten Knoten"). In Abb. 6 the feeder covers none of N2 (only DK5EN-98 and
  DL2JA-2 ever heard DL2UD-1), so a silent S with a live F loses all 15 stations while the check passes.
  Mitigation that the paper does not mention: D60 drops a dead S within 60 min. This does not cover an S that is alive but not relaying.
- **OLSR duplicate-set rule missing, first copy decides (B-5): REFUTED.** RFC 3626 §3.4.1 step 2 re-considers a duplicate only if
  `D_retransmitted is false, AND the (address of the) interface which received the message is not included among the addresses in D_iface_list`.
  On a single-interface node (one LoRa radio) the second copy arrives on an interface that is already listed, so OLSR is
  first-copy-wins too. The MeshCom ordering finder B cites is correct (`:962` before `:1429`) but is not a departure from OLSR.
  The scenario itself needs an N2 station that hears DL2UD-1, and the fixture has none.
- **A v5 feeder may not name the super node (B-4/B-8 item): CONFIRMED as a design gap and not triggered in Abb. 6 today.**
  Under the paper's own rules a relay resets the path and applies its own via, S relays only when named, and the id is recorded
  before that check. A v5 F whose set omits S therefore defeats the feeder. DL2UD-1 is 4.35p today. Its fixture K would be
  {DK5EN-98, DL2JA-2}, so even as v5 it would probably name DL2JA-2. For the rule-correct feeder DB0ED-99 this is unknown.

## Verdict summary

| Claim                                 | Verdict                              |
| ------------------------------------- | ------------------------------------ |
| I-C1 feeder uses reversed edge        | CONFIRMED (rule → DB0ED-99)          |
| I-C2.1-3 second hand ≠ missed direct  | REFUTED (code proves it)             |
| I-C2.4 / B-3 86 % base, 2 %, 300 s    | CONFIRMED                            |
| B-6 reverse-direction coverage        | CONFIRMED                            |
| B-7 N3 loss via feeder (H0)           | CONFIRMED (unstated cost)            |
| B-8 ~35-65 min detection, masking     | CONFIRMED                            |
| B-5 OLSR duplicate-set analogy broken | REFUTED                              |
| v5 feeder may not name S              | CONFIRMED (gap), not in Abb. 6 today |
