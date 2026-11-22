# OTA-LOWHEAP-01: Auto Update refuses with "Not enough free memory" after long uptime

Date: 2026-11-22. Tree: `fork-dev` at `afde3da0`, read-only analysis, no build, no flash. Status: open, nothing
reproduced on the bench yet. Tracked as fork issue DK5EN/MeshCom-Firmware#3, backlog row `OTA-LOWHEAP-01`;
related rows `AU-24`, `AU-25`.

## 1. Problem

Field reports from the 11.22 rollout on 2026-11-22:

| Report  | Board                     | What happened                                                                                                                                                |
| ------- | ------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Field 1 | Heltec V3                 | Update 11.21 to 11.22. Node had run overnight. Three attempts failed with "not enough memory" during the download. A reboot did not help, a power cycle did. |
| Field 2 | T-Beam Supreme, Heltec V3 | Update to 11.22. Worked only on the 5th to 10th attempt.                                                                                                     |

Facts that frame the analysis:

- The image that refused is the **old** one (11.21 or older). 11.22 itself has not run the gate yet on those nodes.
- The message is the web text for every `lowheap*` token (`src/web_functions/au_ui_state.h:170`). It comes from the
  pre-TLS heap gate, not from flash space (Safeboot's "Not enough space" is a different text).
- Both boards are ESP32-S3 without the PSRAM-TLS path in use, so the gate measures internal heap only.
- "Works after some retries" means the margin is small and depends on the heap state at that moment. It is not a
  board that can never pass.

## 2. What the gate checks on an ESP32-S3

`heapGateEval()` (`src/esp32/fw_update_net.cpp:701`) on S3:

- `FWNET_MIN_FREE` is 0 on non-classic targets: **no total-free check**.
- Largest internal block must be at least 17000 B, then `twoRecBufProbe()` must get **two 16717 B blocks at the same
  time** (mbedTLS in/out record buffers).
- Check and download are two TLS sessions; the download adds a 4 KB staging block.

So the S3 refusal is a **fragmentation** verdict (no two contiguous 16.7 KB pieces), not "RAM full".

The S3 also lacks the rescue paths the classic ESP32 has (`AU_BLE_PAUSE` is 1 only for `CONFIG_IDF_TARGET_ESP32`,
`src/esp32/fw_update_net.h:240`):

| Rescue path                                | Classic ESP32 | ESP32-S3                 |
| ------------------------------------------ | ------------- | ------------------------ |
| BLE pause before TLS (frees about 25 KB)   | yes           | only under the BLE guard |
| Job restart on a new stack when fragmented | yes           | no                       |
| Purge closed web sockets (`twPurge`)       | yes           | yes                      |
| Shed KISS TCP / net console                | yes           | yes                      |

## 3. What fills the RAM

### 3.1 Not the messages

Unread messages cannot grow the heap. The phone rings are fixed static byte rings (`src/loop_functions.cpp:833-836`),
the server-outage hold is static `.bss` (`UDP_HOLD_BYTES`, `src/configuration_global.h:344-388`). A full ring drops
old entries; it does not allocate.

### 3.2 Static RAM grew 21 KB since September, unnoticed

PlatformIO `RAM: used` from the CI release builds, compared with `tools/resource_baseline.json` (2026-09-11):

| Env                      | Baseline 09-11 | 11.06 CI | 11.08 CI | 11.21 CI | 11.22 CI | Delta since 09-11 |
| ------------------------ | -------------- | -------- | -------- | -------- | -------- | ----------------- |
| `heltec_wifi_lora_32_V3` | 116,244        | 137,212  | 137,268  | 136,924  | 137,052  | **+20,808**       |
| `ttgo_tbeam_supreme`     | 116,572        | 137,540  | 137,596  | 137,244  | 137,372  | **+20,800**       |
| `ttgo_tbeam` (classic)   | 114,264        | 107,256  | 107,304  | 106,960  | 107,080  | -7,184            |
| `t_deck_plus`            | not listed     | 156,612  | 156,948  | 158,084  | 158,204  |                   |

Reading:

- November is flat: 11.22 did not make it worse. The field problem is not a 11.22 regression of the static image.
- The S3 boards lost about 21 KB of heap headroom between September and November. The classic class got smaller (the
  reclaim work), the S3 class got bigger. Nobody saw it because the baseline was never refreshed and
  `tools/resource_watch.py` does not run in CI (it only self-tests in stage 2).
- The comment in `fw_update_net.cpp:118` ("S3 runs with 119 KB free, min 63 KB during TLS") predates those 21 KB. The
  S3 margin is unmeasured on the current tree.

### 3.3 Dynamic and uptime-dependent (the likely trigger)

Our own code allocates little at run time (about 30 `malloc` sites, mostly lazy one-shot buffers). What changes with
uptime is framework heap, and it fragments rather than leaks:

- lwIP / WiFi: TCP control blocks, TIME_WAIT sockets and pbufs from web GUI polling (the reason `twPurge()` and
  `tools/bench/au_gate_burst.py` exist). A connected web client costs 6-8 KB.
- The scheduled AU checks during the night: each TLS session allocates and frees about 60 KB in many pieces.
- Arduino `String` temporaries (about 570 uses in `src/` outside libraries).
- NimBLE on S3, which is never paused when no phone is connected.

### 3.4 Why a soft reboot did not help

Every reset rebuilds the heap. Only two small `RTC_NOINIT` structs survive a soft reset (`g_loopCrumb`,
`instrument.cpp` carry), neither touches the heap. So the state after the reboot must have recreated the condition.
Hypotheses, to be told apart by the bench runs in section 4:

- **H1** A browser tab with the web GUI kept polling and hit the node right after the reboot (burst of page loads, then
  update click).
- **H2** A phone reconnected over BLE right after the reboot (S3 cannot pause BLE with a phone up).
- **H3** The "reboot" was not a chip reset (which button or command was used is unknown).
- **H4** The power cycle only changed the timing (longer wait before the click, browser had given up polling).

Question back to the reporter of field report 1: which reboot (web button, `--reboot`, app), was a browser tab or the app open, how long
between boot and update click.

## 4. How to reproduce

Bench rules apply: `identity_guard.py` first, `lsof` before any port, TX at most 2 dBm, traffic only as PN to `TEST`
or own nodes, never DK5EN-98. Opening a USB port reboots an ESP32, so the trigger and the readout go over the net
console (port 2323), not serial.

| Step | Node                      | Image                                            | What                                                                                                                                                         | Pass / fail signal                                  |
| ---- | ------------------------- | ------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------ | --------------------------------------------------- |
| R1   | DK5EN-1 (Heltec V3)       | CI image `v4.40a.11.21`, exactly the field image | Gateway on, WiFi, AU scheduled checks on, a web GUI tab polling, normal mesh RX. Run 8-12 h. Then start check + download from the web GUI without rebooting. | Field symptom seen or not; `heap=` from STAT lines  |
| R2   | same                      | same                                             | After a refusal: soft reboot and click at once with the tab open; soft reboot with tab closed; power cycle. Each 3 times.                                    | Discriminates H1, H3, H4                            |
| R3   | same                      | same                                             | R2 with the phone app connected over BLE instead of the tab.                                                                                                 | H2                                                  |
| R4   | DK5EN-1                   | INSTRUMENT build of the 11.21 commit             | Repeat R1 with `--heaplog 60`. Read `[AU];refuse;lowheap;ifree;..;iblk;..;twobuf;..` and the `[HEAP]` free / largest block curve over the night.             | Shows fragmentation (free high, block low) vs. lack |
| R5   | T-Beam Supreme if on hand | INSTRUMENT, current `fork-dev`                   | Same as R4 on the current tree.                                                                                                                              | Is current `fork-dev` worse or better than 11.21    |
| R6   | DK5EN-1                   | INSTRUMENT                                       | Fast-forward: loop `au_gate_burst.py` page bursts plus forced checks for 1 h without reboot; plot largest block over time.                                   | A 1 h recipe that replaces the overnight run        |

R6 is the one to keep: if it reproduces in an hour, it becomes the regression test (T3).

## 5. Regression tests so it shows up before a release

| ID  | Test                         | Where                     | What it catches                                                                                                                                                                                                    | Effort |
| --- | ---------------------------- | ------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ------ |
| T1  | Static RAM budget in CI      | `meshcom-ci.yml`          | Run `resource_watch.py check` per env on every CI build; fail when `dram0_0_seg` grows more than an agreed budget (for example 1 KB) without a baseline update in the same commit. Refresh the 09-11 baseline now. | small  |
| T2  | Idle heap per RAM class      | stage 3, `bench_suite.py` | `heap_idle.py` against `tools/heap_baseline.json` for Heltec V3, T-Beam Supreme, T-Beam and T-Deck Plus (today: T-Beam only, from 11-06, not in the suite).                                                        | small  |
| T3  | Aged-heap AU gate            | stage 3 / soak            | The R6 recipe: no reboot, traffic and web bursts, then N check + download rounds over the net console. Pass = every round reaches the server. Also report the margin: largest block minus 2 x 16717.               | medium |
| T4  | Gate margin in the STAT line | firmware, `[LOG] STAT`    | Add the largest internal block (`iblk=`) next to `heap=`. Field logs and log reports then show the margin before anyone clicks Update.                                                                             | small  |
| T5  | Nightly AU on test nodes     | NIGHTLY-01                | Every night the test nodes update themselves after a full day of uptime. That is T3 under real conditions, for free once NIGHTLY-01 runs.                                                                          | given  |

Release rule: no tag unless T1 is green and T3 passed on one board of each RAM class (classic ESP32 without PSRAM, S3
without PSRAM, S3 with PSRAM).

## 6. Fix approach

In this order; steps 3 to 5 are decided on the data from section 4.

1. **Field guidance now.** Release note and FAQ: on "Not enough free memory", close web GUI tabs and the app, power
   cycle the node, start the update right after the boot.
2. **Find the 21 KB.** Compare the symbol tables of the 09-11 and the 11.22 Heltec V3 ELF (`tools/nm_symsum.py`), get
   back what is not needed on S3, and refresh the baseline (T1).
3. **Reserve the TLS blocks.** Allocate one 2 x 16.7 KB reserve block at boot while AU is enabled and free it right
   before the TLS session. That turns a fragmentation lottery into a guarantee, at the cost of about 34 KB less heap
   in normal operation. Needs the R4 numbers to know whether the S3 can afford it.
4. **Update from a fresh boot.** When the gate refuses, store "update pending" in NVS, reboot, and run the job early in
   the boot before web server, BLE and services take their heap. Self-healing for every board; replaces the manual
   power cycle.
5. **Give the S3 the classic rescue paths.** BLE pause when no phone is connected and the job restart on a new stack
   (`fwJobRestartDue`) are classic-only today.
6. **Structural, long term:** download in Safeboot, where more than 200 KB are free and no service runs
   (`docs/meshcom-heap-verdict-20261018.md`, section 6).
7. **Process:** every PR states its static RAM delta per RAM class (fork review rule in `docs/review-fork.md`), the way
   REVIEW.md R4 does for the LoRa byte budget.

## 7. Sources

- `src/esp32/fw_update_net.cpp` (gate: 701-722, 768-822; job: 1480-1590), `src/esp32/fw_update_net.h:236-246`,
  `src/fw_update.h:859-906`, `src/web_functions/au_ui_state.h:71-171`
- `docs/meshcom-heap-verdict-20261018.md` (classic heap map, Safeboot download proposal)
- `tools/resource_baseline.json` (2026-09-11), `tools/heap_baseline.json` (2026-11-06), `tools/bench/heap_idle.py`,
  `tools/bench/au_gate_burst.py`
- CI runs `42306803386` (11.06), `42508948997` (11.08), `44895542343` (11.21), `44939256914` (11.22), `RAM:` lines
