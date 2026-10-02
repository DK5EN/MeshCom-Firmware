# 04 — Complexity & Duplication

> **Where is the code tangled, duplicated, or genuinely unmaintainable?**

Metrics from `tools/arch_metrics.py` and `tools/arch_duplication.py` over `src/`, excluding
fonts, images and map assets. 1,032 functions parsed.

`CC` below is a **heuristic decision count** (`if`/`for`/`while`/`case`/`catch`/`&&`/`||`/`?`
plus one), not a compiler-grade cyclomatic complexity. `NEST` is maximum brace depth
including the function body. `PP` is preprocessor branches inside the function. Use these
to rank, not to quote.

## Size distribution

| Function length | Count | Share |
| --------------- | ----: | ----: |
| > 100 LOC       |    81 |  7.8% |
| > 200 LOC       |    36 |  3.5% |
| > 400 LOC       |    11 |  1.1% |
| > 800 LOC       |     6 |  0.6% |

92% of functions are under 100 lines. The problem is not diffuse — it is concentrated in
**six functions that together carry the entire firmware**.

## The six

|   LOC |  CC | NEST |  PP | Function                                                     |
| ----: | --: | ---: | --: | ------------------------------------------------------------ |
| 4,916 | 606 |    6 |  91 | `commandAction()` — `src/command_functions.cpp:194`          |
| 1,947 | 263 |    9 |  64 | `esp32loop()` — `src/esp32/esp32_main.cpp:1743`              |
| 1,233 | 185 |    8 |  24 | `nrf52loop()` — `src/nrf52/nrf52_main.cpp:1102`              |
| 1,181 |   4 |    2 |   0 | `setDisplayLayout()` — `src/t-deck/lv_obj_functions.cpp:448` |
| 1,129 |  60 |    3 | 107 | `esp32setup()` — `src/esp32/esp32_main.cpp:601`              |
|   968 | 126 |   13 |  14 | `OnRxDone()` — `src/lora_functions.cpp:288`                  |

Runners-up worth naming:

| LOC |  CC | NEST | Function                                                     |
| --: | --: | ---: | ------------------------------------------------------------ |
| 698 |  42 |    6 | `nrf52setup()` — `src/nrf52/nrf52_main.cpp:403`              |
| 597 | 155 |    4 | `webSetup_setParam()` — `src/web_functions/web_setup.cpp:18` |
| 466 |  75 |    5 | `decodeAPRSPOS()` — `src/aprs_functions.cpp:531`             |
| 466 |  47 |    5 | `sendDisplayText()` — `src/loop_functions.cpp:2034`          |
| 419 |  51 |   11 | `getUDP()` — `src/nrf52/nrf_eth.cpp:209`                     |
| 395 |  40 |    7 | `readPhoneCommand()` — `src/phone_commands.cpp:208`          |
| 378 |  57 |    5 | `decodeAPRS()` — `src/aprs_functions.cpp:122`                |

### `commandAction()` — 4,916 lines, 218 dispatch arms

The single worst maintenance object in the tree. It is one flat `if / else if` chain:

```c
if(commandCheck(msg_text+2, (char*)"utcoff") == 0)
{
    sscanf(msg_text+9, "%f", &meshcom_settings.node_utcoff);
    ...
}
else
if(commandCheck(msg_text+2, (char*)"postime ") == 0)
{
    sscanf(msg_text+10, "%d", &meshcom_settings.node_postime);
    ...
}
else
...  // 216 more
```

Three compounding problems:

1. **Hardcoded argument offsets.** Every arm re-derives where its parameter starts:
   `msg_text+9` for `--utcoff`, `msg_text+10` for `--postime `, `msg_text+11` for
   `--compress `, up to `msg_text+20`. 15 distinct offsets. Rename a command, or add a
   space, and you must hand-recompute the offset — with no compiler or test catching it.
   One arm at `:280` already carries a comment explaining the arithmetic, which is the
   tell.

2. **Sequential matching.** 218 `strncmp`-equivalents run in declaration order on every
   command from serial, BLE and the phone app. Ordering is load-bearing:
   `--setinfo off` must be tested before `--setinfo`, and nothing enforces that.

3. **No isolation.** Arms write directly into `meshcom_settings` and ~60 different globals.
   There is no way to exercise one command without linking the whole firmware.

**The fix is mechanical and high-value:**

```c
struct Command {
    const char *name;          // "postime"
    uint8_t     argc;          // expected args
    void      (*handler)(const char *args, bool ble);
};
static const Command COMMANDS[] PROGMEM = { ... };
```

The offset becomes `strlen(name)` computed once. Dispatch becomes a loop (or a sorted
binary search). Each handler becomes independently testable. This deletes roughly 4,900
lines of branching and is the first thing in the codebase that could get a real unit test.

