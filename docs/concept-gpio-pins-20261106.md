# Concept: ESP32 hardware pins on the Pins page and over RM1

**Plan for the next firmware batch, after the current release. Nothing here changes the frozen firmware `86fb4a7a`.** The operator took all five decisions on 2026-11-06. It runs as six waves and ends in one CI build, one bench sitting and an overnight soak.

Status 2026-11-07: in implementation, see [campaign-gpio-01-20261107.md](campaign-gpio-01-20261107.md). Backlog item `GPIO-01`. Operator change 2026-11-07: no bench sitting and no soak; OMs test it in the field. The W6 bench steps below are therefore not run.

## Key facts

- **The gap:** no web page and no RM1 command can switch a free ESP32 GPIO high or low.
  - The RM1 "Output pin" control (`src/web_functions/web_rm_page.cpp:94`) sends `setout <a0..b7>`, which addresses MCP23017 bits, not GPIOs (`src/rm_commands.h:49`, `src/command_functions.cpp:5057`).
- **Already there, web only:** four settings choose which GPIO a function uses, spread over Setup (`src/web_functions/web_functions.cpp:3447-3498`): 1-Wire, user button, analog, anemometer.
  - RM1 cannot read or change them. No RM source file mentions them.
- **Already there on the Pins page:** board LED, charge LED and, with an expander, MCP23017 level and direction (`tools/webgui_pins_test.js:204`).
- **The real risk is a dead node:** the wrong pin can stop the radio, the display or the boot. The RAK brick fixed today (`e7a6fbeb`) came from a stored pin setting, and over RM1 nobody is at the node to recover it.
- **A good start exists:** `wxWindPinOk()` (`src/command_functions.cpp:524`) already refuses radio, Ethernet, SD, I2C, GPS, button and flash pins from the variant macros, but only for the anemometer and only under `ENABLE_WIND`.

## Goal

Two features, built on one shared pin policy:

| Part | What the operator gets                                                                 | Risk   |
| ---- | -------------------------------------------------------------------------------------- | ------ |
| A    | All pin assignments on one page, the same fields readable and settable over RM1        | Medium |
| B    | A per-board list of free GPIOs with a switch and a live level, on the web and over RM1 | High   |

A ships without B if B runs into trouble. B does not ship without A's pin policy.

**Out of scope:** PWM, analog output, I2C or SPI scanning, automation rules, MCP23017 changes, upstream PRs until the fork has run it.

## Design

### One pin policy for every path

New pure header `src/pin_policy.h`, without Arduino, host-testable. Every path asks it: console, web, RM1 and the load-time sanitizer.

| Class     | Meaning                                                                      | Output | Assign |
| --------- | ---------------------------------------------------------------------------- | ------ | ------ |
| Forbidden | Radio, SPI, flash, PSRAM, USB, console UART, I2C, GPS, display, Ethernet, SD | no     | no     |
| Strapping | Decides how the chip boots, for example GPIO 0, 2, 12, 15 on classic ESP32   | no     | no     |
| Input     | Input-only pins, GPIO 34-39 on classic ESP32                                 | no     | yes    |
| Claimed   | In use by an enabled function: 1-Wire, button, analog, anemometer, LED       | no     | owner  |
| Free      | Everything else the chip has                                                 | yes    | yes    |

- **Default is refuse:** a pin not known to be free is forbidden. A new board shows no free pins until its table is reviewed.
- **Boards:** `wxWindPinOk()` moves here and stays the base. The classes above are the target; which macros cover which board is the job of W0.
- **nRF52:** `mcPinReservedNrf52840()` (`src/settings_sanitize.h`) stays and joins the policy for part A. Part B is ESP32 only, as requested. RAK, T114 and T-Echo answer "not on this board".

### Part A: pin assignments

- **Web:** the Pins page gets a section with the four assignments, using the existing `setparam` names (`onewiregpio`, `buttongpio`, `angpio`, `windpin`). The Setup fields stay, so nothing moves for existing users.
- **New read endpoint:** `GET /pinassign`, separate from `/pinstatus`, whose buffer is sized to its own worst case (`src/pin_status_json.h:36`).
- **Console:** existing commands stay. They get the policy check, because today only the anemometer checks a full list and only nRF52 refuses reserved pins for button and analog.
- **RM1:** new `pins` command, reading without an argument and writing as `pins <ow|btn|an|wind> <n>`.
- **Whether a change needs a restart:** W0 records it per function, and the page says so per field.

### Part B: free GPIO control

