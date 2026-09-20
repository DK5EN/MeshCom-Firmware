# 02 — Build System & Variants

> **How do ~30 board variants map onto one source tree?**

## Mechanism

```
platformio.ini                          root: shared sections + default_envs
  extra_configs = variants/*/platformio.ini
                                        every variant dir contributes its own fragment
variants/<board>/platformio.ini         [env:<board>], extends = esp32 | nrf52
variants/<board>/configuration.h        pins, BOARD_* macro, hardware flags
```

Each environment sets exactly one `-D BOARD_<X>="<name>"` flag and adds
`-I variants/${this.__env__}` so that `#include <configuration.h>` resolves to that board's
header. Board-specific behaviour is then selected inside application code by
`#if defined(BOARD_<X>)`.

**34 environments** are declared, **32** are in `default_envs`:

- 30 firmware targets + 2 safeboot bootloader images
- `t5_epaper` is declared but commented out of `default_envs`
- `vision-master-e213-preview` is declared but not in `default_envs`

## Shared sections

| Section             | Where                                                              | Purpose                                       |
| ------------------- | ------------------------------------------------------------------ | --------------------------------------------- |
| `[libs]`            | root                                                               | 16 sensor/GPS/JSON libs common to all boards  |
| `[esp32libs]`       | root                                                               | WiFi, RadioLib 7.6.0, NimBLE 2.2.3, ESP32Ping |
| `[nrf52libs]`       | root                                                               | RAK13800-W5100S, SX126x-Arduino, SHTC3, LPS2X |
| `[common]`          | root                                                               | 22 `RADIOLIB_EXCLUDE_*` build flags           |
| `[esp32]`           | root                                                               | platform, `src_filter`, NimBLE tuning, C++17  |
| `[nrf52]`           | **`variants/{wiscore_rak4631,heltec_t114,t_echo}/platformio.ini`** | see finding B-01 below                        |
| `[upload_settings]` | root                                                               | monitor speed                                 |

## Source filtering

`build_src_filter` per platform excludes whole subtrees:

```ini
[esp32]  -<nrf52/*> -<Displays/*> -<Fonts/*> -<GFX_Root/*> -<Platforms/*>
         -<safeboot/*> -<t-deck/*> -<t-deck-pro/*> -<t5-epaper/*> -<lvgl/*>
[nrf52]  -<esp32/*> -<Displays/*> -<Fonts/*> -<GFX_Root/*> -<Platforms/*>
         -<safeboot/*> -<t-deck/*> -<t-deck-pro/*> -<t5-epaper/*>
         -<tinyxml_function.cpp>
```

Boards that need a subtree re-add it (`t_deck`, `t_deck_pro`, `t5_epaper`,
`vision-master-*`, `wireless-paper`). Displays are therefore selected by a combination of
`build_src_filter`, `lib_ignore` and `#ifdef` — three mechanisms doing one job.

## Board macro inventory

32 distinct `BOARD_*` macros appear inside preprocessor conditionals in `src/`. Ranked by
how often application code has to branch on them:

| Macro                  | Conditional sites | Macro                                 | Conditional sites |
| ---------------------- | ----------------: | ------------------------------------- | ----------------: |
| `BOARD_T_ECHO`         |               102 | `BOARD_E290`                          |                29 |
| `BOARD_T_DECK_PLUS`    |                83 | `BOARD_E213`                          |                23 |
| `BOARD_T_DECK`         |                83 | `BOARD_TBEAM_1W`                      |                21 |
| `BOARD_RAK4630`        |                64 | `BOARD_HELTEC_V4`                     |                20 |
| `BOARD_T_DECK_PRO`     |                61 | `BOARD_TBEAM_V3`                      |                19 |
| `BOARD_T5_EPAPER`      |                52 | `BOARD_E22_S3`                        |                17 |
| `BOARD_HELTEC_T114`    |                47 | `BOARD_TLORA_OLV216`                  |                15 |
| `BOARD_T_CONNECT_PRO`  |                45 | `BOARD_STICK_V3`                      |                14 |
| `BOARD_WIRELESS_PAPER` |                31 | `BOARD_HELTEC_V3`                     |                12 |
| `BOARD_TRACKER`        |                30 | `BOARD_T3S3_V13`, `BOARD_T_ETH_ELITE` |           11 each |

