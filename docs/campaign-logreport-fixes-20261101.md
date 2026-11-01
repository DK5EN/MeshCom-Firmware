# Campaign: Fixes from the 24-h log analysis 2026-11-01 (LRF)

| Item      | Value                                                                                                                                                                                                        |
| --------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Started   | 2026-11-01 (plan only, nothing implemented)                                                                                                                                                                  |
| Baseline  | `fork-dev` at `e644e21c`. Code facts below were read at this commit                                                                                                                                          |
| Evidence  | [`log-reports/2026-11-01-24h/data/CORE-FINDINGS.md`](log-reports/2026-11-01-24h/data/CORE-FINDINGS.md), `verify/*.md`, `uebersicht.html`                                                                     |
| Mechanics | `/orchestrate-waves`; one commit per wave on `fork-dev`; Opus advisor gate before every commit; upstream PRs later, one per item                                                                             |
| Status    | W0-W2 done, W3 bench done 2026-11-01 (section 7). Items 1, 2, 5 upstream as one PR #1247 (operator choice). Next: release candidate (operator go), then the 24-h field capture and `tools/logreport/` re-run |

Line numbers drift: `src/` changed in 9 files between the analysis commit `651e0920` and `e644e21c`. Every brief
must `grep -n` the named symbol before trusting a line number in this document.

## 1. Items and outcome

| #   | Finding (report wording)                        | Operator decision 2026-11-01                           | Section | Work           |
| --- | ----------------------------------------------- | ------------------------------------------------------ | ------- | -------------- |
| 1   | The ping target is not validated                | Callsign targets only; lock every path in the firmware | 2.1     | W1a            |
| 2   | Own beacons starve at busy nodes                | Own beacons get the priority of an own group message   | 2.2     | W1b            |
| 3   | ACK forwards have no overhear-cancel            | Accepted as is. No cancel for binary ACKs              | 2.3     | none           |
| 4   | `{CET}` server slots are always "sole provider" | Accepted as is. A sole provider sends; no change       | 2.4     | none           |
| 5   | OE3XIA is deaf at times                         | Extend the firmware logging                            | 2.5     | W0-M5, W1c, W2 |

Campaign goal, unchanged from the analysis: a stable network, nothing builds up, minimal double transmissions,
nothing lost, maximum useful LoRa airtime. Every claim is backed by a measurement and checked adversarially.

## 2. Analysis, decisions and proposals

### 2.1 Ping target not validated (item 1): lock it

**What happens (measured):**

- OE3XIR-12 has `node_pingcall = "255."`. Every 60 s the auto-ping writes a `{ping}` frame to the TX pool (688 of
  3307 ring writes, 20.8 %). The frame wins the prio-1 pick, costs one CAD cycle (689 `CAD_FREE_NO_TX`) and is
  dropped at decode ("Destination-CallSign Error [255.]"). None was aired.
- OE3XWJ-12 pings group `2025` every 60 s (1529 pings). These are aired. Foreign gateways ACK them (about 2.9 ACK
  ids per ping), and 97.9 % of all ACK forwards in the cluster belong to those ACKs: 8727 forwards, 41.3 % of
  cluster TX count, 20.2 % of cluster TX airtime.

**Can a group be pinged today?** Officially no, in practice yes, over five paths (code at `e644e21c`):

| Path                                                                                            | Checks the target today?                                                                                                        | Group or junk gets through? |
| ----------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------- | --------------------------- |
| Console / web command `--pingcall <x>`                                                          | Yes: `checkRegexCall()` (since `be216750`, 2026-07-24). The regex (`src/regex_functions.cpp:9`) needs a letter after the digits | No (`2025`, `255.` refused) |
| JSON config import (`src/config_json.h`, `node_pingcall`)                                       | No (`CFG_STR`, `CFG_NORANGE`)                                                                                                   | Yes                         |
| nRF52 BLE settings v1 import (`src/nrf52/ble_settings_v1.cpp`)                                  | No (`memcpy`)                                                                                                                   | Yes                         |
| Settings stored by an older build, loaded at boot                                               | No: `sanitize_loaded_settings()` only checks the terminator                                                                     | Yes                         |
| Manual text `{ping}` from app, console, web, T-Deck (`sendMessage()`, `src/loop_functions.cpp`) | No: any destination, including a group or `*` (the default when no `{ZIEL}` is given). Sent once (`addTxRingEntryOnce()`)       | Yes                         |

- A group can never answer: only a node whose own call is the destination sends `{pong}`
  (`src/lora_functions.cpp`, `strcmp(destination_call, meshcom_settings.node_call) == 0` branch). A group ping
  measures nothing and only draws gateway ACKs.
- How OE3XWJ got `2025` and OE3XIR got `255.` is unknown (field build unknown). One of the four unchecked paths,
  or a build from before 2026-07-24.
- Even on the checked path, `checkRegexCall()` lets the special targets `*`, `H`, `HG`, `TEST`, `TESTER`,
  `BOT GATE`, `WLNK-1`, `APRS2SOTA`, `OE2YOTA-1` through.

**Fix (D1, decided): one rule, enforced on every path.**

1. **The rule:** new pure header `src/ping_target.h` with `pingTargetValid(const char *call, const char *own_call)`.
   - Accepts a callsign that passes `checkRegexCall()`, with an optional SSID of 1-2 digits and no leading zero (same
     SSID rule as `checkViaList()`).
   - Rejects groups (all-digit targets, `CheckGroup() != 0`), every special target listed above, and the node's own
     callsign.
2. **Console / web setter:** the `"pingcall "` block in `src/command_functions.cpp` uses `pingTargetValid()` instead of
   `checkRegexCall()`. Error text names the reason (`group`, `special target`, `own call`, `not a callsign`).
