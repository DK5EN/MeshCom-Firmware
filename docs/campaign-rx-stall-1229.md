# Campaign: RX loss during web stalls (upstream issue #1229)

Status: **approved 2026-10-23, W0 done, W1a next.** Defect ID **WEB-RX-01**. Written 2026-10-22 on `fork-dev`
at `92d3adbf`. All line numbers below refer to that commit unless marked `upstream/dev`.

A later agent picks up here: read this file (section 10 is the progress log), then `docs/RESUME.md`.
Dispatch mechanics follow `/orchestrate-waves`.

Operator answers to section 8 (2026-10-23): all recommended answers accepted -- sender DK5EN-92 on an INSTRUMENT

- `MC_INJECT_HOOKS` build (flash OK for DK5EN-1, -14 (OTA only), -92, -90); the operator runs
  `sudo tools/bench/web_throttle.sh` per arm; go/no-go 5 percentage points; 4 park slots; no DK5EN-98 soak for
  now; the #1229 reply after W1b with numbers. Precondition of section 3 met: `6b2fdfac` (#1224) is in fork-dev
  since the sync `414a328c`. The campaign runs together with AU-14/AU-17, the #1235 hardware check, RM-NVS-01
  and BL-01 on the same bench session ([campaign-bench-20261023.md](campaign-bench-20261023.md)).

## 1. Context

- Upstream issue: <https://github.com/icssw-org/MeshCom-Firmware/issues/1229> ("Change WebApp
  Architecture", opened 2026-10-22 by Mateusz-Krajewski). Proposal: move the web app into its own
  FreeRTOS task at priority 0, possibly the gateway networking too, because slow web requests
  starve the loop and drop LoRa packets.
- Our verdict: **diagnosis right, cure wrong**. The web stall is real, but moving the web server
  out of the loop is the expensive fix. Move the small critical part (getting the packet off the
  chip) out of harm's way instead.
- Reply to the issue: drafted in English in the session of 2026-10-22, **not posted**. Operator
  reviews it; the plan suggests posting after W1b so it carries numbers (open question 6).

## 2. Verified findings (code, not measured)

| Claim in #1229                                 | Holds? | Evidence                                                                                                                                                                                                                                        |
| ---------------------------------------------- | ------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Web app runs in `loop()` on ESP32              | yes    | Blocking `WiFiServer` (`src/web_functions/web_commonServer.h:29`), `loopWebserver()` called from `esp32loop()` (upstream/dev `src/esp32/esp32_main.cpp:4161`)                                                                                   |
| Radio RX depends on the loop                   | yes    | ISR `setFlagReceive()` (`esp32_main.cpp:901`) only sets `receiveFlag`; `checkRX()` (`:5203`) runs in the loop. SX126x holds one packet in continuous RX, so a second frame during a stall is lost                                               |
| "Crash mitigated, stall remains"               | yes    | The mitigation is our PR #1224 (`204bb7ad`, merged upstream as `6b2fdfac`): stall abort after `WEB_TX_STALL_MS` 2000, page budget `WEB_TX_PAGE_MAX_MS` 15000 (`src/web_functions/web_txbuf.h:27-31`). Inside that window the loop stays blocked |
| Packets are actually dropped                   | open   | Not measured. At ~0.5-1 s airtime per frame, a 15 s stall on a busy channel should lose frames. W1 measures it                                                                                                                                  |
| Gateway uploads stall the loop                 | weak   | Gateway is UDP; DNS is already polled non-blocking (`wifiDnsPoll()`). No evidence in the issue                                                                                                                                                  |
| "Only one priority level below loop available" | no     | `vTaskPrioritySet()` on the loop task, or pinning to core 0, solves it                                                                                                                                                                          |

Why a priority-0 web task is the wrong shape:

- `esp32loop()` has no unconditional `delay()`/`yield()`, so a priority-0 task on core 1 starves.
- Web handlers call `commandAction()`, write `meshcom_settings`, trigger flash saves and render the
  MHeard/message rings that `OnRxDone()` writes. A coarse mutex re-creates the block; pre-rendering
  pages in RAM does not fit (scaffold `/` is ~94 kB, classic ESP32 has no PSRAM).
