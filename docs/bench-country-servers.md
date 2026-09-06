# Bench: do the three country gateway servers answer the same way? (TM-39)

`--gateway srv OE|DL|IT` is the only selector a node has for which central server it registers
with. The destination is chosen once, at connect time, in `startMeshComUDP()`
(`udp_functions.cpp`, mirrored in `nrf52/nrf_eth.cpp`'s `NrfETH::startUDP()`), so a country change
only takes effect after a reconnect or reboot — not instantly. `docs/BACKLOG.md` TM-39 asked
whether the three servers behave the same once a node is registered with them: same KEEP/heartbeat
answer, same server-pushed traffic, same timing, same behaviour when the server goes quiet.

## Server selection, read from the code

| Platform                         | Path (own IP 44.x or `--hamnet on`)                                                                                                                                                           | Path (internet, the normal case)                                                            |
| -------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------- |
| ESP32 / RAK-WiFi                 | IT → `meshcom.dig-italia.it` (comment: "not available for IT-Server" on HAMNET, so IT is routed to the internet host even here) · DL → `meshcom.hamnet.cloud` · else → literal `44.143.8.143` | IT → `meshcom.dig-italia.it` · DL → `meshcom.hamnet.network` · else → `meshcom.oevsv.at`    |
| nRF52 (RAK4631, Ethernet/W5100S) | IT → literal `145.239.75.155` · DL → literal `44.148.230.197` · else → literal `44.143.8.143`                                                                                                 | IT → literal `145.239.75.155` · DL → literal `192.68.17.26` · else → literal `89.185.97.38` |

Both tables above describe the state **after** CTY-02 (see the 2026-09-06 update at the end of
this document). They no longer match what this probe measured in August 2026 — at that time the
Internet path had no DL arm on either platform, which is exactly the defect CTY-02 fixed. The
historical measurement below is kept as recorded, with its now-superseded reading marked.

- The nRF52 side never resolves a hostname — every destination is a literal IP baked into the
  firmware (no DNS resolver on the W5100S/Ethernet path), where the ESP32 side resolves the name at
  connect time (async DNS, `[WIFI];dns` marker). Since CTY-02 both platforms read the same table
  (`src/gwsrv_select.h`), which returns a name and the matching literal for every cell, so the two
  can no longer drift apart.

At the time of this probe neither table was changed by the work — TM-39 asked what the servers do,
not what the selection logic should do. CTY-02 later changed both, for the reason recorded below.

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

- **DL and OE are the same server today.** _(Superseded 2026-09-06 — this was the bug, see CTY-02
  below. The reading as recorded in August 2026 follows.)_ Both resolve `meshcom.oevsv.at` → `89.185.97.38` on this
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

## Update 2026-08-31 — CTY-01: nRF52-DHCP-Pfad kennt jetzt den IT-Split

Bis zur CTY-01-Behebung galt die Laenderunterscheidung auf nRF52 (Ethernet) nur im
Fix-IP-Pfad (`startFIXUDP()`); der DHCP-Pfad (`startUDP()`) ging im Internet-Zweig
immer auf `89.185.97.38`. Seit der Wave-A-Behebung hat `startUDP()` denselben Split
wie `startFIXUDP()`: IT → `145.239.75.155`, sonst → `89.185.97.38`, inklusive der
passenden NTP-Pool-Wahl. Die Tabellen oben beschreiben damit beide nRF52-Pfade.

## Update 2026-09-06 — CTY-02: the DL Internet fall-through was a regression, not a topology question

Upstream issue [#1133](https://github.com/icssw-org/MeshCom-Firmware/issues/1133) reported that a
Heltec V3 and a Heltec Wireless Tracker both ignored `--gateway srv dl` after upgrading to 4.35s.
They did. The RCA overturns the verdict this document and `docs/BACKLOG.md` recorded in August.

**What this document got wrong.** The August reading treated "DL has no case of its own on the
Internet path" as a deliberate server-topology decision for upstream to answer. It was not. The DL
Internet arm _existed_ — a bare literal `192.68.17.26` — and was **overwritten rather than
extended** when the Italian server was added on 2026-08-20. Every DL node on a normal Internet
uplink has landed on the Austrian server ever since. The bench run above measured the regression
faithfully and then mis-attributed it.

**The endpoint.** `meshcom.hamnet.network` resolves to `192.68.17.26` — the same literal that was
removed, and the same host the issue reporter used to check his node. There was never an open
question about which server DL should use.

**Why HAMNET was unaffected.** The HAMNET branch kept its DL arm (`meshcom.hamnet.cloud` →
`44.148.230.197`) throughout. Only nodes on a plain Internet uplink were hit — the typical German
home-WiFi node, which is why the report came from exactly that setup.

**The nRF52 half was never right.** `startFIXUDP()` and `startUDP()` have never had a DL arm on
their Internet branch — CTY-01 added only the `IT`/`OE` split. A DL RAK4631 gateway on
Ethernet/Internet had the same symptom, and always had.

**The fix.** The country/transport matrix was extracted from the three inline copies into one pure
table, `src/gwsrv_select.h`, consumed by `startMeshComUDP()` (ESP32) and by both nRF52 functions.
The table returns a DNS name _and_ the matching literal for every cell, so the resolver-based ESP32
path and the resolver-less nRF52 path can no longer disagree about where a country points. Pinned
by `test/test_gwsrv_select/` (9 cases, `pio test -e native -f test_gwsrv_select`), which was written
against the broken behaviour first and observed to fail on the DL-Internet cell before the fix.

| country | transport | host                     | literal        | path     |
| ------- | --------- | ------------------------ | -------------- | -------- |
| OE      | inet      | `meshcom.oevsv.at`       | 89.185.97.38   | `inet`   |
| OE      | hamnet    | —                        | 44.143.8.143   | `hamnet` |
| DL      | inet      | `meshcom.hamnet.network` | 192.68.17.26   | `inet`   |
| DL      | hamnet    | `meshcom.hamnet.cloud`   | 44.148.230.197 | `hamnet` |
| IT      | both      | `meshcom.dig-italia.it`  | 145.239.75.155 | `inet`   |

IT deliberately answers `inet` on both transports — it runs no HAMNET server — and that is what
drives the NTP choice on nRF52, so the collapse of the old per-branch NTP assignments to a single
`path`-keyed ternary is behaviour-preserving on every cell.

**Reserved arms for further countries.** `src/gwsrv_select.h` carries commented-out `HB`
(Switzerland) and `US` (United States) arms for both the HAMNET and the Internet block, with an
activation checklist at the foot of the file; `src/command_functions.cpp` carries the matching
ready-made replacement for the `--gateway srv` allow-list. Both places have to be uncommented — the
allow-list is checked first, so an arm alone stays unreachable. `node_gwsrv` is `char[3]` and the
comparison is `memcmp(.., 2)`, so a country code is exactly two uppercase characters; a longer one
would be a flash-format change.

**Bench proof 2026-09-06 — ESP32 confirmed, nRF52 outstanding.** `DK5EN-93` (Heltec V3, ORBI63
WLAN, non-HAMNET Internet path), flashed with the fix, `Gateway off` / `Webserver on` so no KEEP
could leave the node, and the fork-only `--srvip 192.0.2.1` (TEST-NET-1) armed as a sink first —
the `[GW];srv` marker is printed before DNS starts, so it reports the real selection while the
override keeps traffic off the live DL server:

```
>>> --srvip 192.0.2.1
[GW];srv;OE;host;meshcom.oevsv.at;path;inet;ms;67736
>>> --gateway srv dl
>>> --srvip 192.0.2.1
[WIFI]...inet UDP-DEST meshcom.hamnet.network
[GW];srv;DL;host;meshcom.hamnet.network;path;inet;ms;71802
[WIFI];dns;meshcom.hamnet.network;ip;192.0.2.1;ms;42      <- sink, not the real server
```

Node restored to `--gateway srv oe`, `--srvip 0.0.0.0` afterwards.

`DK5EN-90` (RAK4631) was flashed with the same commit and boots it, but **its Ethernet cable is
unplugged** (`Ethernet link OFF - skip DHCP`, `[ETH];event;link;down`). Both nRF52 entry points
bail before the selection code when the link is down — `initethfixIP()` returns after five LinkOFF
retries, `initethDHCP()` needs `startETH()` to succeed — so `startUDP()`/`startFIXUDP()` never ran
and the nRF52 half of this change has **no hardware evidence yet**. It builds under `-Werror` and
consumes the same unit-tested table as the ESP32 path, but the call-site wiring is unproven. Plug
the cable in and expect `[GW];srv;DL;host;192.68.17.26;path;inet` after `--gateway srv dl`. The August probe (`srvprobe.py`) is the right instrument for
the end-to-end proof and needs a bench node on USB; see the outstanding item in `docs/RESUME.md`.
