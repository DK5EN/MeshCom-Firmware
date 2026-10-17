# T-Beam 1W: node stuck in Safeboot after Auto Update (AU-16)

**Status 2026-10-17:** open, cause unknown. Backlog row `AU-16` in [`BACKLOG.md`](BACKLOG.md) section 3.2.
Field node OE5HWN-17 (LilyGo T-Beam 1W, env `LilyGo_T-Beam-1W`, ESP32-S3 N16R8), dev channel, 10.16 -> 10.17.

## Symptom

- Web GUI: "Check" -> "new version available" -> "1 job running" -> the node is gone. No web page, no BLE,
  no display. A reset does not bring it back. No button press needed: after staging, the app hands over to
  Safeboot on its own (`src/esp32/esp32_main.cpp`, `FW_HANDOVER`).
- First occurrence 2026-10-16 (update to 10.16), recovered with the web flasher; afterwards the webserver
  setting was off. Reproduced 2026-10-17 (update to 10.17) with PuTTY attached.

Serial output of the reproduction (as pasted, possibly incomplete):

```
-----------------------------
OTA UDATE started
[SAFEBOOT];ver;2

NVS Flash Settings:
Callsign:
Wifi SSID: none
Webserver: 0
Gateway: 0
WIFI AP: 0

Starting Wifi AP
AP IP: 192.168.4.1
mDNS responder started
mDNS http service added
[SAFEBOOT];app;image;invalid;rc;8194
```

## What the node is doing

The node is alive, in Safeboot AP mode. Safeboot read no WiFi SSID, so it opened its own access point
`MeshCom-OTA` (no callsign read, so the default hostname) at 192.168.4.1. Safeboot has no BLE and no display.

| Log line                               | Meaning                                                                                                                                                         |
| -------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `[SAFEBOOT];ver;2`                     | Correct Safeboot (AU-capable, v2).                                                                                                                              |
| no `[SAFEBOOT];apply;...` line         | Safeboot found no staged record in NVS `fwstage`/`rec`, although the app showed "staged v4.40a.10.17" right before.                                             |
| `Callsign:` empty, `Wifi SSID: none`   | NVS namespace `Credentials` read as empty: these are the `init_flash()` defaults. The app had just shown OE5HWN-17.                                             |
| missing `[INIT]...init_flash`          | Safeboot always prints it before "NVS Flash Settings". The paste is incomplete or not the first Safeboot boot (native USB: PuTTY loses the port at each reset). |
| `[SAFEBOOT];app;image;invalid;rc;8194` | 8194 = 0x2002 = `ESP_ERR_IMAGE_INVALID`: `esp_image_verify()` rejects ota_0. There is no bootable app.                                                          |

**Why it does not reboot:** by design. The 180 s fallback to the app is armed only while ota_0 is valid
(`src/safeboot/ota_state.h`, `fallback_in_ms` -1 while `app_valid` is false). With an invalid app a reboot
would only loop, so Safeboot waits for an upload. The otadata points to Safeboot, so a reset lands there again.

## Ruled out

- **Download not reachable / corrupt.** "staged" is only written after the SHA-256 matched the release
  digest and a read-back CRC over the staged bytes in flash matched (`src/esp32/fw_update_net.cpp`,
  `stageFinish()`). Safeboot re-checks that CRC before erasing anything; a bad file prints
  `[SAFEBOOT];apply;fail;crc`.
- **Release asset.** `T-Beam-1W.autoupdate` (1172066 B) inflates byte-identical to `T-Beam-1W.bin`
  (1854784 B), valid ESP32-S3 image (chip ID 9).
- **Shared S3 parts.** `bootloader-s3.bin`, `safeboot-s3.bin`, `T-Beam-1W.bin` and `t_deck_plus.bin` have the
  same flash mode (DIO), frequency (80 MHz) and chip ID. DK5EN-14 (T-Deck Plus, also N16R8) passed the same
  update on 2026-10-17.

## Hypotheses

1. **Safeboot misreads the flash on this board.** If the paste is the first Safeboot boot, no apply ran, yet
   both NVS and ota_0 look wrong. One cause for all three symptoms.
2. **NVS wiped at Safeboot start.** Safeboot is built on the Tasmota platform (IDF 5), the app on
   arduino-esp32 2.0.14 (IDF 4.4). The Arduino core erases the NVS partition when `nvs_flash_init()` returns
   `ESP_ERR_NVS_NO_FREE_PAGES` or `ESP_ERR_NVS_NEW_VERSION_FOUND` (e.g. a full 20 KB NVS). Explains the empty
   settings, the missing record and "webserver off" after the first recovery; does not explain the invalid
   ota_0 on its own.
3. **An uncaptured earlier Safeboot boot ran the apply and failed.** Would explain the invalid ota_0 and the
   dropped record, not the empty `Credentials`.

The 1W had never run Safeboot before: the web flasher writes otadata pointing at ota_0, so the app boots
directly. No 1W is in the bench fleet.

## Immediate actions for the operator

1. **Auto Update off** on the 1W (mode `off`, not just channel `prod`): every check stages and hands over
   again.
2. **Before recovering, dump the flash** (download mode: hold BOOT during reset):
   ```
   esptool -p COMx read_flash 0x9000 0x5000 nvs.bin
   esptool -p COMx read_flash 0xC0000 0x340000 ota0.bin
   ```
   Send both files.
3. **Recover:** join WiFi `MeshCom-OTA`, open http://192.168.4.1, upload `T-Beam-1W.bin`. Or use the web
   flasher and answer **no** to erase.
4. **Report:** did the callsign survive the recovery? Was erase chosen in the web flasher on 2026-10-16?

## Reading the dump

| Finding                                   | Verdict                                        |
| ----------------------------------------- | ---------------------------------------------- |
| `nvs.bin` all `0xFF`                      | NVS wiped (hypothesis 2)                       |
| `nvs.bin` holds `Credentials` / `fwstage` | Safeboot cannot read intact NVS (hypothesis 1) |
| ota0 head = intact 10.16 image            | Safeboot misreads flash, no apply ran (1)      |
| ota0 head erased or partly 10.17          | An apply ran (3); compare with `T-Beam-1W.bin` |
| ota0 tail holds the 10.17 zlib stream     | Staging was intact (`78 da` at `rec.off`)      |

## Debug plan (fork-dev)

1. **Safeboot heartbeat.** Every 5 s, repeat the verdict on serial, e.g.
   `[SAFEBOOT];alive;up;42;app;invalid;rc;8194;apply;none;cfg;empty;ap;MeshCom-OTA;192.168.4.1`.
   A late PuTTY reconnect (native USB) then still shows the cause. Cheap; stays in the regular Safeboot.
2. **Diagnostic Safeboot for the 1W.** At boot additionally print the `nvs_flash_init()` return code,
   `nvs_get_stats()` (used/free entries), the partition table as Safeboot sees it, and the first 32 bytes
   of ota_0. Operator flashes it once with esptool at 0x10000, then runs "Check" again.
3. **Bench check of hypothesis 2** on DK5EN-14 (T-Deck Plus, same chip family): fill NVS to near full, stage,
   hand over, watch whether Safeboot wipes it.
4. **Capture the first Safeboot boot** of the handover: the heartbeat (1) plus a terminal that reconnects
   automatically.
