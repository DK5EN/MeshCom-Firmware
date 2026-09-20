# EXTUDP bench regression (TM-43) — both directions, on real hardware

The EXTUDP transport (`src/extudp_functions.cpp`, JSON over UDP `1799` /
`EXTERN_PORT`) had never been exercised by an automated test on hardware: every
EXTUDP defect so far — `N-22` (loop-task stack overflow), `N-23` (brick trap
without Ethernet) — was found by hand at the bench. TM-43 closes that gap, and
`UDP-01` is the open report it was built to answer: _"switching `--extudp on` on
a RAK kills the node"_.

The assertion that matters is **liveness**. Datagrams arriving is the easy half;
the failure under investigation is the node dying while they do.

---

## 1. Where the pieces live

| Piece                                   | What it is                                                                                                      |
| --------------------------------------- | --------------------------------------------------------------------------------------------------------------- |
| `tools/bench/extudp_peer.py`            | the host end of the link: binds UDP 1799, timestamps every datagram, parses the JSON, detects heartbeat gaps    |
| `tools/bench/test_extudp_peer.py`       | its unit tests — loopback sockets only, no hardware (`python3 -m unittest tools/bench/test_extudp_peer.py`)     |
| `tools/bench/rak_harness.py`            | scenario `extudp`: drives the node over serial, asserts both directions, liveness and the soak tail             |
| `test/test_getextern/`                  | the cheap half, natively: `getExtern()` parsing incl. the boundary and transport-size vectors (`native_extern`) |
| `src/extudp_functions.cpp` (instrument) | two fork-only lines `[EXT];rx;…` / `[EXT];tx;…` with the loop-task stack watermark                              |

Runs land in `tools/bench/runs/` like every other bench run: a summary JSON
(`--out`) plus the raw console log `rak_run_<stamp>.log` (and `.log.peer` for
the LoRa peer node).

---

## 2. How to run it

Cheap half first — no hardware, no node:

```sh
pio test -e native_extern                       # getExtern() parser + boundaries
python3 -m unittest tools/bench/test_extudp_peer.py
```

Then the hardware probe. Run it from `tools/bench/runs/` so the logs land there:

```sh
# RAK4631 DK5EN-90 (the node UDP-01 is about), with the Heltec as LoRa source
python3 ../rak_harness.py --scenario extudp \
    --port /dev/cu.usbmodem201301 \
    --peer-port /dev/cu.usbserial-0001 \
    --soak-seconds 600 --out extudp_rak.json

# Heltec V3 DK5EN-93 as the ESP32 control (same shared code, different platform)
python3 ../rak_harness.py --scenario extudp \
    --port /dev/cu.usbserial-0001 \
    --soak-seconds 120 --out extudp_heltec.json
```

The session handles both port families itself: `--dtr auto` (the default) turns
DTR **on** for `/dev/cu.usbmodem*` (the RAK's own CDC stays silent without it and
does not reset when opened) and **off** for a USB-UART bridge (which resets the
node on open — the harness then waits out the boot before the first command).
`--dtr on|off` overrides.

`extudp` is deliberately **not** part of `--scenario all`: it reconfigures the
node, may reboot it and carries a ten-minute soak tail by default, so it is asked
for by name.

Useful knobs: `--host-ip` (skip the auto-detection), `--ext-port`,
`--ext-start-timeout`, `--ext-trigger-timeout`, `--soak-seconds`,
`--soak-interval`.

**The node is left exactly as it was found.** The harness reads
`...EXTUDP on/off ...EXT IP x` and `--debug` before it changes anything and
writes all three back at the end, then re-reads `--info` to confirm
(`restore.matches_pre` in the summary). One caveat: `--extudpip none` _clears_
the field, so a node found holding the literal string `none` comes back with an
empty field — the same state to the firmware (`strlen < 7` disables the socket),
which is why the check normalises `none` / empty / the stray byte a cleared
buffer prints.

---

## 3. What is asserted

**Bring-up.** `--extudpip <host>` (host address auto-detected), `--extudp on`,
then wait for `[EXT]...now listening` — or, for a node already pointed at this
host, a datagram from it, because `startExternUDP()` prints its banner once and
then returns early while `hasExternIPaddress` is true. Only if neither shows up
does the harness reboot the node (N-23 order trap: gateway/Ethernet first, which
is exactly what the boot path does). The liveness baseline is taken **after**
that optional reboot, so the uptime and boot-line checks below cover the test
itself and nothing else.