3. **Boot sanitize:** new `sanitize_ping_target()` in `src/settings_sanitize.h` / `.cpp`. It clears an invalid target and logs
   `[FLASH]...sanitized node_pingcall: <old> -> ""` once. Called from both `sanitize_loaded_settings()` (ESP32
   `src/esp32/esp32_flash.cpp`, nRF52 `src/nrf52/nrf52_flash.cpp`).
4. **Imports (W0-R1):** the JSON config import (`configImportJson()`, web) saves and reboots, so the boot sanitize
   covers it (except ESP32 `MC_SAFEBOOT`, which skips the keyed load). The nRF52 BLE v1 legacy blob load is
   sanitized at boot. **Bypass:** the nRF52 live BLE write `applyPendingBleSettings()` (`src/nrf52/nrf52_ble.cpp`)
   calls `bleSettingsFromV1()` and `save_settings()` without reboot or sanitize: W1a adds `sanitize_ping_target()`
   there. The ESP32 `sanitize_loaded_settings()` does not touch `node_pingcall` at all today.
5. **Last guard in `sendPing()`:** an invalid target returns a new `PING_INVALID_TARGET` before the msg_id advances
   and before any pool write. It logs `[PING]...invalid target <x>, ping not sent` once per episode (same static-flag
   pattern as the TRACK notice). ESP32 loop: `node_pingcount` is not decremented for this result. nRF52 loop: no
   per-interval "FAILED" line for it (orchestrator edit at the gate, hotspot carve-out).
6. **Manual `{ping}` text:** `sendMessage()` refuses a text starting with `{ping}` whose destination fails
   `pingTargetValid()`, with `BP_SEND_INVALID` and `[ERROR]...{ping} needs a callsign target`. Same place and
   pattern as the existing "DM to own call" refusal, before any msg_id is minted.

**Regression tests (fail before, pass after):**

- `test/test_ping_target` (new, env `native`): `255.`, `2025`, `*`, `TEST`, `HG` and the own callsign rejected;
  `OE3XIR-12` accepted; `OE3XIR-05` rejected. "Before" run against `checkRegexCall()`: must fail on at least
  `*`, `TEST`, `HG`.
- `test/test_settings_sanitize` (extend): a loaded `255.` and a loaded `2025` are cleared, a valid call stays.
- `sendPing()` guard and manual `{ping}` refusal: no native env compiles `loop_functions.cpp` (W0-R1), so source
  pins in `test_command_setters` style (`read_repo_file()`). The `{ping}` guard sits after the `{ZIEL}` parse (a
  bare `{ping}` parses as destination `PING`), next to the own-call refusal, before the msg_id is minted.
- `test/test_ping_target` needs an entry in the `[env:native]` `test_filter` allowlist (`platformio.ini`).

**Network impact:** OE3XIR saves 688 dead pool writes and 689 CAD cycles per day. Once OE3XWJ's owner flashes the
build, its 1529 group pings and their 8727 ACK forwards per day stop: the largest single airtime gain in this
campaign, and the ACK backlog that starves own beacons (2.2) shrinks with it.

**Not in scope, recorded:** the auto-ping count difference ESP32 vs RAK4631 (`node_pingcount` decremented only on
ESP32; `--ping stop` has no effect on RAK). Operator decision still open (CORE-FINDINGS item 1).

### 2.2 Own beacons starve at busy nodes (item 2): the age floor explained (D2 open)

**What happens (measured, OE3XOC-12 and OE3XIA-12):**

- Own HEY aired in 41.8 % (OC) / 78.8 % (IA) of episodes.
- Own WX lost to the TTL: OC 34/49, IA 11/49, IR 5/22, WJ 4/50, HL 0/49. OE3XOC sent no own WX for 7.64 h
  (31.10. 21:14 - 01.11. 04:52).
- Relays of the same priority and type get through: relay positions lost 1.1 %, relay HEY 1.4 % (OC).
- TX duty at OC is 4.2 %: the node has airtime to spare; it lacks channel access.

#### How a node gets onto the channel today

After every frame it **receives**, a node re-arms a hold timer for the frame at the head of its TX pool. It may
send only after a silence as long as that hold. Every further received frame restarts the timer
(`OnRxDone`, `src/lora_functions.cpp`). The hold per priority (`src/configuration_global.h`):

| Prio | Class                   | Base hold | Jitter       | Escape when waiting long                                 |
| ---- | ----------------------- | --------- | ------------ | -------------------------------------------------------- |
| 1    | ACK, own DM             | 3000 ms   | 0-10 x 35 ms | age floor                                                |
| 2    | group, broadcast        | 3000 ms   | 0-10 x 35 ms | age floor                                                |
| 3    | relay text, server text | 4500 ms   | 0-10 x 35 ms | age floor                                                |
| 4    | position, WX            | 5500 ms   | 0-10 x 35 ms | relays only: NBR case A, 150 ms after 8 s. **Own: none** |
| 5    | HEY                     | 5500 ms   | 0-10 x 35 ms | relays only: NBR case A, 150 ms after 8 s. **Own: none** |

OE3XOC hears about 35 frames per minute; the median gap between two received frames is 1.42 s (W0-M2, all 43749
frames; the 3.48 s of the first draft counted only the `OnRxDone` print lines). A silence of at least 5.8 s follows
only 4.5 % of receptions, so an own beacon at the head waits and waits. Measured: an own beacon at the head goes out 0.44-0.63 times per minute,
a relay at the head 13-16 times per minute. The TTL (position 120 s, HEY 180 s) then drops the own beacon.

#### What the age floor is

