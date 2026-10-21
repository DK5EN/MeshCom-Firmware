# MeshCom TX Buffer and Dedup: From Ring to Priority Pool

| Item           | Value                                                                                                           |
| -------------- | --------------------------------------------------------------------------------------------------------------- |
| Type           | Concept paper                                                                                                   |
| Date           | 2026-10-21                                                                                                      |
| Code baseline  | `fork-dev` at `bd49c742`                                                                                        |
| Files examined | `src/txring_functions.cpp`, `src/lora_functions.cpp`, `src/dedup_functions.cpp`, `src/configuration_global.h`   |
| Field evidence | DG0OPK campaign (Aug 2026, 47.6 h, five nodes), DK5EN-98 neo soak (12 h, gateway)                               |
| Goal           | High LoRa channel utilisation through per-packet mesh decisions                                                 |
| Status         | Draft; open discussion points in section 10                                                                     |
| Part B         | [`mesh-evolution-concept-20261021.md`](mesh-evolution-concept-20261021.md): review since v4.40a, ideas, roadmap |
| Evidence rule  | Every number is read from the code or cited from a logged measurement                                           |

## 1. BLUF

The TX ring is the wrong data structure for what the firmware already does with
it. Since priority selection was added, nothing reads the ring in order any
more; `iRead`/`iWrite` only survive as a capacity accounting that goes wrong
exactly when a packet is cancelled or parked. Replace the ring with a **slot
pool ordered by priority and enqueue time, with a per-priority time-to-live**.
This costs **0 bytes of RAM**, removes the gap problem and the starvation of
low-priority packets at once, and makes the existing cancel-on-overhear and
supersede logic safe to extend.

For dedup, change the semantic from "last N ids" to "ids heard in the last 45
minutes". The count-based ring has a load-dependent window that falls out of the
measured safe corridor (longer than 39.6 min packet life, shorter than 48.5 min
id reuse) at both ends. A time-aged table sized for 45 minutes of peak traffic
costs **+847 B on classic ESP32** and **+697 B on S3/nRF52**, inside the
10 to 11 kB static headroom of every classic board except `E22_XML-DevKitC`.

Feature parity across boards is then a parity of **semantics** (same TTLs, same
45-minute dedup window), not of slot counts. The only remaining difference is
how many entries a board must hold to cover 45 minutes of its own peak load.

Recommendation, in order:

| Stage | Change                                                         | RAM delta classic | Fixes                      |
| ----- | -------------------------------------------------------------- | ----------------- | -------------------------- |
| 1     | Per-priority TTL in the existing age sweep                     | +0 B              | Problem 2 (stale low prio) |
| 1b    | SNR-weighted relay delay (section 10.2), default off           | +0 B              | Airtime, reach per TX      |
| 2     | Slot pool replaces ring (free = any empty slot, order by time) | +0 B              | Problem 1 (gaps)           |
| 3     | Time-aged dedup table, 45 min window, sized per board class    | +847 B / +697 B   | Problems 3 and 4 (dedup)   |
| 4     | Supersede-in-place for own and relayed HEY/POS                 | +0 B              | Airtime, queue depth       |
| later | Descriptor table + byte pool for payloads (variable length)    | −2 kB or +slots   | Problem 3 (ring parity)    |

Stages 1 to 4 are one campaign on `fork-dev`, each stage its own commit with a
native regression test, bench proof on DK5EN-1/-92, then an upstream PR per the
usual flow.

## 2. The seven questions, answered

| #   | Question                                                   | Answer                                                                                                                                                                                                                                                                  |
| --- | ---------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | How long does a HEY stay in the buffer?                    | Until sent, evicted by a higher priority on a full ring, cancelled by the neighbour-matrix scan, or **180 s** (`RING_BG_MAX_AGE_MS`), whichever comes first. Typical wait with an empty queue is 3.7 to 5.9 s (CSMA base 5500 ms). Under load it reaches the 180 s cap. |
| 2   | What is the ageing mechanism?                              | `txRingAgeBackground()`: a 2-second main-loop sweep over all slots, acting **only on priority 5 (HEY)**, dropping entries older than 180 s with `RING_DROP_STALE`. No other priority ages. Section 3.2.                                                                 |
| 3   | RAM cost of ageing all packets by time?                    | **Zero.** `ringEnqueueTime[]` (80 B) already exists for every slot. A TTL table is five constants in flash. Section 5.1.                                                                                                                                                |
| 4   | How to turn the strict ring into a buffer that fills gaps? | Stop writing at `iWrite`: pick any slot with `len == 0`. Selection already scans every slot by priority; add enqueue time as the tie-break and the ring indices become unnecessary. Section 5.2.                                                                        |
| 5   | Is it then a priority/expiry buffer, not a ring?           | Yes. It is a bounded priority queue with expiry, backed by a fixed slot pool. The word "ring" should go from the code; the index pair, `advanceIReadPastEmpty()` and the N-24 relocation go with it.                                                                    |
| 6   | Other possibilities?                                       | Compacting ring (rejected), heap/linked-list queue (no gain at N=20), descriptor table + byte pool (the real parity lever for classic boards, later stage), supersede-in-place, airtime-aware admission, coverage-gated relay cancel (exists, gated). Section 5.4.      |
| 7   | Dedup and ring parity on all boards?                       | Define parity as time: 45-minute dedup window everywhere, same TTLs everywhere. Size tables per board class from the peak rate. Ring parity is already 20 slots on all boards; what differs is RAM per slot, which the byte-pool stage addresses. Section 6.            |

## 3. Current state, from the code

### 3.1 TX ring

- `ringBuffer[MAX_RING][260]`, `MAX_RING` = 20 on every shipped board. Slot layout:
  `[0]` length, `[1]` status, `[2]` type, `[3..6]` msg_id, `[7]` flags, `[8..]` frame.
- Side arrays per slot: `retryCount`, `ringPriority`, `ringSource`, `ringKind` (1 B each),
  `ringEnqueueTime` (4 B), `ringNeed`/`ringAlone` (8 B each on classic, 16 B on S3/nRF52).
  Static total **5680 B classic, 6160 B S3/nRF52**.
- Status: `READY 0x00` (own text, retransmittable), `SENT 0x01..0x14` (ladder counter,
  2-second ticks, retry at 0x15 = 40 s, three retries), `DONE 0xFF` (one shot: every
  relay, HEY, position, ACK, `addTxRingEntryOnce()` texts), `EXT_PENDING 0x80`.
- Priority classes: 1 ACK/DM, 2 group/broadcast text, 3 relay, 4 position, 5 HEY.
  CSMA base 3000/3000/4500/5500/5500 ms plus slot jitter
  (`src/configuration_global.h:436-456`).
- Two different ACKs, both close a retry ladder:
  - **Binary frame ACK** `MSG_TYPE_ACK` 0x41, 12 B (`src/ack_functions.h`). Only a
    gateway (`bGATEWAY`) sends it, and only for a group/broadcast text (`*`, `WLNK-1`,
    `APRS2SOTA`, groups) that is not its own TX (`src/lora_functions.cpp:2218-2240`).
    It quotes the text's msg_id and stops the sender's ladder. It is **enqueued twice**,
    both `DONE`, tags `rx_dm_ack_gw` and `rx_dm_ack_new` (`:2270`, `:2294`), the second
    with a fresh own id; the code gives no reason for the second copy. Not sent when
    `bSendAckGateway` is cleared: `{ping}`, `{pong}`, `{MCP}`, `{SET}`, `{CET}`,
    `msg_server` frames, destination `100001` (`:2111-2203`). No source path; byte 5
    holds a **7-bit** hop field (`0x80 | max_hop_text`), decremented per relay,
    plausibility bound `MAX_HOP_LIMIT` 7 (`:537`).
  - **DM ACK** `:ackNNN`, an APRS text frame (payload type `:`) from the DM recipient
    back to the sender (`src/loop_functions.cpp:5570`, `%-9.9s:ack%03i`). It carries a
    path and the 4-bit nibble like any text.
- Priority per frame kind (`getMessagePriority()`, `src/txring_functions.cpp:114-177`).
  The type check comes first; for text, status `DONE` returns 3 **before** the
  destination is parsed, so every relayed text is 3 whatever its destination.

  | Frame kind                         | Own                                            | Relayed |
  | ---------------------------------- | ---------------------------------------------- | ------- |
  | DM (text to one call)              | 1, `READY`, ladder 40 s, `MAX_RETRANSMIT` 3    | 3       |
  | DM ACK `:ackNNN`                   | 1, once (`addTxRingEntryOnce`, P15)            | 3       |
  | Group/broadcast text               | 2, `READY`, ladder                             | 3       |
  | `{ping}`, `{SET}`, RM1 reply       | 1 to a call, 2 to a group; once                | 3       |
  | Server frame injected by a gateway | 3 (`DONE`, "udp_rx"), whatever its destination | 3       |
  | Binary ACK 0x41                    | 1 (gateway only), twice                        | 1       |
  | Position                           | 4                                              | 4       |
  | HEY (`H`, `HG`)                    | 5                                              | 5       |

