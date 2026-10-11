# RAK4631 + W5100S: no DHCP lease after `--reboot` or a runtime IP loss (DB0AIS, MikroTik)

- **Status:** fixed on fork-dev 2026-10-11 (F1 + F2 + F3, `708b5091`), bench-verified on DK5EN-90,
  not yet in the field and no upstream PR (held back). Router side: `always-broadcast=yes` test at
  DB0AIS since 2026-10-09 19:02, result pending. Backlog row `ETH-04`.
- **Firmware:** V4.40a
- **Hardware:** RAK4631 + RAK13800 (W5100S), DHCP from a MikroTik router
- **Reported:** 2026-10-05 by the DB0AIS operator; second occurrence 2026-10-06 (runtime, no reboot)
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

## Update 2026-10-06: runtime occurrence and self-repair

The operator sent a second capture (chat 2026-10-06, analysed 2026-10-09). The node was not
rebooted in between.

MikroTik log:

| No. | Time     | Topic                 | Message                                                                            |
| --- | -------- | --------------------- | ---------------------------------------------------------------------------------- |
| 996 | 15:09:08 | system, info, account | user dg3fbl logged in from 44.149.189.195 via telnet                               |
| 997 | 15:09:15 | dhcp, warning         | DB0AIS Service offering lease 44.149.189.136 for 00:AB:26:D6:9A:08 without success |
| 998 | 15:10:00 | dhcp, warning         | same                                                                               |
| 999 | 15:10:16 | dhcp, warning         | same                                                                               |

Node console, failed state (15:04) and the spontaneous recovery (17:48):

```
15:04:35.315 [ETH];event;reset;ms;100843065
15:04:35.315 Initialize Ethernet
15:04:47.448 [ETH];stall;dhcp_begin;ms;12133;task;mcloop
15:04:47.449 Failed to configure Ethernet using FIX/DHCP
15:04:47.449 Ethernet.localIP(): 0.0.0.0
15:04:47.550 [ETH];link;down;link;1;link_age_s;35690;ip;0.0.0.0;dest;44.148.230.197;hb_age_s;12;
             got_ip_n;2;downs;2;renews;0;renew_fail;0;resets;1204;rx_n;1328;rx_max_ms;5;
             tx_fail;82;tx_max_ms;1644;ms;100855300
15:04:47.550 [ETH];event;dhcp_acquire_retry;link;1;ms;100855300
15:04:47.550 [ETH];event;reset;ms;100855301
15:04:59.684 [ETH];stall;dhcp_begin;ms;12133;task;mcloop
15:04:59.685 Failed to configure Ethernet using FIX/DHCP
...
17:47:54.600 Initialize Ethernet
17:48:06.732 [ETH];stall;dhcp_begin;ms;12132;task;mcloop
17:48:06.732 Failed to configure Ethernet using FIX/DHCP
17:48:17.416 17:47:08 [MAIN] resetDHCP (retry)
17:48:17.417 [ETH];event;reset;ms;110665270
17:48:21.586 [ETH];stall;dhcp_begin;ms;4170;task;mcloop
17:48:21.586 Ethernet.localIP(): 44.149.189.136
17:48:21.661 [ETH];event;got_ip;44.149.189.136;ms;110669444
17:48:21.944 [NTP];ok;epoch;1791301701;rtt;132
17:48:24.539 [GW];keep;tx;ok;1;ms;110672394
```

### What the new capture proves

1. **Same boot as the 2026-10-05 report.** `ms;100855300` is 28.0 h of uptime, so the node booted
   at about 11:03 on 2026-10-05. That is the `--reboot` in the evidence above. The operator's "it
   has been running for three days" is off.
2. **The lease is lost at runtime, not only after a reboot.** `link_age_s;35690` puts the last PHY
   link edge at about 05:10 on 2026-10-06. `resets;1204` at one retry per 30 s is about 10 h of
   failed retries. The node has been off the LAN since about 05:10, not since the afternoon.
