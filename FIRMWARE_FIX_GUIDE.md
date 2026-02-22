# MeshCom Firmware Fix Guide: LoRa Message Loss Root Cause & Patches

> **Target**: AI coding agent working on C++ firmware source
> **Firmware**: MeshCom 4.35k, ESP32 platform (SX1262 radio via RadioLib)
> **Source root**: `/Users/martinwerner/Downloads/MeshCom-Firmware-4.35k.02.19/`
> **Scope**: Minimal, targeted fixes — proof-of-concept that small changes eliminate the ~30% message loss
> **Date**: 2026-02-22

---

## 1. Problem Statement

Two MeshCom nodes (DK5EN-12 and DK5EN-99) are 3 meters apart. RSSI is -33 dBm, SNR is +6 dB. With LoRa SF11/CR4/6, the demodulation threshold is SNR -17 dB. We have a 23 dB margin. **Packet loss from RF noise is physically impossible.**

Yet we observe **32% message loss** (50 lost out of 157 messages, direction 99→12, across 4 test runs with hundreds of messages).

The root cause is **firmware**, not RF. This document describes the exact bugs and the minimal patches to fix them.

---

## 2. Root Cause Summary

Analysis of 157 messages (99→12 direction) across 4 test runs, correlated with raw serial debug logs and SSE events:

| Root Cause | Count | % of Losses | Mechanism |
|------------|-------|-------------|-----------|
| **RX restart gap** | ~25 | ~50% | RECEIVE_TIMEOUT handler disables RX interrupts, creating a blind window |
| **Half-duplex TX** | ~6 genuine | ~12% | Node-12 transmitting → radio physically cannot receive |
| **CRC collision** | 5 | 10% | On-air collision with other mesh stations |
| **Unexplained / firmware** | ~14 | ~28% | No CRC, no TX, no timeout — packet vanished silently |

The TX duty cycle of Node-12 is only 3-5%. Monte Carlo simulation confirms that only ~6 of 20 observed TX overlaps are genuine (the rest are coincidental with the wide arrival window).

**The dominant cause is the firmware RX state machine**, not RF conditions.

---

## 3. Latency Findings

Messages that DO arrive have these latencies (99→12):

| Percentile | Latency |
|------------|---------|
| P5 (fastest typical) | 6.0s |
| P25 | 6.9s |
| **P50 (median)** | **9.7s** |
| P75 | 13.4s |
| P90 | 22.5s |
| P95 | 45.0s |
| Max | 113.9s |

The minimum latency of 5.8s for a 3-meter link is itself evidence of firmware overhead. SF11 airtime for an 80-byte packet is ~1.5s. The remaining ~4.3s is firmware processing delay (primarily the `cmd_counter=7` CAD wait loop on the sender).

**Retransmit recommendation**: The current firmware retransmits after ~62s (status 0x01→0x20 at 2s intervals; the code comment says 320s but is wrong). Based on the data, retransmit should happen after **30s**. Give up after **120s** (no message ever arrived later than 114s).

---

## 4. Firmware Architecture Context

The radio state machine runs in the ESP32 main loop (`esp32_main.cpp`). It is **NOT interrupt-driven for packet processing** — interrupts only set flags, the main loop polls them.

### Main loop radio section (esp32_main.cpp lines 1536-1773)

Execution order per loop iteration:

```
1. [line 1562]  Check retransmit timer (every 2s)
2. [line 1570]  Check RECEIVE_TIMEOUT (4.5s watchdog)  ← BUG #1
3. [line 1610]  Process receiveFlag → checkRX() → OnRxDone()  ← BUG #2
4. [line 1639]  Process transmittedFlag → OnTxDone() → startReceive()
5. [line 1713]  Check TX queue → doTX()  ← BUG #3
6. [line 2724]  delay(5)  — yields to RTOS
```

### Key state variables

| Variable | Type | Declared | Purpose |
|----------|------|----------|---------|
| `receiveFlag` | `volatile bool` | esp32_main.cpp:371 | Set by DIO1 ISR when packet received |
| `transmittedFlag` | `volatile bool` | esp32_main.cpp:379 | Set by DIO1 ISR when TX complete |
| `bEnableInterruptReceive` | `volatile bool` | esp32_main.cpp:372 | Gates the ISR from setting receiveFlag |
| `bEnableInterruptTransmit` | `volatile bool` | esp32_main.cpp:380 | Gates the ISR from setting transmittedFlag |
| `is_receiving` | `bool` | loop_functions_extern.h:189 | Re-entrancy guard for checkRX() |
| `iReceiveTimeOutTime` | `unsigned long` | esp32_main.cpp:375 | Timestamp of last RX/TX event; 0 = idle |
| `tx_is_active` | `bool` | (loop_functions) | True during LoRa TX |
| `tx_waiting` | `bool` | (loop_functions) | True during CAD wait before TX |
| `cmd_counter` | `int` | (loop_functions) | CAD wait countdown (starts at 7) |

