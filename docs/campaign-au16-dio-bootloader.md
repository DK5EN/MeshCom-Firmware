# Campaign AU-16: DIO bootloader breaks the S3 Safeboot (2026-10-19)

Resume point for this campaign. Release target: v4.40a.10.20 (old tag on `da59c5b8` deleted local + origin
2026-10-19 with operator OK; `/release-firmware` re-tags later).

## Root cause (bench-proven 2026-10-19, DK5EN-14)

- `boards/esp32-s3-wroom-1-n16r8.json` (only `LilyGo_T-Beam-1W`) had `"flash_mode": "dio"`, so the 1W shipped
  `bootloader_dio_80m` (14032 B). All 16 other S3 release boards ship the QIO bootloader (15104 B).
- A/B on DK5EN-14, release `safeboot-s3.bin` 10.19, only the bootloader at 0x0 swapped:
  - QIO: Safeboot on the LAN after 7 s, `app_valid:true`, back in the app after 198 s.
  - DIO (the 1W `bootloader.bin` of 10.19): `Callsign:` empty, `Wifi SSID: none`, AP 192.168.4.1,
    `[SAFEBOOT];app;image;invalid;rc;8194`, no fallback. Identical to the OE5HWN-17 field log.
- Flash stays intact (`verify_flash` ok on ota_0 and Safeboot). Safeboot misreads it: NVS shows no valid page,
  Safeboot writes a fresh seq-0 page with empty `Credentials`, and the app then boots on defaults (XX0XXX).
- The IDF 4.4 app runs fine on the DIO bootloader; only the IDF 5 Safeboot is affected.
- The partition table is not involved (1W table byte-identical to Heltec V3, where AU passes).

## Operator decisions

- Guard on every ESP32-S3 board (runtime detection), not only the 1W. Classic ESP32 and nRF52 unaffected.
- No override (`force`): Safeboot under DIO destroys settings in every case.
- Web GUI: AU button disabled, notice that Safeboot/bootloader must be renewed with the Webflasher.
- Board json to `qio` ships in v4.40a.10.20; release notes ask 1W owners for a first Webflasher run as field test.

## Waves

| Wave  | Owner                     | Files                                                                                                                                                              | State |
| ----- | ------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ----- |
| -     | orchestrator              | `boards/esp32-s3-wroom-1-n16r8.json` (`flash_mode` qio)                                                                                                            | done  |
| W1-A  | implementer Sonnet medium | `src/esp32/fw_update_net.{h,cpp}`, `src/command_functions.cpp`, `src/esp32/esp32_main.cpp`, `src/esp32/rm_au_esp32.cpp`, `src/fw_update.h`, `test/test_fw_update/` | done  |
| W1-B  | implementer Sonnet medium | `src/web_functions/web_functions.cpp`, `src/web_functions/au_ui_state.h`, `test/test_au_ui_state/`, `tools/webgui_au_sbwhy_test.js` (new)                          | done  |
| Gate  | orchestrator              | regression stage 1,2; builds 1W/t_deck_plus/Heltec V3/ttgo_tbeam/RAK; 1W bootloader = 15104 B qio                                                                  | done  |
| Bench | orchestrator              | DK5EN-14 QIO vs DIO with the guard build                                                                                                                           | done  |
| W2    | implementer Sonnet medium | BACKLOG AU-16, troubleshooting + md5 issue docs, RESUME, defect catalogue                                                                                          | open  |

## Detection (to prove on the bench)

`SPI_MEM_CTRL_REG(0)` (cache SPI0) FREAD_QIO/FREAD_QUAD bits as set by the 2nd-stage bootloader; the app does
not reconfigure SPI0. `--update status` prints the raw register value for the bench proof. Fallback if the
register does not discriminate: classify the bootloader image at flash 0x0.

## W1 result (2026-10-19)

- Gate: `tools/regression.sh --stage 1,2` PASS (76 Unity envs, 2723 cases; `webgui_au_test.js` 108 checks,
  `webgui_au_sbwhy_test.js` 17 checks, wired into stage 2). Builds LilyGo_T-Beam-1W, t_deck_plus,
  heltec_wifi_lora_32_V3, ttgo_tbeam, wiscore_rak4631 ok. The 1W `bootloader.bin` is now 15104 B,
  byte-identical to `bootloader_qio_80m`. `bootloader_dio` strings present in the S3 images.
- New regression test `test_dio_bootloader_blocks_safeboot` (test_command_setters): fails on the old
  `command_functions.cpp`, passes on the new one.
- Advisor (Opus): logic approved; 5 rework items (test gaps, setup card text, early return in
  `auHandoverRecord`, stage-2 wiring) applied by the orchestrator, gate re-run green.
- Bench DK5EN-14 (guard build, release Safeboot 10.19):
  - QIO bootloader: `--update status` `safeboot;ok ... boot;qio;spi0;0x012c2008` (bit 24 FREAD_QIO);
    `--ota-update` -> Safeboot on the LAN after 7 s, back in the app after 198 s.
  - DIO bootloader (1W 10.19): `safeboot;dio ... boot;dio;spi0;0x00ac2008` (bit 23 FREAD_DIO);
    `--ota-update` -> `[ERR];safeboot;bootloader_dio;renew with the web flasher`, no reboot;
    `--update install` refused; `/austatus` `sbok:0`, `sbwhy:"dio"`; Info row shows the DIO notice with
    "Check now" disabled (Chrome); Setup card shows the locked text. NVS untouched (identity guard ok).
  - Restored: QIO bootloader, DK5EN-14 runs the guard build.
- Behaviour change: in web state S the button is now disabled for an old Safeboot too (was clickable).

## Open

- W2 docs (BACKLOG AU-16, troubleshooting, md5 issue, RESUME, defect catalogue, test-suite-map).
- Field: first Webflasher run on a real 1W (QIO bootloader boots?), then release notes for 10.20.