- Selection `getNextTxSlot()`: scans `iRead..iWrite`, lowest priority value wins, ties
  go to scan order (oldest by position, not by time).
- Enqueue `addTxRingEntryCore()`: always writes at `iWrite`. Full means
  `iWrite+1 == iRead` **and** the slot at `iRead` is occupied. Then: evict the worst
  occupied entry if the new one has a strictly higher priority (head drop), else drop
  the new one (tail drop). Eviction of a non-head slot relocates the head entry into the
  freed slot (N-24 fix), which costs that entry its FIFO rank.
- Consume `doTX()`: copies the frame out, sets `len = 0`, advances `iRead` past leading
  empties. Rollback paths restore `len`; a successful own text gets its `len` restored
  so the ladder can find it (parked `SENT` slot).

**Where the gaps come from.** Any slot freed between `iRead` and `iWrite` by a cancel
(overhear, ACK, neighbour-matrix cancel, stale HEY, retry give-up) is dead capacity
until `iRead` walks past it. `iRead` stalls on a parked `SENT` text for up to 120 s.
During that time the ring reports full with fewer than 20 occupied slots and drops
new packets (`RING_DROP_NEW`) while holes exist. `txRingDepth()` already counts
occupied slots instead of index distance (BP-02, DJ8MEH RCA: 8-minute QRT on a
phantom depth of 19 with 3 to 4 real entries). The accounting was fixed for the
back-pressure state machine; the write path still uses index distance.

### 3.2 HEY lifetime and ageing

- Own HEY: `sendHey()`, destination `H`/`HG`, payload `R<n>;` (about 30 B on air),
  `max_hop_text` (default 4; `initAPRS()` gives `@` the text budget), enqueued `DONE`, priority 5. Trickle timer: Imin 30 s,
  Imax 15 min, k = 2; a gateway sends at least one HG per Imax.
- Relayed HEY: same priority 5, same `DONE`, plus the signal-report chain appended
  per hop.
- Exit paths, in order of frequency on a loaded node: sent; `RING_DROP_PRIO` (a
  position or text arrived on a full ring); `RING_DROP_STALE` at 180 s (BP-03,
  motivated by a DJ8MEH blocker that sat 10 minutes); neighbour-matrix cancel
  (`--nbrrelay on`, case B, need mask empty); case-B hold (20 s one-time deadline,
  60 s cap on the long backoff).
- Measured before BP-03 (DG0OPK-11, FW 4.35p): heartbeat RX-to-relay median 860 s,
  max 2197 s. Position median 182 s, text median 30 s, p99 286 s. The cause was
  21.4 % CSMA success, not queue size; the ring was pinned at 29/30.
- Consequence of the 180 s cap on the own HEY: when it ages out, no copy follows
  until the next trickle interval, up to 15 minutes. Supersede-in-place (stage 4)
  turns that drop into a refresh.

### 3.3 Dedup ring

- `ringBufferLoraRX[MAX_DEDUP_RING][5]`: msg_id (4 B) + server flag (1 B). Pure
  overwrite ring, no timestamp, linear scan.
- Sizes: 70 classic ESP32, 100 S3/nRF52, 60 `E22_XML`. Static 350 / 500 / 300 B.
- Window is load dependent: at the DK5EN-98 rates (median 2.2 new ids/min, peak 3.8)
  70 slots hold 31.8 min median and 18.4 min peak; 100 slots hold 45.5 min median
  and 26.3 min peak. Required corridor from DG0OPK: longer than 39.6 min (longest
  measured packet life), shorter than 48.5 min (shortest msg_id reuse). Two proven
  overflows (`5D01F26F`, `BC98723D`): heartbeats kept alive by a sibling cluster were
  accepted a second time; only `max_hop` = 0 stopped a loop.
- A second, time-aged layer already exists for DMs (`dm_dedup.h`: 16 slots, 60-minute
  age, keyed source+NNN) and documents the same conclusion: count-based ageing
  breaks on a runaway sender, wall-clock ageing does not.

### 3.4 Board classes and RAM

| Class                 | Boards                                             | Fleet share (2026-09-20) | DRAM static headroom        | Heap idle    |
| --------------------- | -------------------------------------------------- | ------------------------ | --------------------------- | ------------ |
| classic ESP32         | TLORA V2.1, T-Beam V1.1/1.2, E22, Heltec V2        | ~63 %                    | 10.2 kB (T-Beam) to 11.7 kB | 69 kB T-Beam |
| classic, `ENABLE_XML` | `E22_XML-DevKitC`                                  | small                    | **1.2 kB**                  | n/a          |
| S3 / nRF52            | Heltec V3, T-Deck, RAK4631, T-Beam 1W, Supreme ... | ~32 %                    | 150 kB+ (S3)                | large        |

**Per build env.** The class is chosen in `src/configuration_global.h` by
`ENABLE_XML`/`ENABLE_SBUFFER`, then `CONFIG_IDF_TARGET_ESP32S3` or `BOARD_RAK4630`
(set in `[nrf52_base]`, so all three nRF52 envs take it), else classic. `MAX_RING`
also sizes `own_msg_id[]`, `retryCount[]` and the retry/prio side arrays. `NBR rows`
is the neighbour-matrix size (`NBR_MAX_ROWS`). Headroom is static DRAM left in
`dram0_0_seg` (ESP32) or total RAM minus static use (nRF52), from
`tools/resource_baseline.json` (2026-09-11).

| Env                         | MCU           | Class   | TX ring | Dedup | LOG | NBR rows | Static headroom |
| --------------------------- | ------------- | ------- | ------: | ----: | --: | -------: | --------------: |
| E22-DevKitC                 | ESP32 classic | classic |      20 |    70 |  20 |       64 |         11.5 kB |
| E22_1262-DevKitC            | ESP32 classic | classic |      20 |    70 |  20 |       64 |         11.5 kB |
| esp32-loraprs-e22           | ESP32 classic | classic |      20 |    70 |  20 |       64 |         11.6 kB |
| esp32-loraprs-ra01          | ESP32 classic | classic |      20 |    70 |  20 |       64 |         10.6 kB |
| heltec_wifi_lora_32_V2      | ESP32 classic | classic |      20 |    70 |  20 |       64 |         11.7 kB |
| ttgo-lora32-v21             | ESP32 classic | classic |      20 |    70 |  20 |       64 |         11.7 kB |
| ttgo_tbeam                  | ESP32 classic | classic |      20 |    70 |  20 |       64 |         10.3 kB |
| ttgo_tbeam_SX1262           | ESP32 classic | classic |      20 |    70 |  20 |       64 |         10.2 kB |
| ttgo_tbeam_SX1268           | ESP32 classic | classic |      20 |    70 |  20 |       64 |         10.2 kB |
| E22_XML-DevKitC             | ESP32 classic | XML     |      20 |    60 |  20 |       64 |          1.2 kB |
| E22_1262_S3-DevKitC-1-N16R8 | ESP32-S3      | large   |      20 |   100 |  10 |      128 |        156.6 kB |
| E22_1268_S3-DevKitC-1-N16R8 | ESP32-S3      | large   |      20 |   100 |  10 |      128 |        156.6 kB |
| heltec_wifi_lora_32_V3      | ESP32-S3      | large   |      20 |   100 |  10 |      128 |        160.9 kB |
| heltec_wifi_lora_32_V4      | ESP32-S3      | large   |      20 |   100 |  10 |      128 |        160.0 kB |
| heltec_wireless_stick       | ESP32-S3      | large   |      20 |   100 |  10 |      128 |        160.9 kB |
| heltec_wireless_tracker     | ESP32-S3      | large   |      20 |   100 |  10 |      128 |        162.4 kB |
| wireless-paper              | ESP32-S3      | large   |      20 |   100 |  10 |      128 |        161.5 kB |
| vision-master-e213          | ESP32-S3      | large   |      20 |   100 |  10 |      128 |        160.4 kB |
| vision-master-e290          | ESP32-S3      | large   |      20 |   100 |  10 |      128 |        157.7 kB |
| LilyGo_T-Beam-1W            | ESP32-S3      | large   |      20 |   100 |  10 |      128 |        157.1 kB |
| LilyGo_T3_S3_V1_3           | ESP32-S3      | large   |      20 |   100 |  10 |      128 |        158.7 kB |
| LilyGo_T_Connect_Pro        | ESP32-S3      | large   |      20 |   100 |  10 |      128 |        155.0 kB |
| ttgo_tbeam_supreme          | ESP32-S3      | large   |      20 |   100 |  10 |      128 |        158.0 kB |
| T-ETH-ELITE_1262            | ESP32-S3      | large   |      20 |   100 |  10 |      128 |        151.2 kB |
| t_deck                      | ESP32-S3      | large   |      20 |   100 |  10 |      128 |        133.1 kB |
| t_deck_plus                 | ESP32-S3      | large   |      20 |   100 |  10 |      128 |        133.1 kB |
| t_deck_pro                  | ESP32-S3      | large   |      20 |   100 |  10 |      128 |         82.0 kB |
| t5_epaper                   | ESP32-S3      | large   |      20 |   100 |  10 |      128 | not in baseline |
| wiscore_rak4631             | nRF52840      | large   |      20 |   100 |  10 |      128 |        160.0 kB |
| heltec_t114                 | nRF52840      | large   |      20 |   100 |  10 |      128 |        148.3 kB |
| t_echo                      | nRF52840      | large   |      20 |   100 |  10 |      128 |        156.2 kB |