3. **Same receive-side symptom.** The MikroTik answers every DISCOVER with an OFFER, but the node
   never sends a REQUEST. Each attempt runs the full 10 s `Ethernet.begin()` timeout (12.1 s
   stall, three DISCOVER/4 s cycles).
4. **The node recovered without a reboot, without a hardware reset and without a visible link
   flap.** The 17:48 success is a plain `resetDHCP()`. The ETH-02 acquire retry
   (`nrf52_main.cpp`) and the gateway retry (`gateway_service_nrf52.cpp:155`) both call that same
   function, so the success is roughly retry number 1500 of identical code. It answered in 4.2 s.
5. **Statement 3 of the first analysis is refuted.** It said a retry could never clear the stuck
   state. The state did clear on its own after about 12.5 h, which fits something that expires on a
   timer.

### Re-weighted hypotheses

- **H1 (router side), now favoured.** Self-repair by plain retry after hours points to state that
  ages out: a lease still bound to the MAC, an ARP entry, or the reply form the server chooses
  while it "knows" the client's IP. RouterOS has an `always-broadcast` option ("send replies as
  broadcasts even if the destination IP is known"; not yet verified against current RouterOS
  docs). If it is off, the server may unicast the OFFER to 44.149.189.136 while the W5100S SIPR is
  0.0.0.0, and the chip drops it although the client sets the broadcast flag.
- **H2 (chip side), weakened.** A wedged W5100S receive path does not usually heal without a reset
  or link flap. Not excluded, because the full log around 17:48 (any `[ETH];event;link` line) has
  not been seen yet.

### Probable trigger chain (to confirm)

1. About 05:10: a link edge (`downs;2`) and/or UDP send failures (`tx_fail;82`, `tx_max_ms;1644`).
2. Ten consecutive send failures (`MAX_ERR_UDP_TX`, `udp_drain_nrf52.cpp:89`) clear
   `hasIPaddress` and call `resetDHCP()`. `Ethernet.begin()` writes 0.0.0.0 into the chip first, so
   the firmware discards a lease that is most likely still valid on the router.
3. The fresh DISCOVER meets the router in its "client known" state, and the OFFER does not reach
   the W5100S (H1).
4. When that router state ages out, at about 17:48, a normal broadcast OFFER is accepted.

### Possibly the same bug earlier

On 2026-09-26 DK5EN-90 had link but no traffic, and moving the cable to another router port cured
it. That was attributed to a bad router port (memory note `rak-eth-no-traffic-20260926`). A port
change is also a link flap, so that case may have been this bug as well.

### Open questions to the operator

1. **Pending:** output of `/ip dhcp-server print detail`, especially `lease-time` and
   `always-broadcast`. As a test, set `always-broadcast=yes` on "DB0AIS Service". If the failure
   no longer reproduces, H1 is confirmed.
2. The full node log around 05:10 and 17:48 on 2026-10-06, especially `[ETH];event;link`,
   `[ETH];event;dhcp` and `Sending UDP Packet failed` lines. The router log around 17:48
   (`assigned`, `deassigned`, interface link events).
3. In the failed state, `/tool sniffer` on ether3 for UDP 67/68: is the OFFER L2/L3 broadcast or
   unicast?

### Fix candidates, revised (not implemented)

- **F1, new: fixes the trigger.** After a run of send failures or a link flap, keep a lease that
  is still valid. Re-open the UDP socket and renew with a REQUEST via `Ethernet.maintain()`
  (renew/rebind) instead of a fresh `begin()` DISCOVER. The server answers a renew at the IP the
  chip still holds, so this works even under H1. Only start a new DISCOVER when the lease has
  expired or the server sends a NAK.
- **F2: recovery for H1 and H2.** This is fix candidate 1 above: after 2 consecutive DHCP failures
  with the link up, run one `initETH_HW()` (a real link flap) before the next `startETH()`.
- **F3: seed the RNG.** Unchanged, secondary.

Order: F1 + F2 together once the router answer (question 1) is in. If `always-broadcast=yes`
removes the symptom, F1 is still worth doing because it removes the needless lease loss. F2 then
becomes a safety net instead of the primary fix.

