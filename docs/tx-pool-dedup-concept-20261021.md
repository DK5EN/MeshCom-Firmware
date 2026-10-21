# MeshCom TX Buffer and Dedup: From Ring to Priority Pool

| Item           | Value                                                                                                         |
| -------------- | ------------------------------------------------------------------------------------------------------------- |
| Type           | Concept paper                                                                                                 |
| Date           | 2026-10-21                                                                                                    |
| Code baseline  | `fork-dev` at `bd49c742`                                                                                      |
| Files examined | `src/txring_functions.cpp`, `src/lora_functions.cpp`, `src/dedup_functions.cpp`, `src/configuration_global.h` |
| Field evidence | DG0OPK campaign (Aug 2026, 47.6 h, five nodes), DK5EN-98 neo soak (12 h, gateway)                             |
| Goal           | High LoRa channel utilisation through per-packet mesh decisions                                               |
| Status         | Draft; open discussion points in section 10                                                                   |
| Evidence rule  | Every number is read from the code or cited from a logged measurement                                         |

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
- Two different ACKs, both close a retry ladder:
  - **Binary frame ACK** `MSG_TYPE_ACK` 0x41, 12 B (`src/ack_functions.h`). A gateway
    sends it for a group/broadcast text (`*`, `WLNK-1`, `APRS2SOTA`, groups); it quotes
    the text's msg_id and stops the sender's ladder. No source path, only the hop
    nibble in byte 5 (`0x80 | max_hop_text`), decremented per relay. Own and relayed:
    priority 1 (`getMessagePriority()` maps 0x41 to `MSG_PRIO_CRITICAL`).
  - **DM ACK** `:ackNNN`, an APRS text frame (payload type `:`) from the DM recipient
    back to the sender (`src/loop_functions.cpp`, `%-9.9s:ack%03i`). It carries a path
    like any text. Own: priority 1 (P15); relayed: priority 3 like every text relay.
    CSMA base 3000/3000/4500/5500/5500 ms plus slot jitter.
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
  `max_hop_pos` (default 2), enqueued `DONE`, priority 5. Trickle timer: Imin 30 s,
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

| Prio | Class          | Proposed TTL                                                      | Rationale                                                                                                                                              |
| ---- | -------------- | ----------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------ |
| 1    | ACK, own DM    | 90 s for both ACK types (0x41 and own `:ackNNN`); none for own DM | Retry ladder (3 × 40 s) governs own DMs. An ACK older than two ladder steps stops nothing. A relayed `:ackNNN` sits in prio 3 and takes the relay TTL. |
| 2    | own group text | none                                                              | User intent; the ladder and the "already heard" check release it.                                                                                      |
| 3    | relay text     | 300 s                                                             | DG0OPK p99 relay latency 286 s under pathological deferral; humans wait for text.                                                                      |
| 4    | position       | 120 s                                                             | Next beacon is 30 min away, but a 2-minute-old position still has map value; beyond that the relay chain has moved on.                                 |
| 5    | HEY            | 180 s                                                             | Keep BP-03.                                                                                                                                            |

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
   arrived. Never for text or ACK. The superseded msg_id is already in the dedup table,
   so neighbours lose nothing.
2. **Coverage-gated relay cancel.** Exists (`--nbrrelay`, neighbour matrix stage 2,
   case A/B). It is the largest airtime lever and is gated because its field proof is
   pending. The pool and TTL make it safer: a cancelled relay frees real capacity
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

The hop clamp (10.1) is not an option here: it trades reach for airtime and so
conflicts with the goal "reach a wide field of nodes"; it stays a discussion point
until the stage-0 hop histogram exists.

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
- Risk: E equals D only because it ships default off behind `INSTRUMENT`, like
  `--nbrrelay`. Default on before the field proof would score 2 (E = 53).
- Testability: the delay function is native-testable; its benefit needs a bench or
  field proof (two receivers at different SNR, count the cancelled copy).
- RAM: C to E all cost the dedup table only; with the 5 B entry of 10.4 the classic
  delta drops from +847 B to +505 B.

**Recommendation: E as six sequential commits** (stages 0, 1, 1b, 2, 3, 4), each
reviewable and revertible on its own, one upstream PR per stage. The hard
prerequisite is stage 0: without the hop and age histograms the TTL values, the
dedup size and the 1b delay curve are guesses.

## 8. Campaign plan