The fourth branch (`ENABLE_TBEAM`: 10 / 10 / 10 / 32 rows) is defined by no env and
no variant; the T-Beam builds take the classic branch. The headroom numbers for S3
and nRF52 are static-link figures only; on the S3 the runtime heap and PSRAM
policy decide, not this column.

Headroom from `tools/resource_baseline.json` (2026-09-11), heap from
`tools/heap_baseline.json` (2026-10-18). The classic heap is the binding resource
for TLS (AU-17 needs 76 kB), not for a kilobyte of static tables.

## 4. Problem analysis (MECE)

| Problem                        | Root cause                                                  | Symptom in the log                                                             | Costs airtime?                                     |
| ------------------------------ | ----------------------------------------------------------- | ------------------------------------------------------------------------------ | -------------------------------------------------- |
| A. Gaps (dead capacity)        | Write pointer fixed at `iWrite`; holes behind a parked head | `RING_DROP_NEW` with `queued < 20`                                             | Indirectly: drops useful packets, keeps stale ones |
| B. Stale low-priority entries  | Only priority 5 has a TTL; 3 and 4 wait until evicted       | `lat=` in `RING_TX_READ` of minutes, positions sent long after the next beacon | Yes: airtime for obsolete frames                   |
| C. Dedup window load-dependent | Count-based ring, no timestamp                              | `RX_DEDUP_NEW` for an id seen 38 min earlier                                   | Yes: second flood of a live heartbeat              |
| D. Board asymmetry             | Three constant sets, chosen by RAM class not by requirement | 70 vs 100 dedup slots, same 20-slot ring                                       | Yes on 63 % of the fleet (shorter window)          |

A and B are properties of the TX structure; C and D of the dedup structure. They
do not overlap, and solving A with a pool makes B trivial, since a pool needs
exactly the age information that B needs.

## 5. Options

### 5.1 TTL for all priorities (Problem B)

Extend `txRingAgeBackground()` to every priority with a per-class limit:

| Prio | Class               | Proposed TTL                                                      | Rationale                                                                                                                                              |
| ---- | ------------------- | ----------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------ |
| 1    | ACK, own DM         | 90 s for both ACK types (0x41 and own `:ackNNN`); none for own DM | Retry ladder (3 × 40 s) governs own DMs. An ACK older than two ladder steps stops nothing. A relayed `:ackNNN` sits in prio 3 and takes the relay TTL. |
| 2    | own group text      | none                                                              | User intent; the ladder and the "already heard" check release it.                                                                                      |
| 3    | relay text          | 300 s                                                             | DG0OPK p99 relay latency 286 s under pathological deferral; humans wait for text.                                                                      |
| 3    | `{CET}` time beacon | 30 s                                                              | Injected by gateways as a relay (prio 3); a time that arrives minutes late sets a wrong clock. Own class by payload, see 10.8.                         |
| 4    | position            | 120 s                                                             | Next beacon is 30 min away, but a 2-minute-old position still has map value; beyond that the relay chain has moved on.                                 |
| 5    | HEY                 | 180 s                                                             | Keep BP-03.                                                                                                                                            |

Cost: 0 B RAM (table in flash, `ringEnqueueTime[]` exists), 0 extra CPU (sweep
already walks all slots). Make the values `INSTRUMENT`-tunable (`--txttl`) and log
`RING_DROP_STALE prio=`. Pick the final values from an age-at-TX histogram per
priority on DK5EN-98 before freezing; `stat_latency_max[]` saturates at 65535 ms
and must become `uint32`.

Risk: a TTL on relays reduces redundancy in a sparse net where a relay legitimately
waits minutes for a free channel. Mitigation: generous relay TTL, and the pool
(5.2) removes the main reason relays wait that long today.

### 5.2 Replace the ring (Problem A)

| Option                                         | RAM             | Code delta | Fixes gaps | nRF52 lock / EXT_RADIO ownership        | Verdict                                                        |
| ---------------------------------------------- | --------------- | ---------- | ---------- | --------------------------------------- | -------------------------------------------------------------- |
| O1 keep ring, reuse holes on write             | 0 B             | small      | partly     | unchanged                               | half measure: still needs `iRead` logic for depth and overflow |
| **O2 slot pool, order by prio + enqueue time** | **0 B**         | medium     | yes        | simpler: no relocation, stable slot ids | **recommended**                                                |
| O3 compacting ring (memmove on cancel)         | 0 B             | medium     | yes        | worse: copies under lock, slot ids move | rejected                                                       |
| O4 heap / linked-list priority queue           | +60 B           | large      | yes        | more pointers to guard                  | no gain at N=20                                                |
| O5 descriptor table + byte pool for payloads   | −2 kB or +slots | large      | yes        | handles instead of indices              | later: parity lever for classic                                |

O2 design:

- Free slot = `len == 0` and status not `EXT_PENDING` and not in flight. `addTxRingEntryCore()`
  takes the first free slot; "full" means no free slot. Eviction policy unchanged
  (strictly higher priority evicts the oldest entry of the worst priority, else tail drop),
  but "oldest" is now by `ringEnqueueTime`, so the flood test policy (`test_txring_flood`)
  still holds and the N-24 relocation disappears.
- Selection: scan all 20 slots, lowest priority value wins, ties by the earliest
  enqueue time (wrap-safe `int32_t` compare). The case-B hold logic is untouched.
- In-flight marker: `doTX()` today signals "consumed" with `len = 0` and restores it on
  rollback. In a pool that window would let a concurrent enqueue take the slot. Replace
  with a status `RING_STATUS_INFLIGHT` that keeps `len`; success sets `DONE` + `len = 0`
  or `SENT` for the ladder, rollback sets `READY`. This also removes the save/restore of
  the status byte and the `BOARD_TLORA_OLV216` special case.
- `txRingDepth()`, `txRingPrioCounts()`, the ACK scans, `findAndStopRingSlot()`, the
  neighbour-matrix scan and the age sweep already walk all slots by index; they need no
  change beyond the new status value.
