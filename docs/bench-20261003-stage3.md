# Bench session 2026-10-03: first stage-3 run (REG-01)

Three nodes on USB: Heltec V3 `DK5EN-1`, T-Beam v1.2 `DK5EN-92`, T-Deck Plus `DK5EN-14`.
Driver `tools/bench/bench_suite.py` via `tools/regression.sh --stage 3`, base `fork-dev`
`0d905dc8`. Five runs; runs 1 to 4 fixed the driver and two tools, run 5 is the result.

## Result (run 5, 16 steps, 24 min)

| Step                       | Heltec DK5EN-1               | T-Beam DK5EN-92      | T-Deck DK5EN-14                     |
| -------------------------- | ---------------------------- | -------------------- | ----------------------------------- |
| identity guard             | ok                           | ok                   | ok                                  |
| board harness              | 8/8 PASS                     | 7/8 (`dirty` flaky)  | 19/22 (`tabs`, `input`, `msg_roll`) |
| OTA regression (TM-40)     | PASS 56 s                    | PASS 57 s            | PASS 56 s                           |
| web GUI badge test (jsdom) | PASS                         | SKIP (no group tabs) | SKIP (no group tabs)                |
| mesh exchange (TM-26)      | PASS, 3x2 pairs heard, 142 s |                      |                                     |

Instrument images (`-DINSTRUMENT_ENABLED=1`, ELF marker verified) were built and flashed by
the driver in run 2 (app only at 0xC0000); the OTA step re-flashed the same image three
times. All three bench nodes now run instrument builds of `0d905dc8`.

## What the five runs changed

- `bench_suite.py`: `--node` for the harnesses that run their own guard; the identity probe is
  the gate and runs once (a second guard 7 s later hit a rebooting node); live IPs from
  `--info` replace `fleet.json` hosts and are written back (DHCP moved every node during the
  session); `wait web` before OTA and before the badge test; `--instrument` build with ELF
  marker check; T-Deck flashed app-only via esptool, not the variant `upload_command`; exit 3
  from a tool = SKIP.
- `identity_guard.py`: DTR only on native-USB ports. On the Heltec's CP2102, DTR is GPIO0,
  so a held DTR during the read was a PRG long press: DK5EN-1 was found in DS-03 deep sleep
  after runs 3 and 4. After the open the guard now waits for `[BOOT];ready` before `--info`,
  because a bridge resets the ESP32 on open and an early `--info` reads "no IP, clock INIT".
- `ota_regression.py`: same boot-ready wait after `CLIENT STARTED` before its own guard.
- `webgui_badge_test.js`: jsdom gets `TextEncoder`/`TextDecoder`; nodes without two group
  tabs exit 3 (SKIP) instead of crashing.
- Node prerequisite found again: the T-Deck needs `--debug on` and `--debug csv`, otherwise
  every `[INSTR-*]` line is suppressed (printfdeb) and heap/audio/cdc scenarios report null.

## Open after this session

- T-Deck harness drift against 4.40a (not firmware defects as far as the logs show):
  `tabs` expects a repaint when re-selecting the active tab 0; `input` sees no `[REFR]` after
  trackball steps (`refr_ms` null); `msg_roll` reports `stalled_after`. The `screen` scenario
  passed in run 5 but its CRC readback is constant (panel has no MISO, known since
  2026-08-30). One T-Deck session with the harness open next to the firmware.
- OLED `pos` (run 2) and `dirty` (run 5) are timing-flaky on the T-Beam; both passed in other
  runs. Rerun alone before calling either a defect.
- Group tabs on DK5EN-92 and DK5EN-14 (`--setgrp`) would un-skip the badge test there.
- RAK DK5EN-90 was not on the desk; its harness, EXTUDP and the RAK side of the mesh exchange
  are still unproven under the driver.
