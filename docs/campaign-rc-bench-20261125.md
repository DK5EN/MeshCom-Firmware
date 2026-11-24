# Campaign RC-BENCH: release candidate bench run (2026-11-24/25)

Resume point of the release-candidate campaign: mbedTLS copy out, all board builds, flash the fleet, sync-word A/B
test, `hmac_connect.py` fix, wave-5 bench, one night, release and PR documents. Operator brief 2026-11-24.

## Operator decisions (2026-11-24, do not re-ask)

- The copied mbedTLS code under `src/esp32/idf_override/mbedtls/` is removed before builds and tests; Auto Update
  builds, links and works without it.
- DK5EN-98: exactly one OTA flash of the release-candidate image in this campaign. Nothing else on it (no outage
  simulation, no test traffic, no setting change). Logs may be read.
- The next upstream PR is ONE combined PR: Auto Update, small fixes, gateway, remote management.
- Stop point: fork-dev committed and pushed, all documents prepared. No tag, no PR branch pushed, no PR, no issue
  without the operator's go.

## Steps and status

| Step | What                                                    | Status                                             |
| ---- | ------------------------------------------------------- | -------------------------------------------------- |
| 1    | mbedTLS copy removed, host gate, AU re-measured -1, -92 | code done (`5488f0bc`), bench in step 3            |
| 2    | full host gate, every board env                         | done: 35/36 envs, external-radio its own `#error`  |
| 3    | flash all bench nodes + DK5EN-98 (OTA), AU on T-Deck    | done 2026-11-24 15:38                              |
| 4    | sync-word A/B on DK5EN-90                               | done: no side effect on the bench; gaps stated     |
| 5    | `tools/hmac_connect.py` fixes (HMAC-WIN-01)             | done (`732238bd`)                                  |
| 6    | wave 5 bench (#1307, #1308, #1303)                      | done: all pass; Read button click not in a browser |
| 7    | one night: AU check + download x3 per ESP32 node        | running since 19:25                                |
| 8    | documents (release notes, journal, PR text, issues)     | open                                               |

## Step 1: mbedTLS copy removed

Effect on the Auto Update path: the output record buffer goes back to the stock 16717 B (content 16384). The AU
buffer of a job (`FWNET_BUF32_LEN`, max(in + out record, inflate dictionary)) grows from 32768 B to 33440 B
(2 x 16720 in 16-byte slots), +672 B at the job peak. Outside Auto Update every TLS session costs 12 KB more than
with the copy, the state before `469bb942`.

- Code: `src/esp32/idf_override/mbedtls/` and `README-mbedtls.md` deleted (about 23,000 lines); link marker
  `mc_mbedtls_out4k_marker` removed from `mc_ssl_client.cpp` (the partial-write loop in `mc_ssl_send` stays, it is
  correct for any record size); `FWNET_TLS_OUT_BUF` = `FWNET_TLS_REC_BUF` (16717 B); AU buffer length from
  `fwAuBufLenFor()` in `src/fw_au_buf.h`.
- Regression test: `native_fw_au_buf` `test_au_buffer_holds_both_stock_records`. Red with the old output constant
  (`-DFWNET_TLS_OUT_BUF=4429u`: "Expected 16717 Was 4429"), green now; suite 17/17, `native_fw_update` 133/133.
- Gate 2026-11-24 14:48 (`tools/regression.sh --stage 1,2`): 129 Unity envs, 4235 cases, 0 failed; pytest 1830
  passed; board links t_echo 55.7 %, heltec_t114 53.2 %, wiscore_rak4631 83.7 %, heltec_wifi_lora_32_V3 52.9 %,
  ttgo_tbeam 55.9 %. One red step, the suite inventory (new case), regenerated and rechecked.
- Bench re-measure on DK5EN-1 and DK5EN-92: with the step-3 image (same AU code), see step 3.

## Step 5: `tools/hmac_connect.py` (HMAC-WIN-01), done

- `732238bd`: `wake_main_thread()` (pthread_kill where it exists; on Windows a console Ctrl-C event, fallback
  `_thread.interrupt_main()`), stdout encoding errors written with replacement characters and any other receive
  exception ends the session and wakes the main thread, `cleanup()` ignores SIGINT, SIGTERM and SIGHUP.
- `tools/tests/test_hmac_connect.py`: 3 cases, all red on the old script, green after, 10 of 10 runs green.
- Not run on Windows (no machine): the missing `pthread_kill` is simulated by removing it in the test; the console
  Ctrl-C path itself never ran. Gate `tools/regression.sh --changed` PASS (pytest 1833 passed, ruff clean).

## Image plan (steps 3, 4, 7)

- One commit for every node. ESP32 bench images (DK5EN-1, -14, -92) are built with `MC_BUILD_TAG=v4.40a.11.21`, so
  11.22 counts as newer and the Auto Update check and download can run (stage only, never apply; AU-27 locks a local
  build without a tag to the last installed tag). DK5EN-98 gets the same commit without a tag: it reports no newer
  release and does not download in production.
- RAK image B = the regular build of the commit (`0x24B4`); image A = the same commit with `0x242B` at the two nRF52
  call sites, a local bench build, never committed.

## PR scope findings (2026-11-24, read-only analysis, [pr-combined-scope-20261125.md](pr-combined-scope-20261125.md))

- **The AU buffer needs the FORMAL-01 TLS allocation cap** (`fw_tls_alloc_cap.h`): `tlsCalloc()` serves records from
  the buffer only while the cap is armed (`fw_update_net.cpp:392`), and before FORMAL-01 the mbedTLS hook was
  installed only on PSRAM boards. Proposal for the PR: the cap (about 95 lines, own code, not a library patch)
  travels with Auto Update. Operator to confirm.
- **The PR image differs from the fork image:** the PR leaves the FORMAL-01 library patches out (AE-01..03, NimBLE,
  mDNS, DHCP server, W5100S), the fork RC image has them. Bench evidence from fork images is evidence for the fork
  firmware; the PR branch needs its own build of every env and a short bench run before it is opened.
- **AU-28 has no firmware part:** it was a defect in the fork's own HTTP client patch (FORMAL-01), so it is not in
  the PR.
- **`--udphold` is an INSTRUMENT-only bench switch** (`src/udp_hold_bench.cpp:14`): the #1307 bench on DK5EN-1 needs
  an INSTRUMENT image of the same commit. The hold ring itself is in every build.
- **`Closes #1309` would be wrong:** #1309 includes SB-06 (Safeboot), which stays out.
- **RM Queues card:** the web part is upstream already (`web_rm_page.cpp:80`); the fork adds 3 lines. Bench owed.

## Step 2: builds (2026-11-24, commit `732238bd`)

- Host gate: the full gate of step 1 ran on the same `src/` (129 Unity envs, 4235 cases); the only later change is
  `tools/hmac_connect.py` with its change-based gate (pytest 1833 passed).
- Board envs, `MC_AU_DEV_CHANNEL=1`, one `pio` at a time: 35 of 36 build. `esp32-external-radio` fails on its own
  `#error "EXTERNAL_RADIO requires EXTERNAL_RADIO_HOST and EXTERNAL_RADIO_PORT from the build overlay"`
  (`src/esp32/external_radio_glue.cpp:48`), as on upstream dev. Per env RAM/flash:
  `tools/bench/runs/harness/rc-bench-20261125/builds/results.tsv`. Highest flash: RAK4631 83.7 %, T5 e-paper 71.6 %.
- The safeboot envs rewrite the tracked `safeboot.bin` / `safeboot-s3.bin` in the repo root; restored to the committed
  copies.

## Step 3: flash and Auto Update (2026-11-24)

| Node     | Board       | Image (all commit `732238bd`)                         | md5              | Method                       | Back with                                                |
| -------- | ----------- | ----------------------------------------------------- | ---------------- | ---------------------------- | -------------------------------------------------------- |
| DK5EN-1  | Heltec V3   | `heltec_wifi_lora_32_V3`, `MC_BUILD_TAG=v4.40a.11.21` | `60cdd358`       | OTA, webflash.py             | build 15:25:41, guard ok, gateway on                     |
| DK5EN-92 | T-Beam v1.2 | `ttgo_tbeam`, tag 11.21                               | `c782b381`       | OTA, webflash.py             | build 15:26:40, guard ok                                 |
| DK5EN-14 | T-Deck Plus | `t_deck_plus`, tag 11.21                              | `4d2c8378`       | OTA, webflash.py             | build 15:27:30, guard ok                                 |
| DK5EN-90 | RAK4631     | `wiscore_rak4631`, image B (`0x24B4`)                 | `52723f33` (hex) | serial DFU (`pio -t upload`) | build 14:56:11, guard ok                                 |
| DK5EN-98 | Heltec V3   | `heltec_wifi_lora_32_V3`, no tag                      | `c1e9adc9`       | OTA, webflash.py, 15:38      | build 15:11:17, all settings as before, server connected |

- DK5EN-1: the first OTA attempt broke off (`Broken pipe` during the upload). Entering Safeboot installed the 11.22
  image staged there by the AU-30 bench on 2026-11-24; the second attempt flashed the RC image. Bench node only.
- DK5EN-98: the single approved OTA flash; `--info` before and after identical except the build (mesh on, setlog on,
  EXTUDP target, 433.175 MHz, 22 dBm, AU notify dev, gateway on). AU there: tag `(local)`, nothing offered.

Auto Update, check + install (stage only, never apply), `tools/bench/ota_lowheap_trigger.py --attempts 1`, plain
images, logs `tools/bench/runs/harness/rc-bench-20261125/au/`:

| Node     | Free / largest block at start | Lowest free during job | AU buffer         | TLS records in buffer | Download              | Erase, idle0max | Result                         |
| -------- | ----------------------------- | ---------------------- | ----------------- | --------------------- | --------------------- | --------------- | ------------------------------ |
| DK5EN-1  | 103,120 / 90,100 B            | 19,564 B               | 33,440 B          | check 2/2, dl 4/4     | 1,180,497 B in 11.2 s | 2.7 s, 164 ms   | staged                         |
| DK5EN-92 | 62,300 / 57,332 B             | 11,332 B               | 33,440 B, twice   | check 2/2, dl 4/4     | 1,240,686 B in 13.4 s | 6.0 s, 362 ms   | staged, image check in the job |
| DK5EN-14 | 61,696 / 51,188 B (internal)  | 40,196 B               | none (PSRAM path) | n/a                   | 1,617,579 B in 20.7 s | 2.4 s, 84 ms    | staged                         |

- "Lowest free" is `--info` HEAP `min` (minimum since boot) read after the job; the plain image has no
  `--heaplog`. Not the same instrument as the INSTRUMENT figures of 2026-11-23 (`[INSTR-HEAP]`).
- One run per node so far; the night (step 7) repeats it three times per node.

## Step 4: sync-word A/B (SYNC-NRF-01)

Harness `tools/bench/syncword_ab.py` (12 group-9999 messages per node through the web API, one sender at a time,
every other node listening with `--setlog on`; restored after), `tools/bench/syncword_passive.py` (RAK listening
only, compared with DK5EN-98's log). Raw logs: `tools/bench/runs/harness/rc-bench-20261125/syncword/`.

- **First attempt aborted (no RF effect):** the console does not take `:{9999}text` ("wrong command", nothing
  sent); the harness now sends through `/?sendmessage&tocall=9999` and refuses any other destination.
- **Losses at strong signal are channel access, not sync word:** every frame the RAK missed in run B arrived while
  its CAD (listen before send) found the channel busy (`CAD_SCAN result=-702`) with gateway ACKs from the server in
  its queue; after a busy CAD the RAK returns to idle and does not receive the frame that made the channel busy.
  The same conditions apply under image A, so the A/B difference stays meaningful. New finding, see the end of this
  section.
- **Near the sensitivity limit not reachable on this bench:** at -9 dBm and under a metre apart every pair still
  has SNR +5 to +11 dB (SF11 decodes to about -17 dB); RSSI moves by more between runs than between power
  levels. A margin test needs a physical attenuator. The passive field comparison is the only low-SNR evidence.

#### Run B, 2 dBm (2026-11-24 15:42-15:53)

| Sender   | Receiver | Sent | Decoded direct | Relay only | Errors in window | RSSI med/min | SNR med/min |
| -------- | -------- | ---- | -------------- | ---------- | ---------------- | ------------ | ----------- |
| DK5EN-90 | DK5EN-1  | 12   | 12             | 0          | 1                | -50 / -51    | 6 / 5       |
| DK5EN-90 | DK5EN-92 | 12   | 12             | 0          | 0                | -53.5 / -55  | 11 / 10     |
| DK5EN-90 | DK5EN-14 | 12   | 12             | 0          | 0                | -70 / -71    | 6 / 6       |
| DK5EN-1  | DK5EN-92 | 12   | 11             | 1          | 0                | -50 / -51    | 10 / 9      |
| DK5EN-1  | DK5EN-90 | 12   | 11             | 0          | 0                | -44 / -45    | 6 / 6       |
| DK5EN-1  | DK5EN-14 | 12   | 12             | 0          | 0                | -65 / -66    | 6 / 5       |
| DK5EN-14 | DK5EN-1  | 12   | 11             | 0          | 0                | -74 / -74    | 6 / 5       |
| DK5EN-14 | DK5EN-92 | 12   | 11             | 0          | 0                | -75 / -76    | 10 / 9      |
| DK5EN-14 | DK5EN-90 | 12   | 9              | 0          | 2                | -72 / -72    | 7 / 6       |
| DK5EN-92 | DK5EN-1  | 14   | 13             | 0          | 0                | -45 / -45    | 6 / 5       |
| DK5EN-92 | DK5EN-90 | 14   | 10             | 0          | 0                | -42 / -42    | 7 / 6       |
| DK5EN-92 | DK5EN-14 | 14   | 13             | 0          | 0                | -65 / -65    | 6 / 5       |

#### Run B-low, -9 dBm on DK5EN-1, -14, -90; DK5EN-92 at 2 dBm (refuses below -4) (15:54-16:06)

| Sender   | Receiver | Sent | Decoded direct | Relay only | Errors in window | RSSI med/min | SNR med/min |
| -------- | -------- | ---- | -------------- | ---------- | ---------------- | ------------ | ----------- |
| DK5EN-90 | DK5EN-1  | 12   | 11             | 1          | 0                | -49 / -51    | 6 / 5       |
| DK5EN-90 | DK5EN-92 | 12   | 12             | 0          | 0                | -58.5 / -74  | 11 / 10     |
| DK5EN-90 | DK5EN-14 | 12   | 12             | 0          | 0                | -69 / -77    | 6 / 6       |
| DK5EN-1  | DK5EN-92 | 12   | 12             | 0          | 0                | -55 / -59    | 11 / 10     |
| DK5EN-1  | DK5EN-90 | 12   | 12             | 0          | 0                | -49 / -52    | 6 / 6       |
| DK5EN-1  | DK5EN-14 | 12   | 12             | 0          | 0                | -49 / -68    | 6 / 5       |
| DK5EN-14 | DK5EN-1  | 12   | 10             | 0          | 0                | -59 / -60    | 6 / 5       |
| DK5EN-14 | DK5EN-92 | 12   | 11             | 0          | 0                | -64 / -68    | 10 / 9      |
| DK5EN-14 | DK5EN-90 | 12   | 9              | 1          | 0                | -75 / -76    | 6 / 6       |
| DK5EN-92 | DK5EN-1  | 13   | 12             | 0          | 0                | -43 / -44    | 6 / 5       |
| DK5EN-92 | DK5EN-90 | 13   | 8              | 0          | 0                | -47 / -48    | 6 / 6       |
| DK5EN-92 | DK5EN-14 | 13   | 12             | 0          | 0                | -42 / -44    | 6 / 5       |

## Shared-tree incident (2026-11-24 16:10)

- Another session (`telegram-12`, started about 16:06, the ticket desk) left an uncommitted edit in
  `src/web_functions/web_functions.cpp` (WEB-WX-01, P-0394) while four images were built in this tree (16:06-16:10).
  Those four images were deleted unused. The flashed images (built 15:11-15:27), RAK image A (about 15:40) and the
  step-2 builds are older than that session.
- From now on every RC image is built in a clean worktree at the campaign commit (`../MeshCom-RC-732238bd`), and the
  foreign edit stays untouched and uncommitted in this tree; the desk session was asked to use its own worktree.
- 2026-11-24 about 16:25: the desk session commits its WEB-WX-01 change (P-0394, `web_functions.cpp` WX table, a
  pytest, `test-suite-map.md`) on fork-dev with the operator's approval. It is newer than the bench images
  (`732238bd`) and not in this campaign's bench evidence; the release commit must either be benched with it or the
  notes must say the WX table change is host-tested only. Operator decides at the tag.

## Operator decisions (2026-11-24 evening)

- The FORMAL-01 TLS allocation cap (`fw_tls_alloc_cap.h`, about 95 lines) travels with Auto Update in the combined PR.
- Desk commit `35ae3418` (WEB-WX-01, WX table) goes into the release as a low-risk web page change, host-tested.
- Margin near the sensitivity limit: not testable on the bench table; stated as a gap, no attenuator test.

#### Run A, image A `0x242B`, 2 dBm (17:02-17:14)

| Sender   | Receiver | Sent | Decoded direct | Relay only | Errors in window | RSSI med/min | SNR med/min |
| -------- | -------- | ---- | -------------- | ---------- | ---------------- | ------------ | ----------- |
| DK5EN-90 | DK5EN-1  | 13   | 13             | 0          | 1                | -39 / -41    | 6 / 5       |
| DK5EN-90 | DK5EN-92 | 13   | 13             | 0          | 0                | -56 / -56    | 13 / 10     |
| DK5EN-90 | DK5EN-14 | 13   | 13             | 0          | 0                | -64 / -65    | 6 / 5       |
| DK5EN-1  | DK5EN-92 | 12   | 11             | 1          | 0                | -46 / -48    | 13 / 11     |
| DK5EN-1  | DK5EN-90 | 12   | 9              | 3          | 0                | -34 / -34    | 6 / 6       |
| DK5EN-1  | DK5EN-14 | 12   | 12             | 0          | 0                | -38 / -41    | 6 / 5       |
| DK5EN-14 | DK5EN-1  | 12   | 9              | 0          | 0                | -48 / -48    | 6 / 5       |
| DK5EN-14 | DK5EN-92 | 12   | 10             | 0          | 0                | -58.5 / -59  | 10 / 9      |
| DK5EN-14 | DK5EN-90 | 12   | 7              | 0          | 0                | -65 / -66    | 7 / 7       |
| DK5EN-92 | DK5EN-1  | 12   | 11             | 0          | 0                | -36 / -37    | 6 / 6       |
| DK5EN-92 | DK5EN-90 | 12   | 7              | 0          | 0                | -42 / -43    | 6 / 6       |
| DK5EN-92 | DK5EN-14 | 12   | 11             | 0          | 0                | -46 / -46    | 6 / 6       |

#### Run A-low, -9 dBm as in B-low (17:15-17:27)

| Sender   | Receiver | Sent | Decoded direct | Relay only | Errors in window | RSSI med/min | SNR med/min |
| -------- | -------- | ---- | -------------- | ---------- | ---------------- | ------------ | ----------- |
| DK5EN-90 | DK5EN-1  | 12   | 10             | 2          | 0                | -39 / -40    | 6 / 6       |
| DK5EN-90 | DK5EN-92 | 12   | 11             | 1          | 0                | -55 / -56    | 10 / 10     |
| DK5EN-90 | DK5EN-14 | 12   | 11             | 1          | 0                | -65 / -66    | 6 / 5       |
| DK5EN-1  | DK5EN-92 | 12   | 12             | 0          | 0                | -52 / -53    | 12 / 11     |
| DK5EN-1  | DK5EN-90 | 12   | 9              | 3          | 0                | -43 / -44    | 6 / 6       |
| DK5EN-1  | DK5EN-14 | 12   | 12             | 0          | 0                | -50 / -51    | 6 / 5       |
| DK5EN-14 | DK5EN-1  | 12   | 10             | 0          | 0                | -61 / -61    | 6 / 5       |
| DK5EN-14 | DK5EN-92 | 12   | 11             | 0          | 0                | -70 / -71    | 12 / 10     |
| DK5EN-14 | DK5EN-90 | 12   | 7              | 0          | 2                | -77 / -78    | 6 / 6       |
| DK5EN-92 | DK5EN-1  | 12   | 8              | 0          | 0                | -37 / -38    | 6 / 6       |
| DK5EN-92 | DK5EN-90 | 12   | 9              | 0          | 0                | -42 / -44    | 7 / 6       |
| DK5EN-92 | DK5EN-14 | 12   | 12             | 0          | 0                | -47 / -48    | 6 / 5       |

#### Every missed frame, classified (what the receiver was doing at the time)

| Run   | RAK as receiver: missed / sent | of these: own CAD busy | own TX | other | ESP32 as receivers: missed RAK frames                                        |
| ----- | ------------------------------ | ---------------------- | ------ | ----- | ---------------------------------------------------------------------------- |
| B     | 7 / 40                         | 7                      | 0      | 0     | 0                                                                            |
| B-low | 10 / 40                        | 7                      | 2      | 1     | 1 (DK5EN-1, CAD busy)                                                        |
| A     | 12 / 37                        | 11                     | 1      | 0     | 0                                                                            |
| A-low | 10 / 39                        | 10                     | 0      | 0     | 4: one frame lost at all three (DK5EN-1 sent at the same time), one CAD busy |

- **Verdict, active part: no sync-word side effect at the bench's signal levels**, neither RAK old (`0x242B`) nor
  RAK new (`0x24B4`) against Heltec V3 (SX1262), T-Deck Plus (SX1262) and T-Beam v1.2, in both directions. Every
  loss is explained by channel access (the receiver was in its own CAD or TX) or by a collision. Error lines in the
  windows: 0-2 per pair, same under A and B.
- Differences between A and B in the "decoded" counts follow how often the RAK had gateway ACKs queued, not the
  image. All SNR values +5 to +13 dB, so nothing here speaks to the margin near the sensitivity limit.

## Step 6: wave 5 bench

### #1308 EXTUDP register query: PASS (2026-11-24 17:31, DK5EN-92, T-Beam, RC image)

- Set for the test: `--extudpip 192.168.68.58` (the Mac), `--extudp on`; restored to `--extudp off`, no peer
  (`--info` after: `EXTUDP off ...EXT IP` empty), as before.
- `{"type":"reg","q":"SN"}`: one reply `TYP SN`, 21 keys, none matching the secret rule; `q:"I"`: one reply, no
  `BPIN`; `q:"XX"`: `{"type":"reg","err":"unknown"}`; two queries 200 ms apart: one reply (limiter); `q:"all"`: 16
  registers (TM, W, IO, I, IS1, IS2, G, SE, S1, SW, S2, SN, SN1, AN, SA, CONFFIN) and `done`, no secret key in any.
- Not testable with one host: replies only to the configured peer (host test only).
- Logs: `wave5/1308-result.json`, `1308-udp.log`, `1308-dk5en92-console.log`.

### #1307 hold during server outage: PASS (2026-11-24 17:35-17:39, DK5EN-1, Heltec V3 gateway, INSTRUMENT image)

- Image: `rc-732238bd-heltec_v3-INSTRUMENT.bin` (md5 `606f373d`, clean worktree); `--udphold` exists only there.
  Never on DK5EN-98.
- `--udphold outage on` -> `[GW];hold;outage;start`; a group-9999 text from DK5EN-92 over LoRa (`x4106E045`,
  RSSI -37) -> `held;1;bytes;85;cap;6144;pushed;1`.
- `--udphold outage off` after 149 s -> `outage;end;dur_s;149;held;1`, `replay;start;n;1`, `replay;done;replayed;1`,
  `TX-UDP ... x4106E045` (same id), ring empty, `drop_full 0`, `drop_age 0`.
- DK5EN-98 gated the same frame to the server at once (it heard it too); the server-side dedup of the replay is not
  checked here.
- Operational finding: a node with a staged Auto Update image installs it when it enters Safeboot, so the first
  `webflash.py` pass fails with `Broken pipe` and the second flashes (DK5EN-1, twice today). Before re-flashing a
  bench node after an AU run, expect two passes.

### Step 4 completed (2026-11-24 18:40)

- Passive A (17:32-18:32): as B, only DL2JA-2 (SX126x, 92 copies at DK5EN-98, SNR below -10 dB), none at the RAK, no
  nRF52 field transmitter in either window.
- TRACK under image B (18:34): `--track on`, one `--sendtrack` (APRS 433.775 MHz, then back to 433.175 MHz after
  `OnTXDone`), `--track off`; check run `B-after-track`: RAK decodes 10/12 of the ESP32 nodes, they decode 12/12 of
  the RAK. `--loradebug` on the RAK was switched off by the test sequence and restored.
- RAK back on image B (`52723f33`, DFU 18:33). Result doc: [syncword-ab-bench-20261124.md](syncword-ab-bench-20261124.md).

### #1303 remote management: node side PASS (2026-11-24 19:2x, DK5EN-92 -> DK5EN-1 over LoRa, night images)

- DK5EN-1 `--remotemgmt on` for the test, `off` afterwards (as before). Sender DK5EN-92, saved slot 0 (DK5EN-1), web API
  `POST /rmsend`, answer read from `/rmstatus`.

| Command      | Answer                                                     | Time | Expected                             |
| ------------ | ---------------------------------------------------------- | ---- | ------------------------------------ |
| `pins`       | `ok ow=0 btn=0 an=0 wind=0`                                | 10 s | read                                 |
| `gpio`       | `ok m=0 o=0000000000000 i=0000000000000 l=0000000000000`   | 10 s | read                                 |
| `pins btn 0` | `err blocked`                                              | 10 s | button pin is read-only              |
| `pins ow 0`  | `ok ow=0 btn=0 an=0 wind=0`                                | 10 s | write of the stored value, read back |
| `gpio 13 on` | `err master`                                               | 21 s | refused, master switch off           |
| `txq`        | `ok q=0/20 bp=quiet tx=3 rt=0 dr=0 u=0`                    | 10 s | Queues card row                      |
| `mbox`       | `ok m=off u=0/50 b=0 a=0/20 st=0 dl=0 ak=0 dr=0 bl=0 nt=0` | 10 s | Queues card row                      |
| `maxhop`     | `ok t=4 p=2`                                               | 10 s | Queues card row                      |

- **Not done: the Read button click itself.** The Chrome extension was not connected, so the three Queues requests
  were sent with the same parameters the card's Read buttons send (`mcrm.js` `data-act=qread`), not by a click.
  The card code is upstream since #1281.
- A real pin change (`gpio` with the master switch on, a jumper) was not done: no jumper on the bench.

## Step 7: night (started 2026-11-24 19:25)

- Images: night images of `732238bd` with `MC_AU_TEST=1` (adds only `--update forget` and `--update unstage`) and tag
  11.21, built in the clean worktree: DK5EN-1 `6d80384a`, DK5EN-92 `ea483de2`, DK5EN-14 `8b82e179` (md5 prefixes).
  Same AU code as the RC image.
- `tools/bench/ota_lowheap_trigger.py`, one detached run per node, 3 attempts 3 h apart (DK5EN-1 from 19:25, -92
  from 19:35, -14 from 19:45): check, install (stage only), status, forget, unstage. Heap and uptime from `--info`
  before and after each attempt (`after_min` = lowest free since boot); a dropped console (reboot) reconnects.
- Tool change for this: `--update unstage` after every attempt (otherwise attempts 2 and 3 only report "already
  staged"), `--info` heap fallback, reconnect; `tools/bench/test_ota_lowheap_trigger.py`.
- Output: `tools/bench/runs/harness/rc-bench-20261125/night/`. DK5EN-98 logger (read-only) runs until 09:00 as the
  reference for reboots and channel.
- Attempt 1 DK5EN-1 19:26: staged, download 1,180,497 B in 11.4 s, records 4/4 in the AU buffer, after_min 21,736 B.

- Night attempt 1 (19:26-19:47): all three staged. DK5EN-1 after_min 21,736 B; DK5EN-92 iblk at start 42,996 B,
  after_min 11,700 B; DK5EN-14 after_min 30,728 B (internal). Attempts 2 and 3 at +3 h and +6 h.

## Step 8: documents (drafts 2026-11-24 evening)

- `release-notes.md`: new top section for `v4.40a.11.25` (Action needed: nRF52 sync word), 11.22 folded into the
  history, verification, known gaps (FORMAL-01 not benched one by one, sync-word gaps, CAD-RXLOSS-01, wave-3 items
  without bench), upstream status. Placeholders `[morning]`: night result and gate counts.
- `docs/release-journal.md`: German entry, same placeholders. `FLASH_VERSION` bump to 20261125 goes with the
  release-doc commit in the morning (stamp only: `--info` and the STAT line).
- PR text draft: [pr-combined-text-20261125.md](pr-combined-text-20261125.md); scope:
  [pr-combined-scope-20261125.md](pr-combined-scope-20261125.md). Issue drafts:
  [upstream-issue-drafts-20261125.md](upstream-issue-drafts-20261125.md).
- Tag name `v4.40a.11.25` free on origin and upstream (2026-11-24).
