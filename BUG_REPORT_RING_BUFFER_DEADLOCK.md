# Bug Report: Ring Buffer Deadlock & Bidirectional Message Loss

> **Target**: AI coding agent working on MeshCom C++ firmware
> **Firmware**: MeshCom 4.35k, ESP32 platform (SX1262 radio via RadioLib)
> **Source root**: `/Users/martinwerner/Downloads/MeshCom-Firmware-4.35k.02.19/`
> **Date**: 2026-02-22
> **Prerequisite**: All patches from `FIRMWARE_FIX_GUIDE.md` and `RING_BUFFER_FIX_GUIDE.md` have been applied

---

## Executive Summary

Test harness run (12 test cases, 249 messages) reveals **75 lost messages (30%)** caused by three interacting firmware bugs. The bugs form a vicious cycle: aggressive retransmits saturate the ring buffer, which fills the TX queue, which monopolizes the half-duplex radio, which prevents reception of the other node's messages, which triggers more retransmits.

**Four bugs to fix (in priority order):**

| # | Bug | Severity | File | Impact |
|---|-----|----------|------|--------|
| 1 | `done` slots never reclaimed — ring buffer deadlocks permanently | **Critical** | `lora_functions.cpp`, `loop_functions.cpp` | 193 phantom RING_OVERFLOW events, 35+ minute deadlock |
| 2 | Retransmit parameters too aggressive (5 retries, 30s interval) | **High** | `lora_functions.cpp:1170` | TX channel monopolized for 150-283s per message |
| 3 | `updateRetransmissionStatus()` scans consumed slots outside iRead–iWrite | **High** | `lora_functions.cpp:1174` | Ghost retransmits from already-sent slots fill the buffer |
| 4 | Bug 1 fix clears slot too early — before CAD-wait/APRS rollback paths | **Medium** | `lora_functions.cpp:doTX()` | Zero-length TX on retry, message data lost from ring buffer |

---

## Test Evidence

```
Forward (12→99):  T04=0%, T07=0%, T09=0%, T11=3%     — forward path works
Reverse (99→12):  T07R=100%, T09R=70%, T08R=80%       — bidi reverse catastrophic
Unidirectional:   T04(99→12)=0%                        — reverse path alone works fine
```

Key insight: **unidirectional 99→12 = 0% loss**, but **bidirectional 99→12 = 70-100% loss**. The reverse path itself is fine — the problem is TX contention from the forward path monopolizing the radio.

---

## Bug 1: Ring Buffer Deadlock — `done` Slots Never Reclaimed (CRITICAL)

### Symptom

After ~50 minutes of operation, the ring buffer enters a permanent deadlock state:
```
[MC-DBG] RING_STATUS queued=0 pending=0 retrying=0 done=30 iW=18 iR=18
```
All 30 slots contain stale `0xFF` (done) entries with `size > 0`. The buffer reports `queued=0` (nothing to send), but every subsequent write triggers `RING_OVERFLOW` because `addRingPointer` wraps `iWrite` into `iRead`. **This deadlock persisted for 35+ minutes** in the test run, producing 166 phantom overflow events.

### Root Cause

`doTX()` consumes a slot by reading its data and advancing `iRead`, but **never clears the consumed slot's data**:

```cpp
// lora_functions.cpp:993-1015
if (iWrite != iRead && iRead < MAX_RING)
{
    sendlng = ringBuffer[iRead][0];
    memcpy(lora_tx_buffer, ringBuffer[iRead] + 2, sendlng);

    if(ringBuffer[iRead][1] == 0x00)
        ringBuffer[iRead][1] = 0x01;  // mark as sent, but slot retains size > 0

    iRead++;  // advance past slot, but slot data remains
    if (iRead >= MAX_RING)
        iRead = 0;
    // ... transmit ...
}
```

The consumed slot still has `ringBuffer[slot][0] > 0` (valid size) and `status = 0x01`. This slot is now "behind" `iRead` — logically consumed — but physically still holds data that `updateRetransmissionStatus()` will find (see Bug 3).

Eventually these ghost slots reach `0xFF` (done) via retransmit giveup. With all 30 slots holding `size > 0, status = 0xFF`, no new data can be written without triggering overflow.

### Fix

Clear consumed slots in `doTX()` immediately after reading them. Add after line 1013 (`iRead++`):

**File**: `src/lora_functions.cpp`, inside `doTX()`, after `iRead` is incremented (around line 1015)

```cpp
// Clear the consumed slot so it can be reused
ringBuffer[save_read][0] = 0;       // clear size — marks slot as empty
ringBuffer[save_read][1] = 0xFF;    // terminal status
retryCount[save_read] = 0;          // reset retry counter
```

