# Toggle soak 2026-09-18: Heltec V3 and T-Deck Plus, no crash reproduced

Operator report: toggling functions (rx gain boost, webserver, display,
gateway, mesh, ext UDP) and a 2-minute soak makes the Heltec V3 reboot (boot
logo seen); also BLE attach/detach. Same to be checked on the T-Deck Plus.
Second report: the indoor temperature reading looks wrong.

## Result in one line

Neither node rebooted on any of the toggles, over any of the three entry
paths (serial console, web GUI `/setparam/`, phone-app text frame over BLE),
with a 2-minute soak per state and a net-console witness. The two by-design
reboots (`--setboostedgain` on the Heltec, 5 s deferred `ESP.restart`) came
exactly when the code says. The wrong temperature was two things at once:
the BME280 was **disabled** in the node settings (`BME280: off`, so the node
reported 0.0 °C) and the indoor offset held **+999999 °C**, residue of the
golden command corpus that drives every setter with `999999`
(`test/golden/corpus/commands/script.txt:203`). With `--bme on` and the
offsets zeroed the node reads 24.6 °C / 44.5 % rH / 960.7 hPa.

## Nodes and images

| node        | port                     | IP            | image                                                          |
| ----------- | ------------------------ | ------------- | -------------------------------------------------------------- |
| Heltec V3   | `/dev/cu.usbserial-0001` | 192.168.68.71 | 4.35t build Sep 17 2026 23:40 (`dry-unification`, `e64ce346`+) |
| T-Deck Plus | `/dev/cu.usbmodem101`    | 192.168.68.72 | 4.35t build Sep 17 2026 23:30 (same branch)                    |

The Heltec now announces itself as `DK5EN-1` (call, BLE name
`MC-8968-DK5EN-1`); macOS still shows the cached name `MC-8968-DK5EN-93`, which
is why the BLE cycler takes the CoreBluetooth UUID (`--address`) on this Mac.

## Method

`tools/bench/toggle_soak.py` holds the serial port open for the whole run (a
CP2102 board reboots on every port open, the native-USB T-Deck is attached
with DTR high and does not), sends each step, listens for the soak period and
classifies: reset banner or panic marker without a `!` on the step = CRASH,
with `!` = EXPECTED-REBOOT, else OK. `tools/bench/netlurk.py` is the second
witness on TCP 2323 (passive, reconnecting, idle timeout marks a lost node).
`tools/bench/ble_cycle.py` plays the phone app: scan, connect, subscribe,
hello frame, optionally `--send` text commands as `0xA0` frames, hold,
disconnect.

## Runs

| run                         | node   | steps                                                                                                  | verdict                   | evidence                                              |
| --------------------------- | ------ | ------------------------------------------------------------------------------------------------------ | ------------------------- | ----------------------------------------------------- |
| serial toggles + BLE cycles | Heltec | gain off/on (`!`), webserver, display, gateway, mesh, extudp each off/on, 120 s each                   | PASS                      | `test/golden/hw/G2/heltec-93/toggle/toggle-report.md` |
| web GUI toggles             | Heltec | `/setparam/?display\|gateway\|mesh\|extudp=on/off`, 120 s each (`webserver` has no web param, rc 2)    | PASS                      | `.../toggle/web-report.md`                            |
| BLE app path                | Heltec | 3 connect/hold 30 s/disconnect; gain off/on as text frames (`!`); ten toggles in one 20-min connection | PASS (2 expected reboots) | `.../toggle/ble-report.md`                            |
| serial toggles + BLE cycles | T-Deck | same as Heltec run 1, no `!` (T-Deck never auto-reboots)                                               | PASS                      | `test/golden/hw/G2/t-deck-14/toggle/toggle-report.md` |
| BLE app path                | T-Deck | twelve toggles (incl. gain off/on) in one 24-min connection, 120 s each                                | PASS                      | `.../toggle/ble-report.md`                            |

