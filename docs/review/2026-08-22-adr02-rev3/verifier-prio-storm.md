# Verifier: Prio/Storm claims for ADR nc-importance-backoff (Rev. 3 input)

Verified against /Users/martinwerner/WebDev/MeshCom-Firmware-DEV-Main, branch v4.35p_prio,
2026-08-22. All line numbers checked against the working tree.

---

## CLAIM 1 — Importance slots miss the HEY/POS relay classes

**VERDICT: CONFIRMED.**

### Relay enqueue path

`OnRxDone()` relay block, `src/lora_functions.cpp:1283`:

```c
// no retransmission for ANY relay message; ...
addTxRingEntry(RcvBuffer, size, RING_STATUS_DONE, "rx_relay", 0, true);
```

This one call is the relay path for ALL payload types (the surrounding block at
lora_functions.cpp:1206-1293 gates only on hops/mesh/loop checks, not type). No
priority is passed; `addTxRingEntry()` computes it via `getMessagePriority(w)`
(`src/txring_functions.cpp:280`), which reads the payload type byte at
`ringBuffer[slot][2]`.

### getMessagePriority() type dispatch (src/txring_functions.cpp:46-73, decisive head)

```c
uint8_t msg_type = ringBuffer[slot][2];

if(msg_type == MSG_TYPE_ACK)        // 0x41
    return MSG_PRIO_CRITICAL;
if(msg_type == MSG_TYPE_POSITION)   // 0x21 '!'
    return MSG_PRIO_LOW;
if(msg_type == MSG_TYPE_HEY)        // 0x40 '@'
    return MSG_PRIO_BACKGROUND;
if(msg_type == MSG_TYPE_TEXT)       // 0x3A ':'
{
    // Relay detection: OnRxDone sets RING_STATUS_DONE for relayed packets
    if(ringBuffer[slot][1] == RING_STATUS_DONE)
        return MSG_PRIO_NORMAL; // Relay
    ...  // user-originated text: CRITICAL (DM) / HIGH (group, "*", parse-fail)
}
// Unknown type: normal priority
return MSG_PRIO_NORMAL;
```

The POSITION and HEY checks come BEFORE the TEXT/RING_STATUS_DONE relay branch, so a
relayed HEY never reaches the "Relay" (NORMAL) classification:

- Relayed HEY '@' → **MSG_PRIO_BACKGROUND (5)**, band base 5500 ms (`CSMA_PRIO_BASE_5`).
- Relayed position/WX '!' → **MSG_PRIO_LOW (4)**, base 5500 ms (`CSMA_PRIO_BASE_4`).
  (WX frames are sent with payload type '!' — `initAPRS(aprsmsg, '!')` at
  loop_functions.cpp:4028; there is no separate WX type on air.)
- Relayed text ':' with RING_STATUS_DONE → **MSG_PRIO_NORMAL (3)** — the only relay
  class in the Prio-3 band, plus any unknown payload type (fallback NORMAL).
- Forwarded ACKs (lora_functions.cpp:299, 1092, 1116) → CRITICAL.

### Is the ADR table wrong or imprecise?

