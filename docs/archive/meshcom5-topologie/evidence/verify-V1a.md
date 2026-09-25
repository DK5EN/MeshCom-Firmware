# Verifier V1a: B-1 and B-2 (4.8 Auto-Via)

Sources: repo @ 082c2412 (fork), upstream/dev @ 472304e8, upstream tags, field log
rpizero `~/meshlog/dk5en-98/2026-09-2[34].log` (all 18 day files for the server-path counts).
Old sources were extracted with `git show <tag>:src/...`; line numbers refer to that version.

| Claim                                                                                             | Verdict                                                                |
| ------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------- |
| B-1 pre-June builds read the first via token as the destination, relay regardless, keep the via   | CONFIRMED, with two corrections (below)                                |
| B-1 DL2UD-1 runs such a build                                                                     | CONFIRMED (behavioural proof)                                          |
| B-1 old super node named first never relays and shows `*` text as a DM                            | CONFIRMED in code; does NOT apply to Abb. 6 (DL2JA-2 is 4.35t)         |
| B-2 injecting gateways call checkVia without resetting the path; the via leaks through the server | CONFIRMED (code and TX-LoRa of DK5EN-98 itself), text only in practice |
| B-2 field proof "4.35t nodes did not relay the injected via frames"                               | PARTLY: consistent, but confounded for DK5EN-1/DL2JA-2 and n=2         |

## (a) What a 4.35 node does with `X,Y,*`, by build window

Three windows, not two. The finder put the reset at 06-10. It came three days later.

| Window                      | Commits                            | Tags                                                    | Decode                               | Relay decision                          | Re-encode                                        | Display                                  |
| --------------------------- | ---------------------------------- | ------------------------------------------------------- | ------------------------------------ | --------------------------------------- | ------------------------------------------------ | ---------------------------------------- |
| W0: before 2026-06-09 16:10 | before 2c96f11b                    | 4.35a to 4.35o, 4.35p 2026-03-22 to v4.35p.06.08        | `msg_destination_call` = FIRST token | X: never. Everyone else: always (bMESH) | via carried unchanged                            | not shown, except at X, where it is a DM |
| W1: 2026-06-09 to 06-12     | 2c96f11b (06-09), 6781171c (06-10) | v4.35p.06.10 only                                       | last token                           | only if named (substring)               | via carried unchanged                            | correct                                  |
| W2: from 2026-06-13 00:31   | a8dc23a2 "v4.35p via"              | v4.35p.06.13 to 09.02, 4.35s, 4.35t, upstream/dev, fork | last token                           | only if named                           | path reset to the destination, then own checkVia | correct                                  |

Checked with `git merge-base --is-ancestor` against every 4.35p tag: all tags up to v4.35p.06.08
lack 2c96f11b, and every tag from v4.35p.06.10 on has it. The `p` letter covers 2026-03-22 to
2026-09-02, not April to August, and the 5-character on-air version field cannot separate W0
from W2.

W0 details (`v4.35p.06.08`; v4.35d and v4.35k are identical in logic):

- **Decode.** In `aprs_functions.cpp:296-331`, `cConcat3` stops at the first `,` and becomes
  `msg_destination_call`. `msg_destination_last` holds the last token, and
  `msg_destination_path` holds the full path. v4.35d has the same code at :264-279.
- **Relay.** `lora_functions.cpp:703` sets `destination_call` to the first token. The gate at
  :1027 is `strcmp(destination_call,node_call)!=0 && !bSetLoRaAPRS && checkMesh() && bMeshDestination`,
  and `via_functions.cpp` `checkMesh()` is just `return bMESH;`. So X never relays, and every
  other node relays whether it is named or not.
- **Re-encode.** The relay (:1030-1075) only appends its own call to the source path, then calls
  `encodeAPRS`, which writes `msg_destination_path` unchanged (`aprs_functions.cpp:1014`). The
  via travels on.
- **Display.**
  - At X, the DM branch runs (:709, `destination_call==node_call`) and calls
    `queueDisplayText`. `sendDisplayText` labels the frame "DM" because the path is not `*`. A
    `{nnn` suffix would even trigger `SendAckMessage`.
  - At every other node, the text branch requires
    `(destination_call=="*" && !bNoMSGtoALL) || CheckOwnGroup(destination_call)`. The frame is
    neither shown nor passed to the BLE app.
  - The dedup mark (`addLoraRxBuffer`, :687) comes first. A later clean `*` copy is therefore a
    duplicate and is never shown either.
  - Positions are not affected, because the POSITION branch ignores the destination.
- **DM and `:ack`.** A DM or `:ack` sent with a via to a W0 node is not recognised as addressed
  to it.

## (b) Who runs what

- **What the FW field shows.** It is the ORIGINATOR's firmware, not the last hop's. `initAPRS`
  sets it (`aprs_functions.cpp:127-128`), decode reads it (:493-515), and relays re-encode it
  unchanged: no relay path writes `msg_source_fw_*`. So only frames the node itself originated
  count.
- **Versions.** Counted from frames each node originated, 23/24.09:

  | Node     | Frames | Version |
  | -------- | ------ | ------- |
  | DL2UD-1  | 171    | FW:35:p |
  | DL2JA-2  | 463    | FW:35:t |
  | DB0ED-99 | 276    | FW:35:t |
  | DK5EN-1  | 216    | FW:35:t |

