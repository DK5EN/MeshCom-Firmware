# Campaign: bench fixes and features 2026-11-06

Resume point. Findings of the 2026-11-06 bench run plus new operator requests. Order: plan and build all waves, then
flash and test (W5). Nothing here is released; the release stays on hold until the bench passes.

## Operator decisions (2026-11-06)

- Build first, then flash and test. Flash and reboot of DK5EN-1, -14, -90, -92 approved. DK5EN-98 is not part of this.
- T-Deck Path column cut-off: fix it. `WEB-TX-01`: root cause and fix.
- BLE registers over UDP: read-only `{"type":"reg","q":"SN"}`, only with EXTUDP on, reply only to the configured
  `node_extern` IP, no secrets on the wire (`BPIN`, `WSPWD` and anything like them filtered).
- Central server drops duplicate message IDs (operator). Server buffer: text messages only, held while no `BEAT` arrives,
  maximum age, paced replay, size per board.
- Auto update: the switch stays pinned to notify, `AU_AUTO_INSTALL` stays 0 (D14). Build the schedule and the countdown
  notice. Default: the next night 3 days after release, maximum 7 days; a fixed weekday is also possible; install time
  adjustable by console and web. The schedule must be testable by clock jump (INSTRUMENT), without waiting.
- No MCP23017 on the bench, so pin switching on real expander hardware cannot be tested. T-Beam has an OLED.

## Waves and file ownership

| Wave                                                                                                              | Agent | Items                               | Exclusive files                                                                              | Status |
| ----------------------------------------------------------------------------------------------------------------- | ----- | ----------------------------------- | -------------------------------------------------------------------------------------------- | ------ |
| W1                                                                                                                | A     | TD-33, T-Deck Path column           | `src/t-deck/lv_obj_functions.cpp`, `src/t-deck/event_functions.cpp`                          | done   |
| W1                                                                                                                | B     | WEB-LED-02, WEB-DUP-01              | `src/web_functions/web_functions.cpp`, `webUIComponents.cpp`, `web/mc.css`, `tools/webgui_*` | done   |
| W1                                                                                                                | C     | OLED screenshot, test text command  | `src/loop_functions.cpp`, `src/command_functions.cpp`, new OLED files, `tools/bench/`        | done   |
| W1                                                                                                                | H     | WEB-TX-01                           | `src/web_functions/web_txbuf.{h,cpp}`, its native test                                       | done   |
| W2                                                                                                                | D     | BLE registers over UDP              | `src/command_functions.cpp`, `src/extudp_functions.*`, new header, docs                      | done   |
| W2                                                                                                                | E     | Live pin status                     | `src/web_functions/web_functions.cpp`, `web_setup.cpp`, new header, `tools/webgui_*`         | done   |
| W3                                                                                                                | F     | Auto update schedule and clock jump | `src/fw_update.h`, `src/esp32/fw_update_net.cpp`, settings, console, web, docs               | done   |
| W4                                                                                                                | G     | Server buffer                       | `src/udp_functions.cpp`, drain files, `src/byte_fifo.*`, gateway service files               | done   |
| W5                                                                                                                | me    | Flash and bench, all items below    | hardware, serialized                                                                         | done   |
| Orchestrator-owned hotspots: `platformio.ini`, `tools/regression.sh`, `docs/test-suite-map.md`, the INI baseline, |
| `docs/BACKLOG.md`, `docs/RESUME.md`, this file.                                                                   |

## W5 bench list

- RAK DK5EN-90: silent after the 2026-11-06 replug (no console, no ping on `.72`). Capture state, 1200 baud touch, reflash,
  restore settings, find out whether a stored setting blocks boot. Hung first in commands 50-75 of the script.
- Restore all nodes from the vault with `test/golden/backup_nodes.py --restore`. Nodes are scrambled from the command run.
- Command script reboots to attribute: Heltec near `--analog slope 999999`, `--aht20 off`, `--gpsdebug on`,
  `--setout 999999`; T-Beam near `--aht20 off`, `--setout 999999`; T-Deck near `--tft off`.
