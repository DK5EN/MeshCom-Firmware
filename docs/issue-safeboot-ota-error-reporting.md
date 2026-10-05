# Safeboot OTA: wrong failure reason and stale "image incomplete" box

- **Status:** open, to fix later
- **Firmware:** V4.40a (release v4.40a.10.06), Safeboot web OTA page
- **Reported:** 2026-10-05, field screenshots from a T-Beam 1W (DD7MH-66) and a T-Beam Supreme
- **Related:** the root cause of the T-Beam 1W failure is still under investigation (suspect: Safeboot
  MTU 1280 since NMTU-01 `e6d28658`). The two defects below are independent of it.

## Symptoms

1. **T-Beam 1W:** the upload of `T-Beam-1W.bin` (1724 kB, release asset is a valid ESP32-S3 app
   image) ends with "The uploaded image failed the checksum check. (md5_mismatch)".
2. **T-Beam Supreme:** the upload of `ttgo_tbeam_supreme.bin` succeeds ("Update installed, the node
   is rebooting into the app"), but the red box "The firmware image on this node is incomplete ...
   the node stays in the bootloader until a complete firmware upload succeeds" stays visible next
   to the success message.

## Defect 1: every `Update.end()` failure is reported as `md5_mismatch`

`src/safeboot/ElegantOTA.cpp:380-389` (async upload handler, final frame):

```cpp
if (!Update.end(true)) {
    ...
    Serial.println(_update_error_str.c_str());
    g_ota.onVerified(millis(), false, safeboot::OtaSession::Reason::Md5Mismatch);
}
```

`UpdateClass::end(true)` (arduino-esp32 `Updater.cpp`) fails for several distinct reasons:

| Updater error           | Cause                                                               |
| ----------------------- | ------------------------------------------------------------------- |
| `UPDATE_ERROR_MD5`      | bytes written differ from the client-side MD5 (lost/corrupt chunks) |
| `UPDATE_ERROR_READ`     | `_enablePartition()` / `_partitionIsBootable()` failed (magic byte) |
| `UPDATE_ERROR_ACTIVATE` | `esp_ota_set_boot_partition()` rejected the image (image verify)    |
| earlier error           | `hasError()` already set by a previous step, or `_size == 0`        |

All of them reach the browser as `md5_mismatch`. The real text (`Update.printError()`) goes only to
the serial console, and the completion handler sends only `OtaSession::reasonName(reason)`. A field
report therefore cannot distinguish a transfer problem from an image problem.

**Fix:**

- Map `Update.getError()` to distinct `OtaSession::Reason` values (at least `md5_mismatch`,
  `activate_failed`, `not_bootable`, `update_error`), or carry the Updater error code alongside.
- Add the matching texts to the `reasons` table in `src/safeboot/ota.html` (~line 440).
- Optionally include `_update_error_str` in the 400 response body and in `/ota/state`.
- Extend `test/test_safeboot_state` for the new reasons and update `docs/safeboot-ota-contract.md`.

## Defect 2: `app_valid` is not re-checked after a successful upload

- The red box `#appInvalidNotice` (`src/safeboot/ota.html:334`) is driven by `app_valid` from
  `/ota/state` (`ota.html:557-558`).
- `app_valid` is refreshed only by `refreshAppValid()` at boot and after a drained Abort
  (`src/safeboot/main.cpp:889`, `:1028`).
- `ota_state.h:278-281` states that after `onVerified(ok=true)` "the caller re-checks the image and
  calls setAppValid(true) afterwards". No caller does that.
- So a node that starts the upload with an invalid ota_0 (after an earlier aborted attempt) keeps
  `app_valid=false` through the successful upload, and the page shows "incomplete" next to "Update
  installed".

**Fix:** after a successful `onVerified()` (or when the SwitchPartition action is drained in
`main.cpp`), call `refreshAppValid()` (or `setAppValid(true)` after `checkAppImageValid()`), and
hide `#appInvalidNotice` on the client once the state reaches Done. Add a regression test in
`test/test_safeboot_state` (invalid app → successful upload → `app_valid` true).

## Notes

- The red box on the Supreme indicates its ota_0 was already invalid before the successful upload,
  so that node most likely had a failed attempt first too.
- Safeboot is only replaced by a full USB / web-flasher flash, never by an app OTA. Fixes reach
  field nodes only through the web flasher.
- Both fixes need `safeboot.bin` and `safeboot-s3.bin` rebuilt (`esp32-safeboot`,
  `esp32-S3-safeboot`).
