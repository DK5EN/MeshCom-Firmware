# Campaign RM1 `upd chan` (update channel switch DEV/MAIN, 2026-10-31)

Request: "dann mach in mcapp RM1 bitte einen Schalter für DEV/MAIN". Fork-only (RM1 and AU are not
upstream), no upstream PR. Base `fork-dev` at `b7802398`.

## Grammar

| Command         | Reply                                                                        |
| --------------- | ---------------------------------------------------------------------------- |
| `upd chan prod` | `ok chan prod`, `err aubusy`, `err unsupported`                              |
| `upd chan dev`  | `ok chan dev`, `err aubusy`, `err unsupported`                               |
| both, no flag   | `err blocked` (builds without `MC_AU_DEV_CHANNEL` refuse the argument shape) |

- Wire tokens stay `prod`/`dev` (as `c=` and the console); "MAIN"/"DEV" is a UI label only.
- A change saves `node_updchan` and clears the last check result (`a=-`, `n=0`); a staged image (`st=`)
  stays. No check starts; the UI sends `upd check` afterwards (sender spacing 10 s).
- Same channel: `ok`, nothing saved or cleared. `err aubusy` while `b=1` or `g=1`.
- One function `auSetChannel()` (`src/rm_au.h`) serves RM1 and console `--updchan`; `/setparam updchan`
  runs `--updchan`, so the web path gets the same behaviour. Console change while busy is new:
  `[ERR];updchan;busy`.

## Operator decisions 2026-10-31

- Clear the check result on a switch, keep the staged image; no automatic check.
- Fix the stale `a=` on the console/web path in this campaign.
- Remote switch only in builds with `MC_AU_DEV_CHANNEL`; no strict-mode requirement.
- McApp: requirements file plus backlog item B14 in the MCProxy repo (a later code agent implements FE
  and BE there).
- Bench: sender DK5EN-92, target DK5EN-1, both on USB; flashing both explicitly approved.

## Waves

| Wave | Content                                                                        | State |
| ---- | ------------------------------------------------------------------------------ | ----- |
| 0    | Contract `src/rm_au.h`, `rm_commands.h` comment, `[env:native_remote_cmd_dev]` | done  |
| 1    | A1 wire, A2 AU, B console, C web page                                          | done  |
| 2    | Gate stage 1,2; clean builds Heltec V3, T-Beam, RAK4631; image scan; advisor   | done  |
| 3    | McApp requirements + B14 in MCProxy                                            | done  |
| 4    | Docs: `docs/rm1-commands.md`, RESUME, BACKLOG                                  | done  |
| -    | Bench: sender DK5EN-92, target DK5EN-1                                         | done  |

### Wave 1 file ownership

| Agent | Files                                                                                                                                                                        |
| ----- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| A1    | `src/remote_cmd.cpp`, `src/rm_exec_write.cpp`, `src/rm_exec_ext.h`, `tools/remote_cmd.py`, `tools/tests/` (vectors, py tests), `test/test_remote_cmd`, `test/test_rm_format` |
| A2    | `src/esp32/rm_au_esp32.cpp`, `src/rm_au_none.cpp`, `src/esp32/fw_update_net.{h,cpp}`, `src/fw_update.h`, `test/test_fw_update`                                               |
| B     | `src/command_functions.cpp` (`--updchan` rung), `test/test_command_setters`                                                                                                  |
| C     | `src/web_functions/web_rm_page.cpp`, `tools/webgui_rm_test.js`                                                                                                               |
| Orch. | `src/rm_au.h`, `src/rm_commands.h`, `platformio.ini`, `test/golden/`, docs                                                                                                   |

### Waves 1-2 closed (2026-10-31)

- Four Sonnet-medium writers; each new test failed before and passes after (B's scan: orchestrator ran it
  against the HEAD copy of `command_functions.cpp`).
