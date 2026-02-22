# MeshCom Firmware Fix Guide: Ring Buffer Overflow Resolution

> **Target**: AI coding agent working on C++ firmware source
> **Firmware**: MeshCom 4.35k, ESP32 platform (SX1262 radio via RadioLib)
> **Source root**: `/Users/martinwerner/Downloads/MeshCom-Firmware-4.35k.02.19/`
> **Scope**: Fix ring buffer overflow — the dominant remaining cause of message loss after BUG #1–#5 patches
> **Prerequisite**: All patches from `FIRMWARE_FIX_GUIDE.md` have been applied
> **Date**: 2026-02-22

---

## 1. Problem Statement

After applying 5 firmware fixes (RECEIVE_TIMEOUT, early RX restart, OnHeaderDetect, CAD counter, retransmit timer), unidirectional message loss dropped from 32% to **0% with mesh off**. The firmware RX path is fixed.

However, the `[MC-DBG] RING_OVERFLOW` debug message reveals a new dominant failure:

| Run | RING_OVERFLOW events | Message loss |
|-----|---------------------|--------------|
| Mesh ON | **52** | 40–55% |
| Mesh OFF | **89** | 0% uni / 50–55% bidi |

The ring buffer (30 entries) overflows because:

1. **Mesh relay messages get retransmitted** — relay text messages are inserted with status `0x00` (retransmit enabled), consuming 2+ ring buffer slots per relayed message (original + retransmit copy)
2. **No retransmit cap** — if a retransmit copy is never acknowledged, it triggers another retransmit, creating a cascade
3. **Retransmit copies add entries** — each retransmit copies the message to a NEW slot at `iWrite`, consuming ring buffer capacity

With mesh ON, the node performed **290 TX_START / 179 RADIO_TX** events during one test run — most were relay traffic for other stations, all consuming ring buffer slots and triggering retransmissions.

---

## 2. Ring Buffer Architecture

### 2.1 Main TX Ring Buffer

**Declared**: `src/loop_functions.cpp` line 247

```cpp
unsigned char ringBuffer[MAX_RING][UDP_TX_BUF_SIZE+5] = {0};
int iWrite=0;    // line 248 — next write position
int iRead=0;     // line 249 — next read position (consumed by doTX)
```

- `MAX_RING = 30` (default build, `src/configuration_global.h` line 75)
- `UDP_TX_BUF_SIZE = 255` (line 45)
- Each entry: **260 bytes** (255 + 5)
- Total RAM: **7,800 bytes**

### 2.2 Entry Byte Layout

```
ringBuffer[i][0]     = msg_len (payload length)
ringBuffer[i][1]     = status byte (see Section 2.4)
ringBuffer[i][2]     = msg_type (0x3A=text, 0x21=position, 0x40=hey, 0x41=ACK)
ringBuffer[i][3..6]  = msg_id (4 bytes, little-endian)
ringBuffer[i][7..N]  = payload remainder (APRS-encoded message)
```

### 2.3 Circular Buffer Mechanics

**Producer** (writing): Fill `ringBuffer[iWrite]`, then call `addRingPointer(iWrite, iRead, MAX_RING)` which increments `iWrite` and wraps at `MAX_RING`.

**Consumer** (reading): `doTX()` reads `ringBuffer[iRead]`, transmits it, updates the status byte, and increments `iRead` directly.

**Overflow**: If `iWrite` catches `iRead`, `addRingPointer` forcibly advances `iRead` (dropping the oldest unread message) and prints `[MC-DBG] RING_OVERFLOW`.

**Critical**: After `doTX()` reads and advances `iRead`, the consumed slot is NOT zeroed. It retains its data (`size > 0`, status `0x01`). The `updateRetransmissionStatus()` function scans ALL `MAX_RING` slots (not just between `iRead` and `iWrite`), so it finds these consumed-but-not-cleared entries and starts their retransmit timer.

### 2.4 Status Byte State Machine (`ringBuffer[i][1]`)

