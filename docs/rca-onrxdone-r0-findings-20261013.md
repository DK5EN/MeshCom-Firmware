# RCA R0 findings: slow OnRxDone (Issue B), 2026-10-13

Read-only analysis wave R0-B of `docs/rca-brief-dup-onrxdone-20261013.md`. Nothing here changed
firmware, tools or captures. Code facts were read from the working tree of `fork-dev` (HEAD
`7edfc23a`, tree clean at session start). No commit is cited, so no "already fixed" claim is made.
Labels: **Fact** = read in code or counted in a capture; **Estimate** = computed from a model;
**Hypothesis** = plausible, not tested; **Recommendation** = proposal for the operator.

## Verdict (BLUF)

- **Fact:** on the ESP32 gateway about 91 % of the summed OnRxDone time in the overnight capture
  tracks the number of bytes printed to the serial/console path (`ms = -7 + 0.085 * bytes`, R2 =
  0.985; 115200 baud costs 0.0868 ms per byte). The RAK4631 (nRF52) prints a comparable volume (599 B median per frame)
  and takes 5 ms; the T-Deck Plus (ESP32-S3, USB CDC) also takes 5 ms but its capture has setlog
  off and its print volume was not checked, so it is not a same-volume comparison. UART-bridge
  ESP32 boards (Heltec V3, T-Beam) take 58 to 100 ms.
- **Hypothesis (not yet tested by a flags-off run):** the slowness is serial output blocking, not
  decode, dedup, relay or UDP work. R1 must confirm with a flags matrix on DK5EN-1.
- **Fact (received frames only):** in 2941 radio completions the closest two were 636 ms apart; the
  longest handler was 133 ms; no received frame was pending when a handler returned (slack minimum
  533 ms, which ignores time outside the metric: pre-handler prints, `captureDrain`, other loop
  work). **Code:** the radio is back in RX before OnRxDone starts on ESP32 (`startReceive`
  `esp32_main.cpp:4763`, `OnRxDone` `:4807`), and `startRadioReceive()` is the first action on
  nRF52. **Estimate, not measured:** losing a frame would need a second RxDone to overwrite the chip
  buffer before `readData`, i.e. a loop delay longer than one frame's airtime (about 600 ms or more
  per code comment) against a 133 ms maximum handler; this rests on unverified SX126x buffer
  behaviour in RX INF. No loss measurement exists: a frame that was never received is invisible in
  the log.
- **Fact:** `RX_IRQ_STALE` is a timeout-path event, not an OnRxDone event, and shows no association
  with slow handlers in this capture (no independence test was run).
- **Recommendation:** 50 ms measures the logging cost on UART boards. See (c).

## Capture facts used throughout

`~/meshlog/rpizero/dk5en-98/run-20261012-2310.log`, DK5EN-98 (Heltec V3, FW 4.40a build Oct 11 2026),
23:11 to 08:00.

- Flags during the capture: the logger turned `--txcapture on`, `--loradebug on`, `--setlog on` at
  23:11:02; `--info` before that showed `LORADEBUG off`,
  `TXCAPTURE off`, `NBRDEBUG on`, `DisplyLog on`. `--loradebug on` also sets `bDisplayInfo` and
  `bDisplayRetx` (`src/command_functions.cpp:2908-2910`) and saves them to settings. So the capture
  carries loradebug + displayinfo + setlog + nbrdebug output; the production state is (hypothesis:
  same as the `--info` header) setlog + nbrdebug only.
- 2941 `RX_FLAG_PROCESS` = 2652 received frames (`[LOG]` RX line, `ONRXDONE_TIME`, `RX_FRAME`)
  - 289 `CRC_ERROR`/`OnRxError` (9.8 %). 5175 `RX_TIMEOUT_FIRE`. `CHECKRX missed_edge`/`zero_length`
    and `RX_OTHER_ERROR`: 0.
- `ONRXDONE_TIME` (n = 2652): min 22, p5 44, p10 45, p25 48, p50 59, p75 99, p90 112, p99 123, max
  133 ms. 1857 over 50 ms (70.0 %), 625 over 100 ms. `ONRXDONE_STATS` warn sum 1888 = 1857 + 31 from
  the first window, which predates the logger.
- Summed handler time 188.6 s over about 31,700 s = 0.6 % of wall time.
- Pi-side timestamps are TCP arrival times and bunch several lines into 1 to 2 ms; they cannot time
  phases. All timing below uses device `millis()` fields (`RX_FLAG_PROCESS ts=`, `t=`) or
  `ONRXDONE_TIME`.

## (a) Per-phase map of OnRxDone

### Call context

| Platform | Caller and context                                                                                                                                                                                                                                                                                           |
| -------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| ESP32    | Arduino loop task: `receiveFlag` set by the DIO1 ISR -> `RX_FLAG_PROCESS` (`src/esp32/esp32_main.cpp:3101-3116`) -> `checkRX()` (`:4737` `getPacketLength`, `:4742` `readData`, `:4763` `startReceive`) -> `OnRxDone(payload,...)` (`:4807`). Payload is a stack copy; RX is restarted before OnRxDone runs. |
| nRF52    | `RadioEvents.RxDone = OnRxDone` (`src/nrf52/nrf52_main.cpp:1117`), runs in the 16 KB LORA task. `OnRxDone` copies the payload into a double buffer under `taskENTER_CRITICAL` and calls `startRadioReceive()` as the first action (`src/lora_functions.cpp:~836-880`, "FIX BUG #2 (nRF52)").                 |

The metric starts at `_onrxdone_start = millis()` (`src/lora_functions.cpp:819`) and ends at the main
exit (`:2583-2594`). It therefore **excludes** the work in `checkRX` before the call (RX_FLAG_PROCESS
print `:3103-3105`, `RX_RESTARTED` print `:4777`, six `printdeb` calls "Received packet" `:4783-4795`,
about 190 B) and the deferred raw dump `[MC-TEST] RX_FRAME` (about 260 B, printed later from
`captureDrain()` in the loop). **Estimate:** these add about 15 to 20 ms before and about 20 to 25
ms after each frame at 115200 baud, on top of the measured 59 ms median.

