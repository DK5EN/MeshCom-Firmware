# Plan: nRF52 `--dumpsettings` prints passwords and the BLE PIN in clear text (P-0380)

Date: 2026-11-21. Branch `fork-dev` at `067d29cd`. Problem file P-0380 (RCA pinned at `96aaa67c72`, HEAD is 19 commits
later). Status: plan only, nothing implemented. Sibling: P-0141 (the report that led here).

## 1. Symptom

On nRF52 boards the serial command `--dumpsettings` prints the stored settings file (`key=value` lines) unmasked. The
file holds the node console password, the WLAN password, the web GUI password, the MeshCom server password and the BLE
pairing code. Whoever can open the USB serial port of the node reads them. `--info` masks the same passwords with
`maskSecret()`; the dump does not. Reported by a user complaint (uid 140, 138: "a settings command also outputs the
passwords"); the code reading shows the defect independently of what the reporter ran. Not captured on hardware yet; the
format is confirmed by the bench captures in `docs/bench/w3-baseline/w3-store-fixed-20260912.txt:40-110`, where the
operator replaced the values by `<masked>` by hand before committing.

## 2. Root cause (re-verified at HEAD)

| What                                                                       | Where at HEAD                                                                                                                  |
| -------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------ |
| `settingsStoreDump()` reads the file in 64-byte chunks and prints them raw | `src/nrf52/settings_store_nrf52.cpp:411-446`, print at `:438`                                                                  |
| The file is written from the schema table                                  | `src/settings_schema.cpp:200` (`CFG_FIELD_LIST(X) SETTINGS_PERSIST_ONLY_LIST(X)`)                                              |
| Secret rows in that table                                                  | `src/config_json.h:269` (`node_pwd`), `:316` (`node_passwd`), `:318` (`bt_code`), `:324` (`node_webpwd`), `:326` (`node_lpwd`) |
| Command branch, `#if defined(NRF52_SERIES)`                                | `src/command_functions.cpp:1611-1630` (the pin cited `:1606`; moved by 12 lines)                                               |
| Masking helper that the dump does not use                                  | `src/mask_secret.h:25` (`maskSecret()`)                                                                                        |

Commits since the pin: `git log --oneline 96aaa67c72..HEAD -- src/nrf52/settings_store_nrf52.cpp src/command_functions.cpp src/config_json.h src/settings_schema.cpp src/mask_secret.h src/settings_store.h src/settings_store.cpp`
lists `8b4d509b`, `ce05f38c`, `f4af55b0`, all only in `command_functions.cpp` (ethdrop hook, AppLink, CMD-DASH-01), none in
the dump function or the schema; `git diff 96aaa67c72 HEAD` on the other six files is empty. The defect is unchanged.
No commit since the pin is a revert (`git log --grep=Revert 96aaa67c72..HEAD` is empty). Origin of the code: squash
`6966c527` (in `fork-dev`), already in upstream.

The ticket lists four secrets. There are five on nRF52: the schema key `node_pwd` is the struct member `node_opwd`, the
MeshCom server password (`char[40]`, `meshcom_settings.h:80`, legacy: default `"none"` on ESP32, zero on nRF52). It is
persisted (`config_json.h:269`) and therefore dumped.

## 3. Platforms and paths, checked per platform

Secret fields in the persisted schema (key in the file, struct member, meaning):

| Key              | Member            | Meaning                                                                              | On nRF52      | On ESP32 |
| ---------------- | ----------------- | ------------------------------------------------------------------------------------ | ------------- | -------- |
| `node_passwd`    | `node_passwd`     | console / RM / KISS / AppLink password                                               | yes           | yes      |
| `node_webpwd`    | `node_webpwd`     | web GUI password                                                                     | yes           | yes      |
| `node_lpwd`      | `node_pwd`        | WLAN password                                                                        | yes           | yes      |
| `node_pwd`       | `node_opwd`       | MeshCom server password (legacy)                                                     | yes           | yes      |
| `bt_code`        | `bt_code`         | BLE pairing code, 0 = no PIN                                                         | yes           | yes      |
| `node_aprsispas` | `node_aprsispass` | APRS-IS passcode (`config_json`-less, `settings_schema.h:147`, under `#ifdef ESP32`) | not persisted | yes      |

Not secrets: `node_extern` (ext. UDP target IP), `node_gwlab`, `node_gwsrv`, `node_gwrc` (a call sign). There is no ext UDP
key or token field in the struct (`git grep` on `meshcom_settings.h` for pwd, pass, key, token finds only the rows above).

Output paths:

| Path                                                 | nRF52                                                                                                                                                                                                                                                                                          | ESP32                                                                                                                                                                                                          |
| ---------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Serial `--dumpsettings`                              | **all five in clear** (this defect). Output goes to `Serial.printf` only; no net console exists on nRF52 (`git grep net_console src/nrf52` is empty); BLE gets only `ok` or `failed` (`command_functions.cpp:1624`).                                                                           | Command does not exist (`#if defined(NRF52_SERIES)`, `:1611`). ESP32 persists per key in NVS (`esp32_flash.cpp`, schema backend), has no file to dump and no code that prints schema values. **Not affected.** |
| `--info` (serial and ESP32 net console, `printfdeb`) | `node_passwd` (`:8271`), `node_webpwd` (`:8485`), `node_pwd` WLAN (`:8515`) masked; `node_opwd` not printed; **`bt_code` printed in clear** (`:8335`, `...BTCODE %06i`)                                                                                                                        | same code, same result                                                                                                                                                                                         |
| `--info` over BLE (`ble` branch)                     | `idoc["BPIN"] = bt_code` (`:8207`) to the paired phone                                                                                                                                                                                                                                         | same                                                                                                                                                                                                           |
| BLE settings image (v1 characteristic)               | none: the 0xF0A1 settings characteristic was removed (guarded by `test_ble_set_01_dead_settings_characteristic_is_gone`, `test/test_nrf52_settings_paths/test_nrf52_settings_paths.cpp:1046`); `bleSettingsV1MaskSecrets()` (`src/nrf52/ble_settings_stage.h:84`) has no caller left in `src/` | no settings characteristic                                                                                                                                                                                     |
| AppLink (LAN phone frames, `ce05f38c`)               | RAK4631 + RAK13800 only: `--dumpsettings` over AppLink answers only `ok`/`failed`, like BLE, so not affected. `--info` BPIN (`:8207`) now also reaches the LAN AppLink client: out of scope                                                                                                    | same: `ok`/`failed` only; BPIN reaches the AppLink client, out of scope                                                                                                                                        |
| BLE `SN1` JSON                                       | `WSPWD` = web password in clear (`command_functions.cpp:8886`), by design for the app                                                                                                                                                                                                          | same                                                                                                                                                                                                           |
| `GET /config.json` and restore                       | all in clear by operator decision 2026-08-30 (`config_json.h:40-43`)                                                                                                                                                                                                                           | same                                                                                                                                                                                                           |
| Web setup page                                       | none (no web server on nRF52)                                                                                                                                                                                                                                                                  | WLAN password in the form field (`src/web_functions/web_functions.cpp:3532`, `src/web_functions/web_setup.cpp:182`), behind the web login if one is set                                                        |
| Pairing prompts                                      | `loop_functions.cpp:1897` (display `BLE-C`)                                                                                                                                                                                                                                                    | plus `esp32_main.cpp:463` (`Server PassKeyRequest`), by design                                                                                                                                                 |

Conclusion: the defect is nRF52 only. ESP32 has no dump command and its `--info` already matches nRF52 (shared code). The
shared masking helper already exists (`maskSecret()`); no new cross-platform helper is needed. One related finding:
`bt_code` in `--info` is printed on purpose (CQ-01, `docs/archive/backlog-journal-20260818-20260929.md:2934`, closed by the
operator 2026-09-03: the PIN must be readable on the console to pair). That decision covers `--info`; it says nothing about
the dump, and the dump is not needed for pairing. This plan leaves `--info` alone.

## 4. Fix design (minimal)

Mask the secret rows while `settingsStoreDump()` streams the file. Everything else (begin and end markers, byte count,
filesystem inventory, non-secret lines) stays byte-identical, so the bench use of the command (boot-2 settings loss
hunt) is unchanged.

1. New header-only helper `src/settings_dump_mask.h` (next to `src/mask_secret.h`, includes it; no platform includes, so
   any native env can compile it):
   - `settingsDumpKeyIsSecret(const char *key, size_t keyLen)`: compares against one table with the six schema keys of
     section 3 (the sixth, `node_aprsispas`, is not persisted on nRF52 today but costs nothing and keeps the list equal
     to the schema).
   - `settingsDumpMaskLine(const char *line, size_t len, char *out, size_t outCap)`: one `key=value` line without the
     newline in, masked line out. Secret string key: `key=***`, or `key=` when `maskSecret()` says unset (empty or leading
     space; `node_passwd` is space padded). `bt_code`: `0` stays `0` (no PIN), anything else `***` (`maskSecret("0")` would
     wrongly say `***`; the BLE image uses the same rule, `ble_settings_stage.h:77`). Non-secret line: copied unchanged.
     Keeps the key, so the dump still shows whether a value is set, which is what the diagnostic needs.
2. `settingsStoreDump()` (`settings_store_nrf52.cpp:411`) assembles lines instead of printing chunks: keep the 64-byte read
   chunk, collect into a 192-byte line buffer, on `\n` (or EOF) print the output of `settingsDumpMaskLine()` plus `\n`.
   192 bytes hold the longest secret line (`node_lpwd`: 64 value bytes, at most 128 escaped, plus key). A line that fills the
   buffer without a newline is decided on its key prefix: secret key -> print the masked form once and discard the rest of
   the line (fail closed); other key -> flush the buffer and stream the rest raw (a 150-character `node_eqns` value is
   legal). Keep the `Serial.printf("%.*s", ...)` primitive (the native test double stubs only `printf`, comment at `:422-425`).
3. Text only: help line `command_functions.cpp:1713` -> `dump settings store (secrets masked)`, the comment block at
   `:1612`, `settings_store_nrf52.h:119`, and the row in `docs/rm-gui/unification/inventory-console-part1.md:107`.

Rejected: a `secret` flag in `settings_store::FieldDescriptor` (single source of truth, but it touches the codec header
that every settings test brace-initialises, and `settings_store.h` already differs from upstream, so the upstream PR grows).
The drift risk of a second key list is covered by the test in section 5. Rejected: making `--dumpsettings` print nothing
secret-bearing by whitelisting keys (a new field would silently vanish from the diagnostic). Rejected: reusing
`extRegKeyIsSecret()` (`src/ext_reg.h:127`, a denylist by key-name pattern): it misses `node_aprsispas` (contains `PAS`, not
`PASS`), and including `ext_reg.h` drags its dependencies into a header-only helper. Its pattern can serve as a second
opinion in the drift test (`test_dump_secret_list_covers_schema_names`).

## 5. Test that fails before and passes after

Env `native_nrf52_settings_paths` (`platformio.ini:620`, builds the real `settings_store_nrf52.cpp` and the real schema),
file `test/test_nrf52_settings_paths/test_nrf52_settings_paths.cpp`. The `Serial` double (`test/support/Arduino.h:243`)
collects `printf` output in `Serial.captured()`, and the fake filesystem (`g_fake_fs.seed()`) feeds the store, so the real
`settingsStoreDump()` is host-testable as it is. No seam to extract.

New cases (names are proposals):

- `test_dump_masks_the_five_secrets`: build a state with `node_passwd="pw-console-91"` (space padded to 14 like
  production), `node_webpwd="pw-web-7712"`, `node_pwd="wlan-psk-4471"` (key `node_lpwd`), `node_opwd="srv-pw-3308"` (key
  `node_pwd`), `bt_code=731942`; `make_keyed_store(state)`; seed `/MeshCom-Settings-Store`; `Serial.clear()`;
  `settingsStoreDump()`. Assert: none of the five values appears in `Serial.captured()`; `node_passwd=***`,
  `node_webpwd=***`, `node_lpwd=***`, `node_pwd=***`, `bt_code=***` do. **Fails today** at the first assertion: the dump
  prints the file raw, so `pw-console-91` is in the output. The failure comes from section 2, not from the harness.
  (the same state dumped after the fix passes; run it before the fix and record the failing assertion).
- `test_dump_keeps_unset_and_non_secret_lines`: empty `node_webpwd` prints `node_webpwd=`; `bt_code=0` prints `bt_code=0`;
  `node_call=DK5EN-90` and a 150-character `node_eqns` value arrive unchanged; the `[SETST];dump;begin;bytes=N` and
  `;end;read_error=0` markers are still there with N equal to the file size. Passes before and after (guards the
  streaming rewrite).
- `test_dump_masks_across_chunk_boundaries`: the full store is about 1.5 KB, so lines already straddle the 64-byte chunks in
  every case above. Add one case that stores secrets at lengths 1, 13, 14 and 63 (so a key or value ends exactly on a
  chunk edge) and one that arms `g_fake_fs.force_short_read` (a one-shot knob, `Adafruit_LittleFS.h:69`) for the first read.
  The secrets are still absent. Fails before, passes after.
- `test_dump_secret_list_covers_schema_names`: walk `settings_schema::fields()`; every key that contains `pwd`, `pas` or
  `code` must be `settingsDumpKeyIsSecret()` or be in an explicit allowlist in the test; every key of the helper table
  must exist in the schema (the ESP32-only key is skipped by `#ifdef ESP32` in this env, so the table check allows it).
  Fails when someone adds a password field and forgets the list.

Unit cases for the pure helper go into `test/test_mask_secret/test_mask_secret.cpp` (env `native`, filter list at
`platformio.ini:226`): secret string, unset (empty, spaces), `bt_code` 0 and non-zero, an over-long line, a non-secret
line, `outCap` too small (returns 0, never overruns). Run order for the writer: write the tests first, run
`pio test -e native_nrf52_settings_paths -f test_nrf52_settings_paths` and keep the failing output for the report, then
fix, rerun, then `pio test -e native -f test_mask_secret`. Update `docs/test-suite-map.md` counts (the map still says 26, the suite already has 32 cases; 32 -> 36 for
`test_nrf52_settings_paths`, 6 -> new total for `test_mask_secret`). Gate: `tools/regression.sh --stage 1,2` (never bare `pio test`).
Also build one nRF52 board env (`rak4631`) to prove the firmware compiles; only one `pio` process at a time.

## 6. Bench step

Board: DK5EN-90 (RAK4631, nRF52, USB serial `230D6EBB3266D20E`, `tools/bench/fleet.json`), INSTRUMENT image or normal build.
Not DK5EN-98 (production gateway). The dump is a serial read; the node transmits nothing, so the TEST-group and TX-power
rules are not touched. Steps:

1. `tools/bench/identity_guard.py`; `lsof` on the port; the RAK4631 needs DTR to talk.
2. Set throwaway values: `--btcode 731942`, `--passwd bench-pw-1`, `--webpwd bench-web-1`. Leave the WLAN settings alone
   (the node is a gateway on the real server); its WLAN password line is checked as is.
3. `--dumpsettings`. Expect `node_passwd=***`, `node_webpwd=***`, `node_lpwd=***`, `node_pwd=***` (or empty when unset),
   `bt_code=***`, no throwaway value anywhere in the capture, and the usual `[SETST];dump;begin;bytes=` ... `;end;read_error=0`
   plus the `[SETST];fs;dump;...` inventory. Run `grep -c 731942` on the capture: 0.
4. Restore: `--btcode 0`, `--passwd none`, `--webpwd none`. Capture goes to `tools/bench/runs/harness/` (summary only, raw
   `.log` stays gitignored). A before capture on a build without the fix is optional; the native test already proves it.

## 7. Upstream impact

`settingsStoreDump()` and its `--dumpsettings` branch exist in `upstream/dev` (`5698913a`) byte-identical to the fork:
`git diff upstream/dev HEAD -- src/nrf52/settings_store_nrf52.cpp` touches only 5 lines elsewhere in the file, none in the
dump (`upstream/dev:src/nrf52/settings_store_nrf52.cpp:410`, `src/command_functions.cpp:1434-1446`). `src/mask_secret.h` is
identical. The upstream schema rows are at the same `config_json.h` lines. So the defect is upstream too: **upstream PR**,
cut from `upstream/dev` per `/submit-pr`, after it landed on `fork-dev`. The PR carries the helper header and the dump
change (about 60 lines, `src/` only). The tests do not travel as they are: `upstream/dev` has no `test_nrf52_settings_paths`,
no `test_mask_secret` and no `native_nrf52_settings_paths` env (its `test/` holds 11 suites). Options for the PR: ship the
helper with a small `test_settings_dump_mask` suite and a `native_settings_dump_mask` env (the helper is header-only and
needs no stubs), or ship without tests and state that the fork suite covers it. Fork-only parts: the backlog row, docs and
the two fork suites.

## 8. Risks

- Diagnostic value: a masked dump no longer shows a corrupted or truncated password, only set or unset. The boot-2 loss
  hunt the command was built for concerns structure and counts, which stay visible. Accepted.
- `bt_code` masking versus CQ-01: the operator accepted `bt_code` in clear on the console for pairing. Masking it in the dump
  adds little while `--info` prints it. Kept in the plan for consistency with the ticket; operator ruling wanted (section 9).
- Stack: 192 bytes of line buffer on top of the 64-byte chunk, in the task that runs `commandAction()`. Not measured; use a
  `static` buffer instead if the stack headroom on the RAK4631 is tight (the command is a bench diagnostic, so a
  permanent 192 bytes of RAM is the price). Check on the bench node during step 3 of section 6.
- Second key list: the helper table duplicates key names; mitigated by `test_dump_secret_list_covers_schema_names`.
- Line handling: unterminated last line and over-long lines are covered by the tests above; a bug here would show wrong
  dump text, not lost settings (the function only reads).
- Documents older than this fix (`docs/bench/w3-baseline/README.md`, CHANGELOG) describe the dump as raw; they are history
  and stay.

## 9. Open questions for the operator

1. `bt_code` in the dump: mask (plan default, consistent with the ticket) or leave in clear because
   `--info` shows it anyway (CQ-01)?
2. Five keys instead of the ticket's four: include `node_pwd` (the legacy MeshCom server password)? Plan: yes.
3. Not in this plan: `...BTCODE` in `--info` stays (closed CQ-01). nRF52 has no BLE settings image any more, so the
   unmasked `node_opwd` in `bleSettingsV1MaskSecrets()` has no output path.

## Operator decisions 2026-11-21

- `bt_code` is masked in the dump (the plan default); `--info` keeps showing it (CQ-01 unchanged).
- The legacy `node_pwd` is masked too: five keys.
