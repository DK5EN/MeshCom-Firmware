# Bug report: web setup "Voltage" switch cannot be changed

Date: 2026-09-23
Status: root cause identified from code, not yet bench-reproduced, no fix applied
Affects: every board with the web server (ESP32 and RAK4631), upstream `dev` and fork, since `v4.35p.07.05`
Line numbers: branch `fork-neo-test` at `b221a57a` unless marked (up) for `upstream/dev`; `fork-main` differs by a few lines

## Summary

The web GUI switch "Voltage - show batt. voltage, not percent" can never change the setting. The web handler
sends the bare commands `--volt` / `--proz`, but since upstream commit `93d9fee6` (2026-07-03) the command
parser only accepts `--volt on|off` / `--proz on|off`. The command is rejected as "wrong command", the flag
does not change, and the web API answers 422 "Value could not be set".

## Field report

- Heltec V3: "Volt statt %" cannot be switched on.
- T-Beam Supreme: "Volt statt %" is on and cannot be switched off. Error text: "wrong command --volt" or
  "value could not be set".

Both symptoms are the same bug. Each node stays in whatever state it had stored in `node_sset` bit `0x0001`
before; the web switch can move it in neither direction. The board type is irrelevant.

## Root cause

1. Upstream commit `93d9fee6` ("Update command_functions.cpp", Kurt, 2026-07-03) changed the two parser
   rungs from bare names to on/off arguments and updated the help text accordingly:

   ```diff
   -    if(commandCheck(msg_text+2, (char*)"volt") == 0)
   +    if(commandCheck(msg_text+2, (char*)"volt on") == 0 || commandCheck(msg_text+2, (char*)"proz off") == 0)
   ...
   -    if(commandCheck(msg_text+2, (char*)"proz") == 0)
   +    if(commandCheck(msg_text+2, (char*)"proz on") == 0 || commandCheck(msg_text+2, (char*)"volt off") == 0)
   ```

   Fork location today: `src/command_functions.cpp:654` and `:670`.

2. The only internal caller was not updated. `src/web_functions/web_setup.cpp:174-185` still builds the old
   bare form:

   ```cpp
   if(setupData->paramValue.equals("on")){
       snprintf(message_text, sizeof(message_text), "--volt");
       ...
   } else {
       snprintf(message_text, sizeof(message_text), "--proz");
       ...
   }
   ```

3. A bare `volt` cannot match the rung `volt on`, because the input is shorter than the command name.
   - Upstream `commandCheck()` copies with `strncpy` (zero-padded), so `vmsg` is `"volt"` and
     `casecmp("volt", "volt on")` fails. This is deterministic, not stack-dependent.
   - Fork `commandMatches()` (`src/command_match.h`) rejects it explicitly: "A short input can never match a
     longer command."

4. The command falls through the whole ladder to the default branch, which prints `--wrong command --volt`
   (`src/command_functions.cpp:5616`).

5. `web_setup.cpp` then compares `bDisplayVolt` with the requested value. It is unchanged, so `returnCode` is
   `WS_RETURNCODE_FAIL`, the web server sends HTTP 422, and the GUI shows "Value could not be set". The
   returned value is the old state, so the switch snaps back.

The setting itself is loaded at boot from `meshcom_settings.node_sset & 0x0001`
(`src/esp32/esp32_main.cpp:856`, `src/nrf52/nrf52_main.cpp:590`). It changes only through the serial/BLE
command `--volt on|off` / `--proz on|off`, which still works.

## Scope

| Path                                | Status                                             |
| ----------------------------------- | -------------------------------------------------- |
| Web GUI switch "Voltage"            | Broken in both directions, all boards              |
| Serial / BLE `--volt on` / `off`    | Works                                              |
| Serial / BLE bare `--volt`/`--proz` | Rejected ("wrong command"), documented syntax only |
| Phone apps                          | Not checked (code not in this repo)                |

`web_setup.cpp:176/180` are the only places in `src/` that send a bare `--volt` / `--proz`.

First affected tags: `v4.35p.07.05`, then every later upstream and fork release.

## Workaround

Set the value over serial or BLE:

```
--volt on     show voltage
--volt off    show percent
```

## Proposed fix (not applied)

Minimal, upstream-first, in `src/web_functions/web_setup.cpp:174-185`: send the argument form, as the
`--gps`, `--ina226` and the other switch handlers already do:

```cpp
snprintf(message_text, sizeof(message_text), "--volt %s", setupData->paramValue.c_str());
commandAction(message_text, bPhoneReady);
setupData->returnCode = (bDisplayVolt == (setupData->paramValue.compareTo("on")==0))?WS_RETURNCODE_OKAY:WS_RETURNCODE_FAIL;
setupData->returnValue = bDisplayVolt?"on":"off";
```

`paramValue` is `on` or `off` from the switch; any other value is rejected by the parser and reported as 422,
which is the intended behaviour.

