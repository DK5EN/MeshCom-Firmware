# Campaign AU-21: TFT sprite heap, shedding and lowheap label (2026-10-24)

Resume point for the AU-21 waves. BACKLOG row: AU-21. Field log: `~/Desktop/2026-10-24-dj8meh-81-putty.log`.

## Finding

The Heltec Wireless Tracker (ESP32-S3FN8, no PSRAM) boots with about 26 KB less free heap than a
Heltec V3 with the same chip (ble 153.7 vs 179.6 KB, mdns 87.7 vs 115.0 KB). The cause is
`initTFT()` (`src/tft_display_functions.cpp:36`): `sprite.createSprite(160,80)` allocates
160 x 80 x 2 = 25600 bytes on the internal heap on every boot, also with the display off, and never
frees it. Only the tracker uses `initTFT()` (`HAS_TFT`); T-Connect Pro and T114 have their own
display paths, the T-Deck LVGL buffer is in PSRAM. In the field the AU check started at 86.4 KB
free, the P-384 chain verify plus concurrent web traffic ran the internal heap to zero, the SHA
bounce buffer failed and mbedTLS reported -9984, which the GUI showed as "No connection to GitHub".

## Operator decisions (2026-10-24)

- "Display off" for not allocating the sprite = the stored setting `bDisplayOff` only, not the
  screensaver `bDisplayIsOff`.
- During an update with the display on: draw "Update..." directly to the TFT (no sprite).
- Logging: TFT lines plus a `...HEAP free/min/blk` line in `--info`. GUI state H text unchanged.
- No separate tracker test build: ships with the next v* release (no tracker on the bench).
- Out of scope, separate after a DK5EN-1 measurement: S3 `FWNET_MIN_FREE`, S3 BLE pause.

## Waves

| Wave | Owner        | Files                                                                                                                      | Content                                                                                                | State                                                                                          |
| ---- | ------------ | -------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------ | ---------------------------------------------------------------------------------------------- |
| W0   | scout        | read only                                                                                                                  | every sprite access and tft_display_functions caller runs in the loop task                             | done: yes (OnRxDone runs in esp32loop; other tasks fwnet, con_auth, audio never touch the TFT) |
| W1a  | implementer  | `src/fw_update.h`, `test/test_fw_update/test_main.cpp`                                                                     | `FW_SHED_TFT`, `fwShedSettled` with TFT, `fwTlsFailToken` (`lowheap_tls`), `fwTftSpriteWanted` + tests | done (W1 commit)                                                                               |
| W1b  | implementer  | `src/tft_display_functions.cpp`, `src/tft_display_functions.h`                                                             | sprite acquire/release/active/bytes, `[TFT];sprite` lines, no alloc when off, `tftShowUpdating`        | done (W1 commit), host tests in test_tft_backlight                                             |
| W2a  | implementer  | `src/esp32/fw_update_net.cpp`                                                                                              | `httpFail()` lowheap on alloc fails, `FW_SHED_TFT` before CHECK/DOWNLOAD, `[AU];shed;tft`              | done (W2 commit)                                                                               |
| W2b  | implementer  | `src/esp32/esp32_main.cpp`, `src/esp32/loop_actions_esp32.cpp`, `src/loop_functions_extern.h`, `src/command_functions.cpp` | loop sprite reconcile, shed avail/settled, `(tft)` heap mark, `--info` TFT + HEAP lines                | done (W2 commit)                                                                               |
| W3   | orchestrator | bench DK5EN-1                                                                                                              | AU check still works, no `[TFT]` lines, `--info` HEAP line                                             | open                                                                                           |
| W4   | orchestrator | `docs/BACKLOG.md`, `docs/architecture/08-defect-catalogue.md`, `docs/RESUME.md`                                            | AU-21 state                                                                                            | open                                                                                           |

Gate per wave: diff read, `tools/regression.sh --stage 1,2`, board builds (W2: tracker, Heltec V3,
T-Beam, T-Deck Plus, RAK) one at a time, Opus advisor, commit on `fork-dev`. Every pio call goes
through the scratchpad `piolock.sh`.

## W1 notes

- Advisor rework: a TLS heap failure gets its own token `lowheap_tls`, not `lowheap`. Plain `lowheap` does not use a
  download attempt (`fwErrConsumesAttempt`), and on the S3 (no BLE pause, no `lowheap_ble_off` rewrite) a DOWNLOAD that
  dies in the handshake would retry every 10 min forever. `lowheap_tls` uses an attempt and maps to GUI state H
  (`au_ui_state.h`, web JS). W2a: `httpFail()` returns `lowheap_tls <code>` with the token as the first word.
- `test_tft_backlight` compiles the real `tft_display_functions.cpp` against a stub; the stub now tracks sprite state.
- `[TFT];sprite;free` is not in the W1 tracker binary yet: `tftSpriteRelease()` has no caller until W2b (gc-sections).
- W2 notes from the advisor: rate-limit re-acquire in the loop reconcile (no 25.6 KB calloc + log line every pass),
  pass `tftSpriteActive()` as the 4th `fwShedSettled` argument, log the TFT shed (the `shedHold` log shows console/kiss
  only). Redraw after re-acquire comes from the periodic `displayTFT` in `loop_functions.cpp:1515`. With the display
  stored off the boot splash no longer waits 5 s.

## W2 notes

- `httpFail()` returns `lowheap_tls <code>` when allocations failed during the job, else `tls <code>`.
- The job holds `FW_SHED_TFT` before every CHECK/DOWNLOAD (`[AU];shed;tft`, `shed;services;...;tft;1`); the loop
  (`tftSpriteReconcile`, `esp32_main.cpp`) releases the sprite, shows "Update..." when the display is on, and re-acquires
  at once when the hold ends (redraw via `sendDisplayHead(true)`), else at most every 30 s after a failed acquire.
- Advisor nice-to-have applied: no re-acquire while an AU job is BUSY (`fwTftSpriteWanted` third argument), so a retry
  cannot take 25.6 KB in the middle of a handshake and a CHECK->DOWNLOAD chain does not flicker.
- Boot mark `[HEAP];...;(tft)` after `initTFT()`; `--info` gains `...HEAP : free/min/blk` (all ESP32) and
  `...TFT sprite : <n> B [(display off)]` (tracker).
- Not applied (noted): `sendDisplayHead(true)` after re-acquire ignores `pageHold`, a held message page returns to the
  head page after an update; INSTRUMENT `[AU];shed;fail` also fires when only the TFT was shed.
- Gate: regression stage 1,2 PASS (2872/2872); builds tracker, Heltec V3, T-Beam, T-Deck Plus, RAK4631 OK; tracker elf
  carries `[TFT];sprite` alloc/free, `shed;tft`, `(tft)`, `TFT sprite`.

## Known gap

Sprite release and re-acquire are covered by host tests and build/artifact checks only, not on
tracker hardware, until the first field report after the release.