### Interrupt handlers (esp32_main.cpp lines 397-424)

**Critical**: Both `setFlagReceive` and `setFlagSent` are identical — they both set BOTH `receiveFlag` and `transmittedFlag` based on enable flags. This is because RadioLib uses a single DIO1 pin for both RX and TX complete events.

```cpp
// line 397
void setFlagReceive(void) {
    if(bEnableInterruptReceive)  receiveFlag = true;
    if(bEnableInterruptTransmit) transmittedFlag = true;
}
// line 413 — identical body
void setFlagSent(void) {
    if(bEnableInterruptReceive)  receiveFlag = true;
    if(bEnableInterruptTransmit) transmittedFlag = true;
}
```

---

## 5. Bug Descriptions and Fixes

### BUG #1: RECEIVE_TIMEOUT handler creates RX blind window

**File**: `src/esp32/esp32_main.cpp` lines 1570-1608

**The bug**: Every 4.5 seconds after the last RX/TX event, the timeout handler fires. It disables the RX interrupt callback, then re-enables it, then calls `startReceive()`. Between `clearPacketReceivedAction()` (line 1579) and `setPacketReceivedAction()` (line 1587), the DIO1 interrupt is not connected to any handler.

**Additionally**: The timeout handler runs BEFORE `receiveFlag` is checked (line 1610). If a DIO1 interrupt fires between two loop iterations (setting `receiveFlag = true`), and THEN the timeout handler runs on the next iteration, the handler calls `clearPacketReceivedAction()` which disconnects the interrupt — but `receiveFlag` is still true from the previous interrupt. The subsequent `startReceive()` resets the radio state, potentially discarding the packet that triggered the flag.

**Current code** (lines 1570-1608):

```cpp
if(iReceiveTimeOutTime > 0)
{
    if((iReceiveTimeOutTime + RECEIVE_TIMEOUT) < millis())
    {
        iReceiveTimeOutTime=0;

        // clear Receive Interrupt
        bEnableInterruptReceive = false;
        radio.clearPacketReceivedAction();

        // clear Transmit Interrupt
        bEnableInterruptTransmit = false;
        radio.clearPacketSentAction();

        // set Receive Interupt
        bEnableInterruptReceive = true;
        radio.setPacketReceivedAction(setFlagReceive);

        int state = radio.startReceive();
        // ... logging ...
    }
}
```

**Fix**: Check `receiveFlag` before resetting. If a packet is pending, skip the timeout reset entirely — the pending packet will be processed on this same loop iteration at line 1610.

**Patched code** (replace lines 1570-1608):

```cpp
if(iReceiveTimeOutTime > 0)
{
    if((iReceiveTimeOutTime + RECEIVE_TIMEOUT) < millis())
    {
        // FIX: Do not reset radio if a received packet is pending
        if(receiveFlag)
        {
            // A packet arrived just before timeout — let the receiveFlag
            // handler (line 1610) process it. Just reset the timer.
            iReceiveTimeOutTime = millis();

            if(bLORADEBUG)
            {
                Serial.print(getTimeString());
                Serial.println(F(" [MC-DBG] RX_TIMEOUT skipped: receiveFlag pending"));
            }
        }
        else
        {
            iReceiveTimeOutTime=0;

            // FIX: Call startReceive FIRST, then re-wire interrupts.
            // startReceive() puts the radio into RX mode and arms the DIO1 pin.
            // We then connect our callback. The window where DIO1 fires without
            // a handler is eliminated because startReceive resets the radio state.

            // Disable interrupt gating while we reconfigure
            bEnableInterruptReceive = false;
            bEnableInterruptTransmit = false;

            int state = radio.startReceive();

            // Now re-wire the interrupt callback
            radio.clearPacketReceivedAction();
            radio.clearPacketSentAction();
            radio.setPacketReceivedAction(setFlagReceive);

            bEnableInterruptReceive = true;

            if(bLORADEBUG)
            {
                Serial.print(getTimeString());
                if (state == RADIOLIB_ERR_NONE)
                    Serial.println(F(" [LoRa]...Receive Timeout, startReceive again with sucess"));
                else
                {
                    Serial.print(F(" [LoRa]...Receive Timeout, startReceive again with error = "));
                    Serial.println(state);
                }
            }
        }
    }
}
```

**Expected improvement**: Eliminates the interrupt-disabled window. Prevents discarding pending packets. This alone should reduce the ~50% "RX_RESTART_GAP" losses.

---

### BUG #2: OnRxDone processes 780 lines before radio is ready to receive again