Total `BOARD_*` references across `src/`: **1,131**.

`configuration_global.h` additionally maintains a parallel numeric hardware-type registry
(`TLORA_V2 1` … `ESP32_LORAPRS_RA01 60`, 38 entries) used for the over-the-air `msg_source_hw`
field. Board identity is therefore expressed twice — once as a macro, once as a number —
with no compile-time link between them.

## Findings

> **CORRECTED 2026-07-31 — B-01's mechanism is wrong and its proposed fix breaks two
> boards.** ConfigParser merges duplicate sections option-by-option; there is no glob-order
> race. `pio project config --json-output` shows `heltec_t114` and `t_echo`
> **deterministically** inherit `-D BOARD_RAK4630="RAK4630"` (used at 66 `#if` sites),
> `-Isrc/nrf52`, and `+<../variants/wiscore_rak4631/*>` from the `wiscore_rak4631` copy of
> the section. The cleanup proposed below would silently strip all three from both boards.
> Removing the duplication is still worthwhile, but only together with explicit per-board
> restatement of those three options. See
> [08 C-05](08-defect-catalogue.md#c-05--02s-finding-b-01-has-the-wrong-mechanism-and-a-board-breaking-fix--verified).

### B-01 — `[nrf52]` is declared three times

`[nrf52]` appears at the top of `variants/wiscore_rak4631/platformio.ini`,
`variants/heltec_t114/platformio.ini` **and** `variants/t_echo/platformio.ini`. Because
`extra_configs = variants/*/platformio.ini` merges all fragments into one namespace, the
three definitions collide and the effective content depends on glob order.

The `wiscore_rak4631` copy carries `extends = upload_settings`, `build_src_filter`,
`build_flags` and `+<../variants/wiscore_rak4631/*>` — the other two carry only `platform`
and `framework`. Whichever loses, two of the three nRF52 boards inherit something other
than what their file says.

**Fix:** move `[nrf52]` into the root `platformio.ini` next to `[esp32]`, and drop the
board-specific `+<../variants/wiscore_rak4631/*>` line down into `[env:wiscore_rak4631]`.

### B-02 — 26 variants carry a `lib_ignore` entry that cannot match

`lib/SensorLibTDECkpro/library.json` declares `"name": "SensorLib"`. PlatformIO matches
`lib_ignore` against the **declared library name**, not the directory name. The variants
spell it `SensorLibTDECKpro` (20 files) or `SensorLibTDECKPro` (6 files) — neither matches
`SensorLib`, and neither matches the directory `SensorLibTDECkpro` either.

`variants/ttgo-lora32-v21/platformio.ini:16` additionally has `XPowerLib` where the library
is `XPowersLib`.

Impact today is limited — with `lib_ldf_mode` at its default the library is not compiled
unless something includes it. But the config is dead weight that reads as protection, and
it will bite the moment LDF mode or `lib_compat_mode` changes. Note also that
`lewisxhe/SensorLib` (used by `t_deck_pro`, `t5_epaper`) declares the _same_ name as the
vendored copy — a genuine collision if both are ever in one environment.

### B-03 — three `build_src_filter` exclusions target files that do not exist

| Written                   | Actual file                 | Effect                                  |
| ------------------------- | --------------------------- | --------------------------------------- |
| `-<tinyxml_function.cpp>` | `src/tinyxml_functions.cpp` | filter never matches — file is compiled |
| `-<esp32/esp32_gps.cpp>`  | does not exist              | no-op                                   |
| `-<gps_l76k.cpp>`         | does not exist              | no-op                                   |

All three are harmless **today**. `src/tinyxml_functions.cpp` wraps its entire body
(lines 7–245) in `#if defined(ENABLE_XML)`, which only `E22_XML-DevKitC` defines, so on
nRF52 it compiles to an empty translation unit. The exclusion is dead config that appears
to do something it does not — worth fixing so the next person does not rely on it.

### B-04 — platform pins are inconsistent across variants

| Pin                    | Environments                                                 |
| ---------------------- | ------------------------------------------------------------ |
| `espressif32@^6.13.0`  | `[esp32]` base → all ESP32 boards that do not override       |
| `espressif32@^6.6.0`   | `vision-master-e213`, `vision-master-e290`, `wireless-paper` |
| `espressif32 @ 6.6.0`  | `t_deck`, `t_deck_plus` (comment cites TFT_eSPI issue #3332) |
| `espressif32@6.5.0`    | `t_deck_pro`, `t5_epaper`                                    |
| tasmota `2026.02.30`   | `esp32-safeboot`, `esp32-S3-safeboot`                        |
| `nordicnrf52` unpinned | `wiscore_rak4631`, `heltec_t114`, `t_echo`                   |

Two consequences:

- **The nRF52 platform is unpinned.** CI runs `pio platform install nordicnrf52` and gets
  whatever is latest (currently 10.12.0, Adafruit nRF52 core 1.7.0). A developer with an
  older local install builds against a different core. Local and CI artifacts are not
  comparable. This should be pinned.
- The caret pins are largely cosmetic: `^6.6.0` and `^6.13.0` both resolve to 6.13.0
  today, and _every_ official `espressif32` release — including 7.0.1 — pins Arduino-ESP32
  to `~3.20017.0` (= 2.0.17). See [03 — Dependencies](03-dependencies.md).

### B-05 — RadioLib is pinned at two different versions inside one codebase

`[esp32libs]` pins `jgromes/RadioLib@7.6.0`. `t_deck_pro` and `t5_epaper` override with
`RadioLib@7.1.2` — five minor releases behind.

`src/lora_functions.cpp` (CAD, CSMA, retransmission, `getNextTxSlot`) is shared by all
boards. Those two boards therefore run the shared RF timing logic against a different
radio driver than every other board, and no test tells you when that diverges.

### B-06 — `lib_deps` blocks are copy-pasted per variant

Several variants restate the full sensor library list instead of referencing
`${libs.lib_deps}`. `bblanchon/ArduinoJson` appears as `^7.4.3` in the root and `^7.4.1` in
two variants; `lewisxhe/SensorLib` as `^0.2.6` and `@^0.2.6`. Each variant that drifts is a
separate dependency graph to reason about.

### B-07 — CI builds on tags only

`.github/workflows/meshcom-ci.yml` triggers on `push: tags: '*'`. There is no
build-on-PR and no build-on-push gate. With 30 environments, ~24 contributors and
`#ifdef`-driven board variance, a change that breaks 12 boards is not detected until
someone cuts a tag.

This is the cheapest high-value fix in the whole repository: add a `pull_request` trigger
and a build matrix. It costs CI minutes and nothing else.

## Suggested cleanups, ordered by value/effort

| #   | Change                                                        | Effort | Value  |
| --- | ------------------------------------------------------------- | ------ | ------ |
| 1   | CI: build all envs on PR and push (B-07)                      | S      | High   |
| 2   | Pin `nordicnrf52` to an exact version (B-04)                  | XS     | High   |
| 3   | Fix `-<tinyxml_function.cpp>` typo (B-03)                     | XS     | Low    |
| 4   | Single `[nrf52]` in root `platformio.ini` (B-01)              | S      | Medium |
| 5   | `lib_ignore` → `SensorLib`, `XPowersLib` (B-02)               | S      | Low    |
| 6   | Converge RadioLib to one version across all boards (B-05)     | M      | High   |
| 7   | Replace per-variant `lib_deps` copies with `${libs.…}` (B-06) | M      | Medium |
