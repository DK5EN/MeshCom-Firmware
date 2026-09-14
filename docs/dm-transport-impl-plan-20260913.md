# DM transport reliability + store node — implementation and test plan

Plan, 2026-09-13. Derived from `docs/dm-reliability-and-store-node-verdict-20260913.md` (design and
traps), `docs/MeshCom-Store-Node-Concept-20260911.md` (the role) and
`docs/review/fable-dm-store-node-verdict-20260913.md` (review findings). Nothing in code yet.

## Stage status log

| Stage | Content                                 | Status                                                                                                                                                          |
| ----- | --------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 0     | ARQ repair, instrumentation, `--airgap` | **code in tree 2026-09-13**, native + build gate green; bench T-0.1..T-0.5 and the advisor pass open                                                            |
| 1     | Outbox + the 9-send ladder              | not started — needs M0-1 (below)                                                                                                                                |
| 2     | Destination dedup + bounded ACK repeats | **2.1 dispatched 2026-09-14** (pulled ahead of stage 1: receivers must understand fresh-id attempts before any sender emits them); **2.2 deferred** (see below) |
| 3     | Store node + mailbox GUI                | not started                                                                                                                                                     |
| 4     | Sender-visible custody notice           | deferred, not planned                                                                                                                                           |

Resume rule: this table is the authority. A compacted or interrupted session reads it, not the
git log.

## Decisions in force

| ID  | Decision                                                                                                                                     |
| --- | -------------------------------------------------------------------------------------------------------------------------------------------- |
| D1  | Ladder is (3x40 s + 1 min) x 3 = **9 transmissions over 9 minutes**, then report failure. Rate is unconditional; no evidence gate.           |
| D2  | Store-node delivery: direct neighbour only, **`max_hop` 0 on the raw wire field**.                                                           |
| D3  | Custody ACK is display-only, which in v1 means **no on-air notice at all** — the store node logs it locally. A `0x41` would stop the ladder. |
| D4  | Store set is `heard`, against the existing **12 h** mheard window. No mheard change.                                                         |
| D5  | Store node runs the same 9-send ladder per presence trigger, then a **one-hour cooldown**, until ack or `storetime`.                         |
| D6  | Group messages **out of scope** — not stored, and not given the fresh-msg_id repair.                                                         |
| D7  | **No pull model**, no retrieval request, no multi-hop retrieval.                                                                             |
| D8  | Failure is reported as BLE status **`0x03`** plus the web GUI, scoped to user-originated DMs only.                                           |

## Open measurement that gates stage 1

**M0-1. Does a ring slot survive 9 minutes on a real node?** Disputed during design and not
settled: a parked retransmit slot is overwritten once `iWrite` laps the ring (20 enqueues), because
`addTxRingEntry()` writes unconditionally at `w = iWrite` (`src/txring_functions.cpp:490`,
`:500-503`) before any priority logic runs, and a parked slot sits behind `iRead` where the
priority scan (`:549-566`) cannot see it. Whether 20 enqueues happen inside 9 minutes depends on
ambient relay load, not on what the operator does.

Stage 0's counters make this measurable under normal traffic. Take the answer from a bench node
sitting on the live network for an hour — no constructed load, no burst.

- **If ring slots survive**: stage 1 may park the payload in the ring and skip the outbox.
- **If they do not**: stage 1 implements the outbox as specified below.
- **Recommendation regardless**: implement the outbox. A mechanism whose reliability varies with
  ambient relay traffic behaves differently on a quiet home node and a busy hilltop, and the
  difference does not show up in testing. 1.3 kB buys determinism.

---

## Stage 0 — ARQ repair and instrumentation

Self-contained, upstream-PR-sized, repairs real defects on its own, and produces the measurements
every later stage needs. No behaviour depends on a decision that is still open.

### 0.1 Mint ACK ids from `millis()`, drop the flash write

**File:** `src/loop_functions.cpp` (`SendAckMessage()`, ~`:4910-4990`).

All eight `node_msgid++` sites are followed by `save_settings()`. On nRF52 that re-reads the whole
struct from LittleFS and, because `node_msgid` always differs, does a full remove and rewrite every
call; on ESP32 it is ~20 NVS `put*()` per send with no change detection. The gateway ACK path at
`src/lora_functions.cpp:1241` already mints from `millis()` and writes nothing — copy it.

- `SendAckMessage()` mints `msg_id` from `millis()`, does **not** touch `node_msgid`, does **not**
  call `save_settings()`.