Note: `save_read` already holds the pre-increment value of `iRead` (set at line 1004).

### Why This Alone Is Insufficient

Clearing at consumption time prevents **future** deadlocks, but doesn't handle slots that `updateRetransmissionStatus()` creates via retransmit copies (those also eventually reach `0xFF` and linger). The retransmit copy mechanism (Bug 3) can still fill the buffer from the `iWrite` side. All three bugs must be fixed together.

---

## Bug 2: Retransmit Parameters Too Aggressive

### Symptom

Each message generates up to 5 retransmission copies at ~30s intervals. With retransmit timer drift under load (actual intervals 34-74s), a single message occupies the TX pipeline for **150-283 seconds**. During this time:

- 6 ring buffer slots consumed per message (1 original + 5 retransmit copies)
- 6 × 710ms = 4.3s of TX airtime per message (radio deaf during TX)
- 93% of messages exhaust all retries (only 6 of 89 messages got ACKed)

With multiple messages in flight, the cumulative TX time monopolizes the radio and blocks reception. T12 QSO sweep data shows bidi works at gaps >= 23s but fails below that — the retransmit chain keeps the channel busy for ~20s after each send.

### Required Change

**File**: `src/lora_functions.cpp`, line 1170

Change:
```cpp
#define MAX_RETRANSMIT 5
```
To:
```cpp
#define MAX_RETRANSMIT 3
```

**File**: `src/lora_functions.cpp`, line 1190

Change the timer threshold from 0x10 (15 ticks × 2s = 30s) to 0x15 (20 ticks × 2s = 40s):
```cpp
uint8_t threshold = 0x15;  // 40s per retry (20 ticks × 2s)
```

**Impact**: Max retry period drops from 150-283s to 120s (3 × 40s). Max slots per message drops from 6 to 4. TX airtime per message drops from 4.3s to 2.8s.

Update the comment at lines 1188-1189 accordingly:
```cpp
// Fixed-interval retransmit: 40s per retry (20 ticks × 2s)
//   Retry 1-3: each waits 40s → total max 120s (2 min)
```

---

## Bug 3: `updateRetransmissionStatus()` Scans Outside Active Range

### Symptom

`updateRetransmissionStatus()` generates retransmit copies from slots that `doTX()` has already consumed. These are "ghost retransmits" — the original message was already sent, but the unconsumed slot data triggers a duplicate retransmission cycle.

### Root Cause

**File**: `src/lora_functions.cpp`, line 1174

```cpp
bool updateRetransmissionStatus()
{
    for(int ircheck=0; ircheck < MAX_RING; ircheck++)  // ← scans ALL 30 slots
    {
        int size = ringBuffer[ircheck][0];
        if(size > 0 && ringBuffer[ircheck][1] != 0x00 && ringBuffer[ircheck][1] != 0xFF)
        {
            ringBuffer[ircheck][1]++;  // tick status on consumed slots too!
            // ...
```

The loop iterates over ALL `MAX_RING` slots (0-29), regardless of whether they are in the active `iRead → iWrite` range. Slots behind `iRead` that still have `size > 0` and `status = 0x01..0x0F` (set by `doTX()` marking as "sent") will be ticked and eventually trigger retransmit copies.

### Fix

**If Bug 1 is fixed** (consumed slots cleared), this bug is automatically resolved — cleared slots will have `size = 0` and be skipped by the `size > 0` check at line 1184.

**However**, as a defense-in-depth measure, also restrict the scan to active slots only. Replace the loop at line 1174:

```cpp
bool updateRetransmissionStatus()
{
    // Only scan slots in the active iRead→iWrite range
    int count = (iWrite >= iRead) ? (iWrite - iRead) : (MAX_RING - iRead + iWrite);

    for(int q = 0; q < count; q++)
    {
        int ircheck = (iRead + q) % MAX_RING;

        // ... rest of function body unchanged, using ircheck ...
```

**Wait — this changes semantics.** Retransmit copies are written at `iWrite` and are in the active range. But already-consumed slots (which Bug 1 now clears) are behind `iRead`. So restricting to the active range means only queued-but-not-yet-sent and retransmit-copy slots are scanned. This is the correct behavior.

**Important edge case**: The retransmit copy at `iWrite` gets advanced into the active range by `addRingPointer`. When `doTX()` later consumes it, Bug 1's fix clears it. So the lifecycle is clean: write → queue → send → clear.

---

## Bug 4: Bug 1 Fix Clears Slot Too Early — Before Rollback Paths (MEDIUM)

### Symptom

After Bug 1's slot-clearing patch is applied, messages intermittently transmit as zero-length packets or are silently lost. This occurs specifically when `doTX()` enters a CAD (Channel Activity Detection) wait or when APRS chip-switching fails.

