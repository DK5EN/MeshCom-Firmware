# S&F port to feature-snf — campaign state

Started 2026-09-26. Ports the DM store-and-forward stages from `fork-main` onto
`feature-snf`, branched from `feature-neighbour-matrix` `0d4b914c` (neo line, upstream base
`e4a2393f`). Resume rule: this file is the authority, not the git log.

Source of truth for the feature itself: `fork-main:docs/dm-transport-impl-plan-20260913.md`
(stage table, decisions D1-D8) and the stage plans next to it.

## Status

| Wave | Content                                                             | Status          | Commit   |
| ---- | ------------------------------------------------------------------- | --------------- | -------- |
| 0    | branch, this doc, neo path lists (NBR gap)                          | done 2026-09-26 | 4bb0dc45 |
| 1    | new modules + host tests, shared headers, `env:native`              | done 2026-09-26 | see log  |
| 2    | stage 0 (without 0.1) + stage 2.1 hooks                             | done 2026-09-26 |          |
| 3    | stage 1 outbox + ladder (`--dmretry`)                               | done 2026-09-26 |          |
| 4    | stage 3 store node + stage 4 custody notice                         | done 2026-09-26 |          |
| 5    | docs over, CHANGELOG/BACKLOG, all-env build, RAM snapshot same-base | done 2026-09-26 |          |

Per-wave gate: files exist; host suite (native envs only, never bare `pio test`);
`test/golden/help_parity_lint.py`; 7 lead envs clean and sequential; string scan of the S3 and
RAK images; `/fable-review` advisor pass; one commit. No hardware, no push in this campaign.

## Operator decisions (2026-09-26)

| ID  | Decision                                                                                                                                |
| --- | --------------------------------------------------------------------------------------------------------------------------------------- |
| P1  | ACK msg_id: keep the target's `msgid_counter.*` high-water design. Stage 0.1 (millis() minting in `SendAckMessage()`) is NOT ported.    |
| P2  | `--store on` + `--mesh off`: store delivery and the `:sto` notice stay independent of `bMESH`, as on fork-main.                         |
| P3  | S&F is meant to reach neo: every new S&F src/test file goes into `tools/neo/paths` (code: CORE, tests: K19) in the commit that adds it. |
| P4  | Boards as fork-main: `ENABLE_MSGSTORE` on ESP32-S3 + RAK4631 only; dm_stats/dedup/outbox on every board.                                |
| P5  | Stage 2.2 stays deferred; upstream 5efa2171 (repeat-counter MSB, dormant on fork-main) is not ported.                                   |

## Port rules (from the recon)

- Port the post-rework version of each stage, never the first cut: 4a569e6f (airgap teardown,
  echo scope), 1595542c (the ack stops the ladder OUTSIDE `checkOwnTx`), 96050d72 (the ESP32
  tick is the main-radio `if(bRadio)` one), 150b0a4a (`:sto` on both server ingress paths).
- `aprsMessage` fields are `char[]` on the target: every `String` call in a hook becomes a
  `src/mc_text.h` helper.
- Server ingress lives in `src/esp32/udp_frame_esp32.cpp` / `src/nrf52/udp_frame_nrf52.cpp`,
  not `udp_functions.cpp` / `nrf_eth.cpp`; `env:native_udp_frame_twin` must learn the new modules.
- `addTxRingEntry(Once)` carries `kind/need/alone` (NBR); S&F slots use `RING_KIND_OTHER`.
- `mheard_functions.*` is gone (ccb3ec23): the store set's heard age comes from
  `nbrFind()` + `nbrMhGet()` (`src/nbr_views.h`), minute resolution, 12 h window unchanged.
- Commands match exact tokens; every `--help` line sits under its handler's `#if` guard.
- Host tests stay in `env:native` as on fork-main (pure modules, `build_src_filter`).

## Neo path lists

Wave 0 found the whole NBR branch unregistered, not only `nbr_matrix.*`: 19 code paths (CORE) and
19 test paths (K19) since `e4a2393f`. All added; README counts updated (744 / 281 / 463). Separately,
`fork-neo-test` itself lacks `src/mask_secret.h` and `src/mheard_throttle.h` in its lists — not
touched here.

## Wave log

