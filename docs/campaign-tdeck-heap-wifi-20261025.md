# Campaign: T-Deck Plus heap loss and WiFi starvation (TD-20, TD-21)

Started 2026-10-25. Node DK5EN-14 (T-Deck Plus, bench, `dk5en-14.local`, USB attached). Problem statement:
`~/Desktop/RCA-DK5EN-14-heap-wifi-20261024.md` (24 h meshlogger run, WiFi/console lost twice with free
internal heap below 30 KB). Operator scope: measure, reproduce, RCA, fix, re-test. No PR, no release.

## Bottom line

- **Root cause (TD-20), confirmed on the bench:** `persisted_msgs` (`src/t-deck/lv_obj_functions.cpp`) kept a
  RAM-only copy of every received message until reboot. With a 76-char test message the internal heap fell by
  ~500 B per message; after ~250 messages free internal RAM was 4.5 KB (largest block 1.5 KB), five 124 B
  allocations with caps `0x80c` (internal, DMA-capable: WiFi/lwIP buffers) failed and the IP path wedged.
- **Wedge (TD-21):** the station stays associated (`WL_CONNECTED`, RSSI -74, `[WIFI];link;up` heartbeat continues)
  but answers neither ARP nor ICMP. It did not recover in 20 min although free heap came back to 59 KB once dead
  connections released their buffers. `--wifidrop` (re-association) recovered it in 5 s. The existing watchdog only
  acts while not `WL_CONNECTED`, so nothing restarted the link (field: 47 min outage).
- **Fixes (`5b1e1da8`):** TD-20 stages messages only under `T_DECK_SPIFFS`; TD-21 adds a gateway ICMP liveness probe
  with reconnect / network reset / restart escalation.
- **Slow ping (WIFI-PS-01):** WiFi modem sleep, not heap and not BLE coexistence. Decision for the operator.

## Defects

| ID           | Defect                                                                                                   | State                                            |
| ------------ | -------------------------------------------------------------------------------------------------------- | ------------------------------------------------ |
| TD-20        | `persisted_msgs` RAM-only copy of every non-System message until reboot                                  | Fixed `5b1e1da8`, benched                        |
| TD-21        | `checkWifiPing()` returns early on `WL_CONNECTED`; a wedged IP path on an associated link never recovers | Fixed `5b1e1da8`, benched (`td21_bench`)         |
| TD-22        | Per-tab model ~650 B internal RAM per bubble, 50 per tab, tab count uncapped                             | Fixed `bae97fd1`, benched                        |
| WIFI-PS-01   | LAN ping 80-100 ms median from WiFi modem sleep; PS none needs BLE off (IDF aborts otherwise)            | Accepted (operator), guard `9e4d8781`            |
| WIFI-HEAP-01 | TD-21 esp_ping session held a 3 KB task for good: release heap gate failed (63884 B vs 69332 B)          | Fixed: raw ICMP pcb in the tcpip thread, benched |

## Measurements (logs and summaries in `tools/bench/runs/harness/`)

| Run                                                    | Build                        | Result                                                                                                                                                                            |
| ------------------------------------------------------ | ---------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Overnight field log 10-24 23:49 to 10-25 08:09         | release 10-24                | 43 messages, internal heap 67.1 -> 51.6 KB, no loss                                                                                                                               |
| `td20_drain1` (inject 1 msg / 3 s, BLE on)             | INSTRUMENT `b55a5a6a`        | Wedge at ~250 messages (08:50:23): ifree 4.5 KB, iblk 1.5 KB, afail 5 x 124 B caps 0x80c; no recovery 20 min                                                                      |
| `td20_drain1/wifidrop.log`                             | same, wedged node            | `--wifidrop` 09:12:43 -> connected 09:12:46 -> ping ok 09:12:48                                                                                                                   |
| `td20_drain2` (same stimulus, 400 messages)            | INSTRUMENT, TD-20 fix        | `persisted_msgs` 0; ifree 76.5 -> 41.6 KB plateau once the tab holds 50 bubbles; 1306/1315 pings; no watchdog action                                                              |
| `td20_ping_ab_fresh.summary` (BLE on)                  | INSTRUMENT                   | PS min p50 87-96 ms, PS max p50 102 ms; `--wifips none` -> `RESET_REASON=4 PANIC`                                                                                                 |
| `td20_ping_ab_noble.summary` (BLE off)                 | INSTRUMENT + `DISABLE_BLE`   | PS min p50 77-86 ms, PS none p50 6.2 ms (p90 47 ms); internal free 133.7 KB vs 78 KB with BLE                                                                                     |
| Ping vs heap during `td20_drain1`                      |                              | mean 137 ms at 77-30 KB free, 77 ms at 15-5 KB: latency does not follow the heap                                                                                                  |
| `td21_bench` (`--heapeat` to 204 B free, then release) | INSTRUMENT, final `5b1e1da8` | IP wedged 09:54:16; heap back to 71.8 KB, IP still dead; 3 failed probe rounds; `ipdead;reconnect` 09:56:11; ping ok 09:56:17 (outage 2 min 1 s vs no recovery in 20+ min before) |