- Delete: `iRead`, `iWrite`, `advanceIReadPastEmpty()`, `RING_OVERFLOW`,
  `ringOverflowAdvance`, `parkedOverwrite`, `stat_preempt_count` (redefine as "sent a
  slot that was not the oldest" if still wanted).
- `EXTERNAL_RADIO` ownership is by slot index; the pool never moves an entry, which
  is strictly better than today.
- Native tests: `test_txring` (drop N-24, add "fill a hole behind a parked SENT
  slot"), `test_txring_flood` unchanged in policy, new "TTL per prio" cases.

### 5.3 Dedup (Problems C, D)

| Option                                          | RAM classic | Window behaviour                                                         | Verdict                                                                            |
| ----------------------------------------------- | ----------- | ------------------------------------------------------------------------ | ---------------------------------------------------------------------------------- |
| D1 status quo, count ring                       | 350 B       | 18 to 32 min depending on load; both corridor ends violated              | reject                                                                             |
| D2 count ring + 2 B minute stamp, 45-min cap    | +140 B      | fixes the upper end (id reuse); peak load still shortens below 39.6 min  | partial                                                                            |
| **D3 time-aged table sized for 45 min of peak** | **+847 B**  | 45 min at any load up to 3.8 ids/min; above that degrades to count-based | **recommended**                                                                    |
| D4 hashed/compressed ids (3 B)                  | +513 B      | same as D3, 1e-5 false-duplicate chance per packet                       | reject: msg_id is 22-bit node id + 10-bit counter, truncation hits same-node reuse |

D3 design:

- Entry: msg_id 4 B, flags 1 B (server bit, room for "was relayed by me"),
  `uint16_t` uptime minutes = 7 B. Table size per class: 171 entries
  (3.8/min × 45 min) = 1197 B. Lookup: linear scan, skip entries older than
  `DEDUP_WINDOW_MIN` (45). Insert: first expired or empty slot, else the oldest.
- Same window on every board. A board that cannot afford 171 entries gets fewer
  and degrades gracefully at peak; `E22_XML` (1.2 kB headroom) stays at 100 entries
  (+400 B) unless its `ENABLE_XML` buffers are revisited.
- `checkOwnRx()`/`checkServerRx()` keep their signatures. `test_dedup_replay` (8813
  verdicts against real node logs, 0 deviations today) is re-run with a clock column;
  the expected change is exactly the two proven overflows turning into `DUP`.
- Also fixes UDP-02 (positions from the server enter the dedup ring regardless of
  novelty) as a side effect if the insert goes through one function.

### 5.4 Other possibilities

1. **Supersede-in-place (stage 4, 0 B).** At enqueue of a HEY or position (own or
   relayed), look for a queued entry of the same type and source call that is not in
   flight and not `SENT`; overwrite it instead of adding. A node's own HEY then never
   ages out silently, a stale relayed position never burns airtime after a fresher one
   arrived. Never for text or ACK, with one exception: a newer `{CET}` time beacon replaces a queued older one (10.8). The superseded msg_id is already in the dedup table,
   so neighbours lose nothing.
2. **Coverage-gated relay cancel.** Exists (`--nbrrelay`, neighbour matrix stage 2,
   case A/B). Fork production builds force it on (`prodLockNormalise()`,
   `src/prod_lock.h:177`, QRT wave 1, `docs/campaign-lora-qrt-20261018.md`); only
   `INSTRUMENT` builds can switch it; upstream status not checked. The bench log of
   2026-09-26 still shows only `CANCEL?` (count mode) and no executed cancel
   (`docs/bench/snf-20260926/dk1.txt:556`). The pool and TTL make it safer: a cancelled relay frees real capacity
   immediately instead of leaving a hole.
3. **Airtime-aware admission.** Refuse to enqueue a priority 4/5 relay when the pool is
   above 70 % (mirrors BP-01's 80 % gateway back-pressure). Cheap, but it decides at
   enqueue time what TTL decides better at send time; only if measurements show the pool
   saturating after stages 1 to 4.
4. **Deadline-aware CSMA.** Shorten the backoff of an entry close to its TTL (case B
   already does a 60 s cap). Marginal; not recommended before the age histogram exists.
5. **Byte pool for payloads (O5).** Mean frame on air is 77 B (RAM reclaim 2026-09-20),
   the slot is 260 B; 5200 B of ring hold 20 frames where a byte pool holds about 60.
   This is the only way to give classic boards more queue than S3 for the same RAM, or
   the same queue for 2 kB less. Large change: every slot-indexed consumer needs handles.
   Do it only if the pool's high-water mark shows 20 entries binding; DK5EN-98 peaked at
   4/20, DG0OPK-11 pinned at 29/30 because of channel access, not capacity.

## 6. Board parity

Parity is defined on behaviour, not on constants:

| Behaviour         | All boards after the campaign             | RAM delta classic     | RAM delta S3/nRF52 |
| ----------------- | ----------------------------------------- | --------------------- | ------------------ |
| Queue capacity    | 20 entries (unchanged), no dead capacity  | 0 B                   | 0 B                |
| TTL per priority  | one table, same values                    | 0 B                   | 0 B                |
| Dedup window      | 45 minutes, any load up to the sized peak | +847 B                | +697 B             |
| Supersede HEY/POS | on                                        | 0 B                   | 0 B                |
| Total             |                                           | **+847 B of 10.2 kB** | **+697 B**         |

The three `#if` branches in `configuration_global.h` shrink to one number per class
(`DEDUP_ENTRIES`); `MAX_RING`, the TTL table and `DEDUP_WINDOW_MIN` become
unconditional. The dead `ENABLE_TBEAM` branch (10/10/10, no variant defines it) is
deleted in the same commit.

Not in scope but the same disease: the neighbour matrix row count. `MAX_MHEARD` no
longer exists; MeshCom-5 wave 4 replaced MHeard and the path table by the neighbour
matrix (`src/nbr_matrix.h`, `docs/meshcom5-campaign.md`). The binding limit is now
`NBR_MAX_ROWS`: 64 classic, 128 S3/nRF52, against 121 distinct callsigns measured on
one gateway in 12 hours, which is exactly the matrix window (`NBR_WINDOW_MIN` 720).
Section 10.7 has the effect.

## 7. Decision matrix (campaign shape)

Revised 2026-10-21. The first version weighted only cost and risk; option A won on
paper (51) while D was recommended. The matrix now runs in two steps: knock-out
criteria first, then weighted scores among the options that pass.

**Options.**

| Option | Scope                                                                                                     |
| ------ | --------------------------------------------------------------------------------------------------------- |
| A      | Stage 1: TTL per priority                                                                                 |
| B      | A + stage 2: slot pool                                                                                    |
| C      | B + stage 3: time-aged dedup                                                                              |
| D      | C + stage 4: supersede HEY/POS                                                                            |
| E      | D + stage 1b: SNR-weighted relay delay (10.2), `INSTRUMENT`-switchable, default off until the field proof |

The hop cap (10.1) is not an option here. Its variants are open: H1 is an outlier
guard that leaves normal frames unchanged only if `L` is uniform (unverified, 10.0
item 3), while the locality variants (H2, H3, H5)
trade reach for airtime and so touch the goal "reach a wide field of nodes". It stays
a discussion point until the stage-0 hop data exists.

**Step 1, knock-out.** An option must solve all four structural problems of section 4.

| Knock-out criterion          | A   | B   | C   | D   | E   |
| ---------------------------- | --- | --- | --- | --- | --- |
| K1 problem A (gaps)          | no  | yes | yes | yes | yes |
| K2 problem B (stale entries) | yes | yes | yes | yes | yes |
| K3 problem C (dedup window)  | no  | no  | yes | yes | yes |
| K4 problem D (board parity)  | no  | no  | yes | yes | yes |
| Passes                       | no  | no  | yes | yes | yes |

A stays the fallback: it ships alone if the campaign stalls. B is an intermediate
state, not a target.

**Step 2, weighted.** Scores 1 (poor) to 5 (good), maximum 75.

| Criterion                                         | Weight | C   | D   | E   |
| ------------------------------------------------- | ------ | --- | --- | --- |
| Airtime saved per packet decision                 | 3      | 3   | 4   | 5   |
| Reach per transmission (hop progress, wide field) | 3      | 3   | 3   | 4   |
| Code and field risk (nRF52 lock, EXT_RADIO)       | 3      | 3   | 3   | 3   |
| RAM on classic ESP32                              | 2      | 4   | 4   | 4   |
| Native testability                                | 2      | 5   | 4   | 3   |
| Upstream mergeability (hardest single PR)         | 2      | 3   | 3   | 3   |
| Weighted total                                    |        | 51  | 52  | 56  |

Score rationale:

- Airtime: C stops second floods of live ids; D also stops obsolete HEY/POS copies;
  E lets the farthest receiver relay first, so nearer relays cancel on overhear
  instead of transmitting.
- Reach: TTL, pool, dedup and supersede do not change which node relays; E does,
  and favours the relay with the largest new coverage.
- Risk: E equals D only because it ships default off behind `INSTRUMENT`. Default on
  before the field proof would score 2 (E = 53). Note that `--nbrrelay` itself is
  forced on in fork production builds (5.4 item 2).
- Testability: the delay function is native-testable; its benefit needs a bench or
  field proof (two receivers at different SNR, count the cancelled copy).
- RAM: C to E all cost the dedup table only; with the 5 B entry of 10.4 the classic
  delta drops from +847 B to +505 B.

**Recommendation: E as six sequential commits** (stages 0, 1, 1b, 2, 3, 4), each
reviewable and revertible on its own, one upstream PR per stage. The hard
prerequisite is stage 0: without the hop and age histograms the TTL values, the
dedup size and the 1b delay curve are guesses.

## 8. Campaign plan

| Stage | Commit scope                                                                                                                                                                                                                                                                                                                                                                                      | Gate                                                                                            | Bench proof                                                                             |
| ----- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------- |
| 0     | Instrumentation: age-at-TX histogram per prio, `stat_latency_max` to `uint32`, `RING_DROP_STALE prio=`; hop histogram of relays (10.1); callsigns per 90 min / 12 h and `EVICT` rate (10.7); received nibble vs. own `L`, `{SET}` frames heard, gwcap with injected path (10.1); `{CET}` age per hop (10.8); HG relays per ring (10.9); relayed DM/ACK share, evictions and age in band 3 (10.10) | `tools/regression.sh --stage 1,2`                                                               | 24 h on DK5EN-98 (read-only, explicit OK needed)                                        |
| 1     | TTL table + sweep for all priorities                                                                                                                                                                                                                                                                                                                                                              | new `test_txring` cases                                                                         | DK5EN-1 with `--txttl` short, injected relay flood                                      |
| 1b    | SNR-weighted relay delay, `INSTRUMENT`-switchable, default off                                                                                                                                                                                                                                                                                                                                    | native cases for the delay curve                                                                | DK5EN-1/-92 at different SNR to a sender: the far node relays, the near one cancels     |
| 2     | Slot pool, `INFLIGHT` status, delete ring indices                                                                                                                                                                                                                                                                                                                                                 | `test_txring` (hole reuse behind parked SENT), `test_txring_flood` unchanged, `test_nbr_replay` | DK5EN-1/-92/-90: 1 h gwflood replay, `drop=0` with holes present; RAK for the lock path |
| 3     | Time-aged dedup table, per-class size, remove `ENABLE_TBEAM` branch                                                                                                                                                                                                                                                                                                                               | `test_dedup_replay` with clock, two overflows become DUP                                        | DK5EN-98 soak: `RX_DEDUP_NEW` for an id older than 39 min must not occur                |
| 4     | Supersede-in-place for HEY/POS                                                                                                                                                                                                                                                                                                                                                                    | new native cases (own HEY refresh, relayed POS replaced, text never)                            | DK5EN-1 trickle at Imin, pool depth stays 1 for HEY                                     |

Each stage: docs (`RESUME.md`, defect catalogue), full host gate, then commit on
`fork-dev`. Upstream PRs are cut from `upstream/dev` afterwards, one per stage,
in German.

## 9. Operator decisions needed

1. TTL values for priority 3 and 4 (proposal 300 s / 120 s) after the stage-0 histogram.
2. `DEDUP_ENTRIES` for classic: 171 (+847 B, covers the DK5EN-98 peak) or 128
   (+546 B, covers 33.7 min at that peak). Proposal: 171; the headroom is there.
3. Whether `E22_XML-DevKitC` keeps a shorter dedup window or gets its buffers cut.
4. Whether stage 0 may run on DK5EN-98 (production gateway, read-only, needs the
   usual explicit OK).
5. Discussion points of section 10, all open: hop cap variant (10.1), `{CET}` reach
   and age (10.8, including what "two hops" means), HG budget (10.9), priority of
   relayed DMs and `:ackNNN` (10.10). Each needs its stage-0 data first.

## 10. Open discussion points (review 2026-10-21)

Not yet decided, not yet part of the stage plan. Each point names what it would
change in sections 5 to 8 if adopted.

### 10.0 Hop model, from the code (fact base for 10.1, 10.8 to 10.10)

Rewritten 2026-10-21 after code scouting, an adversarial review and a verification
pass; the first version of 10.1 rested on a wrong model (path count as RF hop count).
Claims carry `file:line`; INFERRED marks what is derived, not read.

1. **Byte 5 of a frame.** Bits 0 to 3 are the remaining hops; 0x80 server flag,
   0x40 track, 0x20 app offline, 0x10 mesh (decode `src/aprs_functions.cpp:181-193`,
   encode `:1033-1045`, the 0x10 bit from the global `bMESH`). The binary ACK 0x41 uses
   byte 5 with a 7-bit hop field (3.1).
2. **The hop count is set per frame by its originator, once.** Every own frame takes
   its nibble from `initAPRS()`: text (`:`) and HEY (`@`, both `H` and `HG`) get
   `max_hop_text`, every other type `max_hop_pos` (`src/aprs_functions.cpp:105-112`).
   Fixed exceptions: HN report and msgstore delivery send 0
   (`src/loop_functions.cpp:5779`, `src/msgstore_glue.cpp:117`); the 0x41 ACK carries
   `max_hop_text` in its own field. No user, app, web, KISS or serial path chooses a
   hop per message. Defaults 4 and 2, `MAX_HOP_LIMIT` 7
   (`src/configuration_global.h:363-365`). `max_hop_text` is 1 to 6 in production,
   0 to 7 via `{SET}` in `INSTRUMENT` builds (`src/maxhop.h`, `src/prod_lock.h:196-200`);
   `max_hop_pos` is pinned to 2 (`src/esp32/esp32_main.cpp:1567`,
   `src/nrf52/nrf52_main.cpp:756`, `src/prod_lock.h:184`).
3. **Who sets the node's budget.** Locally `--maxhop`, remotely the server command
   `{SET}N;M;` (`src/loop_functions.cpp:2744-2761`, bounded by `prodLockSetHop()`).
   Gateways put `{SET}` on LoRa like any server frame (the enqueue at
   `src/esp32/udp_frame_esp32.cpp:565-574` sits after the `0x3A` branch); nodes relay it
   (`bMeshDestination` is not cleared for it) and every receiver applies it with no
   source, server-flag or destination check (`src/lora_functions.cpp:2134`,
   `src/loop_functions.cpp:2723`); no authentication (N-02, accepted 2026-08-18).
   **Unverified:** whether `max_hop_text` is uniform in a region. No field log in the
   repository shows a server `{SET}`; stage 0 counts them.
4. **Relay gates.** On every node: `nibble > 0` (`src/lora_functions.cpp:2455`), then
   `max_hop--` (`:2461`) and the loop check (own call in the path, `:2466-2477`). On
   gateways in addition **gwcap**: no relay when `msg_last_path_cnt >= max_hop_text + 1`
   (text) or `>= max_hop_pos + 1` (position) (`:2349-2358`, sets `bMeshDestination =
false`, which the decision chain at `:2442-2450` treats as no relay). gwcap is a
   path-count cap against the gateway's own setting, i.e. an H4-type rule (10.1) that is
   live today. No node compares the received **nibble** with its own setting, and no
   received nibble is clamped anywhere except by its 4 bits.
5. **`msg_last_path_cnt` is not an RF hop counter.** It is the number of calls in the
   source path (`src/aprs_functions.cpp:203-233`). It differs from RF hops in the local
   island because a gateway that injects a server frame appends its call to the path
   that already carries the foreign island's chain
   (`src/esp32/udp_frame_esp32.cpp:323-324`). A third path form exists: with
   `bSHORTPATH` a relay rewrites the path to "source, own call"
   (`src/lora_functions.cpp:2507-2519`); fork production builds force shortpath off
   (`src/prod_lock.h:176`), so this applies to `INSTRUMENT` builds and to other
   firmware in the field. Its only decision use is gwcap; it is also stored in the
   neighbour matrix for display (`:701`).
6. **Server leg.** A gateway uploads a LoRa frame before its own relay decrement
   (`addNodeData` `src/lora_functions.cpp:2409`, decrement `:2461`). It injects server
   frames **without decrementing**, appends its call and sets the server flag
   (`src/esp32/udp_frame_esp32.cpp:323-326`, nRF52 `:209-212`; deliberate,
   `docs/archive/befund-maxhop-unterlauf.md` section 4): every radio island behind a
   gateway starts with the nibble the server sends. What the server does with the
   nibble between upload and fan-out is not in this repository (open).
7. **Several gateways per island.** Each gateway dedups the UDP copy on its own
   (`is_new_packet()`, `src/esp32/udp_frame_esp32.cpp:220`), so two gateways that get it
   before hearing each other both inject. Inside the island dedup is per node too: a
   copy each node has not seen yet is relayed once by it.
8. **Consequence for distance (INFERRED).** A frame uploaded in one region is injected
   by every connected gateway under the same msg_id. The several-hundred-kilometre reach
   therefore plausibly runs over the server fan-out more than over long RF chains; RF
   chains are bounded by the nibble per island. Not measured.
9. **Outliers in the field.** Builds without the fix wrap the nibble from 0 to 15 on
   relay of a broadcast or group text on a node without a connected app, by ORing 0x20
   into `max_hop` (field: 4.35p, 4.35c, 4.35s; 17 wraps in 10 days, DMs not affected;
   `docs/archive/befund-maxhop-unterlauf.md`). The fork carries two fixes: the flag is
   no longer ORed into `max_hop` (`src/lora_functions.cpp:2170-2180`) and the relay guard
   masks the nibble (`:2455`). The UDP paths still OR 0x20
   (`src/esp32/udp_frame_esp32.cpp:494`, `src/nrf52/udp_frame_nrf52.cpp:455`); this is
   harmless on LoRa (the frame is already encoded, encode masks `& 0x0F`) and dead for
   the BLE copy. A `{SET}` once wrote 44 (C18, `docs/code-quality-2.0.md`).

### 10.1 Hop cap: limit the remaining hops by the node's own setting

**Idea (operator), stated in the terms of 10.0.** A relay receives a frame whose
remaining hops have not run out but exceed the relay's own limit `L` (`max_hop_text`
for text and HEY, `max_hop_pos` otherwise). It rewrites the nibble down instead of
passing the originator's or the server's value on. Goal: frames stay regional
instead of running on across ridges.

Status: open, nothing decided. The variants differ in where the cap applies and what
it is measured against. H0 includes gwcap, which already exists on gateways.

| Variant                            | Rule                                                                                                                                     | Pro                                                                                                                                                                                                                  | Contra                                                                                                                                                                                                                                                                                                                                                |
| ---------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| H0 status quo                      | relay with `nibble - 1`; gateways also apply gwcap                                                                                       | no change; originator and server keep per-frame control; works with the whole fleet                                                                                                                                  | an outlier (wrapped 15, bogus `{SET}` value) runs up to 15 relays per island before dedup and loop check stop it; gwcap counts the wrong metric (10.0 item 5) and may block frames re-injected by another gateway of the same island                                                                                                                  |
| H1 nibble cap on relay             | `nibble_new = min(nibble - 1, L)`                                                                                                        | needs no path, so `bSHORTPATH` and the server leg do not distort it; bounds outliers from the first fixed relay on; 0 B; unfixed downstream nodes keep decrementing the capped value                                 | removes the per-frame control of originator and server (the pro of H0); the first transmission after injection or origination is uncapped; unfixed downstream nodes can still wrap; with a uniform `L` normal frames are unchanged (no locality gain), with mixed `L` reach becomes path-dependent and a low-`L` node can cut off a region it bridges |
| H2 nibble cap at gateway injection | `nibble_new = min(nibble, L_inject)`, set on `aprsmsg` before `encodeAPRS()` (`udp_frame_esp32.cpp:335`) or patched into `convBuffer[5]` | acts on the server fan-out, plausibly the carrier of long distance (10.0 item 8, INFERRED); downstream relays need no update                                                                                         | overrides the server's per-frame choice (for example `{CET}`, 10.8); only on gateways with new firmware; an island with several gateways (10.0 item 7) behaves by whichever copy arrives first, nondeterministic with mixed `L_inject` or mixed firmware                                                                                              |
| H3 server-side policy              | the server sets the nibble per frame type or region before the fan-out                                                                   | one place, consistent across all islands, no firmware change; the server gets HEY signal-report chains, paths and positions from every gateway (`src/lora_functions.cpp:2377-2390`), so it has partial topology data | outside this repository and outside the fork's control; what the server knows and does today is unknown                                                                                                                                                                                                                                               |
| H4 path-count cap                  | `nibble_new = min(nibble - 1, L - msg_last_path_cnt)`                                                                                    | intuitive "total hop budget"; gwcap already does this on gateways as a drop                                                                                                                                          | wrong metric (10.0 item 5): counts the foreign island and the gateway after a server leg; would kill re-injected frames in the local island; extending it to every node spreads gwcap's flaw                                                                                                                                                          |
| H5 geographic radius               | drop a relay when the originator's known position is farther than R km                                                                   | addresses the ridge case directly; hop count is a poor distance proxy (summit digipeaters bridge long distances in one hop; no measured figure in this repository)                                                   | a server-injected frame's originator is usually far away, so H5 would stop local relaying of the whole fan-out unless it exempts `msg_server` frames or measures from the injecting gateway; needs originator positions from the neighbour matrix, whose rows are capped (10.7); a lookup per relay                                                   |

**Exemptions, for every variant that rewrites.** DMs, DM ACKs (`:ackNNN`, `:rejNNN`),
the binary ACK 0x41 (own 7-bit field; sent twice per text by the gateway, 3.1) and
`{ping}`. Both ACK types close a retry ladder; a cut return path costs more airtime
than the cap saves.

**gwcap, to verify.** By 10.0 items 5 and 7, a frame injected by another gateway of the
same island arrives with the foreign chain in its path and may trip gwcap after one RF
hop. The field counts (text 56, position 259 gwcap rejections,
`docs/nbr-wichtigkeit-konzept.md`) do not separate this case.

**Evidence needed (stage 0).** Per payload type: distribution of the received nibble
against the node's own `L`; share of frames with nibble > `L` and with nibble 15; the
`{SET}` frames heard and their values (uniformity of `L`); for gateways the nibble of
server frames at injection; for gwcap rejections, whether the path contains a gateway
injection.

**Assessment (proposal, not decided).** H4 conflicts with 10.0 item 5; whether gwcap
itself should change is a separate question that the stage-0 gwcap data answers. H1 is
a cheap guard against outliers if stage 0 shows frames with nibble > `L`; on its own it
gives locality only where `L` is lowered. If locality is the goal, H3 or H2 act where
the long distance plausibly arises, but that rests on the unmeasured inference of
10.0 item 8; measure it first. H5 only if the hop-based variants leave the ridge case
unsolved.

### 10.2 SNR-weighted relay delay (0 B RAM)

The CSMA backoff today depends on priority and slot jitter only; no backoff in
`src/txring_functions.cpp` or `src/lora_functions.cpp` reads SNR or RSSI. Proposal: the
lower the SNR of the received copy, the shorter the relay delay. The farthest receiver
relays first, nearer ones hear it and cancel through the existing cancel-on-overhear path.
This is the reach-per-airtime lever for a wide field of nodes and is identical on every
board. Candidate stage 1b, `INSTRUMENT`-switchable, ahead of stage 4.

### 10.3 Stage coupling: size dedup after stage 1

The cause of the 39.6-minute packet life that sets the lower corridor bound is not
established in this paper: relays waiting without a deadline is one candidate, a late
copy over a second gateway or the server leg is another. With per-priority TTL
(stage 1), the life of a frame within one island is bounded by roughly hops x relay TTL,
but only where every relay runs stage 1 and no node wraps the nibble; most of the
field runs 4.35x without either. The dedup window must cover the end-to-end life
(copies via another gateway or the server leg), which the per-island bound does not
limit. Re-measure packet life
after stage 1, then fix `DEDUP_ENTRIES` (section 9, decision 2). Whether fewer entries
suffice follows only from that measurement, not from the bound.

### 10.4 Dedup entry format and scaling with network size

Parity by time means dedup RAM grows linearly with the id rate, i.e. with network size;
171 entries come from one gateway's peak (3.8 ids/min). State the curve (entries per kB
against ids/min) explicitly. Format lever: 1 B minute stamp (mod 256, enough for a window
under 60 min) plus the server flag as one bit = 5 B per entry instead of 7 B; 171 entries
= 855 B, +505 B over today on classic instead of +847 B.

### 10.5 Decision matrix vs. recommendation

Resolved 2026-10-21: section 7 now filters on the four problems first (knock-out),
then weights; reach per transmission is a criterion of its own.

### 10.6 Supersede of relayed HEY

Replacing a queued relayed HEY with a newer one drops the older frame's signal-report
chain. Accept it deliberately and document it in stage 4.

### 10.7 Neighbour matrix row limit (formerly `MAX_MHEARD`)

MHeard is gone; the neighbour matrix holds every callsign heard in the last 12 hours
(`NBR_WINDOW_MIN` 720). Rows: 64 classic, 128 S3/nRF52 (`configuration_global.h`,
`NBR_FAMILY_*`). A gateway that hears 121 callsigns in 12 hours overflows on classic,
not on S3/nRF52. On overflow `nbrIPlanRow()` (`src/nbr_matrix.cpp`) evicts the row with
the oldest `last_min`, first among rows not freshly heard direct, only then among all;
each eviction logs `[NBR]|EVICT`.

Effects of the overflow on classic boards:

| Consumer                                   | Effect of an evicted row                                                                                                                                                                                                                                 | Direction                              |
| ------------------------------------------ | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------- |
| Relay need (`nbrIRelayNeed()`), remote row | A path token without a row contributes no coverage, "has the frame" shrinks, the need set grows: the node relays where it could have cancelled.                                                                                                          | Conservative: costs airtime, not reach |
| Relay need, direct neighbour row           | A direct neighbour without a row is not a dependant; the cancel can conclude "everyone covered" while it is not. Protected by the first eviction pass as long as fewer than 63 neighbours are fresh direct, so in practice only on hilltop gateways.     | Aggressive: costs reach                |
| Edge pool (`NBR_MAX_EDGES` 256 classic)    | Oldest edges go first; coverage knowledge thins out.                                                                                                                                                                                                     | Conservative                           |
| MH list (web, app, T-Deck), horizon        | Shows at most 63 other stations on classic; horizon 48 entries classic vs. 112.                                                                                                                                                                          | Display only                           |
| Parity                                     | Same site, same traffic: a classic and an S3 node decide relay cancels differently once more than 63 callsigns fall into the 12-hour window. Fork production builds force `--nbrrelay on` (5.4 item 2), so the difference reaches relay decisions there. | Parity gap, grows with network size    |

**The 12-hour window is the lever, not the row count.** Freshness for relay coverage
(`nbrIFresh()`) is the same 720 minutes as for the list. A relay decision about the next
seconds does not need 11-hour-old edges; the HN report already uses 60 minutes
(`NBR_REPORT_FRESH_MIN`) and 45 minutes (`NBR_REPORT_VALID_MIN`). Same principle as the
dedup (section 6): parity by time, not by count.

| Option                                                      | RAM classic                  | Effect                                                                                   |
| ----------------------------------------------------------- | ---------------------------- | ---------------------------------------------------------------------------------------- |
| N1 classic to 128 rows                                      | about +6 kB (estimate below) | Parity by count; eats most of the 10.2 kB T-Beam headroom, dedup stage on top gets tight |
| **N2 short coverage window (e.g. 90 min), list stays 12 h** | 0 B                          | Relay decisions use only fresh edges on every board; list overflow stays cosmetic        |
| N3 two-tier eviction: remote rows age out after 90 min      | 0 B                          | Rows needed = callsigns per 90 min, not per 12 h; frees rows for direct neighbours       |

Estimate for N1 from the per-row formula (`configuration_global.h`, MeshCom-5 block):
masks widen from 8 to 16 B, so 128 rows x 2 masks x 16 B = 4096 B instead of 1024 B
(+3072), 64 more rows x 20 B call and core (+1280), 256 more edges x 6 B (+1536),
`ringNeed`/`ringAlone` 2 x 20 x 8 B (+320): about +6.2 kB. Rough; a link map decides.

**Evidence needed (stage 0).** From the DK5EN-98 and DG0OPK logs: distinct callsigns per
90 minutes and per 12 hours, `EVICT` count per hour, and how often a relay cancel used an
edge older than 90 minutes. If the 90-minute count stays well under 64, N2 plus N3 give
parity at 0 B.

### 10.8 `{CET}` time beacons: reach and age

Rewritten 2026-10-21; the first version claimed the server's hop value was unknown
and proposed a clamp the server already applies in practice.

**Current path, from the code and the field log.**

- The server sends `{CET}YYYY-MM-DD hh:mm:ss` (despite the name UTC,
  `src/loop_functions.cpp:2778-2788`) to every gateway, with a server pseudo-call as
  source (field log: `OE1XAR-33`, `OE1XAR-62`).
- The gateway shows it (`sendDisplayText()`, `src/esp32/udp_frame_esp32.cpp:347-350`)
  and also queues it on LoRa: the enqueue (`:565-574`, status `DONE`, "udp_rx") sits
  after the `0x3A` branch, so it applies to `{CET}` and `{SET}` too. `DONE` text means
  **priority 3 (relay)**. No decrement at injection (10.0 item 6). It triggers no 0x41
  ACK (`bSendAckGateway` cleared, 3.1).
- Every gateway injects it under the same msg_id. Dedup is per node: each node that has
  not seen the beacon yet relays it once while the nibble allows (10.0 item 7).
- Nodes relay it like any text (prio 3), except `{CET}<` (marked wrong time,
  `src/lora_functions.cpp:2144-2147`).
- A receiver sets its clock only without RTC, GPS fix and NTP and only for a year
  after 2023; phone time does **not** protect it (`bPhoneTimeValid` is commented out,
  `src/loop_functions.cpp:2769`). It takes the payload time as is
  (`:2766-2795`): no correction for queue or air time.

**Field evidence** (`docs/bench/snf-20260926/`, log time MESZ, payload UTC).

Ages at reception (`tbeam.txt`, the DK5EN-91 log):

| Line | Path                                                        | Nibble at RX | Payload time | RX time (UTC) | Age  |
| ---- | ----------------------------------------------------------- | ------------ | ------------ | ------------- | ---- |
| 458  | `OE1XAR-33,DK5EN-98` (gateway direct)                       | H02          | 17:54:59     | 17:55:54      | 55 s |
| 57   | `OE1XAR-62,DM5HR-12,DB0MMR-12,DL2JA-2` (gateway + 2 relays) | H00          | 17:48:27     | 17:49:58      | 91 s |

Relays of one beacon, `x6AAA3153` from gateway DK5EN-98 (received with H02, relayed
with H01, all `prio=3`):

| Node     | Log                 | Queue wait (`wait=`) |
| -------- | ------------------- | -------------------- |
| DK5EN-90 | `rak.txt:354-355`   | 17.0 s               |
| DK5EN-91 | `tbeam.txt:495-496` | 20.3 s               |
| DK5EN-1  | `dk1.txt:566-567`   | 31.7 s               |

Read: in these samples the beacons arrive with nibble 2 from the gateway (INFERRED that
the server sends 2: two beacons, two pseudo-calls); the gateway passes it on and two
relay levels use it up. Airtime per beacon is therefore 1 + N1 + N2 transmissions per
gateway (N1, N2: nodes in the first and second relay ring), not three. In this sample
three first-ring nodes relayed; DK5EN-1 logged a cancel candidate after hearing
DK5EN-90 (`[NBR]|CANCEL?`, `dk1.txt:556`, count mode on that bench day) and still sent.
Relay queue waits are 17 to 32 s per hop; the gateway's own share of the 55 s (server
latency plus queue) needs the gateway log line `[GW];rx;type;CET;...;ms` (TM-39).