**Send direction (node → host).** One trigger at a time, each asserting a
well-formed JSON datagram at the host **and** the matching `[EXT] Out:` console
line:

| Trigger          | Fired by                          | Expected datagram                                      |
| ---------------- | --------------------------------- | ------------------------------------------------------ |
| `sendpos`        | `--sendpos`                       | `src_type:"node"`, `type:"pos"`                        |
| `own_msg`        | `::{TEST}tm43 <n>` on the console | `src_type:"node"`, the marker text in `msg`            |
| `lora_from_peer` | `::{TEST}…` on `--peer-port`      | `src_type:"lora"`, `src` = the peer's call, the marker |

A trigger that logs `[EXT] Out:` but produces **no** datagram fails the run —
that is the `N-22` side symptom. FL-01 rate-limits the "send now" beacon to one
per 30 s; a `sendpos` that hits `[POS];shot;suppressed` is retried once after the
floor instead of being reported as a transport failure.

`sendpos` is reported as **SKIP** on a node whose own position is 0/0 with GPS
off (`--pos`): `PositionToAPRS()` returns an empty payload and `sendPosition()`
returns before it ever reaches `sendExtern()`, so the trigger cannot reach the
EXTUDP path on that node at all. That is a property of the node's configuration,
not of EXTUDP — which is why the position path is asserted on the control node
instead.

**Receive direction (host → node).**

- `{"type":"msg","dst":"TEST","msg":…}` → `[EXT] Inc:` on the console, then the
  node's own frame comes back out on the same socket (`sendMessage()` →
  `sendExtern(…,"node",…)`, `loop_functions.cpp:3798`). That echo is the proof
  the datagram reached the send path and is visible from the host, not only on
  the console; `[BP];notice` (ring depth), `RING_WRITE` and `OnTXDone` are
  collected as supporting console evidence.
- `{"type":"tele",…}` → either `[EXT] tele accepted:` with the push beacon that
  follows (or FL-01's suppression line, which equally proves `sendPosition()`
  was reached), **or** the documented refusal — the `tele ignored` line a node
  with real sensor hardware (a BME280 here) must print instead. That refusal is
  a pass in its own right.

**Rejection vectors.** All seven of `extudp_peer.rejection_vectors()` — broken
JSON, missing `dst`, missing `msg`, `dst` 10 chars, `msg` 151 chars, a datagram
truncated mid-JSON, and one of the full `UDP_TX_BUF_SIZE` (255 B). Each must be
seen (`[EXT] Inc:`) and logged with a reason; UDP is lossy, so a vector that
never arrived is retried once — a vector that _arrived_ and was not logged is the
failure. **The 255-byte vector is deliberately sent last** (see §6).

**Liveness — the part that catches the reported bug.**