```
0x00  →  Queued, not yet sent. Retransmit eligible.
          Set on insertion for text messages (type 0x3A) not starting with "{"

0x01  →  Sent once. Retransmit timer started.
          Set by doTX() when it sends a 0x00 entry

0x02–0x0F  →  Timer counting. Incremented every 2 seconds by updateRetransmissionStatus()

0x10  →  Retransmit trigger threshold (15 ticks × 2s = 30s).
          updateRetransmissionStatus() detects this:
            - marks original → 0xFF (done)
            - copies message to ringBuffer[iWrite] with status 0x7F
            - calls addRingPointer(iWrite, iRead, MAX_RING)

0x7F  →  Retransmission copy. Waiting to be sent by doTX().
          doTX() sets 0x7F → 0xFF after sending

0xFF  →  Done / no retransmission. Terminal state.
          Set for: positions, ACKs, Hey messages, {CET}/{MCP} payloads,
          completed retransmits, cancelled retransmits (heard-back via RX)
```

### 2.5 Dedup Ring Buffer (RX side)

**Declared**: `src/loop_functions.cpp` line 253

```cpp
uint8_t ringBufferLoraRX[MAX_RING][5] = {0};
uint8_t loraWrite = 0;
```

- Write-only ring (no read pointer). Stores 4-byte msg_id + 1-byte server flag.
- `is_new_packet()` (lora_functions.cpp line 925) does linear scan of all `MAX_RING` entries.
- After `MAX_RING` insertions, old msg_ids are silently overwritten → duplicate-as-new risk.

### 2.6 Own Message Tracking

**Declared**: `src/loop_functions.cpp` line 243

```cpp
uint8_t own_msg_id[MAX_RING][5] = {0};
int iWriteOwn=0;
```

- Stores msg_ids of messages this node originated.
- `checkOwnTx(msg_id)` (line 496) returns index if found, -1 if not.
- Used to distinguish "my message echoed back" from "someone else's message to relay".

---

## 3. Three Sources Fill the Ring Buffer

### 3.1 Own Messages (locally generated)

**File**: `src/loop_functions.cpp` lines 2334–2371

When the user sends a text message (BLE/phone/web):

```cpp
ringBuffer[iWrite][0] = aprsmsg.msg_len;
memcpy(ringBuffer[iWrite]+2, msg_buffer, aprsmsg.msg_len);
ringBuffer[iWrite][1] = 0x00;  // retransmit enabled for text
addRingPointer(iWrite, iRead, MAX_RING);
```

Also calls `insertOwnTx(aprsmsg.msg_id)` to register in `own_msg_id[]`.

### 3.2 Mesh Relay Messages (received via LoRa, forwarded)

**File**: `src/lora_functions.cpp` lines 820–840

When a non-own, non-duplicate message with hop count > 0 is received:

```cpp
memset(ringBuffer[iWrite], 0x00, UDP_TX_BUF_SIZE+1);
ringBuffer[iWrite][0] = size;
memcpy(ringBuffer[iWrite]+2, RcvBuffer, size);
if (ringBuffer[iWrite][2] == 0x3A)      // text message?
    ringBuffer[iWrite][1] = 0x00;        // ← BUG: retransmit enabled for RELAY messages!
else
    ringBuffer[iWrite][1] = 0xFF;
addRingPointer(iWrite, iRead, MAX_RING);
```

**This is the primary bug.** Relay text messages get status `0x00`, making them eligible for retransmission. The relaying node will retransmit someone else's message after 30 seconds if it doesn't hear an echo — consuming 2 ring buffer slots per relay message. With 4 mesh stations on the channel, this multiplies rapidly.

### 3.3 Retransmit Copies

**File**: `src/lora_functions.cpp` lines 1149–1221

`updateRetransmissionStatus()` is called every 2 seconds. It scans ALL `MAX_RING` slots. When a slot's status reaches `0x10` (30 seconds):

```cpp
ringBuffer[ircheck][1] = 0xFF;                              // mark original done
memcpy(ringBuffer[iWrite], ringBuffer[ircheck], size + 2);  // copy to new slot
ringBuffer[iWrite][1] = 0x7F;                               // mark as retransmit
addRingPointer(iWrite, iRead, MAX_RING);                    // advance iWrite
```

Each retransmit consumes one additional ring buffer slot. With 30 slots and multiple pending retransmissions, overflow is inevitable.

### 3.4 ACK Relay Messages

**File**: `src/lora_functions.cpp` lines 166–170

ACK relay messages are correctly handled — they always get status `0xFF` (no retransmit). No fix needed.

---

## 4. Fixes

### FIX #1: Mesh Relay — Fire and Forget