- **Why DL2UD-1 is W0.**
  1. DK5EN-98 injected `DB2GS-1,DK5EN-98>F1ZKT-12,*` (H04, TX-LoRa at 20:04:22).
  2. DL2UD-1 then sent `DB2GS-1,DK5EN-98,DL2UD-1>F1ZKT-12,*` (H03, 20:04:41, x11B75386). The
     hop count went from 4 to 3 and DK5EN-98 is in the path, so this is an RF relay, not
     DL2UD-1 injecting.
  3. It did the same with x11B75351, `DB2GS-1,DL2JA-2,DL2UD-1>F4AVI-12,20` (H03).
  4. Relaying a via frame that does not name it is impossible from W1 on, so DL2UD-1 runs a
     build from before 2026-06-09.
- **DK5EN-98's direct neighbours** (one-token paths): DB0ED-99, DK5EN-1, DL2JA-1 and DL2JA-2 run
  t. DL2UD-1 runs p and is W0.
- **Pre-fix nodes two hops out,** behind DL2JA-2: DL2JA-3 (4.35d) and DG3MNF-4 (4.35k).

## (c) Server-injection path

- **Code.** All four injection sites call `checkVia` without resetting the path first:
  - fork `udp_frame_esp32.cpp:252` and `udp_frame_nrf52.cpp:197`;
  - upstream `udp_functions.cpp:381` and `nrf52/nrf_eth.cpp:497`.
    This has been the case since 2b4a47b6/6781171c (2026-06-09/10).
- **When a gateway adds its own via.** Only with `--via on` AND a fixed `node_via`, in which
  case it REPLACES the originator's via. bVIA defaults to off (`loop_functions.cpp:197`, bit
  `node_sset2 0x4000`). Builds from 78c89ea2 (06-09) to c47e4993 (07-22) with `--via on` also
  stamped `HG,<dest>` on gateways. In 18 days of logs, no injected frame carries a via that a
  gateway added.
- **The server passes the via through.** RX-UDP lines show 15 distinct via paths from 9
  originators, for example DB2GS-1 (11 frames), DM3KS-12 (6) and IS0QLX-11 (6). DK5EN-98
  radiated the foreign via itself: `DB2GS-1,DK5EN-98>F5JFA-12,*` at 14:04:30 and
  `…>F1ZKT-12,*` at 20:04:22.
- **Text only.** 0 of 18 days have an RX-UDP position, so in practice the leak affects texts
  only.
- **Non-relay evidence (PARTLY).**
  - DK5EN-1 and DL2JA-2 injected both via frames themselves at H04, so their silence is a dedup
    confound.
  - The clean control is DB0ED-99. It appears as a relay for 392 of the 526 (75 %) msg_ids that
    DK5EN-98 injected on 23/24.09. That includes a no-via injection from DK5EN-98 at 20:03:07
    (`OE1XAR-33,DK5EN-98,DB0ED-99>*`), one minute before the via frame.
  - DB0ED-99 relayed 0 of the 2 via frames. This matches the code (4.35t
    `via_functions.cpp:82`, `indexOf` gives -1 and the result is false), but two frames are
    only suggestive.
- **Why it matters for auto-via.**
  - A v5 gateway uploads its own texts after `checkVia` (`loop_functions.cpp` 3446, then 4318).
    RF-heard texts go up raw (`lora_functions.cpp:1754`, before the rewrite at :1825).
  - Every 4.35 gateway (bVIA off) then injects them in its own region with the originator's
    local next-hop names. There, W1/W2 nodes do not relay and W0 nodes do not display, so the
    text reaches only the injecting gateway's direct neighbours.
  - A v5 gateway with a valid set would overwrite the path, assuming v5 builds it from
    `destination_call` the way the `node_via` branch does. In flood or SPERRE state it would
    pass the via through.
  - The fix for v5 is to reset the path to `destination_call` before `checkVia` in both
    udp_frame files. The 4.35 gateways keep leaking regardless.
  - Section 4.8 never mentions injection, and the mcmap acceptance criterion in rollout step 6
    (positions reaching gateways) cannot see this.

## (d) Consequence for "Altknoten befolgen den Via"

The sentence "gilt für 4.35 wie für den Fork" is false for W0 and partly false for W1. The
sentence "setzt keinen Via und flutet ab dort" holds only for W2 with bVIA off. For Abb. 6
(`DL2JA-2,DL2UD-1,*`):

- DL2JA-2 (t) behaves as the paper assumes.
- DL2UD-1 (W0):
  - It relays whether it is named or not, so dropping it from the set would not silence it.
  - It carries the via onward. In Abb. 6 this is exactly what makes the feeder work.
  - Its own display and app no longer show DK5EN-98's `*` and group texts.
  - A W0 node that decodes DL2UD-1's copy first shows nothing and discards the clean copy as a
    duplicate.
- In general, a W0 node in the super-node slot (named first) never relays and receives every
  text as a DM. The design therefore fails at its key node wherever the super node is W0.

What the paper needs:

- a Fehlerszenario row for W0 nodes;
- a behavioural W0 detector, since the version letter cannot tell: a neighbour that relays via
  frames not naming it, or that keeps a foreign via, is W0. This is observable in today's log;
- a rule not to name a W0 node first.
