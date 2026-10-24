# Campaign: TX Ring to Priority Pool (TXP)

| Item      | Value                                                                                                                                                                            |
| --------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Started   | 2026-10-24                                                                                                                                                                       |
| Baseline  | `fork-dev` at `45978b7a`                                                                                                                                                         |
| Concept   | [`tx-pool-dedup-concept-20261021.md`](tx-pool-dedup-concept-20261021.md) (Part A), roadmap wave W1 of [`mesh-evolution-concept-20261021.md`](mesh-evolution-concept-20261021.md) |
| Mechanics | `/orchestrate-waves`; one commit per wave on `fork-dev`; upstream PRs later, one per stage                                                                                       |
| Status    | W0-W1c committed and benched; all five nodes on `3e07f894` since 2026-10-24 16:20; open: night evaluation (until 2026-10-25 16:21), TTL and release decision                     |

## 1. Goals (operator, 2026-10-24)

1. The TX buffer is no longer a ring: any free slot is usable, no dead capacity behind a parked slot.
2. Age-based cleanup: every priority has a time to live.
3. `{CET}` timing as a refreshing time service: **out of this campaign** (new behaviour, separate topic).
4. Delayed `{CET}` frames are dropped (sanitizer).
5. The WebGUI LoRa Queue gauge keeps measuring the fill of the TX buffer correctly.

## 2. Decisions

| #   | Decision                                                                                                                                                                                                                                                                                                                                     |
| --- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| D1  | `{CET}` age gate: a `{CET}` frame older than **150 s** (payload time vs. a trusted clock) is not relayed and not sent. Checked at gateway injection, at the relay decision and before TX. 150 s = half the combined beacon interval (two server sources, 10 min each, offset: one beacon about every 5 min). Constant, `INSTRUMENT`-tunable. |
| D2  | Only a trusted clock judges the age: GPS, NTP or RTC. A clock set from `{CET}` does not count. A node without a trusted clock does not relay `{CET}` (it still receives and applies it).                                                                                                                                                     |
| D3  | A newer `{CET}` replaces a queued older one regardless of source call; the newer payload time wins.                                                                                                                                                                                                                                          |
| D4  | TTL per priority (concept 5.1): ACK 90 s, own DM and own group text none (retry ladder governs), relay text 300 s, position 120 s, HEY 180 s (BP-03). `INSTRUMENT`-tunable; W0 data may adjust before the upstream PR.                                                                                                                       |
| D5  | Gauge option G3: the count is every slot holding a frame (queued, on air, waiting for ACK, external radio pending); the same number drives QRS/QRT and the "full" test. Own texts waiting for an ACK show as outlined cells in their priority colour, legend "n waiting for ACK".                                                            |
| D6  | RM1 `txq` keeps `q=n/cap` (total); its double count (`execTxq()` adds the total in `prio[0]` to the per-priority counts) is fixed with a regression test.                                                                                                                                                                                    |
| D7  | W0 may run on DK5EN-98: OTA flash of the production image approved 2026-10-24, after a bench node ran the same code.                                                                                                                                                                                                                         |

## 3. Waves

| Wave | Content                                                                                                                                          | Status |
| ---- | ------------------------------------------------------------------------------------------------------------------------------------------------ | ------ |
| W0   | Measurement: `{CET}` age helper + log lines, `stat_latency_max` to `uint32`, `RING_DROP_STALE prio=`; flash DK5EN-1, then DK5EN-98; logger night | done   |
| W1a  | TTL table for all priorities, `{CET}` age gate (D1, D2)                                                                                          | done   |
| W1b  | Slot pool: `INFLIGHT` status, delete `iRead`/`iWrite`; gauge G3 (D5); RM1 `txq` fix (D6)                                                         | done   |
| W1c  | Supersede in place: HEY, position, `{CET}` (D3)                                                                                                  | done   |
| W2   | Time-aged dedup (concept stage 3), dedup line of the gauge: later campaign                                                                       | parked |

## 4. Wave log

### W0 evidence from an existing capture (2026-10-18, DK5EN-98)

Source: rpizero `~/meshlog/dk5en-98-n20261018/2026-10-18.log`, 19:14 to 19:38 (24 min, loradebug on,
78 `RING_TX_READ`). Small sample; the logger night after the W0 flash replaces it.

| Prio | n   | Age at TX p50 | p90     | max     | Over proposed TTL |
| ---- | --- | ------------- | ------- | ------- | ----------------- |
| 1    | 17  | 6.2 s         | 33.6 s  | 42.3 s  | 0 over 90 s       |
| 2    | 31  | 13.1 s        | 31.9 s  | 92.5 s  | no TTL            |
| 3    | 27  | 150.1 s       | 374.4 s | 436.9 s | 5 over 300 s      |
| 4    | 2   | 10.0 s        | 192.8 s | 192.8 s | 1 over 120 s      |
| 5    | 1   | 103.5 s       | -       | 103.5 s | 0 over 180 s      |

Drops: `RING_DROP_STALE` 3, `RING_DROP_PRIO` 19, `RING_DROP_NEW` 19.

