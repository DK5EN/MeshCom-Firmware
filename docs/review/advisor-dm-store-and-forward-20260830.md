# Adversarial review: concept-dm-store-and-forward.md

Reviewer: independent design advisor. All file:line references personally read on
branch `tdeck-partial-refresh-trace` (2026-08-30).

## 1. Verdict in one paragraph

The core mechanism (sender outbox, L2 fresh-msg_id retries, receipt v2 as a text
suffix) is compatible with the installed base at the firmware level — better than
the doc dares to claim (the old `:ackNNN` parser is suffix-tolerant, and phone
apps never see raw text acks at all). But the concept has one true blocker (the
prescribed `FLASH_STRUCT_VERSION` bump wipes the settings of every updating node
on both platforms), one self-defeating mechanism (the 5-minute probe window
contradicts the doc's own latency data), and a receiver-side story that ignores
that the entire DM-ack path already runs in the nRF52 timer task and writes
LittleFS from there — a documented boot-loop class.

## 2. BLOCKER

### B1. The §7.1 persistence recipe wipes every updating node's configuration

What breaks: §7.1 prescribes "bump `FLASH_STRUCT_VERSION`". That bump is, by
design, a settings wipe:

- ESP32: `src/esp32/esp32_main.cpp:742` — `if(!flashLayoutCompatible(node_fversion) || bClear)` → `clear_flash()` → `preferences.clear()`
  (`src/esp32/esp32_flash.cpp:320-332`) — callsign, WiFi, everything in NVS.
- nRF52: `src/nrf52/nrf52_main.cpp:520` → `flash_reset()` → `InternalFS.format()`
  (`src/nrf52/nrf52_flash.cpp:410-428`).
- nRF52 wipes even **before** that check: adding any field to
  `s_meshcom_settings` changes `sizeof`, and `init_flash()` hard-resets on size
  mismatch (`src/nrf52/nrf52_flash.cpp:333-346`, the N-12 guard:
  `stored_size != sizeof(s_meshcom_settings)` → `flash_reset()`). The
  `// nicht im Flash` line is convention only — `save_settings()` writes the
  whole struct (`nrf52_flash.cpp:389`), so a field added _anywhere_ changes the
  blob size.

Why it matters: this is a feature release aimed at the whole fleet. Following
the recipe as written costs every ESP32 and RAK4631 user their callsign, WiFi
credentials and position on first boot after update. The warning block the doc
cites (`src/configuration_global.h:55-102`) says exactly this; the doc quotes it
but does not draw the conclusion.

Resolution:

- ESP32: **no bump needed at all.** NVS is key-value; add the two keys with
  defaults in load (`preferences.getInt("node_msgstore", 1)` pattern, see
  `esp32_flash.cpp:280,300-316`) and puts in save. Absent keys read defaults.
- nRF52: append the new fields at the **end** of the struct (after the current
  last field, not "above the `// nicht im Flash` line" — an insertion shifts
  the tail and breaks prefix compatibility), and add a size-tolerant read: if
  `stored_size` equals the known previous `sizeof`, read that many bytes and
  default the tail, then `save_settings()`. The migration precedent exists
  (`nrf52_flash.cpp:113-313`, the `MESHCOM_COMPAT_MARKER` merge path).
- Then `FLASH_STRUCT_VERSION` stays untouched (its bump is the wipe trigger,
  `configuration_global.h:66-76`).

## 3. MAJOR

### M1. Probe gating defeats itself in exactly the regime it targets

What breaks: §3.3 sends the stored message only "if a `{pong}` returns within
the probe window (5 min)"; a later pong is discarded, and "after 2 unanswered
probes the scheduler falls back to unprobed resends". The doc's own §2/§6 data
says per-hop TX-queue latency is median 30 s, p99 286 s **per hop per
direction** — a 3-4-hop probe round trip has a median around the window edge and
routinely exceeds it under load. Consequences at a DG0OPK-class site:

- Reachable multi-hop destinations look unreachable; after two wasted probe
  floods the ladder falls back to unprobed resends anyway — the probe layer
  degrades to pure added airtime.
- Worse: the probe is a DM text frame, so `getMessagePriority()` classifies it
  MSG_PRIO_CRITICAL (`src/txring_functions.cpp:134`), and under a full ring
  CRITICAL evicts lower-priority relay/beacon slots
  (`txring_functions.cpp:361-370`). The congestion-avoidance mechanism preempts
  real traffic at top priority.

Resolution: (a) treat **any** pong — and any receipt — arriving at any later
time as the reachability signal (freshness bound = the ladder step, not 5 min);
(b) size the probe window to the next ladder interval; (c) classify `{prb}`
below DM priority (needs a payload check in `getMessagePriority()`, like the
existing `{CET}`-class checks in `sendMessage()`, `src/loop_functions.cpp:3760`).

### M2. Legacy acks for older attempts never match a latest-only outbox

What breaks: §3.5 keeps "only the latest (msg_id, NNN) per slot" and falls back
to the content hash — but the hash exists only in v2 receipts. An old-firmware
destination sends plain `:ackNNN` echoing the `{NNN}` of whatever attempt
reached it. With attempt spacing +10/+30/+60 min and legitimate round trips of
4-40 min (§6), the ack that references the **previous** attempt is the common
case, not a border case. The sender reconstructs
`msg_id = ((_GW_ID & 0x3FFFFF) << 10) | (NNN & 0x3FF)`
(`src/lora_functions.cpp:913-914`) — matching a stale NNN finds nothing in the
outbox → the message keeps retrying against a destination that already has it
(old destinations also re-display every retry, compat matrix row 2, making the
annoyance worse).

Resolution: keep the full set of minted NNNs per slot (27 × 2 B = 54 B/slot,
or a compressed range since NNNs are consecutive-ish per slot). RAM cost is
trivial against the 232 B entry.

### M3. The phone app never learns the outcome; T-Deck shows duplicate sends

What breaks:

- The BLE ack notification is synthesized from the ack's NNN
  (`src/lora_functions.cpp:916-944`, `addBLEOutBuffer(print_buff, 7)`;
  UDP twin at `src/udp_functions.cpp:381-399`) — i.e. it carries the msg_id of
  **attempt N**. The app only ever saw the original send (echoed once,
  `src/loop_functions.cpp:3708-3710`, guarded by the one-shot `hasMsgFromPhone`
  flag, cleared at `src/esp32/esp32_main.cpp:2955` / `nrf52_main.cpp:1606`).
  L2 retries are not echoed (flag is false by then), so the ack for a retry
  references a msg_id the app has never seen: the user's app shows the DM
  unconfirmed forever while the node's outbox says "delivered". Same for the
  WebGUI messages page, which renders the checkmark from
  `checkOwnTx(original msg_id)` (`src/web_functions/web_functions.cpp:1586-1600`).
- On T-Deck, `tdeck_add_MSG(aprsmsg, false)` is called unconditionally in
  `sendMessage()` (`src/loop_functions.cpp:3733-3735`) — every L2 retry would
  add a duplicate row to the local message view.

Resolution: when a receipt matches an outbox entry, emit the BLE ack
notification (and own_msg_id status update) for the **original** msg_id the app
knows; suppress `tdeck_add_MSG`/BLE echo for outbox-originated resends (needs a
flag through the send path — see m1).

### M4. `{prb}` probes display as junk DMs on old-firmware destinations

What breaks: on an old destination, a DM payload `{prb}` matches neither
`{ping}`/`{pong}` (`src/lora_functions.cpp:869,886`) nor `:ack`
(`:905`), and `iEnqPos = indexOf("{", 1)` is -1 (the `{` is at position 0) — so
it falls into the display branch: `queueDisplayText()` +
`addBLEOutBuffer(RcvBuffer, size)` (`src/lora_functions.cpp:976-987`). Up to 27
probes/day per stored message appear as "{prb}" DMs on the recipient's display
and phone app. The §5.3 compat matrix ("probes unanswered → ladder falls back")
omits this entirely — it is the loudest user-visible artifact of the whole
design for the installed base.

Resolution: never probe a destination not known to be v2-capable (e.g. probe
only after a v2 receipt has ever been seen from that call; first delivery
attempt sequence runs unprobed). Alternatively make the probe payload read as
harmless human text — ugly. The current "(b) is proposed: zero behavior change
for old nodes" claim is false as written.

### M5. Receiver-side design ignores that the DM-ack path runs in the nRF52 timer task — and writes flash there today

What breaks: §3.1 carefully keeps the **outbox** loop-task-only, but phases 1
and 3 change the **receiver** side, which on nRF52 runs in the FreeRTOS
timer-service task: `OnRxDone` is the radio callback
(`src/lora_functions.cpp:377-379` comment; `src/capture_functions.cpp:44`), and
a DM addressed to me triggers `SendAckMessage()` from there
(`src/lora_functions.cpp:960`), which does `node_msgid++` **and**
`save_settings()` (`src/loop_functions.cpp:4463-4468`) — a LittleFS write from
the timer task. That is the documented, reproducible boot-loop class:
`docs/BACKLOG.md` 2026-08-21, "LittleFS-Schreibzugriffe aus dem
Timer-Service-Task crashen das Board reproduzierbar in einen Boot-Loop". This
is a pre-existing latent hazard, but the concept multiplies traffic through it
(content-dedup "re-ACK, don't re-show" fires one more `SendAckMessage` per
retry received) and its concurrency section claims safety it doesn't provide
for this half.

Resolution: as part of phase 1, defer ack generation to the loop task (small
mailbox, same pattern the outbox already uses for receipts), and mint ack
msg_ids without `node_msgid++`/`save_settings()` — the gateway ack path already
uses `msg_counter = millis()` as a msg_id precedent
(`src/lora_functions.cpp:1127`). Side benefit: kills one flash write per
received DM (see M6).

### M6. Content dedup must hook the UDP path too; re-ACK cost lands on the destination's flash

What breaks:

- A gateway destination receives DMs via UDP as well: display + BLE forward +
  `SendAckMessage()` at `src/udp_functions.cpp:406-437`. A content-dedup cache
  living only in the LoRa RX path re-displays retries that arrive via the
  server. (Note in passing: that UDP branch runs **before** the `bUdpMsgIsNew`
  dedup check — only the relay decision is gated, `udp_functions.cpp:442` — so
  server-side duplicates already re-display today.)
- Every re-ACK is a full `SendAckMessage()` → `node_msgid++` +
  `save_settings()` (`src/loop_functions.cpp:4463-4468`): the doc's flash-wear
  section (10.2) counts only sender-side writes, but 27 retries/day cost the
  **destination** 27 blob writes/day on nRF52 (whole-struct rewrite,
  `src/nrf52/nrf52_flash.cpp:380-397`). Fix together with M5.

### M7. The tri-state promise ("exactly delivered / failed / open") cannot survive v1 RAM-only persistence

What breaks: §1 promises the sender ends in exactly one user-visible state,
with FAILED notification. Any reboot — watchdog, nightly power cycle, battery
swap, the `esp_deep_sleep_start()` paths (`src/command_functions.cpp:1008`) —
silently discards OPEN entries: no delivery, no FAILED notice, no history. §8
documents the reboot loss as a limitation but §1 and §4 still sell the
guarantee. For the EMCOMM audience this is the difference between "the radio
told me it failed" and "the radio forgot".

Resolution: either soften §1 explicitly ("states are guaranteed only across
continuous uptime") and add a boot-time "outbox was lost" notice when the
feature is enabled (the node cannot know _what_ was lost, only _that_ a reboot
happened with msgstore on), or move minimal persistence (dest + hash + state,
~40 B/slot) into the existing settings blob — but only after B1's migration
work, and mindful of M5's write-context rule.

## 4. MINOR

### m1. `sendMessage()` has no return channel for the outbox

`sendMessage()` returns void and can silently do nothing: backpressure refuse
(`src/loop_functions.cpp:3474-3483`), length reject (`:3621-3625`), DM-to-self
reject (`:3653-3657`), ring drop (`w < 0`, `:3770-3782`). The outbox needs the
minted msg_id/NNN and an enqueued/refused status. Reading
`meshcom_settings.node_msgid` before the call is fragile. Extract a variant
returning `{msg_id, status}`; keep the wrapper for existing callers.

### m2. Single-variable receipt mailbox can lose acks

The loop task blocks for long stretches: `save_settings()` sleeps `delay(100)`
plus file IO (`src/nrf52/nrf52_flash.cpp:369,383`), the webserver streams whole
pages synchronously (`src/web_functions/web_functions.cpp:190-221`), the N-20
W5100S stalls run ≥20 s (`docs/BACKLOG.md` 2026-08-21). Two receipts inside one
such window (typical when an offline destination comes back and re-ACKs a
burst) overwrite a single mailbox variable. Use a 4-deep ring; cost ~24 B.

### m3. "mbedtls SHA-256 already linked on both platforms" is false for nRF52

mbedtls is used only in the ESP32-only net console
(`src/net_console.h:26` gates on `defined(ESP32)`; `src/net_console.cpp:32`
`#include <mbedtls/md.h>`). The Adafruit nRF52 core ships Adafruit_nRFCrypto
(CC310 hardware hash) instead — different API, and hardware-crypto-from-
timer-task is an open question. Since the hash is wire-visible and must match
across platforms, ship one small software SHA-256 (or CRC32-of-SHA-input if
weight matters) compiled on both; do not depend on platform crypto stacks.

### m4. The hash's "{NNN} excluded" rule must equal the receiver's actual strip rule — and `{` in user text is a live pre-existing bug

The receiver strips at the **first** `{` from index 1
(`src/lora_functions.cpp:906,962`) and parses the ack id from there (`:952`).
User text containing `{` therefore already truncates the displayed message and
acks a garbage NNN today — the sender's L1 never matches, and the outbox will
see it as "no receipt" and retry 27 times. Define hash input as the
first-brace-stripped payload on both ends, and either escape `{` at enqueue or
document the character as unsupported in stored DMs.

### m5. NNN ambiguity across a 24 h holding window

`node_msgid` is one shared 0..999 counter consumed by every frame type
(positions `src/loop_functions.cpp:4323`, hey `:4552`, acks `:4463`, pings
`:3228`). Over 24 h a busy node wraps it; two outbox slots can then hold the
same "latest NNN", and a plain legacy `:ackNNN` is ambiguous (v2 receipts are
saved by the hash). Field precedent for fast wrap: 1000 msg_ids in ~50 s during
the DL6MDF-11 incident (`src/beacon_rate.h:22-30`). Low probability, but the
matcher should refuse an ambiguous legacy match rather than pick a slot.

### m6. Phase-1 give-up reporting must be scoped, and ack retransmits never stop in sparse topologies

Nothing acks an ack: the retransmit slot is only freed when the destination
hears its own ack relayed (`src/lora_functions.cpp:508-543`). In a two-node
link the ack always burns all 3 transmissions and ends in RETRANSMIT_GIVEUP.
Also, retransmit-eligible status applies to broadcast texts too
(`src/loop_functions.cpp:3758-3763`), which legitimately give up when nothing
acks them. If phase 1 reports RETRANSMIT_GIVEUP to the user unscoped, every
destination in a sparse net gets bogus failure notices about its own acks, and
every broadcast sender about its broadcasts. Report give-up only for
user-originated DM frames (outbox-tracked / `bDM` path).

### m7. `{prb}` as tracking/reflection primitive

New-firmware destinations answer probes silently (by design — no display). That
turns reachability of any callsign into a mesh-wide, invisible query; today's
equivalents are louder ({ping} is 1-hop, `src/lora_functions.cpp:996-1006`; a
DM-with-{NNN} probe triggers an ack but shows on the victim's display).
Source calls are unauthenticated, so spoofed-source `{prb}` reflects
`{prb-ack}` floods at a third party (amplification ≈1:1, bounded by the
per-source rate limit — which a spoofer sidesteps by rotating sources). Accept
and document, add the planned 30 s floor plus a global prb-ack budget, and
consider answering only sources present in mheard.

### m8. Fresh msg_id per attempt inflates server/mcmap statistics

Every L2 attempt on a gateway uploads to the server as a brand-new message
(`src/loop_functions.cpp:3790-3794`); server-side dedup logic is not in this
repo and cannot be verified — assume none at content level. Each attempt also
fans out to every gateway on the server side. Analytics (mcmap message counts,
delivery-rate studies like DG0OPK) must learn to fold retries by content hash
or the feature skews the very numbers used to justify it. Release-notes item.

### m9. Probes must not consume `node_msgid`/`save_settings()` — resolution exists, prescribe it

`sendPing()`/`SendPong()` both bump and save today
(`src/loop_functions.cpp:3228-3233, 3305-3310`). A meshed probe needs a fresh
msg_id each time (same-id probes die in mesh dedup rings for ~40 min,
`src/dedup_functions.cpp:23-51`), but it can mint from `millis()` like the
gateway ack path (`src/lora_functions.cpp:1127,1161`) + `insertOwnTx()` — zero
flash writes. The doc flags the problem ("if avoidable") without the answer;
make it normative.

### m10. WebGUI outbox page: synchronous server, keep it tiny

The webserver runs entirely in the loop task and streams whole pages before
returning (`src/web_functions/web_functions.cpp:190-221`). A 5-row outbox page
is fine; just don't render payload history unbounded, and remember every ms
spent there delays ESP32 RX processing (RX runs in loop on ESP32).

## 5. Answers to the §10.3 advisor questions

**Q1 (fresh msg_ids break dedup assumptions?)** — No breakage in-firmware,
verified: a resent frame with a new msg_id passes `is_new_packet()`
(`src/dedup_functions.cpp:23`) and relays normally; the sender pre-registers
each attempt via `insertOwnTx()`/`addLoraRxBuffer()`
(`src/loop_functions.cpp:3743-3748`), so returning copies are recognized
(`checkOwnTx`, `src/lora_functions.cpp:560,786`) and not re-processed.
`own_msg_id` churns faster (20-deep, `src/loop_functions.cpp:399`) but the
outbox replaces it for stored traffic. Real exposure: server-side dedup is
unverifiable from this repo, and analytics count every attempt (m8).

**Q2 (receipt v2 parsing both directions + apps)** — Better than feared,
verified: the old sender parser is suffix-tolerant —
`substring(iAckPos+4).toInt()` (`src/lora_functions.cpp:913`,
`src/udp_functions.cpp:376`) is `atol()`, which stops at `;` → `:ack123;H=...`
parses as 123 on every fielded build. Phone apps never receive the text ack at
all: both RX paths convert it to a 7-byte binary 0x41 BLE notification
(`src/lora_functions.cpp:916-944`, `src/udp_functions.cpp:381-399`), so the
suffix cannot reach an app parser through a node. The capability-marker
mitigation is therefore probably unnecessary; if wanted anyway, it can ride
inside the `{NNN...}` brace — old destinations strip display at the first `{`
(`:962`) and `toInt()` ignores the suffix (`:952`). Residual risk sits outside
this repo: the server and web/phone clients that consume the ack text via UDP.
But: the app never learns L2 delivery outcomes at all (M3) — that is the real
Q2-adjacent gap.

**Q3 (probe abuse; old-destination fallback)** — The fallback ladder logic is
coherent but indistinguishable from "pong slower than 5 min", which the doc's
own latency data makes common (M1) — so the old-vs-slow cases collapse into
the same (wasteful) behavior. Old destinations additionally _display_ every
probe (M4). Abuse: silent presence tracking and spoofed-source reflection
(m7) — real but incremental over existing primitives.

**Q4 (RAK4631 memory / blob migration)** — RAM is a non-issue (~2 kB vs the
S3/nRF52 buffer class, `src/configuration_global.h:203-210`). The blob is the
issue and it is worse than the question implies: any `sizeof` change hard-resets
settings via the N-12 size guard before version logic even runs
(`src/nrf52/nrf52_flash.cpp:333-346`), and the prescribed version bump wipes
both platforms deliberately (B1). The legacy migration path exists only for the
one ancient `MESHCOM_COMPAT_MARKER` layout (`:113-313`); a new size-tolerant
migration must be written, with fields appended at the struct end.

**Q5 (scheduler vs backpressure/CSMA/eviction under congestion)** — The
planned guards map to real hooks: `bp_state.refusing()`
(`src/backpressure.h:136`) and the BP refuse path in `sendMessage()`
(`src/loop_functions.cpp:3474-3483`) already protect the ring. Two gaps:
probes/retries classify MSG_PRIO_CRITICAL and **evict** lower-priority slots
when the ring is full (`src/txring_functions.cpp:134,361-370`) — a congested
node's retry can push out a relay (M1c); and a BP-refused attempt is invisible
to the outbox (m1), so "skip the cycle" must be checked _before_ calling
`sendMessage()`, which the design does say — keep it that way.

**Q6 (24 h window vs millis rollover / deep sleep / nightly reboots)** —
Rollover is solvable and the codebase has the tested idiom
(`beaconShotAllowed`, `src/beacon_rate.h:60-64`); mandate it for
`next_action_at`/`created_millis`. The real issue is not the 49.7-day wrap but
the reboot class: RAM-only v1 silently voids the §1 guarantee on every watchdog
or nightly power cycle, and deep sleep (`src/command_functions.cpp:1008`) is a
reboot (M7). A node that reboots nightly has an effective holding time of
hours, not the configured 24, with no FAILED notice ever.

**Q7 (phase 1 alone: 3× identical `:ackNNN` at old senders)** — Safe,
verified: retransmits are byte-identical (same msg_id,
`src/lora_functions.cpp:1871`), and the entire text-processing branch at an old
sender is gated by `is_new_packet()` (`src/lora_functions.cpp:786`), so copies
2 and 3 are swallowed by the dedup ring (40 s spacing ≪ 28-52 min turnover).
Mesh-wide, relays are suppressed the same way, so extra copies propagate only
where the first was lost — exactly the intent. Two caveats: the ack retransmit
has no stop condition in sparse topologies (always 3×, bounded cost), and
give-up reporting must be scoped to DMs or it produces bogus failure notices
(m6). Also note phase 1 does not touch the SendAckMessage flash write / timer
task hazard it builds on (M5) — fixing that belongs in the same PR.

## 6. What I verified and consider sound

- **L2-fresh-msg_id is the right call**: same-id resends after ~120 s are
  provably swallowed mesh-wide (`is_new_packet` gates display _and_ relay,
  `src/lora_functions.cpp:786`; ring 100/70 deep, displacement only,
  `src/dedup_functions.cpp:18-52`, `src/configuration_global.h:208,230`).
- §2's description of today's machinery is accurate on every point I checked:
  msg_id composition and `{NNN}` marker (`src/loop_functions.cpp:3675-3695`;
  NNN is `%03i`, wraps at 999 — consistent with the `& 0x3FF` mask), L1 40 s ×3
  (`src/lora_functions.cpp:1786-1884`), silent give-up (`:1820-1834`),
  fire-and-forget ack (`src/loop_functions.cpp:4503-4506`), binary 0x41 only
  from gateways for broadcast/group (`src/lora_functions.cpp:1124-1173`),
  sticky `msg_server` (`src/udp_functions.cpp:329`,
  `src/lora_functions.cpp:1264-1265`), `{ping}` 1-hop exclusions
  (`src/lora_functions.cpp:996-1006,1230`).
- `{prb}` riding the normal text relay path needs no relay-side change:
  intermediates relay any 0x3A not addressed to them; only the marker-specific
  exclusions are payload-matched. Verified against the relay branch
  (`src/lora_functions.cpp:1258-1346`).
- checkVia/escaping concern (10.1): unfounded as long as the raw pre-escape
  text is stored — `sendMessage()` builds a fresh `aprsMessage` per call and
  `checkVia()` writes only `msg_destination_path`
  (`src/via_functions.cpp:94-148`, `src/loop_functions.cpp:3697`). But hash
  input must be the post-escape wire payload (m4).
- Hardware gating in the RAM-class ladder works: the `ENABLE_XML` branch is
  native-test-only (`platformio.ini:333-344`), so keying `ENABLE_MSGSTORE` on
  `CONFIG_IDF_TARGET_ESP32S3 || BOARD_RAK4630` (`configuration_global.h:203`)
  gates exactly the intended boards.
- The "HF only" badge semantics are honestly conservative as claimed — the
  sticky bit can only under-report (verified set-sites above); the
  gateway-origin caveat is real (`src/loop_functions.cpp:3745-3748` sets the
  dedup server flag but the frame goes out with `msg_server=false`).
- Serial/WebGUI plumbing models exist as cited: `--persist*` block
  (`src/command_functions.cpp:4906-4952`), messages page pair
  (`src/web_functions/web_functions.cpp:1254,1544`).
- Backpressure/rate-limit hooks are real and rollover-safe
  (`src/backpressure.h:136`, `src/beacon_rate.h:60-64`).

## 7. Suggested doc edits (summary)

1. Rewrite §7.1 plumbing: no `FLASH_STRUCT_VERSION` bump; ESP32 NVS keys with
   defaults; nRF52 append-at-end + size-tolerant migration (B1).
2. Rework §3.3: late pongs count, window ≥ ladder step, probe priority below
   DM, probe only v2-known destinations (M1, M4).
3. §3.5: keep all minted NNNs per slot (M2).
4. Add "sender-side UI reconciliation": original-msg_id BLE ack synthesis,
   suppress tdeck/BLE echo on resends (M3); `sendMessage()` API change (m1).
5. Fold M5/M6 into phase 1: ack generation deferred to loop, millis-minted ack
   msg_ids, content-dedup hooks in both RX paths.
6. Soften §1's tri-state guarantee to continuous-uptime scope (M7).
7. Replace the mbedtls claim with a platform-neutral SHA-256 plan (m3); define
   the hash strip rule as first-`{` (m4); scope give-up notices to DMs (m6).