Imprecise in a way that matters. `configuration_global.h:261` itself says
`#define MSG_PRIO_NORMAL 3 // Mesh-Relay (weitergeleitete Pakete)` and the ADR copies
that ("Prio 3 | Relay", table in Kap. Kontext; Kap. 4.2 "Innerhalb des Relay-Bandes
stehen 10 Slots"). In code, Prio 3 = **text relays and unknown types only**. The ADR's
own motivating example (Kap. "Ein HEY-Beacon durchs Netz", 7 relay copies of msg
9867002B) runs entirely in the Prio-5 band, untouched by Stufe-1 importance slots.

### Traffic share (production, 2026-08-22, mcmap activity_series + evidence pack)

Server-feed frames/h (24 h window): hey 3400-4400 (spike hour 9366), pos ~3700-3950,
msg (text) **3-41**, tlm 5-40. Evidence pack: 96,074 unique HEY frames/24 h. Text is
well under 1 % of hey+pos volume. Caveat: feed traffic is not channel traffic (63.8 %
of HEY feed lines are gateway self-uploads), but the on-air relay mix is dominated by
the same HEY/POS classes.

**Corrected wording for Rev. 3:** "Die Importance-Slots der Stufe 1 greifen nur im
Prio-3-Band (MSG_PRIO_NORMAL). In das Prio-3-Band fallen aber nur relayte
TEXT-Nachrichten (Payload ':', RING_STATUS_DONE) und unbekannte Payload-Typen —
getMessagePriority() stuft relayte HEYs ('@') vor der Relay-Erkennung als
MSG_PRIO_BACKGROUND und relayte Positionen/WX ('!') als MSG_PRIO_LOW ein
(src/txring_functions.cpp:46-58, Enqueue in lora_functions.cpp:1283 typunabhaengig).
Text ist im Produktionsnetz < 1 % des HEY+POS-Aufkommens (msg 3-41/h vs. hey
3400-4400/h, pos ~3800/h). Die HEY-Flut-Klasse liegt damit vollstaendig ausserhalb
des Stufe-1-Mechanismus; 'Prio 3 = Relay' in der Bestandstabelle ist als
'Text-Relay' zu lesen."

---

## CLAIM 2 — Anchor reset on every RX end starves TX and prevents attempt escalation

**VERDICT: CONFIRMED (mechanism as coded); the numeric P≈0.06 % is feed-derived and
should be labeled an upper-bound illustration, not a per-node measurement.**

### Code anchors, all verified

1. **Every RX end resets the anchor.** `OnRxDone()` (lora_functions.cpp:320-1355) has
   exactly one early return — the ACK path — and both exits reset:
   - ACK path, lora_functions.cpp:452-453:
     `iReceiveTimeOutTime = millis(); csma_timeout = csma_compute_timeout(cad_attempt);`
   - normal end, lora_functions.cpp:1338-1339: identical pair.
     There is no filtering by relevance — duplicates, foreign frames, everything that
     completes RX resets the anchor. On ESP32, `checkRX()` calls `OnRxDone()`
     (esp32_main.cpp:3937) and the main loop resets AGAIN after checkRX returns
     (esp32_main.cpp:2291-2292). Timeout is recomputed at the UNCHANGED `cad_attempt`.

2. **CAD runs only after the timeout expires quietly.** Main loop
   (esp32_main.cpp:2134-2292): while `iReceiveTimeOutTime > 0`, TX is gated. On expiry
   (`millis() - iReceiveTimeOutTime >= csma_timeout`, :2137): if `receiveFlag` → timer
   re-armed (:2149); if preamble/header IRQ active → deferral re-arms (:2205-2206, up
   to 3x header / 1x preamble); else `iReceiveTimeOutTime = 0` (:2212) and radio
   restarts. Only then does the TX gate (`if(iReceiveTimeOutTime == 0 && ...)`,
   :2395) run — and it first polls IRQ flags (:2418-2431): preamble/header present →
   TX abort, timer re-armed at unchanged attempt. Otherwise `radio.scanChannel()`
   (CAD, :2449, double-check :2467).

3. **cad_attempt escalates only on CAD busy.** esp32_main.cpp:2531-2533:
   ```c
   // Channel busy confirmed — backoff
   cad_attempt++;
   csma_timeout = csma_compute_timeout(cad_attempt);
   ```
   Sole increment site in the tree. `csma_reset()` (attempt→0) runs on CAD free
   (:2485) and on TxDone/watchdog. No TX path bypasses this gate on the internal
   radio (EXTERNAL_RADIO bridge and the T_ETH_ELITE `cad_result = FREE` hack aside).

### Consequence

Follows from the code: a node needs `csma_timeout` of continuous absence of completed
RX events (plus no preamble at gate time) to reach its FIRST CAD. Since every RX
completion re-arms the full window at the same attempt, sustained traffic with
inter-frame gaps below the window keeps every co-hearing node at attempt 0 forever —
escalation (and rapid-fire) is unreachable exactly when the channel is busiest.

Two corrections for Rev. 3 wording:

- The required quiet gap is **priority-dependent**: `csma_compute_timeout()` uses the
  head-of-queue priority (lora_functions.cpp:2146-2150), so ACK/DM 3000-3350 ms,
  relay-text 4500-4850 ms, POS/HEY 5500-5850 ms. ">= 4.5 s" is the relay case, not
  universal.
- The 1.64 frames/s / P≈0.06 % figure derives from feed-wide rates. A single node
  hears only its neighborhood (NC median 5, p90 14 per the evidence pack), so its
  local heard-rate is lower and per-gap probability higher; the structural claim
  (anchor reset at unchanged attempt → starvation without escalation under sustained
  local traffic) is what the code guarantees, the exact probability is scenario math.

---

## CLAIM 3 — Rapid-fire returns fixed 100 ms with zero jitter → synchronized convoy

**VERDICT: CONFIRMED, with two stated nuances (natural millisecond-scale skew;
a preamble IRQ-poll before TX exists but cannot break the convoy).**

### Code

`configuration_global.h:224-225`:

```c
#define CSMA_MAX_ATTEMPTS   3       // Ab hier: Rapid-fire CAD bis Kanal frei
#define CSMA_RAPID_RX_MS    100     // Preamble-Check Fenster im Rapid-fire Modus (ms)
```

`csma_compute_timeout_prio()`, lora_functions.cpp:2153-2155:

```c
unsigned long csma_compute_timeout_prio(int attempt, uint8_t priority) {
    if(attempt >= CSMA_MAX_ATTEMPTS)
        return CSMA_RAPID_RX_MS; // rapid-fire with preamble check
```

No `random()` on this path — jitter (`random(0, slots+1) * CSMA_SLOT_SIZE`,
:2170) is only reached below CSMA_MAX_ATTEMPTS. Zero-jitter at rapid-fire: confirmed.

### OnPreambleDetect

lora_functions.cpp:2122-2128: doc comment says "currently not used!", body is a bare
`printfdeb`. On nRF52 the registration is commented out
(`src/nrf52/nrf52_main.cpp:978: //RadioEvents.PreAmpDetect = OnPreambleDetect;`).
On ESP32 it is never registered as a callback. Confirmed unused.

### What actually guards TX at rapid-fire (the nuances)

- The TX gate polls `radio.getIrqFlags()` for HEADER_VALID/PREAMBLE_DETECTED before
  CAD (esp32_main.cpp:2418-2431) — a real preamble check, but it only detects a
  station ALREADY transmitting. It cannot see peers whose rapid-fire timers expire in
  the same instant and who are themselves still in CAD; then 1-2 `scanChannel()` CADs
  (~28 ms each) with the same blindness, then TX. The convoy claim survives.
- "Anchored to the same RX-end event": each node's anchor is set at its own OnRxDone
  completion (main-loop poll latency + RX processing, ONRXDONE_TIME is instrumented
  precisely because it varies), so co-hearing nodes fire at 100 ms + a natural skew of
  roughly one to a few tens of ms — of the same order as one 28 ms CAD window. Not
  literally simultaneous, but far inside collision range; and once in the rapid-fire
  loop, a CAD-busy result re-arms 100 ms from near-identical instants again
  (esp32_main.cpp:2551).

**Rev. 3 wording:** state "100 ms fix ohne Jitter (lora_functions.cpp:2154-2155),
Preamble-Callback unbenutzt (OnPreambleDetect nur Debug-Print, Registrierung auf
nRF52 auskommentiert); der IRQ-Poll vor dem CAD (esp32_main.cpp:2418-2431) erkennt
nur bereits laufende Aussendungen, nicht gleichzeitig startende Peers — Restskew
zwischen Nodes wenige ms bis einige 10 ms, also innerhalb des 28-ms-CAD-Fensters."

---

## CLAIM 4 — Queue size and drop policy

**VERDICT: CONFIRMED, with one precision: MAX_RING is 20/30/10 by memory class, and
the evictee is the oldest entry of the WORST priority, evicted only if the new entry
is strictly better.**

- `src/configuration_global.h:169-201`: MAX_RING = **20** (ENABLE_XML/SBUFFER branch),
  **20** (ESP32-S3 + RAK4630/nRF52840), **10** (ENABLE_TBEAM developer branch),
  **30** (original ESP32 fallback branch). "20 (10 on T-Beam)" is right for the two
  branches the claim names but omits the 30-slot classic-ESP32 branch.
- `addTxRingEntry()` overflow (`src/txring_functions.cpp:288-356`): when the ring is
  full, it scans for the oldest entry of the numerically highest (= worst) priority
  (`ringPriority[scan] > worst_prio`, tie → first found from iRead = oldest;
  EXT_PENDING slots exempt). If `prio < worst_prio` (new strictly better) the victim
  is dropped (`stat_drop_count[worst_prio]++`, slot zeroed, N-24 relocation keeps the
  iRead entry). Otherwise the NEW packet is dropped (`droppedNew`,
  txring_functions.cpp:349-354: "New packet is same or lower priority than everything
  in queue — drop it"). So under a BACKGROUND-flood queue, incoming HEY relays drop
  each other (same prio → new one dropped), while any better-prio arrival evicts the
  oldest HEY — consistent with the finder's summary.

---

## Bottom line for Rev. 3

All four claims stand on the code. The two wording repairs that matter:
(1) Stufe 1's importance slots govern a band that carries <1 % of the relay volume —
the ADR must either say "Text-Relay-Band" honestly or extend the mechanism to
Prio 4/5 (where the HEY example it motivates itself with actually lives);
(2) the starvation/convoy analysis (anchor reset at unchanged attempt; jitterless
100 ms rapid-fire) is code-accurate, but per-gap probabilities should be presented as
feed-rate illustrations, with the priority-dependent window (3000/4500/5500 ms) named.
