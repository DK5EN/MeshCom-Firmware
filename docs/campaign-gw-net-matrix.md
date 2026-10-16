# Campaign: gw_servers network matrix on the bench

Resume point for the bench run of PR `pr/gw-servers` (worktree `../MeshCom-PR-next`, commits
`cd911c6e`, `87f2f583`). Started 2026-10-16.

## Wave status

- W0 recon: done. All markers unconditional (`[GW];srv`, `[GW];keep;tx;ok`, `[GW];rx;type;BEAT|CONF`,
  `[NTP];ok`); `[GW];srv` is printed before `[BOOT];ready`. `--setnet` and `--gateway srv` reboot +5 s.
- W1 driver: done. `tools/bench/gw_net_matrix.py` + `tools/tests/test_gw_net_matrix.py` (17 tests).
  Orchestrator fix after the smoke run: evidence window starts at the last `[GW];srv`; mode labels
  `auto/Internet/Hamnet`.
- W2: done. PR build flashed (RAK via DFU, Heltec app-only at 0xC0000; same partition layout).
  Identity guard ok on both. Smoke case N1/inet/OE: firmware correct on both.
- W3 matrix run: done. RAK 42/45, Heltec 33/45 (see Results).
- W4 analysis: done except F2 network side. rpizero was knocked off by my promiscuous tcpdump (08:36), operator power-cycled it; never capture on rpizero again.
- W5: done. Both nodes back on fork-dev firmware (70f2c476), DHCP (.71 / .62), Heltec gateway on/OE + GPS on,
  RAK gateway off, debug off, mode auto. PR body "Getestet" updated. F2 closed as measured (rpizero nft
  ruleset has no forward counter; no capture allowed).

## Operator decisions (2026-10-16)

- Firmware under test: the PR build (upstream/dev + PR). Check RAK callsign/config after flashing.
- Hamnet both ways: N1 DHCP + `--setnet hamnet` via the router's 44/8 route to rpizero, and N3
  static IP with gateway rpizero 192.168.68.64.
- Heltec DK5EN-1 runs as gateway during the test only (<= 2 dBm), then back to its previous
  firmware and config, gateway as before.
- Static IPs: freeze current DHCP leases (RAK 192.168.68.62; Heltec from its boot log), netmask
  255.255.252.0, DNS from the lease.
- Full approval to flash, configure, reset both nodes.

## Matrix

Per node: networks N1 DHCP, N2 static (gw 192.168.68.1), N3 static (gw 192.168.68.64) x modes
auto, inet, hamnet x servers OE, DL, IT, HC, HB = 45 cases.

- Bench addresses are all 192.168.x, so auto resolves to Internet everywhere.
- The firmware has no "DHCP + custom gateway"; a custom gateway needs ownip + gw + ms (static).
- Expected values come from `gwResolveNode` (fork `tools/gw_servers_dump.cpp`), not by hand.

Pass per case: `[GW];srv` host/path as expected, `[GW];keep;tx;ok`, a server answer, NTP time set.

## Nodes

| Node     | Board     | USB serial       | Port (2026-10-16)      |
| -------- | --------- | ---------------- | ---------------------- |
| DK5EN-90 | RAK4631   | 230D6EBB3266D20E | /dev/cu.usbmodem101    |
| DK5EN-1  | Heltec V3 | 0001             | /dev/cu.usbserial-0001 |

## Pre-test state (for W5 restore)

- DK5EN-1 (Heltec): fork build 4.40a Oct 13, DHCP 192.168.68.71, gateway on, server OE, network mode 0
  (legacy; no command writes 0 any more, restore as auto). Log `tools/bench/runs/harness/gwnet_pre_heltec_20261016-0749.log`.
- DK5EN-90 (RAK): fork-dev production build, DHCP 192.168.68.62, gateway off, mode auto (2).
- 192.168.68.63 is now DK5EN-98 (production) -- fleet.json host for heltec-1 is stale; do not touch.

## Results

Logs: `tools/bench/runs/harness/gwnet_{rak,heltec}_20261016-*.{log,jsonl}`.

- All server choices (`[GW];srv` code/host/path) matched the oracle in all 90 cases.
- F1 (both nodes, IT + `--setnet hamnet`, 3 cases each): server correctly IT over Internet
  (meshcom.dig-italia.it, path inet), KEEP + BEAT + NTP ok, but `--setnet`/`--info` print
  `in use: hamnet`. `gwNetInUse()` (src/command_functions.cpp:256) reports the selected network,
  not the path the server row actually takes. Display only.
- F2 (Heltec only, N3 = static gw rpizero, Internet path, 10 of 10 inet cases except one HC pass):
  KEEP sent, no BEAT, no NTP. Hamnet path through rpizero passed (OE/DL/HC/HB). RAK N3 Internet
  passed 15/15. Rerun of N3 inet OE/HC at 08:33 failed; tcpdump on rpizero wlan0 saw no Heltec
  packet, then rpizero went off the network. Open: rpizero forwarding wlan0->wlan0 for a WiFi
  client vs firmware. Firmware side unproven either way.

### W4 reruns (2026-10-16, after rpizero power cycle)

- F1 is intended behaviour (operator): `in use` = selected network. Documented in the PR body;
  driver now expects it (`selected_net`).
- F2 reproduced: Heltec N3 Internet 0/11 (KEEP out, no BEAT, no NTP), N3 Hamnet 4/4; RAK N3 15/15
  at the same time through the same rpizero.
- F2 control with upstream/dev firmware (f2b3a349) on the same Heltec, N3 inet OE/DL/HB: same
  failure (no BEAT, no NTP); N2 inet OE/DL/HB answer + NTP ok. Not caused by the PR.
- F3 (new): in the PR rerun, 3/15 Heltec N2 cases had no NTP line at all. Cause: ESP32 only pumps
  NTP without a GPS fix (`esp32_main.cpp:3020`, `!posinfo_fix`, upstream behaviour). With
  `--gps off`: N2 15/15 (IT display only), NTP in every case.
- Still open for F2: WiFi client -> rpizero (wlan0 in, wlan0 out, no NAT) -> Orbi for Internet
  traffic; replies return asymmetrically. Network-side test proposed (Mac route via rpizero).