### Root Cause

Bug 1's fix placed the slot-clearing code **immediately after `iRead++`** (line ~1017), unconditionally:

```cpp
iRead++;
if (iRead >= MAX_RING)
    iRead = 0;

// BUG: This clears the slot BEFORE the transmit decision
ringBuffer[save_read][0] = 0;       // size zeroed
ringBuffer[save_read][1] = 0xFF;    // terminal status
retryCount[save_read] = 0;
```

However, `doTX()` has **two rollback paths** downstream that restore `iRead` to `save_read` and expect the slot data to still be intact:

1. **CAD wait path** (first call for a new message): Sets `cmd_counter=3`, restores `iRead=save_read` and `ringBuffer[iRead][1]=save_ring_status`, then returns false. On the next call (after the CAD delay), `doTX()` re-reads the slot — but `ringBuffer[save_read][0]` is already 0, so `sendlng=0` and a zero-length packet is transmitted.

2. **APRS chip-switch failure**: `lora_setchip_aprs()` returns false, restores `iRead=save_read` and `ringBuffer[iRead][1]=save_ring_status`, then returns false. Same problem on retry.

Both rollback paths only restore `ringBuffer[iRead][1]` (status byte) but **not** `ringBuffer[iRead][0]` (size), because the original code never zeroed the size — it didn't need to. With the Bug 1 fix zeroing the size eagerly, the rollback becomes incomplete.

### Impact

- **CAD wait path**: Every first-attempt message goes through this path (the `tx_waiting` gate). The slot is cleared, then `iRead` rolled back. On the second call `sendlng=0`, transmitting an empty packet. The actual message data is lost.
- **APRS chip failure**: Rare, but same effect — message lost on retry.
- **Net effect**: With eager clearing, Bug 1's fix inadvertently breaks normal message transmission. Every message that takes the CAD-wait path (which is *all* first-attempt messages) loses its payload.

### Fix

Defer slot clearing to **after** the transmit-or-drop decision is final. Three locations:

**1. After successful APRS transmit** (`return true` in the track/APRS branch):
```cpp
bSetLoRaAPRS = true;

// FIX Bug 4: Clear consumed slot only after successful transmit
ringBuffer[save_read][0] = 0;
ringBuffer[save_read][1] = 0xFF;
retryCount[save_read] = 0;

return true;
```

**2. After successful normal transmit** (`return true` in the main TX branch):
```cpp
// FIX Bug 4: Clear consumed slot only after successful transmit
ringBuffer[save_read][0] = 0;
ringBuffer[save_read][1] = 0xFF;
retryCount[save_read] = 0;

return true;
```

**3. After non-rollback drop paths** (TX disabled, decode failure — slot consumed but unsendable):
```cpp
// FIX Bug 4: Clear consumed slot on non-rollback drop paths
ringBuffer[save_read][0] = 0;
ringBuffer[save_read][1] = 0xFF;
retryCount[save_read] = 0;
```

The two **rollback paths** (CAD wait, APRS chip failure) do NOT clear the slot — they restore `iRead` and the status byte, leaving the slot intact for retry on the next `doTX()` call.

### Path Analysis

| Exit path | iRead restored? | Slot cleared? | Correct? |
|-----------|----------------|---------------|----------|
| CAD wait (`tx_waiting` gate) | Yes | No — slot retried next call | Yes |
| APRS chip-switch failure | Yes | No — slot retried next call | Yes |
| APRS TX success | No | Yes — slot consumed | Yes |
| Normal TX success | No | Yes — slot consumed | Yes |
| TX disabled / decode failure | No | Yes — slot dropped | Yes |

---

## How the Four Bugs Interact (Vicious Cycle)

```
Message sent by doTX()
    ↓
Slot NOT cleared (Bug 1) — retains size>0, status=0x01
    ↓
updateRetransmissionStatus() finds it (Bug 3) — scans ALL slots
    ↓
Status ticks to 0x10 → retransmit copy created at iWrite
    ↓
5 retransmit copies created over 150s (Bug 2)
    ↓
Each copy = 1 new ring buffer slot + 710ms TX airtime
    ↓
Ring buffer fills: iWrite catches iRead → RING_OVERFLOW
    ↓
TX airtime monopolized → radio deaf to incoming packets
    ↓
Remote node's messages not received → remote retransmits too
    ↓
Both nodes in retransmit storms → permanent deadlock
```