- Gate run 1: one red pin, `s_st.zlen =` count 2 (the reset in `fwNetForgetAvail`). Scan now excludes that body.
- Advisor pass 1 (Opus): rework, the RM write branch and the ESP32 glue were compiled by no native env.
  Fresh Sonnet-high implementer added source pins `test_rm_upd_chan_glue_pins` (mutations "drop
  fwNetForgetAvail" and "return 1" fail). Orchestrator: short sent-history label "Update channel DEV|MAIN",
  UCH-14.
- Advisor pass 2: the persistence pins were a blacklist; orchestrator made the `fwNetForgetAvail` body a
  whitelist (mutation `fwNetForgetLast();` fails at the whitelist pin). No third advisor run: test-only,
  mutation-verified; `native_command_setters` rerun after it 51/51 (gate run 2 predates this test-only edit).
- Gate run 2: Unity 85 envs 3219/3219, stage 2 PASS; clean builds with `MC_AU_DEV_CHANNEL=1`:
  heltec_wifi_lora_32_V3 RAM 39.5 % flash 55.9 %, ttgo_tbeam flash 59.4 %, wiscore_rak4631 flash 86.9 %.
  Image scan Heltec V3 and T-Beam: `ok chan %s`, `updchan;busy`, `var rmUpdChan=1` present.
- Known residuals: after a switch a staged image of the old channel stays installable (operator decision);
  `s_auNotifiedTag` survives a switch (a re-reported tag is not notified again); FWNET_STUB builds answer
  `--updchan` with `unsupported`.

### Bench (2026-10-31 09:30-09:41)

- Both nodes flashed over USB with the `636e681b` images (app + otadata at 460800, bootloader and Safeboot
  untouched); identity guard ok (DK5EN-1 192.168.68.71, DK5EN-92 .70, both 2 dBm). RM1 driven through DK5EN-92's
  `POST /rmsend` (no web password set on either node), every reply verified:

```
status          ok v=4.40a up=1 bat=100 heap=109 s=GtdMWl p=2/0/22 rs=0
upd             ok m=notify c=dev n=0 a=- st=- b=0 g=0 ck=- e=-
upd check       ok checking
upd             ok m=notify c=dev n=0 a=v4.40a.10.30 st=- b=0 g=0 ck=0 e=-
upd chan prod   ok chan prod
upd             ok m=notify c=prod n=0 a=- st=- b=0 g=0 ck=- e=-
upd chan prod   ok chan prod
upd check       ok checking
upd chan dev    ok chan dev          (check already finished, its prod result cleared)
upd             ok m=notify c=dev n=0 a=- st=- b=0 g=0 ck=- e=-
```

- `err aubusy` over RM1 is not reproducible on air: the 10 s sender spacing outlasts a check. Shown on the
  console path instead (2323, same `auSetChannel()`): `--update check` then `--updchan prod` ->
  `[ERR];updchan;busy`; after the check `--updchan prod` -> `[AU];chan;prod` and `--update status` shows
  `avail;none` (the dev result cleared).
- End state: both nodes on `636e681b`; DK5EN-1 back on `dev` (its setting before the bench).
- Web page channel row: jsdom-tested (UCH-01..14), not clicked in Chrome.
- McApp work order: MCProxy `doc/2026-10-31_1000-rm1-updchan-requirements.md`, backlog B14 (commit `786099a`).

### Chrome check and selected style (2026-10-31 10:00-10:15)

- Remote page of DK5EN-92 in Chrome, target DK5EN-1 (saved slot 0): no password -> "Type the password of DK5EN-1
  first.", nothing armed; first tap arms with the confirm text, second tap sends; `ok chan prod` and `ok chan dev`
  verified over LoRa, card and history row ("Update channel MAIN · verified · Channel set to MAIN. Run Check.") as
  specified. DK5EN-1 back on `dev`.
- Finding fixed: the active channel tile is disabled and looked unavailable (opacity 0.45 from the page rule, 30 %
  text alpha from Chrome's disabled style). Own rule `#rm_upd_chan button.rmsel:disabled` (full opacity, inherited
  colour, grey 2 px border, bold) after the disabled rule; jsdom UCH-15 fails on the HEAD page, passes now. Gate
  stage 1,2 PASS (3219/3219); DK5EN-92 flashed, checked in Chrome.