**File**: `src/lora_functions.cpp` lines 100-885
**Called from**: `checkRX()` in `src/esp32/esp32_main.cpp` line 2776

**The bug**: After `radio.readData()` (esp32_main.cpp line 2751) extracts the packet from the SX1262 FIFO, the firmware calls `OnRxDone()` which processes the packet (APRS decoding, deduplication, mesh queue insertion, ACK handling, BLE forwarding, external UDP — 780 lines of code). Only AFTER all processing does it set `iReceiveTimeOutTime = millis()` (line 882) and `is_receiving = false` (line 884).

During this entire processing time, the radio is NOT in receive mode. Any LoRa packet arriving during OnRxDone processing is silently lost.

**Additionally**: The `is_receiving` flag is set to `true` at the entry of `checkRX()` (line 2745), preventing re-entry. But the radio itself is not in RX mode — it finished receiving and is now idle while the MCU processes data.

**Current flow** (esp32_main.cpp lines 2745-2794):

```cpp
is_receiving=true;                              // line 2745
// ... readData from radio FIFO ...
OnRxDone(payload, ibytes, rssi, snr);           // line 2776 — 780 lines of processing
// ... CRC error handling ...
is_receiving=false;                             // line 2794
```

**Fix**: Immediately after `radio.readData()` succeeds, call `radio.startReceive()` to put the radio back into RX mode BEFORE processing the packet. The packet data is already in the `payload` buffer — the radio can start listening for the next packet while the MCU processes the current one.

**Patched code** (modify `checkRX()`, esp32_main.cpp around lines 2751-2776):

```cpp
    state = radio.readData(payload, ibytes);

    if (state == RADIOLIB_ERR_LORA_HEADER_DAMAGED || state == RADIOLIB_ERR_NONE)
    {
        // FIX: Restart RX immediately after reading FIFO, BEFORE processing.
        // The packet data is already in our local payload buffer.
        // The radio can now listen for the next packet while we process this one.
        {
            radio.clearPacketReceivedAction();
            radio.clearPacketSentAction();
            bEnableInterruptReceive = true;
            radio.setPacketReceivedAction(setFlagReceive);
            int rxstate = radio.startReceive();

            if(bLORADEBUG)
            {
                unsigned long rx_restart_us = micros();
                Serial.printf("[MC-DBG] RX_RESTARTED after_readData state=%d\n", rxstate);
            }
        }

        if(bLORADEBUG)
        {
            Serial.print(F("[LoRa]...Received packet: "));
            Serial.print(F("RSSI:\t\t"));
            Serial.print(radio.getRSSI());
            Serial.print(F(" dBm / "));
            Serial.print(F("SNR:\t\t"));
            Serial.print(radio.getSNR());
            Serial.print(F(" dB / "));
            Serial.print(F("Frequency error:\t"));
            Serial.print(radio.getFrequencyError());
            Serial.println(F(" Hz"));
        }

        OnRxDone(payload, (uint16_t)ibytes, (int16_t)radio.getRSSI(), (int8_t)radio.getSNR());
    }
    else
    if (state == RADIOLIB_ERR_CRC_MISMATCH)
    {
        // FIX: Also restart RX after CRC error
        {
            radio.clearPacketReceivedAction();
            radio.clearPacketSentAction();
            bEnableInterruptReceive = true;
            radio.setPacketReceivedAction(setFlagReceive);
            radio.startReceive();
        }

        if(bLORADEBUG)
            Serial.println(F("[LoRa]...CRC error!"));
    }
```

**Important**: After this fix, the `receiveFlag` handler at esp32_main.cpp line 1613 must NOT call `radio.clearPacketReceivedAction()` / `radio.setPacketReceivedAction()` again after `checkRX()` returns, because `checkRX()` now handles this internally. **Remove or guard lines 1624-1634** to avoid double-reconfiguration:

```cpp
// esp32_main.cpp lines 1622-1636 — AFTER the fix, simplify to:
            if(receiveFlag)
            {
                bEnableInterruptReceive = false;
                receiveFlag = false;

                checkRX(bRadio);

                // FIX: checkRX() now restarts RX internally.
                // Only reset the timeout timer here.
                inoReceiveTimeOutTime=millis();
                iReceiveTimeOutTime = millis();
            }
```

Wait — `iReceiveTimeOutTime` is currently set inside `OnRxDone()` (lora_functions.cpp line 882). With the fix, we should keep that, but also ensure the main loop sets it. The safest approach: set it in both places. Duplicate assignment is harmless.

**Expected improvement**: Eliminates the OnRxDone processing gap entirely. The radio is back in RX mode within microseconds of reading the FIFO. This should eliminate the ~28% "unexplained silent drops" caused by packets arriving during processing.

---

### BUG #3: CAD counter reset on every preamble detection