Early exits `handleACK` (`:939-960`) and HN report (`:1165`) print `ONRXDONE_TIME` (only under
`bLORADEBUG`) but do **not** update `onrxdone_max_ms`/`onrxdone_warn_count` (only the main exit,
`:2588-2592`, does). Consequence: `ONRXDONE_STATS`/`ONRXDONE_SLOW` can undercount against
`ONRXDONE_TIME` lines. In this capture both counts are 1857, so no early exit exceeded 50 ms.

### Phases (code reading = hypothesis unless marked)

Blocking work in the path, in execution order. "Print" means `printfdeb` -> `Serial.write` ->
`MeshSerialClass::write` (`src/net_console.cpp:289-300`): the UART write comes first and blocks
(UART with `_txBufferSize = 0` uses only the 128-byte FIFO, framework `HardwareSerial.cpp:549`
comment; at 115200 baud 1 byte = 86.8 us), then a best-effort non-blocking `send(MSG_DONTWAIT)` to the
2323 client under a try-lock (so the Pi copy is a lower bound of what the UART printed).

| #   | Phase                                                           | Code (lora_functions.cpp)                                                                                                                                                 | Blocking work                                                                                                                                                            | Plausibly slow? (hypothesis)                                                                                                 |
| --- | --------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ---------------------------------------------------------------------------------------------------------------------------- |
| 0   | entry, channel-util bookkeeping, nRF52 buffer swap + RX restart | `:819-900`                                                                                                                                                                | atomics, `memcpy`; nRF52: SPI `startRadioReceive`                                                                                                                        | No. Microseconds (ESP32); nRF52 restart is the intended blind-window minimiser.                                              |
| 1   | ACK handling                                                    | `:939` `handleACK`                                                                                                                                                        | ring scan                                                                                                                                                                | No. Ring is small (`MAX_RING`).                                                                                              |
| 2   | RX-slot release scan, decode                                    | `:962-1055` `decodeAPRS`                                                                                                                                                  | `memcmp` loops, parse; prints only under `bDisplayRetx`/loradebug (`ACK_RECEIVED`, `[RETX]`)                                                                             | Compute: No. Print: yes when it fires (own-echo frames only).                                                                |
| 3   | dedup                                                           | `:1067` `is_new_packet`; prints `RX_DEDUP_NEW/DUP` (`src/dedup_functions.cpp:36,49`)                                                                                      | ring scan (100 slots); one 40 to 70 B print under loradebug                                                                                                              | Compute: No. Print: ~5 ms (66 B median in capture).                                                                          |
| 4   | setlog RX line                                                  | `:1085-1096` `printBuffer_aprs("[LOG]", ...)`                                                                                                                             | format + one ~200 B line                                                                                                                                                 | **Yes: ~17 ms per frame** (198 B median) with `--setlog on`.                                                                 |
| 5   | HN report exit, stage 2 coverage (`bNBRRELAY`)                  | `:1115-1170`, `:1180-1340`                                                                                                                                                | NBR matrix updates, `nbrCopyMask`, ring scan; `nbrLog` prints only on CANCEL/REFUSE                                                                                      | Compute: probably No. Not separable from the data.                                                                           |
| 6   | NBR topology feed                                               | `:1346-1480` (`nbrNoteFrameGw :1404`, `nbrNoteDirect`, `topoUiChanged`)                                                                                                   | matrix update; `nbrLogToConsole` (`:621`) prints 3 to 6 `[NBR]` lines per frame when `--nbrdebug on`                                                                     | Compute: probably No. Print: **yes, ~14 ms** (165 B median, 22 % of in-handler bytes). Active in production (`NBRDEBUG on`). |
| 7   | raw ring + MH-LoRa print                                        | `:1485` `charBuffer_aprs`, `:1504` `printBuffer_aprs("MH-LoRa")` under `bDisplayInfo`                                                                                     | format, one ~160 B line (+ blank)                                                                                                                                        | Print: **yes, ~14 ms**, loradebug-only (21 % of bytes). `msgstorePresence` (`:1518`) is a short lock-scan.                   |
| 8   | own-msg / HEARD / HEY echo                                      | `:1560-1613`                                                                                                                                                              | `setOwnMsgStatus` (RAM), `addBLEOutBuffer` (ring), `setlogPrint` GWU, `addNodeData`                                                                                      | No, except the GWU print (48 B, ~4 ms).                                                                                      |
| 9   | dedup-new branch: extern, KISS, RX-LoRa2, ring registration     | `:1615-1690` `queueExtern :1646`, `queueKiss :1653`, `printBuffer_aprs("RX-LoRa2") :1659`, `addLoraRxBuffer :1683`                                                        | `queueExtern`/`queueKiss` only `memcpy` into a 2-slot queue (deferred send in the loop, `src/extudp_functions.cpp:846-860`); RX-LoRa2 print ~160 B                       | UDP/KISS: **No, deferred**. RX-LoRa2 print ~14 ms (text frames only, 1030 of 2652).                                          |
| 10  | text / position handling, display, BLE                          | `:1690-2210` `queueDisplayText :1752...`, `queueDisplayPosition :2205`, `addBLEOutBuffer :1758,1933,2208`, DM-ACK `addTxRingEntry`, `SendPong`                            | display is deferred: `queueDisplay*` copies into globals (`:190-215`), flush in loop; BLE and TX are ring pushes                                                         | No (deferred). DM paths print under `bDisplayInfo`.                                                                          |
| 11  | gateway upload                                                  | `:2272-2305` `setlogPrint GWU :2302`, `addNodeData :2305` -> `addUdpOutBuffer` (`src/udp_functions.cpp:1290`) -> `bf_push`; `[DATA]` print (`src/udp_functions.cpp:1449`) | **no UDP send in the handler**: ring push only, `sendMeshComUDP` drains it from the loop (CONC-16 comment). Prints: GWU 48 B + `[DATA]` ~80 B                            | UDP: **No**. Print ~11 ms.                                                                                                   |
| 12  | relay decision and TX-ring enqueue                              | `:2315-2510`, `RELAY_QUEUED` print `:2431`, `addTxRingEntry :2446`, RLY print `:2515-2522`                                                                                | `checkMesh`, hop/path rewrite, `encodeAPRS`, `addTxRingEntry` (lock + ring write), RELAY_QUEUED 54 B + RLY 57 B prints                                                   | Compute: **possibly ~11 ms** (see below, residual rises from 3 to 15 ms for relayed frames; cause untested). Print ~10 ms.   |
| 13  | exit: timing, `iReceiveTimeOutTime`, `csma_timeout`             | `:2583-2610`                                                                                                                                                              | `ONRXDONE_TIME` print is **after** the elapsed value is taken (`:2585` vs `:2587`); `OnRxDone` print                                                                     | No.                                                                                                                          |
| -   | flash / SD                                                      | none found in the handler body                                                                                                                                            | `save_settings`, SD and LittleFS calls are absent from `:819-2610` (grep). `ownMsgStatusSet` is RAM (`src/own_msg_status.cpp:50`). `msgstorePresence` is an in-RAM scan. | No.                                                                                                                          |

