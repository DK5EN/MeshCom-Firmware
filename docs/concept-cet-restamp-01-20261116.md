# Concept: a node with NTP or GPS restamps `{CET}` right before LoRa transmit (CET-RESTAMP-01)

**A `{CET}` beacon carries the server's time and ages on its way: server leg, gateway queue, every relay queue. A
receiver sets its clock to that old time and runs behind by the whole age (S3 2026-11-15: 0.8 to 6.9 s). If the
transmitting node has a trusted clock (NTP or GPS with fix), it can overwrite the 19 time characters in its own TX copy
just before the radio starts, rounded and with the frame's airtime added. The receiver then lags only by its own RX
processing and the rounding, about half a second at most. The frame keeps its length and msg_id; only the payload
characters and the FCS change.**

Recommendation: Option B (restamp in the TX copy at the radio, on every node whose clock rank is NTP or GPS with fix,
gateway and relay alike), with airtime compensation and rounding to the nearest second. No change to the frame format,
0 extra bytes on air. Pure helper header plus host tests first, then the hook in the TX path, then a bench with the
`clock_drift.py` tool. Upstream proposal as its own PR after the fork has it in the field. Backlog row `CET-RESTAMP-01`;
analysis only, no source changed.

## Key facts

### Frame and payload

- A `{CET}` beacon is a text frame (`0x3A`): `[0]` type, `[1..4]` msg_id, `[5]` flags, from `[6]` `SRC[,via]>DEST:`, then
  the payload `{CET}YYYY-MM-DD hh:mm:ss` (UTC despite the name, whole seconds, always 24 characters), then the trailer
  (`0x00`, HW, MOD, 2-byte FCS, FW, LH). Parser: `cetParsePayload()` / `cetFindInFrame()` (`src/cet_age.h`).
- The FCS is the plain byte sum of everything before it (`src/aprs_functions.cpp:501-510` on RX,
  `:1153-1166` on encode). A receiver drops a frame whose FCS does not match.
- The firmware already rewrites a queued frame and fixes the FCS: `pnFrameSetMsgId()` (`src/pn_retry.h:537`, offset from
  `pnFrameFcsOffset()`), used for PN retries in `src/lora_functions.cpp:3640-3651`. Restamping needs the same two steps
  on the payload instead of the msg_id.

### Where a beacon waits today

- **Gateway:** a server copy enters the TX ring through `gwUdpRxEnqueue()` (`src/gw_udp_ring.h`), which drops it if it
  is older than `CET_MAX_AGE_S` (150 s) or the gateway's own clock is not trusted.
- **Relay:** `rx_relay` queues a received copy (`src/lora_functions.cpp:2617-2621`) after the same stale test.
- **TX point:** the ring slot is copied into `lora_tx_buffer` right before the send (`src/lora_functions.cpp:3006-3012`).
  The `[CET];tx;age_s;...;wait_ms;...` measurement line sits exactly there (`:3014-3040`). This is the latest moment
  the payload can still change, and the ring slot itself stays untouched (retries and supersede see the original).
- **Supersede:** a newer `{CET}` replaces a queued one in place (`supersedeIsCetText()`, `src/txring_functions.cpp:729`,
  TXP W1c). Restamping does not touch that rule.

### Clock inputs a restamp needs

- **Rank:** `src/time_rank.h` knows `TR_NTP` and `TR_GPS` (`enum TimeRank`, `timeRankBlockingCet()`); CET-SCATTER-01
  Part B made it the single source of "which clock is trusted". A restamp is allowed only while NTP or GPS with fix holds
  rank, never on RTC, phone, manual or CET time.
- **Sub-second:** the clock ticks whole seconds anchored at `millis()` (`SetClock()`, `src/clock.cpp`; tick remainder in
  `clockTickStep()`, `src/clock_tick.h`). There is no public "ms into the current second" accessor yet; the restamp
  needs one (small read-only addition, host-testable via the tick helper).
- **Airtime:** the RX path already computes airtime from the packet length (`src/lora_functions.cpp:899`); the same
  formula gives the TX airtime of the beacon (about 0.4 to 1.5 s depending on preset and length).

## The error budget

| Part                               | Today (bench 2026-11-15/16)       | With restamp                        |
| ---------------------------------- | --------------------------------- | ----------------------------------- |
| Server to gateway, server stamping | unknown, part of the measured age | gone                                |
| Gateway and relay queue wait       | 0.8 to 6.9 s (S3 T-Beam lag)      | gone (stamp at radio start)         |
| Airtime                            | in the age                        | compensated by the estimate         |
| Whole-second truncation            | up to 1 s late                    | rounding: at most 0.5 s either side |
| Receiver processing                | tens of ms                        | unchanged                           |