- **Console:** `--gpio list`, `--gpio <n> on|off`, `--gpio <n> in`, `--gpio <n> release`.
- **Master switch:** `--gpiocontrol on|off`, default off. Off means no pin is touched at boot and every write is refused.
- **Web:** table on the Pins page: pin, class, owner, direction, level, switch. Polling every 5 s only while the page is open, like today (`src/web_functions/web/mc.js:232`). Forbidden pins are listed with the reason, so the page explains itself.
- **Web switch for the master (added 2026-11-07, operator decision):** `/setparam/?gpioctl=on|off`, X-MC-gated, persisted, with an off/on button pair on the Pins page. RM1 and the config import still cannot turn it on; a chained `--gpiocontrol` in any value stays refused.
- **Web guard:** writes need `X-MC: 1` like update actions (`src/web_functions/web_guard.h:786`), so a foreign page cannot toggle a pin.
- **RM1:** new `gpio <n> <on|off|in>`, plus `gpio` alone to read. It works only when `--gpiocontrol` is on, and a remote command cannot switch the master on.
- **After a reboot:** every pin returns to input, because levels stay in RAM (decision 1).

### RM1 length budget (R4)

Worst case from `REVIEW.md` section 1.4: `6 + (9 + 4 x 10 + 1) + 9 + 1 + text + 4 <= 245`, so the text is at most 175 bytes.

- **Frame overhead:** `RM1 4294967295 ok ` is 18 bytes and ` <16 hex tag>` is 17, 35 bytes in total. That leaves **140 bytes** for the result.
- **`pins` reply:** `ow=48 btn=48 an=48 wind=48` is 26 bytes.
- **`gpio` reply:** two 49-bit masks as 13 hex digits each, `o=<13> l=<13>`, is 31 bytes. Pin lists are not sent as text.
- **Command direction:** `RM1 4294967295 gpio 48 off <tag>` is 43 bytes.

### Settings and contracts (R3)

| Surface         | Part A                                       | Part B                                                                                                                                                   |
| --------------- | -------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Settings struct | no new field                                 | `node_gpioctl` only, levels stay in RAM                                                                                                                  |
| BLE settings v1 | unchanged, frozen                            | unchanged, not added                                                                                                                                     |
| BLE JSON, app   | unchanged                                    | unchanged                                                                                                                                                |
| Config backup   | existing keys                                | not exported: `node_gpioctl` is persisted only (`SETTINGS_PERSIST_WX`), so an imported backup never switches gpio control on (decision 4), changed in W3 |
| Displays        | unchanged                                    | unchanged                                                                                                                                                |
| On-air format   | RM1 DM text only                             | RM1 DM text only                                                                                                                                         |
| McApp, MCProxy  | new RM1 names are unknown there, no breakage | same                                                                                                                                                     |

New settings go through `src/meshcom_settings.h`, `src/settings_schema.h` and `test_command_setters`, as `node_wind_pin` did (`src/settings_schema.h:137`).

### Resource limits

Measured against the final release image, on a classic ESP32 because that is the tight one:

- **T-Beam static RAM:** at most 512 B more. The idle heap was already the release blocker this time (`AU-23`).
- **Stack:** no JSON buffer on a task stack above 1 KB.
- **RAK flash:** part A must fit below 84 % (82.4 % at W2 of the release gate).
- **Web:** the endpoints run only while the Pins page is open.

## Decisions

Taken by the operator on 2026-11-06, as recommended.

| #   | Question                                              | Decision                                                                                                      |
| --- | ----------------------------------------------------- | ------------------------------------------------------------------------------------------------------------- |
| 1   | Do output levels survive a reboot?                    | No. A saved level on the wrong pin can stop a boot, and RAM only avoids it                                    |
| 2   | Should RM1 switch free GPIOs at all?                  | Yes, only with the master switch on and RM1 enabled                                                           |
| 3   | Should RM1 change pin assignments?                    | Yes, policy-checked, with a confirm step in the web RM page; button excluded over RM, W5 advisor 2026-11-07   |
| 4   | Master switch default for free GPIOs?                 | Off. An update must not make any node's pins remotely switchable                                              |
| 5   | Input pins: show a live level, or only the direction? | Show the level, sampled in the 5 s poll                                                                       |
| 6   | Web switch for the master?                            | Yes, since 2026-11-07 (operator decision): X-MC-gated `gpioctl`; RM and config import still cannot turn it on |

## Waves

Run with `/orchestrate-waves`, under the agreed flow (`docs/campaign-release-gate-20261106.md`): host tests and targeted local builds first, CI only when the batch is complete. Implementers are Sonnet medium, W0 is Haiku and a Sonnet analysis writer at high, advisors are Opus medium. Time and cost are rough estimates without a measurement behind them.