- The `{NNN` in the DM payload keeps coming from `node_msgid` — that is the transport sequence
  number and must stay as it is.
- Do not touch the other seven sites in this stage.

**Acceptance:** sending 20 DMs produces zero settings writes from the ACK path. Verify by
instrumenting `save_settings()` with a counter behind `bLORADEBUG`, or by LittleFS write counts on
nRF52.

**Note:** advisor M5's justification for this ("runs in the 1 kB timer task") is **false** — nRF52
RX runs in the dedicated 16 kB `_lora_task` (`src/boards/mcu/board.cpp:498`). The change is
justified by flash wear alone. While in the file, fix the stale comment at
`src/nrf52/nrf52_main.cpp:3040` that still asserts the timer-task belief.

### 0.2 Split the dedup gate — re-ACK on duplicate-for-me

**File:** `src/lora_functions.cpp` (the gate at `:896`, the ACK emission at `:1072`).

Today one `setlogCountDedup(rx_is_new)` verdict gates display, relay **and** ACK emission. A
duplicate DM addressed to this node is therefore silently dropped and never re-acked, so a lost ACK
can never be repaired. This alone fixes one-hop ACK loss, which is the common handheld-to-handheld
case, and it is a prerequisite for everything in stages 1-3.

- Duplicate **and** `msg_destination_call == node_call` **and** payload contains `{NNN` ->
  call `SendAckMessage()`, then stop: do not display, do not forward to phone or server, do not
  relay.
- Rate-limit to one re-ACK per (source, NNN) per 60 s so a replayed frame cannot be amplified.
- Everything else about the gate is unchanged.

**Acceptance:** a replayed one-hop DM produces exactly one additional `:ackNNN` and no second
display; a replay inside 60 s produces neither.

### 0.3 Report failure to the app and the GUI (D8)

**Files:** `src/lora_functions.cpp` (`updateRetransmissionStatus()` give-up path, `:2100-2117`),
`src/ack_attribution.h` (status constant).

Today `RETRANSMIT_GIVEUP` writes a debug line and nothing else. Emit BLE status `0x03` through the
existing `buildAckPhoneFrame()`, which already carries a length-prefixed callsign so a parser that
walks the frame will not desync on an unknown status.

- **Scope it to user-originated DMs only.** Advisor m6: ACK frames and broadcast texts are also
  retransmit-eligible and legitimately give up; reporting those unscoped would give every node in a
  sparse net bogus failure notices about its own ACKs, and every broadcast sender about its
  broadcasts.
- Mark the message failed in the web GUI message list as well.
- Verify against `mc-chat` and `MCProxy` (ours). Do not block on McApp, which we do not control.

**Acceptance:** a DM to an unreachable callsign produces exactly one `0x03` after the last attempt;
a broadcast and an ACK that give up produce none.

### 0.4 STAT counters and the RTT histogram

**Files:** `src/lora_functions.cpp`, the setlog STAT line.

There is no end-to-end DM outcome measurement in the network today. Without these, stages 1-3
cannot be shown to have worked, and M0-1 cannot be answered.

Counters: DMs sent, echo heard, gateway ACK, peer ACK, gave up, attempts per DM. Plus
send-to-`:ackNNN` time as a small histogram (suggest 6 buckets: <15 s, <40 s, <2 m, <9 m, <30 m,
older). Plus, for M0-1: **ring enqueues per minute** and **parked-slot overwrite count** — a
counter incremented when `addTxRingEntry()` lands on a slot whose length is non-zero and whose
status is a retransmit-pending value.

**Acceptance:** counters appear in `--setlog`, survive an hour of live traffic, and the
overwrite counter is non-zero only under load.

### 0.5 The `--airgap` bench instrument

**Files:** `src/command_functions.cpp` (the command), `src/lora_functions.cpp` (the two hooks).

The bench needs a node that is convincingly out of range while its console stays alive. Power-down
loses the console exactly when the node's return is interesting; an SSID rename needs a settings
write, a reboot, and a wait for the store node to learn the new callsign via mheard — two extra
variables per run.

- `--airgap on|off`, default off, **not persisted** (RAM flag only, so a reboot always clears it).
- **RX hook:** in `OnRxDone()`, before `is_new_packet()` and before any mheard or dedup work, drop
  the frame entirely when the flag is set. Dropping _before_ dedup is what keeps the node's dedup
  ring and heard tables clean — the node behaves exactly as if the frame never arrived, which is
  the concern raised during design and is why the hook goes at the radio boundary and nowhere else.