| Stage | Commit scope                                                                                                                                                                                | Gate                                                                                            | Bench proof                                                                             |
| ----- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------- |
| 0     | Instrumentation: age-at-TX histogram per prio, `stat_latency_max` to `uint32`, `RING_DROP_STALE prio=`; hop histogram of relays (10.1); callsigns per 90 min / 12 h and `EVICT` rate (10.7) | `tools/regression.sh --stage 1,2`                                                               | 24 h on DK5EN-98 (read-only, explicit OK needed)                                        |
| 1     | TTL table + sweep for all priorities                                                                                                                                                        | new `test_txring` cases                                                                         | DK5EN-1 with `--txttl` short, injected relay flood                                      |
| 1b    | SNR-weighted relay delay, `INSTRUMENT`-switchable, default off                                                                                                                              | native cases for the delay curve                                                                | DK5EN-1/-92 at different SNR to a sender: the far node relays, the near one cancels     |
| 2     | Slot pool, `INFLIGHT` status, delete ring indices                                                                                                                                           | `test_txring` (hole reuse behind parked SENT), `test_txring_flood` unchanged, `test_nbr_replay` | DK5EN-1/-92/-90: 1 h gwflood replay, `drop=0` with holes present; RAK for the lock path |
| 3     | Time-aged dedup table, per-class size, remove `ENABLE_TBEAM` branch                                                                                                                         | `test_dedup_replay` with clock, two overflows become DUP                                        | DK5EN-98 soak: `RX_DEDUP_NEW` for an id older than 39 min must not occur                |
| 4     | Supersede-in-place for HEY/POS                                                                                                                                                              | new native cases (own HEY refresh, relayed POS replaced, text never)                            | DK5EN-1 trickle at Imin, pool depth stays 1 for HEY                                     |

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

## 10. Open discussion points (review 2026-10-21)

Not yet decided, not yet part of the stage plan. Each point names what it would
change in sections 5 to 8 if adopted.

### 10.1 Local hop clamp: rewrite `max_hop` down to the node's own limit

**Idea (operator).** A relay that receives a packet whose remaining hops would carry
it beyond the relay's own hop limit rewrites `max_hop` down instead of passing the
originator's value on. Long chains stop, packets stay regional and no longer cross
mountain ridges for several hundred kilometres.

**Rule sketch.** On relay, with `travelled = msg_last_path_cnt` and the node's own
limit `L` (`max_hop_text` / `max_hop_pos`, per payload type):

```
remaining_new = min(max_hop & 0x0F, max(0, L - travelled))
remaining_new == 0  -> deliver locally, do not relay (counted as rly_reason "hopclamp")
```

Flag bits in the upper nibble stay untouched. Cost: 0 B RAM, a few lines in the relay
block of `src/lora_functions.cpp` (`max_hop--` near line 2461).

**What exists today.** Gateways already _drop_ (not rewrite) text and position relays
whose `msg_last_path_cnt >= max_hop_text+1` / `max_hop_pos+1` (`rly_reason "gwcap"`,
SL-02, `src/lora_functions.cpp:2349` and `:2355`). That is the same policy, gateway-only,
text and position only, and without effect on what downstream nodes do.

| Aspect               | Assessment                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 |
| -------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Airtime              | Strong lever. Flood cost grows with the number of nodes inside the hop radius, roughly quadratically with hops in an area mesh. Cutting the tail hops removes the relays that reach the fewest new nodes per transmission.                                                                                                                                                                                                                                                                                                                                                                                                                 |
| Clamp vs. drop       | Clamping is stronger than the existing gwcap drop: it also binds every node downstream of us, not only our own relay.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      |
| Locality             | Hop count is a poor proxy for distance. One summit digipeater spans 150 to 200 km in a single hop; ten valley hops may cover 20 km. The clamp cuts the valley chain, not the ridge crossing. Locality by geography needs a different signal (see below).                                                                                                                                                                                                                                                                                                                                                                                   |
| Originator intent    | The originator's `max_hop` is overridden by any node on the path. One conservative node can split a region that depends on it as the only bridge. Behaviour becomes path-dependent: the same message reaches different areas depending on which relay wins CSMA.                                                                                                                                                                                                                                                                                                                                                                           |
| ACK return paths     | Both ACK types close a retry ladder, so a clamped return path costs more airtime than it saves. **DM ACK** `:ackNNN` is a text frame with a path: a text clamp hits it unless it is exempt by payload (`:ack`), and a DM that arrived over a long path loses its ACK on the way back; the sender repeats the DM (3 x 40 s). **Binary ACK** 0x41 (gateway confirmation of a group/broadcast text) has no path, only the hop nibble in byte 5; a path-count clamp cannot apply to it, only a clamp on the nibble, and a too-short nibble lets the sender's group-text ladder run. Exempt both, plus the DM itself, or require path symmetry. |
| HEY / neighbour data | HEY already uses `max_hop_pos` (default 2). The clamp matters for text and position, where originators set high values.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    |
| Interop              | Old firmware ignores it and keeps relaying with the original value; the clamp only takes effect where new firmware sits on the path. Safe in mixed nets, gradual effect.                                                                                                                                                                                                                                                                                                                                                                                                                                                                   |
| Dedup coupling       | Shorter chains shorten the maximum packet life, which widens the dedup corridor of section 5.3 (fewer entries needed, see 10.3).                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           |