`txcapture` does not print from this path: the RX raw dump is gated by `bLORADEBUG`
(`src/lora_functions.cpp:~816`, `src/capture_functions.cpp:19`), `bTXCAPTURE` covers the TX side. The
brief's list of three flags therefore reduces to loradebug (+ displayinfo/retx), setlog and
nbrdebug for OnRxDone.

### Evidence from the capture for the phase split

Method: for each of the 2652 handlers, count the bytes of the lines between `RX_FLAG_PROCESS` and
`ONRXDONE_TIME` (script described in the evidence checklist). Both the in-handler lines and `ms` are
device-side quantities; the byte count is read from the net-console copy.

| Model / group                                                              | Result                                                                                                                            |
| -------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------- |
| `ms` vs all bytes from RX_FLAG_PROCESS to ONRXDONE_TIME                    | `ms = -7.0 + 0.0853 * bytes`, R2 = 0.985 (n = 2652). UART 115200 8N1 theory: 0.0868 ms/B.                                         |
| `ms` vs in-handler bytes only (excludes pre-handler lines)                 | `ms = -2.0 + 0.0980 * bytes`, R2 = 0.965; median 637 B per frame.                                                                 |
| Same slope inside each traffic class (dup / new / gwu / relay)             | 0.0836, 0.0859, 0.0862, 0.0872, 0.0770 ms/B, R2 0.85 to 0.97. Slope is class-independent even though class compute differs.       |
| Sum of (0.0868 x in-handler bytes) over sum of `ms`                        | 171.8 s of 188.6 s = 91 %.                                                                                                        |
| In-handler bytes by source                                                 | setlog RX 26.5 %, NBR 22.0 %, MH-LoRa 21.3 %, RX_DEDUP 8.8 %, RX-LoRa2 8.3 %, other (incl. `[DATA]`) 3.6 %, RLY 3.0 %, GWU 2.2 %. |
| Share of in-handler bytes that exist only because of loradebug/displayinfo | 46.3 %                                                                                                                            |
| Per class: median `ms` minus 0.0868 x bytes ("non-print residual")         | dup 4.0 ms; new, no gw, no relay 4.1; new + gw 2.6; new + relay 15.3; new + gw + relay 15.1.                                      |

Residual reading (Estimate): decode, dedup, NBR, display queueing and the gateway ring push together
cost about 3 to 5 ms; the relay path adds about 11 ms of unexplained time (hypothesis: relay
decision plus `addTxRingEntry`, or serial output the Pi did not receive because of the try-lock;
R1 timers 10 and 12 below decide it). Gateway UDP: `new + gw` (2.6 ms residual) shows no
measurable cost beyond the 11 ms of GWU and `[DATA]` printing.

Platform contrast with a similar print volume (see (b)): RAK4631 prints a median 599 B per frame
inside its handler and takes 5 ms (slope 0.0016 ms/B); the ESP32 UART gateway prints 637 B and takes
59 ms. That contrast is the strongest argument that the cost is the UART, not the shared code.

## (b) Does the time cost receptions?

### Code: where is the radio while OnRxDone runs

| Platform                           | RX restart point                                                                                                                                                                                                                                                           | Verdict                                                                                                                                                                                                                                  |
| ---------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| ESP32 (Fact)                       | `checkRX`: `readData` `:4742`, save RSSI/SNR `:4745-4750`, `startReceive(INF)` `:4763` ("Restart RX immediately after reading FIFO, BEFORE processing"), `setPacketReceivedAction` re-armed, DIO1 level check `:4770`, then `RX_RESTARTED` print, then `OnRxDone` `:4807`. | Radio is in RX during OnRxDone. Blind interval = IRQ -> loop notices flag -> `RX_FLAG_PROCESS` prints -> `readData` -> `startReceive`; it does not depend on handler duration. `RADIO_CTRL` boards add a `delay(2)` (`:4761`, per code). |
| nRF52 (Fact)                       | first action of `OnRxDone`: payload copy to `rxPayloadCopy[2]` under critical section, then `startRadioReceive()` (or deferred if `bSPI_ETH_Active`), CAD abort. Comment: "RX sofort neu starten um Blindfenster zu minimieren."                                           | Same: RX is back before processing. RX buffer switch exists only on nRF52 (`RACE-04`, `RX_BUF_OVERWRITE` guard, `LORA_ISR_DEBUG` only).                                                                                                  |
| Chip behaviour (not verified here) | Hypothesis from the SX126x datasheet: with timeout INF the chip stays in RX after RxDone and keeps the old packet in its data buffer until the next payload arrives. Not tested in this wave.                                                                              | Would make the loop have at least one preamble + header time (about 300 ms at the configured SF11/BW250/preamble 32, per the comment at `src/configuration_global.h:401-402`, unverified for this run) before the buffer is overwritten. |

### RX_IRQ_STALE: meaning and emitter

- **ESP32 (Fact):** `src/esp32/esp32_main.cpp:3028` (stale line) and `:3041` (forced-restart line)
  are in the **RX timeout block** (`iReceiveTimeOutTime` expired, `:2982-3050`), not in OnRxDone.
  Condition: IRQ register shows `PREAMBLE_DETECTED` or `HEADER_VALID`, RSSI >= -126 dBm, and the
  deferral budget is used up (header: 3, preamble-only: 1). Then the code logs `RX_IRQ_STALE` and does
  a plain `startReceive` restart. `RX_IRQ_STALE_EARLY` (`:3008`) fires only when RSSI < -126 dBm.