`{CET}` (three server beacons injected by the gateway, all `udp_rx`, prio 3):

| msg_id     | Fate                                                                              |
| ---------- | --------------------------------------------------------------------------------- |
| `6AD42561` | 440 s in the queue, then evicted by a user message (`RING_DROP_PRIO`), never sent |
| `6AD42562` | 305 s in the queue, then evicted by a retransmission, never sent                  |
| `6AD42563` | sent after 21.5 s queue wait; payload 17:37:41 UTC, TX 17:38:05 UTC: **24 s old** |

Reading: the server-to-gateway leg was about 3 s for the one beacon that went out; the queue wait
dominates the age. Without a gate a beacon can sit 5 to 7 minutes and still go out.

### W0 (commit `cee174b7`)

- `src/cet_age.h` (parser, trusted-clock predicate, `cetNowUtc()`), `[CET];gw_rx|rx|tx` lines,
  `stat_latency_max` uint32, `RING_DROP_STALE prio=`. Gate: Unity 2779/2779, golden 48 green.
- DK5EN-1 (GPS clock) flashed OTA, 11-min console capture: `[CET];gw_rx` age 1 s (server leg),
  `[CET];tx` age 6 s after 4.7 s queue wait, `[CET];rx` of a beacon from another island 33 s old
  (path 4, hop 0).
- DK5EN-98 flashed OTA with the same image (build `Oct 24 2026 / 12:03:48`, was Oct 19) on
  2026-10-24 12:28. Logger: rpizero `~/meshlog/dk5en-98-txp-w0/`, 24 h from 12:30 (meshlogger sets
  loradebug and txcapture). Evaluate: per-prio age at TX, `[CET]` ages, `RING_DROP_*` counts.

### W1a

- TTL per priority (`src/txring_ttl.h`, constants in `configuration_global.h`, build-flag
  overridable): only DONE slots age, except HEY (any status, BP-03 unchanged). Prio 1 ages only
  for ACK frames (binary 0x41, `:ackNNN`, WLNK `ackNNNN`); own send-once DMs (`{ping}`, RM1
  replies, mailbox delivery) and prio 2 never age (advisor follow-up, fixed with tests).
- `{CET}` gate at four points: 2 s sweep, CET-only sweep at the start of `doTX()`, relay decision
  (`[CET];drop;where;relay`, RLY reason `cet`), gateway injection (`[CET];drop;where;gw`).
- Field effect (D2, accepted): a node without GPS, NTP or RTC no longer relays `{CET}`; reach of
  the beacon shrinks where such nodes bridge.
- Deviation: tunability is by build flag, not a runtime `--txttl` command.
- Note: `tools/tests/test_mesh_policy_sites.py` pins `"rx_relay"` within 8000 chars of
  `checkMesh(aprsmsg)`; the W1a relay gate leaves little margin, so later edits in that block
  should stay small or move code into helpers.
- Advisor (Opus): APPROVED; three follow-ups applied (prio-1 scope, prio-2 DONE test, `cet` row in
  `how-to-read-meshcom-logs.md`).

### W1b

- Slot pool replaces the ring: free = len 0, capacity 20 (was 19), selection by priority then
  oldest enqueue time, eviction takes the oldest of the worst priority and never touches
  `INFLIGHT` (0x81) or `EXT_PENDING`. `iRead`/`iWrite`, `advanceIReadPastEmpty()`, the N-24
  relocation and `RING_OVERFLOW buf=tx` are gone; `nrf_eth.cpp` routes through `addTxRingEntry()`.
- `doTX()` claims the picked slot right after `getNextTxSlot()` (`txRingBeginTx()`, locked, with an
  enqueue-time snapshot so an nRF52 evict-and-refill in between is detected) and ends it with
  `txRingFinishTx()` (sent, rollback, drop). Loop TX gates use `getNextTxSlot() >= 0`: a parked own
  text waiting for its ACK no longer keeps the CAD cycle running.
- Gauge G3 (D5): `txRingAckWaitCounts()`, `data-w0..w5`, outlined cells, "n waiting for ACK",
  wording "TX queue" / "Queue:". RM1 `txq` counts `prio[0]` (D6, test failed 10 vs 5 before).
- Before/after proof of the hole problem: the same probe (full queue, parked head, two holes)
  fails on `05a18b36` ("enqueue refused although holes exist") and passes on the pool.
- Log changes: `RING_STATUS` lost `iW=`/`iR=`/`dist=`; `preempt=` now means "the pick was not the
  oldest by enqueue time". `RING_ZOMBIES` in `tools/loganalyse.sh` and `tools/serial_monitor.py`
  no longer fires on pool logs (concept obsolete in a pool).
- Advisor (Opus): REWORK (R1 loop gate, R2 claim race), applied by the orchestrator with tests;
  re-check found one nRF52 hole (async CAD: the gate also stays open while `cad_in_progress` or
  `cad_done_flag` is set, so a stale CAD result is consumed, never reused), fixed as proposed.
- Open, next wave: a retry in a full pool can still lose the message (`updateRetransmissionStatus()`
  enqueues the copy before freeing the original; pre-existing). Fix: rearm the parked slot in place.

