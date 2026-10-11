# Campaign: upstream fold-in, LED co-existence (2026-10-11)

Resume document. A fresh session can continue from here without the transcript. No upstream PR
in this campaign (preparation mode). Corpus and decisions: `docs/upstream-corpus-20261011.md`.

## Status

| Wave | Content                                                                                          | State                  |
| ---- | ------------------------------------------------------------------------------------------------ | ---------------------- |
| W0   | `web-guard-flag` merged (ff), worktrees removed, `fix-tempoff-input` deleted local + origin      | done                   |
| W1   | `.zz` → `.autoupdate` rename, buffers 48 B, Safeboot page + bins rebuilt (`2522dccb`)            | done, gate green       |
| W2   | 11 upstream cherry-picks (`-x`) e1e2acea..90ac580d, golden regen, toggle-lint pin 82 → 84        | done, advisor APPROVED |
| W3   | LED feature + upstream LED bug fixes + V4 remote LED + hemisphere dead code (14783b1d..13d28041) | done, advisor 2 rounds |
| W4   | Bench: T-Beam DK5EN-92 (charge LED), Heltec V3 DK5EN-1 (RM led on/flash/off)                     | done, all checks pass  |

W2 skipped on purpose: f5f8d38f, 9a246a16, 5211600d (#1200/#1201, already in the fork), b82e36c0 +
0eb33bfa (YAML add and revert, net zero). Conflict resolutions keep the RM hold: every
`digitalWrite(BOARD_LED, ...)` in the flasher (on and off edge) is skipped while `bRemoteLed`
holds a shared pin (`REMOTE_LED_PIN == BOARD_LED`); `tg_post_board_led_off()` returns early while
RM holds the LED.

## Operator decisions (2026-10-11)

- Charge LED default: **follows charging** (AXP auto mode). `--charge led on|off` is an explicit
  override only.
- RM covers **on, flash, off and the charge LED**.
- Heltec V4 gets the remote LED (GPIO35 as on V3); built, untested until someone reports.
- Upstream LED bugs are fixed in separate fork commits with regression tests (PR-able later).
- Hemisphere: remove the dead flip code in `tdeck_add_to_pos_view` (`lv_obj_functions.cpp`
  ~4075-4080); the real swap in `tdeck_add_pos_point` is already fixed.
- Bench: no shared-pin board available (E22 DevKitC / TTGO LoRa32); T-Beam can be checked.

## W3 specification

### Upstream bug fixes (separate commits)

| ID      | Defect (upstream)                                                                                      | Fix                                                                                                                     |
| ------- | ------------------------------------------------------------------------------------------------------ | ----------------------------------------------------------------------------------------------------------------------- |
| CHG-01  | `bCHARGE_LED` defaults to false and `setChargeLedOn(false)` runs at every boot: AXP192 T-Beams go dark | Charge LED follows charging unless explicitly configured; a new "configured" bit in `node_sset3` (free bit, documented) |
| CHG-02  | sset3 bit 0x0001 was `bMHONLY`; stale nodes come up with the bit set                                   | Bit 0x0001 only counts when the "configured" bit is set                                                                 |
| CHG-03  | `XPOWERS_CHOP_AXP2101` typo (`esp32_main.cpp` ~1562): AXP2101 boards (Supreme) skip the boot init      | `XPOWERS_CHIP_AXP2101`                                                                                                  |
| CHG-04  | Charge LED missing from the BLE settings export (`nsetdoc`, `command_functions.cpp` ~7225)             | add key `CHGLED`                                                                                                        |
| LED-POL | RM writes `REMOTE_LED_PIN` hard-coded HIGH/LOW; an active-low shared pin would invert `led on`         | `REMOTE_LED_ON_LEVEL` honours `BOARD_LED_ACTIVE_LOW`; compile-time check in `loop_functions_extern.h`                   |

Charge-LED decision logic goes into a header-only helper with a native test (fails before, passes
after), e.g. `src/charge_led_mode.h` + `test/test_charge_led_mode` + `[env:native_charge_led_mode]`.

### RM LED wire spec

| Command                | Effect                                                                                                | Reply              |
| ---------------------- | ----------------------------------------------------------------------------------------------------- | ------------------ |
| `led on`               | RM holds `REMOTE_LED_PIN` steady on (unchanged)                                                       | `ok led=on`        |
| `led flash`            | RM holds the pin and drives the 30 ms / 2 s flash pattern itself, independent of `--board led`        | `ok led=flash`     |
| `led off`              | releases the hold; the pin returns to local control (board flasher if enabled, else dark) (unchanged) | `ok led=off`       |
| `chgled auto\|on\|off` | AXP boards only: auto = PMU charge control, on = steady, off = dark. Runtime override, not persisted  | `ok chgled=<mode>` |

- Non-AXP boards answer `err unsupported` to `chgled`; boards without `REMOTE_LED_PIN` keep
  `err unsupported` for `led`.
- Nothing persisted; deep sleep and the WP_DISP path force the RM LED off (as today); boot
  restores the console charge-LED setting.
- Status keeps 6 letters (`GTDMWL`); `L` = RM holds the LED (on or flash).
- Stable tokens: `led`, `on`, `off`, `ok led=on|off`, status letter `L`, JSON `status_led`.

### File ownership

| Slice                          | Files                                                                                                                                                                                                                                                                                                                                                                                                       |
| ------------------------------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| A firmware (one implementer)   | `src/rm_commands.h`, `src/remote_cmd.cpp`, `src/rm_runtime.cpp`, `src/loop_functions.cpp`, `src/loop_functions_extern.h`, `src/esp32/esp32_main.cpp`, `src/esp32/esp32_sleep.cpp`, `src/command_functions.cpp` (charge rows, BLE export, `remoteLedForceOff` site), new `src/charge_led_mode.h`, `variants/heltec_wifi_lora_32_V4/configuration.h`, `test/test_remote_cmd`, new `test/test_charge_led_mode` |
| B sender/GUI (one implementer) | `src/web_functions/web_rm_page.cpp`, `tools/remote_cmd.py`, `tools/webgui_rm_test.js`, `tools/tests/remote_cmd_vectors.json`, `tools/tests/test_remote_cmd.py`                                                                                                                                                                                                                                              |
| Orchestrator (hotspots)        | `platformio.ini` (new native env) + `test/golden/native/variant-ini-effective.json` regen, `test/golden/toggle_table_lint.py` pins, `src/t-deck/lv_obj_functions.cpp` dead code, docs (`docs/rm-gui/*`, BACKLOG, test-suite-map, this file)                                                                                                                                                                 |

A and B run in parallel (disjoint files, spec fixed above). Gate: `tools/regression.sh --stage 1,2`,
builds ttgo_tbeam, ttgo_tbeam_supreme, heltec V3, V4, E22-DevKitC, E22_1262_S3, wiscore_rak4631,
t_echo; advisor (Opus) before commit. Every pio call through the lock wrapper (one pio at a time).

## W3 result (2026-10-11)

Commits: `14783b1d` CHG-03, `e31c31e1` CHG-04, `a4da854c` CHG-01/02, `ca985372` RM led flash +
chgled + LED-POL + V4, `13d28041` T-Deck dead code. Gate `--stage 1,2` PASS (71 envs, 2368/2368);
board builds green: ttgo_tbeam, ttgo_tbeam_supreme, heltec V3, V4, E22-DevKitC, E22_XML-DevKitC,
E22_1262_S3-DevKitC-1-N16R8, wiscore_rak4631, t_echo.

Deviations from the spec above:

- CHG-01/02 marker is **`node_sset2` bit 0x8000** (`SSET2_CHGLED_CFG`, `src/charge_led_mode.h`),
  not a `node_sset3` bit: all 16 low sset3 bits are taken, and settings decode clamps sset3 to
  0..65535 (also upstream), so a bit above 15 would become 65535 = every sset3 flag on after a
  downgrade. The console rows keep the upstream masks; the post hooks set the marker and both rows
  are `TG_POST_FIRST` (hook before `save_settings()`). `--gateway pos` / `--netconsole off` masks
  widened (0x7EFF/0x6FFF cleared bit 15). Old firmware typing those two commands clears the marker:
  harmless, the node falls back to auto.
- Console `--charge led on` = follows charging (auto); steady on exists only as RM `chgled on`.
- LED-POL folded into the RM feature commit (same functions); defensive only, both active-low
  variants share BOARD_LED with OneWire and get no `REMOTE_LED_PIN`.
- `ttgo_tbeam` has no `REMOTE_LED_PIN` (GPIO4 = OneWire), so on DK5EN-92 `led` answers
  `err unsupported`; `chgled` is AXP-only and answers `err unsupported` on Heltec V3.
- GUI: "Light flash" switch + "Charge LED" select (harness forbids extra tiles); a pre-W3 node's
  `err blocked` to `chgled` marks it not supported.
- Not covered natively: rm_runtime reply text and pin levels (Arduino-bound); pinned via Python
  vectors, jsdom GUI test and W4 bench.

## W4 bench

- DK5EN-92 (T-Beam v1.2, AXP192): charge LED follows charging after the update; `--charge led off`
  darkens it; RM `chgled on/off/auto` from DK5EN-1.
- DK5EN-1 (Heltec V3, GPIO35): RM `led on/flash/off`.
- Bench rules: TX ≤ 2 dBm, `identity_guard.py` first, DM to own node or group TEST only.

## W4 result (2026-10-11)

Both nodes USB-flashed at `891e26a5` (Heltec V3 `pio upload`, T-Beam esptool 460800); identity
guard ok before and after (DK5EN-1 192.168.68.63, DK5EN-92 192.168.68.69, 2 dBm, web on, GPS
clock). RM over the air as DMs between own nodes, through the senders' web API (saved slots; the
operator saved DK5EN-92 on DK5EN-1). LED states observed by the operator. T-Beam has no 18650 cell,
so auto and off both look dark; `chgled on` is the discriminating check.