After CET-SCATTER-01 Part A the receiver takes the freshest beacon and holds older ones, so today's S4 result (clock 1.4
to 1.7 s behind host, `f2-clk-cet-20261116.summary`) is the age of the freshest copy heard. A restamped copy would be
the freshest by construction and pull that lag to under 0.5 s.

## Options

### Option A: only the gateway restamps, at injection

The gateway writes its own time when the server copy enters the ring (`gwUdpRxEnqueue()`).

- **For:** one call site, gateways are the nodes with NTP.
- **Against:** the queue wait after injection (the largest part, up to several seconds under load) stays in the age;
  relays keep forwarding the old stamp. Removes only the server leg.

### Option B: every transmitter with NTP or GPS restamps its TX copy (recommended)

At the TX point, after the copy into `lora_tx_buffer` and before the send: if the frame is a `{CET}` text, the node's
rank is NTP or GPS-with-fix, and the clock is set, overwrite the 19 time characters with `round(now_utc_ms + airtime_ms)`
and recompute the FCS.

- **For:** removes server leg, every queue wait and the airtime in one place; works on gateways and on relays with
  their own good clock; the ring slot, retries, supersede and dedup stay as they are.
- **Against:** a node changes the payload of a frame it did not originate. The msg_id stays, so dedup (by msg_id) still
  sees one beacon; a receiver may hear copies with slightly different stamps, and Part A takes the freshest.

### Option C: add a transit-age field instead of rewriting

The transmitter appends its queue time to the frame; the receiver adds it.

- **Against:** changes the wire format, costs bytes on every beacon, needs every receiver (upstream nodes, apps,
  server) to understand it. Not worth it while Option B costs 0 bytes.

## Interactions

- **Byte budget (REVIEW R4):** 0. The payload is fixed width; the frame length does not change.
- **Dedup:** by msg_id (`RX_DEDUP_*`), not by payload, so restamped copies are still duplicates of each other. The
  first copy a node hears wins dedup; with Option B that is a restamped one in most cases.
- **Stale drop (`CET_MAX_AGE_S`):** a restamped copy is about 0 s old at the next relay, so it is never dropped there.
- **CET-SCATTER-01 Part A:** forward-only filter prefers the freshest payload; restamped copies win, unrestamped older
  copies from other gateways are held (`[CET];hold;why;band`).
- **Part B rank:** unchanged; nodes with NTP or GPS ignore every `{CET}` anyway, restamped or not.
- **Upstream nodes:** they set the clock from any beacon at arrival; a restamped beacon is simply more accurate for them.
  No upstream code change is needed to benefit.
- **Server and apps:** the restamp only touches the outgoing TX copy. If another gateway uploads a restamped LoRa copy,
  it carries the original msg_id, so the server should treat it as a duplicate; to check on the mock (open point). Apps
  that log `{CET}` text show the restamped time.

## Plan

1. `src/cet_restamp.h` (pure, host-tested): `cetRestampFrame(frame, len, utc_ms, airtime_ms)` finds the payload,
   writes `YYYY-MM-DD hh:mm:ss` for the rounded time, recomputes the FCS; refuses non-`{CET}` frames, `{CET}<`, wrong
   length, missing FCS. Tests: round-trip with `cetParsePayload()`, FCS against the RX check, rounding at .499/.500,
   midnight and month and year rollover, refused frames untouched.
2. Clock: a read-only "ms into the current second" accessor on `Clock`, tested through `clock_tick.h`.
3. TX hook at `src/lora_functions.cpp` next to the `[CET];tx` line: rank gate (`TR_NTP` or `TR_GPS` with fix), airtime
   estimate, call, log `[CET];restamp;old_age_s;<n>;airtime_ms;<n>;ms;<n>`. ESP32 and nRF52 share this file.
4. Bench: a gateway with NTP (DK5EN-1 or -90) restamps; a LoRa-only node (DK5EN-92, `--timesrc cet`) measures with
   `clock_drift.py`. Exit: `|delta|` under 0.5 s on at least 5 beacons, `[CET];restamp` lines on the sender.
5. Upstream PR after field time, German text, in the wave plan after the CSMA track.

## Questions for the operator

1. **Who restamps:** every node with NTP or GPS (Option B, recommended), or only gateways (Option A, removes only the
   server leg)?
2. **GPS:** count GPS with fix as good enough to restamp (recommended: yes, a GPS fix is within a few ms), or NTP only?
3. **Default:** on by default (recommended, it only makes beacons more accurate), or behind a setting first for the
   field test?