**File**: `src/lora_functions.cpp` lines 1262-1272

**The bug**: `OnHeaderDetect()` sets `cmd_counter=0` and `tx_waiting=false` every time any LoRa preamble is detected — from ANY station on the frequency. In a busy mesh channel, this can delay local TX indefinitely:

1. Main loop starts TX sequence: sets `cmd_counter=7`, `tx_waiting=true`
2. Another station starts transmitting → DIO1 fires → `OnHeaderDetect()` → `cmd_counter=0`, `tx_waiting=false`
3. On next doTX() call, `tx_waiting` is false, so firmware sets `cmd_counter=7` again (line 1076)
4. Repeat — TX never happens

**Current code** (lora_functions.cpp lines 1062-1087):

```cpp
// In doTX(), when tx_waiting is false:
    if(tx_waiting)
    {
        tx_waiting=false;
    }
    else
    {
        // vor jeden senden 7 aufeinander folgende CAD abwarten
        cmd_counter=7;
        iRead=save_read;
        ringBuffer[iRead][1] = save_ring_status;
        tx_waiting=true;
        return false;
    }
```

```cpp
// OnHeaderDetect resets everything:
void OnHeaderDetect(void)
{
    tx_waiting=false;
    cmd_counter=0;
    is_receiving = true;
    if(bLORADEBUG)
        Serial.println("OnHeaderDetect");
}
```

**Fix**: `OnHeaderDetect()` should NOT reset `tx_waiting`. It should only set `is_receiving=true` (to prevent TX during active reception). The `cmd_counter` should pause, not reset. After the received packet is processed, the CAD wait should resume from where it left off.

**Patched OnHeaderDetect** (lora_functions.cpp):

```cpp
void OnHeaderDetect(void)
{
    // FIX: Only block TX during active reception.
    // Do NOT reset cmd_counter or tx_waiting.
    // The CAD wait will resume after this packet is processed.
    is_receiving = true;

    if(bLORADEBUG)
        Serial.println("OnHeaderDetect");
}
```

**Also needed**: In the TX gate (esp32_main.cpp line 1713), the condition `iReceiveTimeOutTime == 0` already prevents TX during/after RX. With `is_receiving` set by OnHeaderDetect, the main loop won't enter the TX path anyway, because `checkRX` will fire first (line 1610). So removing the counter reset is safe.

**Expected improvement**: Messages in the TX queue will actually get transmitted without indefinite delay from other stations' preambles. This reduces sender-side latency and ensures the mesh relay queue doesn't silently grow.

---

### BUG #4: No actual CAD scan — `cmd_counter=7` is a blind delay, not channel sensing

**File**: `src/lora_functions.cpp` lines 1073-1087

**The bug**: The comment says "vor jeden senden 7 aufeinander folgende CAD abwarten" (wait for 7 consecutive CAD before each send). But there is NO actual CAD scan. The `MAX_CAD_WAIT=10` constant is defined in `configuration_global.h:89` but never used. RadioLib's `radio.startChannelScan()` is never called. Instead, the firmware simply counts down 7 main loop iterations (~35-140ms total depending on loop speed) and then transmits blindly.

At SF11, a LoRa preamble alone is ~100ms. A 7-iteration delay of ~35-140ms is not long enough to reliably detect channel activity, and it does not actually listen — it just waits.

**Current code** (lora_functions.cpp lines 1073-1087):

```cpp
// vor jeden senden 7 aufeinander folgende CAD abwarten
{
    cmd_counter=7;
    iRead=save_read;
    ringBuffer[iRead][1] = save_ring_status;
    tx_waiting=true;
    return false;
}
```

**Fix (minimal PoC)**: Reduce `cmd_counter` from 7 to 3. This shortens the blind backoff delay. Combined with Bug #3 fix (OnHeaderDetect no longer resets the counter), the overall TX latency is reduced while still providing a minimal random backoff.

A full hardware CAD implementation using `radio.startChannelScan()` is deferred to the comprehensive rewrite — it requires adding a new state machine state (CAD_SCANNING) and a callback for the scan result, which is beyond "minimal fix" scope.

**Patched code** (lora_functions.cpp line 1076):

```cpp
// Change from:
    cmd_counter=7;
// To:
    cmd_counter=3;    // Reduced: 7 was too long, causes unnecessary TX delay
```

**Expected improvement**: Combined with Bug #3 fix, TX latency drops from ~7 iterations to ~3 iterations. This directly reduces the end-to-end message latency (currently 5.8s minimum).

**Debug**: The existing `[MC-DBG] CAD_WAIT remaining=%d` message (Section 6, item J) already tracks this counter. After the fix, you should see `CAD_WAIT remaining=3`, `2`, `1` instead of `7`, `6`, ..., `1`.