| Check                                                 | Reply / result                    | Observed            |
| ----------------------------------------------------- | --------------------------------- | ------------------- |
| T-Beam boot after update, never configured            | info: `auto (follows charging)`   | dark (no cell)      |
| DK5EN-92 -> DK5EN-1 `led on`                          | `ok led=on`                       | white LED steady    |
| `led flash`                                           | `ok led=flash`                    | short flash per 2 s |
| `led off`                                             | `ok led=off`                      | dark                |
| DK5EN-1 -> DK5EN-92 `chgled on`                       | `ok chgled=on`                    | charge LED lit      |
| `chgled off`                                          | `ok chgled=off`                   | dark                |
| `chgled auto`                                         | `ok chgled=auto`                  | -                   |
| `chgled on` to Heltec, `led on` to T-Beam             | `err unsupported` both            | -                   |
| `chgled on`, then reboot                              | override gone, info `auto`        | dark                |
| `--charge led off`, reboot, `--charge led on`, reboot | info `off`, `off`, `auto`, `auto` | -                   |

Note: status letters are `GtdMw` on the T-Beam (no `REMOTE_LED_PIN`, so no `L`); unchanged
behaviour, the spec's "6 letters" holds only for boards with the remote LED. Heltec V4 remains
built but untested on hardware.