### `esp32loop()` / `nrf52loop()` — the duplicated scheduler

`esp32loop()` is 1,947 lines of `if (millis() - x_timer >= interval)` blocks: RTC, NTP,
GPS, WiFi ping, softserial, BLE connect/disconnect/queue, phone commands, MCP refresh,
display refresh, position beacon, trickle-HEY, telemetry, display timeout, auto-reboot,
battery, heap monitor, OneWire, BMx280/AHT20/SHT21, BMP390, MCU811, INA226 — then the
radio state machine.

`nrf52loop()` is the same scheduler, written again, 1,233 lines, for the boards that use
SX126x-Arduino instead of RadioLib. The clone detector finds **46 duplicated 12-line
windows** between the two files.

This is a direct consequence of there being no radio interface ([01,
§3](01-system-overview.md)). Every feature added to the ESP32 loop has to be ported by
hand to the nRF52 loop, or the nRF52 boards quietly fall behind.

### `OnRxDone()` — 968 lines, nesting depth 13

The RX interrupt-callback path. Deepest nesting in the codebase. It performs dedup,
decode, mheard update, routing, display, BLE enqueue, UDP enqueue and forwarding decisions
inline. Because it runs off the radio callback, everything it touches is shared with the
main loop — and only 12 of the 423 globals are atomic.

This function is the highest-consequence code in the firmware (it is on the path of every
received packet from an untrusted RF source) and it is the least testable.

### `esp32setup()` — 107 preprocessor branches in 1,129 lines

Board bring-up. Roughly one `#if` per ten lines. The function that ships is a different
function on each of the 29 ESP32 environments. This is where a board descriptor table
would pay for itself most directly.

### `setDisplayLayout()` — 1,181 lines, CC 4

Included for contrast. It is enormous but nearly branchless — a flat sequence of LVGL
widget construction. Long, tedious, _not_ complex. Do not spend refactoring budget here;
it is generated-code-shaped and behaves like data. The same applies to `create9()`
(364 LOC, CC 8) and `create0()` (166 LOC, CC 8).

**Length alone is not the signal.** Rank by `LOC × CC × NEST`, not by LOC.

## Preprocessor density

| File                        | `#if`/`#ifdef`/`#else`/`#endif` |
| --------------------------- | ------------------------------: |
| `src/esp32/esp32_main.cpp`  |                             404 |
| `src/loop_functions.cpp`    |                             294 |
| `src/command_functions.cpp` |                             206 |
| `src/nrf52/nrf52_main.cpp`  |                             114 |
| `src/lora_functions.cpp`    |                             110 |
| `src/batt_function_old.cpp` |                             104 |
| `src/gps_functions.cpp`     |                             103 |

### `batt_function_old.cpp` is not dead code — it is half the fleet

The name invites deletion. It must not be deleted. `src/batt_function_old.cpp` (654 lines,
104 preprocessor branches) is wrapped in `#ifndef USE_NEW_BATT`, and
`src/batt_functions.cpp` in `#if defined(USE_NEW_BATT)`. The switch is per board, in
`variants/<board>/configuration.h`, and the migration is **half finished**:

| Path                          | Boards | Which                                                                                                                                                                                                                                                                                                |
| ----------------------------- | -----: | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `batt_functions.cpp` (new)    |     13 | `E22-DevKitC`, `E22_XML-DevKitC`, `E22_1262-DevKitC`, `E22_1262_S3…`, `E22_1268_S3…`, `esp32-loraprs-e22`, `LilyGo_T-Beam-1W`, `LilyGo_T3_S3_V1_3`, `t_deck`, `t_deck_plus`, `ttgo-lora32-v21`, `vision-master-e213`, `wireless-paper`                                                               |
| `batt_function_old.cpp` (old) |     17 | `heltec_wifi_lora_32_V2/V3/V4`, `heltec_wireless_stick`, `heltec_wireless_tracker`, `heltec_t114`, `ttgo_tbeam`, `ttgo_tbeam_SX1262/SX1268`, `ttgo_tbeam_supreme`, `t_echo`, `t_deck_pro`, `T-ETH-ELITE_1262`, `LilyGo_T_Connect_Pro`, `wiscore_rak4631`, `vision-master-e290`, `esp32-loraprs-ra01` |

Two of them (`vision-master-e290`, `esp32-loraprs-ra01`) have the `#define` present but
commented out, which reads as "tried, reverted".

The comment in every variant says _"kommt wenn alle Nodes umgestellt sind raus"_ — it goes
away once all nodes are switched. Two full battery/ADC implementations are therefore being
maintained in parallel, split roughly down the middle of the fleet, including across the
most-deployed boards (`heltec_wifi_lora_32_V3` old, `t_deck` new).

