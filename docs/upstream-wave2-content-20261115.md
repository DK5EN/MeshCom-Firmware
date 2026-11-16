# Upstream wave 2: content list (Ping plus web GUI)

Deadline Fri 2026-11-20. Base: `upstream/dev` 70ae124d, which is also the merge base of `fork-dev` (HEAD 7e444092), so
`git diff upstream/dev fork-dev -- <path>` is exactly the fork-only delta. Evidence was read-only (`git diff`, `git grep`,
`git apply --check` against a scratch index); nothing was built (the regression gate owns `pio`). Check again at port time.

## BLUF

- **Ping** is small and portable: 3 commits, 1 new header, 5 touched source files, no LoRa wire change. One part should
  stay out: the CSMA-03 1 ms loop poll inside `b3b2bf0b`.
- **Web GUI (option D)** is not a file-level cut. The split sources are the end state of the whole fork GUI, and the
  C++ pages that emit their DOM ids are tangled with fork-only features of waves 3, 4 and 6 (Pins page, RM1 commands,
  Auto Update schedule). It needs a hand port with a trimmed `mc.js` and `mcrm.js`; decision 1 below.
- Every hash cited is on `fork-dev` and not reverted (see Evidence).

## Evidence

| Claim                                                              | Check                                                                                                                                                                                         |
| ------------------------------------------------------------------ | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `b3b2bf0b`, `5583b177`, `b6d0da94`, `591a9e76` exist               | `git branch --contains` lists only `fork-dev` for each; none is a merge from upstream                                                                                                         |
| Not reverted                                                       | `git log fork-dev --grep='^Revert' -i` gives 28 commits (old upstream reverts, `#1223`); none names these hashes or touches ping/web                                                          |
| CLK-PING-01 (`dispClkHeldFrame` 2-arg, `bHeldFrameDrawn`)          | Already upstream: `746f9616` (display clock gate), present in `upstream/dev:src/display_clock_gate.h`                                                                                         |
| Ping target rules (LRF W1, `ping_target.h`, `PING_INVALID_TARGET`) | Already upstream: `43ddf202`; net diff is one comment line                                                                                                                                    |
| `web_assets.h` matches its sources                                 | `python3 -I tools/gen_web_assets.py --check` exit 0 on `fork-dev`                                                                                                                             |
| Other ping commits not in upstream                                 | `git log upstream/dev..fork-dev -i --grep=ping`: only `b3b2bf0b`, `5583b177`, `b6d0da94` carry ping code; the rest are docs/merges or already upstream (`16a33b12` came in as upstream #1247) |

## Part A: Ping

### Commits

| Commit     | IDs                                      | What goes to upstream                                                          |
| ---------- | ---------------------------------------- | ------------------------------------------------------------------------------ |
| `b3b2bf0b` | PING-HOLD-01, PING-NRF-01 (CSMA-03: out) | Ping frame held 10 s, shared ping budget for nRF52 and ESP32                   |
| `5583b177` | PING-HOLD-02, PING-STOP-01               | Message page redrawn after a ping frame; `--ping start/stop` clear `bPingSend` |
| `b6d0da94` | TRACK-PING-01 only                       | Track page replaces a ping frame at the 10 s deadline                          |

Order matters: `5583b177` uses `heldFrameUntilMs` from `b3b2bf0b`; TRACK-PING-01 uses `dispClkFrameExpired` from
`5583b177`. `b6d0da94` also holds CET-AGE-LOG-01, TD-NAV-01, AU-26, web brightness (`.mcsegval`) and docs: only the two
TRACK-PING-01 hunks below are wave 2.

### Files

| File                                | Take                                                                                                                                                                                                                             | Leave                                                                                                                                                                |
| ----------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `src/ping_budget.h` (new, 22 lines) | all (`pingBudgetOpen`, `pingCountAfterSend`)                                                                                                                                                                                     |                                                                                                                                                                      |
| `src/display_clock_gate.h`          | `#include <stdint.h>`, `PING_FRAME_HOLD_MS`, `dispClkPingHoldMs`, `dispClkFrameDeadline`, 4-arg `dispClkHeldFrame`, `dispClkFrameExpired`, `dispClkTrackMayDraw`                                                                 | fork header comment (CLK-01 doc reference, INSTRUMENT `[DISPCLK]` wording)                                                                                           |
| `src/loop_functions.cpp`            | `heldFrameUntilMs` definition; `sendDisplayClockLine()` (`dcNow`, `heldFrameExpired`, PING-HOLD-02 redraw under the `#if !BOARD_T5_EPAPER/!HAS_TFT*` guard); `DisplayPong()` deadline line; TRACK block in `mainStartTimeLoop()` | `oledTextTest()` and its `heldFrameUntilMs = 0` line (fork-only console OLED text test, `oled_shot.h`); every `clockInstr*` call (`clock_instrument.h` is fork-only) |
| `src/esp32/esp32_main.cpp`          | `#include "ping_budget.h"`, `pingBudgetOpen()` condition, `pingCountAfterSend()` line                                                                                                                                            |                                                                                                                                                                      |
| `src/nrf52/nrf52_main.cpp`          | `#include "ping_budget.h"`, whole `pingBudgetOpen` block (PONG FAILED path, budget)                                                                                                                                              | the `delay(csmaLoopDelayMs(...))` hunk (see below)                                                                                                                   |
| `src/command_functions.cpp`         | two `bPingSend = false;` lines (`--ping start`, `--ping stop`)                                                                                                                                                                   |                                                                                                                                                                      |

Stays out of the PR: `src/csma_timing.h` (`csmaLoopDelayMs`), `platformio.ini` (test_filter line), `docs/rca-csma03-*`,
`docs/rca-pong-rx-01-*`, `test/golden/native/variant-ini-effective.json`, and the fork-only tests `test_ping_budget`,
`test_display_clock`, `test_csma_timing`, `test_command_setters`. Upstream `test/` holds only `test_compress`,
`test_external_radio_*`, `test_kiss_ax25`, `test_pn_retry`, `test_safeboot_state`, `test_web_auth`, `test_web_txbuf` and
`platformio.ini` has no native env that carries ping or clock-gate tests. Optional: a new `[env:native_ping_budget]`
for `test_ping_budget` (pure header, no stubs); it is the only ping test that would run without fork stubs.

### Fork-only dependencies

None for the files above. Checked in `upstream/dev`: `bHeldFrameDrawn`, `dispClkHeldFrame` (2-arg), `PongFail`,
`bPingSend` (`loop_functions_extern.h:116`), `PING_SUPPRESSED_TRACK`, `PING_INVALID_TARGET`, `node_pingcount`,
`sendDisplay1306(bool,bool,int,int,char*)` and `pageLineAnz` all exist. The fork `sendDisplayClockLine()` carries
CLK-01 instrument hooks and `gate15` text that upstream lacks: do not copy them.

### CSMA-03 decision (recommend: out)

`csmaLoopDelayMs` makes `nrf52loop()` poll at 1 ms while a CAD runs or its result is pending. It is channel-access
timing (wave 7, "channel access stabilisation"), its re-bench is open ("threshold stays 5 ms until re-bench"), and
BACKLOG `PWR-01` lists exactly this 1 ms poll as a suspect for the RAK4631 never sleeping. It also pairs with the
fork-only CSMA-03 recheck (`csmaCadFreeNeedsRecheck`, `CSMA_CAD_MAX_AGE_US`, not upstream). Splitting it out leaves the
upstream nRF52 loop at `delay(100)`; PING-NRF-01 does not depend on it.

### Conflict risk against `upstream/dev`

`git apply --check --3way` of each original commit against a scratch index of `upstream/dev`: conflicts in
`loop_functions.cpp` (CLK-01 instrument hooks around `sendDisplayClockLine`), `esp32_main.cpp` and `nrf52_main.cpp`
(context lines such as `#include "csma_timing.h" // CSMA-05` and the `LRF 2.1` comments do not exist upstream),
`display_clock_gate.h` (only for `5583b177`/`b6d0da94`, because they need the earlier commit), `platformio.ini` and
`csma_timing.h` (dropped anyway). All are context mismatches, not semantic. Plan: hand-apply the net diff, not
`cherry-pick`. Upstream touched `loop_functions.cpp`, `display_clock_gate.h` and the mains on 11-13/11-14 (`#1273`,
`746f9616`, `fd7736bb`); re-fetch before cutting.

### REVIEW.md

| Rule             | Status                                                                                                                                                                                                                                                                                                                     |
| ---------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| R1 platforms     | Hold and TRACK changes act on u8g2 OLED boards only (`bHeldFrameDrawn` is set only in the u8g2 branch of `DisplayPong`; the TRACK hunk sits in the non-`WP_DISP` branch). TFT, e-paper and T-Deck compile the helpers but do not use them. PING-NRF-01 changes RAK4631, T114, T-Echo; only the RAK4631 is on the B-1 list. |
| R2 every env     | Gate: one env per family at least (`heltec_wifi_lora_32_V3`, `wiscore_rak4631`, `ttgo_tbeam`, `t_deck`, a T114/TFT and a T-Echo/e-paper env). The `(void)heldFrameExpired;` line exists for `-Werror=unused-variable` on heltec_t114.                                                                                      |
| R3 surfaces      | `--ping start/stop/max` console, web `callfunction`, BLE share `commandAction`; PING-STOP-01 acts on all of them.                                                                                                                                                                                                          |
| R4 LoRa bytes    | No change: `sendPing()` and the frame builder are untouched; only the scheduler and display change.                                                                                                                                                                                                                        |
| Behaviour change | nRF52 no longer pings without `--ping start`; the budget is not persisted. Needs a line in the PR body (release-note warning already in BACKLOG).                                                                                                                                                                          |

### Bench rows before the PR

| Row                        | Node                | Source                             |
| -------------------------- | ------------------- | ---------------------------------- |
| PING-HOLD-02, PING-STOP-01 | DK5EN-1 (Heltec V3) | BACKLOG, campaign B-1              |
| TRACK-PING-01 (TRACK mode) | DK5EN-1             | BACKLOG, campaign B-1              |
| PING-NRF-01                | DK5EN-90 (RAK4631)  | campaign B-1 ("wave 2 needs it")   |
| PING-HOLD-01 (10 s frame)  | DK5EN-1             | no own row; seen with PING-HOLD-02 |

Known gap, not a blocker: after the 10 s hold the track page stays frozen until the ping time ends
(campaign "Known gaps"). No T114 or T-Echo node on the bench list: PING-NRF-01 is verified on the RAK4631 only.

## Part B: Web GUI (option D)

### Files that are new on the fork (take)

| File                              | Lines | Note                                                                                                |
| --------------------------------- | ----- | --------------------------------------------------------------------------------------------------- |
| `src/web_functions/web/mc.css`    | 213   | all pages; WEB-DARK-01/02, WEB-A11Y-01, WEB-LED-02 (`.mcledbtn`), WEB-TD-01 (`.mcsegval`) live here |
| `src/web_functions/web/mc.js`     | 475   | shared logic; trim the Pins block (decision 1)                                                      |
| `src/web_functions/web/mcau.js`   | 108   | ESP32 only; Auto Update card                                                                        |
| `src/web_functions/web/mcrm.js`   | 330   | Remote page; RM-Q-01; trim fork-only commands (decision 1)                                          |
| `src/web_functions/web/README.md` | 39    | regeneration rules                                                                                  |
| `tools/gen_web_assets.py`         | 110   | stdlib only, `--check` mode; not called by the build                                                |
| `src/web_functions/web_assets.h`  | 2124  | generated; must be regenerated after trimming, never copied                                         |

Gzip size of the blobs: 4927 + 9836 + 3347 (ESP32 only) + 15337 = 33,447 B (30,100 B without `mcau.js`). Whether the
inline CSS/JS removed from the `.cpp` files pays this back is not measured; compare flash per env at port time
(`upstream/dev` against the port), especially nRF52.

The build does not call the script: `web_assets.h` is a checked-in `static const uint8_t[]` header included by
`web_functions.cpp` only. CI cannot break on it. A mismatch between sources and header is invisible to the build; add
`python3 tools/gen_web_assets.py --check` to the PR description as the check, not to CI.

### C++ glue (hand port, by hunk group)

`git diff --stat upstream/dev fork-dev -- src/web_functions tools/gen_web_assets.py`: 27 files, +5033 -1630. About 130
fork commits touch the directory since the merge base. Most of that is not wave 2.

| File / hunk group (fork-dev line anchors)                                                                                                                             | Decision          | Why                                                                                                        |
| --------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------- | ---------------------------------------------------------------------------------------------------------- |
| `web_functions.cpp` `deliver_web_asset()` (973), call in `work_webpage()` (1157), `deliver_scaffold()` head (1561-1642), `<style>`/inline JS removal                  | take              | the split itself (591a9e76)                                                                                |
| `web_functions.cpp` `sub_page_setup()` web-look block (3320-3335), T-Deck block (3337-3395), `extudpip` id (3536)                                                     | take              | WEB-DARK-01, WEB-TD-01, WEB-DUP-01                                                                         |
| `web_functions.cpp` `setparam()` T-Deck params `tdecktheme`, `tdchatfont`, `tdbldisp`, `tdblbright` (5057-5118)                                                       | take              | WEB-TD-01; all callees exist upstream (checked, see below)                                                 |
| `web_functions.cpp` small class/markup changes in `sub_page_wx/mheard/path/neighbours/info` (1904-2972, 4106-4239)                                                    | take, review each | match the CSS classes in `mc.css`; some belong to WX-01/Gateway label (already upstream)                   |
| `web_functions.cpp` Pins page: `sub_page_pins`, `sub_pin_status`, `sub_pin_assign`, `sub_gpio_status`, `gpioStatusCollect` (130-224, 4460-4640), nav button           | out               | wave 3                                                                                                     |
| `web_functions.cpp` `send_http_header()` chunked output, `webTxBeginChunked` (4690)                                                                                   | out               | WEB-TX-01, fork-only `web_txbuf` rewrite                                                                   |
| `web_functions.cpp` `sub_config_download/upload` hunks (876, 966)                                                                                                     | check             | verify per hunk whether they belong to the config JSON feature                                             |
| `web_functions.cpp` `sub_au_status` / `au_ui_state.h` `auAppendSched`                                                                                                 | out               | AU install schedule, wave 6; `mcau.js` degrades (`s.sched` absent gives empty text)                        |
| `web_functions.cpp` `HAS_NRF_ETH` guards (line 1 of four files)                                                                                                       | out               | fork build flag (`82627dcf`), not in upstream `platformio.ini`                                             |
| `web_rm_page.cpp/.h` (437 lines upstream, 115 fork)                                                                                                                   | take, trimmed     | replaces the inline RM JS by `mcrm.js`; same file as wave 4, expect overlap with RM work                   |
| `web_login_page.cpp` (1 line), `webUIComponents.cpp` (2 lines)                                                                                                        | take              | tokens for dark mode                                                                                       |
| `web_auth.cpp/.h` `webAuthSetPassword`, `web_rm_handlers.cpp/.h` `sub_web_passwd` (`POST /rmwebpwd`), `web_rm_parse.h` `rmParseWebPwdBody`, `web_guard.h` allow entry | take              | WEB-PWD-01, self-contained in the directory (`node_webpwd` exists upstream)                                |
| `web_setup.cpp` (table-driven rewrite, 1384 to 1030 lines)                                                                                                            | out               | a refactor; "no rewrites". LED mode, `gpioctl`, schedule, mcpout refusal, `webCoordMatches` all live in it |
| `web_coord.h` (WEB-LATLON-01, `test_web_coord`)                                                                                                                       | open              | a 16-line helper plus a host test; needs a backport into the upstream `web_setup.cpp` shape                |
| `web_oled_shot.*`, `web_txbuf.*`, `web_guard.h` gpio helpers, `Web-API_documentation.txt` fork rows                                                                   | out               | OLED screenshot (bench tool), chunked output, wave 3                                                       |

### Web features and what they need

| Feature (BACKLOG)                         | Where it lives                                                                                    | Fork-only dependency outside `web_functions/`                                                                                                                                               | Wave 2?                                                              |
| ----------------------------------------- | ------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------- |
| WEB-DARK-01/02, WEB-A11Y-01               | `mc.css`, `web_login_page.cpp`                                                                    | none                                                                                                                                                                                        | yes                                                                  |
| WEB-TD-01                                 | `mc.js` `tdSet/tdFont/tdDisp/tdBright`, setup T-Deck block, `setparam`                            | none: `node_tdeck_theme`, `node_tdeck_chatfont`, `brightness_mode_parse`, `brightnessSetActiveLevel`, `tdeck_refresh_SET_view` and `--theme/--chatfont/--bl mode` are all in `upstream/dev` | yes (T-Deck only, `#if BOARD_T_DECK*`)                               |
| WEB-DUP-01                                | `web_functions.cpp:3536` (id `extudp` becomes `extudpip`)                                         | none; upstream still has the bug at `web_functions.cpp:3672`                                                                                                                                | yes; could be its own one-line PR                                    |
| WEB-PWD-01                                | see C++ glue                                                                                      | none (`webAuthReset` is upstream)                                                                                                                                                           | yes                                                                  |
| WEB-GZ-01                                 | `deliver_web_asset()`, `web_assets.h`                                                             | none; ESP32 and nRF52 both serve it                                                                                                                                                         | yes                                                                  |
| RM-Q-01                                   | `mcrm.js` Queues card (`13b714ee`: 8 lines)                                                       | `/rmstatus`, `/rmsend`, `/rmnodes` exist upstream; the RM1 `txq` command exists upstream (`rm_exec_read.cpp`)                                                                               | yes, with the trimmed `mcrm.js`                                      |
| WEB-LED-01/02 (buttons)                   | Pins page only (`pn_led_*`, `372f6195`); Setup keeps `_create_setup_led_mode_element` as `unused` | `boardLedModeGet()` (`loop_functions.cpp:124`), `BOARD_LED_MODE_*` (`board_led.h`, upstream lacks the modes), persisted LED mode in settings (campaign W2/W3, `42bbe18f`)                   | no, wave 3. Only the `.mcledbtn` width rule (WEB-LED-02) rides along |
| Pins page, free GPIO, `gpioctl`           | `mc.js` lines ~285-430, `sub_page_pins`                                                           | `pinsInit`, `pinBoard*`, `gpioCtl*` (`gpio_control.*`), `pin_assign_json.h`, `gpio_status_json.h`, fork `/pinstatus /pinassign /gpiostatus`                                                 | no, wave 3                                                           |
| RM1 pins / gpio / chgled / upd chan cards | `mcrm.js` (`rmCmds`, `rmPinsView`, `rmGpioView`, `rmChgRow`, `rmUpdView`)                         | fork RM1 commands in `rm_exec_write.cpp`, `rm_commands.h`, `rm_format.h` (wave 4); the sender refuses unknown commands                                                                      | no, wave 4                                                           |
| Auto Update install schedule              | `mcau.js` line 48-50, `fw_schedule.h`                                                             | `node_auday/audelay/autime`, `fwSchedParse*`, `auAppendSched`                                                                                                                               | no, wave 6; degrades safely                                          |

### Fork-only dependencies that block a literal copy

| Symbol                                                                            | Defined in (fork only)                                  | Reached from                                            |
| --------------------------------------------------------------------------------- | ------------------------------------------------------- | ------------------------------------------------------- |
| `boardLedModeGet`, `BOARD_LED_MODE_STEADY/FLASH/OFF`                              | `src/loop_functions.cpp`, `src/board_led.h`             | Pins page, `web_setup.cpp`                              |
| `pinBoardContext`, `pinClassify`, `pinsInit`, `gpioCtlSnapshot`, `gpioCtlCommand` | `src/pin_*`, `src/gpio_control.*`                       | Pins page, `web_setup.cpp`, `web_guard.h`               |
| `webTxBeginChunked`, `webTxSendFramed`                                            | `src/web_functions/web_txbuf.*`                         | `send_http_header()`                                    |
| `fwSchedParseDay/Delay/Time`, `auAppendSched`, `fwBattOk`                         | `src/fw_schedule.h`, `src/fw_update.h`, `au_ui_state.h` | Setup AU card, `/austatus`, `web_nodefunctioncalls.cpp` |
| `mcpout_check_port`, `mcpout_refusal`                                             | `src/mcpout_refusal.h`                                  | `web_setup.cpp`                                         |
| `oledShotSource`, `tdeck_dbg_shadow_*`                                            | `web_oled_shot.cpp`, T-Deck shadow buffer               | `/screen/oled`                                          |
| `HAS_NRF_ETH`                                                                     | fork `platformio.ini` / variants                        | first line of four web `.cpp` files                     |
| `clock_instrument.h`                                                              | `src/` (fork only)                                      | `loop_functions.cpp` (Part A, drop)                     |

### Conflict risk

High. `web_functions.cpp` (5067 lines upstream, 5199 fork) has 60+ hunks against the fork, and `web_rm_page.cpp` is
replaced almost whole. Upstream last touched the directory on 2026-11-13 (`71ba7b69`, `#1258`) and wave 4 (RM) edits
`web_rm_page.cpp` again. Plan: start from `upstream/dev`, add the new files, then apply the "take" groups by hand and
regenerate `web_assets.h` from the trimmed sources. Do not cherry-pick the fork web commits.

### REVIEW.md

| Rule          | Status                                                                                                                                                                                                                   |
| ------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| R1 platforms  | `mcau.js` ESP32 only (the generator wraps it in `#if defined(ESP32)`); the web GUI itself is compiled on ESP32 and, in upstream, behind `NRF52_SERIES` branches on nRF52 (W5100S Ethernet). T-Deck block is T-Deck only. |
| R2 every env  | The header is `static` and included by one translation unit; build every env with a web server, and one without. Flash per env: compare `upstream/dev` against the port.                                                 |
| R3 surfaces   | Browser (Chrome, Safari), `setparam` read-back, BLE/console unaffected. Escaping lint `tools/tests/test_web_escape_lint.py` is fork-only: run it locally.                                                                |
| R4 LoRa bytes | No change in the web part; `mcrm.js` only sends RM1 commands the firmware already encodes. Trimming unknown commands avoids sender-side refusals.                                                                        |

### Bench rows before the PR

| Row                                                                                               | Where                                     | Source                                              |
| ------------------------------------------------------------------------------------------------- | ----------------------------------------- | --------------------------------------------------- |
| WEB-DARK-01, WEB-DARK-02, WEB-A11Y-01 (Chrome light/dark, Neighbours, queue chart, selected menu) | Browser                                   | BACKLOG, campaign B-1                               |
| WEB-DUP-01 (IP field and switch both save)                                                        | Browser                                   | BACKLOG                                             |
| WEB-PWD-01 (set, change, clear)                                                                   | Browser, one node                         | BACKLOG                                             |
| WEB-GZ-01 (cold cache, two browsers)                                                              | ESP32 and DK5EN-90                        | BACKLOG, campaign B-1                               |
| WEB-TD-01 (Safari look)                                                                           | Browser, DK5EN-14                         | BACKLOG, campaign B-1                               |
| RM-Q-01 (Queues card Read via RM1 between bench nodes)                                            | Browser, two nodes                        | BACKLOG, campaign B-1                               |
| WEB-LATLON-01 (`setlat/setlon`)                                                                   | DK5EN-90, only if `web_coord.h` is ported | campaign B-1                                        |
| WEB-LED-01/02                                                                                     | DK5EN-1, Browser                          | listed in B-1 but wave 3 if the Pins page stays out |

## What stays out of the PR

| Item                                                                                                                                                                                   | Why                                                                    |
| -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------- |
| `docs/*`, `RESUME.md`, `BACKLOG.md`, rca files                                                                                                                                         | fork docs                                                              |
| `test/test_display_clock`, `test_ping_budget`, `test_csma_timing`, `test_command_setters`, `test_web_guard`, `test_web_coord`, `test_web_stop_gate`, `test_rm_web_parse`, golden files | upstream has no such envs; fork stubs                                  |
| `tools/webgui_*_test.js`, `tools/webgui_assets.js`, `tools/tests/test_gen_web_assets.py`, `tools/regression.sh`                                                                        | fork tooling (the generator test is a candidate if upstream wants one) |
| `src/clock_instrument.h` and all `clockInstr*` calls, `oledTextTest`, `web_oled_shot.*`                                                                                                | INSTRUMENT/bench tooling                                               |
| `CLAUDE.md`, `REVIEW.md` changes, `.claude/`                                                                                                                                           | fork-only                                                              |

`test_web_auth` and `test_web_txbuf` are the only web tests upstream carries. The fork added 55 lines to `test_web_auth`
(16 added lines reference `webAuthSetPassword`) and 418 to `test_web_txbuf` (chunked output). The first can go with WEB-PWD-01; the second
stays out with the chunking.

## Operator decisions 2026-11-16

These answers replace the five open questions. They override the "out" and "open" verdicts in the tables above.

1. **Web scope: ship everything.** The full end-state `mc.js` and `mcrm.js` go into wave 2, including the Pins, GPIO,
   charge-LED and update-channel blocks. Reason: fail fast in the field. A new upstream release reaches only 3-5% of nodes
   in its first days, so a gradual wave rollout limits the damage. RM cards may show refusals on upstream nodes until
   waves 3 and 4 land.
2. **CSMA-03 loop poll: keep it.** The 1 ms loop poll stays inside `b3b2bf0b` when that commit is ported.
3. **`web_setup.cpp`: take the table-driven rewrite.** Port it together with WEB-LATLON-01 (`web_coord.h`, `test_web_coord`).
4. **WEB-DUP-01: on the release schedule.** No separate early PR; the fix goes into the wave-2 web PR.
5. **Time: keep the plan.** Two PRs (Ping and Web) on Fri 2026-11-20. If something breaks in the field, react quickly and
   learn from it.

## Remaining note

- **Observation, outside this wave (closed in I-2, see the campaign doc).** `upstream/dev:src/nrf52/nrf52_ble.cpp` calls `sanitize_ping_target(...)` before
  `prodLockNormalise()` in the BLE settings write; `fork-dev` no longer does there (the call remains in
  `nrf52_flash.cpp:421` and `esp32_flash.cpp:254`). Check whether the fork's BLE path still sanitises a pushed
  `node_pingcall`.

## Trial build 2026-11-16 (option A, real dependency list)

Scratch branch `scratch/web-trial-20261116` (worktree `../mc-web-trial`, local only, not pushed) from `upstream/dev`
`70ae124d`, unchanged since the last sync. Method: the fork's whole `src/web_functions/` copied in, then rebuilt until
every env linked. Only the files and hunks needed for that were added. Out-of-scope code was put behind a switch
(`MC_WEB_AU_SCHED`, off) and not ported. Six commits on the branch, one per step.

### Builds

Four envs build and link on the trial branch. The baseline is the same env on `upstream/dev`.

| Env                      | Flash upstream | Flash trial | Delta   | RAM upstream | RAM trial | Delta  |
| ------------------------ | -------------- | ----------- | ------- | ------------ | --------- | ------ |
| `wiscore_rak4631`        | 693,628        | 664,712     | -28,916 | 99,348       | 102,948   | +3,600 |
| `heltec_wifi_lora_32_V3` | 1,774,353      | 1,740,237   | -34,116 | 129,372      | 130,428   | +1,056 |
| `t_deck`                 | 2,395,261      | 2,363,717   | -31,544 | 150,796      | 151,852   | +1,056 |
| `ttgo_tbeam`             | 1,877,445      | 1,842,825   | -34,620 | 104,664      | 105,704   | +1,040 |

The gzip assets pay for themselves: flash shrinks by 29-35 KB on every env. That answers the open flash question from
part B. The nRF52 RAM growth (+3.6 KB) is not explained yet; check `web_txbuf` and the pin tables. Not built: E22,
T-Beam Supreme, T-Deck Plus, and an env without a web server. All of these are needed for R2.

### Must come along (compile-time, in scope)

| Item                                                                                                                      | Size             | Why                                                |
| ------------------------------------------------------------------------------------------------------------------------- | ---------------- | -------------------------------------------------- |
| `pin_board.cpp/.h`, `pin_policy.h`, `pin_status_json.h`, `pin_assign_json.h`, `gpio_control.cpp/.h`, `gpio_status_json.h` | 1,579 lines, new | Pins page, `/pinstatus /pinassign /gpiostatus`     |
| `meshcom_settings.h` + `settings_schema.h`: `node_gpioctl` (ESP32 block, persisted)                                       | 3 lines          | `gpio_control.cpp`; a new persisted setting        |
| `settings_sanitize.h`: `mcPinReservedNrf52840`                                                                            | 7 lines          | `pin_policy.h`                                     |
| `board_led.h`: `BOARD_LED_MODE_*`, `boardLedMode/Name/Decide/SteadyRearm`                                                 | 44 lines         | LED buttons in Setup and on the Pins page          |
| `loop_functions.cpp` + `_extern.h`: `boardLedModeGet()`                                                                   | 7 lines          | same                                               |
| `mcpout_refusal.h`                                                                                                        | 38 lines, new    | `web_setup.cpp` (WEB-PIN-01)                       |
| `configuration_global.h`: `MESHCOM_VERSION_SHORT`                                                                         | 3 lines          | web logo; the other BOOT-01 places can stay behind |
| `fw_update.h`: `fwBattOk` (QA-AU-09)                                                                                      | 5 lines          | `updapply` in `web_nodefunctioncalls.cpp`          |

### Must come along (runtime only, the compiler does not see it)

The build links without these, but the web buttons send console commands upstream does not know, so the click fails
quietly. Not ported in the trial.

| Item                                                                                                          | Where (fork-dev)                                             |
| ------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------ |
| `--board led steady` row and `tg_post_board_led_flash/steady` (steady bit `node_sset4 0x4000`, free upstream) | `command_functions.cpp` 530-531, 689-691                     |
| `boardLedTick` steady branch, RM hold, NeoPixel re-arm                                                        | `loop_functions.cpp` (about 40 lines)                        |
| `--gpiocontrol on/off` and `--gpio list/<n> on/off/in/release`                                                | `command_functions.cpp` 2821-2890                            |
| pin assign refusal and load repair (`pinCanAssign`, `pinRepairLoaded`, wind pin)                              | `command_functions.cpp`, `esp32_flash.cpp`, `esp32_main.cpp` |

Checked and already upstream: `--charge led on/off`, `--rmstrictsecurity`, `--update check/install`, the T-Deck
setters, every JS endpoint except the three Pins endpoints (served by the copied `web_functions.cpp`).

### Fenced out (hand trim in the PR)

| Item                                                                                                                      | Trim                                                                                    |
| ------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------- |
| AU install schedule (wave 6): `node_auday/audelay/autime`, `FWSCHED_*`, `fwNetSchedGet`, `auAppendSched`, `fw_schedule.h` | 5 blocks in `web_functions.cpp` (2) and `web_setup.cpp` (3); `mcau.js` degrades already |
| `HAS_NRF_ETH` guard on line 1 of seven web `.cpp` files                                                                   | must go: without it the RAK fails to link (`startWebserver`, `loopWebserver` undefined) |
| `web_oled_shot.*`, `oled_shot.h`                                                                                          | dropped; `/screen.*` endpoints are `INSTRUMENT_ENABLED` and compile out upstream        |

Still to decide by hunk while porting: `web_rm_page.cpp` and `mcrm.js` call RM1 commands of wave 4. They compile, but
upstream nodes refuse them (accepted in decision 1). `web_assets.h` stays consistent because the sources were not
trimmed; regenerate it with `tools/gen_web_assets.py --check`, which has to come along too.
