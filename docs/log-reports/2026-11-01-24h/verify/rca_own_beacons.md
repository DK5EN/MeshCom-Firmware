# RCA: own prio-4/5 beacons fare worse than relays of the same prio and type

Scope: OE3XOC-12 (main case), OE3XIA (repeat), OE3XHL (control), OE3XWJ (extra), logs from 2026-10-31/11-01
(`$S/norm/*.log`). Code at HEAD 651e0920. The field build is unknown; where the code is used to explain log
behaviour, the behaviour itself confirms that code path (see "Build caveat").

## Verdict

**Mechanism (fact, log and code):** own and relayed beacons share priority and FIFO position, but once
either is the **head** of the pool (the `getNextTxSlot()` pick), each gets a different CSMA hold:

- **Relays (`RING_KIND_RELAY`, NBR case A):** under `--nbrrelay on` the hold comes from
  `txringCaseBackoffSlot()` (`src/txring_functions.cpp:385-405`). Before 8 s of waiting it is a 3500 ms base;
  after 8 s (`NBR_RELAY_CASE_A_MAX_WAIT_MS`, `src/configuration_global.h:581`) it drops to the **150 ms short
  search + 0..2 x 35 ms** (`:582`, `:580`). The branch is taken in `csma_compute_timeout_slot_nofloor()`,
  `src/lora_functions.cpp:3998-4006`.
- **Own beacons (`RING_KIND_OTHER`, set by default in `addTxRingEntry(..., "user_wx")` / `"auto_pos"`,
  `src/loop_functions.cpp:5487`, `:5769`):** they always get `csma_compute_timeout_prio()`: **5500 ms base +
  0..10 x 35 ms** (`src/configuration_global.h:486-487`). They have **no escape**. The age floor excludes prio
  4-5 (`src/csma_timing.h:102`), and the case-A short search applies only to relays.
- **Every received frame re-arms the timer** for the current head (`OnRxDone`: `iReceiveTimeOutTime = millis();
csma_timeout = csma_compute_timeout(cad_attempt);`, `src/lora_functions.cpp:2746-2747`). The TX gate opens
  only after a silence of at least `csma_timeout` (`src/esp32/esp32_main.cpp:3591`, `:3863-3874`). OE3XOC
  hears about 35 frames/min. A 5.5 s silence is therefore rare, while a 150-220 ms one follows almost every
  frame.

So an own beacon at the head **sits there and blocks** until a long gap or rapid-fire CAD (attempt >= 3, 100 ms)
lets it out. A relay at the head leaves within seconds. The TTL (120 s for position, 180 s for HEY,
`src/txring_ttl.h:48-64`) then kills the own entry.

**Classification: design gap, not a coding bug.** The QRT fix plan states it on purpose: "Priority 4-5
(position, HEY) never escalate; they age out" (`docs/meshcom-lora-qrt-fix-plan-20261018.md:332`, `:431`). That
premise silently fails for relays: the NBR case-A short search (stage 2, written earlier) already escalates prio-4/5
**relays**. As a result, own beacons are the only class without any escape. This is not a config error, although
it only shows when `--nbrrelay on` is set (relays case A) and the channel is busy.

## Evidence

### 1. Same prio, same FIFO, very different head drain rate (fact)

The head was reconstructed from RING_WRITE/PRIO/TX_READ/DROP_STALE/SUPERSEDE (lowest prio, then earliest enqueue,
exactly `getNextTxSlot()`, `src/txring_functions.cpp:213-255`). The table shows, per head class, the time as head
and the TXs per minute while that class was the head (`rca_own_beacons_checks/head_hazard.py`).

