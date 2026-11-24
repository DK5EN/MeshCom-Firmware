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

| Step | What                                                    | Status  |
| ---- | ------------------------------------------------------- | ------- |
| 1    | mbedTLS copy removed, host gate, AU re-measured -1, -92 | code done (`5488f0bc`), bench in step 3 |
| 2    | full host gate, every board env                         | done: 35/36 envs, external-radio its own `#error` |
| 3    | flash all bench nodes + DK5EN-98 (OTA), AU on T-Deck    | done 2026-11-24 15:38 |
| 4    | sync-word A/B on DK5EN-90                               | running |
| 5    | `tools/hmac_connect.py` fixes (HMAC-WIN-01)             | done (`732238bd`) |
| 6    | wave 5 bench (#1307, #1308, #1303)                      | open    |
| 7    | one night: AU check + download x3 per ESP32 node        | open    |
| 8    | documents (release notes, journal, PR text, issues)     | open    |

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

| Node     | Board       | Image (all commit `732238bd`)                     | md5        | Method          | Back with                         |
| -------- | ----------- | ------------------------------------------------- | ---------- | --------------- | --------------------------------- |
| DK5EN-1  | Heltec V3   | `heltec_wifi_lora_32_V3`, `MC_BUILD_TAG=v4.40a.11.21` | `60cdd358` | OTA, webflash.py | build 15:25:41, guard ok, gateway on |
| DK5EN-92 | T-Beam v1.2 | `ttgo_tbeam`, tag 11.21                            | `c782b381` | OTA, webflash.py | build 15:26:40, guard ok          |
| DK5EN-14 | T-Deck Plus | `t_deck_plus`, tag 11.21                           | `4d2c8378` | OTA, webflash.py | build 15:27:30, guard ok          |
| DK5EN-90 | RAK4631     | `wiscore_rak4631`, image B (`0x24B4`)              | `52723f33` (hex) | serial DFU (`pio -t upload`) | build 14:56:11, guard ok |
| DK5EN-98 | Heltec V3   | `heltec_wifi_lora_32_V3`, no tag                   | `c1e9adc9` | OTA, webflash.py, 15:38 | build 15:11:17, all settings as before, server connected |

- DK5EN-1: the first OTA attempt broke off (`Broken pipe` during the upload). Entering Safeboot installed the 11.22
  image staged there by the AU-30 bench on 2026-11-24; the second attempt flashed the RC image. Bench node only.
- DK5EN-98: the single approved OTA flash; `--info` before and after identical except the build (mesh on, setlog on,
  EXTUDP target, 433.175 MHz, 22 dBm, AU notify dev, gateway on). AU there: tag `(local)`, nothing offered.

Auto Update, check + install (stage only, never apply), `tools/bench/ota_lowheap_trigger.py --attempts 1`, plain
images, logs `tools/bench/runs/harness/rc-bench-20261125/au/`:

| Node     | Free / largest block at start | Lowest free during job | AU buffer        | TLS records in buffer | Download              | Erase, idle0max | Result |
| -------- | ----------------------------- | ---------------------- | ---------------- | --------------------- | --------------------- | --------------- | ------ |
| DK5EN-1  | 103,120 / 90,100 B            | 19,564 B               | 33,440 B         | check 2/2, dl 4/4     | 1,180,497 B in 11.2 s | 2.7 s, 164 ms   | staged |
| DK5EN-92 | 62,300 / 57,332 B             | 11,332 B               | 33,440 B, twice  | check 2/2, dl 4/4     | 1,240,686 B in 13.4 s | 6.0 s, 362 ms   | staged, image check in the job |
| DK5EN-14 | 61,696 / 51,188 B (internal)  | 40,196 B               | none (PSRAM path)| n/a                   | 1,617,579 B in 20.7 s | 2.4 s, 84 ms    | staged |

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

| Sender | Receiver | Sent | Decoded direct | Relay only | Errors in window | RSSI med/min | SNR med/min |
| ------ | -------- | ---- | -------------- | ---------- | ---------------- | ------------ | ----------- |
| DK5EN-90 | DK5EN-1 | 12 | 12 | 0 | 1 | -50 / -51 | 6 / 5 |
| DK5EN-90 | DK5EN-92 | 12 | 12 | 0 | 0 | -53.5 / -55 | 11 / 10 |
| DK5EN-90 | DK5EN-14 | 12 | 12 | 0 | 0 | -70 / -71 | 6 / 6 |
| DK5EN-1 | DK5EN-92 | 12 | 11 | 1 | 0 | -50 / -51 | 10 / 9 |
| DK5EN-1 | DK5EN-90 | 12 | 11 | 0 | 0 | -44 / -45 | 6 / 6 |
| DK5EN-1 | DK5EN-14 | 12 | 12 | 0 | 0 | -65 / -66 | 6 / 5 |
| DK5EN-14 | DK5EN-1 | 12 | 11 | 0 | 0 | -74 / -74 | 6 / 5 |
| DK5EN-14 | DK5EN-92 | 12 | 11 | 0 | 0 | -75 / -76 | 10 / 9 |
| DK5EN-14 | DK5EN-90 | 12 | 9 | 0 | 2 | -72 / -72 | 7 / 6 |
| DK5EN-92 | DK5EN-1 | 14 | 13 | 0 | 0 | -45 / -45 | 6 / 5 |
| DK5EN-92 | DK5EN-90 | 14 | 10 | 0 | 0 | -42 / -42 | 7 / 6 |
| DK5EN-92 | DK5EN-14 | 14 | 13 | 0 | 0 | -65 / -65 | 6 / 5 |

#### Run B-low, -9 dBm on DK5EN-1, -14, -90; DK5EN-92 at 2 dBm (refuses below -4) (15:54-16:06)

| Sender | Receiver | Sent | Decoded direct | Relay only | Errors in window | RSSI med/min | SNR med/min |
| ------ | -------- | ---- | -------------- | ---------- | ---------------- | ------------ | ----------- |
| DK5EN-90 | DK5EN-1 | 12 | 11 | 1 | 0 | -49 / -51 | 6 / 5 |
| DK5EN-90 | DK5EN-92 | 12 | 12 | 0 | 0 | -58.5 / -74 | 11 / 10 |
| DK5EN-90 | DK5EN-14 | 12 | 12 | 0 | 0 | -69 / -77 | 6 / 6 |
| DK5EN-1 | DK5EN-92 | 12 | 12 | 0 | 0 | -55 / -59 | 11 / 10 |
| DK5EN-1 | DK5EN-90 | 12 | 12 | 0 | 0 | -49 / -52 | 6 / 6 |
| DK5EN-1 | DK5EN-14 | 12 | 12 | 0 | 0 | -49 / -68 | 6 / 5 |
| DK5EN-14 | DK5EN-1 | 12 | 10 | 0 | 0 | -59 / -60 | 6 / 5 |
| DK5EN-14 | DK5EN-92 | 12 | 11 | 0 | 0 | -64 / -68 | 10 / 9 |
| DK5EN-14 | DK5EN-90 | 12 | 9 | 1 | 0 | -75 / -76 | 6 / 6 |
| DK5EN-92 | DK5EN-1 | 13 | 12 | 0 | 0 | -43 / -44 | 6 / 5 |
| DK5EN-92 | DK5EN-90 | 13 | 8 | 0 | 0 | -47 / -48 | 6 / 6 |
| DK5EN-92 | DK5EN-14 | 13 | 12 | 0 | 0 | -42 / -44 | 6 / 5 |

## Shared-tree incident (2026-11-24 16:10)

- Another session (`telegram-12`, started about 16:06, the ticket desk) left an uncommitted edit in
  `src/web_functions/web_functions.cpp` (WEB-WX-01, P-0394) while four images were built in this tree (16:06-16:10).
  Those four images were deleted unused. The flashed images (built 15:11-15:27), RAK image A (about 15:40) and the
  step-2 builds are older than that session.
- From now on every RC image is built in a clean worktree at the campaign commit (`../MeshCom-RC-732238bd`), and the
  foreign edit stays untouched and uncommitted in this tree; the desk session was asked to use its own worktree.
