# Plan: manual OTA onto an old Safeboot, and OTA that cannot update Safeboot (P-0209)

Date: 2026-11-21. Branch `fork-dev` at `067d29cd`; the problem file's RCA pin is `96aaa67c72` (v4.40a.11.21).
Ticket: P-0209 (incident desk, `state: analyzed`, `check.verdict: open`). Proposed backlog Id: `OTA-SAFEBOOT-01`.
Status: plan only, nothing implemented. ESP32 only.

Sources: the problem file `telegram/problems/P-0209-ota-cannot-update-safeboot-old-safeboot-needs-usb.md`,
`docs/safeboot-ota-contract.md`, `docs/campaign-au16-dio-bootloader.md`, `docs/concept-nightly-01-20261116.md`.

Operator decisions already taken: none recorded for P-0209. `telegram/problems/DECISIONS-2026-11-21.md` has no
P-0209 or safeboot entry (checked by grep); the check report leaves the question open ("gate manual OTA by the
Safeboot version, or let Safeboot update itself"). Both are put to the operator in section 8.

## 1. Symptom

- 35403 / 35405 (2026-10-18): OTA never installs a new Safeboot, so a node keeps the Safeboot it was flashed with.
  The app refuses auto update when that Safeboot is too old; one pass through the web flasher is needed.
- 35232 / 35229 / 35222 (2026-10-24): a node was updated by hand over the air while it ran firmware with a reboot
  bug and did not come back. The automatic return from Safeboot after 180 s exists only in current Safeboot images.
  Which Safeboot DB0ND ran is not known (not verified).

Two separate things, kept apart (the problem file does the same):

| Case                                                       | Verdict at HEAD | This plan                          |
| ---------------------------------------------------------- | --------------- | ---------------------------------- |
| A. No OTA path replaces Safeboot (35403, 35405)            | by design       | Phase 2, optional, decision D4     |
| B. Manual OTA onto an old Safeboot is not gated (35232 ..) | open            | Phase 1, the fix this plan carries |

## 2. Root cause at HEAD (path:line)

Case A, by design, unchanged. The upload writes `ota_0` only: `src/safeboot/ElegantOTA.cpp:193`
(`Update.begin(UPDATE_SIZE_UNKNOWN, ... U_SPIFFS : U_FLASH)`, ESP32 branch; `:183` is the ESP8266 branch). The
staged apply erases and writes `ota_0` only (`src/safeboot/main.cpp:619`, `esp_partition_erase_range(ota0, ...)`).
Contract: `docs/safeboot-ota-contract.md:234-235` "OTA and auto update never replace Safeboot; only the web flasher
does." No app-side code writes the `safeboot` partition: `grep -rn "esp_partition_write\|esp_partition_erase_range" src` outside `src/safeboot` finds only a comment (`src/esp32/fw_update_net.cpp:934`).

Case B, open. The guard covers the automatic path only:

- `src/command_functions.cpp:274` `auRebootToSafeboot()` checks the bootloader (`fwNetBootloaderQio()`, `:278`) and
  that a `safeboot` partition exists (`:289`), then `esp_ota_set_boot_partition()` (`:295`) and `esp_restart()`.
  It never reads the Safeboot version.
- `src/command_functions.cpp:1680-1682`: `--ota-update` calls `auRebootToSafeboot()` directly. Same door from the web
  button `otaupdate` (`src/web_functions/web_nodefunctioncalls.cpp:42-43`, button at `web_functions.cpp:3386`), which
  also goes through `commandAction("--ota-update")`. The X-MC header guard (`web_guard.h:788`) is no version check.
- `src/command_functions.cpp:6175-6177`: `--update apply` only logs `[AU];apply;note;old_safeboot_flag_set_trying_anyway`
  or `[AU];apply;note;safeboot_too_old;ver;%d;trying_anyway` and proceeds.
- Guarded paths for contrast: `--autoupdate auto` (`src/command_functions.cpp:5944`), boot demotion
  (`src/esp32/esp32_main.cpp:3126-3134`), the handover (`fwNetSafebootCapable()`, `esp32_main.cpp:3210`, `:3272`),
  `--update install` via `fwGoDecide` (`FWGO_SAFEBOOT`, `src/fw_update.h`). The version is read by
  `fwNetSafebootVersion()` (`src/esp32/fw_update_net.cpp:2186`), `AU_SAFEBOOT_MIN 1`,
  `SAFEBOOT_VERSION 3`, marker `MCSB;ver;003` (`src/safeboot/safeboot_ver.h:26-33`).

Why the old Safeboot is dangerous: v4.35o Safeboot calls `esp_ota_set_boot_partition(ota_0)` without checking the
result and restarts (`git show v4.35o:src/safeboot/main.cpp`, line 229), so a node without a valid app loops. Fixed
by `a59b74d5` (2026-09-25, ancestor of HEAD, tags from `v4.35u`, not reverted); the `app_valid` check was hardened
later by SB-06 (`src/safeboot/main.cpp:383-391`, `checkAppImageValid()`). An OTA cannot deliver either fix.

`git log --oneline 96aaa67c72..HEAD` over `src/command_functions.cpp`, `src/esp32/fw_update_net.cpp`,
`src/esp32/fw_update_net.h`, `src/esp32/esp32_main.cpp`, `src/fw_update.h`, `src/safeboot/`,
`docs/safeboot-ota-contract.md` lists three commits: `8b4d509b` (bench hook), `ce05f38c` (AppLink),
`f4af55b0` (CMD-DASH-01 dash tolerance). None touches the cited guards. Per-symbol searches
`git log --oneline 96aaa67c72..HEAD -S<sym>` for `auRebootToSafeboot`, `trying_anyway`, `safeboot_too_old`,
`fwNetSafebootVersion`, `AU_SAFEBOOT_MIN` are all empty. The only line drift since the pin is from those three
commits (`:1670` is now `:1682`, `:6089` is now `:6177`, `:276` is now `:278`). The two tree-modified files
`safeboot.bin` and `safeboot-s3.bin` are not read or touched by this plan.

## 3. Platforms

| Target             | Safeboot partition                                                                                                                                                 | Affected                                                                                                                                                                                                                                                                                                         |
| ------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| ESP32 classic      | factory, 0x10000, 704K (`partitions-4MB-safeboot.csv`), image `safeboot.bin` (0x1000 bootloader)                                                                   | yes, case A and B                                                                                                                                                                                                                                                                                                |
| ESP32-S3           | same layout, image `safeboot-s3.bin`, bootloader at 0x0                                                                                                            | yes; additionally AU-16: a DIO bootloader already blocks `--ota-update` (`command_functions.cpp:278`)                                                                                                                                                                                                            |
| ESP32-C3           | none: no C3 env or board in the tree (`grep -i c3 platformio.ini boards` hits only a library name), only `esp32-safeboot` and `esp32-S3-safeboot` build a Safeboot | not applicable                                                                                                                                                                                                                                                                                                   |
| nRF52 (RAK4631 ..) | no Safeboot, no `safeboot` partition                                                                                                                               | no. `--ota-update` and `auRebootToSafeboot()` are `#ifdef ESP32` (`command_functions.cpp:273`, `:1678`; `web_nodefunctioncalls.cpp:41`). nRF52 is updated through the UF2/serial DFU bootloader (`--dfu`, `command_functions.cpp:1595`), by hand; the app does not replace that bootloader either. Out of scope. |

Partition headroom matters for Phase 2: the 4MB and 16MB tables both give Safeboot 704K = 720896 B. Committed
classic images: `v4.35o` 687264 B, `v4.35u` 716128 B, `v4.40a.10.05` 719360 B (1536 B below the limit),
`v4.40a.11.21` 690800 B. S3: 644128, 677008, 680368 B. Version evidence per tag (marker search in the committed
image, read-only `git show <tag>:safeboot.bin`): `v4.35o`, `v4.35u`: no marker (version 0); `v4.40a.10.05`: no marker,
`FWS2` present (version 1); `v4.40a.10.19`: `MCSB;ver;002`; `v4.40a.11.21`: `MCSB;ver;003`.

## 4. Fix design

### Phase 1: gate the manual door (small, native-testable)

Make `--ota-update` (serial, BLE and net console, web button; RM1 already answers `err blocked`, `docs/rm1-commands.md:79`) refuse when the Safeboot version is known to be below the
minimum, with one decision function shared by tests and firmware.

1. `src/fw_update.h`, next to `fwSafebootWhy()` (`:680`): a pure helper

   ```cpp
   enum FwManualOtaVerdict : uint8_t { FWOTA_GO = 0, FWOTA_OLD };
   static inline FwManualOtaVerdict fwManualOtaDecide(int sbVer, int minVer, bool force);
   ```

   Rules: `force` gives GO; `sbVer < 0` (partition missing or unreadable, or the `FWNET_STUB` measurement build,
   `fw_update_net.cpp:98`) gives GO because `auRebootToSafeboot()` already reports the missing partition and no new
   refusal may appear in builds without the scanner; `0 <= sbVer < minVer` gives OLD; otherwise GO. The DIO case
   stays where it is (`auRebootToSafeboot()`, AU-16, no override).

2. `src/command_functions.cpp:1680`: before `auRebootToSafeboot()`, call `fwManualOtaDecide(fwNetSafebootVersion(),
AU_SAFEBOOT_MIN, hasForceArg)`. On OLD print lines of the same shape as the auto path (`[ERR];ota-update;...` next to the auto path's `[ERR];autoupdate;...`, `command_functions.cpp:5944`), so tools see one wording:
   `[ERR];ota-update;safeboot_too_old;ver;%d;need;%d` and `[AU];note;flash once with the web flasher to install the new
Safeboot`, then add `[AU];note;--ota-update force overrides at your own risk` and return without a display or boot
   pointer write. `--ota-update force` is parsed with the ladder's existing argument helper (the exact-token rule of
   `command_match.h` lets a space end the name). Update the help line (`:1726`) and `docs/rm1-commands.md` (both
   language sections) and the command ladder lint inputs if they list arguments.
3. Web button: `commandAction` returns void, so the page cannot tell. Reuse the existing status fields `sbok` and
   `sbwhy` of `/austatus` (AU-16 already ships them) to grey the button and name the reason, as the AU dropdown does.
4. `docs/safeboot-ota-contract.md:229-235` (also still says "current: 2" at `:225`; the code says 3, drive-by
   doc fix): add that manual `--ota-update` is refused below `AU_SAFEBOOT_MIN` and the `force` override.
5. `--update apply` stays as is (decision D2): an install needs a capable Safeboot (`FWGO_SAFEBOOT`), so a staged
   record normally cannot exist next to an old Safeboot.

What this does not do: it cannot protect a node whose Safeboot reports version 0 but already contains the `a59b74d5`
fix (images from `v4.35u` to `v4.40a.10.04`, no marker). Those are refused too unless `force`. The scanner cannot
tell them apart; adding a second marker string for that is not worth it (decision D3).

### Phase 2: Safeboot self-update from the app (optional, large)

Only if the operator wants Safeboot to reach nodes without USB. Sketch, not a commitment:

- Source: the release already publishes `safeboot.bin` and `safeboot-s3.bin` (`.github/workflows/meshcom-ci.yml:203-204`).
  Download through the existing job and TLS path, digest-checked like `.autoupdate`. Caveat: the TLS heap limits
  (AU-21, AU-24, AU-25) apply to a second download.
- Stage in the tail of `ota_0` (the layout of `fwStageLayout()`, `src/fw_update.h:256-290`), verify with the
  existing scanner (`SafebootVerScan`: marker version must be at least the installed one) and the chip check
  (`checkAppImageHead`, `src/safeboot/app_check.h:72`: classic 0x0000 vs S3 0x0009, so an S3 image can never go
  into a classic node), then erase and write `safeboot` from the running app (the app runs from `ota_0`, Safeboot
  is not executing, so nothing erases the running code), verify read-back, keep the staged copy until verified.
- Resume: if power fails between erase and verify, the next boot of the app sees an invalid `safeboot` partition
  (`fwNetSafebootVersion()` returns -1) plus a staged copy, and repeats the copy.
- Always needs USB: the 2nd-stage bootloader (0x1000 or 0x0) and the partition table (0x8000); the DIO to QIO
  bootloader fix (AU-16); every node whose app is old enough to lack this code; any node already bricked.

One rule for both doors: the web button is greyed by `sbok`/`sbwhy` from `fwNetSafebootWhy()`
(`src/fw_update_net.cpp:2267`: version >= minimum and the `nocap` failure flag clear). The console gate must use the
same predicate, so a failed version scan (-1) and the `nocap` flag refuse on both doors; do not write a second rule.
The existing pin `test_ota_update_and_the_handover_share_one_reboot_function` (`test/test_command_setters/test_command_setters.cpp:959-994`)
cuts the `--ota-update` rung up to the next `#endif`: the new gate must stay inside that rung.

## 5. Test: fails before, passes after

Phase 1, two tests, both native, no board:

1. `test_manual_ota_decide` in `test/test_fw_update/test_main.cpp` (envs `native_fw_update` and
   `native_fw_update_prod`; run `pio test -e native_fw_update -f test_fw_update`, never bare `pio test`). Cases:
   version 0 with min 1 gives OLD; version 1, 2, 3 give GO; -1 gives GO; version 0 with `force` gives GO; min 0
   gives GO. Today it does not compile (`fwManualOtaDecide` is absent), which is the before-run; after the header
   change it passes. Because "does not compile" is a weak failing state, the second test makes the before-run an
   assertion failure:
2. `test_manual_ota_gated_by_safeboot_version` in `test/test_command_setters/test_command_setters.cpp` (env
   `native_command_setters`, the same source-pin style as `test_dio_bootloader_blocks_safeboot`, `:1560`): in
   `command_functions.cpp` the `"ota-update"` branch must contain `fwManualOtaDecide(` and the
   `safeboot_too_old` refusal text before `auRebootToSafeboot();`. Today the branch (`:1680-1685`) holds only the
   call, so `TEST_ASSERT_TRUE_MESSAGE` fails; after the change it passes. This pin is what stops the door from
   being reopened by a refactor. The existing pin at `:1569`
   (`--update apply` text with `trying_anyway`) stays valid under D2.

Phase 2 (if built): the staged-copy resume decision, the chip check and the "marker version must not go down"
rule are pure and go into the same `fw_update.h` style helpers with native tests; the flash write itself is
bench-only.

## 6. Bench step (Phase 1)

Board: one ESP32 classic and one ESP32-S3 bench board from the `/bench` skill (load it first), never DK5EN-98. Rules:
`tools/bench/identity_guard.py` first, `lsof <port>` before opening a port, test traffic only to group `TEST` or a
direct message to an own node, TX at most 2 dBm, no foreign call sign. Opening a serial port reboots an ESP32.

1. Build and flash the Phase 1 image over USB (operator OK for the flash).
2. Control: with the current Safeboot (marker 003) send `--ota-update`; expected: node reboots into Safeboot as today
   (back to the app after 180 s or a normal upload).
3. Old Safeboot: write the version-0 image by USB with the operator's OK, taken read-only from git
   (`git show v4.35u:safeboot.bin > <scratchpad>/sb-v0.bin`, S3: `safeboot-s3.bin`) to the Safeboot partition 0x10000
   only, not the bootloader or otadata. `--update status` must show `sbver;0;sbneed;1`. Send `--ota-update`; expected:
   `[ERR];ota-update;safeboot_too_old;ver;0;need;1`, no display change, no reboot. Send `--ota-update force`; expected:
   the node enters Safeboot (state recoverable by USB on the bench).
4. Version 1 image (`git show v4.40a.10.05:safeboot.bin`): `--ota-update` is accepted (version 1 meets the minimum).
5. Restore the current Safeboot from the web flasher. Web button: greyed with `sbwhy` on the version-0 image.

## 7. Upstream impact

Yes. `upstream/dev` (`5698913a`, an ancestor of HEAD) has the same ungated path: `auRebootToSafeboot()` at
`upstream/dev:src/command_functions.cpp:250`, `--ota-update` at `:1502-1504`, the `trying_anyway` notes at `:5880-5882`,
and `AU_SAFEBOOT_MIN` in `safeboot_ver.h`. Phase 1 is small and ESP32-only: a PR cut from `upstream/dev` after it
lands and benches on `fork-dev` (PR body in German, minimum port, one commit for the gate, tests and docs). It
touches no LoRa frame and no byte budget (R4 does not apply). Phase 2 is fork-only until it has run on hardware.
Interaction: NIGHTLY-01 wave W5 (Safeboot v4, USB flash once per opted-in node) is the same question seen from the
nightly side (`docs/concept-nightly-01-20261116.md:79`, `:393`); Phase 2 would remove that USB step.

## 8. Risks

- Bricking, Phase 2 (explicit): the `safeboot` partition is rewritten from the app. A write that fails after the
  erase leaves no usable Safeboot. If the app still boots, `--ota-update` fails (no valid image to boot) and the node
  keeps running but cannot be updated over the air any more until the staged copy is replayed or USB is used. If the
  write is interrupted and `ota_0` is also bad (for example an OTA in the same window) the node is unrecoverable
  without USB. The ESP-IDF bootloader's fallback to another valid app slot when the selected one is invalid is
  expected behaviour but was not verified here (no IDF bootloader source in the package store); it must be proven on a
  bench board by cutting power during the write before Phase 2 is accepted. Headroom is thin: a new image must stay
  at or below 720896 B (the `v4.40a.10.05` build was 1536 B under the limit).
- Phase 1: refusing a manual OTA can strand an operator whose only way to update is OTA. Mitigation: the `force`
  override (D1) and a clear refusal text. Nodes on `v4.35u`..`v4.40a.10.04` Safeboots are refused unnecessarily (D3).
- First call of `fwNetSafebootVersion()` scans 704K once per boot (cached, `fw_update_net.cpp:2186`); the web handler
  and the console call it from their task. Latency not measured; the AU path already pays it.
- Wrong-chip image (Phase 2 only): an S3 image into a classic node. Guarded by the chip-id check above.
- `force` mirrors AU-16's "no override" stance only for DIO (settings are destroyed in every case there); for old
  Safeboot the node is recoverable, hence an override is defensible.

## 9. Decisions needed (operator)

| Id  | Question                                                 | Options and consequence                                                                                                                                                                                                                                                                                                                                                                                                    | Recommendation                      |
| --- | -------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------- |
| D1  | How hard is the manual refusal for a too-old Safeboot?   | (a) refuse, `--ota-update force` overrides: safe by default, one extra word for those who know. (b) warn and proceed (like `trying_anyway`): no protection, the 35232 case stays reachable. (c) refuse with no override: strongest, but strands a node whose owner has no other way.                                                                                                                                       | (a)                                 |
| D2  | Also gate `--update apply`?                              | (a) leave as is: install already needs a capable Safeboot, so the case is rare. (b) refuse below the minimum too: closes the corner, but the existing note says it is meant to try after a stale `nocap` flag.                                                                                                                                                                                                             | (a)                                 |
| D3  | Threshold: `AU_SAFEBOOT_MIN` (1) or a new, higher value? | (a) reuse 1: one number, matches the auto path; refuses version 0 including the images from `v4.35u` that already have the loop fix. (b) new `MANUAL_OTA_MIN` 2 or 3: refuses more nodes (version 1 has staged apply but not v3 identity), needs a second constant to keep in step.                                                                                                                                        | (a)                                 |
| D4  | Build Safeboot self-update (Phase 2)?                    | (a) no: one USB or web-flasher pass per node stays the rule; NIGHTLY-01 v4 costs one USB flash per opted-in node. (b) yes, as its own campaign: removes that step for nodes whose app is new, costs a download path under AU heap limits and a power-cut fault drill on three boards, and carries the brick risk of section 8. (c) ship the image inside `.autoupdate`: larger asset, less room in the `ota_0` stage tail. | (a) now; revisit with NIGHTLY-01 W5 |
| D5  | Wording of the refusal and the contract line             | Reuse the auto path's `safeboot_too_old` wording (recommended) or a new `ota-update` specific code. Reuse keeps one string for the web flasher hint and the tools.                                                                                                                                                                                                                                                         | reuse                               |

## 10. Evidence checklist

- [x] Every cited commit is an ancestor of HEAD and not reverted: `72b3259a`, `4d449147`, `a59b74d5`, `afaa2f40`
      (all `merge-base --is-ancestor` rc 0 in the problem file's check, `a59b74d5` re-checked here), `8b4d509b`,
      `ce05f38c`, `f4af55b0`, `07c49d5a`.
- [x] Every path:line in section 2 re-read at HEAD `067d29cd` (`command_functions.cpp:274, 278, 289, 295, 1680-1682,
6175-6177, 5944`; `fw_update_net.cpp:2186`; `esp32_main.cpp:3126-3134, 3210, 3272`; `safeboot_ver.h:26-33`;
      `ElegantOTA.cpp:193`; `safeboot-ota-contract.md:234-235`; `ota_state.h:101`).
- [x] Platforms checked per target (section 3): classic, S3 (partition tables, build envs, AU-16), C3 (absent), nRF52
      (`#ifdef ESP32` guards).
- [ ] Not verified: Safeboot version and firmware on DB0ND; whether the IDF bootloader falls back to `ota_0` when the
      factory image is corrupt; latency of the first version scan; nothing was built, flashed or run in this plan.

## 11. Backlog row (to be added by the orchestrator, table "Implement (firmware and tooling)")

| OTA-SAFEBOOT-01 | OTA / Safeboot | Manual OTA (--ota-update, web button) is not gated by the Safeboot version: it goes straight to auRebootToSafeboot() (command_functions.cpp:1680) while the auto path refuses below AU_SAFEBOOT_MIN; a node with an old Safeboot (v4.35o loop, no 180 s return) can be lost by hand (P-0209, DB0ND 2026-10-24). OTA cannot replace Safeboot (by design, contract); a new Safeboot needs the web flasher. ESP32 only; same code in upstream/dev. Plan [plan-ota-safeboot-gate-20261121.md](plan-ota-safeboot-gate-20261121.md). Problem file P-0209. | Open 2026-11-21: plan written, not implemented. | Operator decisions D1-D5 in the plan; then Phase 1 (gate, two native tests, bench on one classic and one S3 board). |
