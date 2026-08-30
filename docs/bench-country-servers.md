# Bench: do the three country gateway servers answer the same way? (TM-39)

`--gateway srv OE|DL|IT` is the only selector a node has for which central server it registers
with. The destination is chosen once, at connect time, in `startMeshComUDP()`
(`udp_functions.cpp`, mirrored in `nrf52/nrf_eth.cpp`'s `NrfETH::startUDP()`), so a country change
only takes effect after a reconnect or reboot — not instantly. `docs/BACKLOG.md` TM-39 asked
whether the three servers behave the same once a node is registered with them: same KEEP/heartbeat
answer, same server-pushed traffic, same timing, same behaviour when the server goes quiet.

## Server selection, read from the code

| Platform                         | Path (own IP 44.x or `--hamnet on`)                                                                                                                                                           | Path (internet, the normal case)                                                       |
| -------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------- |
| ESP32 / RAK-WiFi                 | IT → `meshcom.dig-italia.it` (comment: "not available for IT-Server" on HAMNET, so IT is routed to the internet host even here) · DL → `meshcom.hamnet.cloud` · else → literal `44.143.8.143` | IT → `meshcom.dig-italia.it` · **everything else (including DL) → `meshcom.oevsv.at`** |
| nRF52 (RAK4631, Ethernet/W5100S) | IT → literal `145.239.75.155` · DL → literal `44.148.230.197` · else → literal `44.143.8.143`                                                                                                 | **no per-country case at all → always literal `89.185.97.38`**                         |

Two asymmetries fall out of just reading the code, before any bench measurement:

- **ESP32 internet path:** `DL` is not special-cased. A DL-configured node without a HAMNET
  address sends to the Austrian server (`meshcom.oevsv.at`), same as `OE`. Whether that is
  intended is an operator question, not a firmware bug — this document only states what the code
  does and what the bench observed, per the backlog item's own framing.
- **nRF52 internet path is flatter still:** unlike the ESP32 side, the non-HAMNET branch has no
  `IT`/`DL` distinction whatsoever — every country reaching the internet path lands on the same
  literal IP, `89.185.97.38`. This is a second, RAK-specific instance of the same class of
  asymmetry, found while instrumenting this backlog item; not bench-measured (the RAK4631 is not
  on this session's bench port), reported here as a code-reading finding for the operator.
- The nRF52 side never resolves a hostname — every destination is a literal IP baked into the
  firmware (no DNS resolver on the W5100S/Ethernet path), where the ESP32 side resolves
  `meshcom.dig-italia.it` / `meshcom.hamnet.cloud` / `meshcom.oevsv.at` at connect time (async DNS,
  `[WIFI];dns` marker).

Neither table is changed by this work — TM-39 asks what the servers do, not what the selection
logic should do.

## Firmware markers added for this probe

Added to `src/udp_functions.cpp` (ESP32/RAK-WiFi) and `src/nrf52/nrf_eth.cpp` (nRF52), all raw
`Serial.printf` (unconditional — `printfdeb()` needs `--debug` and strips `;` outside csv mode,
neither of which this probe wants to depend on):

- `[GW];srv;<OE|DL|IT>;host;<host>;path;hamnet|inet;ms;<N>` — once per (re)connect, right after the
  destination is chosen and before the DNS lookup (ESP32) / right after the literal IP is picked
  (nRF52).
- `[GW];rx;type;SET|CET|BEAT|DATA|OTHER;len;<N>;ms;<N>` — one line per server frame in both
  platforms' gateway RX path, classified by the same `{SET}`/`{CET}` prefix checks the dispatch
  code already makes; `DATA` is any other relayed mesh frame (position/text/hey) the server pushed
  back down to LoRa; `BEAT` is the heartbeat; `OTHER` is an unrecognized indicator. The nRF52 build
  additionally recognizes a fourth server indicator, `CONF` (server-pushed callsign/lat/lon/alt),
  that the ESP32/RAK-WiFi path has no case for at all — see "Platform asymmetry found while
  instrumenting" below.
- `[GW];keep;tx;ok;<1>;ms;<N>` — on every `sendKEEP()` call (shared code, both platforms). `ok`
  reflects the ring-buffer enqueue only (`addUdpOutBuffer()` has no failure path) — not confirmed
  on-wire delivery, which is a separate concern tracked by the pre-existing `--udpstat` counters on
  ESP32.

Reused, pre-existing markers: `[UDP];rx`/`[UDP];tx` (ESP32's TM-31 instrument, gated by
`--udplog`), `[WIFI];dns` (async DNS resolution), `[NTP];ok|timeout|txfail|kod` (TM-35 async NTP),
`[BOOT];ready` (boot marker), and the plain-English heartbeat-timeout warning in
`esp32/esp32_main.cpp` ("Server not responding for Ns…" at 35 s, "Heartbeat timeout Ns…" at 65 s —
`HB_WARN_TIME`/`MAX_HB_RX_TIME`, `configuration_global.h`). That warning is `printfdeb()` too, but
neither format string contains a `;`, so csv-stripping is a no-op and it prints unconditionally,
`--debug` or not.

### Platform asymmetry found while instrumenting

The ESP32/RAK-WiFi `getMeshComUDPpacket()` recognizes exactly two server indicators, `GATE` and
`BEAT`; anything else falls into "Received udp message without indicator" (this probe's `OTHER`
bucket). The nRF52 `NrfETH::getUDP()` recognizes three: `GATE`, `CONF`, and `BEAT` — `CONF` carries
a server-pushed callsign/longname/lat/lon/alt update and has its own decode path. A `CONF` frame
sent to an ESP32/RAK-WiFi gateway today would be silently swallowed into the `OTHER` bucket instead
of being acted on. Not bench-measured here (found while reading the RX dispatch to place the `[GW]`
markers, and the RAK4631 is out of this session's bench scope) — reported for the orchestrator,
not fixed, per the minimal-change brief for this session.

### nRF52 `--udplog` gap (TM-38 follow-up)

The ESP32 side's per-datagram `[UDP];rx`/`[UDP];tx` markers and their `bUDPLOG` gate live entirely
inside `#if defined(ESP32)` in both `udp_functions.cpp` and `udp_functions.h` — the symbol does not
exist in the nRF52 build at all (confirmed by a link failure: `undefined reference to bUDPLOG`
against the first version of this change). `nrf_eth.cpp` now carries the same markers in
`NrfETH::getUDP()`/`sendUDP()`, gated by its own `bool bUDPLOG = false;` (same name, same default,
separate definition — the two platforms are separate firmware images, so there is no link
conflict). Nothing sets it to `true`: `--udplog` is not in the nRF52 command table
(`command_functions.cpp`, out of scope for this change). The orchestrator can add the same four
lines the ESP32 side already has, inside the nRF52 `#if defined(NRF52_SERIES)` command block
(`command_functions.cpp`, next to `--ethstat`/`--ethdrop`):

```cpp
if(commandCheck(msg_text+2, (char*)"udplog on") == 0 || commandCheck(msg_text+2, (char*)"udplog off") == 0)
{
    bUDPLOG = (commandCheck(msg_text+2, (char*)"udplog on") == 0);
    Serial.printf("[UDP];log;%d\n", bUDPLOG ? 1 : 0);
    return;
}
```

The `[GW];srv`/`[GW];rx`/`[GW];keep` markers above are **not** gated by `bUDPLOG` on either
platform — they fire unconditionally. They have to: `[GW];srv` is needed right after boot, before
this probe (or an operator) ever sends `--udplog on`, and the traffic is low-rate by construction
(one KEEP every 30 s, one RX per server frame) so leaving them always-on costs nothing.

## How to run the probe

```
cd tools/bench/runs
python3 ../experiments/srvprobe.py --port /dev/cu.usbserial-0001
python3 ../experiments/srvprobe.py --countries OE,IT --seconds 60      # shorter/partial run
python3 ../experiments/srvprobe.py --parse-only srvprobe_<ts>/          # re-reduce saved logs
```

Regression tests for the reducer (no hardware): `python3 -m unittest tools/bench/experiments/test_srvprobe.py`.

For each country the probe: sends `--gateway srv <XX>`, `--gateway on`, `--reboot`; waits for
`[BOOT];ready` and `[GW];srv`; turns `--udplog on`; observes for `--seconds` (default 180);
counts KEEP sent, `[GW];rx` by type, raw `[UDP];rx`/`[UDP];tx`, DNS resolution, NTP outcomes, any
heartbeat-timeout warning, and any `{SET}`/`{CET}` content line that happened to be logged by
something else (neither `[GW];rx` nor `[UDP];rx` carries payload text, so this is opportunistic
only — see the source docstring). At the end it restores `--gateway srv OE`, `--gateway off`,
`--udplog off`, reboots, and confirms with `--info`.

## Measured table (DK5EN-93, Heltec V3, ORBI63 WLAN, non-HAMNET internet path, 2026-08-30, 180 s per country)

`tools/bench/runs/srvprobe_20260830-193907/` (`OE.log`/`DL.log`/`IT.log`, `summary.txt`).

| country | host                    | resolved IP      | dns ms | KEEP tx | BEAT rx | raw UDP rx | NTP    | hb_warn |
| ------- | ----------------------- | ---------------- | ------ | ------- | ------- | ---------- | ------ | ------- |
| OE      | `meshcom.oevsv.at`      | `89.185.97.38`   | 61     | 7       | 7       | 6          | —      | 0       |
| DL      | `meshcom.oevsv.at`      | `89.185.97.38`   | 62     | 7       | 7       | 6          | ok × 1 | 0       |
| IT      | `meshcom.dig-italia.it` | `145.239.75.155` | 1673   | 7       | 7       | 6          | ok × 1 | 0       |

Every KEEP got exactly one BEAT back, in all three countries, every 30 s on the dot (OE: `ms`
9053 → 30043 → 60048 → 90100 → 120101 → 150142 → 180145, i.e. one KEEP right after boot then the
usual 30 s cadence with normal jitter) — 0 losses, 0 resets, 0 heartbeat-timeout warnings, and the
BEAT reply was byte-identical in shape across all three servers: 20 bytes, every time. `--info`
confirmed the node was back to `Gateway off` after the run.

## Reading the result

- **DL and OE are the same server today.** Both resolve `meshcom.oevsv.at` → `89.185.97.38` on this
  WLAN — the internet-path asymmetry read from the code (DL has no `case` of its own on that path)
  is exactly what the bench shows: a DL-configured node without a HAMNET address talks to the same
  Austrian server an OE node does, with the same KEEP/BEAT behaviour. Nothing in the bench data
  suggests DL nodes are treated differently once connected — because they are not; they are, from
  the server's point of view, indistinguishable from OE traffic on this path. Whether DL should get
  its own internet-path server is an operator/upstream decision, not something this probe or the
  underlying selection logic can answer — this document only reports what was read in the code and
  what the bench observed, per the backlog item's own framing.
- **IT is a genuinely different, working server** (`meshcom.dig-italia.it` → `145.239.75.155`) and
  answered every KEEP just as reliably as OE/DL — same BEAT cadence, same reply size, 0 warnings.
  The one operational difference bench-measured: **its DNS resolution took 1673 ms**, against 48–62
  ms for every other name resolved in this run (`meshcom.oevsv.at`, `pool.ntp.org` three times).
  That is a one-time cost at connect time (async, does not block the loop — F6/TM-34), not a
  per-message latency; nothing downstream of it showed any slowdown. Worth a second data point
  before treating it as characteristic of the Italian DNS record rather than one slow resolver
  round trip on this WLAN.
- **KEEP/BEAT reply behaviour is identical across all three servers** in this run: 7-for-7 answered,
  same 20-byte reply, no missed cycles, no stage-1/stage-2 heartbeat-timeout warnings anywhere. The
  probe's silence detection (no reply to any KEEP in the whole window) never triggered.
- **No `{SET}`/`{CET}` frame arrived from any server** in this run (none of the three pushed a
  config update while the probe watched); the only `{CET}` content observed was the node's own
  outgoing `[MESH]` payload (a local time broadcast, unrelated to the gateway RX path), confirming
  the opportunistic content capture works but had nothing server-side to catch in this window.
- **NTP is not a country comparison here** — OE ran right after boot and its 3600 s NTP timer had
  not fired within the first 180 s; DL and IT (running later in the same held-open session) each
  picked up one `[NTP];ok` in their windows. Not a per-server difference, just where each window
  landed relative to the async NTP refresh cadence (TM-35).
- `--udplog on/off` and the raw `[UDP];rx`/`[UDP];tx` markers behaved identically in shape across
  all three: TX lengths matched the actual traffic mix (32 B = KEEP, 66/72/99/112/138/144 B =
  position/telemetry frames relayed from LoRa), 6 raw RX per window (one per BEAT plus other short
  server traffic), all with `ok;1`.

## What "server silent" looks like

`HB_WARN_TIME` is 35 s and `MAX_HB_RX_TIME` is 65 s (`configuration_global.h`) — with a 30 s KEEP
interval, a node that gets nothing back for two cycles logs `"...Server not responding for Ns —
WiFi CONNECTED"` (stage 1, 35 s) and then, unresolved, `"...Heartbeat timeout Ns — WiFi CONNECTED,
server unresponsive, waiting"` (stage 2, 65 s), repeating every cycle it stays quiet — the node
does not reset itself while WiFi is still up; the reset path only fires when WiFi itself is down at
the time of the check. **Not observed in this run** — all three servers answered every KEEP inside
the window, so neither warning ever fired; a genuinely silent server (or a firewalled/blackholed
one) remains untested here and would need a run against a server known to be down or unreachable.
