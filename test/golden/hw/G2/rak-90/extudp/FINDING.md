# EXTUDP golden, RAK-90 (step H8) -- G2 NOT CAPTURED, and that is the finding

2026-09-17. The node answered **none** of the 23 corpus objects. G1 recorded
**8** node-originated responses from this node on the same corpus. No capture
file is committed here because there is nothing to compare; this note is the
result of the step.

## What was established

| Check                                                      | Result                                        |
| ---------------------------------------------------------- | --------------------------------------------- |
| `--info`: `EXTUDP on`, `EXT IP 192.168.68.58`              | correct                                       |
| `--info`: `hasIpAddress: yes`, IP `192.168.68.66`          | link up, address held                         |
| 23 corpus objects + 3 priming frames + 6 post-reboot probes | **0 responses**                               |
| after `--reboot`                                           | 0 responses                                   |
| after `--webserver off` + `--reboot`                        | 0 responses                                   |
| Heltec-93, same corpus, same host, same hour                | 8/8 responses, identical to G1                |

The node is otherwise healthy: it transmits (Heltec-93 hears its HEY beacons at
RSSI -42), receives (its own `STAT` line reports `rx`, `mh=3`) and answers
serial commands throughout.

## What was ruled out

- **Not the configuration.** Verified by `--info` after each change.
- **Not a stale socket.** Two reboots.
- **Not W5100S socket exhaustion.** That chip has four hardware sockets and the
  web server holds one, so the obvious theory was that DHCP + UDP-1990 + NTP +
  web left none for `UdpExtern`. `--webserver off` plus a reboot changed
  nothing, so the theory is refuted, not merely untested.
- **Not the host side.** The identical sender and listener drove Heltec-93 to a
  clean pass minutes earlier.

## What was NOT established

Whether `startExternUDP()` reaches `UdpExtern.begin(EXTERN_PORT)`
(`src/extudp_functions.cpp:142`) on this platform at all. The two
`[EXT]...now listening` / `now sending` lines it prints on success
(`:168-169`, raw `Serial.printf`, not gated by `--setlog`) were never observed,
but the boot capture that would have shown them was truncated when the USB CDC
port dropped across the reboot, so their absence is **not** evidence yet. That
is the next thing to measure, and it is one clean capture away: hold the port
across a power cycle, or watch over the network console.

## Why this matters beyond one bench node

The call sites exist and are reached on paper: `nrf52_main.cpp:2426`
(`getExternUDP()` under `bEXTUDP`) and `:2467` (`startExternUDP()` under
`bEXTUDP && neth.hasIPaddress`). BACKLOG §3.8l already carries "EXTUDP on the
RAK4631 -- reported crash, and the missing UDP regression test" as an open row,
so this is the second independent sighting of the nRF52 EXTUDP path not
behaving, and the first with a controlled comparison against a working ESP32
node in the same hour.

`EXT-01` -- the telemetry-frame socket teardown -- was confirmed fixed on
**DK5EN-93 (ESP32)** today. It has never been confirmed on nRF52, and this
finding is the reason it could not be.