**Open: what "two hops" means.** Two RF transmissions (gateway plus one relay, nibble 1)
or gateway plus two relays (nibble 2, the value seen today). The options below hold for
both; only the target value differs.

**Reach options.**

| Option                                | Pro                                                            | Contra                                                                                                    |
| ------------------------------------- | -------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------- |
| R0 status quo (2 seen in the field)   | already bounded; no change; works with the whole fleet         | gateway plus two relay rings, each ring adds wait (17 to 32 s measured) and so error, and N transmissions |
| R1 server sends 1                     | one place, no firmware change, takes effect everywhere         | outside the fork's control (10.1 H3)                                                                      |
| R2 gateway caps at `CET_MAX_HOP`      | in the fork's control; downstream relays need no update        | overrides the server; only on gateways with new firmware; several gateways per island (10.0 item 7)       |
| R3 receiver prefers the freshest copy | accept `{CET}` only with nibble >= 1, i.e. near a gateway; 0 B | nodes beyond one relay get no time from `{CET}`; the beacon still costs the same airtime                  |

**Age options.**

| Option                                 | Pro                                                                                                  | Contra                                                                                                                                  |
| -------------------------------------- | ---------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------- |
| A0 status quo                          | no change; no rewriting of server content; works with the whole fleet                                | ages of a minute and more are set as current time, also over a phone-synced clock                                                       |
| A1 TTL in the queue (for example 30 s) | 0 B, fits stage 1; stops sending stale time                                                          | with relay waits of 17 to 32 s measured, 30 s would already drop part of the second ring; the value needs the stage-0 age histogram     |
| A2 age correction at TX                | adds the queue wait (`now - ringEnqueueTime[slot]`, exists) to the payload; error left about airtime | rewrites the payload of a server frame on every relay; the server-to-gateway latency stays uncorrected; old firmware relays uncorrected |
| A3 supersede in the queue              | a newer `{CET}` replaces a queued older one (5.4 item 1)                                             | helps only when two beacons meet in one queue                                                                                           |
| A4 priority raise (to 2)               | shorter wait                                                                                         | pushes group text back; does not fix the server-to-gateway part                                                                         |
| A5 phone time protects the clock       | re-enable `bPhoneTimeValid` in the `{CET}` gate                                                      | needs regular phone time (the code comment says so); does not help nodes without a phone                                                |