- **TX hook:** in `doTX()`, refuse to transmit while the flag is set. Same place as the existing
  `TX DISABLED` backstop.
- Print a single unmistakable marker on each transition: `[AIRGAP];on` / `[AIRGAP];off`, as a raw
  `Serial.printf`, **not** `DEBUG_MSG` — `DO_DEBUG 0` compiles `DEBUG_MSG` away entirely and the
  marker would silently not exist.
- Gate the whole thing on the existing instrument build flag so it can be compiled out of a
  release image, and string-scan the image afterwards to confirm it is gone (INS-01 precedent).

**Acceptance:** with `--airgap on` the node emits nothing and its mheard/dedup state is unchanged
after a minute of nearby traffic; `--airgap off` restores normal behaviour without a reboot.

### Stage 0 implementation notes (2026-09-13)

Deviations from the text above, decided at the wave gate:

- **Re-ACK limiter is 30 s, not 60 s, and the original ACK seeds it.** A relay's copy of a DM
  arrives at the destination as a same-`msg_id` duplicate seconds after the original; unseeded,
  every relayed DM would have been acked twice. Seeding at the original ACK suppresses that, and a
  60 s window would then swallow the sender's first retry (40 s cadence), so the window is 30 s.
  `src/reack_limiter.h`, test `test/test_reack_limiter`.
- **The `DM` setlog line prints before the STAT line**, same tick and same `bDisplayLog` gate.
  `setlogFillStat()` owns the counters; the STAT print itself sits in each platform's main loop.
- **`dmstat_echo` counts every own text echo**, DM or not: `own_msg_id[]` carries no destination.
- **`dmstat_attempts` counts every text-slot transmission**, first send and retry alike.
- **`{` in DM text is rewritten to `(` at the sender** (`sendMessage()`, `bDM` only) so the
  receiver's `{NNN` parse stays unambiguous.
- Counter module is `src/dm_stats.{h,cpp}` (native-tested), the airgap flag lives in
  `src/instrument.cpp` behind `INSTRUMENT_ENABLED`, and the give-up status is `ACK_STATUS_FAILED`
  (`0x03`) in `src/ack_attribution.h`; the web GUI renders it as a ballot X.
- Release-image string scan: `AIRGAP` count 0 on the non-instrument Heltec V3 image.
- **Advisor rework (docs/review/fable-dm-stage0-verdict-20260913.md, same day):** the airgap
  RX hook now sits after the platform RX plumbing and tears receive state down like the
  `handleACK()` return path (F1, nRF52 stayed TX-mute otherwise); the re-ACK branch excludes
  `{ping}`/`{pong}` (F2) and treats server-injected frames like the original ACK path (F7);
  `dmstat_echo` counts only own text frames with an ack tag (F3); a late echo no longer
  downgrades a failed (`0x03`) message (F6); a re-noted NNN replaces the stale send time (F5);
  tests cover the DONE/READY/EXT_PENDING exclusions and prefix callsigns (F4).

### Stage 0 gate

- `pio run -e native_dedup`, `-e native_aprs`, `-e native_parsers`, and the full native suite.
- Build every affected board env cleanly and **sequentially** — parallel `pio run` on the same env
  corrupts `.pio/build` archives.
- Bench: T-0.1 through T-0.4 below.
- Fable advisor re-reads the diff against these acceptance criteria before stage 1 starts.

---

## Stage 1 — outbox and the 9-send ladder

**Depends on:** stage 0, and the M0-1 answer.

### 1.1 The outbox

**File:** new `src/outbox_functions.{h,cpp}`, hooked from `src/loop_functions.cpp`.

Sized 5 slots on ESP32-S3/nRF52 (~1.3 kB against 150 kB headroom) and 3 on classic ESP32 (~780 B
against 6.6 kB). Each slot: source, destination, NNN, payload, first-send time, attempt counter,
next-attempt time, state.

**Keyed on NNN, never on a reconstructed msg_id** (T1). The incoming ACK rebuilds
`msg_id = (_GW_ID << 10) | NNN` and `findAndStopRingSlot()` compares the full 32-bit value read
from bytes 3-6 of the stored frame. Attempts 2..n carry a fresh msg_id whose low 10 bits are no
longer NNN, so matching on the reconstructed id stops nothing and the ladder would run to its cap
after successful delivery. The outbox must stop **every attempt sharing the NNN**.