| Wave | Work                                                  | Who                    | Time   | Cost |
| ---- | ----------------------------------------------------- | ---------------------- | ------ | ---- |
| W0   | Per-board pin table, setter audit, restart needs      | scouts, 1 analysis     | 30 min | $3   |
| W1   | `pin_policy.h`, sanitizer and setter checks           | 1 implementer, advisor | 30 min | $5   |
| W2   | Part A: `/pinassign`, Pins page section               | 1 implementer, advisor | 30 min | $5   |
| W3   | Part B: `--gpio`, master switch, runtime              | 1 implementer, advisor | 30 min | $5   |
| W4   | Part B: web table, guard, `/gpiostatus`               | 1 implementer, advisor | 30 min | $5   |
| W5   | RM1 `pins` and `gpio`, RM page, sender tool, vectors  | 1 implementer, advisor | 40 min | $6   |
| W6   | Gate, all default envs, CI build, bench sitting, soak | orchestrator, operator | night  | $5   |

### File ownership

W1 to W5 run one after the other, because each builds on the one before, so no two writers overlap.

| Wave | Exclusive files                                                                                                                       |
| ---- | ------------------------------------------------------------------------------------------------------------------------------------- |
| W0   | `docs/gpio-pin-map.md`, read-only otherwise                                                                                           |
| W1   | `src/pin_policy.h`, `src/settings_sanitize.h`, `src/command_functions.cpp`, `test/test_pin_policy/`, `platformio.ini`                 |
| W2   | `src/web_functions/web_functions.cpp`, `web_setup.cpp`, `web/mc.js`, `tools/webgui_pins_test.js`, `test/test_pin_status/`             |
| W3   | `src/gpio_control.*` (new), `src/command_functions.cpp`, `src/meshcom_settings.h`, `src/settings_schema.h`, `test/test_gpio_control/` |
| W4   | `src/web_functions/web_functions.cpp`, `web_guard.h`, `web/mc.js`, `tools/webgui_pins_test.js`                                        |
| W5   | `src/rm_commands.h`, `src/rm_exec_*`, `src/rm_validate.h`, `web_rm_page.cpp`, `web/mcrm.js`, `tools/remote_cmd.py`, its vectors       |

### Tests

- **`test_pin_policy`:** every class on every board table, and a new board without a table yields no free pin. This is the regression test for the RAK brick class.
- **`test_gpio_control`:** master off refuses everything, reboot state is input, claimed pins refuse, release returns the pin.
- **`webgui_pins_test.js`:** table states, refusal text, `X-MC` on every write.
- **`remote_cmd_vectors.json`:** new vectors; the firmware host test reproduces each tag. The longest reply is pinned against the 140 byte limit.
- **Builds:** all default envs (R2). Part B code must compile away on nRF52, checked with a symbol scan of the image.

### Bench sitting, on the one CI image

Needs the operator for the wiring and the phone.

1. Identity guard on all four nodes, TX at 2 dBm or less, no broadcast.
2. **Master off after update:** no pin changes, all writes refused, on web and RM1.
3. **Loopback:** a jumper from one free pin to another on the T-Beam and Heltec. Switch one as output and read the other as input, 10 of 10, on web and RM1.
4. **Forbidden pins:** radio chip select, flash and a strapping pin refused on all paths, and the node keeps receiving.
5. **Assignment:** change the anemometer and button pins through RM1, read them back, reboot, confirm they hold.
6. **Reboot:** outputs return to input.
7. **Heap:** T-Beam idle with the Pins page closed, compared with the release image. Overnight soak last.

## What can go wrong

- **A board table is wrong and marks a used pin free:** default-refuse and a review per board limit this. The T-Deck has few free pins and may offer none, which is fine.
- **A claimed pin is enabled later:** the function must win. Enabling 1-Wire on a pin held as output releases that pin first, tested in W3.
- **External wiring:** a switched pin drives whatever is attached. The page says so once, near the master switch.
- **Boards not on the bench:** only T-Beam, T-Deck Plus, Heltec V3 and RAK can be tested. Part B stays off on other boards until someone checks their table.
- **RM1 on a remote node:** a wrong but allowed assignment can stop a sensor, and only a further RM1 command fixes it. Forbidden pins cannot be reached, so radio and boot stay safe.

## Not checked yet

- **Per-board pin tables:** this plan names classes, not verified pins per board. That is W0.
- **Setter behaviour:** the policy gap for 1-Wire, button and analog on ESP32 comes from a search, not an audit. W0 confirms it.
- **Current RAM and flash room:** the limits above are targets, to be checked against the release image.
