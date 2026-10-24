# Campaign TRK-02: web flasher leaves TRACK on (2026-10-24)

Resume point for the TRK-02 measurement waves. Measure and reproduce first; no fix in this campaign.

## Report

Field report: after an update through our web flasher a Heltec V3 came up with `TRACK:true`. No
further details (version, erase, browser, board revision unknown).

## Hypothesis (code reading, not yet measured)

- Heltec V3 user button is GPIO0 (`variants/heltec_wifi_lora_32_V3/configuration.h:50-51`,
  `SET_BUTTON_PIN`); with default settings (`node_sset == 0x0004`, e.g. after an erase) the button
  is enabled and saved at boot (`src/esp32/esp32_main.cpp:1673-1679`).
- GPIO0 is also the BOOT strap the CP2102 auto-reset circuit pulls LOW with DTR asserted and RTS
  deasserted.
- `attachMultiClick(tripleClick)` (`src/onebutton_functions.cpp:321`) fires for 3+ clicks (50 ms
  debounce, 400 ms gap); `tripleClick()` toggles `bDisplayTrack` and runs `--track on`, which sets
  `node_sset |= 0x0020` and calls `save_settings()` (`src/command_functions.cpp:2143-2166`).
- Open: which Web Serial sequence of the flasher (esptool-js reset strategies, open/close, console)
  produces GPIO0 pulses while the app runs.

Side finding TRK-03 (candidate): `src/esp32/esp32_main.cpp:1668` ORs `0x0035` into default
settings on every T-Beam variant; the comment says "bGPSON = true", but GPS is 0x0040 and 0x0020 is
TRACK. Since `da5c5bd4` (V4.21).

## Operator decisions (2026-10-24)

- A position sent by a double click (`--sendpos`) during the tests is acceptable.
- W3 runs both the real GitHub Pages flasher (release image) and a locally served flasher with a
  current local build; INSTRUMENT builds may be flashed over serial or OTA.
- W4: full permission to erase and restore DK5EN-92.
- DK5EN-1: full permission to enable the 2323 console.

## Waves

| Wave | Content                                                                   | Status                                     |
| ---- | ------------------------------------------------------------------------- | ------------------------------------------ |
| W0   | Recon: flasher signal timeline (S1), observer and bench inventory (S2)    | done                                       |
| W1   | `tools/bench/dtr_replay.py` + fake-port self-test                         | done                                       |
| W2   | Synthetic replay matrix: strategy x board (DK5EN-1, DK5EN-92) x button    | done                                       |
| W3   | Real Chrome flasher on DK5EN-1 with Web Serial call logger + 2323 capture | done (A1 only, operator stopped measuring) |
| W4   | T-Beam 0x0035: export config, erase, flash, `--info`, restore             | done                                       |
| W5   | TRK-03 fix + test, defect catalogue, BACKLOG, RESUME; advisor pass        | done                                       |

## Ownership

| Path                                                             | Owner              |
| ---------------------------------------------------------------- | ------------------ |
| `tools/bench/dtr_replay.py`, `tools/bench/test_dtr_replay.py`    | W1 implementer     |
| `tools/bench/runs/harness/trk02-*`                               | orchestrator       |
| this file, `docs/BACKLOG.md`, `docs/RESUME.md`, defect catalogue | orchestrator       |
| `src/**`, `pages/**`, AU-21 working-tree files                   | nobody (read only) |

Local builds come from `fork-dev` at `7f960e9b` (AU-21 W1-W2 committed, tree clean). The AU-21
campaign finished its DK5EN-1 bench run before this campaign started.

## Results

### W0 (code reading, 2026-10-24)

- Flasher bridge flow (CP2102): `flasher.js` listen pulses only RTS (`(0,1)` 120 ms, `(0,0)`).
  esptool-js uses `classicReset` only for this PID (`[Wr(50), Wr(550)]`, 7 attempts); IO0 goes LOW
  (`DTR=1,RTS=0`) only at EN release for ROM entry, so no running app sees it. Post-install `wa` +
  hard reset is RTS-only. Improv probes (1.5 s before, 10 s after install) only write. Nothing in
  repo code holds `DTR=1,RTS=0` while the app runs.
