# Toggle soak 2026-09-18: four boards, no crash reproduced

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

## T-Beam v1.2 (DK5EN-92), same matrix, 2026-09-18 midday

Port `/dev/cu.usbserial-573C0005841` (CH9102, reboots on open), IP
192.168.68.76, flashed to the branch image with the fix (build Sep 18 2026
11:48, `ttgo_tbeam`, esptool at 460800 because 921600 fails on this bridge).
GPS on, no environmental sensor (`BMP280: off / BME280: off`, correct). The net
console was off on this node and was switched on for the run (`--netconsole
on`, no reboot needed).

| run                         | steps                                                                                         | verdict | evidence                                              |
| --------------------------- | --------------------------------------------------------------------------------------------- | ------- | ----------------------------------------------------- |
| serial toggles + BLE cycles | gain on/off, webserver, display, gateway, mesh, extudp each on/off, 120 s each; 3 BLE cycles  | PASS    | `test/golden/hw/G2/t-beam-92/toggle/toggle-report.md` |
| web GUI + BLE app path      | `/setparam/` display, gateway, mesh, extudp on/off 120 s each; ten toggles in one BLE session | PASS    | `.../toggle/webble-report.md`                         |
| ext UDP retest + offset     | `--extudp on/off` serial and web with the ext IP set; `--tempoff in 999999` rejected          | PASS    | `.../toggle/extudp-retest-report.md`                  |

`--setboostedgain` does not exist on this SX1276 board (the rung is compiled
only for SX126x radios); the node answers `wrong command`, no reboot, which is
the correct behaviour. Gateway on shows the `[GW];rx;type;BEAT` exchange; the
only resets in the logs are the `POWERON` from each port open.

The first two runs' ext UDP steps were no-ops: this node had no external IP
and answers `Please set EXPUDP IP first` (correct). With `--extudpip
192.168.68.58` set, the BLE session and the retest show the real path
(`[EXT]...now listening at IP 192.168.68.76, UDP port 1799`, position sent),
still without a reboot. The net console was switched back off at the end; the
ext IP stays set.

## RAK4631 (DK5EN-90), same matrix, 2026-09-18 afternoon

Port `/dev/cu.usbmodem2101` (native USB, attached with DTR high, no reset on
open; a real reset re-enumerates the port and the harness reopens it),
Ethernet IP 192.168.68.66, flashed to the branch image via serial DFU (build
Sep 18 2026 10:19, `wiscore_rak4631`). No net console on nRF52, so the serial
capture is the only witness. BLE with the node's PIN (hashed hello).

| run                                 | steps                                                                                                                                                                                    | verdict                  | evidence                                           |
| ----------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------ | -------------------------------------------------- |
| serial + web GUI + BLE, one session | gain on/off (`!`), webserver, display, gateway, mesh, extudp; `/setparam/` display, gateway, mesh, extudp; 3 BLE cycles; ten toggles in one 20-min BLE connection; `--tempoff in 999999` | PASS (2 expected resets) | `test/golden/hw/G2/rak-90/toggle/toggle-report.md` |

`--setboostedgain` resets the nRF52 5 s later as designed
(`RESETREAS=0x00000004`, soft request). Gateway on shows the server BEAT
exchange over Ethernet; ext UDP on/off (the ETH-03 path) stays up; the offset
clamp answers on this platform too (`%.1f` renders on nano printf). Ext UDP
left off as the handover asked.

**On four boards** (Heltec V3, T-Deck Plus, T-Beam v1.2, RAK4631) with the
branch image: no toggle over serial, web GUI or BLE app path rebooted a node,
and BLE attach/detach is clean; the only resets are the documented
`--setboostedgain` restarts on SX126x boards.

## Follow-up fixes 2026-09-18 afternoon

**H6-01 closed.** A zero-padded datagram on UDP 1990 made the ESP32 caller run
the full `resetMeshComUDP()` (WiFi teardown). Reproduced on the shipping
Heltec image with `tools/bench/udp_inject.py` and corpus datagram `33-m03`
sent from this Mac: `[WIFI];event;disconnected;reason;8`, `timeout when WiFi
un-init`, 9 s offline (the reboot seen at G2 is the non-deterministic tail of
that outage and did not occur in this run). Fix in `src/udp_functions.cpp`:
while WiFi is connected only the socket is re-armed; the full teardown stays
for the heartbeat paths that already require WiFi to be down. After: `UDP
socket re-armed, WiFi kept`, server BEAT keeps arriving through four injected
datagrams. Evidence `test/golden/hw/G2/heltec-93/udp/h6-01-fix/`.

**Range checks for the other 999999 leftovers.** `--onewire gpio` now clamps
to 0..99 (proven: 999999 rejected, 0 stored). `--postime` and `--gpsdebug`
were deliberately left alone: the JSON restore bounds in `src/config_json.h`
contradict the firmware (postime is stored in seconds with a 300 s floor and
a 1800 s default, the JSON bound is 0..1440; gpsdebug level 3 is the valid
raw-NMEA mode, the JSON bound is 0..2). Those two bounds are the defect, not
the setters; open item.

## Open

- `QNH` equals `QFE` and `ALT asl: 0` although the GPS has a fix at ~490 m:
  the barometric base altitude is latched only when `baroBaseLatchAllowed()`
  (GPS-05b fusion) permits; not investigated further.
- `--seset` prints nothing on the serial/net console (its output is the BLE
  JSON only); the text block comes from `--wx`.
- `--postime` and `--gpsdebug` still accept 999999; the matching JSON bounds
  in `src/config_json.h` (0..1440 minutes vs seconds stored, 0..2 vs the valid
  level 3) are wrong and need a decision before either side is clamped.
- The operator's boot-logo observation: closed 2026-09-18 by operator decision.
  Not reproduced by any toggle in three passes per node; the most probable
  cause is the by-design 5 s deferred restart after `--setboostedgain`, which
  the app and console both trigger on SX126x boards.