| Node   | Head class | Head time s | TX by head | TX/min as head | TX_GATE/min | RX/min | fired hold p50 ms |
| ------ | ---------- | ----------: | ---------: | -------------: | ----------: | -----: | ----------------: |
| OE3XOC | relay_HEY  |        6309 |       1657 |          15.76 |       33.90 |  27.61 |               185 |
| OE3XOC | relay_POS  |        3590 |        780 |          13.04 |       28.78 |  32.34 |               185 |
| OE3XOC | own_HEY    |        5594 |         41 |       **0.44** |        1.24 |  35.41 |          **5535** |
| OE3XOC | own_WX     |        1427 |         15 |       **0.63** |        1.60 |  37.17 |          **4793** |
| OE3XOC | ACK (p1)   |       50417 |       3373 |           4.01 |        9.75 |  37.13 |              1640 |
| OE3XIA | relay_HEY  |        3850 |       1107 |          17.25 |       43.33 |  24.33 |               150 |
| OE3XIA | own_HEY    |        7312 |        149 |           1.22 |        3.68 |  31.84 |              4793 |
| OE3XIA | own_WX     |        1249 |         38 |           1.83 |        5.33 |  28.44 |              4688 |
| OE3XWJ | relay_POS  |        2965 |        489 |           9.90 |       14.69 |  29.20 |               220 |
| OE3XWJ | own_WX     |        1621 |         46 |           1.70 |        2.59 |  24.39 |              5535 |
| OE3XHL | relay_HEY  |       27418 |        858 |           1.88 |        2.52 |  19.06 |              4570 |
| OE3XHL | own_WX     |         768 |         36 |           2.81 |        3.75 |  22.59 |              5570 |

Fired-hold histogram (`hold_hist.py`, `RX_TIMEOUT_FIRE wait=`) on OE3XOC:

- **Relay head:** 64-70 % in 150-220 ms, which is exactly case A short (150 + {0,35,70}), and 22-24 % rapid
  (100 ms).
- **Own head:** 44-58 % in 5001-6000 ms and 21 % in 4001-5000 ms (the 5/6 retry scaling of 5500). The rest is
  rapid.
- **Mode on:** no other code path produces 150-220 ms (`csmaAgedHoldMs` min is 500 ms, rapid is 100 ms), so
  OE3XOC runs `bNBRCANCEL` = `--nbrrelay on` (inferred). All 2919 `[NBR]|NEED` lines on OE3XOC are case `A`
  (1729 H, 793 P, 397 T). OE3XIA, XWJ and XIR are likewise 100 % case A.
- **Gate check:** TX_GATE/min ≈ timer fires/min for own heads (1.24 vs 117 fires / 5594 s = 1.25/min). Each fire
  leads to a gate attempt, so no extra own-TX gate refuses them. The timer simply rarely expires.

### 2. Own entries lose by spending their head time unsent (fact)

Per-entry outcome (`timeline.py`, OE3XOC):

| Class     |    n | tx   | ttl | superseded | episodes aired | lat_tx p50 / p90 |
| --------- | ---: | ---- | --- | ---------- | -------------- | ---------------- |
| own_HEY   |  141 | 41   | 57  | 43         | 41.8 % (41/98) | 58.9 s / 151.7 s |
| relay_HEY | 1727 | 1657 | 24  | 46         | 98.6 %         | 19.7 s / 96.5 s  |
| own_WX    |   49 | 15   | 34  | 0          | 30.6 %         | 26.7 s / 81.7 s  |
| relay_POS |  791 | 780  | 9   | 2          | 98.9 %         | 13.4 s / 51.0 s  |

- **Head time before the drop** (`blocker_share.py`): TTL-dropped own WX on OE3XOC spent 70 % of their life
  behind prio-1 ACKs and **28 % as the head themselves**. Per entry, the time as head had a median of **32 s**, and
  32 of 34 were head for 5 s or more. TTL-dropped own HEY had a median of **49 s** as head, 54 of 57 for 10 s or more.
  OE3XIA: own WX median 40 s (11 of 11 ≥ 10 s), own HEY median 64 s (39 of 40 ≥ 10 s).
- **Comparison:** a relay head on OE3XOC drains at about 15/min, which is about 4 s per head period (inference
  from the table above).
- **Example, OE3XOC own WX `81125F9A`:** RING_WRITE 08:01:14.429 slot 6 (queued 6/20), RING_DROP_STALE
  08:03:14.692 age_s=120. In between, the head was a stream of `rx_ack_fwd` ACKs (e.g. RING_TX_READ 08:01:15.150
  `D334903E` lat=27146, 08:01:51.244 `0CCF9755` lat=28398, 08:02:14.158 `7E2A39F3` lat=25722), with CAD_BUSY at
  nearly every gate.
