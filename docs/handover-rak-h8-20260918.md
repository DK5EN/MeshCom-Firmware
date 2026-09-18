# Handover: RAK4631 (DK5EN-90) after the ETH-03 fix -- what is proven, what is not, where it stopped

Written 2026-09-18 08:05, stopped by the operator mid-run. A later agent picks
up from here without re-reading the session.

## State of the node right now

| item            | value                                                                                                                                                                                                                                                                                                                                            |
| --------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| image           | `dry-unification` HEAD `1bea29a1` (ETH-03 8 kB loop task + ETH-02b), flashed 07:33 via UF2 after a double-tap                                                                                                                                                                                                                                    |
| `--info build:` | `Sep 18 2026 / 07:25:21` -- that is `command_functions.cpp`'s compile time; the ETH-02b TU was rebuilt at 07:30 (`dhcp_acquired_late` string is in the ELF, `[BOOT];loopstack;words;2048` printed at boot)                                                                                                                                       |
| settings        | `--extudp` **unknown** -- the last confirmed state was `off` (07:36); the two aborted H8 attempts sent `--extudp on` without a confirmed echo. Read `--info`, expect `EXTUDP off`, set it if not. `--extudpip 192.168.68.58` (the Mac) is still stored, harmless with extudp off. Gateway off, mesh off, webserver on, IP `192.168.68.66` (DHCP) |
| USB             | `/dev/cu.usbmodem2101`, app running (`WisCore RAK4631 Board`)                                                                                                                                                                                                                                                                                    |

## Proven this morning

1. **ETH-03 is fixed on hardware.** `tools/bench/eth03_probe.py --port /dev/cu.usbmodem2101 --ip 192.168.68.66 --n 4`
   -> `=== overall PASS`: four `[EXT];rx` lines, `stack_hwm` 590 words (2.3 kB
   headroom), no `RESETREAS`, no USB drop, http 200 before and after. Log:
   `test/golden/hw/G2/rak-90/eth03-probe-after.txt`. Same probe on `d05a0dc3`
   rebooted the node after datagram 1.
2. **ETH-02 confirmed by its counters.** The first boot today got no lease;
   `[ETH];stat` showed `resets;1` and `got_ip` at 48.8 s -- the 30 s retry from
   BACKLOG §3.8au acquired it.
3. **ETH-02b found and fixed in code, not yet proven.** After that late lease
   the web server stayed dead and `--info` said `hasIpAddress: no` (services
   are only re-run on the 15-minute `web_timer`; `--info` reads a copy of the
   flag). Fix in `src/nrf52/nrf52_main.cpp` (ETH-02b block: zero `web_timer`
   when the retry acquires a lease, marker `[ETH];event;dhcp_acquired_late`).
   The boot after flashing got its lease first try (`resets;0`), so the path
   has not executed on hardware. Proof needs a boot where DHCP fails first;
   it happened once today on this switch port, so it is opportunistic: boot
   the RAK, watch for `dhcp_acquire_retry` + `dhcp_acquired_late`, then
   `curl http://192.168.68.66/` must be 200 within ~10 s.

## Not done: H8 (EXTUDP golden) on the RAK

G1 has 8 corpus responses from this node; G2 has none yet (the 17.09. "0 of
23" in `test/golden/hw/G2/rak-90/extudp/FINDING.md` was ETH-03). Three
attempts this morning produced nothing and were deleted (0 datagrams
received, the node never had `EXTUDP on` confirmed); no capture is committed.

Why they failed, so the next attempt does not repeat it:

- `tools/bench/serial_session.py` has no `--dtr` flag; `--dtr on` made it send
  `on` as a command. The RAK is mute on serial unless the port is opened with
  **DTR toggled false->true and RTS true, then ~3 s settle**, and commands
  work best as `"\r\n--cmd\r\n"`. This pattern got `--info` answers reliably
  (see the probe script in this session; write it into a tool, e.g.
  `serial_session.py --rak`).
- `--extudp on` prints no echo of its own; confirm the state with a following
  `--info` (`...EXTUDP on ...EXT IP 192.168.68.58`).

Recipe once serial is under control (same as the Heltec G2 run in
`test/golden/hw/G2/heltec-93/extudp/README.md`):

1. `--extudp on`, confirm via `--info`.
2. `python3 tools/bench/extudp_peer.py --bind 0.0.0.0 --port 1799 --listen 105 --record test/golden/hw/G2/rak-90/extudp/extudp-received.jsonl`
3. Send `test/golden/corpus/extudp/01.json .. 23.json` to `192.168.68.66:1799`
   at 3.5 s intervals. **Pilot rule:** after 01 and 02, read the serial log and
   confirm what went on the air is addressed to `DK5EN-1` / group `9999`, never
   `*` (memory `no-broadcast-test-messages`); only then send 03-23.
4. Write `extudp-sent.txt` (`NN.json <content>` per line); build the text form
   of the record (`<addr>  <len> <text>` per line) and run it through
   `test/golden/normalize.py` into `extudp-received.txt`; keep the `.jsonl`
   sibling (the compare tool uses it for foreign-source filtering).
5. `python3 test/golden/compare_extudp.py test/golden/hw/G1/rak-90/extudp test/golden/hw/G2/rak-90/extudp`
   -- expected: `corpus responses identical (8)`; relayed/beacon counts may
   differ, that is declared a non-failure. Expect one more `[EXT] Out` line
   per corpus object than G1 only if DR-18's JSON ack lands here (see
   `test/golden/hw/G2/EXPECTED-DIFF.md`, DR-18 row) -- check whether
   `compare_extudp.py` already handles `"type":"ack"`.
6. `--extudp off`, confirm via `--info`. Write the README like the Heltec one.

## Also open on this node

- ETH-02b hardware proof (above).
- `--dfu` behaviour: it did not enter the bootloader on its own; the reset
  came on the NEXT port open (DTR toggle) and then landed in **UF2 mode with
  the volume mounted** (07:32) -- unlike last night, where after a 1200-baud
  touch the bootloader stayed in serial-only DFU and only a double-tap
  helped. Not investigated; noted so nobody trusts either observation alone.

## Where the rest of the campaign stands

`docs/RESUME.md` top section and `docs/BACKLOG.md` §3.8ay. Owed elsewhere:
T-Deck four by-eye checks (`test/golden/hw/G2/t-deck-14/ui/README.md`),
`H6-01` (rejected UDP-1990 datagram resets WiFi + reboots the Heltec),
`ENABLE_MCU811` typo decision.