- nRF52 is not affected: `OnRxDone()` already runs in its own LORA task there.

## 3. Recon results that shape the plan

- **Hook point:** `txFeed()` in `src/web_functions/web_txbuf.cpp:53` (bound at `:69`) runs in every
  socket wait (`web_txbuf.h:127,137`), next to `vTaskDelay(1)` at `web_txbuf.cpp:57`. Hook only
  there; the other `meshcom_wdt_feed()` callers (`web_functions.cpp`, `gps_functions.cpp`) stay
  untouched.
- **Readout:** `checkRX()` (`esp32_main.cpp:5203-5320`): `is_receiving` guard, `getPacketLength()`
  before `readData()` (`:5245`, see the comment block above it), RSSI/SNR (`:5255`), RX restart
  (`:5281`), then `OnRxDone(payload, len, rssi, snr)` (`:5313`). CRC and header-error paths follow
  at `:5338` and `:5398`.
- **Instrumentation already present (INSTRUMENT builds):** `INSTR_IRQ_STAMP()` in the ISR and
  `INSTR_IRQ_READ()` after `readData()` give `irq_to_read_us` per frame (`src/instrument.h:125`);
  `[INSTR-LOOP];gap;ms;...;in;<section>` names the loop section that held the loop
  (`src/instrument.cpp:174`); `LOOP_SECTION(LSEC_WEB)` marks the web call.
- **Traffic source:** TM-06 TX burst (`src/test_inject.cpp:151ff`, behind `MC_INJECT_HOOKS`) sends N
  numbered frames to group `TEST` (`:175-176`).
- **Harness to reuse:** `tools/bench/onrx_matrix.py` parses `[INS] ONRX ... irq_to_read_us=...`
  lines and does identity guard + HMAC login on 2323. `tools/bench/web_throttle.sh` slows a
  node's port-80 replies (needs sudo; delay 2500 = stall, 1200 = trickle).
- **Precondition:** `fork-dev` does not contain the merge commit `6b2fdfac` of #1224 yet
  (`git merge-base --is-ancestor 6b2fdfac HEAD` says no). W0 syncs it.

## 4. Waves

| Wave           | Content                                                                                                                                                                                                                                                    | Who                               | Gate / decision                                                                                                                                       |
| -------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------- |
| W0             | `/sync-upstream` (merge `6b2fdfac`). Copy this file to `docs/campaign-rx-stall-1229.md` with pre-registered hypotheses. New defect ID (grep `docs/architecture/08-defect-catalogue.md` for collisions). Entries in `docs/BACKLOG.md` and `docs/RESUME.md`. | orchestrator                      | `tools/regression.sh --stage 1,2` green                                                                                                               |
| W1a            | Bench harness `tools/bench/rx_stall_bench.py` + offline analyser test on fixture logs (section 5)                                                                                                                                                          | implementer, Sonnet 5.5 medium    | Own pytest only. Tools-only wave, advisor skipped (say so in the report)                                                                              |
| W1a (parallel) | Read-ahead for W2: every write of `receiveFlag`, `is_receiving`, `bEnableInterruptReceive/Transmit` and of the radio RX state (TX start, CAD, RX-timeout handler). Shared SPI buses (T-Deck: LoRa + TFT + SD). Does any web page builder touch SD or TFT?  | scout, Haiku 5.5 low              | Report of at most 15 lines                                                                                                                            |
| W1b            | Bench run, hardware only, serialized (section 5)                                                                                                                                                                                                           | orchestrator                      | **Go/no-go for W2 (proposed):** arm B loses at least 5 percentage points more than arm A, not seen at the reference receiver. Else post numbers, stop |
| W2             | Implementation: interface header by the orchestrator first, then two disjoint writers in parallel (section 6)                                                                                                                                              | 2x implementer, Sonnet 5.5 medium | Gate (section 6), advisor Opus 5.5 medium, then commit                                                                                                |
| W3             | Same harness on the W2 build: arms A and B vs the W1b numbers. Then an overnight soak on DK5EN-1 with normal traffic                                                                                                                                       | orchestrator                      | Arm B loss back at arm A level, no reboot in the soak. Else W4                                                                                        |
| W4 (cond.)     | Step 3, RX task: only if W3 still shows loss, or `[INSTR-LOOP]` names non-web sections (SD, display, 2323). Planned separately; the hard part is an SPI mutex between the task and the loop's radio TX/CAD calls                                           | separate plan                     | -                                                                                                                                                     |
| W5             | Upstream PR via `/submit-pr`: park + hook, cut from `upstream/dev`, German description with the W3 before/after table                                                                                                                                      | orchestrator                      | Operator OK before submit                                                                                                                             |