## Update 2026-10-09: router configuration, `always-broadcast` test running

The operator sent `/ip dhcp-server print detail` (chat 2026-10-09, 18:54):

```
name="DB0AIS Service" interface=Bridge_44_Service lease-time=1w
  address-pool=Service DHCP Pool authoritative=yes use-radius=no lease-script=""
```

`always-broadcast` is not printed because it holds the default `no` (operator's confirmation,
18:56). At 19:02 the operator set `always-broadcast=yes` and is watching for a recurrence.

### What the answer settles

1. **The H1 precondition holds.** With `always-broadcast=no` the server is free to unicast the
   OFFER to the client's known IP. The W5100S then drops it, because its SIPR is 0.0.0.0 while
   `Ethernet.begin()` runs, even though the client sets `DHCP_FLAGSBROADCAST` (`Dhcp.cpp:145`).
   Many embedded DHCP clients need `always-broadcast=yes` on RouterOS for the same reason.
2. **The lease was valid when the node threw it away.** `lease-time=1w`: on 2026-10-05 the node
   had held 44.149.189.136 for well under a week, and on 2026-10-06 it had re-acquired it only
   about 18 h earlier (`got_ip_n;2`). Every failure therefore starts from a lease the router still
   considers bound. This makes F1 (renew instead of DISCOVER) the primary fix, not a refinement.
3. **The 12.5 h self-repair is not lease expiry.** A one-week lease cannot age out in 12.5 h. What
   the server keys "IP known" on and why it flipped at 17:48 stays unexplained (an ARP entry, the
   `deassigned` event from 11:02:21 on 2026-10-05, or the bound lease being released by some other
   path). This does not change the fix plan.

### Reading the test

The symptom showed on 2026-10-05 (after a reboot) and from 2026-10-06 05:10 (runtime). Two quiet
days with `always-broadcast=yes` are weak evidence on their own. The operator can provoke it:
before the change a `--reboot` failed often enough to need two or three tries.

| Result with `always-broadcast=yes`                                      | Verdict                                                  |
| ----------------------------------------------------------------------- | -------------------------------------------------------- |
| 5 to 10 `--reboot` in a row, each gets a lease within one DISCOVER      | H1 confirmed (unicast OFFER). Router workaround in place |
| Optional A/B: back to `no`, the failure reproduces within a few reboots | H1 proven beyond doubt                                   |
| Failure still occurs                                                    | H1 refuted; H2 (chip) or OFFER parsing. Sniffer needed   |

Even with H1 confirmed, the firmware fix stays necessary: other gateway operators run RouterOS
with the default `always-broadcast=no`, and the node should not discard a valid lease.

### Fix plan after the answer

- **F1, primary:** on a `MAX_ERR_UDP_TX` run or a link flap with the lease still inside its
  lifetime, keep the IP in the chip, re-open the UDP socket and renew through
  `Ethernet.maintain()` (REQUEST to the bound IP). DISCOVER only after expiry or a NAK.
- **F2, safety net:** after 2 DHCP failures with link up, one `initETH_HW()` before the next
  `startETH()`.
- **F3:** seed the RNG from the nRF52 hardware RNG.

## Implementation 2026-10-11 (`bdaf0093`, `708b5091`, fork-dev, no upstream PR yet)

All recovery paths still call `NrfETH::resetDHCP()`. It now asks the pure decision function
`eth_recovery::decide()` (`src/nrf52/eth_recovery.h`) what to do:

| Situation                                                         | Action                                               |
| ----------------------------------------------------------------- | ---------------------------------------------------- |
| Fixed IP                                                          | unchanged legacy path                                |
| DHCP lease held, link up, no maintain failure, last reopen got RX | **F1:** keep the chip IP, reopen the UDP socket only |
| No lease, maintain rc 1/3, or a reopen with no UDP RX since       | full DHCP (`startETH()` → `Ethernet.begin()`)        |
| Full DHCP failed twice in a row with link up                      | **F2:** `initETH_HW()` first, at most once per 5 min |