**Assessment (proposal, not decided).** Measure first (stage 0): `{CET}` age at TX and
at reception per hop, and the gateway's receive time. If queue waits dominate, A1
(value from the histogram) plus A3 are small and in line with stages 1 and 4. A2 is the
most accurate but changes server content on air; a maintainer decision. For reach: R1
if the server operators agree, otherwise R2; R3 as a receiver-side alternative without
airtime gain.

### 10.9 HG (gateway HEY)

**From the code.**

- `sendHey()` sends destination `HG` when the node is a gateway, `H` otherwise
  (`src/loop_functions.cpp:5677-5680`), payload `R<n>;`. Hop from `initAPRS('@')`:
  `max_hop_text`, default 4, **not** `max_hop_pos`. Priority 5 for own and relayed,
  set by type in `getMessagePriority()`. No back-pressure check (BP-01 sits only in
  the user-message path, `:4497`). The gateway does not upload its own bare HG (GW-01).
- Trickle counts `H` and `HG` alike (`src/lora_functions.cpp:1739-1741`). A gateway is
  exempt from suppression once its last own HEY is at least Imax (15 min) old
  (`src/hey_policy.h`), so every gateway sends at least one HG per Imax.
- Receivers: `HG` sets `NBR_FLAG_GW` on the sender's row, `H` clears it, held 45 min
  (`NBR_GW_HOLD_MIN`, `src/lora_functions.cpp:793-802`, `src/nbr_matrix.cpp:1216-1228`).
  Gateway-held rows are removed from the relay dependant set
  (`src/nbr_matrix.cpp:1770-1774`): a node does not count on a gateway to relay for it.