- **nRF52 (Fact):** only the "forced restart after N deferrals" text exists (`nrf52_main.cpp:1601`),
  keyed to `is_receiving && rx_irq_defer_count >= 3`. Different predicate. RAK captures: 0 lines.
- OnRxDone only enters through the timer: it re-arms `iReceiveTimeOutTime`/`csma_timeout` at exit
  (`:2601-2602`).
- The brief's "468 RX_IRQ_STALE" counts two lines per event: **234 events** (234 `RX_IRQ_STALE rssi`
  - 234 `forced restart`). `tools/loganalyse.sh` already reports 234 (anchored greps, line 1467-1469).

### Capture tests (dk5en-98 run-20261012-2310)

| Test                                                                                           | Count / result                                                                                                                                                                                                |
| ---------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Gap between consecutive radio completions (device `RX_FLAG_PROCESS ts`, rx + crc, n = 2940)    | min 636 ms, p1 1361, p5 2439, p50 10233. 0 gaps below 500 ms, 2 below 700, 12 below 1000.                                                                                                                     |
| Slack = next flag start minus (this start + handler `ms`)                                      | min 533 ms, p1 1223, p50 10241. No case where a flag was already pending when a handler returned (a pending flag would show slack of a few ms).                                                               |
| Longest handler vs shortest inter-frame gap                                                    | 133 ms vs 636 ms. LoRa frames at this site last about 0.6 s and more (code comment `esp32_main.cpp:4733`: 608 to 1394 ms for 48 to 133 B), so a second RxDone inside a handler window is physically excluded. |
| CRC error share of the event after a SLOW handler vs a FAST one                                | after SLOW: 179 of 1857 = 9.6 %; after FAST: 81 of 794 = 10.2 %; after >100 ms: 51 of 625 = 8.2 %. Overall 260 of 2651 = 9.8 %.                                                                               |
| Same, restricted to the next event within 2 s                                                  | after SLOW: 1 of 72; after FAST: 1 of 30 (too small to conclude).                                                                                                                                             |
| Hourly CRC share vs slow share (9 hours)                                                       | r = -0.71 (hour 07: CRC 13.8 %, slow share 61 %; other hours CRC 8.6 to 10.1 %, slow 67 to 75 %). Confounded by traffic mix, not evidence for a causal link in either direction.                              |
| `RX_TIMEOUT_FIRE` outcomes (n = 5175)                                                          | restart 3962, defer_pre 615, defer_hdr 361, STALE 234, defer_flag 3.                                                                                                                                          |
| STALE flags / RSSI                                                                             | all 234 `flags=PRE cnt=1`; RSSI -113 to -90, median -112 dBm (ambient noise level on this channel, above the -126 gate). `STALE_EARLY` = 0.                                                                   |
| Time from the last RxDone flag to a STALE                                                      | min 7090 ms, median 13647; 0 of 234 within 6 s. A STALE needs a second timeout cycle with a latched preamble flag, so it cannot follow a handler directly.                                                    |
| STALE probability per timeout fire, by the previous event (6 to 12 s / 12 s and more after it) | previous SLOW: 5.7 % (58/1015) / 10.6 % (70/661); previous FAST: 8.7 % (31/357) / 10.9 % (39/359); previous CRC: 15.2 % (20/132) / 14.0 % (16/114). No excess after SLOW.                                     |
| Did a STALE restart cut a frame? Next RxDone flag within 1 s of the fire                       | STALE: 0.4 %; plain restart: 1.9 %; defer_pre: 14.0 %; defer_hdr (a real reception): 94.2 %. STALE behaves like noise, not like an aborted reception.                                                         |

Interpretation (Fact where counted, Hypothesis for mechanism): in this capture handler duration shows
no relation to CRC errors, no overlap with the next reception and no relation to STALE. The only
reception loss path through OnRxDone that the data cannot rule out is loop latency between the DIO1
IRQ and `readData` (loop busy elsewhere), which does not depend on OnRxDone length when frames are at
least 636 ms apart. A loss caused by OnRxDone would be a frame that never appears in the log, so this
capture cannot count it directly; R1 needs a reference receiver (see R1 plan).

Not covered: TX. A relayed frame is enqueued at phase 12 and sent by the loop after the handler, so
OnRxDone adds at most the remaining handler time (tens of ms) to relay latency, against relay slot
waits of seconds (hypothesis, slot waits not measured here).

### Platform comparison (Fact; per file)

| Capture                                  | Board / output                                | n    | p10 | p50 | p90 | p99 | max | > 50 ms |
| ---------------------------------------- | --------------------------------------------- | ---- | --- | --- | --- | --- | --- | ------- |
| `dk5en-90/serial-20261001.log`           | RAK4631 nRF52, USB CDC, loradebug + nbrdebug  | 2409 | 4   | 5   | 7   | 9   | 24  | 0       |
| `dk5en-90/serial-20260929.log`           | RAK4631                                       | 9932 | 4   | 6   | 7   | 9   | 22  | 0       |
| `rpizero/dk5en-14/2026-10-02.log`        | T-Deck Plus (S3, `ARDUINO_USB_CDC_ON_BOOT=1`) | 1662 | 3   | 5   | 40  | 47  | 79  | 0.5 %   |
| `rpizero/dk5en-92/2026-10-02.log`        | T-Beam (UART bridge), setlog + nbrdebug       | 1525 | 44  | 61  | 89  | 99  | 105 | 72.7 %  |
| `rpizero/dk5en-1/2026-09-30.log`         | Heltec V3 (UART, CDC off), flags not checked  | 2618 | 58  | 100 | 102 | 113 | 132 | 91.3 %  |
| `rpizero/dk5en-98/run-20261012-2310.log` | Heltec V3 gateway, loradebug+setlog+nbrdebug  | 2652 | 45  | 59  | 112 | 123 | 133 | 70.0 %  |

