# Bug-fix impl plan: BMx280 "Humidity: not fitted" (RM `sens`)

Status: plan, verified by /fable-review 2026-10-18 (5 finders, 3 Opus verifiers, orchestrator
spot-checks). Not implemented. Input for `/orchestrate-waves`.

## Wave status log

| Wave | Content                                        | Status  |
| ---- | ---------------------------------------------- | ------- |
| W1   | Presence helper, `bmx_is_bme`, execSens, tests | pending |
| W2   | Gate, bench readback of `sens`, docs, commit   | pending |

## BLUF

The RM `sens` reply decides "sensor fitted" from the **config flags** (`--bmp on` / `--bme on`), not
from the **chip the driver detected**. Those flags really only pick the I2C address (`--bmp` = 0x77,
`--bme` = 0x76). The driver identifies the chip itself (chip ID 0x58 BMP280 / 0x60 BME280).
OE5HWN-14 runs a BME280 at 0x77 enabled with `--bmp on`. The node measures 49.8 % rH and sends it
over the mesh, but `execSens` emits `h=-` and McApp renders "not fitted".

Fix: compute presence from `flag && *_found && detected chip type` in a pure helper in
`src/rm_format.h`, and cover it with native regression tests. Code is fork-only (not in
`upstream/dev`), so this lands on `fork-dev` with no upstream PR.

## Field report

Node OE5HWN-14, ESP32, fw 4.40a, owner calls the sensor a "BMP280". Screenshots 2026-10-18:

- 10:36, McApp remote Sensors card: Temperature 20.5 °C, Humidity **not fitted**, Pressure 989.4 hPa,
  Temperature 2 not fitted.
- 10:37, node web WX page: BME(P)280 on (found), BME680 off, 1-Wire off (GPIO 0), Temperature 20.4,
  Tout 0.0, Humidity **49.8 % rH**, QFE 989.4.

## Verified root cause

| #   | Claim                                                                                                                                                                         | Verdict   | Evidence                                                                                               |
| --- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------- | ------------------------------------------------------------------------------------------------------ |
| C1  | `execSens` sets `hasH = bBMEON \|\| bBME680ON \|\| bAHT20ON \|\| bSHT21ON`; `bBMPON` absent. Flags are mutually exclusive.                                                    | CONFIRMED | `src/rm_exec_read.cpp:107`; boot `esp32_main.cpp:1453-1466`; setters `command_functions.cpp:2289/2320` |
| C2  | BMx280 humidity is non-zero only for chip ID 0x60. `begin()` accepts only 0x58/0x60.                                                                                          | CONFIRMED | `src/bmx280.cpp:274-288`; `BMx280MI.cpp:66`, `isBME280()` = `id_ == 0x60`                              |
| C3  | Flag selects the address only (BMP 0x77, BME 0x76); chip type is auto-detected.                                                                                               | CONFIRMED | `src/bmx280.cpp:14-15`, override only `variants/t_echo/configuration.h:17` (nRF52)                     |
| C4  | `node_hum` is runtime-only, zeroed at boot, rewritten every 60 s.                                                                                                             | CONFIRMED | `meshcom_settings_runtime.h:31`, `esp32_main.cpp:1539`, writer `:4639`                                 |
| C5  | No other humidity writer fits the evidence: AHT20/SHT21 flags would set `hasH`; BME680 off; DHT needs `bONEWIRE` (web shows off); EXTUDP tele returns early when `bmx_found`. | CONFIRMED | `onewire_functions.cpp:161`, `extudp_functions.cpp:249`, `web_functions.cpp:2009`                      |
| C6  | McApp side is correct: `-` → null → "not fitted"; nothing else yields that text.                                                                                              | CONFIRMED | `McApp/src/mcapp/node_admin_state.py:322-335`, `webapp/.../RemoteSensorsCard.vue:43`                   |
| C7  | Mirror defect: a BMP280 at 0x76 (common default) must be enabled with `--bme on`, and then `sens` reports `h=0` as measured.                                                  | CONFIRMED | same lines as C1/C2                                                                                    |
| C8  | BMx enabled but `begin()` failed: `sens` claims t and p and prints 0.0.                                                                                                       | CONFIRMED | writers gated on `bmx_found` (`esp32_main.cpp:4623`, `nrf52_main.cpp:2510`), `execSens` is not         |

Residual alternative (not excluded by code): an AHT20+BMP280 combo board with `--aht20 on` at 10:37
and off at 10:36 (the screenshots are one minute apart). Settles with any of: web Info page
"Sensors" block (`web_functions.cpp:4119-4134`), `--wx`, or boot log `[INIT]...BME280 started` /
`setupBMX280 - I2C:77`. Ask the reporter; the fix is correct either way.

## Refuted claims (do not re-investigate)

- "No other sensor could supply humidity" in general: wrong, DHT on 1-Wire writes `node_hum`
  (`onewire_functions.cpp:161`). Only excluded for this node because 1-Wire is off.