## TD-21 design (`src/wifi_liveness.h`, `src/udp_functions.cpp`)

- Every 30 s while `WL_CONNECTED`: collect the previous round, run `wifi_liveness::step()`, start a new round only when
  no action follows. Round = 3 ICMP echoes to `WiFi.gatewayIP()`, esp_ping session created once (3 KB task stack, held
  permanently), re-created on a gateway change.
- Disarmed until the first reply after link up or a gateway change (a gateway that never answers ICMP never escalates).
- 3 failed rounds -> `[WIFI];watchdog;ipdead;reconnect` (disconnect + `wifiBegin()`); +3 -> `ipdead;reset` (existing
  stage-2 path, `startNetwork()`); +10 -> `ipdead;restart`. A round still pending at the second check counts as failed
  (wedged tcpip thread never fires `on_ping_end`).
- Known blind spot: a gateway change while the old round is wedged never escalates (logged in BACKLOG TD-21).
- Reviews: Opus advisor three rounds (stuck round, gateway change, round straddling our own action, session delete
  while busy, callback from an abandoned session); all items applied.

## Waves

| Wave | Content                                                                           | State                      |
| ---- | --------------------------------------------------------------------------------- | -------------------------- |
| 0    | Recon: injectmsg path, alloc-fail slot, WiFi PS/coex, watchdog, overnight log     | Done                       |
| 1    | INSTRUMENT `[INSTR-HEAP]`, boot-wide alloc-fail capture, `--wifips`, `--heaplog`  | Done `b55a5a6a`            |
| 2    | Bench: ping A/B, drain to wedge, `--wifidrop` recovery, BLE-off ping              | Done                       |
| 3    | Fix TD-20 + TD-21, host tests, `--heapeat` injector                               | Done `5b1e1da8`, gate PASS |
| 4    | Bench re-test TD-20 (drain2), TD-21 (`--heapeat`), release image back on DK5EN-14 | Done; 24 h soak running    |

## Wave 0 findings

- `--injectmsg` is INSTRUMENT-only; it reaches `msg_tabs_add_message()` with no dedup and lands on a tab named after
  the source call. One injection per main-loop flush.
- The heap failed-alloc callback has one global slot; `fw_update_net.cpp` arms it per update job.
- No runtime BLE stop on S3 (deinit crashes, AU-20).
- STAT format is pinned by `test/test_setlog_lines`; the golden `variant-ini-effective.json` pins the native test list.

## Open

- 24 h soak of the release build (09:40:55) on DK5EN-14: rpizero `screen td20-14`, output
  `~/meshlog/dk5en-14-td20/`, started 2026-10-25 09:59, ends ~2026-10-26 10:00. Evaluate STAT heap slope and
  `[WIFI];watchdog;ipdead` lines.

- TD-22 decision (bubble Strings to PSRAM or cap tabs).
- WIFI-PS-01 decision.
- Released in v4.40a.10.25; upstream since 2026-10-25 inside #1238 (all fork firmware, merged).

## Follow-up waves (2026-10-25, operator decisions)

Decisions: TD-22 = 16-tab cap with LRU eviction of inactive tabs plus a heap floor (below 40 KB free internal RAM
drop the oldest bubbles of the least recently used inactive tab; active tab keeps at least 10). WIFI-PS-01 = accept
and document; `--wifips none` (INSTRUMENT) refuses while the BT controller runs instead of panicking. Full control
over DK5EN-14 (soak interrupted for the bench).