**Wave 1 (2026-09-26).** 19 modules + 7 test suites from fork-main tip; 17 files byte-identical,
only `dm_outbox_glue.cpp` and `msgstore_glue.cpp` adapted (`char[]` via `mc_text.h`, heard age via
`nbrFind()`+`nbrMhGet()`, `NBR_WINDOW_MIN` 720 = `MSGSTORE_HEARD_WINDOW_MS`). Orchestrator:
`ack_attribution.h` (0x03/0x04), `backpressure.h` (`BP_NACK_OUTBOX_FULL`), `ENABLE_MSGSTORE`,
`env:native` lists, neo paths (+19 S&F code incl. the two modified headers, +7 tests). Gate: host
44/44 envs, 1373 cases; 7 lead envs clean green; objects present on RAK (guard not eating them);
the ELF drops them until wave 2 wires a caller. Advisor skipped: no caller, no behaviour change;
the two glue adaptations go to the advisor of the wave that wires them. Baseline for later
comparison (inert code): RAK flash 92.8 % (756592 B), RAM 79948 B; Heltec V3 RAM 101276 B.
Pre-existing red, not ours: `test/golden/drift_matrix_lint.py` (DR-03, DR-16 lack asserting tests).

**Wave 2 (2026-09-26).** Stage 0 without 0.1 and stage 2.1, three writers. `lora_functions.cpp`:
dmstat counters (incl. both gw_ack sites), `--airgap` RX drop/teardown and TX refusal
(`INSTRUMENT_ENABLED` only), give-up -> 0x03 + `ACK_STATUS_FAILED`, duplicate-DM re-ACK `else`
on the `setlogCountDedup()` branch, 2.1 dedup in the `iEnqPos` branch. `txring_functions.cpp`:
M0-1 counters in `addTxRingEntryCore()`. Both `udp_frame_*` twins: 2.1 dedup + re-ACK gate,
twin regression test (red without the gate). `loop_functions.cpp`: brace escape with `{ping}`/`{SET}`
exemption, dmstat_sent, DM setlog line. `--airgap` command, web "failed" marker. Gate: host 44/44
envs, 1377 cases; 13 lints; 7 lead envs; DM strings in Heltec/RAK/T-Beam images; instrumented RAK
build carries the AIRGAP strings. Advisor: one finding (second `dmstat_gw_ack` site, gateway
self-ack in OnRxDone) fixed, rest verified equal to fork-main tip. RAK flash 93.4 % (761256 B).

