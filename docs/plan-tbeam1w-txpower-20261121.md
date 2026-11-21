# Plan: T-Beam 1W TX power, boot and runtime paths set the PA differently (P-0030 case 2)

Date: 2026-11-21. Branch `fork-dev` at `067d29cd`. Problem file `P-0030` (RCA at pin `96aaa67c72`). Backlog id
`TBEAM1W-PWR-01`. Nothing implemented in this session.

## 1. Scope: only case 2 is open

| P-0030 case                                        | State at HEAD | Basis                                                                                                                                                                                                               |
| -------------------------------------------------- | ------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1. Reported value clamped to `TX_OUTPUT_POWER` (8) | fixed         | `a672d18b` (2026-04-03) and `546ad011` (2026-04-18) are ancestors of HEAD and not reverted; `getPower()` (`src/lora_setchip.cpp:165-178`) clamps only to `TX_POWER_MAX` / `TX_POWER_MIN`. Fixed before the pin.     |
| 2. Boot and runtime paths set the PA differently   | open          | this plan                                                                                                                                                                                                           |
| 3. Web client shows 8 dBm on the node info page    | fixed         | same cause as case 1 (the page prints `getPower()`); fixed with case 1 before the pin.                                                                                                                              |
| 4. Output flat or erratic on air                   | undetermined  | PA saturation on 2S 18650 is a PA property (P-0030 evidence 2806); only a conducted measurement can tell, see section 6. This plan does not promise a flat curve, it removes the boot-versus-runtime inconsistency. |

Cases 1 and 3 get no work and no test here (a `getPower()` regression test is optional, see section 8).

## 2. Symptom

After boot the node configures the SX1262 PA with the datasheet values (`paDutyCycle` 0x04, `hpMax` 0x07, `power`
as is). After any `--txpower`, `--bw`, `--sf`, `--cr`, `--country` or other change that ends in
`lora_setchip_meshcom()` it reconfigures with RadioLib's `paOptTable` (duty cycle and `hpMax` per dBm). The same
setting therefore gives a different PA operating point before and after the first radio command. Reporters
(OE3WAS, MHX) saw dips and erratic low levels that match the table; the table is tuned for the 22 dBm-class bare
SX1262, not for the external PA of the 1W front end.

## 3. Root cause at HEAD

| Path                                                         | File:line at HEAD                                        | Call                                     |
| ------------------------------------------------------------ | -------------------------------------------------------- | ---------------------------------------- |
| Boot, all boards except T-Deck Pro (so the 1W)               | `src/esp32/esp32_main.cpp:2322` (`#else` of `:2321`)     | `radio.setOutputPower(tx_power, false)`  |
| Boot, T-Deck Pro (kept on the table since `593495c1`)        | `src/esp32/esp32_main.cpp:2316` (under `#if` at `:2315`) | `radio.setOutputPower(tx_power)`         |
| Runtime, every ESP32 board (`#elif defined (ESP32)`, `:514`) | `src/lora_setchip.cpp:575`                               | `radio.setOutputPower(tx_power)` (table) |

- RadioLib 7.6.0 (`platformio.ini:1743`; `.pio/libdeps/E22_XML-DevKitC/RadioLib/library.json` 7.6.0):
  `SX1262::setOutputPower(int8_t power)` is `setOutputPower(power, true)`; with `optimize == true` `paVal`,
  `paDutyCycle` and `hpMax` come from `paOptTable[power + 9]`, with `false` they are `power`, 0x04, 0x07
  (`src/modules/SX126x/SX1262.cpp:144-156`). `SX1268` has the same pair (`SX1268.h:120,128`).
- History: the one-argument runtime call is from `5a8b3155` (2025-02-12). The boot path went to `false` in
  `b8d8b79d` (2026-06-06, first in `v4.35p.06.07`) and `593495c1` (2026-06-07) added the T-Deck Pro exception. All
  three are ancestors of HEAD and not reverted. `git log --oneline 96aaa67c72..HEAD -- src/lora_setchip.cpp
src/esp32/esp32_main.cpp variants/LilyGo_T-Beam-1W` shows only `ce05f38c` (APPLINK-01), which does not touch
  the PA lines (checked with `git show ce05f38c -- <both files>`, no `power` line). Line numbers moved by +8 in
  `esp32_main.cpp` since the pin (`:2314` is now `:2322`); `lora_setchip.cpp:575` is unchanged.
- The runtime path is `lora_setchip_new()` (`src/lora_setchip.cpp:497`), reached from `lora_setchip_meshcom()`
  (`:375`) and `lora_setchip_aprs()` (`:491`); callers `src/lora_functions.cpp:3165`, `:3930`, `:3973`.
- The power value is the same on both paths (`node_power` clamped to `TX_POWER_MAX` / `TX_POWER_MIN`; boot at
  `esp32_main.cpp:2290-2302`, runtime through `getPower()`), so only the PA tuning differs.

## 4. Platforms