- The HG frame itself has no relay rule of its own: hop, cancel and case A/B are the
  same as for `H`. Gateways upload every received HEY (`H` and `HG`) with their own
  signal report appended, even with the server flag set
  (`src/lora_functions.cpp:2377-2390`).

**What this means for the hop discussion.** A relayed HG carries the gateway's
existence up to `max_hop_text` (4) hops into the mesh, at priority 5 and with a growing
signal-report chain (capped at 106 B, `HEY_PATH_PAYLOAD_MAX`). The receivers' only use
of the HG flag is local (need set, display); the server gets the chain through the
upload of any gateway.

| Option                           | Pro                                                                               | Contra                                                                                                                   |
| -------------------------------- | --------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------ |
| G0 status quo                    | gateway visible across the whole hop radius; signal-report chain for the server   | HG airtime grows with every relay ring; nodes several hops away gain a GW flag they cannot use for the need set          |
| G1 HG with a lower nibble        | HG mainly informs direct and two-hop neighbours (need set, display); less airtime | the server's signal-report chains from far rings shrink; changes `initAPRS()` semantics for one destination; mixed fleet |
| G2 HG as `max_hop_pos` (2)       | aligns HG with the position budget, which is pinned and uniform                   | same as G1; HEY of normal nodes would differ from HG unless `H` follows                                                  |
| G3 leave HG, cap all HEY (H, HG) | one rule for neighbour discovery; trickle already limits own HEY                  | larger change; HEY is the neighbour matrix's main input beyond one hop                                                   |

**Assessment (proposal, not decided).** Stage 0 should count HG relays per ring and how
often a far-ring GW flag is used. Without that data G0 stays; G1 or G2 are cheap if the
data shows far-ring HG airtime without use.

### 10.10 Priorities of personal messages and their `:ackNNN`