The age floor ("Brief G" of the LoRa-QRT campaign, 2026-10-18) shortens the hold for a frame that has waited long.
"Age" is the time since the frame was put into the pool (`ringEnqueueTime`). Code: `csmaApplyAgeFloor()` and
`csmaAgedHoldMs()` in `src/csma_timing.h`, applied in `csma_compute_timeout_slot()` (`src/lora_functions.cpp`).

| Stage | From age | Hold after a received frame                                                | Why this value                                                                                                                                        |
| ----- | -------- | -------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------- |
| 0     | 0 s      | normal hold (table above)                                                  | -                                                                                                                                                     |
| 1     | 20 s     | `2850 ms - own airtime - 175 ms`, clamped to 500-1500 ms, plus 0-5 x 35 ms | The receivers of the frame just heard may answer with an ACK after 3.0 s. The aged frame must be off the air by 2850 ms so it does not hit those ACKs |
| 2     | 60 s     | 100 ms plus 0-5 x 35 ms (100-275 ms)                                       | The same window rapid-fire CAD already uses after 3 failed attempts                                                                                   |

Three properties matter:

- **Never after an own TX.** The post-TX hold (`csma_reset()`) ignores the age. A node cannot capture the channel
  with back-to-back frames; it always gives way to one foreign frame first.
- **Within one node, strict priority is unchanged.** An ACK at the head still goes before any beacon. The floor
  only changes how long the head frame waits after a foreign frame.
- **Today prio 1-3 only.** The QRT plan said "prio 4-5 never escalate, they age out"
  (`docs/meshcom-lora-qrt-fix-plan-20261018.md`). The NBR case A already escalates prio-4/5 relays, so own beacons
  are the only class without an escape. That is the design gap.

**Measured effect of the floor so far:** QRT replay 4.0 -> 11.2 sends per minute for the starving head
(`docs/mesh-evolution-concept-20261021.md`). In this capture 13709 `CSMA_AGED` events (about 3670 slot episodes)
and no collision signature: RX errors after an aged TX are as frequent as after a normal one.

#### What extending it to own beacons would do

```
age of an own WX (TTL 120 s)
0 s ............ 20 s ...................... 60 s ...................... 120 s
| hold 5.5 s     | hold 0.5-1.7 s              | hold 0.1-0.3 s            | TTL drop
| (as today)     | stage 1                     | stage 2                   |
```

- **0-20 s, nothing changes.** Relays and ACKs keep their lead.
- **From 20 s:** after each received frame the beacon needs a silence of at most about 1.7 s instead of 5.5-5.9 s.
  At OE3XOC about 26-29 % of gaps qualify (against 1.5-4.5 % today). That is enough: a beacon at the head sees
  17-21 re-arms, so one qualifying gap comes soon.
- **Typical life of a lost own WX at OE3XOC:** about 70 % behind prio-1 ACKs, 28 % at the head (median 32 s).
  It reaches the head already older than 20 s, often older than 60 s. The floor then acts at the first gap.

**Variants for D2:**

| Variant                            | Change                                                          | Expected gain                                                                     | Risk                                                                                                                                                           |
| ---------------------------------- | --------------------------------------------------------------- | --------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| A1. Same stages as prio 1-3        | Prio 4/5, non-relay slots: stage 1 at 20 s, stage 2 at 60 s     | Highest                                                                           | Stage 2 puts beacons into the same 100-275 ms window as aged ACKs and relays of other nodes: hidden pairs collide there. Cross-node priority inversion (below) |
| **A2. Stage 1 only (recommended)** | Prio 4/5, non-relay slots: stage 1 at 20 s, never stage 2       | Close to A1 at a busy node: a 1.7 s gap is common (W0-M2 measures the difference) | Inversion only against holds of 3000 ms or more, never in the rapid window                                                                                     |
| A3. Later start                    | Stage 1 at 40 s for prio 4/5                                    | Lower: the first 40 s of a position's 120-s TTL stay blocked                      | Lowest                                                                                                                                                         |
| B. Case-A parity                   | Own prio 4/5 take the 150 ms short search after 8 s like relays | Highest                                                                           | Own beacons compete with relays in the 150-220 ms window; touches NBR code                                                                                     |
| C. Priority ageing in the pick     | An old own beacon may overtake ACKs in `getNextTxSlot()`        | Also fixes the "behind ACKs" part                                                 | Changes strict priority; ACK latency rises (max today 64.6 s vs 90-s TTL)                                                                                      |
| D. Longer TTL                      | Per-class TTL                                                   | None: waits longer, sends just as rarely                                          | Stale positions; rejected                                                                                                                                      |

**Cross-node priority inversion, in numbers:** an own beacon at node X in stage 1 may send 0.5-1.7 s after a frame.
A neighbour Y holding a fresh ACK waits 3.0 s after the same frame. X goes first, Y's ACK waits one more beacon
airtime (about 0.7 s). This is bounded by the beacon cadence: OE3XOC wrote 49 own WX and 141 own HEY in 25 h, about
7.6 per hour.

**Network cost:** measured own airtime is about 0.95 s per WX/position and 0.44 s per HEY (W0-M2, verified within
4 %). Under A2 the recovered beacons at OE3XOC and OE3XIA cost about 94 s per 25 h, +1.5 % of their own TX
airtime, about 266 s on air including the measured relays.

**W0-M2 replay (OE3XOC + OE3XIA, 25 h, 142 own beacons lost to the TTL; verified, `verify/lrf_m2_verdict.md`):**

| Variant | Recovered (M2) | Verifier bracket | Extra own airtime | TX in the 100-275 ms window from the floor |
| ------- | -------------- | ---------------- | ----------------- | ------------------------------------------ |
| A1      | 128.9          | 126-134          | 101 s             | 105-116                                    |
| A2      | 118.6          | 110-129          | 94 s              | 0                                          |
| A3      | 112.2          | 102-121          | 86 s              | 0                                          |

