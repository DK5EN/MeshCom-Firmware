# Campaign BAT-04: battery charge state and current (upstream issue #1073)

| Item      | Value                                                                                                                                                                                                                                                                                    |
| --------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Started   | 2026-11-02 (plan only, nothing implemented)                                                                                                                                                                                                                                              |
| Baseline  | `fork-dev` at `adbf723f`. Code facts below were read at this commit                                                                                                                                                                                                                      |
| Request   | icssw-org/MeshCom-Firmware#1073 (DD8BA): show whether the battery is charging or discharging, including the current, on the T-Beam                                                                                                                                                       |
| Mechanics | `/orchestrate-waves`; one commit per wave on `fork-dev`; Opus advisor gate before every commit; one upstream PR afterwards via `/submit-pr`                                                                                                                                              |
| Status    | W0, W1, W2 done 2026-11-02 (findings 5a, wave log 7); W3 bench and W4 upstream open (operator's machine). Q1-Q4 answered 2026-11-02 (recommendations accepted); runs in the cloud with WX-01, schedule in [campaign-bat04-wx01-cloud-20261102.md](campaign-bat04-wx01-cloud-20261102.md) |

Line numbers drift. Every brief must `grep -n` the named symbol before trusting a line number in this document.

## 1. Verdict on the issue

- **The charge LED work does not answer the request.** `--charge led on` (LED-01..15, upstream with #1238) lets
  the AXP2101 drive its own charge LED. The firmware sets the LED mode but never reads the charge state. That
  covers tkuschel's side comment ("the T-Beam AXP2101 already has a charge LED"), not DD8BA's question.
- **Charge state is readable on every PMU board; current only on some.** The AXP2101 has no battery current ADC.

| PMU (boards)                                      | Charge state                                  | Battery current                                                                      |
| ------------------------------------------------- | --------------------------------------------- | ------------------------------------------------------------------------------------ |
| AXP2101 (T-Beam v1.2, T-Beam Supreme)             | `isCharging()`, `isDischarge()`, `isVbusIn()` | **none**: the chip has no battery current measurement                                |
| AXP192 (T-Beam v1.0 / v1.1)                       | same                                          | `getBatteryChargeCurrent()`, `getBattDischargeCurrent()` (ADC already enabled)       |
| BQ25896 + BQ27220 (T-Deck Pro, T5 e-paper)        | `PPM.isCharging()`, `bq27220.getIsCharging()` | `PPM.getChargeCurrent()` (charge only); BQ27220 average current is signed (W0 scout) |
| No PMU (Heltec, RAK4631, E22, T-Deck, Stick, ...) | none                                          | none (only with an external INA sensor; out of scope)                                |

## 2. Code facts (`adbf723f`)

- **One PMU read path.** `loopAction_battCheck()` in `src/esp32/loop_actions_esp32.cpp`, branch
  `MODUL_FW_TBEAM`: once per second (`BATT_SCHED_PROFILE_FIXED`) it calls `PMU->getBattVoltage()`,
  `PMU->getBatteryPercent()` (only for the "no battery" verdict, `< 0`) and, without a cell,
  `PMU->getVbusVoltage()`. Nothing reads the charge state or a current.
- **`MODUL_FW_TBEAM` boards:** `variants/ttgo_tbeam`, `ttgo_tbeam_SX1262`, `ttgo_tbeam_SX1268`, `ttgo_tbeam_supreme`.
- **PMU object:** `XPowersLibInterface *PMU` (`src/esp32/esp32_main.cpp`), created in `setupPMU()`
  (`src/esp32/esp32_pmu.cpp`), `NULL` when no PMU answers. Chip check: `PMU->getChipModel() == XPOWERS_AXP192`
  / `XPOWERS_AXP2101`.
- **Interface gap:** `isCharging()`, `isVbusIn()`, `isBatteryConnect()` and `isDischarge()` are virtual in
  `XPowersLibInterface`. The two AXP192 current getters are **not**: they need a `static_cast<XPowersAXP192 *>(PMU)`
  guarded by the chip-model check.
- **AXP192 current ADC:** `enableBattVoltageMeasure()` sets `MONITOR_BAT_CURRENT | MONITOR_BAT_VOLTAGE`, and
  `setupPMU()` already calls it. No new init needed (W0 confirms it runs for the AXP192 branch too).
- **BQ boards:** `src/t-deck-pro/ui_deckpro_port.cpp` and `src/t5-epaper/ui_port.cpp` already read
  `PPM.isCharging()`, `PPM.isVbusIn()` and `PPM.getChargeCurrent()` for their own UI. None of it reaches the console,
  web or BLE. Neither board writes `global_batt` itself; where its battery value comes from is a W0 scout question.
- **Surfaces that show the battery today:**

| Surface                | Where (`grep -n`)                                                                              | Shows today                     |
| ---------------------- | ---------------------------------------------------------------------------------------------- | ------------------------------- |
| Console `--info`       | `src/command_functions.cpp`, the `printf` with `global_batt/1000.0, global_proz, ...node_maxv` | voltage, percent, max voltage   |
| BLE `"TYP":"I"`        | `src/command_functions.cpp`, `idoc["BATP"]`, `idoc["BATV"]`                                    | percent, voltage                |
| Web status page        | `src/web_functions/web_functions.cpp`, `<tr><td>Battery</td>` (BAT-01 comment above it)        | voltage, percent, max, or "USB" |
| OLED / TFT status line | `src/loop_functions.cpp`, `cbatt` formatting                                                   | voltage or percent              |
| APRS position          | `src/loop_functions.cpp`, `/B=%03d`                                                            | percent                         |
| RM1 `status`           | `bat=` field                                                                                   | percent                         |

## 3. Operator decisions (answered 2026-11-02: all four recommendations accepted)

| #   | Question                                                                    | Recommendation                                                                                          |
| --- | --------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------- |
| Q1  | Which surfaces get the charge state?                                        | Console `--info`, web status, BLE `I`. Not APRS and not RM1 `status` (LoRa byte budget, REVIEW R4)      |
| Q2  | Include the T-Deck Pro and T5 (BQ chips), or T-Beam only?                   | Include them if W0 finds a clean hook; otherwise T-Beam only and a follow-up row                        |
| Q3  | OLED / TFT marker (for example `+` after the battery value while charging)? | No in this campaign; the display line is already tight on the 128 px OLED                               |
| Q4  | Current sign and unit                                                       | One signed integer in mA: positive = into the battery, negative = out of it; absent when not measurable |

Out of scope: tkuschel's board-LED blink-rate scheme (cosmetic, separate item), external INA sensors, charge
current settings.

## 4. Design

### 4.1 Pure core: `src/batt_charge.h` (header-only, host-testable)

```cpp
enum BattChargeState : uint8_t {
    BCS_UNKNOWN = 0,   // no PMU / not measurable on this board
    BCS_NO_BATT,       // VBUS present, no cell
    BCS_CHARGING,      // VBUS present, cell charging
    BCS_FULL,          // VBUS present, cell connected, charger idle (full or paused)
    BCS_DISCHARGING,   // no VBUS, running from the cell
};

struct BattChargeInfo {
    BattChargeState state;
    bool    ma_valid;   // false on AXP2101 and on boards without a current ADC
    int16_t ma;         // signed: + into the battery, - out of it (Q4)
};

BattChargeState battChargeClassify(bool vbus, bool batt_connected, bool charging);
int16_t         battNetMa(float charge_ma, float discharge_ma);   // clamp to int16, round
const char     *battChargeName(BattChargeState s);                // "unknown" "no battery" "charging" "full" "discharging"
size_t          battChargeFormat(char *buf, size_t n, const BattChargeInfo &ci);  // "charging +312 mA" / "discharging" / "unknown"; always terminated
```

- Classification table (host test pins every row): `vbus && !batt -> NO_BATT`; `vbus && batt && charging ->
CHARGING`; `vbus && batt && !charging -> FULL`; `!vbus && batt -> DISCHARGING`; `!vbus && !batt -> UNKNOWN`.
- `battChargeFormat` never prints `0 mA` for "not measurable": the current is left out when `ma_valid` is false.

### 4.2 State: one global, written by the battery loop only

- `BattChargeInfo global_batt_charge` (zero-initialised = `BCS_UNKNOWN`), defined next to `global_batt` in
  `src/batt_functions.cpp`, declared in `src/batt_functions.h` and `src/loop_functions_extern.h`.
- Written only from `loopAction_battCheck()` (ESP32) and the BQ hook (W1-C); readers copy the struct once.

### 4.3 ESP32 PMU read (W1-B)

- Inside the existing 1-s PMU slot, after the voltage read: `vbus = PMU->isVbusIn()`, `batt = PMU->isBatteryConnect()`,
  `chg = PMU->isCharging()`, then `battChargeClassify()`.
- AXP192 only: `auto *axp = static_cast<XPowersAXP192 *>(PMU)`, `battNetMa(axp->getBatteryChargeCurrent(),
axp->getBattDischargeCurrent())`, `ma_valid = true`. AXP2101: `ma_valid = false`.
- `PMU == NULL`: `BCS_UNKNOWN`. `DISABLE_BATTERY`: `BCS_UNKNOWN`.
- `--display cont` print (the existing 10-s line) gains the formatted state.
- Cost: 3 status register reads per second, plus 4 ADC register reads on the AXP192. No new task, no new timer.

### 4.4 BQ boards (W1-C, only if Q2 = include)

- W0 scout names the one place per board where the battery value is refreshed. W1-C fills `global_batt_charge`
  there from `PPM.isVbusIn()`, `PPM.isCharging()` and the BQ27220 signed average current (`ma_valid = true`), or
  `PPM.getChargeCurrent()` when the gauge is absent (then `ma_valid` only while charging; discharge stays absent).

### 4.5 Surfaces (W1-D, W1-E)

- **Console `--info`:** one new line `Charge: <battChargeFormat>` after the battery line; omitted when
  `BCS_UNKNOWN`, so boards without a PMU print exactly what they print today.
- **BLE (as built, C5):** own register `IS2` after `IS1` with `CHG` (state name) and `CHGMA` (mA, only when
  `ma_valid`); not sent on `BCS_UNKNOWN`. The original plan (keys `CHG`/`BATI` in `I`) failed on the `I` budget, see 5a.
  W0 measures the worst-case `I` document against the BLE send buffer before the keys are added; if the margin is
  under 30 bytes, the keys move to SN1-style follow-up (decision then, not now).
- **Web status page:** the Battery row gains `, charging +312 mA` (same formatter). "USB" rows (BAT-01,
  `global_batt == 0.0`) gain the state too when it is known.
- **Not touched:** APRS `/B=`, RM1 `status`, OLED/TFT (Q1, Q3).

## 5. Waves

### W0 (orchestrator, before any writer)

1. Contract: create `src/batt_charge.h` with the declarations of 4.1 (bodies stubbed `return BCS_UNKNOWN;` etc.) and
   the global of 4.2 (`batt_functions.{h,cpp}`, `loop_functions_extern.h`).
2. `platformio.ini`: `[env:native_batt_charge]` (pattern of the other `native_*` Unity envs); regenerate
   `test/golden/native/variant-ini-effective.json` at the gate.
3. Scout (read-only):
   - confirm `enableBattVoltageMeasure()` runs on the AXP192 path in `setupPMU()`;
   - where the T-Deck Pro and the T5 refresh their battery value, and whether `bq27220` exposes a signed average
     current getter;
   - worst-case size of the BLE `I` document and the buffer it goes into;
   - which bench boards carry an AXP192, an AXP2101, a BQ25896 (`/bench`).
4. Operator answers Q1-Q4 recorded in section 3.

### W1 (parallel writers, disjoint files)

| Agent | Task                                      | Exclusive files                                                                                                                      | Model        |
| ----- | ----------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------ | ------------ |
| A     | Pure core: bodies of 4.1, host tests      | `src/batt_charge.h`, `test/test_batt_charge/`                                                                                        | Sonnet, med  |
| B     | ESP32 PMU read (4.3), source pins         | `src/esp32/loop_actions_esp32.cpp`, `test/test_batt_charge_pmu_pins/`                                                                | Sonnet, high |
| C     | BQ boards (4.4), only if Q2 = include     | `src/t-deck-pro/tdeck_pro.cpp` or `ui_deckpro_port.cpp`, `src/t5-epaper/t5epaper_main.cpp` or `ui_port.cpp` (W0 names one per board) | Sonnet, high |
| D     | Console `--info` line, BLE `I` keys (4.5) | `src/command_functions.cpp` (the `--info` printf and the `idoc` block only), `test/test_command_setters/` or a new pin test          | Sonnet, med  |
| E     | Web status Battery row (4.5)              | `src/web_functions/web_functions.cpp` (Battery row only), web status test if one covers the row                                      | Sonnet, med  |

Orchestrator-owned: `src/batt_charge.h` declarations (A fills bodies only, no signature changes),
`src/batt_functions.{h,cpp}`, `src/loop_functions_extern.h`, `platformio.ini`, `test/golden/`, docs.

Brief rules for every writer: no `pio` outside the lock wrapper, never bare `pio test`, no git, touch only the
listed files, every behaviour gets a test that fails on the stub and passes after.

### W2 (gate, orchestrator)

- `tools/regression.sh --stage 1,2` green, Unity passed = collected.
- Clean builds (with `MC_AU_DEV_CHANNEL=1`): `ttgo_tbeam`, `ttgo_tbeam_supreme`, `heltec_wifi_lora_32_V3`
  (no-PMU board: output unchanged), `wiscore_rak4631` (nRF52 compiles the shared files), plus `t_deck_pro` and
  `t5_epaper` if Q2 = include. RAM / flash regions recorded against the previous commit.
- Opus advisor on the full diff; rework until APPROVED; then commit W1+W2 as one firmware commit.

### W3 (bench, orchestrator)

- `/bench` first, `identity_guard.py`, `lsof` on the port, 2 dBm, no broadcast (none needed: this is local only).
- AXP2101 T-Beam: USB in, cell in -> `charging` or `full`; USB out -> `discharging`; cell out on USB ->
  `no battery`. No mA anywhere.
- AXP192 T-Beam (if in the fleet): same, and the sign of the current flips between USB in and out; the value is
  plausible against a USB meter (within 20 %).
- BQ board (if Q2 = include): same transitions, mA present.
- Heltec V3: `--info`, web and BLE `I` byte-identical to the previous build.
- Evidence: summaries under `tools/bench/runs/harness/`, cited in the wave section of this doc.

### W3 result (2026-11-02, DK5EN-92, T-Beam AXP2101, fork-dev `328e858a`)

| Step             | Console `--info`      | Web Battery row                        | BLE `IS2`                           |
| ---------------- | --------------------- | -------------------------------------- | ----------------------------------- |
| USB in, no cell  | `Charge: no battery`  | `5.186V (100%) max 4.200V, no battery` | `{"TYP":"IS2","CHG":"no battery"}`  |
| USB in, cell in  | `Charge: charging`    | `4.189V (99%) max 4.200V, charging`    | `{"TYP":"IS2","CHG":"charging"}`    |
| USB out, cell in | `Charge: discharging` | `4.146V (95%) max 4.200V, discharging` | `{"TYP":"IS2","CHG":"discharging"}` |

- No `CHGMA` anywhere (AXP2101 has no current ADC), as designed. `I` and `IS1` unchanged; uptime continuous across
  the USB unplug (no reboot). BLE via `tools/bench/ble_golden.py` (one `--info` write), web via Chrome and the page text.
- Heltec V3 (DK5EN-1, no PMU), baseline `8681fcb4` vs fork-dev `328e858a`: console `--info` identical apart from
  volatile values, no `Charge:` line; BLE `I`/`IS1` same keys, no `IS2`; web Battery row unchanged (the info page gains
  the WX-01 lines `UV (LTR390): off` / `Anemometer: off`). With a cell from boot both builds read 4.12 V. A cell
  hot-plugged into a running node is not detected on either build: pre-existing, BACKLOG `BAT-05`.
- Heltec V3 on battery (USB unplugged, cell from boot): `3.98 V / 73 %` on console and web, no `Charge:` line, no
  reboot.
- Not observed: `full` (cell at 95-99 %). Not benched: an AXP192 T-Beam (none in the fleet, so the signed mA path is
  host-tested only), (the Heltec V3 check followed the same day, line above).

### W4 (docs and upstream)

- This doc, `docs/RESUME.md`, BACKLOG row BAT-04, defect catalogue entry.
- `/submit-pr`: branch from `upstream/dev`, port the slice, German PR body that references #1073.
- German comment on #1073: charge LED via `--charge led on` (since #1238); charge state on every PMU board;
  current on AXP192 and BQ boards; AXP2101 cannot measure it.

## 5a. W0 findings (2026-11-02, cloud session C0)

- **AXP192 current ADC is not enabled.** `enableBattVoltageMeasure()` runs only in the AXP2101 branch of
  `setupPMU()` (`src/esp32/esp32_pmu.cpp`). W1-B adds it to the AXP192 branch (file added to its set).
- **No clean BQ hook (Q2 fallback: T-Beam only).** T-Deck Pro and T5 refresh `global_batt` through the generic
  `read_batt()` path in `loopAction_battCheck()`; `PPM` / `bq27220` are read only by the UI getters
  (`ui_deckpro_port.cpp`, `ui_port.cpp`), with no periodic poll. The BQ27220 driver has `int16_t getCurrent()`
  (register 0x0C), no average-current getter. **W1-C dropped; follow-up row BAT-04b.**
- **BLE `I` keys deferred.** The worst-case `I` document is 296 bytes against `msg_buffer[300]` (3 bytes margin) and
  already above `BLE_JSON_PAYLOAD_MAX` 244, where `bleJsonFrameFailSoft()` drops trailing keys. Margin under 30
  bytes, so per 4.5 `CHG` / `BATI` are not added to `I`. **Closed 2026-11-02 (operator decision, C5):** a new
  register `"TYP":"IS2"` after `IS1` carries `CHG` (state name) and `CHGMA` (signed mA, only when measurable); it is
  sent only when the state is known, so `I` and `IS1` stay byte-identical everywhere.
- **Both battery files define `global_batt_charge`:** the T-Beam, Heltec, RAK and BQ boards build
  `batt_function_old.cpp`, the E22 boards `batt_functions.cpp`.
- **Bench boards per PMU:** not checked in the cloud session (no `/bench` access); W3 on the operator's machine.

## 6. Definition of done

- Every row of the classification table and the format cases (`ma_valid` false, clamp at int16 limits, buffer
  of 1 byte) pinned by host tests that fail on the W0 stubs.
- Source pins: the PMU branch calls `isCharging()` and casts to `XPowersAXP192` only behind the chip-model check.
- Boards without a PMU produce byte-identical `--info`, web and BLE `I` output (checked on the Heltec V3 bench run).
- No new LoRa bytes (APRS and RM1 untouched unless Q1 says otherwise).
- Gate green, builds listed in W2 green, advisor APPROVED, bench W3 done.

## 7. Wave log

| Wave  | Commit                                    | Gate                                                                                                               | Advisor               | Notes                                                                                                                                                                                                                                                                                                                                                                                                                    |
| ----- | ----------------------------------------- | ------------------------------------------------------------------------------------------------------------------ | --------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| W0    | -                                         | -                                                                                                                  | -                     | Findings in 5a: AXP192 current ADC not enabled, no clean BQ hook (W1-C dropped, follow-up BAT-04b), BLE `I` keys deferred (3 bytes margin)                                                                                                                                                                                                                                                                               |
| W1+W2 | BAT-04 firmware commit (cloud session C1) | stage 1,2 PASS (92 envs, 3383/3383); clean builds of all 7 gate envs green; T-Beam image +2136 B flash, RAK +544 B | REWORK, then APPROVED | Writers A (core + 10 host tests), B (PMU read, AXP192 ADC enable, 5 source pins), D (`--info` line + pin), E (web row, no test: no native harness covers `sub_page_info()`). Rework: AXP192 VBUS ADC stays off (a cell-less T-Beam keeps showing "USB"), currents above 4095.5 mA (failed I2C read) leave `ma_valid` false, exact guard pin. `--info` prints `...Charge: <state>` (prefix like the other `--info` lines) |
| C5    | BAT-04 BLE commit (cloud session C5)      | stage 1,2 and builds, see the cloud doc C5 row                                                                     | see the cloud doc     | Register `IS2` (`CHG`, `CHGMA`) after `IS1`, only when the state is known; source pin + worst-case size test (48 JSON bytes against 244); `I` pin narrowed to the `I` document; harness note (HELTEC/RAK profiles have no PMU, burst unchanged)                                                                                                                                                                          |

Open for the operator: W3 bench (AXP2101 and AXP192 T-Beam transitions, Heltec V3 byte-identical check), W4 PR and
the #1073 comment, BAT-04b (BQ boards: needs a periodic `PPM`/`bq27220` poll). The BLE keys shipped as register `IS2` (C5).