## 5. W1: measurement

**Pre-registered hypotheses (record in the campaign doc before W1b):**

- H1: arm A (no web client) loses about as many frames at the DUT as the reference receiver does.
- H2: arm B (stalled browser) shows `irq_to_read_us` max in the seconds range (up to the 15 s page
  budget) and `[INSTR-LOOP]` gaps attributed to `web`.
- H3: arm B loses frames at the DUT that the reference receiver got.

**Harness `tools/bench/rx_stall_bench.py` (W1a brief):**

- Identity guard (`tools/bench/identity_guard.py`), HMAC login on 2323 per node (`MC_CONSOLE_PW`).
- Capture the DUT and the reference receiver console; optional web-load loop (curl on `/` in a
  loop); start `--loratx <n> <ms>` on the sender (TM-06(b), INSTRUMENT builds: at most 20 frames per command, interval floor 100 ms, destination fixed to `TEST`, payload `LORATX <i>`); match frames by msgid.
- Summary JSON to `tools/bench/runs/harness/`, raw logs gitignored there.
- Sends only console commands and `--loratx` (always `TEST`). Never a broadcast, never a TX power change,
  never a foreign callsign.
- Reuse the ONRX parser from `onrx_matrix.py` (import or copy the regex, do not edit that file).
- Exclusive files: `tools/bench/rx_stall_bench.py`, `tools/bench/test_rx_stall_bench.py`, fixtures
  under `tools/bench/fixtures/rx_stall/`. Verification: `uv run pytest tools/bench/test_rx_stall_bench.py`.

**Bench run W1b (orchestrator, no writer running):**

1. DUT DK5EN-1 (Heltec V3) on an INSTRUMENT build of synced `fork-dev`
   (`-DINSTRUMENT_ENABLED=1`, no space). Standing flash OK for DK5EN-1; OTA is blocked in auto
   mode, so the operator runs `tools/webflash.py`.
2. Sender DK5EN-92 (T-Beam v1.2) on an INSTRUMENT + `MC_INJECT_HOOKS` build (open question 1).
   Reference receiver DK5EN-14 (T-Deck Plus).
3. Resolve every node by callsign on its start page (DHCP addresses move). TX power at most 2 dBm.
4. Arms, each 60 frames at a 3 s interval to `TEST`:
   - A: no web client
   - B: operator runs `sudo tools/bench/web_throttle.sh on <DUT-ip> 0 2500` + curl loop on `/`
   - C: same with delay 1200 (trickle)
   - `web_throttle.sh off` after each throttled arm.
5. Per arm: frames at DUT vs reference, `irq_to_read_us` p50/max, `[INSTR-LOOP]` gaps by section.
6. Write results into the campaign doc; commit the summaries it cites, delete the rest
   (`git status` clean).

## 6. W2: implementation

**Interface (orchestrator writes it before dispatch, hotspot carve-out):** `src/lora_rx_park.h`

```cpp
bool     rxParkPush(const uint8_t *buf, size_t len, int16_t rssi, int8_t snr, int state);
bool     rxParkPop(uint8_t *buf, size_t *len, int16_t *rssi, int8_t *snr, int *state);
uint32_t rxParkOverflow();
void     loraRxParkService();   // implemented in esp32_main.cpp
```

Fixed slots (`RX_PARK_SLOTS`, default per open question 4), payload at most `UDP_TX_BUF_SIZE`,
no Arduino dependencies in the ring.

**File ownership:**