- A2 per node: OE3XOC WX 24.8 of 34, HEY 47.9 of 57; OE3XIA WX 9.8 of 11, HEY 36.1 of 40.
- Model error about 20 % (baseline replay matches measured aired counts only moderately). It sizes the gain
  reliably but cannot separate A1 from A2 (4-12 % apart). A1's extra over A2 is not measurable; its rapid-window
  TX are certain.
- Measured relays per aired own beacon: OE3XOC WX 0.87, HEY 1.83; OE3XIA WX 1.95, HEY 2.57 (lower bound, 6
  loggers).

**Network gain:** busy gateways appear on the map again (position, WX), and their HEY reaches the neighbours, so
the surrounding neighbourhood matrices learn about them. Quiet nodes (OE3XHL: 0/49 lost) see no change.

**Two side effects to know:**

- HEY supersede resets the age (`ringEnqueueTime`, `src/txring_functions.cpp`). A HEY replaced by a newer one starts
  at 0 s again. This is fine for freshness, but it delays the floor for HEY chains.
- The prio-1 ACK backlog stays: entries that sit behind ACKs for their whole TTL still drop. Item 1 (no more group
  pings) removes most of that backlog in this cluster.

**Decision D2 (operator, 2026-11-01, after W0): own beacons are treated like an own group message.** An own
position, WX or HEY is classified as prio 2 (`MSG_PRIO_HIGH`) instead of 4/5; nothing else changes. The age floor
is not touched. Relayed beacons stay at prio 4/5. Consequences, all inherited from prio 2:

- In the own pool an own beacon goes before relay text and relayed beacons, still behind ACKs and own DMs (prio 1).
- Hold 3000 ms after a received frame instead of 5500 ms, and the existing age floor applies (stage 1 from 20 s,
  stage 2 from 60 s), as for an own group message.
- Pool-full eviction treats it like prio 2.
- Kept as today (not a prio-2 property): the TTL by frame type (own position/WX 120 s, own HEY 180 s, BP-03) and
  HEY supersede. Prio 2 has no TTL, so `txRingTtlMs()` must keep the beacon TTL for these entries.

The variant analysis below (A1-A3) is the record that led to the decision.

**Recommendation before D2 was taken: A2.** It is one condition, it touches no NBR code, and it never enters the rapid window.
Re-measure after item 1 is in the field; take C only if own beacons still sit behind ACKs.

**Regression test (W1b, after D2):** `test/test_txring` (env `native_aprs`): an own position, own WX and own HEY
classify as prio 2 (fail before: 4/5); the same frames as relays (status DONE) stay 4/5; own beacon TTL stays 120 s
/ 180 s (not "no TTL"); in a pool with relay text, relayed beacons and an own beacon, `getNextTxSlot()` picks the
own beacon before the relays and after an ACK.

### 2.3 ACK forwards have no overhear-cancel (item 3): accepted, no change (D3)

**Which ACK:** the binary ACK, the 12-byte frame `0x41` (optional 3-byte appendix with the originating gateway's
hash), forwarded in `src/lora_functions.cpp` as `addTxRingEntry(..., "rx_ack_fwd", 0)` with `RING_KIND_OTHER`. The
text ACK `:ackNNN` is an ordinary `:` message and already takes the relay path with the overhear-cancel scan.

**Decision (operator, 2026-11-01): binary ACK forwards are not cancelled. This behaviour is accepted.**

- **Short and cheap:** a binary ACK forward costs 0.31 s of airtime on average in this capture (2735 s for 8727
  forwards), against 0.87 s for the average other frame.
- **High value:** an ACK stops the originator's retry ladder. A lost ACK makes the originator resend a longer DM,
  and the DM is relayed again. Cancelling a cheap frame risks expensive ones.
- **No path:** a binary ACK names no forwarder, so the neighbourhood matrix cannot tell whom a heard copy covers.
  A cancel after a fixed number of heard copies would be blind, and the risk of losing the ACK is too high.
- **The ACK load is handled at its source:** 97.9 % of the ACK forwards in this capture answer OE3XWJ's group
  pings, which item 1 locks out.

### 2.4 `{CET}` server slots always "sole provider" (item 4): accepted, no change (D4)

**What it means:**

- A gateway gets the `{CET}` time telegram from the server and queues it. The neighbourhood matrix asks whether
  some neighbour can get the frame only from this gateway (the "alone set").
- If yes, the gateway is the sole provider (case A): the slot is never cancelled, even after other gateways have been
  heard radiating the same telegram.
- For `{CET}` the answer is always yes: 666 of 666 NEEDs in OE3, 349 of 349 at DB0AIS, 0 CANCEL. OE3MIF-12 and
  OE3NRA-1 sit in the alone set in 95.6-100 % of NEEDs.
- The cost: about 20 radiations per telegram (5 own gateways, about 6 foreign, about 9 relays), about 15 s airtime
  every 607 s, about 2.5 % of channel time.

**Decision (operator, 2026-11-01): no change. If the matrix says a gateway is the sole provider for a neighbour,
the gateway sends.**

- The veto is the guarantee that no neighbour is left out. It stays unconditional.
- Counting heard copies is not wanted. It would replace the matrix's per-neighbour answer with a guess.
- The other options considered (unknown hearers do not veto, GW-held neighbours as future providers, veto release on
  a heard copy, server-side reduction) are not pursued in this campaign.
- The redundancy is the price of the guarantee and is recorded as known behaviour in the report.

### 2.5 OE3XIA deaf at times (item 5): extend the firmware logging (D5)