- ESP32 only. The T-Beam 1W is `LilyGo_T-Beam-1W` (ESP32-S3, `variants/LilyGo_T-Beam-1W/configuration.h:16-17`
  `RADIO_TYPE_STR "SX1262"`, `USING_SX1262`, `TX_OUTPUT_POWER 8` at `:122`, `RADIO_CTRL` 21, `RADIO_LDO_EN` 40, no PA
  enable pin).
- The runtime call is compiled for every ESP32 board with a RadioLib radio, so the change is not 1W specific: every
  SX1262 and SX1268 board (Heltec V3/V4, Wireless Paper, E22, T-Beam SX1262/SX1268, ...) currently also flips to the
  table after its first radio command. Moving the runtime path to `false` makes all of them equal to their own
  boot behaviour.
- SX127x boards (`SX1278`): the second parameter is `forceRfo` (`SX1278.h:227`), default `false`. Passing `false`
  is identical to the one-argument call, and the boot path already does so. No behaviour change.
- SX1276 boards (`src/esp32/esp32_main.cpp:888`): inherit SX1278, the second argument is `forceRfo`, so `false` keeps
  today's behaviour.
- LLCC68 boards (`src/esp32/esp32_main.cpp:895`): inherit SX1262 and use `paOptTable` at runtime today, so they change
  behaviour with the fix exactly like the SX1262 boards; include one in the bench or say why not.
- nRF52 (RAK4631, T114, T-Echo): not affected. Boot (`src/nrf52/nrf52_main.cpp:1297-1302`) and runtime
  (`src/lora_setchip.cpp:336-338`, `:428-430`, `:445-447`) both go through `Radio.SetTxConfig(..., getPower(), ...)`
  of the Semtech driver; there is no `paOptTable` and no second code path.
- T-Deck Pro keeps the one-argument form on both paths (explicit exception, as today).

## 5. Fix design

One helper used by both call sites, so the two cannot drift again.

- New header-only `src/rf_pa.h` (pattern of `src/daylight.h`, `src/prod_lock.h`):
  `template <typename Radio> inline int16_t rfApplyOutputPower(Radio &r, int8_t power, bool optimize = RF_PA_OPTIMIZE)`
  returning `r.setOutputPower(power, optimize)`. `RF_PA_OPTIMIZE` is `true` when `BOARD_T_DECK_PRO` is defined, else
  `false`, overridable with `#ifndef` for the host test. The `BOARD_T_DECK_PRO` exception then lives in exactly one
  place.
- `src/esp32/esp32_main.cpp:2315-2327`: replace the `#if BOARD_T_DECK_PRO / #else` pair by one call
  `rfApplyOutputPower(radio, tx_power)`; keep the `== RADIOLIB_ERR_INVALID_OUTPUT_POWER` handling and the restart.
- `src/lora_setchip.cpp:575`: `rfApplyOutputPower(radio, tx_power)`. Include `rf_pa.h` next to `radio_units.h`.
- Reuse: no existing helper wraps `setOutputPower` (`grep -rn setOutputPower src` finds the five call sites above,
  plus `src/t-deck-pro/peri_lora.cpp:76` and `src/t5-epaper/peri_lora.cpp:110`, which are board-private init code
  with their own fixed values and stay as they are). `getPower()` and `resolve_tx_power()`
  (`src/settings_sanitize.cpp:164`) are unchanged.
- Out of the fix: the tx-power value logic, the T-Deck Pro and T5 init files, any RadioLib change.

## 6. Test

Host test, new Unity env `native_rf_pa` (own env like `native_daylight`, `platformio.ini:824`:
`test_filter = test_rf_pa`, `-I src`, `-D NATIVE_BUILD=1 -D UNIT_TEST=1`, `test_build_src = no`), new file
`test/test_rf_pa/test_rf_pa.cpp`. Add the suite to `docs/test-suite-map.md` and the regression runner list as the
other native envs are.

| Test                                           | Assertion                                                                                                                                                                                                                                                      | Fails today because                                                                                                                                                                           |
| ---------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `test_helper_passes_optimize_false_by_default` | A mock radio with `setOutputPower(int8_t, bool)` records the arguments; `rfApplyOutputPower(mock, 12)` records `(12, false)`.                                                                                                                                  | `rf_pa.h` does not exist (compile error counts as the before-run); the semantic is pinned by the next two tests too.                                                                          |
| `test_helper_tdeckpro_keeps_table`             | With `RF_PA_OPTIMIZE` defined true before the include (second translation unit or explicit `optimize` argument `true`) the mock records `(p, true)`.                                                                                                           | same                                                                                                                                                                                          |
| `test_both_paths_use_the_helper` (source pin)  | `slurp("src/lora_setchip.cpp")` and `slurp("src/esp32/esp32_main.cpp")` (pattern of `test/test_ping_budget/test_ping_budget.cpp:47-57`, `__FILE__` relative) each contain `rfApplyOutputPower(radio,` and the text `radio.setOutputPower(` appears in neither. | `src/lora_setchip.cpp:575` contains `radio.setOutputPower(tx_power)` and `src/esp32/esp32_main.cpp:2316` and `:2322` call it directly. This one is a real red before-run on unchanged source. |