**After fixing all four bugs:**
```
Message sent by doTX()
    ↓
Slot CLEARED after successful TX (Bug 1+4 fix) — size=0, invisible to scanner
    ↓
CAD wait rollback preserves slot data (Bug 4 fix) — retry works correctly
    ↓
updateRetransmissionStatus() only scans active range (Bug 3 fix)
    ↓
Max 3 retransmits at 40s intervals (Bug 2 fix) → 120s max, 4 slots max
    ↓
Ring buffer stays well under capacity
    ↓
TX airtime reduced → radio has RX windows for incoming packets
    ↓
Bidirectional communication works
```

---

## Verification Plan

After applying all three fixes, re-run the test harness:

```bash
uv run lora-harness run --test TC-T09 --test TC-T11 --test TC-T12 --test TC-T02 --test TC-T04 --test TC-T06 --test TC-T07 --test TC-T08
```

**Expected improvements:**

| Metric | Before Fix | Expected After |
|--------|-----------|---------------|
| RING_OVERFLOW events | 193 | 0 |
| Ring buffer deadlock | 35+ min deadlock | Never |
| T07R (bidi fast, reverse) | 100% loss | < 30% loss |
| T09R (bidi slow, reverse) | 70% loss | < 10% loss |
| T08R (bidi medium, reverse) | 80% loss | < 30% loss |
| Max ring buffer occupancy | 30/30 (saturated) | < 15/30 |
| Retransmit giveups | 83 (93% failure) | < 20 |

**Monitoring**: Watch `[MC-DBG] RING_STATUS` output during tests. The `done` counter should stay near 0 (slots are cleared after consumption), and `queued` should never approach 30.

---

## File Change Summary

| File | Line(s) | Change |
|------|---------|--------|
| `src/lora_functions.cpp` | ~1015 (after `iRead++` in `doTX()`) | **Removed** eager slot clearing — replaced with deferred clearing (Bug 4 fix) |
| `src/lora_functions.cpp` | APRS TX success path (`return true`) | Add slot clearing: `ringBuffer[save_read][0] = 0; ringBuffer[save_read][1] = 0xFF; retryCount[save_read] = 0;` |
| `src/lora_functions.cpp` | Normal TX success path (`return true`) | Add slot clearing (same 3 lines) |
| `src/lora_functions.cpp` | After TX_ENABLE/decode-failure block | Add slot clearing for non-rollback drop paths (same 3 lines) |
| `src/lora_functions.cpp` | 1180 | `#define MAX_RETRANSMIT 3` (was 5) |
| `src/lora_functions.cpp` | 1203 | Change threshold to `0x15` (was `0x10`), update comment |
| `src/lora_functions.cpp` | 1183-1187 | Restrict scan to active iRead→iWrite range (defense-in-depth) |

Total: 7 changes across 1 file. Zero RAM cost. No API changes.

---

## Appendix: Key Data Points from Test Run

### Ring Buffer Timeline (from `[MC-DBG] RING_STATUS`)

```
t=0s       queued=0  done=0    — clean start
t=556s     queued=2  done=9    — normal operation
t=803s     queued=27 done=17   — buffer saturating (T11/T12 bidi)
t=837s     queued=29 done=11   — peak queue depth
t=1580s    queued=0  done=27   — queue drained, done slots accumulating
t=2987s    queued=0  done=30   — DEADLOCK: all slots are done, none reclaimable
t=4410s    queued=0  done=30   — still deadlocked 24 minutes later
t=4657s    queued=29 done=1    — new test burst hits deadlocked buffer
```

### Retransmit Statistics (from `[MC-DBG] RETRANSMIT` / `RETRANSMIT_GIVEUP`)

- 89 unique messages entered retransmit system
- 83 gave up after exhausting retries (93% — ACKs almost never arrive)
- 6 acknowledged before giveup
- 428 total RETRANSMIT events (avg 4.8 retries per message)
- Actual retry intervals: 34-74s (nominal 30s, inflated by TX queue contention)

### Half-Duplex Timing (from `[MC-DBG] TX_START` / `TX_DONE` / `RX_TIMEOUT_FIRE`)

- TX duration: ~710ms (56-byte packets), ~1130ms (106-byte packets)
- RX listen window: 4,501ms (RECEIVE_TIMEOUT constant)
- Minimum TX cycle: 5.2s (710ms TX + 4.5s RX)
- At 3s bidi intervals (T07): send rate (0.33/s) > drain rate (0.19/s) → queue grows indefinitely

### T12 QSO Sweep — Minimum Safe Bidi Gap

```
Gap 11-19s:  33-67% reverse loss
Gap 21s:     33% reverse loss
Gap 23-25s:  0% reverse loss   ← threshold
Gap 30s:     33% (1/3, noise)
```

The 23s threshold aligns with: initial TX (710ms) + ACK processing + retransmit at 30s partially overlapping with the next send. With 40s retransmit interval (Bug 2 fix), the safe gap should drop to ~15s.
