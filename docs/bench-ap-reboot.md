# The AP-reboot recovery test (TM-38) — operator runbook

All four bench nodes run unattended for about a quarter of an hour while **you** power-cycle the
access points. Pass condition: every node re-associates on its own, has an IP again, NTP time is
valid again, and the UDP link to the central server is back — with no reboot, no serial command and
no manual touch.

This is the network case no soak covers. TM-36 drops the link driver-side (`--wifidrop`) while the
AP stays up; here the AP itself is gone for tens of seconds, so the DHCP lease, DNS, ARP and the
server socket all have to come back. **One of the APs carries the RAK4631's LAN cable**, so the
gateway loses its Ethernet link in the same moment — its `[ETH]` link edges are part of the test,
not a side effect.

Runner: [`tools/bench/experiments/apreboot.py`](../tools/bench/experiments/apreboot.py).
Tests: `python3 -m unittest tools/bench/experiments/test_apreboot.py`.

## Why the runner detaches

The bench Mac is on the same APs. The moment you cut them, the Mac loses WiFi and Internet for the
~2 minutes of the reboot, and **every interactive session on it dies** — ssh, the Claude Code
harness, and the 2323 net console, which is exactly the witness that cannot survive the event it is
supposed to observe.

So `apreboot.py start` double-forks into a session of its own (`setsid`, no controlling terminal,
stdout/stderr to `runner.out`/`runner.err`, pid in `runner.pid`), talks **USB serial only**, and
uses no network at all. You can close the terminal, log out, or watch the Mac drop off the WLAN —
the run keeps going. Verified on the bench: `ps -o pid,ppid,sess,tty -p <pid>` shows `PPID 1` and
no TTY.

## Before you start

1. **Nothing else may hold the four ports.** The runner needs them exclusively for the whole run.

   ```
   lsof /dev/cu.usbmodem1101 /dev/cu.usbserial-0001 \
        /dev/cu.usbserial-573C0005841 /dev/cu.usbmodem201301
   ```

   No output is what you want. If a `meshlogger.py`, `wifisoak.py`, a serial monitor or a browser
   Web-Serial tab is on one, close it first — the TM-36 soak died exactly this way.

2. **Turn the ESP32 nodes into gateways**, or the UDP half of the test is dead weight. A node with
   `Gateway off` never sends KEEP, so it produces no `[UDP];tx` at all and the runner can only
   assert link and IP for it. Measured on the bench 2026-08-30: DK5EN-93 answered `--info` with
   `Webserver on / Webpwd <> / Gateway off`, and 90 s of settle produced zero `[UDP];tx` on all
   three ESP32 boards.

   Send `--gateway on` to the T-Deck, Heltec and T-Beam **before** arming the runner (the RAK is a
   gateway already), and let them re-register. The runner warns about it at the settle gate and
   `--strict-udp` turns that warning into an abort.

3. **Confirm the RAK's LAN cable is on one of the APs you are about to cycle.** If it hangs off the
   router instead, the RAK will simply never see a link edge and its part of the test is void.

4. Know where the AP power is and how long you will hold it off. Ten to twenty seconds off is
   plenty; the default cycle window gives you three minutes to get there and do it.

## Arming the run

```
python3 tools/bench/experiments/apreboot.py start --label ap1 \
    --board tdeck=/dev/cu.usbmodem1101 \
    --board heltec=/dev/cu.usbserial-0001 \
    --board tbeam=/dev/cu.usbserial-573C0005841 \
    --board rak=/dev/cu.usbmodem201301
```

It prints the run directory and returns immediately:

```
apreboot detached, pid 62586
  run dir   .../tools/bench/runs/apreboot_ap1_20260830-190430
  phases    settle 180 s -> prompt (window 180 s) -> recovery 600 s (~16 min total)
  watch     python3 apreboot.py status
  stop      python3 apreboot.py stop
```

Defaults: `--settle 180`, `--cycle-window 180`, `--recovery 600` — about 16 minutes end to end.
**Do not shorten the settle below 180 s.** The RAK's only `udp_rx` witness is the `rx_n` counter of
its 60-second `[ETH];link` heartbeat, so it needs at least two of them to establish a baseline; a
60-second settle catches one and the RAK's `udp_rx` silently degrades to `n/a` (seen on the bench,
`apreboot_smoke3_20260830-190430`).

Board names are free; a name containing `rak` picks the RAK profile (DTR high, `[ETH]` markers) and
everything else the ESP32 one. Force it with `name=/dev/port:rak` or `:esp32` if you ever rename a
board.