### W1c

- Supersede in place (D3): a new HEY or position replaces a queued READY/DONE slot with the same
  type, priority, originator (first path token) and destination (`H`, `HG`, `HN` stay apart); a
  relayed `{CET}` (DONE, prio 3) replaces a queued one when its payload time is newer or equal,
  else the new one is dropped (`RING_SUPERSEDE_KEEP`, RLY reason `full`). A user text that begins
  with `{CET}` is never treated as a beacon. `stat_supersede_count` counts replacements (not yet in
  STAT or the web page). Trade-off (concept 10.6): a replaced relayed HEY loses its report chain.
- Retry rearm: `updateRetransmissionStatus()` rewrites the parked slot in place
  (`txRingRearmRetry()`, re-check status 0x15 and length under the lock), so a full pool no longer
  loses the message; `RING_WRITE src=retransmit` became `RING_REARM`.
- Tests: 11 W1c cases (8 failed before) plus 2 advisor regressions (HEY vs HN, user `{CET}` text;
  both failed before). Advisor (Opus): REWORK (destination match, CET relay-only), applied by the
  orchestrator.

### Bench 2026-10-24 (INSTRUMENT images of `1f5521d9`, legend fix `eda5edb8` on DK5EN-1)

Logs in `tools/bench/runs/harness/` (raw, gitignored): `txp-w1c-dk5en1-rearm3.log`,
`txp-rak-pool.log`, `txp-cet-mock-92.log`.

| Check                       | Node     | Result                                                                                        |
| --------------------------- | -------- | --------------------------------------------------------------------------------------------- |
| Gauge G3 on hardware        | DK5EN-1  | DM waiting for ACK: one outlined crit cell, "1/20 queued", legend "bg 0, 1 waiting for ACK"   |
| ACK path on the pool        | DK5EN-1  | DM to DK5EN-92: server ACK `:ack306` after 4 s stops the ladder                               |
| Retry rearm, ESP32          | DK5EN-1  | DM to offline DK5EN-93: 3 x `RING_REARM` in slot 0, new retry ids, 40 s apart, `giveup=1`     |
| Retry rearm, nRF52          | DK5EN-90 | same, 3 x `RING_REARM` in slot 0; a WX position in slot 1 sent while slot 0 waited; no fault  |
| `{CET}` gate at the gateway | DK5EN-92 | mock server, airgap: 200 s old and 200 s future dropped (`[CET];drop;where;gw`), fresh queued |
| `{CET}` supersede           | DK5EN-92 | newer beacon replaces the queued one (`RING_SUPERSEDE`), older one dropped (`..._KEEP`)       |
| Clock for the gate          | DK5EN-92 | `cetNowUtc()` within about 1 s of the Mac clock                                               |

Not run: the relay-side `{CET}` gate on air (bench nodes run `--mesh off`; covered by native tests)
and a 1 h gwflood replay.

### Fleet flash and logger night 2026-10-24 (gate build of `3e07f894`, TXP + BLK-01)

Gate before the flash: `tools/regression.sh --stage 1,2` PASS (80 Unity envs, 2861/2861), all 32 `default_envs`
built as production images (no INSTRUMENT, no build tag), `make_autoupdate.py --check` 27/27 app images. Images
string-checked for `RING_SUPERSEDE` (TXP) and the `--blockcall` help text (BLK-01).

| Node     | Board       | Method                                   | Build                    |
| -------- | ----------- | ---------------------------------------- | ------------------------ |
| DK5EN-1  | Heltec V3   | OTA `.autoupdate` (1.20->1.89 MB)        | `Oct 24 2026 / 16:01:45` |
| DK5EN-92 | T-Beam      | OTA `.autoupdate` (1.26->2.02 MB)        | `Oct 24 2026 / 16:07:01` |
| DK5EN-14 | T-Deck Plus | OTA `.autoupdate` (1.61->2.51 MB)        | `Oct 24 2026 / 16:09:55` |
| DK5EN-90 | RAK4631     | USB DFU (no WiFi OTA on nRF52)           | `Oct 24 2026 / 16:11:33` |
| DK5EN-98 | Heltec V3   | OTA `.autoupdate`, same image as DK5EN-1 | `Oct 24 2026 / 16:01:45` |

W0 baseline: the W0 logger was stopped with SIGINT at 16:19 (flags restored), `~/meshlog/dk5en-98-txp-w0/`
holds 12:30-16:19 (57 808 lines) of the W0 image, plus the 10-18 sample above.

Loggers on rpizero, detached `screen` sessions (survive the SSH session, not a rpizero reboot), 24 h from
2026-10-24 16:21: `txp-98` -> `~/meshlog/dk5en-98-txp-w1c/`, `txp-14` -> `~/meshlog/dk5en-14-txp-w1c/`
(stdout in `~/meshlog/run-txp-w1c-98.out`, `run-txp-w1c-14.out`). Evaluate against W0: per-prio age at TX,
`[CET]` ages and drops, `RING_DROP_*` / `RING_SUPERSEDE*` counts, full events, gauge counts.