**Measured (verify/health.md, all receptions counted, ACKs included):**

- 35 silences longer than 5 min, 15620 s in total (17.4 % of the capture), the longest 1024 s
  (31.10. 11:49:01 - 12:06:05). The first started 31.10. 08:26.
- Not a logger drop: largest line gap inside the silences 4.9 s, millis continuous. The firmware itself counts
  `CHANNEL_UTIL rx=0` in all 1511 samples inside the silences.
- The band was not quiet: the other OE3 nodes kept receiving (37-582 receptions per window), including the usual
  OE3XIA neighbours.

**Why the RF path (antenna, coax, wind) is unlikely on the current data:**

| Observation inside the silences                                               | Loose coax / wind would give | Stuck receive state would give               |
| ----------------------------------------------------------------------------- | ---------------------------- | -------------------------------------------- |
| CAD saw the channel busy in 161 of 218 scans (73.9 %), same antenna path      | CAD fails too                | CAD works (separate radio command)           |
| Own TX heard by at least one other logger: 40 of 57 (70 %)                    | Weak or lost TX              | TX works                                     |
| 30 of 35 silences end within 3 s after a Standby+CAD cycle (base rate 20.6 %) | No link to a radio command   | Standby resets the receive state             |
| The ~4.8 s watchdog re-issues `Radio.Rx()` without Standby and never recovers | -                            | Fits: Rx() without Standby does not reset it |

**Was anything sent during the deaf phases, and was it heard?** Yes: 57 TX in 15620 s (13/h against 160/h
normally; with nothing received there is nothing to relay). 40 of the 57 were heard by at least one other logger
with OE3XIA-12 as last hop. W0-M5 adds the base rate outside the silences and the RSSI/SNR at the neighbours inside
vs outside. Equal values mean the antenna path is fine: the direct coax test. It also correlates the silences with
wind from weather stations near Exelberg.

**W0-M5 result (verified, `verify/lrf_m5.md`, `verify/lrf_m5_verdict.md`): no RF-path signature.**

- Neighbours hear OE3XIA-12 as last hop at the same SNR inside and outside the silences (medians within 1 dB,
  OE3XHL n=34/1369, OE3XIR 10/319, OE3XOC 27/987; p >= 0.055). Last-hop attribution checked via the LH byte.
- Heard share of non-ACK TX: 40/44 (90.9 %) inside vs 1710/2031 (84.2 %) outside, p = 0.30. The 40/57 above mixes
  in 13 ACK TX that carry no path.
- CAD busy share 73.9 % inside vs 82.7 % outside; the other OE3 nodes drop the same way in those hours.
- CAD uses the same RX chain, so a front-end loss large enough to silence every frame would show in CAD: a gross
  LNA/switch fault is unlikely too.
- Further stuck-state signs: the periodic RX restart keeps firing inside silences (775/h vs 842/h) and never
  restores reception; 12 of 35 silences end 0.4-2.6 s after an own TX (chance about 0.4).
- Wind: not testable, mcmap holds no wind or gust field. Earlier capture (2026-09-01, 8 h night, older build): one
  300.5 s gap only, against 10 silences in the same window on 31.10.
- Open: chip-internal stuck state vs firmware RX arming / RxDone IRQ fault. Only the RX_SILENCE snapshot separates
  them.

**What the logs cannot tell today:**

- Whether the chip is still in RX mode during a silence (`SX126xGetOperatingMode()`, `SX126xGetStatus()`).
- Whether IRQ flags are set but not serviced (`SX126xGetIrqStatus()`), or the chip reports errors
  (`SX126xGetDeviceErrors()`: PLL lock, calibration, XOSC).
- Whether RF energy reaches the receiver (`SX126xGetRssiInst()`: a noise-floor reading means it listens;
  -127 dBm or a frozen value means the front end is dead).
- What ended the silence (CAD, own TX, watchdog, a real reception).

All five calls exist in the nRF52 library (`.pio/libdeps/wiscore_rak4631/SX126x-Arduino/src/radio/sx126x/sx126x.h`),
but `SX126xGetOperatingMode()` returns the driver's cached mode, not a chip read (W0-R2). The `mode=` field must come
from the chip-mode bits of `SX126xGetStatus()` (0xC0); the cached value may be printed as `drv=` for contrast.

ESP32 RadioLib 7.6.0 (W0-R2): `getIrqFlags()` and `getRSSI(false)` are public and read-only; `getStatus()` and
`getDeviceErrors()` are protected, no chip-mode getter. Both are reachable through the public
`Module::SPIreadStream(0xC0 / 0x17, ...)` if the firmware keeps the `Module*` it passes to the `SX1262` constructor
(`src/esp32/esp32_main.cpp`). ESP32 CAD is blocking `radio.scanChannel()`; the RX restart is the software timeout
poll in `esp32loop()`.

**Logging design (W1c nRF52, W2 ESP32 parity):**

- New pure tracker `src/rx_silence.h`: fed with "RX event" (OnRxDone, OnRxError, header detect, ACK frame) and
  "now". It decides when a silence starts (no RX event for `RX_SILENCE_MS` = 60 s, `INSTRUMENT`-tunable), when to
  print a snapshot (every 60 s while silent) and when it ends.
- Snapshot line, `--loradebug`:
  `[MC-DBG] RX_SILENCE t=<s> mode=<chip mode from status> drv=<cached opmode> st=<status hex> irq=<irq hex> err=<err hex> rssi_inst=<dBm> cad=<busy>/<scans> restarts=<n> boosted=<0|1>`