Opening the port resets every ESP32 bench node — that first boot of the run is expected and is the
only `rst:0x` the verdict tolerates. The RAK does not reset on open but stays silent without DTR,
which the runner sets for it.

**The only thing ever sent over serial is one `--udplog on` per ESP32 board, during settle.** The
RAK gets nothing at all: `--udplog` is not in the nRF52 command table (it answers
`...wrong command`) and its UDP path has no per-datagram print anyway. Nothing is sent during or
after the outage — a command after t0 fails the board on purpose.

## What happens, and when it calls you

**SETTLE (3 min).** Every board must reach link-up and an IP. Everything else it does in this
window — `[UDP];tx`, `[UDP];rx`, `[NTP];ok` — becomes its _baseline_: whatever it demonstrated
before the outage has to come back after it. A board that never gets online aborts the run here,
before you are sent to the APs for a test that cannot pass.

**PROMPT (up to 3 min) — this is your cue.** The runner:

- writes a `PROMPT` file into the run directory,
- puts `*** CYCLE THE ACCESS POINTS NOW ***` at the top of `status.txt`,
- raises a macOS notification titled **MeshCom bench: apreboot** with the body
  _"Cycle the access points now."_,
- and says it out loud: **"cycle the access points now"**.

Both the notification and the spoken line are best-effort — a headless or muted Mac just skips
them, which is why `PROMPT` and `status.txt` say the same thing. (`--no-notify` turns both off.)

**Now go and power-cycle the APs.** Off for ten to twenty seconds, then back on. Do not touch the
nodes, do not open a serial monitor, do not send anything.

The first `[WIFI];event;disconnected` / `[WIFI];link;down` / `[ETH];event;link;down` on any board is
**t0**, and every number in the report is measured from it. If no board loses its link inside the
cycle window, the run ends `FAIL — no outage detected`: the APs were not actually cut, or the nodes
were not on them.

**RECOVERY (10 min from t0).** The runner watches every board come back and writes the report.
Leave everything alone until it is done.

## Watching it

```
python3 tools/bench/experiments/apreboot.py status        # newest run
python3 tools/bench/experiments/apreboot.py status <rundir>
```

`status.txt` is rewritten every 10 seconds:

```
apreboot ap1  .../tools/bench/runs/apreboot_ap1_20260830-190430
now                2026-08-30 19:05:41
phase              RECOVERY
outage t0          2026-08-30 19:04:12
recovery           t0+89 s of 600

board   kind   state
tdeck   esp32  link=up ip=1 tx=3 rx=2 ntp=1 boots=1 rst=1 wd=0 lines=1204
heltec  esp32  link=up ip=1 tx=4 rx=3 ntp=1 boots=1 rst=0 wd=1 lines=2480
tbeam   esp32  link=up ip=1 tx=3 rx=2 ntp=1 boots=1 rst=1 wd=0 lines=1533
rak     rak    link=up ip=1 tx=0 rx=2 ntp=1 boots=0 rst=0 wd=0 lines=52
```

`events.csv` is refreshed on the same tick, so you can watch the markers arrive without touching a
port. To end a run early:

```
python3 tools/bench/experiments/apreboot.py stop
```

That is a clean SIGTERM: the runner closes the ports, writes `summary.txt` and exits. A run stopped
before it prompted has no t0 at all and reports as such — it never invents an outage from the
boot-time `[WIFI];link;down` every ESP32 board prints.

## Reading the result

`summary.txt` is written when the run ends; rebuild it any time from the raw logs, with no hardware
and no pyserial:

```
python3 tools/bench/experiments/apreboot.py report tools/bench/runs/apreboot_ap1_*/
python3 tools/bench/experiments/apreboot.py report .../apreboot_ap1_*/tdeck.log
```

```
apreboot ap1  .../tools/bench/runs/apreboot_ap1_20260830-190430
started            2026-08-30 19:01:12
windows            settle 180.0 s / cycle 180.0 s / recovery 600.0 s
outage t0          2026-08-30 19:04:12  (first down edge: tdeck 200:BEACON_TIMEOUT)
verdict            PASS

== tdeck (esp32)   PASS
marker       before t0   after t0
link_up              4   +60.0 s
got_ip               4   +63.0 s
ntp_ok               1   n/a (no refresh due in the window)
udp_tx               2   +72.0 s
udp_rx               2   +88.0 s
reboots after t0        0
rst:0x / [BOOT];ready   1 / 1
[WIFI];watchdog         0
stall lines             0
commands after t0       none
```

Each board's five markers, with the seconds after t0 at which they came back:

| marker    | ESP32                                        | RAK4631                                                        |
| --------- | -------------------------------------------- | -------------------------------------------------------------- |
| `link_up` | `[WIFI];event;connected` or `[WIFI];link;up` | `[ETH];event;link;up` or the `[ETH];link` heartbeat            |
| `got_ip`  | `[WIFI];event;got_ip`                        | `[ETH];event;got_ip`, or a heartbeat carrying a non-zero IP    |
| `ntp_ok`  | `[NTP];ok;epoch;…;rtt;…`                     | same (shared gateway socket)                                   |
| `udp_tx`  | `[UDP];tx` — KEEP/DATA leaving               | **n/a** — no per-datagram print exists in `nrf_eth.cpp`        |
| `udp_rx`  | `[UDP];rx` — server traffic returning        | the `rx_n` counter of the 60-s `[ETH];link` heartbeat going up |

### Pass criteria

- **Board PASS** — every marker that board is held to came back inside the recovery window, **and**
  no `rst:0x` or `[BOOT];ready` appeared after t0 (a reboot fails the board: the node has to
  recover, not restart), **and** no serial command was sent after t0.
- **Run PASS** — every board passed and an outage was actually detected.

Which markers a board is held to:

- `link_up` and `got_ip`: **always**.
- `udp_tx` / `udp_rx`: only if the board showed them **before** t0. That is the point of the settle
  baseline — a node that was never sending KEEP cannot be failed for not resuming it. Those show as
  `n/a (none before t0)`, and the settle-gate `WARN` tells you the run was weaker than it should
  have been.
- `ntp_ok`: only if an NTP refresh actually **fired** after t0 — then it must have succeeded
  (`[NTP];timeout|txfail|kod` after t0 with no `[NTP];ok` fails the board). A refresh is only due
  every 15 minutes (`esp32_main.cpp` ~2628, `nrf52_main.cpp` ~1214) and never at all while a node
  has a GPS fix, so the default 600 s window often ends before one is due; that reports as
  `n/a (no refresh due in the window)`. To assert NTP properly, run
  `--recovery 1000 --require-ntp` and accept the longer run.
- `udp_tx` on the RAK: never — see the table above.

Also worth a look every time, even on a PASS:

- `[WIFI];watchdog` lines — `reconnect` is stage 1 (grace expired, `disconnect()+begin()`), `reset`
  is stage 2 (radio restart). On a healthy AP reboot the driver's own auto-reconnect should get
  there first and stage 2 should never be reached.
- `stall lines` — any `[WIFI];stall` / `[ETH];stall` over 500 ms is a finding of its own.
- `rst:0x / [BOOT];ready` — one each per ESP32 board (the port-open reset), zero on the RAK.

## Files in a run directory

| file                  | what it is                                                                                                                       |
| --------------------- | -------------------------------------------------------------------------------------------------------------------------------- |
| `<board>.log`         | the raw serial capture, one wall-clock timestamp per line; `## ` lines are the runner's own notes (port opens, anything it sent) |
| `status.txt`          | live phase and per-board state, rewritten every 10 s; the verdict at the end                                                     |
| `events.csv`          | every marker as a row, with `t0±Ns` in the note column                                                                           |
| `summary.txt`         | the report and the verdict                                                                                                       |
| `meta.json`           | windows, board list, prompt time, t0, verdict                                                                                    |
| `runner.pid`          | the detached runner's pid                                                                                                        |
| `runner.out` / `.err` | its stdout/stderr — empty on a clean run                                                                                         |
| `PROMPT`              | written the moment the operator was called                                                                                       |

The logs are the source of truth: `report` rebuilds `events.csv` and `summary.txt` from them at any
later date, so the run survives even if the runner is killed mid-flight.

## When it goes wrong

| symptom                                             | what it means                                                                                             |
| --------------------------------------------------- | --------------------------------------------------------------------------------------------------------- |
| `FAIL — no outage detected`                         | no board lost its link inside the cycle window: the APs were not cut, or the nodes are on a different one |
| `settle incomplete -- <board>: no IP during settle` | that board never got online; the run aborted before you were sent to the APs                              |
| `serial error: ... multiple access on port?`        | something else holds the port — check `lsof` and start over                                               |
| `WARN ...: no [UDP];tx during settle`               | that ESP32 node is not a gateway; the UDP half of the test did not run                                    |
| `<board>: rebooted after t0`                        | a real finding — the node restarted instead of recovering                                                 |
| `<marker> only after the recovery window`           | it did come back, just too late; re-run with a longer `--recovery` and report the number                  |