- "`bmx_found` is reset only in `setupBMX280`": wrong, `--bmx/bme/bmp off` clears it
  (`command_functions.cpp:2418`). Full list of clear sites: `bmx280.cpp:126,172`,
  `command_functions.cpp:2291,2322,2418`, `esp32_main.cpp:1460,1467`, `nrf52_main.cpp:778,785`.
- "nRF52 lacks the SHT21 reader, so `execSens` mis-claims SHT21 there": unreachable,
  `ENABLE_SHT21_DISABLED` on every nRF52 variant and `bSHT21ON` is never loaded in `nrf52_main.cpp`.
- "`isBME280()` from the RM path touches I2C": no, the default argument returns the cached `id_`
  (`BMx280MI.cpp:338-344`).
- "RM exec races the sensor loop": no, `rmDrain()` runs in the loop task on both platforms
  (`esp32_main.cpp:3621`, `nrf52_main.cpp:1521`), as does `setupBMX280`.
- "The app parser or stale state produces 'not fitted'": no, only a literal `-` does.
- "BME680 + BMx leaves `bBMPON` set": wrong, `bmx280.cpp:124-126` clears `bBMPON`, `bBMEON` and
  `bmx_found`.
- "Workaround: `--bme on`" (first RCA reply): wrong for this node, `--bme` probes 0x76 and the chip
  is at 0x77, so `begin()` fails. There is no config-only workaround.
- "`docs/test-suite-map.md` says 18 for test_rm_format and that is current": stale, the suite has 20
  `RUN_TEST` (lines 573-592). Total at `docs/test-suite-map.md:13`.

## Scope and operator decisions

| ID  | Item                                                                                                                                                | Recommendation                                                                                                                                                                                   |
| --- | --------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| D1  | BMx280 presence from `flag && bmx_found && chip` (h by chip, t/p by found)                                                                          | **In scope** (the reported bug, C1/C7/C8)                                                                                                                                                        |
| D2  | Same found-gating for AHT20, SHT21, BME680 (`bAHT20ON && aht20_found` etc.); all `*_found` are unconditional globals (`loop_functions.cpp:221-226`) | **In scope**, same helper, no extra cost; removes claimed-but-never-written values                                                                                                               |
| D3  | BMP3 writes `node_temp` when `!aht20_found` (`src/esp32/loop_actions_esp32.cpp:131-135`), but `sens` does not claim t for BMP3                      | **In scope**: `hasT` gets `bBMP3ON && bmp3_found`; the in-code comment at `rm_exec_read.cpp:100-103` is updated                                                                                  |
| D4  | DHT on 1-Wire writes `node_temp`/`node_hum` (`onewire_functions.cpp:140,161`), `sens` claims only t2                                                | **Defer** (backlog SENS-03): `dht_found` is set from the compiled type name with no bus probe and latches false after one NaN read (`onewire_functions.cpp:82-84,130-157`). Needs its own design |
| D5  | Value-printing sites with no presence test (`--wx`, BLE "W", OLED, web WX table, telemetry) show 0.0 for a BMP280                                   | **Out of scope**, cosmetic, flag-free                                                                                                                                                            |
| D6  | Web WX page row "BME(P)280" names neither flag nor chip                                                                                             | **Optional** small add-on: print the detected chip, e.g. `on (BME280 @0x77)`. Decide before W1 or move to backlog                                                                                |

Proposed defect IDs (new prefix, add to `docs/architecture/08-defect-catalogue.md`): **SENS-01**
(D1), **SENS-02** (D2+D3), **SENS-03** (D4, backlog only).

## W1: implementation (one implementer, Sonnet medium)

### File ownership (exclusive)

| File                                | Change                                                                                  |
| ----------------------------------- | --------------------------------------------------------------------------------------- |
| `src/loop_functions.cpp`            | add `bool bmx_is_bme = false;` next to `bmx_found` (line 222)                           |
| `src/loop_functions_extern.h`       | add `extern bool bmx_is_bme;` next to `bmx_found` (line 165)                            |
| `src/bmx280.cpp`                    | set `bmx_is_bme = bmx280.isBME280();` immediately before `bmx_found = true;` (line 209) |
| `src/rm_format.h`                   | new pure inline helper `rmSensPresence(...)` next to `RmSensIn` (line ~390)             |
| `src/rm_exec_read.cpp`              | `execSens` fills the helper inputs from globals and calls it; update comment at 98-103  |
| `test/test_rm_format/test_main.cpp` | new regression tests + `RUN_TEST` lines                                                 |
| `docs/test-suite-map.md`            | row at line 251 (20 + new) and total at line 13                                         |

`bmx_is_bme` is a plain global (not an accessor in `bmx280.h`) because `bmx280.h/.cpp` are entirely
under `#if defined(ENABLE_BMX280)`, and 5 variants define `ENABLE_BMX280_DISABLED`
(vision-master-e213, heltec_t114, wireless-paper, t5_epaper, t_deck_pro) while `rm_exec_read.cpp` is
built everywhere. The global needs no guard and is never set on those builds. A stale `bmx_is_bme`
is harmless because every use is ANDed with `bmx_found`; mirroring the clear sites is not required.