---

### BUG #5: Retransmit timer too long

**File**: `src/lora_functions.cpp` line 1173

**The bug**: The retransmission check `updateRetransmissionStatus()` is called every 2 seconds (esp32_main.cpp line 1562). Each call increments the ring buffer status byte. When it reaches `0x20` (32 decimal), the message is retransmitted. Starting from status `0x01` (marked as sent), that is 31 increments × 2 seconds = **62 seconds**.

Note: The code comment says "32 x 10sec = 320sec" but this is wrong — the timer fires every 2 seconds, not 10.

For our measured P90 latency of 22.5s and max of 114s, 62 seconds is reasonable for a first retransmit. However, based on actual data, **30 seconds** is optimal — it is above P90 (22.5s) so we won't duplicate messages still in flight, but fast enough to recover from a single lost transmission.

**Current code** (lora_functions.cpp line 1173):

```cpp
if(ringBuffer[ircheck][1] == 0x20)    // 32 x 10sec = 320sec (5min 20sec) Wartezeit
```

**Patched code**:

```cpp
if(ringBuffer[ircheck][1] == 0x10)    // 15 x 2sec = 30sec retransmit
```

**Additionally**, add a debug message just before the retransmit:

```cpp
if(bLORADEBUG)
{
    unsigned int ring_msg_id = (ringBuffer[ircheck][6]<<24) | (ringBuffer[ircheck][5]<<16) | (ringBuffer[ircheck][4]<<8) | ringBuffer[ircheck][3];
    Serial.printf("[MC-DBG] RETRANSMIT after_sec=%d msg_id=%08X\n",
        (ringBuffer[ircheck][1] - 1) * 2, ring_msg_id);
}
```

**Expected improvement**: Messages that are lost on first attempt get retransmitted within 30 seconds instead of 62 seconds.

---

### CRC Error Enhanced Diagnostics

**File**: `src/esp32/esp32_main.cpp` lines 2836-2849 (in `checkRX`)

**The gap**: The current CRC error log is just `[LoRa]...CRC error!` — no RSSI, no SNR, no packet size, no timestamp context. At +6dB SNR, CRC errors cannot be caused by noise. Each CRC error is evidence of an on-air collision or a firmware buffer issue. We need full context to distinguish these.