- End line: `[MC-DBG] RX_SILENCE_END dur=<s> by=<rx|cad|tx|watchdog> rssi=<dBm> snr=<dB>`.
- The watchdog restart line gains the chip mode before the restart: `RX_RESTART src=timeout mode_before=<opmode>`.
- Always-on boot counters (silences, longest, total s) in the existing `--info` / stats print. No new settings field.
- Chip reads run in the loop task, inside the same `taskENTER_CRITICAL()` pattern the existing radio calls use;
  never from the ISR or OnRxDone. All are legal SX1262 commands in RX mode.

**Not in this campaign:** a recovery change (for example `Radio.Standby()` before `startRadioReceive()` in the
watchdog restart). It is decided after the field logs show what ends a silence.

**Field step (operator):** OE3XIA-12 lists OE1KBC in the mcmap `name` field. A capture with the W1c build needs the
owner. Bench verification only proves that the lines exist and the tracker works.

## 3. Decisions

| #   | Decision              | Status             | Content                                                                                                           |
| --- | --------------------- | ------------------ | ----------------------------------------------------------------------------------------------------------------- |
| D1  | Valid ping targets    | decided 2026-11-01 | Callsigns only: no groups, no special targets, not the own callsign. Enforced on all five paths (2.1)             |
| D2  | Own-beacon fix        | decided 2026-11-01 | Own position/WX/HEY classified as prio 2 like an own group message; TTL and supersede unchanged; relays unchanged |
| D3  | Binary ACK forwards   | decided 2026-11-01 | No overhear-cancel; behaviour accepted (2.3)                                                                      |
| D4  | `{CET}` sole provider | decided 2026-11-01 | No change; a sole provider sends (2.4)                                                                            |
| D5  | OE3XIA                | decided 2026-11-01 | Extend the firmware logging; no recovery change yet (2.5)                                                         |

## 4. Waves

| Wave | Content                                                                                                        | Gated by | Status                             |
| ---- | -------------------------------------------------------------------------------------------------------------- | -------- | ---------------------------------- |
| W0   | Read-only recon (R1, R2) and measurements on the 2026-11-01 capture (M2, M5), adversarial verification         | -        | done                               |
| W1   | W1a ping lock (item 1), W1b own beacons at prio 2 (item 2), W1c RX-silence logging nRF52 (item 5)              | W0, D2   | done                               |
| W2   | W2b RX-silence logging ESP32 parity (item 5)                                                                   | W1       | done                               |
| W3   | Bench (RAK + Heltec V3), docs, release candidate; field capture by the operators, re-run of `tools/logreport/` | W2       | bench done; release and field open |

### 4.1 W0: recon and measurements (read-only on `src/`)

Raw logs: `logs/20261101-24h-log/` (gitignored), normalised copies in `logs/20261101-24h-log/.logreport-work/norm/`.
Reuse the check scripts in `docs/log-reports/2026-11-01-24h/verify/*_checks/` (for example
`rca_own_beacons_checks/head_hazard.py`, `health_checks/xia_tx_heard_by_others.py`). New scripts go to
`docs/log-reports/2026-11-01-24h/verify/lrf_checks/<task>/` (stdlib, paths via argv, `python3 -I`), results to
`data/extra/lrf_<task>.json` plus a short `verify/lrf_<task>.md`.

| Task | Agent                           | Question                                                                                                                                                                                                                                                                                                                                                                                                                                                                        | Exclusive output        |
| ---- | ------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------- |
| R1   | `scout`, Haiku, medium          | Every writer of `node_pingcall` (console, web, BLE v1 import, JSON config import, remote command if any) and every caller of `sendMessage()` that can carry `{ping}`. Does each settings path end in `sanitize_loaded_settings()`? Is there a host harness that can call `sendPing()` / `sendMessage()`?                                                                                                                                                                        | report only             |
| R2   | `scout`, Haiku, medium          | ESP32 RadioLib in this tree (`.pio/libdeps/heltec_wifi_lora_32_V3/RadioLib`): which of status / IRQ flags / device errors / instantaneous RSSI / chip mode are public for SX1262? Where is the ESP32 RX watchdog restart?                                                                                                                                                                                                                                                       | report only             |
| M2   | `implementer`, Sonnet, high     | Own-beacon replay for D2 variants A1, A2, A3 at OE3XOC and OE3XIA. For every own prio-4/5 entry: head episodes, the gaps after each received frame, the first gap at least as long as the variant's hold -> predicted TX before or after the TTL. Output per node, class and variant: recovered beacons, extra airtime, and how many of those TX would fall in the 100-275 ms rapid window. Plus the measured relay count per aired own HEY/WX (how many neighbours relayed it) | `lrf_m2`                |
| M5   | `implementer`, Sonnet, high     | OE3XIA RF-path test. (a) RSSI/SNR at which the other loggers received OE3XIA-12 as last hop, inside vs outside the 35 silences. (b) Heard share of OE3XIA TX inside (40/57) vs outside. (c) CAD busy share inside vs outside. (d) Wind: mcmap weather stations near Exelberg (48.2497 N 16.2452 E, 613 m), gust series 31.10.-01.11., silence starts against gusts. (e) Earlier OE3XIA captures in `logs/` or `~/meshlog/`: did silences exist before 31.10.?                   | `lrf_m5`                |
| V    | `general-purpose`, Opus, medium | After M2 and M5 land: one verifier per task tries to refute every number with an independent script from the raw logs. Verdict table CONFIRMED / CORRECTED / REFUTED in `verify/lrf_<task>_verdict.md`                                                                                                                                                                                                                                                                          | `lrf_<task>_verdict.md` |

W0 ends with a short report to the operator: the M2 numbers per D2 variant and the M5 verdict on the RF path.

### 4.2 W1: firmware, parallel writers