**Wave 3 (2026-09-26).** Stage 1 outbox + ladder behind `--dmretry off|3|9` (default off), four
writers. Ack stops the ladder independent of `checkOwnTx()` (1595542c shape) in OnRxDone and both
`udp_frame` twins (twin regression test red without it); F5 fresh-id ring slot stop; echo hook
mode-independent. Ticks: `loopAction_retransmit()` in `loop_actions_esp32.cpp` /
`loop_actions_nrf52.cpp` (loop task) plus the ESP32 inline `EXTERNAL_RADIO` tick; boot
`dmSettingsLoad()` then `dmOutboxGlueInit()` after the cleanflash branch (advisor: the consistent
order here, fork-main loads before `clear_flash()`). `sendMessage()`: outbox-full refusal with
distinct NACK, `{ping}` exempt (advisor R1 fixed: `bPingMsg` hoisted from `strMsg` as on
fork-main), `bUseOnce` for `--dmretry` DMs, `dmOutboxAdd()`. `--dmretry`, `--info`, `--help`
(fork-main had no help line; help_parity_lint requires it), web select + info line, OUTBOX setlog
line. Gate: host 44/44 envs, 1378 cases; 13 lints; 7 lead envs; `esp32-external-radio` builds with
a dummy overlay (`EXTERNAL_RADIO_HOST/PORT`, the env's own precondition) and links `dmOutboxLoop`.
RAK flash 94.1 % (767048 of 815104 B) -- 48 kB left before stage 3/4.

**Wave 4 (2026-09-26).** Stages 3 and 4 plus the deferred stage 1-4 links, five writers.
`lora_functions.cpp`: presence hook where `updateMheard()` sat (same gate and callsign as
fork-main), store/purge/peer-delivery hooks in the existing destination-not-us `else`, `:sto`
parse -> 0x04 + `ACK_STATUS_HELD` + `dmOutboxOnHeld()`, echo guard `!= 0x04`, give-up vs held.
Twins: `:sto` consumption with `bStoConsumed` gates (twin regression test). Commands `--store*`,
`--mbox`, `--storenotice`, `--info STORE` (one unguarded `store` rung with the `#if` inside, forced
by `command_ladder_lint`; help lines added, fork-main had none), MBOX setlog line, Mailbox page,
setup card, messages-page held marker, `msgstoreGlueInit()`/`msgstoreSettingsLoad()` at boot,
`msgstoreLoop()` on both main ticks. Advisor: four findings, all fixed -- F1 peer-delivery tell
was the pre-stage-1 msg_id rule (now fork-main's path-shape rule), F2 `msgstoreLoop()` missing in
the ESP32 `EXTERNAL_RADIO` tick, F3 `stoHolderClear()` now runs on every accepted ack incl.
outbox-only (twin test asserts it, red on the old nesting), F4 Mailbox table no longer wrapped in
a div (this branch's table CSS rule). A writer ran `git stash` on the two twin files against its
brief; the stash matched the tree byte for byte and was dropped. Gate: host 44/44 envs, 1379
cases; 13 lints; 7 lead envs; store node only in S3/RAK images, none in classic; RAK flash 96.3 %
(784720 of 815104 B).

**Wave 5 (2026-09-26).** 18 S&F docs from fork-main (plans, stage verdicts, client guide,
command references, bench plan, mockup) byte-identical except prettier on
`review/fable-dm-stage4-verdict-20260914.md`; `docs/CHANGELOG-snf.md` new; BACKLOG §3.8az
(DM-01..DM-16); RESUME entry; neo path lists complete against `e4a2393f`
(`src/web_functions/web_functions.h` added, README counts 774 / 304 / 470). Docs-only wave, no
advisor. Final gate: 33 of 33 board envs build on HEAD `fd2bc48f` (both safeboot envs skipped:
S&F touches nothing under `src/safeboot/` and a build rewrites the tracked root images;
`esp32-external-radio` with the dummy overlay).

Same-base resources, 7 lead envs, clean builds of `0d4b914c` vs `fd2bc48f` (bytes):

| Env                    | RAM base | RAM S&F | delta  | Flash base | Flash S&F | delta  |
| ---------------------- | -------- | ------- | ------ | ---------- | --------- | ------ |
| heltec_wifi_lora_32_V3 | 101260   | 113996  | +12736 | 1502177    | 1533029   | +30852 |
| E22-DevKitC            | 95456    | 97624   | +2168  | 1614361    | 1627401   | +13040 |
| ttgo_tbeam             | 95272    | 97440   | +2168  | 1634757    | 1647153   | +12396 |
| ttgo_tbeam_supreme     | 101580   | 114324  | +12744 | 1538465    | 1569265   | +30800 |
| t_deck                 | 121668   | 134404  | +12736 | 2231649    | 2263077   | +31428 |
| t_deck_plus            | 121668   | 134404  | +12736 | 2231445    | 2262773   | +31328 |
| wiscore_rak4631        | 79748    | 92820   | +13072 | 756064     | 784720    | +28656 |

The ~10.5 kB of the S3/RAK RAM delta is the 50-slot mailbox table (`msgstore.cpp` BSS); classic
ESP32 carries only the sender side (+2.2 kB). RAK4631 flash ends at 96.3 % (30384 B free).

## Bench 2026-09-26 -- minimal store-node run, PASS

Three nodes over LoRa only (gateway off on all three during the run), 2 dBm, on one desk. RAK4631
DK5EN-90 = store node on `feature-snf` (build 19:15:55, `--store heard`); T-Beam DK5EN-92 = receiver
on `feature-snf` (build 19:26:08, flashed over WiFi); Heltec DK5EN-1 = sender, left on
`0d4b914c` because it is in the neighbour-matrix soak (gateway off 19:48-20:02, net console on for
the run, both restored). Logs: `docs/bench/snf-20260926/{rak,tbeam,dk1}.txt` (wall-clock stamps; SSID/BSSID redacted).

| Step | What                                       | Result                                                                                                                                                                                   |
| ---- | ------------------------------------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1    | RAK -> T-Beam DM                           | PASS: `{409}` sent 19:50:05, `:ack409` direct 19:50:10, ring retransmit stopped                                                                                                          |
| 2    | DK5EN-1 -> T-Beam DM                       | PASS: `{511}` acked directly 19:51:03; the RAK missed that direct ack, sent `:sto511` at 19:51:06, purged on the relayed ack 19:51:14                                                    |
| 3    | RAK stored step 2's DM                     | PASS: `stored=1 ack=1 sto=1/0 used=0`                                                                                                                                                    |
| 4    | T-Beam away (`--setcall DK5EN-91`, reboot) | done 19:52:10                                                                                                                                                                            |
| 5    | DK5EN-1 -> DK5EN-92, nobody answers        | PASS: `{512}` on air 19:54:18, RAK `:sto512 DK5EN-92` 19:54:30, received by DK5EN-1 (shown as plain text -- old firmware)                                                                |
| 6    | DM held on the RAK                         | PASS: `[MBOX];0;DK5EN-92;DK5EN-1;512;HELD`                                                                                                                                               |
| 7    | T-Beam back as DK5EN-92, reboot            | done 19:57:10                                                                                                                                                                            |
| 8    | Delivery via the RAK                       | PASS: own POS from DK5EN-92 heard 20:00:00 (presence), delivery `DK5EN-1,DK5EN-90>DK5EN-92 ... {512` H00, fresh id, 20:00:38; `:ack512` 20:00:42 reaches DK5EN-1; `deliv=1 ack=2 used=0` |

Observations:

- Presence counts only a frame the destination originates and the store node hears directly;
  DK5EN-92 relaying other traffic for 2.5 min after its reboot did not arm the entry. The run
  triggered its beacon with `--sendpos`; in the field a returning node arms the mailbox with its
  first own beacon, so delivery latency follows the destination's beacon interval.
- The `:sto` notice also goes out when the destination did ack but the store node missed that ack
  (step 2). By design (notice on every new entry); a sender on `feature-snf` would see "held"
  briefly and then "acked".
- DK5EN-1 on the old image retried once and then stopped on hearing its own relayed copy; the
  outbox ladder (`--dmretry`) was not exercised.
- Not covered yet: stage 4 on the sender side (DK5EN-1 on `feature-snf`), `--dmretry` ladder, the
  Mailbox web page, cooldown/storetime expiry, peer store nodes.

### Run 2 -- Mailbox page demo and delivery (2026-09-26 20:09-20:19)

T-Beam away as DK5EN-91 (20:09), DK5EN-1 sends `{517}` (20:10, gateway off 20:10-20:11 only):
the entry shows HELD on the RAK's Mailbox page (`DK5EN-92 | DK5EN-1 | 517 | 34 B | HELD | 0.0 of
9 sent`), `:sto517` reaches DK5EN-1. T-Beam back as DK5EN-92 at 20:11:53. Its own POS waited
about four minutes in its queue behind live group-20 relays (prio 3); a DM it sent to DK5EN-90 at
20:15:58 was not received by the RAK (busy channel). Presence at 20:17:01 (own POS heard
directly), delivery `DK5EN-1,DK5EN-90>DK5EN-92 ... {517` H00 at 20:17:32, `:ack517` to DK5EN-1 at
20:17:37 (confirmed in DK5EN-1's soak capture). RAK afterwards `used=0 deliv=2 ack=3 sto=3/0`.
Logs: `docs/bench/snf-20260926/{rak,tbeam,dk1}-run2.txt`.

Observation: under channel load a returning destination's own POS/beacon is starved by relay
traffic, so time-to-delivery is load dependent, not just beacon-interval dependent.

## Bench coverage after 2026-09-26

Test IDs from `docs/dm-transport-impl-plan-20260913.md` (T-0.x, T-3.x), `docs/dm-stage1-plan-20260914.md`
(T-1.x) and `docs/dm-stage4-plan-20260914.md` (T-4.x).

| Covered (partly)                                                                                   | Evidence     |
| -------------------------------------------------------------------------------------------------- | ------------ |
| T-3.1 absent destination, one hop-0 delivery, ack reaches sender (absence by callsign, not airgap) | run 1 and 2  |
| T-4.2 sender without stage 4 (DK5EN-1 on `0d4b914c`, not upstream 4.35t): `:sto` shown as one text | run 1 and 2  |
| store/purge-by-ack in the healthy case, notice when the direct ack is missed                       | run 1 step 2 |

| Open         | What                                                                                                                                               |
| ------------ | -------------------------------------------------------------------------------------------------------------------------------------------------- |
| T-0.2        | replayed DM: exactly one extra `:ackNNN`, no second display (dedup + re-ack limiter on air)                                                        |
| T-0.3        | DM to an unreachable call gives one `0x03`; broadcast/ACK give-ups give none                                                                       |
| T-0.4        | `--airgap` (instrumented build)                                                                                                                    |
| T-0.5 / M0-1 | one hour on the live net, ring enqueue and parked-overwrite counters                                                                               |
| T-1.1..T-1.9 | the whole `--dmretry` ladder: 9 attempts, stop on ack (T1/T2 regressions), echo gate, QRT, outbox full, `off` byte-identical, upstream destination |
| T-3.2        | a relay never forwards a hop-0 delivery                                                                                                            |
| T-3.3        | destination never acks: 9 sends, 1 h cooldown, storetime expiry                                                                                    |
| T-3.4        | two store nodes (peer cancel; mutually airgapped)                                                                                                  |
| T-3.5        | store node that is also a gateway, server-injected frame                                                                                           |
| T-3.6        | store node reboot with pending entries                                                                                                             |
| T-3.7        | destination on upstream 4.35t                                                                                                                      |
| T-3.8        | string scan of the release image (no 0x41 path for stored DMs, airgap compiled out)                                                                |
| T-3.9        | 25 DMs to an absent destination: caps hold                                                                                                         |
| T-4.1        | sender on `feature-snf`: held mark in app/web, flips to delivered on ack                                                                           |
| T-4.3..T-4.7 | re-flood while held, ladder gives up while held, two holders, `--storenotice all`, notice via server                                               |
| web          | Mailbox page actions (Deliver, Purge), setup card persistence across reboot, `--dmretry` web select                                                |

## Catch-up from fork-main (2026-09-26 evening)

A line-by-line comparison of fork-main's code commits against the `feature-snf` tree found five
items the neo line never got; everything else (P13, volt switch, batt maxv, RAM header, `c_json`
in BSS, `--mesh off` on via paths, byte-FIFO rings, loop stack 12288, EXTUDP originator keys, DHCP
hostname, P15, coordinate compare) is present in equivalent form. Safeboot differs from
upstream/fork-main only in two comments (the upstream PR dropped fork-internal references).

| Item                                                                                                                                                          | Source                           | Port                                                                                   |
| ------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------- | -------------------------------------------------------------------------------------- |
| `WSPWD`/`ASYM` from SN to SN1 (SN overflowed the BLE limit), SN/SN1 resent after `--webpwd` over BLE                                                          | upstream `f73cbf9e`              | clean                                                                                  |
| `--ping` fails loudly (TRACK suppression, ring refusal)                                                                                                       | fork-main `c570e62e`             | conflicts resolved to fork-main tip: P15 `addTxRingEntryOnce()` plus the refusal check |
| `RADIO_TX` at all six transmit sites (TXM-02), nRF52 `CAD_FREE`/`TX_START` parity                                                                             | fork-main `47eb2011`, `ec070235` | clean; markers verified directly before each send call                                 |
| EXTUDP boot line with an empty IP in the DNS branch                                                                                                           | fork-main `4879d6e5`             | clean                                                                                  |
| Tools: `serial_session.py` (DTR by port, boot marker, LF), `webflash.py` OTA session support, `ota_abort.py` + test + 41 run records, `safeboot_page_test.js` | fork-main                        | copied; `test_ota_abort.py`, `test_ota_regression.py`, `webflash.py --self-test` pass  |

`src/mask_secret.h` registered in the neo path lists (305 / 470 / 775). Gate: host 44/44 envs,
1379 cases; 13 lints; 7 lead envs; the new markers are in the Heltec, RAK and T-Beam images; RAK
flash 785504 of 815104 B. Not bench-tested: SN/SN1 over BLE to the app.
