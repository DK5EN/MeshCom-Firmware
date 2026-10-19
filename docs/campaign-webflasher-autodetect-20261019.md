# Campaign: web flasher autodetect (WF-01..WF-04)

Source: field bug report 2026-10-18 (T-Beam 1W, "keine MeshCom-Antwort auf --info", 17 candidate
boards). Scope agreed 2026-10-19: F2, F3, F1, F5 of that report; F4 (identity read from flash,
`MCBOARD` marker, `--id`) is out. Fork only, no upstream PR. Ships with the next
`/release-firmware`, no separate gh-pages push.

## Defects

| ID    | Report | Defect                                                                                    | Wave |
| ----- | ------ | ----------------------------------------------------------------------------------------- | ---- |
| WF-01 | F2     | `parseInfo()` ignores `...AU env=<env>`; shared hwids leave several candidates            | W1a  |
| WF-02 | F3     | `listen()` leaves a native S3 at DTR1/RTS0 (download flag armed) while the port is held   | W1   |
| WF-03 | F1     | Silent native board undiagnosable: received text hidden, no Safeboot/ROM hint, no reset   | W2   |
| WF-04 | F5     | `t_deck_pro` ships `default_16MB`; the manifest writes Safeboot into app0, app never runs | W1b  |

## Waves

| Wave | Content                                                          | Owner                  | State |
| ---- | ---------------------------------------------------------------- | ---------------------- | ----- |
| W0   | Recon (env names, partition tables, vendored reset, test runner) | orchestrator           | done  |
| W1a  | WF-01 + F7 comments + detect fixtures                            | implementer            | done  |
| W1b  | WF-04 partition table + manifest/partition cross-check           | implementer            | done  |
| W1   | WF-02 (orchestrator), host gate, `t_deck_pro` build, advisor     | orchestrator + advisor | done  |
| W2   | WF-03 raw output, Safeboot/ROM recognition, reset-into-app       | implementer            | open  |
| W3   | Bench DK5EN-14 (T-Deck Plus, native S3), scenarios A-E           | orchestrator           | open  |
| W4   | BACKLOG, defect catalogue, RESUME                                | orchestrator           | open  |

## File ownership

| File                                                          | W1a  | W1b             | W1 gate         | W2   |
| ------------------------------------------------------------- | ---- | --------------- | --------------- | ---- |
| `pages/flash/detect.js`                                       | owns |                 |                 | owns |
| `tools/tests/test_flasher_detect.mjs`                         | owns |                 |                 | owns |
| `pages/flash/flasher.js`                                      |      |                 | WF-02 + F7 line | owns |
| `pages/flash/index.html`                                      |      |                 |                 | owns |
| `tools/pages_flasher.py`, `tools/tests/test_pages_flasher.py` |      | owns            |                 |      |
| `variants/t_deck_pro/platformio.ini`                          |      | owns            |                 |      |
| `.pio/build/t_deck_pro`                                       |      | only `pio` user |                 |      |
| DK5EN-14, USB port, Chrome                                    |      |                 |                 |      |

## W0 facts

- detect.json env names: DK5EN-14 = `t_deck_plus` (hwid 46), 1W = `LilyGo_T-Beam-1W` (hwid 51),
  T-Deck Pro = `t_deck_pro` (hwid 50). Only alias today: hwid 12 -> three T-Beam images.
- `t_deck_pro`: board json `T-Deck-Pro.json` 16 MB, `default_16MB.csv`. The 16 MB safeboot table
  keeps NVS at 0x9000 (36K, 20K), same as `default_16MB`, so settings survive the switch.
- ESP Web Tools runs its own USB-JTAG reset sequence on connect (vendored install-dialog chunk), so
  the line state left by detection does not matter for flashing.
- Host tests: `node --test tools/tests/*.mjs`, pytest via `uv run --with pytest pytest -q tools/tests`.

## Bench plan (W3, DK5EN-14)

| Scenario                                       | Expected after the fix                         |
| ---------------------------------------------- | ---------------------------------------------- |
| A app running, detect                          | exactly `t_deck_plus`, basis names `env=`      |
| B reset while the page holds the port          | before: ROM; after: app                        |
| C Safeboot                                     | "Safeboot" hint, raw text shown, reset works   |
| D ROM download mode                            | "ROM" hint, hard reset brings the app back     |
| E detect, then flash the release via held port | flash ok, node back with callsign and settings |

## W1 result (2026-10-19)

- WF-01: `parseInfo()` returns `env`; `candidates()` takes it as the only candidate when the release
  ships it, its chip family matches ROM/USB, and its own radio did not fail (advisor rework: an env
  whose radio failed is the wrong image, it falls through to the radio filter). `listen()` waits up
  to 1 s after the NODE line for the `...AU env=` line. Tests: `test_flasher_detect.mjs` 27/27, the
  new cases failed before the change.
