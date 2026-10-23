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

Run 1, LoRa only (gateways OFF on both nodes, no mock, no `--srvip`, no setup phase):

```
python3 tools/bench/gwbench/gwrun.py --lora-only --rak-host 192.168.68.73 > /tmp/gwrun1.out 2>&1
python3 tools/bench/gwbench/gwanalyze.py tools/bench/runs/gwbench_<timestamp>
```

Run 2 is the default invocation above (both gateways against the mock). The guard aborts unless
`--info` shows the gateway OFF on both nodes before the run.

Repeat phase (opt-in, LoRa only, INSTRUMENT image on both nodes; refuses to start when
`--airgap` is not answered):

```
python3 tools/bench/gwbench/gwrun.py --phases guard,repeat --rak-host 192.168.68.73 > /tmp/gwrepeat.out 2>&1
python3 tools/bench/gwbench/gwanalyze.py tools/bench/runs/gwbench_<timestamp>
```

- (a) lost command: `--airgap on` at DK5EN-90, `status` from DK5EN-1, `--airgap off` at DK5EN-90 34 s
  after copy 1 has left the sender (its `TX-LoRa` line; if none shows up within 20 s, 20 s after
  `/rmsend` returned ok). Expected: copy 2 (about 42 s) executes (one `[RM];ok`), the reply verifies, no copy
  after the stop.
- (b) lost reply: `status`, `--airgap on` at DK5EN-1 right after copy 1 left, `--airgap off` 40 s after copy 1 (a copy 2 that falls into the gap is dropped; copy 3 then meets the cache).
  Expected: the target executed once, copy 2 is answered from its cache (`[RM];cached`, no second
  `[RM];ok`), verified. If the reply beat the airgap the event says `reply_beat_airgap` and the
  analyzer FAILs on the missing `[RM];cached`: rerun.
- Why +34 s: neighbour nodes (DK5EN-92, -14, -98 on 2026-10-11) re-radiate copy 1 and the reply for
  up to ~24 s, and TX backlog can hold copy 1 back by 9 s. With an earlier airgap end a relay delivers the frame and copy 2 is never needed (the
  analyzer then FAILs "expected >= 1 repeat").
- Takes about 6 minutes. Nothing is written to the nodes and no TX power is changed.

`gwanalyze.py` exits 1 and prints `FAIL ...` lines on: second execution at the target, strike
(`[RM];reject;` other than replay/rate after the command), second verified row, stray repeat after
the stop (`[RM];stop;ctr;N[;slot;S]` if the firmware prints it, else the verifying reply). A repeat
enqueued after the stop (its `[RETX] RM1RETRY|PNRETRY` line follows the stop) is stray; one copy queued
before the stop may still go out once after it (in-flight, noted, allowed), a second one FAILs. Per command it
reports the reply latency (measured only, no threshold), the winning path (rf/srv, first reply
copy at the sender), repeats sent (distinct msg ids with equal `id & ~0xC00`, plus `[RETX] PNRETRY`
lines), cache answers and every late copy of command or reply. The driver switches
`--setinfo on` and `--setretx on` (RAM only) in LoRa-only and repeat runs to get those log lines.

The run takes about 40 minutes (the default run; "No answer" now takes about 170 s). A single instance only; stop it with SIGINT, never SIGKILL
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
  Passwords come from `MC_PW_<CALL>` in the repo's gitignored `.env` (`tools/mc_console.py`).
- Phase `repeat` and the INSTRUMENT image: `--airgap on|off` (RAM only, drops all RX and own TX). Put the
  stock image back on both nodes afterwards.
- Phase `neg` switches `--rmstrictsecurity on` at the target for the strike/lock tests (strikes and the lock
  exist only with strict security, BF-01) and restores the previous value afterwards and again in the teardown
  (the setting is saved on the node).
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

Airgap safety: a node in airgap is deaf and mute. Teardown always sends `--airgap off` to both nodes
and waits for `[AIRGAP];off` (`AIRGAP OFF CONFIRMED <call>`), also after an abort; a reboot clears it.
Never power-cycle as a substitute.

## What it covers

- Every RM command in both directions, with write commands restored afterwards.
- Junk frames, lockout, valid command while locked, unlock. Every junk DM starts with `RM1 ` and so
  repeats 4x on the ladder (~42 s apart); the neg phase waits the full ladder (140 s) per check and
  asserts on strike/lock state: one junk frame -> `rej_d >= 1`, no lock (copies are one guess); three
  distinct junk frames 15 s apart -> lock and `rej_d >= 3` (raw reject counts include every copy).
- The `guard` phase (identity_guard, gateway-off confirmation) is added automatically whenever a phase
  talks to the nodes; the driver prints a note when it did. `txpower` args must be an integer <= 2.
- Plain DM and group `TEST` in both directions.
- Phase `refused` (opt-in, LoRa only, run as `--phases guard,refused` without `setup`): reboots
  DK5EN-1 (empty learnt range), sends `txpower 1` to the RAK (floor 2 dBm) and expects a verified
  `err range 2..22`, the target marker `[RM];refused;ctr;<n>` and no lock; then the local refusal
  (`/rmsend` 422, `"range":"2..22"`), a `radio` read with `p=<cur>/2/22` and `txpower 2` -> `ok
txpower=2`. Every value sent is at most 2 dBm. Stock image is enough.
- Analysis per frame: copies seen via RF and via the server, which came first, duplicates
  suppressed, mock DATA in and GATE out, foreign frames re-radiated, anomalies.

## Known limits of the driver

- Name and text write-back: an empty value reads back as `-`; do not write that back (it stores a
  literal `-`).
- A refused send (TX backpressure at the sender) is recorded as a result; check the analyzer's
  negative-phase section before trusting a pass.
- Tests of the toolbox itself: `python3 -m pytest tools/bench/gwbench` (offline).