Do not rely on `own_msg_id` as the authority (T2): it is 20 slots, and beacons consume one per
transmission via an inline write at `src/loop_functions.cpp:4829-4831` that bypasses
`insertOwnTx()`. Still call `insertOwnTx()` per attempt so echo recognition keeps working.

### 1.2 The ladder

**File:** `src/lora_functions.cpp` (`updateRetransmissionStatus()` or a sibling driven from the
loop task).

- 3 attempts 40 s apart, 1 minute gap, 3 more, 1 minute gap, 3 more = 9 transmissions over 9
  minutes, then give up and report per 0.3.
- Attempt 1 keeps the **same msg_id and is echo-gated**: if `own_msg_id[][4]` is still `0x00` after
  ~15 s nobody relayed it and a same-id retry is the only kind that can help; if it was relayed,
  skip to the first fresh-id attempt. This removes the one genuinely wasted transmission per DM
  without changing the rate.
- Attempts 2..9 mint a **fresh msg_id from `millis()`**, keep NNN stable, keep the same payload and
  `max_hop`, and register with `insertOwnTx()`.
- Gated on the existing back-pressure state: no attempt while QRS/QRT is latched
  (`bp_state.refusing()`, `src/loop_functions.cpp:4070`), at most one outbox action per 30 s.
- nRF52: all ladder work in the loop task, nothing in the receive path.

**Acceptance:** T-1.1 through T-1.3 below.

---

## Stage 2 — destination dedup and bounded ACK repeats

**File:** `src/lora_functions.cpp`, plus the server/GATE receive path.

### 2.1 Dedup on (source, NNN)

16 entries, ~14 B each. **Aged by time, not by count** — `node_msgid` is one 0..999 counter shared
by DMs, positions, HEY, pings and ACKs, and the DL6MDF-11 incident burned 1000 ids in ~50 s
(`src/beacon_rate.h:22-30`). Drop entries older than ~1 h, and compare payload length plus a 16-bit
CRC of the stripped payload before suppressing a display.

Hook **both** receive paths — LoRa and server/GATE — or a DM arriving by two routes still displays
twice (advisor M6). A duplicate is never displayed, never forwarded twice, and **always re-acked**.

**Implementation notes (2026-09-14):** module `src/dm_dedup.{h,cpp}` (16 slots, keyed on source
call + NNN, aged 1 h, payload length + 16 bits of CRC32; a same-NNN different-payload sighting
counts as the counter having wrapped and replaces the entry). Hooked in three places, one table:
LoRa RX in `OnRxDone()`, the ESP32 server path in `getMeshComUDPpacket()`, and the RAK4631
Ethernet gateway path in `src/nrf52/nrf_eth.cpp`, which is a hand-duplicated twin of the ESP32
server path and would otherwise have displayed every attempt. A duplicate is re-acked through the
stage 0 limiter, not displayed, not forwarded to the app; mheard and the msg_id ring still see the
frame. The limiter table moved from the header into `src/reack_limiter.cpp` so all three paths
share one 30 s window per (call, NNN). Test `test/test_dm_dedup` (11 cases).

### 2.2 Bounded ACK repeats — deferred (2026-09-14)

Not built. With stage 0 in place a lost ACK is repaired by the sender's own 40 s retry plus the
receiver's re-ACK, driven by an actual signal that the ACK did not arrive. Blind repeats would add
up to two ACKs to every DM on a two-node link, where the echo stop never triggers, for a loss case
that is already covered. Revisit only if the stage 1 measurements show ACK loss the retry path
does not close.

ACKs are enqueued `RING_STATUS_DONE` and never retransmitted (`src/loop_functions.cpp:4972-4975`),
so this needs its own small scheduler, not the ring's retransmit path.

- At most **2 repeats**, each with a fresh `millis()`-minted msg_id.
- **Stop on echo**: hearing the ACK relayed (`own_msg_id[][4] == 0x01`). Nothing acks an ACK, so in
  a two-node link the echo never comes and every repeat would otherwise fire — hence the hard cap
  as well as the echo stop.

---

## Stage 3 — store node

**Depends on:** stage 2. Fork-first; a new node role needs the concept accepted upstream before
code goes in a PR.

Three mostly-disjoint file sets, so this is the one stage that can be fanned out:

| Owner | Files                                                    | Content                                                           |
| ----- | -------------------------------------------------------- | ----------------------------------------------------------------- |
| A     | new `src/msgstore_*.{h,cpp}`, `lora_functions.cpp` hooks | Mailbox core: store, purge, presence trigger, delivery, §3.5 caps |
| B     | `src/web_functions/web_functions.cpp`                    | `/?page=mailbox` + the switch confirmation                        |
| C     | `src/command_functions.cpp`, settings                    | `--store`, `--storetime`, `--storeslots` via NVS keys (T13)       |

Constraints that must be in every brief:

- **`ENABLE_MSGSTORE` gates the whole feature** to ESP32-S3 and nRF52840. Classic ESP32 has ~6.6 kB
  headroom and is not eligible as a host — but still benefits as sender and receiver.
- **Settings must not touch `struct s_meshcom_settings`** (T13). Adding fields forces a
  `FLASH_STRUCT_VERSION` bump, which runs `clear_flash()` on every updating node — callsign, WLAN,
  sensors. Use own NVS keys or spare bits in existing fields, exactly as `max_hop_text` did; the
  pattern is documented at `src/configuration_global.h:113-118`.
- **Delivery is `max_hop` 0 on the raw field** (D2). `--maxhop` clamps to 1..6
  (`src/maxhop.h:17-18`), so the standard setter would floor at 1 and permit a relay hop. Relays
  skip `max_hop` 0 explicitly (`src/lora_functions.cpp:1431`, reason `hop0` at `:1526`).
- **Never emit a `0x41`** for a stored DM (D3, T9).
- **Direct-neighbour test** (T8): `msg_source_path` holds exactly one callsign. Path fields are
  populated only for payload types `:`, `!`, `@` (`src/aprs_functions.cpp:155`). **Guard on
  `msg_server` / gateway injection first** — server-injected traffic presents path length 1 without
  ever having been heard over RF, so a store node that is also a gateway would learn phantom
  neighbours.
- **Store set from mheard**, which already keys on `msg_source_last`
  (`src/lora_functions.cpp:729`) and is therefore a directly-heard table by construction. The table
  evicts the oldest under pressure, so show the **real last-heard age** per mailbox entry rather
  than implying a clean 12 h window.
- **Mailbox dedup needs the payload check too** (T12), over a 24 h window rather than 1 h.
- **§3.5 caps are mandatory**: one delivery per (source, NNN) per 10 min, max 3 per entry, one
  mailbox action per 30 s, **20 per hour per node**, nothing while QRS/QRT is latched, jitter
  5-60 s with peer cancellation.
- **Owner-only auth on the mailbox page**, and never show payload text — `heard` mode exposes
  third-party DM metadata to an operator who is not a party to it.
- Counters, with `dropped by delivery cap` and `dropped by slot pressure` as **two** counters.
- Manual purge and manual force-deliver actions, or the operator's only recourse is a reboot.

---

## Test concept

### Bench topology

Three nodes on USB. Roles are constrained by hardware: the store node must be ESP32-S3 or nRF52840.

| Role        | Node                 | Notes                                                       |
| ----------- | -------------------- | ----------------------------------------------------------- |
| Sender      | Heltec V3 (DK5EN-93) | ESP32-S3, WiFi net console on 2323                          |
| Store node  | RAK4631 (DK5EN-90)   | nRF52840, Ethernet — reach the GUI by IP, no mDNS           |
| Destination | T-Beam (DK5EN-92)    | classic ESP32; also proves a non-eligible board as receiver |

Drive every node over the **net console on TCP 2323** (`tools/hmac_connect.py`), not the serial
port: opening the serial port reboots some boards, and the console is single-client. Use
`tools/bench/serial_session.py` only where a serial session is genuinely needed, with `--dtr auto`
for the RAK.

**Traffic discipline:** group 9/9999 or a direct contact only, never `*`. Never a foreign callsign
as a frame source. Mesh and gateway off on the bench nodes unless a test needs them.

### Simulating an offline node

`--airgap on` (0.5). The node stops transmitting and drops received frames at the radio boundary
before dedup, so it is out of range in every respect that matters while its console stays alive.
`--airgap off` is the node coming back: its next beacon is what the store node hears, and that is
the real presence trigger, not a synthetic event.

### Stage 0 tests

