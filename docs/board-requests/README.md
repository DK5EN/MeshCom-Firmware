# Board requests

Boards that users asked for or ported themselves. We do not pursue them; this file keeps what we
received so a later decision starts from facts. Pick up only on an operator decision.

## FakeTec v4 (nRF52840 Pro Micro + Heltec HT-RA62 SX1262)

- **Status 2026-11-23:** code received as a zip instead of a PR; stored for a later code review
  in a worktree. Not on GitHub. They also build their own companion app.
- **Source:** `faketec-v4-port-spec-20261123.md`, the spec as received (Polish operators, first
  mentioned 2026-11-07 in the hardware request from Poland).
- **Claimed result:** two-way RF, relaying, DM, BLE and OLED work on real hardware. Flash 706 of
  815 kB, RAM 98 of 248 kB.

### Code received (zip)

- **File:** `faketec-v4-meshcom-port-20261123.zip`, 175 329 bytes, SHA-256
  `5188335812d9981e1df3d2e1ec84fc22cd789f57cf87ff8d901df23d96838141`. Stored as received.
- **Contents:** `faketec_v4_changes.patch` (21 file diffs), the changed files loose
  (`platformio.ini`, `src/loop_functions.cpp`, `src/nbr_views.cpp`, `src/nrf52/api_functions.cpp`,
  `src/nrf52/nrf52_ble.cpp`, `src/nrf52/nrf52_main.cpp`) and `variants/faketec_v4/` (6 files).
  Zip paths use Windows backslashes.
- **Base:** the patch's pre-image blobs match upstream `dev` at `649b5b17` (merge of #1257,
  2026-11-06). Start the review worktree there, not on `fork-dev`.
- **Patch encoding:** UTF-16 LE with BOM and CRLF (PowerShell redirect). Convert before
  `git apply`: `iconv -f UTF-16LE -t UTF-8 | tr -d '\r'`, then drop the BOM.
- **Noise in the patch, not to take over:** deletes `REVIEW.md`; adds four `firmware*.uf2`
  binaries, `uf2conv.py` and `uf2families.json`; touches `variants/t_deck/lv_conf.h` and
  `variants/t_deck_plus/lv_conf.h`.
- **Encoding damage:** the patch's non-ASCII lines went through a wrong code page, so `git apply`
  fails on 4 files even after the UTF-16 conversion. The loose files are the author's tree, but
  their editor had already double-encoded umlauts and em-dashes in about 56 comment lines. Real
  change after putting those lines back: 92 lines in `src/` (`api_functions.cpp` 72+/7-,
  `loop_functions.cpp` +6, `nbr_views.cpp` 2/2, `nrf52_ble.cpp` 1/1, `nrf52_main.cpp` +1) plus
  `variants/faketec_v4/`.
- **Review worktree (2026-11-23):** `../MeshCom-faketec-review`, branch `review/faketec-v4` on
  `649b5b17`, cleaned tree uncommitted. The pre-image blobs of all patched files match `649b5b17`
  exactly.
- **First finding, root `platformio.ini`:** rewritten as a whole (tabs to spaces, RadioLib exclude
  list reordered, the fork comments dropped). It adds `jgromes/RadioLib@7.6.0` twice and a new
  `[nrf52]` section with an unpinned `platform = nordicnrf52`, which brings back the section
  collision that CFG-01 removed (`[nrf52_base]`). `[env:faketec_v4]` is defined in the root file
  and again in `variants/faketec_v4/platformio.ini`.

### What the spec gets right

- The pinout matches the common DIY Pro Micro + HT-RA62 layout: LoRa on SPIM2 (SCK P1.11, MOSI
  P1.15, MISO P0.02, CS P1.13), RESET P0.09, DIO1 P0.10, BUSY P0.29; I2C SDA P1.04, SCL P0.11.
- Board quirks it handles: 3.3 V rail gated by P0.13 (drive high at boot), no 32 kHz crystal
  (`USE_LFRC` instead of `USE_LFXO`), NFC pins P0.09/P0.10 must be freed, TCXO on DIO3 at 1.8 V,
  RF switch on DIO2.
- Flash use is plausible: `wiscore_rak4631` uses 681 of 815 kB today.

### What we would change before taking it

- **HWID:** the spec reuses HWID 9 (RAK4631), so map, server and MHeard show a RAK. A board of its
  own needs its own HWID, assigned with Kurt (server side).
- **NFC pins:** the spec rewrites `NRF_UICR->NFCPINS` at runtime in `initVariant()` and resets. The
  Adafruit nRF52 core does the same in `SystemInit` with `-DCONFIG_NFCT_PINS_AS_GPIOS`; use the
  flag.
- **Regional frequencies:** the spec hard-codes Polish defaults (MeshCom 433.175 MHz, LoRa APRS
  434.855 MHz) in the board port. 433.175 MHz is already EU8; an APRS frequency per country belongs
  in the country table as a separate change, not in a board.
- **RXEN P0.17:** an E22 module needs it; the HT-RA62 has no RXEN line. Harmless, but check.
- **Display:** the spec forces SSD1306 type 1 in `esp32_isSSD1306()`; must stay behind the board
  guard.
- **Scope:** a new board touches about 20 source files plus CI matrix, web flasher, auto-update
  asset map and the golden variant files (T114 is the reference). We have no FakeTec on the bench.

### Way in, if it is picked up

The author submits a PR against upstream `dev` with the changes above; their hardware is the
test. We do not build it from the spec.

## Ebyte EoRa-S3-400TB (ESP32-S3 + SX1268, 433 MHz)

- **Status 2026-11-23:** contributor has a working build and offered a PR (discussion #1282).
- **Closest existing board:** `E22_1268_S3-DevKitC-1-N16R8`.
- **Main risk:** 4 MB flash. Our S3 images are about 1.80 MB; a layout with two OTA slots on 4 MB
  is tight. The PR must name its partition table and show that an online update works.