| Brief | Item | Exclusive files                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           | Tests (own suite only, through `piolock.sh`)                                    |
| ----- | ---- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------- |
| W1a   | 1    | `src/ping_target.h` (new), `src/command_functions.cpp` (`"pingcall "` block only), `src/loop_functions.cpp` (`sendPing()` and the destination checks in `sendMessage()` only), `src/loop_functions.h` (`PingResult`), `src/settings_sanitize.h` + `.cpp`, `src/esp32/esp32_flash.cpp` + `src/nrf52/nrf52_flash.cpp` (one call each in `sanitize_loaded_settings()`), `src/nrf52/nrf52_ble.cpp` (`applyPendingBleSettings()` only, R1), `src/esp32/esp32_main.cpp` (ping block only), `test/test_ping_target/` (new), `test/test_settings_sanitize/`, `platformio.ini` test filter if a new suite needs it | `native -f test_ping_target`, `-f test_settings_sanitize`, `-f test_regex_call` |
| W1b   | 2    | `src/txring_functions.cpp` (`classifyFrame()` and `ringPriority[]` consumers only), `src/txring_ttl.h`, `test/test_txring/`, `test/test_txprio_replay/`, `test/test_bp_regression/`, `test/test_gwflood_frames/`, `test/test_txring_flood/`                                                                                                                                                                                                                                                                                                                                                               | `native*` suites of its own test dirs                                           |
| W1c   | 5    | `src/rx_silence.h` (new), `src/nrf52/nrf52_main.cpp` (RX watchdog block, CAD start sites, loop hook, stats print), `src/nrf52/nrf52_radio.cpp` + `.h` (diag read helper), `test/test_rx_silence/` (new)                                                                                                                                                                                                                                                                                                                                                                                                   | `native -f test_rx_silence`                                                     |

- **Hotspots:** `platformio.ini` belongs to W1a only. If W1c needs a test filter entry, it reports the line and the
  orchestrator adds it at the gate. The one-line nRF52 ping-loop change from 2.1 (`src/nrf52/nrf52_main.cpp`, owned
  by W1c) is applied by the orchestrator at the gate. If R1 names an import path inside a file another brief owns,
  the orchestrator applies that call at the gate too.
- **Shared build resource:** one `pio` process at a time; writers route every `pio` call through `piolock.sh` and run
  only their own suite. Board builds and the full host gate belong to the orchestrator.
- **Artifact proof:** W1c code sits behind `bLORADEBUG` and `BOARD_RAK4630`. The gate scans the RAK firmware image
  for the strings `RX_SILENCE`, `RX_SILENCE_END`, `mode_before=`. W1a: `invalid target` and
  `{ping} needs a callsign target` in the RAK and Heltec V3 images.

### 4.3 W2

- **W2b (item 5, ESP32 parity):** `src/rx_silence.h` (shared tracker from W1c, read-only unless a bug is found),
  `src/esp32/esp32_main.cpp` (RX timeout restart, CAD sites, radio construction to keep the `Module*`), the ESP32
  radio diag helper. IRQ flags and RSSI via the public API; status and device errors via `Module::SPIreadStream()`
  (R2). Device errors in datasheet byte order (the nRF52 library needed a swap, W1c rework).
- **END-line refinement (from the W1 bench):** the RX restart fires about every 4.6 s, so with the 3000 ms window
  `by=watchdog` is assigned by chance to about 60 % of silence ends. `src/rx_silence.h` keeps the time of the last
  CAD, own TX and RX restart separately; `RX_SILENCE_END` gains `since_cad=<ms> since_tx=<ms> since_restart=<ms>`
  (or `-` if none since the silence began), and `by=` prefers cad over tx over watchdog within the window. Applies to
  nRF52 and ESP32; `test/test_rx_silence` extended.
  `src/esp32/esp32_main.cpp` was touched by W1a, so W2b starts after the W1 commit.

### 4.4 Gate per wave (orchestrator)

1. Read the diff. Spot-check the sharpest claim of each report: re-run its regression test against the old code to
   confirm it fails before the fix.
2. `tools/regression.sh --stage 1,2` (never bare `pio test`).
3. Board builds: `wiscore_rak4631`, `heltec_wifi_lora_32_V3` (with `MC_AU_DEV_CHANNEL=1`), plus the string scan
   from 4.2.
4. Opus advisor review of the wave diff; rework per `/orchestrate-waves`.
5. Commit with explicit paths, update this document (wave log, status) and `docs/RESUME.md`.

### 4.5 W3: bench and field

- **Bench (load `/bench` first, `identity_guard.py`, no broadcast, at most 2 dBm, TEST group or DM to an own node):**
  - RAK4631 and Heltec V3 with the gate build: `--pingcall 255.` and `--pingcall 2025` refused; a config import with
    `node_pingcall: "255."` is cleared at boot (`[FLASH]...sanitized node_pingcall`); a manual `{ping}` to a group is
    refused before TX; a `{ping}` to an own bench node still gets its `{pong}`.
  - `RX_SILENCE` lines on a RAK node without an antenna signal (shielded or antenna-less; `--airgap on` stops TX
    only).
- **DK5EN-98 needs an explicit OK** for any flash or test.
- **Field:** the next 24-h capture of the OE3 cluster is evaluated with `tools/logreport/` (README "Re-running"),
  against these targets:

| Item | Metric                                                                  | Baseline 2026-11-01        | Target                     |
| ---- | ----------------------------------------------------------------------- | -------------------------- | -------------------------- |
| 1    | Own frames with "Destination-CallSign Error"; dead ping pool writes     | OE3XIR 688/day             | 0                          |
| 1    | Group pings and their ACK forwards (after the owner's flash)            | 1529 pings, 8727 forwards  | 0                          |
| 2    | Own HEY aired per episode; own WX lost to TTL at OE3XOC (if D2 is done) | 41.8 %; 34/49              | at least 90 %; at most 5 % |
| 5    | Silences with diagnostic lines and an end reason                        | 35 silences, cause unknown | every silence classified   |

## 5. Agents, cost, wall-clock (estimate)

| Wave | Agents                                    | Model / effort                         | Wall-clock | Cost      |
| ---- | ----------------------------------------- | -------------------------------------- | ---------- | --------- |
| W0   | 2 scouts, 2 analysis writers, 2 verifiers | Haiku medium; Sonnet high; Opus medium | 45-60 min  | USD 12-20 |
| W1   | 2-3 implementers, 1 advisor               | Sonnet medium; Opus medium             | 45-60 min  | USD 12-20 |
| W2   | 1-2 implementers, 1 advisor               | Sonnet medium; Opus medium             | 30-45 min  | USD 6-12  |
| W3   | orchestrator, bench                       | -                                      | 1-2 h      | small     |

## 6. Rules for every brief (restate in each)

- Exclusive file set as in 4.2 / 4.3; never create, stub or delete a file another agent owns.
- No git commands, no worktrees, no whole-repo formatter.
- No flashing, no serial ports, no transmissions; bench work is the orchestrator's.
- Every bug fix ships a regression test with a pasted failing "before" run against the old code. Without it the
  item is reported as unverified.
- `grep -n` before reading; no unbounded reads of files over 400 lines.
- Report in at most 15 lines: files, tests run with their result line, deviations, open questions.

## 7. Wave log

| Wave | Date       | Result                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 |
| ---- | ---------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| W0   | 2026-11-01 | R1: one more bypass (nRF52 live BLE write), no host harness for `loop_functions.cpp`. R2: nRF52 op mode is cached, ESP32 status/errors protected. M2 and M5 confirmed by independent Opus verifiers; corrections to 2.2 (gap median 1.42 s, airtime 0.95/0.44 s) applied. Outputs `verify/lrf_*.md`, `data/extra/lrf_m{2,5}.json`                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      |
| W1   | 2026-11-01 | W1a ping target locked on every path (console, boot sanitize both platforms, nRF52 live BLE write, `sendPing()`, manual `{ping}`). W1b own position/WX/HEY at prio 2 (source label `txRingSourceIsOwnBeacon()`), TTL by frame type kept. W1c RX-silence tracker and chip diagnostics on nRF52. Advisor rework applied (BLE sanitize order, repeated FAILED line, case-sensitive `{ping}`, vacuous case-B test, `err=` byte order, no SPI read while the driver reports sleep). Gate: Unity 3254/3254, stage 2 green, RAK and Heltec V3 build with all strings present. Bench: DK5EN-1 refuses `2025`, `255.`, `TEST`, `*`, own call, `DK5EN-05`, accepts `DK5EN-90` (`lrf_w1_dk5en1_pingcall.summary`); DK5EN-90 shows `RX_SILENCE` snapshots (`mode=rx st=0x52 rssi_inst=-110`), END lines and `RXSIL` counters (`lrf_w1_rak90_rxsil.summary`). Separate commit: `tools/webflash.py` web login from `.env`                                                                                                                                                                                                                                                                                                                            |
| W2   | 2026-11-01 | END-line refinement: `src/rx_silence.h` keeps the last CAD, own TX and RX restart separately; `RX_SILENCE_END` gains `since_cad= since_tx= since_restart=` (ms or `-`), `by=` prefers cad over tx over watchdog inside the 3 s window (nRF52 and ESP32). W2b ESP32 parity: tracker in `esp32_main.cpp` (RX events from `checkRXProcess()`, parked web-send frames noted at read time), CAD/TX/restart hooks, `RX_SILENCE` snapshot via new `src/esp32/esp32_radio_diag.*` (GetStatus through `Module::SPIreadStream` with status width 0, device errors MSB first; SX1276 boards print `na`), `mode_before=` on `RX_RESTART src=timeout`, `RXSIL` in `--info` on ESP32. Not covered: preamble/header detect on ESP32 (not observable without polling). Advisor rework applied (park timing, T5 unused functions, `unk` mode name, scan errors not busy, two edge tests). Gate: Unity 3260/3260, stage 2 green, `test_rx_silence` 23/23 (new priority tests fail on the old header); builds RAK, Heltec V3, T-Beam (SX1276), T-Deck Pro (RadioLib 7.1.2), T5 e-paper, external radio (dummy overlay); `RX_SILENCE`, `RX_SILENCE_END`, `since_cad=`, `mode_before=`, `RXSIL` in the RAK and Heltec V3 images. ESP32 not benched yet (W3) |
| W3   | 2026-11-01 | Bench, Heltec V3 DK5EN-1 with `e01a5dc9` (`--loradebug on`, 2 dBm, 18 min, `lrf_w3_dk5en1_rxsil.summary`): six `RX_SILENCE` snapshots (`mode=rx drv=na st=0x52 err=0x0000 rssi_inst=-105..-109`; `drv=na` by design, RadioLib keeps no driver mode) and six `RX_SILENCE_END` lines with the new ages (`by=rx` 4, `by=watchdog` 2; `since_cad=-` when no CAD since the silence began); `RXSIL` in `--info`; 178 `RX_RESTART src=timeout` all `mode_before=rx`; no reset. Silences on the bench are a quiet channel (60-107 s), not a deaf chip. Ping-target refusals on the same board were benched in W1. Not benched: config import with `node_pingcall: "255."` (covered by `test_settings_sanitize`). Docs-only commit, no advisor pass. Open: release candidate, field capture                                                                                                                                                                                                                                                                                                                                                                                                                                                     |