The helper test alone cannot fail on the runtime defect (the defect is a call site, not the helper); the source pin
is the regression guard for the two call sites and is deliberately text-based, the same technique as
`test_ping_budget` (`test_both_loops_use_the_helpers`). Board gate after the change:
`pio run -e LilyGo_T-Beam-1W` plus one SX1262 board (`heltec_wifi_lora_32_V3`) and one SX127x board (`ttgo_tbeam`) to prove the
template compiles against `SX1262`, `SX1268` and `SX1278` (one `pio` process at a time).

## 7. Bench (needs a T-Beam 1W; not automated)

The test above proves consistency, not RF output. The question "is the curve flat after the fix" needs a
conducted measurement.

- Board: one T-Beam 1W. Run `tools/bench/identity_guard.py` first, `lsof <port>` before opening the port (opening
  the port reboots the ESP32).
- Setup: SMA to a 50 ohm path only: attenuator of at least 40 dB into a power meter, or a dummy load with a coupler.
  Never transmit unloaded (the 1W PA can be damaged), no radiating antenna.
- Bench rule conflict: the bench rule limits TX power to 2 dBm. A curve over the range needs settings up to 22 dBm,
  so those steps are allowed only conducted into the attenuator and dummy load with no antenna connected, and only
  with the operator's explicit OK for that session. Anything radiated stays at or below 2 dBm. Own call sign only,
  group `TEST` only.
- Procedure: boot, `--txpower N` for N in {-9, -5, 0, 2, 5, 8, 10, 12, 15, 18, 22}; for each, trigger one beacon to
  `TEST` and read the power meter; repeat the whole series once after a reboot with no radio command in between and
  once after `--txpower` (the two paths). Expected after the fix: the series after reboot and after `--txpower` are
  identical within meter tolerance (before the fix they differ because of the table). Whether the curve is also
  monotonic and flat at the top is the answer to case 4 and is recorded, not asserted.
- Supply: record the supply voltage (2S 18650 about 7.5 V, USB); the top end saturates with supply (P-0030, 2806).

## 8. Upstream impact

- Yes, affected. `git show upstream/dev:src/lora_setchip.cpp` has `radio.setOutputPower(tx_power)` at `:575`;
  `git show upstream/dev:src/esp32/esp32_main.cpp` has the boot calls at `:2221` (one argument) and `:2227`
  (`, false`). Upstream issue icssw-org/MeshCom-Firmware#907 (RadioLib lookup table, closed 2026-06-07) is the
  matching report.
- PR shape: the minimum port is the one-line change at `lora_setchip.cpp:575`; the helper header is the larger
  variant. Cut from `upstream/dev`, German body, per `CLAUDE.md`. Nothing is sent from this plan.
- Optional extra: a native `getPower()` test (P-0030 part 1: `node_power` 12, `TX_POWER_MAX` 22 gives 12). Needs a
  seam because `getPower()` reads `meshcom_settings` directly; not part of this fix.

## 9. Risks

- Behaviour change on every ESP32 SX1262/SX1268 board after the first radio command: the output moves from the table
  to the datasheet setting, i.e. to what the node already does after a reboot. Nodes that were tuned (or measured) in
  the "table" state after `--txpower` will measure differently. Release note needed.
- Regulatory: EIRP could change at a given dBm setting on boards whose PA gain differs between the two modes.
  Check at least one SX1262 board (Heltec V3, `heltec_wifi_lora_32_V3`) conducted before release.
- T-Deck Pro must keep `true` on both paths; covered by `test_helper_tdeckpro_keeps_table` and a
  `t_deck_pro` env build.
- LLCC68: same behaviour change as SX1262 (runtime path leaves `paOptTable`).
- SX127x (SX1278, SX1276): the second argument is `forceRfo`; `false` equals the default. A reviewer may object to passing a bool
  with a different meaning; the helper comment must say so.
- The flat-curve claim of the reporters is unproven; the plan fixes consistency only.

## 10. Evidence checklist

- `a672d18b`, `546ad011`, `b8d8b79d`, `593495c1` are ancestors of HEAD (`git merge-base --is-ancestor`, all rc 0);
  `git log --all -i --grep=revert -- src/lora_setchip.cpp` shows only `136dacdd` (RF-04 guard band, unrelated).
- Every path:line in section 3 was read at HEAD (`067d29cd`).
- Platforms: ESP32 read (boot `esp32_main.cpp`, runtime `lora_setchip.cpp`); nRF52 read (`nrf52_main.cpp:1297-1302`,
  `lora_setchip.cpp` nRF52 branches); both `SetTxConfig`, no split.
- Not verified: RF output, RadioLib behaviour at the 1W supply, whether any T-Beam 1W unit is free for the bench.