- WF-02: `listen()` sets DTR0/RTS0 on esp-native after listening. The ESP Web Tools dialog runs its
  own `usbJTAGSerialReset` (pid 0x1001), so flashing does not depend on the line state. Bench proof
  in W3 (scenario B).
- WF-04: `t_deck_pro` (and `esp32-external-radio`, which extends it) build with
  `partitions-16MB-safeboot.csv`; `pages_flasher.py` `check_partition_layout()` fails the stage
  when a manifest writes Safeboot or the app where the table has no matching app partition (all 27
  ESP32 release envs pass). Build: Flash 18.1 % of 12 MB, partitions.bin safeboot/factory 0x10000,
  app/ota_0 0xC0000. NVS stays at 0x9000; SPIFFS moves (0xC90000 -> 0xCC0000) and is reformatted
  on first boot: release note must tell T-Deck Pro owners to do one full web flash.
- Golden baseline `variant-ini-effective.json` regenerated for the two envs.
- Gate: stage 1 Unity 2723/2723, golden selftest green, stage 2 PASS.

## Proposal, not in scope: Safeboot identity (W5 candidate)

Safeboot is one image per chip family and has no command parser, so it cannot name the board.
Option: the app writes hwid/env to NVS at boot, Safeboot reads it and answers `--info` with the
NODE and `...AU env=` lines plus `[SAFEBOOT];ver;N`. Safeboot v3, reaches boards only by a USB web
flash. Operator decision.

## W2 result (2026-10-19)

- WF-03: `parseBoot()` reports `safeboot` and `bootMode`; pure `diagnose()` picks ok / safeboot /
  rom-download / silent-native / silent-after-reset / no-answer. The result shows the last 2 KB
  received (`<details>`, textContent) and logs the full text to the console. On esp-native, when
  silent, in ROM download mode or in Safeboot, a "Neustart in die App" button resets the board
  (DTR0, RTS1 200 ms, RTS0), re-acquires it through `getPorts()` (only the old object or a port not
  granted before the reset, so a second S3 is never picked) and runs the same result handling once.
- Advisor rework applied: same-VID/PID guard, rejection handler on the button, `select()` after
  dropping the held port, Safeboot offered the reset too.
- Tests: `test_flasher_detect.mjs` 32/32. Browser and hardware behaviour: W3.

## W3 result: bench DK5EN-14 (2026-10-19)

T-Deck Plus, native USB 303a:1001, local flasher at `127.0.0.1:8700` (`before` = published gh-pages
10.20, `after` = fork-dev). Node checked by `identity_guard.py` before and after (2 dBm).

| Scenario                       | Before (published)               | After (fork-dev)                                                                 |
| ------------------------------ | -------------------------------- | -------------------------------------------------------------------------------- |
| A app running                  | T-Deck Plus via hwid 46          | T-Deck Plus, node line shows `Build-Env t_deck_plus`, 5984 B shown               |
| B reset while the port is held | `--reboot` and 3x RST: app boots | not repeated: nothing to fix                                                     |
| C Safeboot (`--ota-update`)    | (not run; report screen)         | 0 B, button offered; after reset 449 B, "Board steht in Safeboot (Version 2)"    |
| D ROM via BOOT strap           | -                                | 0 B, button offered; USB reset lands in the ROM again; RST key brings the app    |
| E detect, flash via held port  | -                                | dialog takes the held port, no chooser; 33 s, app boots by itself, settings kept |

Findings:

- **F3 / WF-02 not reproduced.** With the published flasher holding the port at DTR1/RTS0, a
  software reboot and three RST presses all booted the app. The armed download flag does not
  survive an EN/RST reset on this board. WF-02 stays as hygiene, it fixes no observed failure.
- **ROM entered through the BOOT strap is sticky against a USB-JTAG reset.** DTR0 + RTS pulse from
  the ROM booted the ROM again (no app, no Safeboot after 40 s); only the RST key left it. From a
  normal boot (scenario C) the same reset works. The page now says so and no longer offers the
  reset button for a ROM banner; "silent after reset" names the RST key first.
- **Re-acquire timing.** The first run of D ended in "Verbindung abgerissen": the page reopened the
  old USB device just before it re-enumerated. `restartIntoApp()` now waits up to 1.5 s for the
  `disconnect` event and re-acquires once more on a lost port; the repeat of D re-acquired cleanly.
- The ROM banner and Safeboot output are printed before the page opens the port, so a board that
  sits in either state is "silent" on the first listen; only the reset path can show which.

Not covered: Windows (`usbser.sys` signal handling), a real T-Beam 1W, a bridge board (CP210x).