**From the code** (table in 3.1). Own DM and own `:ackNNN` are priority 1. Once relayed,
both are priority 3, the same band as every relayed group text and every
server frame: `getMessagePriority()` returns 3 for a `DONE` text before it parses the
destination. A server frame injected by a gateway is 3 even when it is a DM to a node
of the island. Field note, `docs/lora-qrt-dk5en98-db0taw14-20261018-verdict.md`
finding 8: "server DMs for other nodes get 3 instead of 1 and are the first evicted",
with the caution that "foreign server DMs at priority 1 would compete with own ACKs".

Effect: a DM and its `:ackNNN` lose their priority at the first relay. Each relay ring
waits in the 4500 ms CSMA band, and in a full queue they compete with group-text relays
on equal terms. The sender's ladder (3 x 40 s)
fires when the ACK is late; every late ACK costs up to three extra DM transmissions.

| Option                                         | Pro                                                                                 | Contra                                                                                                               |
| ---------------------------------------------- | ----------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------- |
| P0 status quo                                  | simple; relays cannot starve own traffic; no class for foreign traffic above 3      | DM and ACK compete with group relays; late ACKs trigger the ladder                                                   |
| P1 relayed DM and `:ackNNN` to priority 2      | ahead of group relays, behind own DMs and ACKs (1); parse cost only for `DONE` text | needs destination parsing for `DONE` text (it exists for `READY`); a relayed DM flood would push own group text back |
| P2 relayed `:ackNNN` only to priority 2        | the ACK stops the ladder, so it saves the most airtime per byte; small change       | DM itself still in 3; needs payload parse (`:ack` after the addressee)                                               |
| P3 server-injected DM to a local node to 2     | addresses finding 8 directly; the gateway knows the destination                     | only gateways; foreign DMs above group relays; the verdict's caution applies if it went to 1                         |
| P4 keep 3, but exempt DM and ACK from eviction | no ordering change; protects against the "first evicted" case                       | does not shorten the wait; adds a rule to the eviction policy (5.2)                                                  |

Interplay with the other stages: with the slot pool (stage 2) the eviction order is by
priority and age; with TTL (stage 1) a relayed DM in band 3 gets the relay TTL (300 s
proposed), longer than the sender's ladder of 3 x 40 s. Each retry carries its own id
(`pnRetryId()`, k = retry count + 1, `src/lora_functions.cpp:3221-3224`), so dedup does
not merge them: a stale relayed copy and the retry are both relayed.

**Assessment (proposal, not decided).** P2 has the best ratio of airtime saved to change
size, followed by P4 for the eviction case. P1 and P3 change the fairness between
foreign and own traffic and need the stage-0 queue data (share of relayed DMs and ACKs
in band 3, their evictions and their age at TX).

### 10.11 Docs inventory for this concept

What the existing docs say about hops, HEY/HG, priorities and `{CET}`/`{SET}`, from a
sweep over `docs/**/*.md` (tags: F fact, M measurement, C concept, A archived).

| Topic             | Doc                                                         | Statement                                                                                                  | Tag                            |
| ----------------- | ----------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------- | ------------------------------ | --- |
| Hop model         | `architecture/11-wire-format.md` (byte 5, `{SET}`, `{CET}`) | byte 5 layout, defaults 4/2, `{SET}N;M;`, `{CET}` gate                                                     | F                              |
| Hop model         | `archive/befund-maxhop-unterlauf.md`                        | wrap 0 to 15, broadcast/group only, nodes without app; gateways do not decrement on injection (deliberate) | A                              |
| Hop model         | `architecture/08-defect-catalogue.md` N-02                  | `{SET}` unauthenticated; accepted/WONTFIX                                                                  | A                              |
| Hop model         | `how-to-read-meshcom-logs.md`                               | `hop0`, `gwcap` relay reasons                                                                              | F                              |
| Hop model         | `nbr-wichtigkeit-konzept.md`                                | gwcap rejections text 56, position 259                                                                     | M                              |
| HEY / HG / HN     | `hey-supp.md`                                               | trickle 30 s / 15 min; own HEY -53 % (87 vs 186), steady state > 90 %                                      | M                              |
| HEY / HG / HN     | `soak-20261001-440a-verdict.md`, `soak-20260928-verdict.md` | gateway HEY median 15.0 min; HG sets the GW flag; 1/146 HN lost to prio-5 starvation                       | M                              |
| HEY / HG / HN     | `hey-storm-2026-10-17.md`                                   | one node sent 14,557 HEYs in an hour                                                                       | M                              |
| HEY / HG / HN     | `nbr-logformat.md`                                          | `[NBR]                                                                                                     | GW` rules, HN report max_hop 0 | F   |
| Priorities        | `how-to-read-meshcom-logs.md`                               | prio 1 ACK+DM, 2 group, 3 relay, 4 position, 5 HEY; CSMA 3000/4500/5500                                    | F                              |
| Priorities        | `lora-qrt-dk5en98-db0taw14-20261018-verdict.md` finding 8   | server text is relay prio 3; server DMs first evicted                                                      | M                              |
| Priorities        | `adr-nc-importance-backoff.md`                              | no differentiation inside the relay band; importance-based slots (not in code)                             | C                              |
| Priorities        | `BACKLOG.md` CSMA-01                                        | a reply never gets its own priority on the first CSMA attempt                                              | C                              |
| Relay suppression | `nbr-wichtigkeit-konzept.md`, `nbr-stage2-campaign.md`      | 350 ms slot order ineffective at 40 neighbours; wait grows about 6 s per heard frame; case A/B             | M/C                            |
| Relay suppression | `campaign-lora-qrt-20261018.md`                             | `--nbrrelay on` locked as production default (fork)                                                        | F                              |
| Field             | `soak-20261001-440a-verdict.md`                             | own relay wait median 3.7 s, p95 10.1 s; foreign copy heard before own TX in 3 to 5 %                      | M                              |

Contradictions and stale points found in the sweep:

1. `prio-talk-flood-networking.md` (2024-06-15) calls the 350 ms slot order reliable;
   `nbr-wichtigkeit-konzept.md` measures it as ineffective at 40 neighbours. The older
   doc predates 4.35.
2. N-02: `architecture/08-defect-catalogue.md` proposes an auth gate;
   `rm-gui/extended-commands-concept.md` records it as accepted WONTFIX.
3. `max_hop_text` persistence: `d1-04-settings-field-triage-20260912.md` says persisted;
   `code-quality-2.0.md` says overwritten by compile defaults after `init_flash()`. To
   check, it decides whether a `{SET}` survives a reboot.
4. `d1-04-settings-field-triage-20260912.md` says `max_hop_pos` is overwritten from a
   received position frame at `loop_functions.cpp:2349`; the code there is only a
   comparison. Likely stale.
5. Stale line references: `hey-echo-upload-concept.md` (`sendHey()` at `:5420`, now
   `:5651`), `adr-nc-importance-backoff.md` (`getMessagePriority()` at `:46-73`, now
   `:114-177`), `soak-20260928-verdict.md` (`nbr_matrix.cpp:1273-1277`).

## Appendix A: RAM arithmetic

```
TX ring, classic ESP32
  ringBuffer        20 x 260   = 5200 B
  retryCount/prio/source/kind 4 x 20 = 80 B
  ringEnqueueTime   20 x 4     = 80 B
  ringNeed/ringAlone 2 x 20 x 8 = 320 B     (S3/nRF52: 2 x 20 x 16 = 640 B)
  total                          5680 B     (S3/nRF52: 6160 B)

Dedup today
  classic 70 x 5 = 350 B, S3/nRF52 100 x 5 = 500 B, XML 60 x 5 = 300 B

Dedup proposed (45-min window, 3.8 ids/min peak)
  171 x 7 = 1197 B   -> classic +847 B, S3/nRF52 +697 B, XML +897 B
  128 x 7 =  896 B   -> classic +546 B (33.7 min at peak, 45 min at median)

Static headroom (dram0_0_seg, 2026-09-11)
  ttgo_tbeam_SX1262 10184 B, E22-DevKitC 11464 B, ttgo-lora32-v21 11696 B,
  heltec_wifi_lora_32_V2 11744 B, E22_XML-DevKitC 1160 B
```

## Appendix B: Code pointers

- `src/txring_functions.cpp`: `getMessagePriority()`, `getNextTxSlot()`,
  `addTxRingEntryCore()` (overflow, N-24 relocation), `txRingAgeBackground()`.
- `src/lora_functions.cpp`: `findAndStopRingSlotImpl()` (ACK stop), the READY-echo
  release, the neighbour-matrix cancel scan, `doTX()` consume and rollback,
  `updateRetransmissionStatus()` (40 s ladder, `MAX_RETRANSMIT` 3).
- `src/dedup_functions.cpp`: `is_new_packet()`, `addLoraRxBuffer()`.
- `src/dm_dedup.h`: the time-aged precedent.
- `src/configuration_global.h`: memory classes, priorities, CSMA bases,
  `RING_BG_MAX_AGE_MS`, trickle constants.
- Tests: `test/test_txring`, `test/test_txring_flood`, `test/test_dedup_replay`,
  `test/test_nbr_replay`.
- Field evidence: `logs/dg0opk/`, `docs/campaign-loganalyse-20261012.md`,
  defect catalogue N-14, N-24, BP-01..03, TM-31, TM-37, UDP-02.