Per-step evidence: every gateway-on state shows the `[GW];keep;tx;ok` /
`[GW];rx;type;BEAT` exchange with the server, every display-on on the T-Deck a
`[TFT];on` marker, every extudp-on a socket (re)open; the closing `--info`
readback matches the last state sent. Reset lines in all logs:
`RESET_REASON=1 POWERON` at the port open (Heltec) and `RESET_REASON=3 SW`
5-6 s after each `--setboostedgain` on the Heltec. Nothing else.

The first Heltec BLE step (in the serial run) was void and is excluded: the
cycler matched the name by substring and `DK5EN-1` picked `MC-65e0-DK5EN-14`;
the Heltec serial log has no `BLE Connected` for that window. The rerun
(`ble-report.md`) matches by address.

## What was wrong with the temperature

`--wx` before: `BMP280: off / BME280: off`, `TEMP: 0.0 °C off 999999.000`,
`TOUT: 0.0 °C off 999999.000`. Also left at 999999 by the same corpus run:
`GPSDEBUG on/999999`, `postime 999999`, `ONEWIRE gpio 999999`.

After `--bme on`, `--tempoff in 0`, `--tempoff out 0` (over the net console,
no reboot): `BME280: on (found)` on I2C 0x76, `TEMP: 24.6 °C`, `HUM: 44.5`,
`QFE: 960.7 hPa`. Whether 24.6 °C is the room or the board's self-heating is
the operator's call; the sensor sits on the primary I2C bus (GPIO 41/42),
read every 60 s in forced mode with 16x oversampling.

The firmware defect behind it: `--tempoff in|out` accepted any float, while
the JSON config restore clamps the same fields to -50..50
(`src/config_json.h:320`). Fix in this campaign: both setters go through
`cmdStoreFloat(..., -50, 50, ...)` and answer
`tempoff in 999999.0 out of range (-50..50 °C), ignored`.

Regression proof on the Heltec (image build Sep 18 10:16, log
`test/golden/hw/G2/heltec-93/toggle/tempoff-range-proof.txt`): fails before
(the 23:40 image stored 999999, `--wx` showed `off 999999.000`), passes after:
`--tempoff in 999999` and `--tempoff out -999` print the range line and leave
the offset alone, `--tempoff in 2.5` is stored (`off 2.500`), `--tempoff in 0`
brings it back. The web GUI setup page had its own unclamped store for the
same two fields (`src/web_functions/web_setup.cpp`, `tempoffsetindoor` /
`tempoffsetoutdoor`); both now call the console setter and report
`returncode 1` with the untouched value on a rejected input
(`.../toggle/tempoff-web-proof.txt`: 999999 and abc rejected, 1.5 stored).
Host side `pio test -e native_command_setters` 14/14; the
nRF52 build of the shared file compiles (`wiscore_rak4631`). The golden
command corpus row `--tempoff in|out 999999` changes accordingly
(`test/golden/hw/G2/EXPECTED-DIFF.md`, row TS-01).

The remaining three leftovers were put back to the values in the node's
vault backup (`test/golden/nodes/heltec-93/settings-base.json`:
`--postime 0` (node clamps to 300), `--gpsdebug 2`, `--onewire gpio 0`); the
proper tool is `test/golden/backup_nodes.py --restore`, which the G0 README
requires before every capture and which was not run after the corpus drive.

## Open

- `QNH` equals `QFE` and `ALT asl: 0` although the GPS has a fix at ~490 m:
  the barometric base altitude is latched only when `baroBaseLatchAllowed()`
  (GPS-05b fusion) permits; not investigated further.
- `--seset` prints nothing on the serial/net console (its output is the BLE
  JSON only); the text block comes from `--wx`.
- The setters behind the other three leftovers (`--gpsdebug`, `--postime`,
  `--onewire gpio`) still accept 999999; the values were restored by hand, the
  range checks are outside this fix.
- The operator's boot-logo observation is not reproduced. Candidates outside
  this matrix: brownout on a weak USB supply when WiFi + gateway come up
  (the Heltec reports `BATT 0.00 V`, no battery), H6-01 (a rejected UDP-1990
  datagram reboots the node, BACKLOG §3.8ay), or a toggle sequence with
  shorter gaps than 120 s.