RAK in-handler bytes: median 599, p90 815 per frame; slope of `ms` on bytes 0.0016 ms/B (script in
checklist). T-Deck `[LOG]` lines: 0 (setlog off), `[NBR]` 6441, `MH-LoRa` 1601. Earlier DK5EN-98
days show a rising median (2026-08-26 p50 25 ms, 2026-09-11 46, 2026-10-05 57); flags and builds of
those days were not checked (unverified), so no trend claim.

nRF52 differs from ESP32 in this path in three ways: OnRxDone is a radio callback in the LORA task,
RX restart is the first statement, and `printfdeb` is a bounded FIFO write that drops after 20 ms
(`src/printfdeb_functions.cpp:126-143`) instead of blocking at baud rate.

## (c) Threshold sanity: is `ONRXDONE_WARN_MS` = 50 right?

Facts: the median on UART-bridge ESP32 boards is 58 to 100 ms with debug output on, so 50 ms flags
70 to 91 % of frames there (the how-to documents "background, not an alarm"). On nRF52 the maximum
over 12,000 frames is 24 ms, so 50 ms is 5x p99 and would be a useful alarm. On a USB-CDC ESP32-S3
(T-Deck) 0.5 % exceed 50 ms. The counter is readable only with `--loradebug on` (how-to 6.13), which
itself adds 46 % of the in-handler bytes on this gateway, so the instrument perturbs the quantity it
reports (Estimate: removing loradebug lowers the gateway median from 59 to about 40 ms, setlog
alone to about 25 ms, no output to about 5 ms; model in (d)).

Recommendation (operator decision, not yet backed by a flags-off measurement):

1. Do not raise a single global number to turn the gateway green. A level above 133 ms would hide
   the nRF52 regressions the 50 ms level is good at catching.
2. Split the meaning. Keep 50 ms as the nRF52/USB-CDC sentinel. For UART-bridge ESP32 boards
   either subtract the time spent inside `printfdeb` from the sampled value (R1 builds exactly that
   accumulator) and keep 50 ms on the print-free value, or warn at a reception-relevant level.
3. Unsupported proposal pending R1: a "could cost a reception" level of about 200 to 250 ms was
   floated from the shortest observed inter-frame gap (636 ms) and a preamble of about 300 ms
   (config comment, unverified for this run). It does not follow from the evidence: the loss
   condition is the loop latency from RxDone to `readData` against the next frame's airtime, and
   OnRxDone is only one part of that latency. A threshold on OnRxDone alone cannot mean "could cost
   a reception". R1 should measure IRQ-to-`readData` latency as its own metric.
4. Per role (gateway vs node): not needed on this evidence. The gateway's extra work is a ring push;
   its extra time is GWU and `[DATA]` printing. Per output sink (UART vs USB CDC vs native) is the
   dimension that explains the numbers.
5. Make the number observable without loradebug: expose `onrxdone_max_ms`/`warn_count` (and the new
   print-excluded value) in the 5-min STAT line, so production reads it with setlog only.

## (d) Debug-output share

- **Flags active in the capture (Fact):** loradebug (+ displayinfo + retx via `--loradebug on`),
  setlog, nbrdebug. txcapture is on but does not print from OnRxDone (see (a)).
- **Share (Estimate, correlational):** 91 % of the summed handler time (171.8 s of 188.6 s) equals
  0.0868 ms/B times the bytes printed inside the handler. 46.3 % of those bytes exist only with
  loradebug/displayinfo (MH-LoRa, RX_DEDUP, RX-LoRa2, MESH, RELAY_QUEUED, `[DATA]`, `checkOwnTx`).
- **Prediction for other flag sets (Estimate; model `ms = 0.0868 x bytes + 5.5 ms`, 5.5 = median
  non-print residual; untested):**

| Flags                                             | p50     | p90     | p99     | share over 50 ms |
| ------------------------------------------------- | ------- | ------- | ------- | ---------------- |
| capture state (loradebug + setlog + nbrdebug)     | 59      | 112     | 123     | 70 % (measured)  |
| production-like: setlog + nbrdebug, loradebug off | 40      | 55      | 60      | 19 %             |
| setlog only                                       | 25      | 33      | 38      | 0 %              |
| no debug output                                   | about 5 | about 5 | about 5 | 0 %              |

The last row is flat because the model uses the median residual; the relay path has a higher residual
(about 15 ms), so real no-output p90 is likely around 15 to 20 ms. Every row except the first is a
prediction.

- **Caveats:** (1) bytes correlate with path complexity (relayed frames print more and compute more);
  the class-wise slopes stay at 0.085 ms/B, which argues against a pure complexity artefact but does
  not prove causation. (2) Pi byte counts are lower bounds (try-lock, `MSG_DONTWAIT`). (3) The
  `[MC-TEST] RX_FRAME` dump and the pre-handler prints are outside the metric but inside the loop.
- **Measurement to separate it (for R1):**
  1. Flags matrix on DK5EN-1, same TEST traffic replayed with `--injectraw`/`--injectmsg`
     (`injectraw` goes through the real OnRxDone, TM-06): A = loradebug+setlog+nbrdebug, B = setlog +
     nbrdebug, C = setlog only, D = all off, E = A on a USB-CDC build if one is available. Metric:
     per-frame total and print-excluded time (below), identical frames.
  2. Print-time accumulator: hook `MeshSerialClass::write` (`src/net_console.cpp:289`), not
     `printfdeb`: direct `Serial.printf`/`Serial.write` callers (the `[GW];rx` line in
     `udp_frame_esp32.cpp`, `printdeb`/`printlndeb`, `printBuffer_aprs`) may bypass `printfdeb`.
     Add `micros()` deltas into a counter while an "in OnRxDone" flag is set; report `total_us` and
     `print_us` per frame. Split UART (`:291`) from TCP (`:296`) there.
  3. Arm F, candidate fix: UART ESP32 boards never call `Serial.setTxBufferSize()` (only the S3
     HWCDC does: `esp32_main.cpp:990`, `net_console.cpp:247`), so every UART print blocks at baud
     rate. About 640 B per frame against more than 600 ms between frames would fit a 2 to 4 KB TX
     ring. Run flags on (arm A) with a UART TX buffer; it may remove the warnings without touching
     log content.
  4. Reference receiver for loss: a second node (flags off) hears the same TEST group frames from a
     third node at <= 2 dBm; compare received `msg_id` sets between DK5EN-1 (flags on) and the
     reference, per flag set.