- uptime strictly increasing from the baseline to the end of the run,
- no `[BOOT]` / `RESETREAS=` line after the baseline,
- no `HardFault` / assert / watchdog hit (the harness's `CRASH` regex),
- the console still echoing at the end,
- **and the UDP socket still alive**: one rejected probe datagram after the
  vector barrage must still show up as `[EXT] Inc:`. A node can be perfectly
  alive on the console with its inbound path dead — §6 is exactly that case.

**Soak tail.** `--soak-seconds` (default 600) of traffic in both directions,
every `--soak-interval` (3 s). Every fourth datagram is a real message (the deep
path: `getExtern()` → `sendMessage()` → TX ring → `sendExtern()`), the others are
malformed and stop inside the parser — that keeps the inbound path under constant
load without putting a LoRa frame on the air every three seconds. `--sendpos`
runs once per FL-01 window. Afterwards: the same liveness check, the number of
inbound datagrams the node actually picked up, and the heartbeat sequence-gap
check. With `-D MC_TEST_HOOKS` the node emits a 500 ms sequence-numbered
heartbeat (`extudp_functions.cpp` ~:337) and a gap in `seq` dates a stall to the
millisecond; stock builds do **not** define `MC_TEST_HOOKS`, so `heartbeats: 0`
is expected and only a _gap_ is a failure.

---

## 4. The firmware instrument (fork-only)

Two raw `Serial.printf` lines, one per direction, in `src/extudp_functions.cpp`:

```
[EXT];rx;len;<bytes>;stack_hwm;<W>;ms;<T>     right after getExtern() returns
[EXT];tx;len;<bytes>;stack_hwm;<W>;ms;<T>     at the end of sendExtern()
```

Raw `Serial.printf`, not `printfdeb()`: `printfdeb` is gated on `--debug` and
strips `;`, and `DEBUG_MSG` compiles away entirely. `W` is
`uxTaskGetStackHighWaterMark(NULL)` — the **lifetime minimum** of free stack for
the task that runs the loop, in **words on nRF52/FreeRTOS (× 4 = bytes)** and in
**bytes on ESP32**. Because it is a lifetime minimum it never rises; the moment
it _drops_ is the moment a new deepest path was taken, which is what makes the
attribution below possible.

---

## 5. Measured, 2026-08-30

Build: `4.35p`, branch `tdeck-partial-refresh-trace`, both nodes flashed with the
instrument.

### RAK4631 DK5EN-90 (`wiscore_rak4631`, W5100S, gateway on, webserver on)

`python3 ../rak_harness.py --scenario extudp --port /dev/cu.usbmodem201301 --peer-port /dev/cu.usbserial-0001 --soak-seconds 600`
→ **PASS** (`tools/bench/runs/extudp_rak_20260830.json`, log `rak_run_20260830-211910.log`)

| Check                                   | Result                                                                                        |
| --------------------------------------- | --------------------------------------------------------------------------------------------- |
| `sendpos`                               | SKIP — LAT/LON 0.0000, GPS off: `sendPosition()` returns before the EXTUDP path (see §3)      |
| `own_msg`                               | PASS — 153 B `type:"msg"` datagram + `[EXT] Out:`                                             |
| `lora_from_peer`                        | PASS — 155 B `src_type:"lora"`, `src:"DK5EN-93"` datagram + `[EXT] Out:`                      |
| `msg_to_txring`                         | PASS — `[EXT] Inc:` → `[BP];notice;QRS;depth;2` → own frame echoed back on the socket         |
| `tele`                                  | PASS — `[EXT] tele accepted: temp=23.3 hum=60.0 press=1018.5`; no push beacon (no position)   |
| all 7 rejection vectors                 | PASS — each seen and logged with a reason                                                     |
| `inbound_alive` after the barrage       | PASS — the 255-byte datagram does **not** wedge `EthernetUDP` (contrast §6)                   |
| soak 601.5 s                            | 197 datagrams in (50 of them real messages), **197 picked up**, 34 out, no boot, no crash     |
| heartbeat gaps                          | none (stock build: `MC_TEST_HOOKS` undefined, so no heartbeat at all)                         |
| liveness (uptime / crash / boot / echo) | PASS — 23 667 → 691 145 ms, no `RESETREAS`, console still echoing                             |
| restore                                 | PASS — `EXTUDP on`, `EXT IP 192.168.68.58`, `--debug off`, confirmed by `--info`              |
| `stack_hwm`                             | rx min **69 words = 276 B** free, 207 rx samples / 43 tx samples (see §7 for the attribution) |

### Heltec V3 DK5EN-93 (`heltec_wifi_lora_32_V3`, WiFi, gateway off) — the control

| Check                                   | Result                                                                       |
| --------------------------------------- | ---------------------------------------------------------------------------- |
| `sendpos`                               | PASS — 258 B `type:"pos"` datagram + `[EXT] Out:`                            |
| `own_msg`                               | PASS — 153 B `type:"msg"` datagram + `[EXT] Out:`                            |
| `msg_to_txring`                         | PASS — `[EXT] Inc:` + the node's own frame echoed back on the socket         |
| `tele`                                  | PASS — documented refusal: `real sensor hardware detected` (BME280 on board) |
| all 7 rejection vectors                 | PASS — each seen and logged with a reason                                    |
| `inbound_alive` after the barrage       | **FAIL** — pre-fix measurement, see §6; PASS after the UDP-02 fix (§6 table) |
| soak 122 s                              | 40 datagrams in, **0 picked up**, 24 out, no boot line, no crash             |
| liveness (uptime / crash / boot / echo) | PASS — 26 094 → 215 067 ms, console still echoing                            |
| `stack_hwm` rx / tx                     | 956 B free (of 8 KB) on both directions                                      |

---

## 6. `UDP-02` — one 255-byte datagram permanently killed EXTUDP receive on ESP32 (fixed)

**Status: fixed 2026-08-30 in `getExternUDP()`, verified on DK5EN-93.** The
analysis below is the state before the fix; the fix and its evidence follow at
the end of the section. The §5 Heltec table is the pre-fix measurement and is
left as it was measured.

Reproduced on DK5EN-93 in both full runs and in a targeted probe: the first
255-byte datagram was processed normally, and every one of the six datagrams
sent after it — including further 255-byte ones — was ignored without a trace,
until the node was restarted.

`getExternUDP()` reads at most `UDP_TX_BUF_SIZE - 1` = **254** bytes
(`UdpExtern.read(incomingExtPacket, UDP_TX_BUF_SIZE - 1)`). A datagram of 255
bytes or more therefore leaves at least one byte unread — and in
arduino-esp32's `WiFiUdp.cpp`:

```c
int WiFiUDP::parsePacket(){
  if(rx_buffer)          // a partially consumed packet is still held
    return 0;            // -> no further packet is ever fetched
  ...
}
int WiFiUDP::read(char* buffer, size_t len){
  int out = rx_buffer->read(buffer, len);
  if(!rx_buffer->available()){ ... delete b; }   // freed only when fully drained
}
```

`rx_buffer` is released only when it has been read to the end, so after one
oversized datagram `parsePacket()` returns 0 forever: the node keeps sending
(beacons, relays, `[EXT] Out:` all continue), the console keeps echoing, uptime
keeps climbing — and every inbound datagram is silently ignored until the next
reboot. In the 122 s soak: 40 datagrams sent, **0** `[EXT] Inc:` lines, 24
datagrams still going out.

That is an unauthenticated LAN input that permanently disables a documented
feature with no error anywhere, and it matches the "EXTUDP is silently dead"
class of report in UDP-01.

The RAK's `EthernetUDP` (W5100S) does **not** behave this way — it processed the
vector and everything after it — which is also why the vector order in
`rejection_vectors()` is pinned by a unit test: anything sent after it on ESP32
would be misreported as "never arrived".

### The fix

`getExternUDP()` (`src/extudp_functions.cpp`) now drops whatever the read left
behind, so the socket cannot stay wedged:

```c
len = UdpExtern.read(incomingExtPacket, UDP_TX_BUF_SIZE - 1);

if (packetExtSize > len)
{
  UdpExtern.flush();
  Serial.printf("[EXT] oversized datagram drained: %d of %d bytes read\n", len, packetExtSize);
}
```

Three things this deliberately does **not** do: it does not enlarge the
processing buffer (the 254 bytes that were read are still handed to
`getExtern()` and still rejected there as truncated JSON — an oversized
datagram remains bad _data_, the point is only that the socket survives it), it
does not touch the outbound path, and it costs nothing on the normal path
(`packetExtSize > len` is false for every datagram that fits).

`WiFiUDP::flush()` is the right primitive: it deletes the held `rx_buffer`
outright (`WiFiUdp.cpp`, arduino-esp32 2.0.14 and 3.x alike), which is exactly
the release that a fully drained `read()` would have performed — no
read-and-discard loop needed. On the RAK the call is inert: `EthernetUDP::flush()`
is an empty TODO there, and `EthernetUDP::parsePacket()` discards the remainder
of the previous packet itself before fetching the next one, which is why the
W5100S never wedged in the first place.

### Evidence

Same node, same scenario, same 120 s soak, only the firmware changed
(`pio run -e heltec_wifi_lora_32_V3`, flashed 2026-08-30 21:43):

`python3 ../rak_harness.py --scenario extudp --port /dev/cu.usbserial-0001 --soak-seconds 120`
→ **PASS** (`tools/bench/runs/extudp_heltec_udp02fix.json`, log
`rak_run_20260830-214440.log`)

| Check                             | Before                              | After                                       |
| --------------------------------- | ----------------------------------- | ------------------------------------------- |
| all 7 rejection vectors           | PASS (each seen and logged)         | PASS (each seen and logged)                 |
| `full_255_byte_datagram`          | seen, then the socket stayed wedged | seen, `deserializeJson() failed`, drained   |
| `inbound_alive` after the barrage | **FAIL** — probe never seen         | **PASS** — probe seen as `[EXT] Inc:`       |
| soak 122 s                        | 40 datagrams in, **0 picked up**    | 40 datagrams in, **40 picked up** (10 msgs) |
| liveness / restore                | PASS                                | PASS — 26 266 → 183 336 ms, restore matched |

Five `[EXT] oversized datagram drained: 254 of 255 bytes read` lines in the run
(one per 255-byte vector: the barrage plus the four the soak loop replays), each
followed by normal traffic — the socket now survives every one of them.

---

## 7. What this says about UDP-01

UDP-01 asks four questions of the reporter and names one measurement this branch
still owed: **the inbound path had never been stack-measured.** `N-22` measured
only the outbound path (`checkSerialCommand()` → `sendMessage()` → `sendExtern()`,
watermark 0 before the fix). Inbound is deeper — `getExternUDP()` → `getExtern()`
(`char val[163]` plus an ArduinoJson `JsonDocument` on the stack) →
`sendMessage()` → `sendExtern()` — and on nRF52 it runs in the same 4 KB loop
task (`LOOP_STACK_SZ`, Adafruit core; `nrf52_main.cpp:2423`).

**Measured, DK5EN-90, 601 s run, 250 instrument samples.** The watermark is a
lifetime minimum, so every _drop_ names the moment a new deepest path was taken.
The four drops of the whole run, in order:

| Free stack    | Taken by                                                                                     |
| ------------- | -------------------------------------------------------------------------------------------- |
| 164 w = 656 B | console message: `checkSerialCommand()` → `sendMessage()` → `sendExtern()` — the `N-22` path |
| 110 w = 440 B | gateway inbound: UDP → LoRa (`src_type:"udp"`, `nrf_eth.cpp:429`) → `sendExtern()`           |
| 106 w = 424 B | **the EXTUDP inbound path**: `[EXT] Inc:` → `getExtern()` → `sendMessage()` → `sendExtern()` |
| 69 w = 276 B  | gateway inbound again, with a 175-byte payload — the minimum of the whole run                |

Two things follow, and neither is what the report assumed:

1. **The EXTUDP receive path is not the deepest consumer.** It bottoms out at
   424 B free; the gateway's own UDP → LoRa path goes 148 B deeper on the same
   4 KB task. EXTUDP inbound costs about 16 B more than the outbound path it
   sits on top of — the `JsonDocument` and `char val[163]` are already accounted
   for by the time `sendExtern()` runs.
2. **The headroom is thin but not zero.** 276 B of 4096 (6.7 %) at the worst
   moment of a ten-minute run under continuous traffic. `N-22` was watermark
   **0**; this is not that. But it is close enough that any few-hundred-byte
   addition to _either_ path — not just the EXTUDP one — re-creates `N-22`, and
   the next such change should be re-measured with exactly these two lines.

Under that load the node did not reset, did not stall and answered every one of
the 197 datagrams: on this bench, with this build, `--extudp on` does not kill a
RAK4631.

Two things this run does **not** settle, deliberately:

- The setup gate at `nrf52_main.cpp:1086` (`bGATEWAY || bWEBSERVER`) is
  untouched. Whether it should become `… || bEXTUDP` is an open operator
  decision in the backlog, not a task: it re-opens exactly the door `N-23`
  closed, and must not move without a hardware-present check.
- Nothing here reproduces "the node dies". On this bench, with a current build,
  it does not — which is why UDP-01 still needs the reporter's firmware version,
  the exact moment of death, `RESETREAS` versus a silent console, and the
  `--gateway` / `--webserver` / `--extudpip` state before any code is touched.
  What the run _does_ add is a second silent-death mechanism (§6) that a user
  would describe exactly the same way.

---

## 8. Native coverage that backs this up

`test/test_getextern` (32 cases, `pio test -e native_extern`) carries the parser
half so hardware time is spent only on transport and stack. TM-43 added the
boundary and transport-size cases:

| Case                                                             | Pins                                                                                               |
| ---------------------------------------------------------------- | -------------------------------------------------------------------------------------------------- |
| `dst` exactly 9 accepted / 10 rejected                           | the `strlen(dst) > 9` boundary from both sides, plus the `[EXT] invalid lengths` log line          |
| `msg` exactly 150 accepted / 151 rejected                        | ditto for `strlen(msg) > 150`                                                                      |
| a full 255-byte datagram, delivered as the node reads it (254 B) | the transport truncation: the closing brace is lost, so it must fail as `deserializeJson() failed` |
| a full 254-byte datagram                                         | the counter-example: the largest payload that arrives intact must be processed in full             |
| truncated mid-JSON                                               | the parser must not run past the end of the buffer                                                 |
| missing `dst` / missing `msg`                                    | the log line the bench probe waits for per vector                                                  |

The three `PT-01` findings that were pinned here are fixed as of 2026-08-30 and their cases are real assertions now (the
`"none"` sentinel collision, the silent truncation at combined maximum lengths,
and the embedded-NUL truncation) — they are documented behaviour, not fixed here.