- Unknown from code: the DTR/RTS state Chrome leaves at `port.open()` / `close()` (five open/close
  edges in one install run). This is what W2 (pyserial) and W3 (Chrome call logger) must measure.
- H2 (human): the install dialog's failure text tells users to hold BOOT while clicking INSTALL.
  On the V3 BOOT is the PRG button = GPIO0 = the MeshCom user button; three presses while the app
  runs toggle TRACK.
- Observers: `--setcont on` (node_sset 0x4000, persisted) prints `SingleClick`/`DoubleClick`/
  `TripleClick`/`LongPress Stop` to Serial, teed to an authenticated 2323 session. `--btn` and
  `--airgap` are INSTRUMENT-only. `--netconsole on` is persisted (node_sset2 0x1000) and needs WiFi.
- Config export/import for W4 is web-only: `GET /config.json`, `POST /config` (saves and reboots).
- Local flasher: `uv run tools/pages_flasher.py stage --version V --out DIR --envs ...`, then a
  static HTTP server in DIR (relative paths only).

### W1 (2026-10-24)

`tools/bench/dtr_replay.py` + `tools/bench/test_dtr_replay.py` (20 tests). Replays each esptool-js
reset strategy, the post-install reset, open/close with driver defaults, and a DTR click train
(positive control) against the running app; TRACK is read from `--pos` (`...Track: on|off`).

### W2 positive control (DK5EN-1, Heltec V3, CP2102, `7f960e9b` image, 2026-10-24)

`dtr_replay.py --strategy click_train --runs 3 --expect-events --setup`: three DTR pulses
(120 ms asserted, 200 ms gap, RTS deasserted) gave `TripleClick` and TRACK off -> on in 3 of 3
runs (`tools/bench/runs/harness/trk02-w2-heltec1-control.summary`). The electrical path
DTR -> GPIO0 -> OneButton multi-click -> `--track on` + `save_settings()` is proven.

### W2 matrix (DK5EN-1, 10 runs per strategy, button on, 2026-10-24)

| Strategy                        | Runs | Runs with button events | TRACK changed | Chip reset  |
| ------------------------------- | ---- | ----------------------- | ------------- | ----------- |
| classic50 (esptool-js `Wr`)     | 10   | 0                       | 0             | 10          |
| classic550                      | 10   | 0                       | 0             | 10          |
| usbjtag (`Zr`)                  | 10   | 0                       | 0             | 10          |
| hardreset (`Vr`)                | 10   | 0                       | 0             | 10          |
| wa (post-install reset)         | 10   | 0                       | 0             | 10          |
| console_reset                   | 10   | 0                       | 0             | 10          |
| open_close (driver default, 3x) | 10   | 0                       | 0             | 10 (3 each) |
| rts_then_dtr (200 ms IO0 hold)  | 10   | 10 (`SingleClick`)      | 0             | 0           |

Summary `tools/bench/runs/harness/trk02-w2-heltec1-matrix.summary`. Every flasher sequence resets
the chip; none produces a click, because the app is not running while IO0 is LOW. Only a deliberate
IO0 hold while the app runs counts (one hold = one click; three = TRACK, see positive control).
pyserial's open/close on macOS resets the ESP32 on every open (`RESET_REASON` 3 per run).

### W3 run A1 (Chrome, GitHub Pages flasher, v4.40a.10.23, DK5EN-1, update without erase)

Web Serial calls captured in the page (`trk02-w3-a1-webserial.summary`): detection = open, RTS
pulse 120 ms, idle; install = close, open, one `classicReset` (DTR asserted with RTS deasserted
for 52 ms, at EN release only), flash, `wa` (RTS 200 ms), close, open (Improv probe, 35 s), close.
The dialog offered no erase prompt. 2323 capture: 0 button events; afterwards `Track: off`,
`BUTTON (0) on`. Identical to the code reading of W0 and the W2 replay.

### History and a third hypothesis

- From V4.21 (`da5c5bd4`, 2023-06) to V4.29c (`adaaa889`) the ESP32 default for a missing
  `node_sset` key was `0x0035` on every ESP32 board, i.e. TRACK on. Only nodes first booted on
  those versions carry it; an update never re-applies a default to an existing key.