Regression test: a host-side lint that extracts every command string `web_setup.cpp` passes to
`commandAction()` and checks it against the parser rungs with `commandMatches()`. It fails on today's tree
and passes after the fix.

## Verification still owed

- Bench reproduction on Heltec V3 (DK5EN-93): toggle via web, observe `--wrong command --volt` on the
  console and HTTP 422.
- After the fix: toggle both directions on Heltec V3 and RAK4631, check display, web status and that the
  value survives a reboot.

## Same category: other producer/parser mismatches

Category: code that builds a command string and passes it to `commandAction()` (web setup, web function
calls, BLE config list, buttons, T-Deck / T-Deck Pro UI) drifting away from the parser rungs it relies on.

Method: a script extracted every internal `commandAction()` producer (166 on `fork-main`, 163 on
`upstream/dev`) and every parser rung and toggle-table row (342 / 331). It instantiated each format string
with `on` and `off` and matched it with the `commandMatches()` rule. It also compared the `#if` guards of
producer and rung. `fork-main` and `upstream/dev` give the same result. Pass-through producers (serial
input, BLE text, multi-command splitting) carry user input and are out of scope. The BLE `config_cmds` list
was checked by hand: all ten commands have rungs.

### A. Command has no rung on any board (same bug as `--volt`)

| Producer                     | Command sent       | Parser                                                                | Impact                                                                                                                                                              |
| ---------------------------- | ------------------ | --------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `web_setup.cpp:176/180`      | `--volt`, `--proz` | only `volt on\|off`, `proz on\|off` since `93d9fee6`                  | This report                                                                                                                                                         |
| `web_setup.cpp:166-171` (up) | `--small on\|off`  | rungs `small on/off` removed in `e837eedf` (v4.34w, Kurt, 2025-04-08) | Dormant. GUI switch is commented out (`//NOT USED`, `web_functions.cpp`), only a direct `/setparam/?small=on` API call reaches it: "wrong command" + 422. Dead code |

No other producer fails to match.

### B. Rung compiled out on some boards, but the web GUI still shows the control

The rung sits inside a feature guard; the web GUI element and the `web_setup.cpp` handler do not. On these
boards the switch produces `--wrong command ...` and HTTP 422 "Value could not be set", the same symptom
as the volt bug. Functionally nothing is lost (the feature is not built in), but the GUI offers a dead
control with a misleading error.

| Web control                                | Rung guard                      | Boards where it is dead (web server present)                                                                                                         |
| ------------------------------------------ | ------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------- |
| INA226 switch                              | `ENABLE_INA226`                 | Heltec V3, Heltec V4, Wireless Stick, Wireless Tracker, T-Deck, T-Deck Plus, T-Deck Pro, T-ETH-Elite, Vision Master E213, Wireless Paper, T5 e-paper |
| BMP280 / BME280 / BME680 / MCU811 switches | `ENABLE_BMX280` (all four)      | T-Deck Pro, Vision Master E213, Wireless Paper, T5 e-paper                                                                                           |
| GPS switch                                 | `ENABLE_GPS` or RAK/T114/T-Echo | Wireless Paper, T5 e-paper                                                                                                                           |
| 1-Wire switch + 1-Wire GPIO                | `OneWire_GPIO`                  | T-Connect Pro, T3-S3 V1.3, T-Beam 1W, T-Deck Pro, T5 e-paper                                                                                         |
| net console switch                         | `!DISABLE_NET_CONSOLE`          | E22_XML DevKitC                                                                                                                                      |

Notes:

- The Heltec V3 from the field report is affected by the INA226 row: switching INA226 on in the web GUI
  fails the same way. The variant disables INA226 on purpose ("I2C fault").
- If a node carries a stored bit for such a feature from an older build, the switch shows "on" and cannot
  be turned off. Same stuck pattern as the volt switch.
- AHT20, SHT21, analog and SSID/password are guarded in the GUI as well, so they are fine.
- Non-web producers with the same guard gap, log-only impact: `onebutton_functions.cpp:227/232` sends
  `--gps on|off` from a button gesture on Wireless Paper and T5 e-paper. `web_setup.cpp` `--button gpio`
  has no rung on T-Deck Pro.
- `--ota-update` (`web_nodefunctioncalls.cpp:32`) has an ESP32-only rung, but its GUI button is also
  ESP32-only, so it is not reachable on the RAK.

### C. Handler checks a flag the rung does not set

All `bool` flag handlers in `web_setup.cpp` compare the flag that the matched rung or toggle row writes.
The string and float handlers (`setcall`, `setname`, `maxv`, `utcoff`, network fields, ...) compare
setting values and were not mechanically verifiable; spot checks (`storenotice`, `kissauth`) are consistent.
No finding.

### Suggested follow-up

- Fix A: `--volt %s` (this report); drop the dead `small` handler.
- Fix B: guard each GUI element and its handler with the same macro as the rung, so the control is absent
  instead of broken.
- Prevention: a host test that runs the producer/rung extraction on every build and fails on an unmatched
  producer. It would have caught `93d9fee6` the day it landed.