- Debug flags on then off on all nodes; timing test (receive performance, CSMA-04 blind spot stamps, RX silence).
- Screenshots and glyph review on Heltec, T-Beam, T-Deck. Deep web GUI pass on Heltec, RAK, T-Deck.
- `WEB-TX-01` on the T-Beam: confirm cause and fix, 10 of 10 full page loads. T-Beam heap against the CI image.

## Wave log

- W1 (2026-11-06): TD-33, T-Deck Path column, WEB-LED-02, WEB-DUP-01, OLED screenshot (`/screen.oled`, `--oledtext`,
  `tools/bench/oled_screenshot.py`), WEB-TX-01. Advisor: 2 must-fix plus 1 measure, re-pass 1 must-fix, all fixed. New
  guard: `configuration_global.h` includes `sdkconfig.h`, because reaching it before `<Arduino.h>` selected the classic
  branch on S3. Gate: Unity 97 envs 3467 cases, pytest 1490, 6 builds. T-Deck `t_deck` not built. Bench open (W5).
- W2 (2026-11-06): BLE registers over EXTUDP (`{"type":"reg","q":"SN"}`, `src/ext_reg.h`, secrets filtered, replies only to
  the configured peer, 1 query per second) and the Pins page (`/pinstatus`, 5 s poll, LED and charge LED switches, MCP pins
  host tested only). Advisor: stack blocker fixed (`work_webpage` frame down 2536 B on RAK, 2160 B on T-Beam), input pin
  button, `all` run after EXTUDP off; re-pass: `--extudpip` refresh removed as unsafe on RAK, now `EXT-PEER-01`. New rows
  `EXT-PEER-01`, `WEB-PIN-01`. Gate: Unity 99 envs 3506 cases, pytest 1490, 6 builds. RAK flash 82.0 %.
- W3 and W4 (2026-11-06): auto update schedule and notice (`src/fw_schedule.h`, `--autoupdate delay|day|time`, Setup fields,
  `--autest` clock jump, install stays off) and the server outage hold ring (`src/udp_hold.h`, `--udphold`, text only, 20 min,
  1 KB classic, 6 KB S3 and RAK). Advisor: schedule read a clock nothing sets (fixed, node date fields); RM1, MCP, SET, CET,
  telemetry, ack, ping and SOTA frames refused from the hold; text held without an IP is replayed, never released. Overruled:
  a single lost `BEAT` may replay the few held frames (about 1 per 4 h, measured), because a longer gap would lose text.
  Gate: Unity 101 envs 3584 cases, pytest 1490, 6 builds. RAK flash 82.3 %. Bench open (W5).
- W5 (2026-11-06, bench, images of this commit): OLED screenshot on Heltec and T-Beam, umlaut dots correct in both fonts
  (`CHARS-02` for Latin-2). `--autest` reaches due and logs `blocked;auto_install_off`. EXTUDP query: 15 registers, no secret,
  rate limit holds. Hold ring: frame held in a simulated outage, replayed once with the same id. `WEB-TX-01`: 60 of 60 T-Beam
  pages complete, longest 0.7 s, no abort; the old failure did not reappear, so the cause stays unconfirmed. TD-33 and the Path
  column confirmed on screen except WiFi off (needs a person, WiFi is the only bench link). Debug flags on and off on all 4, no
  panic. RX timing: 0 % loss at the T-Beam, quiet and under web load (`rx_stall_*_20261106-1133*.json`, `-1136*.json`); CAD to
  free 37-45 ms, free to TX below 1.2 ms, irq to read max 18 ms, worst loop gap 295 ms. Script reboots: `--bleshort`/`--blelong`,
  `--setboostedgain`, `--sethamnet`/`--setinet` are documented; T-Deck `--disptest` (`TD-34`); Heltec near index 55 did not
  reproduce. `RAK-PIN-01` found and fixed. Gate: Unity 101 envs 3587 cases, pytest 1490, 7 builds.
- W5 RAK script (2026-11-06): 382 commands, 368 answered, no hang, 4 soft resets (`RESETREAS=0x4`), `--button gpio 1` refused.
  All four nodes restored and verified against the vault.