- H3 (pre-existing state made visible): since TRK-01 (`d46e2904`, 2026-09-10, also in upstream
  `dev`) every boot with TRACK set prints `[INIT]...Track on - degraded MeshCom RX performance`,
  and the WebGUI warns. The web flasher's detection shows that boot log. A node that has had TRACK
  on for a long time (a triple click on PRG, the 2023 default, an app setting) shows it for the
  first time right after a flash with a TRK-01 build.

Negative control DK5EN-92 (T-Beam, CH9102, button temporarily on, GPIO38): `click_train` 3 runs,
0 events, TRACK unchanged (`trk02-w2-tbeam92-negative.summary`). DTR does not reach the T-Beam
button. Button and setcont restored off; `node_sset` back to 4166.

### W4 TRK-03 (DK5EN-92, T-Beam AXP2101, `ttgo_tbeam`, `7f960e9b` image, 2026-10-24)

Config exported (`GET /config.json`, backup `~/MeshCom-bench-backups/dk5en92-config-20261024-pre-trk02.json`),
`esptool erase_flash` + full write at 460800, first boot (`trk02-w4-fresh-boot.summary`):
`[INIT]...Track on - degraded MeshCom RX performance`, `[OBUT]...One Button GPIO(38) started`,
`--pos`: `...GPS: off`, `...Track: on`. TRK-03 confirmed: a fresh T-Beam starts with TRACK on and
GPS off, the opposite of the `esp32_main.cpp:1668` comment. Restore: `--setcall`, `--setssid`,
`--setpwd`, `--webserver on` over serial, then `POST /config` (114 fields applied); afterwards
111 of 114 fields equal the backup (only GPS-tracked `node_lat`/`node_lon`/`node_alt` differ),
`GPS: on`, `Track: off`, identity guard ok. During the minutes before the restore the node ran
with the default call `XX0XXX-00`, TRACK on and MESH on. It could not transmit: TX-01 refuses
every frame of an unconfigured node at the TX ring (`src/txring_functions.cpp:788`) and again in
`doTX()`, the only caller of `Radio.Send()` (`src/lora_functions.cpp:3055`). mcmap has no node
`XX0XXX` on record either.

## Conclusion (2026-10-24)

- **TRK-02 not reproduced.** The web flasher cannot click the V3 user button: IO0 is LOW only at EN
  release for ROM entry, while no app runs (W0 code, W2 80 replays, W3 one real Chrome install: 0
  events). The mechanism that does set TRACK is three PRG presses while the app runs (W2 control
  3/3). Remaining explanations for the report: the user pressed PRG/BOOT (the flasher's failure
  text asks for it), or TRACK predates the flash and TRK-01's boot warning made it visible. No
  change to the flasher or the button handler; reopen with a log that shows `TripleClick`.
- **TRK-03 fixed** (fork-dev, not released): `TBEAM_FIRST_BOOT_SSET 0x0055` in
  `src/configuration_global.h` replaces the `0x0035` literal in `src/esp32/esp32_main.cpp`; the
  first boot also sets `bGPSON` and `bDisplayVolt` at runtime, so GPS starts without a reboot.
  Regression test `test/test_tbeam_first_boot_sset` (red on 0x0035: 3 of 4 cases,
  `trk02-fix-before.summary`; green on 0x0055, `trk02-fix-after.summary`). Gate stage 1+2 PASS
  (Unity 2876/2876, pytest 1352), builds `ttgo_tbeam`, `ttgo_tbeam_SX1262`, `ttgo_tbeam_supreme`.
  Advisor (Opus): APPROVED; its two suggestions (runtime flags, volt-bit comment) applied.
- **Not migrated:** T-Beams first booted on V4.21 or later already carry TRACK on in NVS; the fix
  only applies to factory settings (an erase). A user who wanted TRACK cannot be told apart, so
  existing nodes keep their setting (`--track off` per node).
- `test/golden/native/variant-ini-effective.json` regenerated for the one new `test_filter` entry.