| Owner                          | Exclusive files                                                                                                             |
| ------------------------------ | --------------------------------------------------------------------------------------------------------------------------- |
| orchestrator (before and gate) | `src/lora_rx_park.h`, `platformio.ini` (new `[env:native_lora_rx_park]`), campaign doc, `docs/BACKLOG.md`, `docs/RESUME.md` |
| I1                             | `src/lora_rx_park.cpp`, `test/test_lora_rx_park/test_main.cpp`                                                              |
| I2                             | `src/esp32/esp32_main.cpp`, `src/web_functions/web_txbuf.cpp`, `test/test_web_txbuf/test_main.cpp`                          |

**I1 brief:** ring preserves order; when full, drop the newest and count overflow. Host tests:
order, overflow, length clamp, empty pop. Verification: `pio test -e native_lora_rx_park` only.

**I2 brief:**

- Split `checkRX()` into a read half (length, `readData`, RSSI/SNR, RX restart) and a process half.
  Behavior of the normal flag path must not change.
- `loraRxParkService()` acts only when `receiveFlag` is set, `is_receiving` is false and no TX is in
  progress: read the packet off the chip into the park, clear the flag, restart RX. Never call
  `OnRxDone()` from the hook.
- The loop drains the park (oldest first) before the normal flag path.
- `txFeed()` calls the hook under `#ifdef ESP32` only.
- Regression test in `native_web_txbuf`: the feed path calls the service hook. Fails before the
  change, passes after; paste both runs.
- Verification: compile `heltec_wifi_lora_32_V3` only, every `pio` call through the scratchpad
  `piolock.sh` wrapper. Never bare `pio test`.

**Shared resources:** only I2 builds a board target; I1 runs only its own native env; no hardware
in W2. A build failure outside an agent's own files is a shared-resource race until proven
otherwise.

**Standard brief rules (restate every dispatch):** exclusive file set; no create/stub/delete at
another owner's path; no git commands; no whole-repo formatters; no flashing, no RF, no 2323
access; out-of-scope bugs escalated; report at most 15 lines.

**Gate W2:**

1. `ls` the expected files, apply `platformio.ini` env.
2. `tools/regression.sh --stage 1,2`.
3. Build every board env sequentially in the background (esp32_main.cpp is shared by all ESP32
   boards); `pio project config` lists them (35 firmware envs, `esp32-external-radio` red by design).
4. RAM delta on `ttgo_tbeam` (classic ESP32), clean build.
5. Artifact check: `nm firmware.elf | grep loraRxParkService` present on V3, T-Deck Plus, T-Beam;
   absent on `wiscore_rak4631`.
6. Advisor: fresh `general-purpose`, Opus 5.5 medium, `fable-review` advisor mode on the diff and
   these criteria. Focus: reentrancy (SPI during a TFT/SD transaction, RX-timeout handler, CAD),
   normal RX path unchanged, nRF52 untouched.
7. Commit per wave, explicit paths, `git status` re-checked before the commit.

The real regression test is the bench: arm B on the W1b build vs the W3 build.

## 7. Cost and wall-clock (estimates)

| Wave        | Agents                    | Agent cost | Wall-clock                                                          |
| ----------- | ------------------------- | ---------- | ------------------------------------------------------------------- |
| W0          | -                         | -          | ~30 min (sync + gate)                                               |
| W1a + scout | 1 Sonnet + 1 Haiku        | ~$4        | ~25 min                                                             |
| W1b         | -                         | -          | ~1.5 h (flashes, 3 arms x ~4 min, setup); needs operator sudo + OTA |
| W2          | 2 Sonnet + 1 Opus advisor | ~$10-14    | ~25 min writers + 60-90 min build matrix + 15 min advisor           |
| W3          | -                         | -          | ~45 min bench + overnight soak                                      |
| W5          | -                         | -          | ~30 min                                                             |

Agent total about $15-20 plus orchestrator context. An advisor rework round adds a fresh
Sonnet-high implementer, about $5.

## 8. Open questions (operator)

1. **Flash DK5EN-92 (sender)** with an INSTRUMENT + `MC_INJECT_HOOKS` build? Only DK5EN-1 has a
   standing flash OK. OTA via `tools/webflash.py`, run by the operator.