**Variants to weigh.**

1. Clamp group/broadcast text and position only; exempt DMs, DM ACKs (`:ackNNN`, `:rejNNN`), the binary ACK 0x41 and `{ping}`. Smallest risk.
2. Clamp only on gateways (extends gwcap from drop to rewrite). Gateways sit at the
   points where regional meshes meet; that is where cross-region floods start.
3. Mesh-wide ceiling instead of per-node `L`: a fixed `MESH_HOP_CEILING` in
   `configuration_global.h`, same on every board. Keeps behaviour path-independent and
   matches the parity goal of section 6.
4. Geographic locality instead of hops: drop a relay when the originator position (from
   the node's mheard/position table) is farther than R km. Addresses the ridge case
   directly, but needs a known position and costs a lookup per relay.

**Evidence needed before deciding.** Hop-count distribution of relayed packets per payload
type (`msg_last_path_cnt` at relay time, already in the `info.pl` field of the RX log,
`src/lora_functions.cpp:701`) from the DG0OPK logs and the DK5EN-98 soak: what share of
relay airtime goes to packets at hop 3 and beyond. Add it to stage 0.

### 10.2 SNR-weighted relay delay (0 B RAM)

The CSMA backoff today depends on priority and slot jitter only; no backoff in
`src/txring_functions.cpp` or `src/lora_functions.cpp` reads SNR or RSSI. Proposal: the
lower the SNR of the received copy, the shorter the relay delay. The farthest receiver
relays first, nearer ones hear it and cancel through the existing cancel-on-overhear path.
This is the reach-per-airtime lever for a wide field of nodes and is identical on every
board. Candidate stage 1b, `INSTRUMENT`-switchable, ahead of stage 4.

### 10.3 Stage coupling: size dedup after stage 1

The 39.6-minute packet life that sets the lower corridor bound is a symptom of relays
waiting without a deadline. With per-priority TTL (stage 1) and optionally the hop clamp
(10.1), maximum packet life is bounded by roughly hops x relay TTL. Re-measure packet life
after stage 1, then fix `DEDUP_ENTRIES` (section 9, decision 2); 128 entries may suffice.

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

| Consumer                                   | Effect of an evicted row                                                                                                                                                                                                                             | Direction                              |
| ------------------------------------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------- |
| Relay need (`nbrIRelayNeed()`), remote row | A path token without a row contributes no coverage, "has the frame" shrinks, the need set grows: the node relays where it could have cancelled.                                                                                                      | Conservative: costs airtime, not reach |
| Relay need, direct neighbour row           | A direct neighbour without a row is not a dependant; the cancel can conclude "everyone covered" while it is not. Protected by the first eviction pass as long as fewer than 63 neighbours are fresh direct, so in practice only on hilltop gateways. | Aggressive: costs reach                |
| Edge pool (`NBR_MAX_EDGES` 256 classic)    | Oldest edges go first; coverage knowledge thins out.                                                                                                                                                                                                 | Conservative                           |
| MH list (web, app, T-Deck), horizon        | Shows at most 63 other stations on classic; horizon 48 entries classic vs. 112.                                                                                                                                                                      | Display only                           |
| Parity                                     | Same site, same traffic: a classic and an S3 node decide relay cancels differently once more than 63 callsigns fall into the 12-hour window. With `--nbrrelay` off (default) only the display differs.                                               | Parity gap, grows with network size    |

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