## R1 timer placement (proposal)

All with `micros()`, behind `INSTRUMENT_ENABLED` (no space in the define), accumulated in a static
struct and emitted from the loop (like `captureDrain()`), never printed inside the handler. After
building, string-scan the image for the new markers.

`src/lora_functions.cpp` (line numbers at HEAD 7edfc23a):

- `:819` T0 (add micros next to the existing millis); `:939` before/after `handleACK`.
- `:962` start of ring-slot scan; `:1055` before/after `decodeAPRS`; `:1067` before/after
  `is_new_packet` (includes the print in `src/dedup_functions.cpp:36,49`).
- `:1085` and `:1096` around the `[LOG]` print.
- `:1115` entry of the HN branch (exit `:1165`); `:1180` and `:1346` around the stage-2 coverage
  block; `:1404` around `nbrNoteFrameGw` + `nbrNoteDirect` + `topoUiChanged` (`:1346-1480`).
- `:1485` and `:1510` around `charBuffer_aprs` + MH-LoRa print (`:1504`).
- `:1560` to `:1613` heard/own-msg block.
- `:1615` to `:1690`: separate stamps at `queueExtern :1646`, `queueKiss :1653`, RX-LoRa2 `:1659`,
  `addLoraRxBuffer :1683`.
- `:1690` to `:2210`: one stamp for the whole text/position handling (display queue, BLE).
- `:2272` to `:2305`: GWU print `:2302`, `addNodeData :2305`.
- `:2315` start of relay decision, `:2431` RELAY_QUEUED print, `:2446` around `addTxRingEntry`,
  `:2515` to `:2522` RLY print.
- `:2583` exit (existing measurement); add `print_us`, `total_us`, phase array, and a frame
  class (dup/new, gw, relay).

Outside the handler (to size the metric gap):

- `src/esp32/esp32_main.cpp:3101-3116` loop-side prints before `checkRX`; `:4742` `readData`,
  `:4763` `startReceive`, `:4777-4795` prints before `OnRxDone`; `src/capture_functions.cpp:218`
  `captureDrain` duration.
- `src/printfdeb_functions.cpp:151` (ESP32 `Serial.write`) and `:126-143` (nRF52 write loop) for
  the print accumulator; `src/net_console.cpp:291` (UART) and `:296` (TCP).

## R1 bench results (DK5EN-1 Heltec V3, 2026-10-13)

Setup: INSTRUMENT image from `2a1dcdf3`, DK5EN-1 as a node (not a gateway), 20 minutes per arm on live
mesh traffic, driver `tools/bench/onrx_matrix.py`, per-arm JSON in `tools/bench/runs/harness/onrx_*.json`.
Arm F is the same image plus `-DINSTR_UART_TXBUF=4096` (firmware confirmed `txbuf=4096/1`).

| Arm | Flags                         | n   | total p50 | total p99 | total max | over 50 ms | print share | non-print p50 |
| --- | ----------------------------- | --- | --------- | --------- | --------- | ---------- | ----------- | ------------- |
| A   | loradebug + setlog + nbrdebug | 40  | 77.9 ms   | 89.9 ms   | 90.2 ms   | 37         | 0.96        | 3.2 ms        |
| B   | setlog + nbrdebug             | 25  | 25.1 ms   | 36.3 ms   | 36.5 ms   | 0          | 0.90        | 2.5 ms        |
| C   | setlog only                   | 34  | 13.5 ms   | 24.6 ms   | 24.7 ms   | 0          | 0.86        | 1.9 ms        |
| D   | all three off                 | 43  | 1.5 ms    | 2.3 ms    | 2.4 ms    | 0          | 0           | 1.5 ms        |
| F   | as A, UART TX buffer 4096     | 56  | 4.4 ms    | 8.4 ms    | 9.0 ms    | 0          | 0.43        | 2.6 ms        |

- **Confirmed on the bench (Fact):** the handler time is serial print time. Non-print work is 1.5 to
  3.2 ms in every arm; each added debug stream adds roughly its byte count times 87 us/B (fit slope
  86 to 107 us/B, 115200 baud = 87 us/B).
- **Production-like cost (arm B):** the `[LOG]` RX line costs about 11 ms and the `nbrdebug` output
  about 12 ms per frame (phases `log`, `nbr`). With loradebug on (arm A) the dedup lookup print alone
  adds about 10.6 ms (phase `ded`) and the relay decision about 32 ms (phase `rly0`).
- **Fix candidate confirmed (Fact on this node):** a 4096 B UART TX ring (`Serial.setTxBufferSize()`
  before `begin`, arduino-esp32 2.0.17) takes arm A from p50 77.9 ms to 4.4 ms and the maximum from
  90.2 ms to 9.0 ms, with identical log content. Print time inside the handler drops from 74.6 ms to
  1.6 ms (the write only copies into the ring).
- **IRQ to `readData` latency:** p50 4.2 to 6.0 ms, p99 6.9 to 8.9 ms, max 9.2 ms in all five arms,
  independent of the arm. It is loop latency, not handler time of the previous frame (frames were
  sparse; the closest pair here was far apart). The 50 ms threshold has no relation to it.
- **Not shown:** reception loss. The arms never produced overlapping frames, so no loss test was
  possible; the code argument (radio back in RX before OnRxDone) and the 90 ms worst case against
  frame gaps of 600 ms or more stand.
- **Limits:** n is 25 to 56 per arm, each arm saw different traffic, the node is not a gateway (no UDP
  path in the measurement), and the model prediction of R0 (B about 40 ms, C about 25 ms, D about
  5 ms) over-estimated: measured 25, 13.5 and 1.5 ms on smaller frames. The gateway's relay path
  (R0 residual of about 11 ms) was not reproduced: `rly0` costs 0.3 ms without prints.
- **R2 considerations:** the ring is heap-allocated at `Serial.begin` (4096 B); size per board
  (classic ESP32 boards have 26 kB DRAM headroom, MEM-04); at 640 B per frame and 11.5 kB/s drain the
  ring refills in about 56 ms per frame, a burst of more than 6 frames within 0.4 s would block again;
  the S3 HWCDC boards already size their ring (`esp32_main.cpp:990`, `net_console.cpp:247`).