2. **Throttle sudo:** operator runs `! sudo tools/bench/web_throttle.sh on <ip> 0 2500` per arm,
   or another way to slow the browser down?
3. **Go/no-go line:** "at least 5 percentage points extra loss in arm B, not seen at the reference
   receiver", or a different threshold?
4. **Park size:** each slot ~260 B; 2 slots ~0.5 kB, 4 slots ~1 kB DRAM (classic ESP32 is tight).
   Suggested: 4, the overflow counter shows whether it suffices.
5. **Field soak on DK5EN-98** before the upstream PR? Needs explicit operator OK at that time.
6. **#1229 reply:** post now ("measuring") or after W1b with numbers? Suggested: after W1b.

## 9. Hard rules that apply throughout

- Bench traffic only to group `TEST` or a DM to an own node; never broadcast; never a foreign
  callsign; TX at most 2 dBm; `identity_guard.py` before any test; bench over 2323, not USB.
- One `pio` process at a time; never bare `pio test`.
- DK5EN-98 is production: no work there without explicit OK per occasion.
- Harness logs to `tools/bench/runs/harness/`, never the repo root.
- Every `gh` call carries `-R DK5EN/MeshCom-Firmware` (or `-R icssw-org/MeshCom-Firmware` for the
  issue/PR).
- Upstream PR descriptions in German; the upstream maintainers merge, never the operator.

## 10. Progress log

- **2026-10-23 W0:** plan copied into the repo, defect ID WEB-RX-01 (catalogue section 2h), BACKLOG row, RESUME
  entry. Sync already done (`414a328c`). Gate: the release gate of `v4.40a.10.22` on the same tree (77 envs, 2741
  Unity cases, pytest 1190).
- **2026-10-23 W1a:** `tools/bench/rx_stall_bench.py` (live capture over 2323 via `mc_console.py`, identity
  guard on all three nodes, sender burst as 3 x `--loratx 20 3000`, frames matched by msgid, ONRX parser imported
  from `onrx_matrix.py`) and `tools/bench/test_rx_stall_bench.py` (14 tests, offline analyser on fixtures under
  `tools/bench/fixtures/rx_stall/`). Tools-only wave, advisor skipped. Open before W1b: check the real DUT `[LOG]`
  line of a received `LORATX` text against the parser regex; the live path is untested.
- **2026-10-23 W1b (bench):** DUT DK5EN-1 (INSTRUMENT build of fork-dev `552077aa`), sender DK5EN-92
  (`--loratx 20 8000` x 3, INSTRUMENT build), reference DK5EN-14 (`--setlog on` for the run); 45 frames per arm at
  8 s (3 s overran the sender's TX ring: the ring lags under DK5EN-98's relays, so `sent` counts `[LOG] TX` lines).
  Harness fixes found on the bench: sent = transmitted ids, `[INSTR-LOOP]` regex accepts the space form of
  printfdeb (17 tests).

  | Arm                    | Sent | DUT | Ref | Lost at DUT, seen at ref | irq_to_read p50 / max | Loop gaps (max / total) |
  | ---------------------- | ---- | --- | --- | ------------------------ | --------------------- | ----------------------- |
  | A no client            | 45   | 44  | 44  | 0                        | 3.8 ms / 7.6 ms       | none                    |
  | B stall (delay 2500)   | 45   | 44  | 45  | 1 (2.2 pp)               | 5.0 ms / 2.07 s       | 54, 2.5 s / 112 s       |
  | C trickle (delay 1200) | 45   | 28  | 44  | 16 (35.6 pp)             | 10.0 s / 14.8 s       | 24, 15.0 s / 328 s      |

  H1 holds (A: DUT = ref). H2 holds: B bounded by the 2 s stall abort of #1224, C by the 15 s page budget (a
  trickling client never trips the stall abort). H3: B 2.2 pp (below the pre-registered 5 pp line, which named
  arm B), **C 35.6 pp**. The loop gaps are attributed to `udp` with `section_ms 0`: the web call is not an
  `INSTR_SECTION`, the blocker sits outside every measured section. Summaries:
  `tools/bench/runs/harness/rx_stall_{A_20261023-170154,B_20261023-171724,C_20261023-172652}.json`.