- **Head-of-line blocking:** own entries also block relays queued behind them. Relay HEY spend 13-16 % of their
  life behind an own HEY head on OE3XOC, and 19-30 % on OE3XIA.

### 3. Control and repeat

- **OE3XHL (control, 0/49 own WX lost, own HEY 98 % aired):**
  - The channel is quieter: 19 RX/min, median OnRxDone gap 4.88 s vs 3.48 s on OE3XOC (`rx_gaps.py`).
  - A 5.5 s hold expires often enough: own head drain is 1.9-2.8/min.
  - Its relays get **no** relay advantage there. About half its relays are case B (849 H:B, 246 P:B in NEED), with
    long holds (60 % in 4001-5000 ms), so own and relay drain at the same rate. The asymmetry vanishes when relays
    do not get the case-A short search.
  - Caveat: case-B slots in their 20 s hold are skipped by `getNextTxSlot()` (`txringInCaseBHold`). My head
    reconstruction ignores that, so the OE3XHL relay head rate is a lower bound.
- **OE3XIA:** same pattern, weaker. Own HEY aired in 78.8 % of episodes, own WX lost 11/49, against relay HEY
  96.9 % and relay POS 99.6 %. Own heads drain at 1.2-1.8/min, relay heads at 13-17/min.
- **OE3XWJ:** same asymmetry (own WX lat p50 34.3 s vs relay POS 9.4 s), but only 4/50 lost because there is no
  ACK backlog (prio 2 dominates, rarely head).

## Candidates ruled out

| Candidate                                                           | Test                                                                                                                                                                                                                                             | Result                                                     |
| ------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ---------------------------------------------------------- |
| Pick order within one prio (slot index vs age)                      | `getNextTxSlot()` compares `ringEnqueueTime` (`txring_functions.cpp:228-251`). Over all TX events during each entry's life: younger same-prio entry sent first = 0.00 per entry for all four classes (OE3XOC, OE3XIA)                            | Ruled out (0 overtakes)                                    |
| Own READY vs relay DONE affecting pick/TTL                          | All own beacons are written `status=FF` (DONE), like relays: `loop_functions.cpp:5487`, `:5769` (0xFF); RING_WRITE `type=21 status=FF src=user_wx` (49), `type=40 status=FF src=auto_pos` (141). TTL is identical (`txring_ttl.h:48-64`)         | Ruled out                                                  |
| Overhear/cancel removing relays early (survivor bias)               | OE3XOC, OE3XIA, OE3XWJ: 0 `CANCEL` lines (all relays case A, cancel needs an empty alone mask, `lora_functions.cpp:1421-1426`). Relays end by tx 96-100 %                                                                                        | Ruled out on these nodes (OE3XHL: 589 CANCEL, but control) |
| Own-TX spacing/duty gate                                            | TX_GATE_ENTER per minute equals RX_TIMEOUT_FIRE per minute while an own entry is head (1.24 vs 1.25 on OE3XOC). Gate code (`esp32_main.cpp:3863-3874`) only checks `getNextTxSlot() >= 0`                                                        | Ruled out                                                  |
| WX written at xx:00:14 / xx:30:44 into network WX bursts            | OE3XOC: loss at :14 = 16/25, at :44 = 18/24 (no phase effect). Writes drift by 30 s each cycle (not locked to a burst). RX/min while own WX is head is 37.2, against 37.1 under an ACK head and 32.3 under a relay POS head                      | Not the cause (at most a minor load difference)            |
| Relays benefit from the "just received" moment                      | Relay lat p50 is 13-20 s, longer than case A's 8 s threshold. Relays mostly leave via the 150-220 ms short search after any later frame, not at their own enqueue moment                                                                         | Subsumed: the benefit is the case-A hold                   |
| Age floor prio 1-3 + strict priority alone (earlier analysis)       | Hits own and relay prio 4/5 equally. Explains why prio-4/5 wait behind ACKs (70-84 % of the life of dropped entries in both groups), not the own/relay gap                                                                                       | Necessary context, not sufficient                          |
| Supersede resetting `ringEnqueueTime` (`txring_functions.cpp:1007`) | Own HEY: 43 of 141 superseded (trickle re-sends). Each supersede restarts age and TTL and moves the HEY behind relay HEYs enqueued meanwhile: episode wait to tx p50 85.2 s vs per-entry 58.9 s. Own WX: 0 supersedes, yet the worst loss (69 %) | Secondary amplifier for HEY only                           |

