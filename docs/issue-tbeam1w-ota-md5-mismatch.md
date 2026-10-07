# T-Beam 1W: web OTA to v4.40a.10.06 fails with `md5_mismatch`

- **Status:** open, not reproduced on the bench
- **Reported:** 2026-10-05 by DD7MH (node DD7MH-66, LilyGo T-Beam 1W, ESP32-S3, WiFi client)
- **Firmware path in the field:** v4.40a.10.02 (installed by USB / web flasher) → web OTA with the
  release asset `T-Beam-1W.bin` from v4.40a.10.06
- **Related:** `issue-safeboot-ota-error-reporting.md` (Desktop): the reported reason is not
  trustworthy, and the "image incomplete" box stays after a successful upload

## Symptom

- **T-Beam 1W:** in the Safeboot update page, the upload of `T-Beam-1W.bin` (1724 kB) ends with
  "The uploaded image failed the checksum check. (md5_mismatch)". The node stays in Safeboot with
  an invalid app slot.
- **T-Beam Supreme** (same reporter environment, Android browser): the upload of
  `ttgo_tbeam_supreme.bin` (1738 kB) succeeded. The red "image incomplete" box stayed visible, so
  this node most likely had a failed attempt before as well.

## What is established

1. **The release assets are fine.** `T-Beam-1W.bin` is 1,765,696 bytes, MD5
   `517a98f71d18307668b7dc13fd0e40eb`; `esptool image_info` reads a valid ESP32-S3 app image (chip
   ID 9, 16 MB header). The Supreme asset is valid too. Both fit the 3324 KB ota_0 slot of
   `partitions-4MB-safeboot.csv`.
2. **The full file was sent.** The page showed 1724 kB, the exact asset size.
3. **The label `md5_mismatch` proves nothing on its own.** `ElegantOTA.cpp:380-389` maps every
   `Update.end(true)` failure to `Md5Mismatch`, including `UPDATE_ERROR_READ` (not bootable) and
   `UPDATE_ERROR_ACTIVATE` (image rejected by `esp_ota_set_boot_partition()`). The real reason is only
   on the serial console.
4. **MTU 1280 is not the cause.** The field path uploads through the **10.02** Safeboot, because an
   app OTA never replaces the Safeboot partition. The 10.02 `safeboot.bin` contains no MTU code
   (`strings | grep 'NET];mtu'` = 0), so that upload runs at MTU 1500 whatever is stored.

## Bench tests 2026-10-05 (DK5EN-92, T-Beam v1.2, classic ESP32, WiFi RSSI -52)

| #   | Safeboot               | MTU / MSS   | Client                       | Image                  | Result   |
| --- | ---------------------- | ----------- | ---------------------------- | ---------------------- | -------- |
| 1   | current (NMTU-01)      | 1280 / 1240 | `tools/webflash.py` (Mac)    | 10.06 `ttgo_tbeam.bin` | 10/10 ok |
| 2   | 10.02 (USB full flash) | 1500        | `tools/webflash.py` (Mac)    | 10.06 `ttgo_tbeam.bin` | ok       |
| 3   | 10.02 (USB full flash) | 1500        | Chrome, Safeboot update page | 10.06 `ttgo_tbeam.bin` | ok       |

- Every run: serial log `MD5 from client: a76801bd...` and `[SAFEBOOT];ota;verify;result;ok`, node
  back on the Oct 5 build.
- MSS 1240 in test 1 was measured from the Mac (`TCP_MAXSEG`) against the running Safeboot.
- USB flashes kept NVS (no erase), as a field node would.

## Open hypotheses

1. **ESP32-S3 specific.** Both field nodes are S3, the bench node is a classic ESP32.
   `safeboot-s3.bin` is a different build (`esp32-S3-safeboot`, Heltec V3 board definition, `-Oz`).
   Note: the S3 Safeboot is built for `heltec_wifi_lora_32_V3`; check whether the 1W (16 MB flash,
   8 MB PSRAM, `esp32-s3-wroom-1-n16r8`) and the Supreme run it without side effects.
2. **Network path.** Weak or lossy WiFi at the reporter, Windows browser. Lost data cannot corrupt
   TCP, but a stall or reconnect during the upload could hit the generation guard in
   `ElegantOTA.cpp` (chunks of a superseded session are dropped silently), which would end as a
   real MD5 mismatch.
3. **Image rejected, not MD5.** `UPDATE_ERROR_ACTIVATE` or `UPDATE_ERROR_READ`, shown as
   `md5_mismatch`. Only the serial log can tell.

## Needed from the field

- Serial log of the 1W during a failed upload (the `Update.printError()` line: `MD5 Check Failed`
  vs `Could Not Activate The Firmware` vs `Flash Read Failed`).
- Does a second upload attempt succeed? Did the Supreme fail first too?
- Which version and how was the node installed before (web flasher 10.02?), and the WiFi RSSI.

## Follow-up: Heltec V3 tests (ESP32-S3) for later

Node DK5EN-1 (Heltec V3, USB serial `0001`, IP 192.168.68.62). Note: on 2026-10-05 it was not
reachable over WiFi; check its WiFi setting first. Opening the serial port reboots it.

1. **Field path, S3.** USB full flash of the v4.40a.10.02 S3 set (`bootloader-s3.bin` at 0x0,
   `otadata.bin` 0xE000, `partitions.bin` 0x8000, `safeboot-s3.bin` 0x10000,
   `heltec_wifi_lora_32_V3.bin` 0xC0000, layout per `upload_command` in `platformio.ini:1342`), no
   erase. Then OTA the 10.06 Heltec image:
   - 5× `tools/webflash.py 192.168.68.62 --env heltec_wifi_lora_32_V3 --bin <10.06 image>`
   - 3× Chrome through the Safeboot update page
   - serial capture running, grep `MD5 from client|verify;result|Error`
2. **Current S3 Safeboot at MTU 1280.** Write the current `safeboot-s3.bin` to 0x10000 by USB,
   `--mtu 1280`, confirm MSS 1240 against the running Safeboot, then 10× webflash and 3× Chrome.
3. **Same at MTU 1500** only if step 2 shows any failure.
4. **Weak link.** Repeat step 1 or 2 with the node at RSSI around -80 dBm (distance or shielding),
   to cover hypothesis 2.
5. If a failure appears: keep the serial log, note the Updater error text, and check `/ota/state`
   (`reason`, `generation`, `received`, `total`) right after the failure.

## Bench state after the tests

- DK5EN-92 runs the 10.06 app with `--mtu 1280` stored, but its Safeboot is the **10.02** one
  again (from the USB full flash). Restore the current `safeboot.bin` at 0x10000 by USB.
- DK5EN-92's USB connection dropped twice during the session (CH9102 `573C000584` disappeared
  from the USB tree); check the cable.