## R2 bench result (DK5EN-1, production ring)

Image: INSTRUMENT plus the production `CONSOLE_UART_TXBUF` default (2048 B on S3, `--info` shows
`CONSOLE_TXBUF 2048/1`), same flags as arm A, 48 frames, 20 minutes: total p50 4.4 ms, p90 6.3 ms, p99
7.1 ms, max 7.2 ms, none over 50 ms; print time p50 1.8 ms; non-print p50 2.5 ms; IRQ-to-`readData` p50
5.3 ms. Equal to the 4096 B experiment of arm F within noise, so 2048 B is enough on this node.

## R2 bench results, T-Beam v1.2 (DK5EN-92, classic ESP32, UART console) and the final DK5EN-1 build

Same flags as arm A (loradebug + setlog + nbrdebug), live traffic, 20 minutes per image, INSTRUMENT images,
free heap via `--heap` (internal heap, no PSRAM). Per-image JSON in `tools/bench/runs/harness/onrx_*.json`.

| Image                | n   | mean    | p50     | p95     | p99     | max      | over 50 ms | print share | free heap at start |
| -------------------- | --- | ------- | ------- | ------- | ------- | -------- | ---------- | ----------- | ------------------ |
| T-Beam, no ring      | 40  | 60.6 ms | 59.7 ms | 93.6 ms | 98.9 ms | 101.7 ms | 24 of 40   | 0.92        | 67,140 B           |
| T-Beam, 1024 B ring  | 50  | 17.5 ms | 18.0 ms | 48.4 ms | 54.7 ms | 58.4 ms  | 2 of 50    | 0.70        | 65,820 B           |
| T-Beam, 2048 B ring  | 42  | -       | 9.6 ms  | -       | 18.0 ms | 18.2 ms  | 0 of 42    | 0.38        | 64,740 B           |
| DK5EN-1, 2048 B ring | 69  | -       | 4.2 ms  | -       | 6.3 ms  | 7.5 ms   | 0 of 69    | 0.39        | -                  |

- **Sizing (Fact):** with all three debug streams on, a frame prints 769 B at the median and up to
  1159 B (T-Beam) on the bench and 1561 B on the gateway capture, so a 1024 B ring still blocks on the larger
  frames. 2048 B covers it. The ring costs about 2.4 kB of roughly 65 kB free internal heap on the T-Beam
  (minimum seen during the run 57.8 kB, 58.6 kB without ring).
- **Decision:** `CONSOLE_UART_TXBUF` defaults to 2048 B on every UART console board.
- **Non-print work is slower on the classic ESP32:** 5 to 7 ms p50 against 2.5 ms on the S3, so the
  T-Beam does not reach the 4 ms of the V3 even with a ring that fits.
- **STAT line on hardware (DK5EN-1, final build):** four 5-minute STAT lines showed `onrx_p95=5..7`,
  `onrx_warn=0`; summed `onrx_n=65` against 69 per-frame records (last partial window), STAT max 7 ms
  against 7.5 ms per frame (STAT counts whole milliseconds).

## Pre-registered predictions for the DK5EN-98 byte-model nights (written 2026-10-13, before the runs)

Method: for each of the 2652 handlers in `run-20261012-2310.log`, subtract 0.0868 ms per printed byte
of the streams that a night no longer prints (`[LOG]` = setlog, `[NBR]` = nbrdebug, everything else
between `RX_FLAG_PROCESS` and `ONRXDONE_TIME` = loradebug/displayinfo/retx). Script:
`tools/bench/onrx_predict98.py`. The observed night had all four streams on.

```text
events 2652 avg bytes/event {'setlog': 236, 'nbr': 164, 'other': 515}
observed (all four streams on)                 n=2652 mean= 71.1 p50= 59.0 p95=114.0 p99=123.0 max=133.0 >50=70.0%
PREDICT night1 loradebug only (drop setlog+nbr) n=2652 mean= 36.3 p50= 27.7 p95= 63.7 p99= 71.0 max= 89.6 >50=32.2%
PREDICT night2 loradebug+setlog (drop nbr)     n=2652 mean= 56.8 p50= 45.9 p95= 91.0 p99= 98.2 max=118.3 >50=42.3%
PREDICT reference loradebug off (drop other)   n=2652 mean= 26.4 p50= 26.7 p95= 43.1 p99= 45.9 max= 52.8 >50= 0.2%
```

- Night 1 on DK5EN-98: `loradebug` only (setlog and nbrdebug off). Night 2: `loradebug` + `setlog`
  (nbrdebug off). Both record `ONRXDONE_TIME` via meshlogger on rpizero; settings are restored by the
  same chain afterwards. The model is confirmed if the measured p50 and p95 land within 20 % of the
  predicted values; a clear miss means the byte model does not carry over to the gateway.
- The `loradebug off` reference row (p50 26.7 ms, p99 45.9 ms) cannot be measured on stock firmware
  (no timing line is printed) but agrees with DK5EN-1 arm B (setlog + nbrdebug, p50 25.1 ms).

## T-Deck Plus (DK5EN-14, ESP32-S3 native USB console), INSTRUMENT image, 2026-10-13

`t_deck_plus` sets `ARDUINO_USB_CDC_ON_BOOT=1` in the env and `ARDUINO_USB_MODE=1` in its board file
(`boards/T-Deck.json`), so it uses the 4096 B HWCDC ring (`net_console.cpp:248`) and not the UART ring.
(Every other CDC env in the repo gets `MODE=1` from its board file as well; an earlier claim that some of
them miss the 4096 B guard was wrong, it came from reading `platformio.ini` without the board files.)

| Arm                      | n   | p50    | p90     | p99     | max     | over 50 ms | print p50 | non-print p50 / p99 |
| ------------------------ | --- | ------ | ------- | ------- | ------- | ---------- | --------- | ------------------- |
| A (loradebug+setlog+nbr) | 50  | 6.1 ms | 11.6 ms | 19.6 ms | 21.3 ms | 0          | 2.4 ms    | 3.7 / 14.0 ms       |
| D (all three off)        | 35  | 1.9 ms | 10.5 ms | 19.5 ms | 20.2 ms | 0          | 0         | 1.9 / 15.7 ms       |

