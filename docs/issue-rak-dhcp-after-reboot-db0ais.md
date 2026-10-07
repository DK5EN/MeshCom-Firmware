# RAK4631 + W5100S: no DHCP lease after `--reboot` (DB0AIS, MikroTik)

- **Status:** open, root cause not isolated (two hypotheses left)
- **Firmware:** V4.40a
- **Hardware:** RAK4631 + RAK13800 (W5100S), DHCP from a MikroTik router
- **Reported:** 2026-10-05 by the DB0AIS operator
- **Possibly related:** DB0ND-22 (Heltec, WiFi) reportedly shows the same symptom (chat only, unconfirmed)

## Symptom

After a `--reboot` the RAK at DB0AIS sometimes gets no DHCP lease and is unreachable on the
network. A second or third `--reboot` brings it back. The operator can reproduce it.

## Evidence

MikroTik log (2026-10-05):

| Time     | Topic         | Message                                                                            |
| -------- | ------------- | ---------------------------------------------------------------------------------- |
| 11:02:17 | interface     | ether3-USER-Bullet link up (speed 100M, full duplex)                               |
| 11:02:21 | dhcp, info    | DB0AIS Service deassigned 44.149.189.136 from 00:AB:26:D6:9A:08                    |
| 11:02:37 | dhcp, warning | DB0AIS Service offering lease 44.149.189.136 for 00:AB:26:D6:9A:08 without success |
| 11:03:11 | dhcp, warning | same                                                                               |
| 11:03:27 | dhcp, warning | same                                                                               |
| 11:03:43 | dhcp, warning | same                                                                               |

Node console (same boot, ~108 s uptime):

```
11:03:47.964 [ETH];stall;dhcp_begin;ms;12133;task;mcloop
11:03:47.965 Failed to configure Ethernet using FIX/DHCP
11:03:47.966 Ethernet.localIP(): 0.0.0.0
11:03:48.060 [UDP] Server not responding for 36s - ETH DOWN
11:03:49.700 [ETH];link;down;link;1;link_age_s;80;ip;0.0.0.0;dest;;hb_age_s;38;got_ip_n;0;downs;0;
             renews;0;renew_fail;1;resets;4;rx_n;0;rx_max_ms;0;tx_fail;0;tx_max_ms;0;ms;108806
```

## What the evidence proves

1. **TX works.** The MikroTik receives every DISCOVER from the node's MAC and answers each one
   with an OFFER.
2. **The node never receives or accepts an OFFER.** It never sends a REQUEST. The OFFER is lost on
   the receive side: either the W5100S does not deliver it to UDP port 68, or
   `DhcpClass::parseDHCPResponse()` rejects it.
3. **The failure persists through every retry in the same boot** (`resets;4`, `got_ip_n;0`), even
   though each `request_DHCP_lease()` call draws a new transaction ID. The cause is therefore state
   that survives a retry and is cleared only by a reboot.

## Why the node does not recover on its own

| Path                                                                        | W5100S hardware reset (WB_IO3) | Link flap seen by the router |
| --------------------------------------------------------------------------- | ------------------------------ | ---------------------------- |
| Boot: `initethDHCP()` → `initETH_HW()`                                      | yes                            | yes                          |
| Retry before N-20: `initethDHCP()`                                          | yes                            | yes                          |
| Retry since N-20 (`780df254`, 2026-08-21, upstream in 4.40a): `resetDHCP()` | no                             | no                           |

N-20 replaced `initethDHCP()` with `resetDHCP()` in the retry path
(`src/nrf52/gateway_service_nrf52.cpp:167`). The reason was that a hardware reset per retry kept an
unplugged-then-replugged cable at LinkOFF forever. Since then a retry is only another
`Ethernet.begin()` on unchanged chip state with no link flap. Whichever side holds the stuck state,
a retry cannot clear it. Only a reboot does, and that is a per-boot dice roll.

Later, `startETH()` (`src/nrf52/nrf_eth.cpp:665`) gained a wait of up to 3 s for LinkON after a
reset. That wait addresses the original N-20 concern.

## Hypotheses

**H1 – router side.** The MikroTik keeps per-MAC state (the old bound lease or its ARP entry) that
only a link-down clears. In that state it sends the OFFER in a form the W5100S drops, for example a
unicast to 44.149.189.136 while the chip's SIPR is still 0.0.0.0. The Arduino DHCP client does set
the broadcast flag (`DHCP_FLAGSBROADCAST`), so this requires the server to ignore it in that state.
Supported by the DB0ND-22 report, if confirmed.

**H2 – chip side.** After a soft reboot the W5100S sometimes comes up with its receive path dead or
filtering. `rx_n;0` does not settle it, because that counter only covers MeshCom UDP.

## Secondary defect (not causal for this issue)

Nothing seeds the nRF52 random number generator (`randomSeed` appears nowhere in `src/`).
`random()` in the Adafruit core is `rand() % n`, so the DHCP transaction ID
(`random(1, 2000)` in `Dhcp.cpp`) likely repeats across boots. It does not explain why retries
within one boot fail, but it should be seeded from the hardware RNG.

## Discriminating tests (operator, no firmware change)

1. **Cable test.** In the failed state, unplug the cable for 5 s and plug it back in. That flaps
   the link without resetting the chip.
   - Next retry succeeds → H1 (MikroTik state cleared by link-down).
   - Still no lease → H2 (W5100S).
2. **Sniffer.** `/tool sniffer` on ether3, filter UDP 67/68, during a failed boot. This shows
   whether the OFFER leaves as broadcast or unicast (L2 and L3 destination) and its xid.
3. **History.** Did DB0AIS see this before the N-20 change (firmware older than ~2026-08-21)?

## Fix candidates (not implemented)

1. **Recovery, independent of H1/H2:** after 2 consecutive DHCP failures with the link up, run one
   full `initETH_HW()` (hardware reset → link flap) before the next `startETH()`. This restores the
   pre-N-20 self-healing without the N-20 regression, which the 3 s LinkON wait now covers.
2. **Diagnostics:** one line per packet in `parseDHCPResponse()` (remote IP, op, xid, chaddr,
   reject reason) in a test image for DB0AIS.
3. **Seed the RNG** from the nRF52 hardware RNG at boot.

## Code references

- `src/nrf52/nrf_eth.cpp:605` `resetDHCP()` (no hardware reset, `//initETH_HW();` commented out)
- `src/nrf52/nrf_eth.cpp:645` `initETH_HW()` (WB_IO3 reset pulse)
- `src/nrf52/nrf_eth.cpp:665` `startETH()` (3 s LinkON wait, `Ethernet.begin(macaddr, 10000UL)`)
- `src/nrf52/gateway_service_nrf52.cpp:167` retry call site (N-20 comment)
- `.pio/libdeps/wiscore_rak4631/RAK13800-W5100S/src/Dhcp.cpp` (library: xid, broadcast flag,
  offer parsing)