**File**: `src/lora_functions.cpp` lines 824–832

**The bug**: Relay text messages get status `0x00` (retransmit enabled). A relaying node should NOT retransmit other stations' messages. Only the originating node should retransmit.

**Current code** (lines 824–832):

```cpp
if (ringBuffer[iWrite][2] == 0x3A) // only Messages
{
    if(aprsmsg.msg_payload.startsWith("{") > 0)
        ringBuffer[iWrite][1] = 0xFF;
    else
        ringBuffer[iWrite][1] = 0x00; // retransmission Status
}
else
    ringBuffer[iWrite][1] = 0xFF;
```

**Patched code** — replace lines 824–832 with:

```cpp
// FIX: Relay messages are fire-and-forget.
// Only the ORIGINATING node should retransmit.
// We relay once (via the ring buffer TX path) and then discard.
ringBuffer[iWrite][1] = 0xFF; // no retransmission for ANY relay message

if(bLORADEBUG)
{
    unsigned int relay_msg_id = (ringBuffer[iWrite][6]<<24) | (ringBuffer[iWrite][5]<<16) | (ringBuffer[iWrite][4]<<8) | ringBuffer[iWrite][3];
    Serial.printf("[MC-DBG] RELAY_QUEUED msg_id=%08X type=%02X len=%d\n",
        relay_msg_id, ringBuffer[iWrite][2], size);
}
```

**Expected impact**: Eliminates relay message retransmissions entirely. With mesh ON, this should reduce ring buffer pressure by ~50% (179 RADIO_TX events in the test, most were relays that would have generated retransmit copies).

**RAM impact**: Zero.

---

### FIX #2: Retransmit Cap — Maximum 3 Retries with Exponential Backoff

**Files**: `src/loop_functions.cpp` (new array), `src/lora_functions.cpp` (updateRetransmissionStatus)

**The problem**: The current retransmit mechanism allows only 1 retry (status `0x7F` → `0xFF` after sending). But if that single retry is also lost, the message is gone forever. We need up to 3 retries with increasing backoff to balance reliability and ring buffer pressure.

#### 2a. Add retry counter array

**File**: `src/loop_functions.cpp`, after line 250 (after `int iRetransmit=-1;`)

Add:

```cpp
// FIX: Per-slot retry counter. Tracks how many times each msg_id has been retransmitted.
// Index corresponds to ringBuffer slot. Reset to 0 when a new message is written to the slot.
uint8_t retryCount[MAX_RING] = {0};
```

**RAM impact**: 30 bytes.

**Declare extern** in `src/loop_functions_extern.h`:

```cpp
extern uint8_t retryCount[MAX_RING];
```

#### 2b. Reset retry counter on new message insertion

Every place that writes to `ringBuffer[iWrite]` before calling `addRingPointer()` must also reset `retryCount[iWrite] = 0`. There are 4 insertion points:

1. **Own messages** (`src/loop_functions.cpp` ~line 2334): Add `retryCount[iWrite] = 0;` before `addRingPointer()`.
2. **Relay messages** (`src/lora_functions.cpp` ~line 840): Add `retryCount[iWrite] = 0;` before `addRingPointer()`. (Note: after FIX #1, relay messages have status `0xFF`, so retransmission won't trigger. But reset for correctness.)
3. **ACK relay** (`src/lora_functions.cpp` ~line 170): Add `retryCount[iWrite] = 0;` before `addRingPointer()`.
4. **UDP/Server messages** (`src/udp_functions.cpp` ~line 332): Add `retryCount[iWrite] = 0;` before `addRingPointer()`.

#### 2c. Modify updateRetransmissionStatus with cap and backoff

**File**: `src/lora_functions.cpp`, replace the `updateRetransmissionStatus()` function (lines 1149–1221):

```cpp
// Maximum retransmit attempts per message
#define MAX_RETRANSMIT 3

bool updateRetransmissionStatus()
{
    for(int ircheck=0; ircheck < MAX_RING; ircheck++)
    {
        // Non-text messages: force no-retransmit
        if(ringBuffer[ircheck][2] != 0x3A)
        {
            ringBuffer[ircheck][1] = 0xFF;
        }

        int size = ringBuffer[ircheck][0];

        if(size > 0 && ringBuffer[ircheck][1] != 0x00 && ringBuffer[ircheck][1] != 0xFF)
        {
            ringBuffer[ircheck][1]++;

            // Retransmit threshold with exponential backoff:
            //   Retry 1: 0x10 (15 ticks × 2s = 30s)
            //   Retry 2: 0x20 (31 ticks × 2s = 62s)
            //   Retry 3: 0x30 (47 ticks × 2s = 94s)
            uint8_t threshold = 0x10 * (retryCount[ircheck] + 1);

            if(ringBuffer[ircheck][1] == threshold)
            {
                // Check retry cap
                if(retryCount[ircheck] >= MAX_RETRANSMIT)
                {
                    // Give up — max retries exhausted
                    ringBuffer[ircheck][1] = 0xFF;

                    if(bLORADEBUG)
                    {
                        unsigned int ring_msg_id = (ringBuffer[ircheck][6]<<24) | (ringBuffer[ircheck][5]<<16) | (ringBuffer[ircheck][4]<<8) | ringBuffer[ircheck][3];
                        Serial.printf("[MC-DBG] RETRANSMIT_GIVEUP retries=%d msg_id=%08X\n",
                            retryCount[ircheck], ring_msg_id);
                    }

                    continue;
                }

                // Debug: log retransmit
                if(bLORADEBUG)
                {
                    unsigned int ring_msg_id = (ringBuffer[ircheck][6]<<24) | (ringBuffer[ircheck][5]<<16) | (ringBuffer[ircheck][4]<<8) | ringBuffer[ircheck][3];
                    Serial.printf("[MC-DBG] RETRANSMIT retry=%d after_sec=%d msg_id=%08X\n",
                        retryCount[ircheck] + 1, (ringBuffer[ircheck][1] - 1) * 2, ring_msg_id);
                }

                int ring_msg_lng = ringBuffer[ircheck][0];

                if(bDisplayRetx)
                {
                    unsigned int ring_msg_id = (ringBuffer[ircheck][6]<<24) | (ringBuffer[ircheck][5]<<16) | (ringBuffer[ircheck][4]<<8) | ringBuffer[ircheck][3];
                    Serial.printf("\n[RETX] Retransmit retid:%i status:%02X lng;%02X msg-id: %c-%08X retry:%d\n",
                        ircheck, ringBuffer[ircheck][1], ringBuffer[ircheck][0], ringBuffer[ircheck][2], ring_msg_id, retryCount[ircheck] + 1);
                }

                // Mark original as done
                ringBuffer[ircheck][1] = 0xFF;

                // Copy message to new slot at iWrite
                memcpy(ringBuffer[iWrite], ringBuffer[ircheck], size + 2);

                if (ringBuffer[iWrite][2] == 0x3A) // text messages
                {
                    ringBuffer[iWrite][1] = 0x01;  // start timer immediately (already "sent once" logically)
                }
                else
                {
                    ringBuffer[iWrite][1] = 0xFF;
                }

                // Transfer and increment retry count
                retryCount[iWrite] = retryCount[ircheck] + 1;

                addRingPointer(iWrite, iRead, MAX_RING);

                return true;
            }
        }
    }

    return false;
}
```

**Key changes from original**:

| Aspect | Before | After |
|--------|--------|-------|
| Retry cap | None (single 0x7F copy, then done) | 3 retries max |
| Backoff | Fixed 30s | 30s → 62s → 94s (exponential) |
| Retransmit copy status | `0x7F` (special, one-shot) | `0x01` (re-enters normal timer cycle with higher threshold) |
| Retry tracking | None | `retryCount[MAX_RING]` array (30 bytes) |
| Debug | `[MC-DBG] RETRANSMIT` | `[MC-DBG] RETRANSMIT retry=N` + `[MC-DBG] RETRANSMIT_GIVEUP` |

#### 2d. Update doTX to handle the new retry copy status

**File**: `src/lora_functions.cpp`, in `doTX()` around line 979–991

The old code had special handling for `0x7F`:

```cpp
if(ringBuffer[iRead][1] == 0x00)
    ringBuffer[iRead][1] = 0x01;

if(ringBuffer[iRead][1] == 0x7F)
{
    ringBuffer[iRead][1] = 0xFF;
}
```

**Replace with**:

```cpp
if(ringBuffer[iRead][1] == 0x00)
    ringBuffer[iRead][1] = 0x01; // mark first send

// FIX: 0x7F handling removed. Retransmit copies now use status 0x01
// and re-enter the normal timer cycle with retryCount tracking.
```

The `0x7F` status is no longer used. Retransmit copies start at `0x01` and count up to their backoff threshold (`0x10 * (retryCount + 1)`).

#### 2e. Cancel-on-RX must also clear retry counter

**File**: `src/lora_functions.cpp` lines 196–213

When a message is heard back, the retransmit is cancelled. Also clear the retry counter:

**Current code** (line 205):

```cpp
ringBuffer[ircheck][1] = 0xFF; // no retransmission
```

**Add after line 205**:

```cpp
retryCount[ircheck] = 0; // clear retry counter
```

**RAM impact for FIX #2 total**: 30 bytes.

---

### FIX #3: Clear Consumed Ring Buffer Entries

**File**: `src/lora_functions.cpp`, in `doTX()` around line 967–995

**The problem**: After `doTX()` reads a slot and advances `iRead`, the old slot retains data (`size > 0`, status `0x01`). `updateRetransmissionStatus()` scans ALL slots, so it finds and processes these stale entries. While this is intentional for retransmit tracking, it means "zombie" entries with `0x01` status accumulate in slots behind `iRead`.

**The fix**: After `doTX()` reads and processes a slot, zero out the slot's length and status fields. The retransmit timer should only apply to the CURRENT retransmit copy (which is ahead of `iRead`), not the consumed original.

However, this changes the retransmit architecture: currently, the original slot holds the retransmit timer, and at threshold, a COPY is made. If we zero the original after `doTX` sends it, we need the retransmit timer to run on the copy instead.

**New approach**: When `doTX()` sends a retransmit-eligible message (status `0x00` → `0x01`), immediately create a "shadow" entry for retransmit tracking, then zero the consumed slot.

**Actually, simpler approach**: Don't zero consumed slots — the retransmit mechanism depends on them being there. Instead, just ensure FIX #1 (relay fire-and-forget) and FIX #2 (retry cap) prevent overflow. The zombie entries are a feature, not a bug, as long as they don't proliferate beyond control.

**Decision**: Do NOT zero consumed slots. FIX #1 + FIX #2 are sufficient.

---

## 5. New Debug Messages

### 5.1 Messages to ADD

All gated behind `bLORADEBUG` unless noted.

#### N. Relay message queued (in FIX #1 patched code)

```cpp
Serial.printf("[MC-DBG] RELAY_QUEUED msg_id=%08X type=%02X len=%d\n",
    relay_msg_id, ringBuffer[iWrite][2], size);
```

#### O. Retransmit with retry count (in FIX #2 patched code)

```cpp
Serial.printf("[MC-DBG] RETRANSMIT retry=%d after_sec=%d msg_id=%08X\n",
    retryCount[ircheck] + 1, (ringBuffer[ircheck][1] - 1) * 2, ring_msg_id);
```

#### P. Retransmit give-up (in FIX #2 patched code)

```cpp
Serial.printf("[MC-DBG] RETRANSMIT_GIVEUP retries=%d msg_id=%08X\n",
    retryCount[ircheck], ring_msg_id);
```

#### Q. Ring buffer utilization (add to main loop, every 30 seconds)

**File**: `src/esp32/esp32_main.cpp`, in the main loop, add a periodic ring buffer status report:

```cpp
// Add a static timer variable near the top of the main loop radio section
static unsigned long ring_status_timer = 0;

// Inside the main loop, after the retransmit timer check:
if(bLORADEBUG && (millis() - ring_status_timer) > 30000)
{
    ring_status_timer = millis();
    int pending = 0, retrying = 0, done = 0;
    for(int i = 0; i < MAX_RING; i++)
    {
        if(ringBuffer[i][0] == 0) continue;
        if(ringBuffer[i][1] == 0xFF) done++;
        else if(ringBuffer[i][1] == 0x00) pending++;
        else retrying++;
    }
    int queued = (iWrite >= iRead) ? (iWrite - iRead) : (MAX_RING - iRead + iWrite);
    Serial.printf("[MC-DBG] RING_STATUS queued=%d pending=%d retrying=%d done=%d iW=%d iR=%d\n",
        queued, pending, retrying, done, iWrite, iRead);
}
```

### 5.2 Summary

| Tag | File | Purpose |
|-----|------|---------|
| `RELAY_QUEUED` | lora_functions.cpp | Confirms relay message inserted with 0xFF (no retransmit) |
| `RETRANSMIT` (updated) | lora_functions.cpp | Now includes `retry=N` showing which attempt |
| `RETRANSMIT_GIVEUP` | lora_functions.cpp | Message abandoned after MAX_RETRANSMIT attempts |
| `RING_STATUS` | esp32_main.cpp | Periodic ring buffer utilization snapshot |

---

## 6. Architecture Decision Record: Mesh Message ACK Process

### ADR-001: ACK Handling for Mesh-Relayed Messages

**Status**: OPEN QUESTION

**Context**: When Station A sends a message and Station B relays it, the current firmware behavior is:

1. A sends message (status `0x00`, retransmit enabled)
2. B receives message, inserts into ring buffer for relay
3. B transmits the relayed message
4. A hears its own message echoed back → cancels retransmit (status → `0xFF`)

With FIX #1, Step 2 changes: B inserts with status `0xFF` (no retransmit). B sends it once and forgets it.

**Open questions**:

1. **Who confirms delivery in mesh mode?** If A sends to C via B, and B relays:
   - Does A consider delivery confirmed when it hears B's relay? (Current behavior: yes, via cancel-on-RX)
   - Or does A need an ACK from C (the final destination)?
   - If C is out of range of A, how does the ACK get back? Does it also mesh-relay?

2. **What if B's relay is lost?** Station B relays once (fire-and-forget). If that relay transmission is corrupted (CRC error, collision), Station C never receives the message. Station A heard B's relay and cancelled its retransmit. The message is lost.
   - Should A only cancel retransmit when it receives an ACK from C, not just any echo?
   - This requires end-to-end ACK, which adds complexity and latency.

3. **What if nobody relays?** If B is the only relay-capable station and it relays once with fire-and-forget, there is exactly one chance for C to receive it. With the old behavior (relay + retransmit), B would retry after 30s, giving C a second chance.
   - Is single-attempt relay acceptable?
   - Should the hop count or RSSI influence retry behavior?

4. **Ring buffer priority**: If the ring buffer is full and a relay message needs to be inserted, should it:
   - Drop the relay (current behavior: RING_OVERFLOW drops oldest entry)
   - Drop the oldest relay to make room for own messages?
   - Skip relay entirely when ring buffer is > 80% full?

**Decision for now**: Relay messages are fire-and-forget (FIX #1). This prevents ring buffer overflow from relay traffic. The trade-off is reduced mesh reliability — acceptable for this PoC where the two test nodes are 3 meters apart and mesh relay is unnecessary.

**Future work**: Implement a lightweight end-to-end ACK mechanism for mesh-relayed text messages. The ACK relay path (Section 3.4) already exists with status `0xFF` — it just needs to be tied to the originator's retransmit cancellation logic.

---

## 7. Files to Modify

| File | Changes | Lines Affected |
|------|---------|----------------|
| `src/lora_functions.cpp` | FIX #1 (relay fire-and-forget), FIX #2 (retransmit cap+backoff), cancel-on-RX retry clear, doTX 0x7F removal | ~824–832, ~979–991, ~196–213, ~1149–1221 |
| `src/loop_functions.cpp` | Add `retryCount[MAX_RING]` array, reset on own message insertion | after line 250, ~line 2334 |
| `src/loop_functions_extern.h` | Declare `extern uint8_t retryCount[]` | near other extern declarations |
| `src/udp_functions.cpp` | Reset `retryCount[iWrite]` on UDP message insertion | ~line 332 |
| `src/esp32/esp32_main.cpp` | Add RING_STATUS periodic debug message | main loop radio section |

**Total new RAM**: 30 bytes (`retryCount[MAX_RING]`).

No new files. No new dependencies. No architecture changes.

---

## 8. Verification Plan

### 8.1 Success Criteria

| Metric | Before fix | Target |
|--------|-----------|--------|
| RING_OVERFLOW events (mesh ON) | 52 per run | **0** |
| RING_OVERFLOW events (mesh OFF) | 89 per run | **0** |
| Message loss (mesh OFF, unidirectional) | 0% | 0% (maintain) |
| Message loss (mesh OFF, bidi 15s) | 50–55% | **< 15%** |
| RETRANSMIT_GIVEUP events | N/A | visible, confirms cap works |
| RELAY_QUEUED events (mesh ON) | N/A | visible, confirms fire-and-forget |
| RING_STATUS queued count | N/A | should stay < 20 (never approach 30) |

### 8.2 How to verify each fix

**FIX #1 verified by**: With mesh ON, count `RELAY_QUEUED` events. These replace the old relay insertions with `0x00`. No RETRANSMIT events should appear for relay messages (their msg_ids will differ from own msg_ids). RING_OVERFLOW count should drop dramatically.

**FIX #2 verified by**: With mesh OFF, run bidi tests (TC-T08). RETRANSMIT events should show `retry=1`, `retry=2`, `retry=3`. After retry 3, `RETRANSMIT_GIVEUP` should appear. Total retransmit copies in the ring buffer should be bounded: at most `MAX_RETRANSMIT` copies per original message.

**RING_STATUS verified by**: The periodic 30-second snapshot shows ring buffer utilization. `queued` should stay well below `MAX_RING`. `retrying` should be bounded by the number of own messages in flight × `MAX_RETRANSMIT`.

### 8.3 Test commands

**Run the same test suite with mesh off** (compare directly to previous results):

```bash
uv run lora-harness run --test TC-T02 --test TC-T04 --test TC-T06 --test TC-T07 --test TC-T08
```

**Then with mesh on** (confirm relay fire-and-forget works):

```bash
uv run lora-harness run --test TC-T02 --test TC-T04 --test TC-T06 --test TC-T07 --test TC-T08
```

---

## 9. Patch Application Order

Apply in this order:

1. **First**: Add `retryCount[MAX_RING]` array and `extern` declaration (FIX #2a, #2b). Add all 4 reset points. This is a no-op without the other changes — just adds a zeroed array.
2. **Second**: Apply FIX #1 (relay fire-and-forget). Run with mesh ON. Verify RELAY_QUEUED events and reduced RING_OVERFLOW.
3. **Third**: Apply FIX #2c (updateRetransmissionStatus rewrite) and FIX #2d (doTX cleanup) and FIX #2e (cancel-on-RX clear). Run with mesh OFF. Verify retry=1/2/3 and GIVEUP events.
4. **Fourth**: Add RING_STATUS periodic debug. Run full test. Verify utilization stays healthy.

---

## 10. Risk Assessment

| Fix | Risk | Mitigation |
|-----|------|------------|
| FIX #1: Relay fire-and-forget | Medium — mesh reliability decreases (relay messages sent once only) | Acceptable for PoC. ADR-001 tracks the open question. |
| FIX #2: retryCount array | Low — 30 bytes RAM, simple counter | Array is zeroed on init, reset on insertion. No overflow possible (uint8_t max 255, we cap at 3). |
| FIX #2: Backoff threshold formula | Low — `0x10 * (retryCount + 1)` may exceed 0xFF if retryCount > 15 | Capped at MAX_RETRANSMIT=3, so max threshold is `0x10 * 4 = 0x40`. Well within uint8_t range. |
| FIX #2: Retransmit copy starts at 0x01 | Low — enters normal timer path instead of one-shot 0x7F | Timer counts up normally. retryCount prevents infinite retries. |
| FIX #2: 0x7F removal from doTX | Low — legacy status no longer used | No existing code path creates 0x7F entries after this patch. Old entries in ring buffer at boot would be harmless (status > 0x10, never triggers). |

---

## 11. Summary

Two fixes to the MeshCom 4.35k firmware ring buffer:

1. **FIX #1 — Relay fire-and-forget**: Set status `0xFF` for all mesh relay messages. Eliminates relay retransmissions that flood the ring buffer. Zero RAM cost.
2. **FIX #2 — Retransmit cap with backoff**: Add `retryCount[MAX_RING]` (30 bytes). Cap at 3 retries with exponential backoff (30s, 62s, 94s). Prevents unbounded retransmit copies.

Plus 4 new `[MC-DBG]` debug messages (`RELAY_QUEUED`, `RETRANSMIT` updated with retry count, `RETRANSMIT_GIVEUP`, `RING_STATUS`).

Plus ADR-001 documenting the open question about mesh message ACK handling.

**Expected outcome**: RING_OVERFLOW events drop from 52–89 per run to 0. Bidi message loss (mesh OFF) drops from 50–55% to < 15%.