| Wave | Content                                                                                                                     | Owner                | State           |
| ---- | --------------------------------------------------------------------------------------------------------------------------- | -------------------- | --------------- |
| 5-A  | TD-22: `msg_tab_policy.h`, tab cap + eviction, heap floor trim, active-index fix on tab removal, test `test_tdeck_msg_tabs` | implementer + rework | Done `bae97fd1` |
| 5-B  | TD-21b: gateway-change blind spot fixed, session bookkeeping in `wifi_liveness.h`, 4 new tests                              | implementer          | Done `ace935c6` |
| 5-C  | `--wifips none` BT guard, test list registration                                                                            | orchestrator         | Done `9e4d8781` |
| 6    | Bench: multi-tab drain (eviction, heap floor), TD-21 starvation re-run, GUI smoke; release image; soak                      | orchestrator         | Open            |

### Wave 6 bench results

| Run             | Build                      | Result                                                                                                                                                                                    |
| --------------- | -------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `td22_multitab` | INSTRUMENT, first TD-22    | 20 groups round robin: 16 tabs held, 146 evictions, ifree 65-67 KB, 0 pings lost                                                                                                          |
| `td22_floor`    | INSTRUMENT, reworked TD-22 | 5 groups, 500 messages: floor engaged 77x (409 bubbles), ifree 41.0-61.9 KB, afail 0, 0 pings lost                                                                                        |
| Reviews         |                            | TD-22: Opus rework (render on removal, use-after-free in the delete callback, view thrash, click recency, persisted iteration, System body cap) then approved; TD-21b approved first pass |
| `td21b_bench`   | INSTRUMENT, final          | Starvation dropped the link this time (status 6, reason 39): probe idle as designed, driver auto-reconnect failed 180 s, old watchdog stage 1 reconnected at 180 s, pings back 70 s later |
| Not covered     |                            | GUI visual check on the device (no camera on the bench); `T_DECK_SPIFFS` paths reviewed only                                                                                              |

Finding (BACKLOG TD-21): after heap starvation the driver's own auto-reconnect did not recover the link within 180 s,
while the watchdog's forced disconnect + `wifiBegin()` reconnected in 3.5 s. Shortening `WIFI_WD_GRACE_S` (180 s)
would shorten that outage; not changed.

Soak 2026-10-25: the 24 h soak (`screen td22-14`, started 11:18) was stopped at 11:24 on operator instruction for
the TXP-01 bench ([campaign-tx-pool.md](campaign-tx-pool.md) W1d). A new soak follows the pending T-Deck PR.

## WIFI-HEAP-01: probe without a task (2026-10-25, release gate)

- **Trigger:** the v4.40a.10.25 idle-heap gate on DK5EN-92 (release image, over 2323) read 63884 B against the
  69332 B baseline (-5448 B, tolerance 3072 B). Operator decision: shrink the probe before the release.
- **Fix:** the esp_ping session (task with 3 KB stack, held for good) is replaced by a raw lwIP ICMP pcb. The loop
  task queues the echo burst with `tcpip_try_callback()` (never blocks; no core locking in this IDF build); the
  receive callback runs in the tcpip thread and records the round only when the reply carries the sequence number
  of the round in flight. A wedged tcpip thread sends and answers nothing, so its rounds fail and the watchdog
  escalates. The pure session layer (busy/stuck/abandon) is gone; `wifi_liveness::Round` replaces it.
- **Review (independent agent):** lwIP usage and thread split confirmed. Applied: replies only to the current round
  count (a 16-bit serial compare would have gone stale after about 11 days of silence), at most one queued send.
  Accepted: the 3 echoes go out back-to-back.
- **Measurements:**

| Run                       | Build                   | Result                                                                                                         |
| ------------------------- | ----------------------- | -------------------------------------------------------------------------------------------------------------- |
| `heap-idle-v4.40a.10.25`  | release, esp_ping probe | DK5EN-92 STAT heap 64228 / 64228 / 63884 B: gate FAIL                                                          |
| `heap-idle-v4.40a.10.25b` | release, raw probe      | DK5EN-92 STAT heap 69264 / 69264 / 68728 B: gate PASS (-604 B); `[WIFI];probe;armed` 60 s after boot           |
| `td21c_bench`             | INSTRUMENT, raw probe   | DK5EN-14: wedge at 588 B free, heap released, 3 failed rounds, `ipdead;reconnect`, outage 2 min 0 s, no reboot |