**Action: finish the migration, do not delete the file.** Concretely: pick the remaining
17 boards, port and verify battery curves on each, then remove `batt_function_old.cpp` and
the `USE_NEW_BATT` guard in one commit. Until that happens, every battery-related bug
report has to be triaged against two implementations first.

## Duplication

393 duplicated 12-line windows (overlapping — treat as a ranking signal, not a count).
The top pairs, verified by hand:

| Pair                                                                | Sizes                           | Differing lines (whitespace-normalised) | Assessment                                                                    |
| ------------------------------------------------------------------- | ------------------------------- | --------------------------------------: | ----------------------------------------------------------------------------- |
| `t-deck-pro/peri_gps.cpp` ↔ `t5-epaper/peri_gps.cpp`                | 417 / 379                       |                                      96 | **~80% identical.** Same GPS driver forked per board.                         |
| `t-deck-pro/ui_scr_mrg.c` ↔ `t5-epaper/scr_mrg.cpp`                 | 267 / 275                       |                                      29 | **~90% identical.** Same screen manager, one renamed `.c`→`.cpp`.             |
| `esp32/esp32_main.cpp` ↔ `nrf52/nrf52_main.cpp`                     | 3,980 / 2,697                   |                       46 cloned windows | The scheduler. See above.                                                     |
| `Platforms/VisionMasterE213/power_controls.cpp` ↔ `WirelessPaper/…` | 93 / 108                        |                                      49 | Three near-copies (E213, E290, WirelessPaper).                                |
| `nrf52/nrf_eth.cpp` ↔ `udp_functions.cpp`                           | 1,019 / 1,110                   |                       19 cloned windows | UDP protocol handling forked per MCU.                                         |
| `mheard_functions.cpp` ↔ `t-deck-pro/ui_deckpro.cpp`                | —                               |                       19 cloned windows | **Layering violation:** neighbour-table logic reimplemented inside a UI file. |
| `t-deck-pro/ui_deckpro.cpp` ↔ `t5-epaper/ui.cpp`                    | —                               |                       18 cloned windows | Two UI stacks, common ancestry.                                               |
| `loop_functions.cpp` self-duplication                               | 1291–1317, 1400–1426, 2549–2575 |                            3× ~27 lines | Same display block pasted three times.                                        |

### The pattern

Duplication is **not** random. It is almost entirely _"new board arrived, copy the closest
existing board's files and edit"_. `t5-epaper` was cloned from (or with) `t-deck-pro`;
`VisionMasterE213`/`E290` from `WirelessPaper`; the nRF52 tree from the ESP32 tree.

That is a rational move under deadline pressure with no abstraction available. It is also
why a bug fixed in `t-deck-pro/peri_gps.cpp` does not reach `t5-epaper/peri_gps.cpp`.

## Ranked remediation

| #   | Action                                                                                              | Deletes / unifies    | Effort | Risk                 |
| --- | --------------------------------------------------------------------------------------------------- | -------------------- | ------ | -------------------- |
| 1   | Merge `peri_gps.cpp` (t-deck-pro / t5-epaper) into one file + board `#define`                       | ~380 LOC             | S      | low                  |
| 2   | Merge `scr_mrg` (t-deck-pro / t5-epaper)                                                            | ~270 LOC             | S      | low                  |
| 3   | Merge the three `Platforms/*/power_controls.cpp`                                                    | ~140 LOC             | S      | low                  |
| 4   | Move mheard logic out of `ui_deckpro.cpp` back into `mheard_functions.cpp`                          | ~200 LOC             | S      | low                  |
| 5   | Replace `commandAction()` if-chain with a dispatch table                                            | ~4,900 LOC           | M      | med                  |
| 6   | Finish the `USE_NEW_BATT` migration on the remaining 17 boards, then delete `batt_function_old.cpp` | 654 LOC + 104 `#if`  | M      | med (needs hardware) |
| 7   | Extract a radio interface; unify `esp32loop`/`nrf52loop`                                            | ~3,200 LOC           | L      | high                 |
| 8   | Split `OnRxDone()` into dedup → decode → route → dispatch                                           | 968 LOC restructured | M      | high                 |
| 9   | Board descriptor table to drain `#ifdef` out of `esp32setup()`                                      | ~107 branches        | L      | med                  |

Items 1–4 are safe, mechanical, and can go upstream as small PRs today. Item 6 needs
hardware per board but no design work. Items 5, 7, 8 and 9 should not be attempted before
the characterization tests in [06 — Test Strategy](06-test-strategy.md) exist.