## Second contributor (fact, separate topic)

- **ACK backlog:** prio-1 ACK forwards (`src=rx_ack_fwd`, 3367 in 25 h on OE3XOC, `lora_functions.cpp:661`) are
  the head 56 % of the logged time on OE3XOC (50417 s) and drain at only 4/min (lat about 25-40 s each).
- **Who it hits:** strict priority makes every prio-4/5 entry wait behind them, own and relay alike.
- **Where the loss happens:** the backlog uses up most of an own beacon's TTL. The missing escape then wastes the
  rest (section 2).

## Smallest fix candidate (no code changed)

- **Fix (a), one condition:** let the age floor cover prio 4 and 5 for **non-relay** slots.
  - **Where:** `csmaApplyAgeFloor()` (`src/csma_timing.h:99-108`) currently returns early for prio > 3. The
    smallest variant widens that condition to prio 1-5. The scoped variant applies it in
    `csma_compute_timeout_slot()` (`src/lora_functions.cpp:4030-4043`) only when
    `(ringKind[slot] & 0x7F) != RING_KIND_RELAY`.
  - **Effect on relays:** the scoped variant leaves case-B relays untouched, so their 20-60 s "let the neighbours'
    flood run first" hold stays.
  - **Bookkeeping:** keep `stat_csma_aged_boot` (`:2941`) and the CSMA_AGED print condition (`:4040`) in step.
- **Expected effect (inference):**
  - **Holds:** an own beacon at the head gets the 500-1500 ms short hold once it is 20 s old, and 100 ms + jitter
    from 60 s, after every received frame (never after an own TX, so no capture).
  - **Drain:** its head drain should rise from 0.4-1.8/min toward the relay values of 10-17/min.
  - **Recovery:** 32/34 dropped own WX and 54/57 dropped own HEY on OE3XOC were head for at least 5-10 s, so most
    would air. The entries that sat behind ACKs for their whole TTL still drop.
  - **Load:** extra channel load is small, about 7.6 own beacons/h on OE3XOC.
  - **Risk:** the known cross-node priority inversion of the floor
    (`docs/mesh-evolution-concept-20261021.md:179`) extends to prio 4/5.
  - **Test:** a native test can pin the min() against the 5500 base (`test_csma_timing`). Replay or bench on a
    busy node before release.
- **Alternative (b):** apply the case-A short search to own prio-4/5 slots after 8 s, for parity with relays.
  It touches the NBR code, so it is a larger change.
- **Not recommended as the fix:** raising the prio-4 or prio-5 TTL. It only extends the stall, and the HEY TTL
  rationale stands (`configuration_global.h:465-473`).

## Build caveat and open points

- **Build:** unknown. The 150-220 ms holds and case-A NEED lines prove that case A and `bNBRCANCEL` are active
  (inferred `--nbrrelay on` on OE3XOC, XIA, XWJ). TTL drops at exactly 120 s / 181 s match the HEAD constants.
- **Instrument limit:** `RX_TIMEOUT_FIRE wait=` shows only holds that fired, not every re-arm. The armed-hold
  claim for own heads rests on the code path plus the near absence of fires (1.25/min at 35 RX/min).
- **Open:** the night clustering of OE3XOC own-WX losses (24 of 26 lost between 21:44 and 07:24) is not
  explained. Hourly load (ACK forwards, relays, OnRxDone) is flat.
- **Not checked:** whether `OnRxError` (RX_ERROR, header without a valid frame) also re-arms the timer. If it
  does, own heads starve even more.
- **Scripts** (stdlib, paths via argv, run with `python3 -I`), in `rca_own_beacons_checks/`: `timeline.py`,
  `head_hazard.py`, `hold_hist.py`, `blocker_share.py`, `rx_gaps.py`.