| ID    | Test                                                                           | Expect                                                                              |
| ----- | ------------------------------------------------------------------------------ | ----------------------------------------------------------------------------------- |
| T-0.1 | Send 20 DMs, count settings writes                                             | Zero from the ACK path                                                              |
| T-0.2 | Replay a one-hop DM to the destination twice                                   | Exactly one extra `:ackNNN`, no second display; a replay within 60 s gets neither   |
| T-0.3 | DM to an unreachable callsign; separately, a broadcast and an ACK that give up | One `0x03` for the DM, none for the other two                                       |
| T-0.4 | `--airgap on`, one minute of nearby traffic, `--airgap off`                    | No TX while on; mheard and dedup state unchanged; normal behaviour after, no reboot |
| T-0.5 | **M0-1**: a bench node on the live network for one hour, mesh on               | Ring enqueues per minute recorded; parked-slot overwrite counter read               |

### Stage 1 tests

| ID    | Test                                                              | Expect                                                                           |
| ----- | ----------------------------------------------------------------- | -------------------------------------------------------------------------------- |
| T-1.1 | Two-hop DM, destination's ACK suppressed                          | All 9 attempts reach the destination (fresh msg_ids), each re-acked, one display |
| T-1.2 | Same, ACK allowed on attempt 2                                    | **Every** remaining attempt sharing the NNN stops (T1 regression)                |
| T-1.3 | Sender beaconing hard so the original rotates out of `own_msg_id` | ACK still attributed, ladder still stops (T2 regression)                         |
| T-1.4 | Echo-gated attempt 1: once with a relay in range, once without    | With a relay: no same-id retry. Without: exactly one                             |
| T-1.5 | QRT latched during a ladder                                       | No attempt fires while latched; ladder resumes or expires cleanly                |

### Stage 3 tests

| ID    | Test                                                                              | Expect                                                                                            |
| ----- | --------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------- |
| T-3.1 | Destination `--airgap on`, send DM, wait, `--airgap off`                          | One delivery at `max_hop` 0 within a beacon interval; ACK reaches sender                          |
| T-3.2 | Watch the relay during T-3.1                                                      | Relay logs `hop0` skip; the delivery is never forwarded                                           |
| T-3.3 | Destination never acks                                                            | 9 sends, one-hour cooldown, then 9 more; `dropped by cap` after `storetime`                       |
| T-3.4 | Two store nodes that hear each other; then repeat with them mutually airgapped    | First: one delivery. Second: two deliveries, per-entry cap of 3 still holds (T14 hidden terminal) |
| T-3.5 | Store node is also a gateway, fed a server-injected frame for an unknown callsign | No phantom neighbour learned (T8); no stale replay to the server                                  |
| T-3.6 | Store node reboot with pending entries                                            | Mailbox empty, drop logged, the RAM warning was shown when enabling                               |
| T-3.7 | Destination on upstream 4.35t                                                     | DM displayed, plain `:ackNNN` matched by the sender                                               |
| T-3.8 | String-scan the release image                                                     | No `0x41` emission path for a stored DM; `--airgap` compiled out                                  |
| T-3.9 | 25 DMs to an absent destination                                                   | §3.5 caps hold, no QRT, ring occupancy flat                                                       |

### Gate after every stage

Native suite, sequential clean board builds, the stage's bench tests, then a fable advisor pass on
the diff against that stage's acceptance criteria **and** prior stages' non-regression criteria.
Next stage starts only on APPROVED, after the current stage is committed. A subagent's self-report
is not verification: read the diff, re-run the gate.

## Risks carried into implementation

- **T11, the only unbounded one.** Nine fresh msg_ids per DM instead of one raises the distinct-id
  arrival rate at every relay and gateway on the path. The dedup ring is count-based with no
  timestamp (`src/dedup_functions.cpp:24-52`), so the measured 39.6-48.5 min safe corridor is a
  function of arrival rate. If a busy node's rotation window drops below it, this degrades dedup for
  HEY and position traffic on nodes that are not party to the DM. Measure in stage 0, re-measure
  after stage 1.
- Store-node airtime for an entry that is heard but never acked: accepted (operator, 2026-09-13),
  bounded by the 20-per-hour ceiling rather than the per-entry cap.
- mcmap and the server must fold on (source, NNN) or DM counts inflate.
- `{` in user text breaks NNN parsing today (advisor m4) and gets worse once NNN is load-bearing.
  Strip or escape at the sender in stage 0.
- Advisor M3, m1, m2 — no per-attempt outcome in the app, no return channel from `sendMessage()`,
  single-variable receipt mailbox — return in stage 2.