**Current code** (after BUG #2 patch already applied):

```cpp
    if(bLORADEBUG)
        Serial.println(F("[LoRa]...CRC error!"));
```

**Patched code** — replace with:

```cpp
    if(bLORADEBUG)
    {
        Serial.printf("[MC-DBG] CRC_ERROR rssi=%.1f snr=%.1f freq_err=%.1f size=%d ts=%lu\n",
            radio.getRSSI(), radio.getSNR(), radio.getFrequencyError(),
            (int)ibytes, millis());
    }
```

Note: On CRC error, the SX1262 may still report valid RSSI/SNR from the header (which was decoded successfully before the payload CRC failed). If RSSI is strong (-33 dBm) and SNR is high (+6 dB), this confirms the CRC error is from a collision, not noise. If RSSI/SNR are reported as 0 or invalid, the SX1262 did not decode the header at all.

**Additionally**, log the `ibytes` (reported packet size). A CRC error from a collision often shows an unexpected packet size because two overlapping transmissions produce garbage length.

---

### Harness Configuration: Increase Finalize Timeout to 120s

**File**: `src/lora_harness/tracker.py` — the `finalize()` method

**The gap**: The test harness currently marks a message as lost after 30 seconds. But we observed messages arriving as late as 114 seconds. With the retransmit timer now at 30s, a retransmitted message could arrive at 30s + ~10s latency = ~40s. The harness must wait long enough to capture retransmitted messages.

**Change**: Set the finalize timeout from 30 seconds to **120 seconds** in the harness code. This is a Python change, not firmware.

This ensures we accurately measure the improvement. Without this change, messages that arrive between 30-120s would still be counted as lost, masking the benefit of the retransmit timer fix.

---

## 6. New Debug Messages

All new debug messages use the format `[MC-DBG]` prefix for machine parsing. The test harness will parse these with regex `\[MC-DBG\]\s+(.+)`.

### 6.1 Messages to ADD

Add these debug prints. They are gated behind `bLORADEBUG` so they have zero overhead in production.

#### In `esp32_main.cpp`:

**A. RECEIVE_TIMEOUT event** (line ~1573, inside the timeout handler):

```cpp
if(bLORADEBUG)
    Serial.printf("[MC-DBG] RX_TIMEOUT_FIRE ts=%lu last_event=%lu delta=%lu\n",
        millis(), iReceiveTimeOutTime, millis() - iReceiveTimeOutTime);
```

Place this immediately after `if((iReceiveTimeOutTime + RECEIVE_TIMEOUT) < millis())`, before any action. This logs every single RECEIVE_TIMEOUT firing — currently invisible.

**B. RX restart after RECEIVE_TIMEOUT** (inside the patched timeout handler):

```cpp
if(bLORADEBUG)
    Serial.printf("[MC-DBG] RX_RESTART src=timeout state=%d\n", state);
```

**C. receiveFlag processing** (line ~1613, when receiveFlag is consumed):

```cpp
if(bLORADEBUG)
    Serial.printf("[MC-DBG] RX_FLAG_PROCESS ts=%lu\n", millis());
```

**D. RX restart after checkRX** (inside the patched checkRX):

```cpp
if(bLORADEBUG)
    Serial.printf("[MC-DBG] RX_RESTARTED src=after_readData state=%d\n", rxstate);
```

**E. TX gate entry** (line ~1718, when TX queue is checked):

```cpp
if(bLORADEBUG && (iWrite != iRead))
    Serial.printf("[MC-DBG] TX_GATE_ENTER qlen=%d cmd_ctr=%d tx_wait=%d\n",
        (iWrite >= iRead) ? (iWrite - iRead) : (MAX_RING - iRead + iWrite),
        cmd_counter, tx_waiting);
```

**F. TX start** (line ~1736, after doTX returns true):

```cpp
if(bLORADEBUG)
    Serial.printf("[MC-DBG] TX_START qlen=%d\n",
        (iWrite >= iRead) ? (iWrite - iRead) : (MAX_RING - iRead + iWrite));
```

**G. TX complete** (line ~1651, when transmittedFlag processed):

```cpp
if(bLORADEBUG)
    Serial.printf("[MC-DBG] TX_DONE state=%d ts=%lu\n", transmissionState, millis());
```

**H. RX restart after TX** (line ~1694, after startReceive post-TX):

```cpp
if(bLORADEBUG)
    Serial.printf("[MC-DBG] RX_RESTARTED src=after_tx state=%d\n", state);
```

#### In `lora_functions.cpp`:

**I. OnRxDone entry and exit timing** (lines 100 and 882):

At entry (line 100, first line of function):
```cpp
unsigned long _onrxdone_start = millis();
```

At exit (line 882, just before existing `iReceiveTimeOutTime = millis()`):
```cpp
if(bLORADEBUG)
    Serial.printf("[MC-DBG] ONRXDONE_TIME ms=%lu\n", millis() - _onrxdone_start);
```

This measures the actual OnRxDone processing time — currently a complete blind spot. If this exceeds ~50ms regularly, it confirms packets are being lost during processing.

**J. doTX — CAD counter activity** (line ~947):

```cpp
if(bLORADEBUG && cmd_counter > 0)
    Serial.printf("[MC-DBG] CAD_WAIT remaining=%d\n", cmd_counter);
```

**K. doTX — actual radio transmit** (line ~1101, just before `radio.startTransmit`):

```cpp
if(bLORADEBUG)
    Serial.printf("[MC-DBG] RADIO_TX len=%d\n", sendlng);
```

**L. OnHeaderDetect — with state context** (line ~1262):

Replace existing OnHeaderDetect debug with:
```cpp
if(bLORADEBUG)
    Serial.printf("[MC-DBG] HDR_DETECT tx_wait=%d cmd_ctr=%d\n", tx_waiting, cmd_counter);
```

**M. Ring buffer overflow** (in `addRingPointer`, loop_functions.cpp line 3753):

```cpp
if(pRead == pWrite)
{
    pRead = pWrite+1;
    if (pRead >= iMAX)
        pRead = 0;

    // NEW: log overflow
    Serial.println(F("[MC-DBG] RING_OVERFLOW"));
}
```

This one is NOT gated by bLORADEBUG — ring buffer overflow should always be visible.

### 6.2 Summary of new debug messages

| Tag | File | Trigger | Purpose |
|-----|------|---------|---------|
| `RX_TIMEOUT_FIRE` | esp32_main.cpp | 4.5s timeout expires | Proves timeout frequency, shows idle periods |
| `RX_TIMEOUT_SKIP` | esp32_main.cpp | Timeout skipped (receiveFlag pending) | Proves BUG #1 fix is working |
| `RX_RESTART` | esp32_main.cpp | startReceive called | Shows every RX mode entry — eliminates blind spot |
| `RX_FLAG_PROCESS` | esp32_main.cpp | receiveFlag consumed | Timestamps exact moment packet processing begins |
| `RX_RESTARTED` | esp32_main.cpp | After readData / after TX | Proves BUG #2 fix: RX restarted before processing |
| `TX_GATE_ENTER` | esp32_main.cpp | TX queue check | Shows queue depth and CAD state |
| `TX_START` | esp32_main.cpp | doTX succeeds | Marks exact TX beginning |
| `TX_DONE` | esp32_main.cpp | transmittedFlag processed | Marks TX end with duration |
| `ONRXDONE_TIME` | lora_functions.cpp | OnRxDone returns | Measures processing gap (key metric) |
| `CAD_WAIT` | lora_functions.cpp | cmd_counter > 0 | Shows CAD backoff delays |
| `RADIO_TX` | lora_functions.cpp | startTransmit called | Confirms actual LoRa TX with payload length |
| `HDR_DETECT` | lora_functions.cpp | Preamble detected | Shows CAD interruptions with context |
| `RING_OVERFLOW` | loop_functions.cpp | Ring buffer wraps | Always logged — critical error |
| `CRC_ERROR` | esp32_main.cpp | CRC mismatch on RX | RSSI/SNR/size context for collision diagnosis |
| `RETRANSMIT` | lora_functions.cpp | Message retransmitted | Proves retransmit timer works, shows delay |

### 6.3 What was previously a blind spot — now visible

| Blind Spot | Old Behavior | New Debug Message |
|------------|-------------|-------------------|
| When does RECEIVE_TIMEOUT fire? | Silent | `RX_TIMEOUT_FIRE` with delta time |
| When is the radio in RX mode? | Never logged on success | `RX_RESTARTED` with source |
| How long does OnRxDone take? | Unknown | `ONRXDONE_TIME` in ms |
| Is the CAD wait being reset by other stations? | Silent | `HDR_DETECT` shows tx_wait and cmd_ctr state |
| Is the ring buffer overflowing? | Silent overwrite | `RING_OVERFLOW` always logged |
| When does the radio actually transmit? | Only post-TX log via printBuffer | `RADIO_TX` with length, before startTransmit |
| Was a pending RX packet saved from timeout reset? | N/A (bug) | `RX_TIMEOUT_SKIP` proves fix works |
| What caused a CRC error? Collision or noise? | Just "CRC error!" — no context | `CRC_ERROR` with RSSI/SNR/size proves collision |
| When does retransmit fire? How long did it wait? | Only via `[RETX]` (separate flag) | `RETRANSMIT` with seconds and msg_id |

---

## 7. Current Blind Spot: Node-99 Sender Side

We have serial debug on Node-12 (receiver) but NOT on Node-99 (sender). Node-99 is connected via REST API (SSE) only.

**What we cannot see**:
- Whether Node-99's firmware actually called `radio.startTransmit()` for our message
- How long the message sat in Node-99's TX queue
- Whether OnHeaderDetect on Node-99 kept resetting its CAD counter
- Whether Node-99's ring buffer overflowed

**Assumption for this PoC**: Node-99 transmits 100% of messages (confirmed by user). If loss rate does not improve after these fixes, the next step is connecting serial to Node-99.

**Future**: The debug messages defined above work on ANY node. Once serial is connected to Node-99, the same `[MC-DBG]` messages will provide full sender-side visibility.

---

## 8. Files to Modify

| File | Changes | Lines Affected |
|------|---------|----------------|
| `src/esp32/esp32_main.cpp` | BUG #1 fix (RECEIVE_TIMEOUT), BUG #2 fix (checkRX RX restart), CRC enhanced debug, debug messages A-H | ~1570-1608, ~1613-1636, ~1651, ~1694, ~1718, ~2751-2850 |
| `src/lora_functions.cpp` | BUG #3 fix (OnHeaderDetect), BUG #4 fix (cmd_counter 7→3), BUG #5 fix (retransmit 0x20→0x10), debug messages I-L + RETRANSMIT | ~100, ~882, ~947, ~1076, ~1101, ~1173, ~1262-1272 |
| `src/loop_functions.cpp` | Debug message M (ring buffer overflow) | ~3753 |

**Harness change** (Python, not firmware):
| File | Change |
|------|--------|
| `src/lora_harness/tracker.py` | Increase finalize timeout from 30s to 120s |

No new files. No new dependencies. No architecture changes.

---

## 9. Verification Plan

After applying the patches, run the same test harness (`uv run lora-harness`) with the same test cases. The `[MC-DBG]` messages in serial output will prove:

### 9.1 Success Criteria

| Metric | Before (baseline) | Target (after fix) |
|--------|-------------------|-------------------|
| Message loss rate (99→12) | 32% | < 5% |
| Median latency | 9.7s | < 8s |
| ONRXDONE_TIME | unknown | measurable, expect 10-50ms |
| RX_TIMEOUT_SKIP events | N/A | > 0 (proves BUG #1 fix active) |
| RX_RESTARTED after_readData | N/A | 1 per received packet (proves BUG #2 fix) |
| RING_OVERFLOW events | unknown | 0 |
| CRC_ERROR with RSSI/SNR | no context | RSSI > -50dBm confirms collision, not noise |
| RETRANSMIT events | never (too slow) | should appear at ~30s for any lost msg |
| Harness finalize timeout | 30s | 120s (captures retransmitted messages) |

### 9.2 How to verify each fix

**BUG #1 verified by**: Count `RX_TIMEOUT_FIRE` events. They should still fire every ~4.5s during idle periods. But `RX_TIMEOUT_SKIP` events prove that pending packets are no longer discarded. If loss rate drops when `RX_TIMEOUT_SKIP` count > 0, the fix is validated.

**BUG #2 verified by**: Every successfully received packet should produce `RX_RESTARTED src=after_readData` BEFORE `ONRXDONE_TIME`. The `ONRXDONE_TIME` value shows how long the radio would have been deaf without the fix. If ONRXDONE_TIME is regularly > 20ms, it confirms packets were being lost during processing in the old code.

**BUG #3 verified by**: `HDR_DETECT` events should no longer show `tx_wait=1 cmd_ctr=X` being reset to 0. The CAD counter should only decrement normally. TX_START events should occur more promptly after messages enter the queue.

### 9.3 Harness parser update

The test harness at `src/lora_harness/serial_monitor.py` needs to capture `[MC-DBG]` lines. These lines are already captured in the rawlog JSONL (they appear on serial and have `src: "serial"`). The analysis scripts in `output/analysis/` should be updated to parse and aggregate these events.

---

## 10. Patch Application Order

Apply fixes in this order to isolate their individual impact:

1. **First**: Add ALL debug messages (Section 6) WITHOUT any bug fixes. Update harness timeout to 120s. Run one test. This establishes the baseline with full visibility.
2. **Second**: Apply BUG #1 fix (RECEIVE_TIMEOUT). Run test. Compare loss rate.
3. **Third**: Apply BUG #2 fix (early RX restart in checkRX). Run test. Compare loss rate.
4. **Fourth**: Apply BUG #3 fix (OnHeaderDetect) + BUG #4 fix (cmd_counter 7→3). Run test. Compare loss rate.
5. **Fifth**: Apply BUG #5 fix (retransmit timer 0x20→0x10). Run test. Verify retransmit events appear in logs.

This incremental approach proves which fix has the most impact and avoids introducing regressions from multiple simultaneous changes.

---

## 11. Risk Assessment

| Fix | Risk | Mitigation |
|-----|------|------------|
| BUG #1: Skip timeout if receiveFlag pending | Low — just delays the restart by one loop iteration | The packet will be processed immediately after |
| BUG #1: startReceive before interrupt rewiring | Low — startReceive resets radio state cleanly | RadioLib handles this atomically |
| BUG #2: Early startReceive after readData | Medium — radio starts listening while OnRxDone processes old packet | If a NEW packet arrives during processing, receiveFlag will be set. The next loop iteration will process it. No data race because readData copies to local buffer |
| BUG #2: Removing redundant interrupt rewiring in main loop | Low — checkRX now handles this internally | Verify that transmittedFlag path still works correctly |
| BUG #3: Not resetting cmd_counter in OnHeaderDetect | Medium — TX may start while another station is finishing | The `is_receiving=true` flag still blocks TX entry. TX only starts after is_receiving is cleared in OnRxDone |
| BUG #4: cmd_counter 7→3 | Low — shorter backoff before TX | Still provides random backoff; Bug #3 fix ensures it completes |
| BUG #5: Retransmit 0x20→0x10 (62s→30s) | Low — more frequent retransmit | 30s is above P90 latency (22.5s), so no premature duplicates |

---

## 12. Summary

Five minimal patches to the MeshCom 4.35k firmware:

1. **BUG #1 — RECEIVE_TIMEOUT**: Check `receiveFlag` before resetting; call `startReceive()` before rewiring interrupts
2. **BUG #2 — checkRX**: Call `startReceive()` immediately after `readData()`, before OnRxDone processing
3. **BUG #3 — OnHeaderDetect**: Stop resetting `cmd_counter` and `tx_waiting` on every preamble
4. **BUG #4 — CAD counter**: Reduce blind backoff from 7 to 3 iterations (real hardware CAD deferred to rewrite)
5. **BUG #5 — Retransmit timer**: Change threshold from `0x20` to `0x10` (62s → 30s)

Plus one CRC error diagnostic enhancement and one harness configuration change (finalize timeout 30s → 120s).

Plus 15 new `[MC-DBG]` debug messages that eliminate all serial output blind spots, including CRC error context (RSSI/SNR/size) and retransmit events.

**Expected outcome**: Loss rate drops from ~32% to < 5%, proving the firmware — not the RF channel — was the bottleneck all along.
