# gwbench: remote-management regression with both bench nodes as gateways

Drives the Heltec V3 (DK5EN-1) and the RAK4631 (DK5EN-90) in gateway mode against a
**local mock MeshCom server** and proves that remote-management commands, their replies and
ordinary DMs and group messages work over the RF path and the server path at once.

## Run

```
python3 tools/bench/gwbench/gwrun.py --rak-host 192.168.68.73 --loradebug > /tmp/gwrun.out 2>&1
python3 tools/bench/gwbench/gwanalyze.py tools/bench/runs/gwbench_<timestamp>
python3 tools/bench/gwbench/gwanalyze.py <run dir> --trace rm:status
```

The run takes about 40 minutes. A single instance only; stop it with SIGINT, never SIGKILL
(the teardown must run).

## Preconditions

- Both nodes run an **INSTRUMENT image** (`PLATFORMIO_BUILD_FLAGS="-DINSTRUMENT_ENABLED=1"`, no
  space after `-D`); only that image has `--srvip`. A stock image answers "wrong command" and
  the driver aborts before it turns the gateway on.
- Addresses are DHCP and move. The Heltec is resolved by `dk5en-1.local`, the RAK (no mDNS) is
  passed with `--rak-host`. Each node's start page must name its own callsign or the run aborts.
- The mock server runs on `rpizero.local` (ssh, directory `~/gwmock`, UDP 1990). A mock on a
  Wi-Fi host could not be reached by the RAK's wired Ethernet in the first attempt.
- The RAK is read over USB serial (`--rak-port`), the Heltec over the net console 2323.
  Passwords come from `GWBENCH_PWFILE` (default: the bench backup file outside the repo).
- Bench rules apply: DMs between the two nodes and group `TEST` only, never a broadcast, 2 dBm.

## Safety design

Gateway mode is saved on the node, `--srvip` is RAM only. A reboot while the gateway is on would
connect the node to the **real** MeshCom server. Therefore:

1. `--srvip` is sent and confirmed first, `--gateway on` only afterwards, and the run aborts
   unless the mock sees KEEP from both callsigns **and** both node addresses within 90 s.
2. `gwguard.BootGuard` tails both node logs from before `--gateway on`; a boot marker (or, for
   the RAK, a lost serial connection) sends `--gateway off` over web and console and aborts.
3. Teardown always runs: gateway off first, a fresh `--info` per node, then
   `GATEWAY OFF CONFIRMED <call>` or `GATEWAY STATE UNCONFIRMED <call>`, only then
   `--srvip 0.0.0.0`.

## What it covers

- Every RM command in both directions, with write commands restored afterwards.
- Junk frames, lockout, valid command while locked, unlock.
- Plain DM and group `TEST` in both directions.
- Analysis per frame: copies seen via RF and via the server, which came first, duplicates
  suppressed, mock DATA in and GATE out, foreign frames re-radiated, anomalies.

## Known limits of the driver

- Name and text write-back: an empty value reads back as `-`; do not write that back (it stores a
  literal `-`).
- A refused send (TX backpressure at the sender) is recorded as a result; check the analyzer's
  negative-phase section before trusting a pass.
- Tests of the toolbox itself: `python3 -m pytest tools/bench/gwbench` (offline).