- **No print problem on the T-Deck (Fact):** the HWCDC ring absorbs the debug output (print p50 2.4 ms with all
  streams on). The 43 ms p95 in the 2026-10-02 capture does not reproduce on this build.
- **Residual tail (Fact for the location, Hypothesis for the cause):** with all debug output off the p99 stays at
  19.5 ms; the time sits in the `rly0` stamp (p99 17.3 ms in arm D), which also covers the text, position,
  display and BLE handling between the NBR feed and the relay decision. The T-Deck's UI work on some
  frames is the likely cause. All frames stay well under the 50 ms warn level.
- Free internal heap 77.0 kB at start (72.1 kB minimum), PSRAM 4.9 MB free. The node was restored to a plain
  build afterwards; its settings are unchanged.

## DK5EN-98 byte-model night 1 (production gateway, stock 10-11 firmware, loradebug only)

Night 1 of the pre-registered check ran from 17:09 to 19:50 (2.7 h, `~/meshlog/dk5en-98-s1-N1/2026-10-13.log`
on rpizero; `setlog` and `nbrdebug` off, `loradebug` on). Night 2 was cancelled when the gateway was
flashed over the air (see the brief, "R3 status"). Result against the predictions written down before the run:

| n    | mean    | p50     | p95     | p99     | max      | over 50 ms |
| ---- | ------- | ------- | ------- | ------- | -------- | ---------- |
| 1094 | 31.7 ms | 24.0 ms | 67.0 ms | 70.0 ms | 113.0 ms | 20.7 %     |
| pred | 36.3 ms | 27.7 ms | 63.7 ms | 71.0 ms | 89.6 ms  | 32.2 %     |

p50 is 13 % below and p95 5 % above the prediction, both inside the 20 % band set beforehand: the byte model
(0.0868 ms per printed byte) carries over from the DK5EN-1 bench to the production gateway. The share over
50 ms is lower than predicted (20.7 % against 32.2 %); the maximum is higher (113 ms against 89.6 ms).

## Open items and unverified

- Whether the relay path really costs about 11 ms of non-print time (Estimate from the residual
  only). Timers 12 above.
- UART blocking behaviour of the pinned framework build (`HardwareSerial` with
  `_txBufferSize = 0`) is read from comments and inferred from the slope; not run on hardware.
- Chip-level RX behaviour after RxDone (datasheet-level, not verified).
- Flags and builds of the other corpus days and of `dk5en-1` captures (not checked).
- LoRa parameters of this run (SF, BW, preamble) were not read from the capture; airtime figures
  come from code comments and the observed minimum gap.
- No loss measurement exists: the capture cannot show frames that were never received.

## Side findings (not fixed; outside the file set)

1. `RX_IRQ_STALE_EARLY` is dead at this site: the gate is RSSI < -126 dBm
   (`esp32_main.cpp:3004`) but the channel's ambient RSSI is -113 to -90 dBm, so only the
   budget-exhaustion branch ever fires. The latched `PREAMBLE_DETECTED` flag is cleared by the
   forced restart; if STALE is wanted as a signal, the gate level needs a site-relative reference.
2. `onrxdone_max_ms`/`onrxdone_warn_count` are not updated on the `handleACK` and HN early exits
   (`lora_functions.cpp:948-950`, `:1165`) although those exits print `ONRXDONE_TIME`.
3. The brief's `468 RX_IRQ_STALE` is a two-lines-per-event count; correct number 234.
4. `--loradebug on` silently also enables `bDisplayInfo` and `bDisplayRetx` and persists them with
   `save_settings()` (`command_functions.cpp:2905-2918`), so the logger's flag toggle changes flash
   settings and roughly doubles the per-frame output.
5. The Pi-side net-console copy is best-effort (`MSG_DONTWAIT`, try-lock, `net_console.cpp:289-300`):
   byte counts from captures are lower bounds of what the UART carried.

## Evidence checklist (commands)

```sh
L=~/meshlog/rpizero/dk5en-98/run-20261012-2310.log
grep -c ONRXDONE_TIME $L                         # 2652
grep -c ONRXDONE_SLOW $L                         # 1857
grep -c "RX_IRQ_STALE rssi" $L                   # 234 (468 = rssi + forced lines)
grep -c "RX_IRQ_STALE forced" $L                 # 234
grep -c RX_FLAG_PROCESS $L                       # 2941 = 2652 + 289 CRC
grep -c "OnRxError" $L                           # 289
grep -c "RX_TIMEOUT_FIRE" $L                     # 5175
grep "RX_IRQ_STALE rssi" $L | grep -c "flags=PRE cnt=1"   # 234
tools/loganalyse.sh $L | sed -n '/ONRXDONE_HEALTH/,/^$/p'  # samples 3152, warn sum 1888, slow 1857
grep -n "flags set\|--loradebug on\|--setlog on\|--txcapture on" $L | head
sed -n 2,12p ~/meshlog/dk5en-90/serial-20261001.log        # RAK flags sent by capture script
grep -c ONRXDONE_SLOW ~/meshlog/dk5en-90/serial-20261001.log   # 0
```

Per-event analysis (regression, gaps, STALE correlation, class table) was done with throwaway
Python 3 scripts in the session scratchpad
(`/private/tmp/claude-501/-Users-martinwerner-WebDev-MeshCom-Firmware-DEV-Main/e4db067a-65e4-4e36-a2ae-d16977511220/scratchpad/onrx/`:
`parse.py`, `an1.py` to `an7.py`); they are not part of the repo. Method in one line each: `parse.py`
walks the log, opens an event at each `RX_FLAG_PROCESS ts=` and closes it at `ONRXDONE_TIME`, summing
`len(line without the 25-char logger prefix) + 2` per line; `an2/an4` fit `ms` on bytes; `an5` takes
gaps of `ts`; `an6` classifies each `RX_TIMEOUT_FIRE` by outcome and by the last event before it.
If R1 wants these kept, they belong in `tools/` (a decision for the orchestrator).

Evidence rules: no commit is cited; no "already fixed" claim is made; platform claims are given per
platform (ESP32 UART, ESP32 USB-CDC, nRF52); unverified items are listed above.