### Helper contract (`src/rm_format.h`)

Header-only, no Arduino includes (`native_rm_format` has `test_build_src = no`). Suggested shape:

```cpp
struct RmSensFlags
{
    bool bmpOn, bmeOn, bmxFound, bmxIsBme; // BMx280: flag picks address, chip from ID
    bool bme680On, bme680Found;
    bool aht20On, aht20Found;
    bool sht21On, sht21Found;
    bool bmp3On, bmp3Found;
    bool oneWireOn;                          // t2 only (DHT out of scope, SENS-03)
};

inline void rmSensPresence(const RmSensFlags &f, RmSensIn &in)
{
    const bool bmx = (f.bmpOn || f.bmeOn) && f.bmxFound;
    const bool b680 = f.bme680On && f.bme680Found;
    const bool aht = f.aht20On && f.aht20Found;
    const bool sht = f.sht21On && f.sht21Found;
    const bool bmp3 = f.bmp3On && f.bmp3Found;
    in.hasT = bmx || b680 || aht || bmp3;
    in.hasH = (bmx && f.bmxIsBme) || b680 || aht || sht;
    in.hasP = bmx || b680 || bmp3;
    in.hasT2 = sht || f.oneWireOn;
}
```

`execSens` keeps its `unsupported` return when all four are false. Match the surrounding code style
(4-space indent, comment density of `rm_format.h`).

### Regression tests (`test/test_rm_format/test_main.cpp`)

Each case asserts the four `has*` bits and, where useful, the formatted `rmFmtSens` line.

1. BMP flag, found, chip BME280 → `t h p` present (**the field bug; fails before, passes after**).
2. BME flag, found, chip BMP280 → `h=-`, t and p present (mirror defect C7).
3. BMP flag, found, chip BMP280 → `h=-`, t and p present.
4. BMP flag, not found, nothing else → all absent (execSens: `unsupported`).
5. Flags off, `bmxFound` true (stale) → BMx contributes nothing.
6. `bmxIsBme` true, not found → h absent.
7. BMP flag + BMP280 + AHT20 found → h present (via AHT20).
8. BMP3 found + BMP280 → p present, t present.
9. BMP3 alone found → t and p present (D3).
10. AHT20 / SHT21 / BME680 flag on but not found → their fields absent (D2).
11. BME680 found, BMx flags and `bmx_found` false (`bmx280.cpp:121-126` clears all three) → t h p
    from BME680.
12. 1-Wire on → t2 present only.

Because the old logic lives in `execSens` (not native-compiled), the "fails before" proof for case 1
is: write the test against a helper that reproduces the current expression first, see it fail,
then switch to the fixed helper. Record both runs in the wave summary.

### Scoped verification for the implementer

- `pio test -e native_rm_format` (never bare `pio test`; one pio process at a time).
- Build `heltec_wifi_lora_32_V3`, one `ENABLE_BMX280_DISABLED` env (`heltec_t114` or
  `t5_epaper`), and `wiscore_rak4631` (needs Rosetta for the nRF52 toolchain).

## W2: gate, bench, docs, commit (orchestrator)

1. Read the W1 diff. Re-run `tools/regression.sh --stage 1,2` yourself.
2. Advisor pass (fresh Opus, medium) on the diff against the acceptance criteria below.
3. Bench (needs a bench node with a BMP280 or BME280; check which node carries one, else
   operator question): flash, run `identity_guard.py`, issue `sens` over RM (`tools/remote_cmd.py`)
   or read the 2323 console. Expect `h=<value>` for a BME280 under either flag, `h=-` for a BMP280.
   If no bench node has a BMx280, the bench step is skipped and that is reported.
4. Docs: SENS-01/02 in `docs/architecture/08-defect-catalogue.md`, SENS-03 in `docs/BACKLOG.md`,
   `docs/RESUME.md` line, summary file under `tools/bench/runs/harness/` if the bench ran.
5. Commit on `fork-dev`, explicit paths. No upstream PR (RM is fork-only).
6. Reply to the reporter: the node was right; next release fixes the remote card; until then
   `--bme on` is not a workaround if the chip sits at 0x77 (it would probe 0x76 and fail).

## Acceptance criteria

- BME280 enabled via `--bmp on` → `sens` reports `h=<value>`.
- BMP280 enabled via `--bme on` → `sens` reports `h=-`.
- BMx enabled but not detected → BMx contributes no field; `unsupported` if nothing else.
- AHT20/SHT21/BME680/BMP3 fields are claimed only when the sensor is detected; BMP3 now claims t.
- No change on boards without a sensor (`err unsupported` stays).
- All 5 `ENABLE_BMX280_DISABLED` variants still link (at least one built in the gate).
- `native_rm_format` green with the new cases; `docs/test-suite-map.md` counts match.
- Wire format unchanged; McApp needs no change.