- **F1** keeps the lease across link flaps too: the 2026-10-06 capture shows a link edge at the
  outage start, and a DISCOVER right after a flap is the observed failure. `Ethernet.maintain()`
  keeps owning renew, rebind and expiry. The library offers no forced renew, so there is no REQUEST
  at reopen time.
- **Escalation:** a reopen arms a check; if `s_ethUdpRx` has not moved by the next trigger, that
  trigger does a full DHCP instead. A link down edge disarms the check (time without link is no
  evidence). Without it, a wedged chip or a failing ARP would reopen forever.
- **F2** never fires while the link is down (N-20).
- **F3:** `seedRandomFromHw()` in `nrf52setup()`, after `init_ble()` and before the Ethernet init,
  seeds `random()` from `sd_rand_application_vector_get()` (direct `NRF_RNG` if the SoftDevice is
  off). Log line `[ETH];event;rng_seed;src;sd|hw|fallback`.
- New log lines: `[ETH];event;reopen_udp;ip;...` and `[ETH];event;hw_reset;reason;dhcp_fail;n;...`.
- `--ethdrop` (INSTRUMENT build) now runs the decision: on a healthy node it only reopens UDP; a
  second one without UDP RX in between does a full DHCP. A webserver-only node never counts UDP RX.
- Accepted side effect: with DHCP and a failed renew (maintain rc 1/3), a malformed UDP frame now
  triggers a real DHCP; before ETH-04 that caller was a no-op.

### Verification

- `native_eth_recovery`: 22 cases (`test/test_eth_recovery`). The core cases fail against the old
  rule (always full DHCP) and against Rule 2 without the stall check.
- `tools/regression.sh --stage 1,2`: PASS, 70 envs, 2347 Unity cases. Builds: `wiscore_rak4631`,
  `heltec_t114`, `t_echo`. RAK flash +920 B (668012 → 668932).
- Bench DK5EN-90 (INSTRUMENT build), 2026-10-11:

| Step                                       | Result                                                  |
| ------------------------------------------ | ------------------------------------------------------- |
| Boot                                       | `rng_seed;src;sd`, lease in 4.2 s                       |
| `--ethdrop`, healthy                       | `reopen_udp`, 3 ms, IP kept, no DHCP                    |
| second `--ethdrop`, `rx_n;0` (gateway off) | escalated to full DHCP, same IP in 125 ms               |
| cable out 17 s, back in                    | `link;down;ip;1` → `link;up;ip;1`, IP kept, no DISCOVER |
| `--ethdrop` after the flap                 | `reopen_udp`, 2 ms; web GUI HTTP 200                    |

- Not exercised on hardware: F2 (needs a DHCP server that stops answering) and the MikroTik
  unicast-OFFER case itself. F2 is covered by the unit tests only. Field confirmation needs a
  DB0AIS image.
- Observed on one bench boot, pre-existing: the PHY took longer than the 3 s LinkON wait, boot
  DHCP was skipped, and the ETH-02 retry got the lease 30 s later.

## Code references

- `src/nrf52/nrf_eth.cpp:605` `resetDHCP()` (no hardware reset, `//initETH_HW();` commented out)
- `src/nrf52/nrf_eth.cpp:645` `initETH_HW()` (WB_IO3 reset pulse)
- `src/nrf52/nrf_eth.cpp:665` `startETH()` (3 s LinkON wait, `Ethernet.begin(macaddr, 10000UL)`)
- `src/nrf52/gateway_service_nrf52.cpp:167` retry call site (N-20 comment)
- `src/nrf52/nrf52_main.cpp` ETH-02 acquire retry (`dhcp_acquire_retry`, 30 s, also `resetDHCP()`)
- `src/nrf52/udp_drain_nrf52.cpp:89` `MAX_ERR_UDP_TX` path: clears `hasIPaddress`, calls `resetDHCP()`
- `.pio/libdeps/wiscore_rak4631/RAK13800-W5100S/src/Dhcp.cpp` (library: xid, broadcast flag,
  offer parsing)
