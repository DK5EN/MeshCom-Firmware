# RCA: DK5EN-14 (T-Deck Plus) drops off WiFi under chat load

Date: 2026-10-24. Node: DK5EN-14, T-Deck Plus, build `4.40a (Oct 24 2026 / 16:09:49)` (TXP W1c bench
build, not INSTRUMENT). Source: `rpizero.local:~/meshlog/dk5en-14-txp-w1c/2026-10-24.log` (meshlogger,
started 16:20:56, 24 h run, still running).

## Bottom line

- **Confirmed:** free internal heap on DK5EN-14 falls with every received chat message and does not come
  back while the node runs: 77.5 KB at 16:25, 28.8 KB at 22:20, 21.9 KB at 23:45. The console
  connection and the WiFi presence (mDNS) were lost twice, both times with free internal heap below
  30 KB.
- **Confirmed:** a reboot restores the heap (67.1 KB at `up=300` after the 23:49 reboot). The memory
  is held by runtime state, not by anything reloaded at boot.
- **Holder identified (code review 2026-10-25):** the T-Deck message store `persisted_msgs`
  (`src/t-deck/lv_obj_functions.cpp:195`). The first version of this report refuted it because it is
  "reloaded from `messages.json` at boot". That reload does not exist in the shipped build: the save and
  load paths sit inside `#ifdef T_DECK_SPIFFS`, the SD line writer inside `#ifdef HEAP_TEST`, and neither
  macro is defined anywhere in the tree (`platformio.ini` included; already noted in
  `docs/code-quality-2.0.md:211`). `persisted_msgs` is therefore a RAM-only second copy of every
  non-System message, capped at 1000 entries (about 200 KB of internal RAM, more than the node has), and
  released only by a reboot. Its per-message cost (three heap Strings plus a 96 B vector element) matches
  the measured ~220 B per message.
- **Not implicated:** the TXP W1c changes. The leak predates them.
- Correction to the first report: the node did **not** hang at 22:26. It stayed up (uptime continued)
  and came back by itself at 23:13. The operator reboot was at ~23:49, during a second loss.

## Impact

- DK5EN-14 not reachable by name (`dk5en-14.local`) or console for 47 min (22:26-23:13), and again from
  23:48 until the manual reboot.
- LoRa RX kept working during the outage (uptime and RX counters continued). Only the network side was
  lost.

## Timeline

| Time (CEST) | Event                                                                                                   | Evidence                                     |
| ----------- | ------------------------------------------------------------------------------------------------------- | -------------------------------------------- |
| 16:25       | First STAT: `heap=77532`, `up=600`                                                                      | `[LOG] STAT`                                 |
| 16:25-22:20 | Heap drifts down to 28.8 KB. Delivery stalls on the console of 12 s (20:20), 23 s (20:30), 47 s (21:20) | STAT series, node vs. logger timestamps      |
| 22:25:00    | Last line from the node, then 64 s silence on a busy channel                                            | log line 48943                               |
| 22:26:04    | Node closes the console TCP connection                                                                  | `[LOGGER] connection lost (peer closed ...)` |
| 22:26-23:12 | `dk5en-14.local` does not resolve (`[Errno -2] Name or service not known`), 47 retries                  | `[LOGGER]` lines 48945-48989                 |
| 23:13:08    | WiFi back without a reboot: `age_s;29;got_ip_n;2`, uptime `ms;25095113` continues. Heap back to 40.1 KB | `[WIFI];link;up` 23:13:37, STAT 23:15        |
| 23:15-23:45 | Busy group-20 chat (14-25 messages / 5 min). Heap 40.1 -> 21.9 KB in 30 min                             | STAT series                                  |
| 23:46-23:47 | Console delivery lags 30 s (lines stamped 23:46:41 arrive 23:47:10)                                     | node vs. logger timestamps                   |
| 23:48:20    | Node closes the console connection again; name does not resolve                                         | `[LOGGER]` lines 58389-58392                 |
| ~23:49      | Operator reboot (`ms;72075` at 23:50:13)                                                                | `[WIFI];link;up`                             |
| 23:54       | First STAT after the reboot: `heap=67084`, `up=300`                                                     | `[LOG] STAT`                                 |

## Evidence

### 1. The STAT `heap=` value is free internal RAM

`src/esp32/esp32_main.cpp:3482` passes `ESP.getFreeHeap()`, which in the arduino-esp32 core in use
(`framework-arduinoespressif32` 3.20017) is `heap_caps_get_free_size(MALLOC_CAP_INTERNAL)`
(`cores/esp32/Esp.cpp:140-143`). The 8 MB PSRAM is not in this number.

### 2. The loss tracks received messages

Counted per 5-minute STAT window: messages shown in the GUI (`[TDECK]...msg_focus_and_alert: Called`)
against the heap delta.

- 253 messages over the run, net heap change -55.6 KB, about 220 B per message on average.
- The 35 windows without a message add up to -3.3 KB net (most exactly 0 B, single swings up to
  +/-3 KB). The 40 windows with messages add up to -52 KB.
- The steepest fall coincides with the busiest chat (23:15-23:45, 115 messages, -18 KB).
- The signal is noisy: single windows move by -14 KB (18:20-18:25, 1 message) or +7 to +11 KB (17:05-17:10,
  18:55-19:00, 23:10-23:15). The +11 KB at 23:15 follows the network outage, i.e. network buffers were
  released when the connections died.

### 3. Low heap precedes both network losses

- Loss 1 at 28.8 KB, loss 2 at about 21-22 KB.
- The console delivery stalls (up to 47 s) appear only in the second half of the run, when the heap is
  below about 40 KB. Signal strength was flat (RSSI -69 to -75 throughout), so the stalls do not
  follow the radio link.
- Heap alone is not a sharp threshold: 21:55 had 30.0 KB without a loss. The metric that matters for
  WiFi/lwIP is the largest free internal (DMA-capable) block, which STAT does not report.

### 4. Reboot restores the heap

16:25 (`up=600`): 77.5 KB. 23:54 after the reboot (`up=300`): 67.1 KB. The ~10 KB gap is not a reload of
`messages.json` (there is none, see evidence 5); it is STAT timing (`up=300` vs. `up=600`) or other
boot-time state. The 55 KB lost at runtime is gone after the reboot.

### 5. The message path in the firmware (code review 2026-10-25)

Three layers hold a received message in `src/t-deck/lv_obj_functions.cpp`:

| Layer                          | Memory                                                                                             | Bound                                                               | Released                                                                                     |
| ------------------------------ | -------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------- | -------------------------------------------------------------------------------------------- |
| View `msg_list` (LVGL bubbles) | PSRAM widgets (`LV_MEM_CUSTOM_ALLOC ps_malloc`) + 3 event structs with internal Strings per bubble | 50 children, `msg_list_trim_view()` (`:2738`)                       | Trim past 50 (`LV_EVENT_DELETE` frees the structs), tab switch (`lv_obj_clean`), user delete |
| Model `msg_tab_entries`        | Internal: 4 Strings per `MsgBubble`, one vector per tab                                            | 50 per tab, `msg_tabs_trim_history()` (`:2754`); tab count uncapped | Trim past 50 per tab, user delete                                                            |
| Staging `persisted_msgs`       | Internal: 4 Strings + group String per entry, 96 B vector element                                  | 1000 entries (`PERSISTED_MSG_LIMIT`, `:197`)                        | Past 1000 entries, user delete of that bubble, reboot. Never time- or pressure-based         |

- Arduino `String` keeps up to 14 characters inline (`WString.h` SSO). Header (`CALL-SSID -> 20`),
  timestamp (`dd.mm.yy hh:mm`, 14 chars, borderline) and body of a normal chat line exceed that and land
  on the internal heap (every malloc under 4 KB is internal, evidence table). Roughly 150 to 250 B per
  message including allocator overhead.
- `save_persisted_messages()` (`:3521`) and `load_persisted_messages()` (`:3620`) are empty stubs in the
  build: their bodies are inside `#ifdef T_DECK_SPIFFS`. `log_message_to_sd()` (`:2836`) is inside
  `#ifdef HEAP_TEST`. Neither macro is defined. The GUI's "persist to flash / SD" switches therefore do
  nothing.
- The only live consumer of `persisted_msgs` is `tdeck_reset_msg_tabs()` (`:4696`), called from the
  "clear text area" action (`tdeck_main.cpp:1441`), which rebuilds the tabs from it. The model
  `msg_tab_entries` already holds the same last 50 messages per tab.
- The INSTRUMENT accessor `instrument_persisted_msg_count()` (`:218`) already exists and would show the
  count rising by one per received message.

## Verification against the firmware

| Claim                                                          | Verdict                  | Basis                                                                                                                                                                                                                                                                                                                          |
| -------------------------------------------------------------- | ------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| STAT heap is internal RAM only                                 | Confirmed                | `esp32_main.cpp:3482`, `Esp.cpp:140-143`                                                                                                                                                                                                                                                                                       |
| Small heap allocations (Arduino `String`) land in internal RAM | Confirmed                | SDK `qio_opi/include/sdkconfig.h`: `CONFIG_SPIRAM_USE_MALLOC 1`, `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL 4096`, applied by `esp32-hal-psram.c:93`. No `heap_caps_malloc_extmem_enable()` override in `src/`. Every malloc under 4 KB goes to internal RAM first.                                                                  |
| LVGL widgets (message bubbles) consume internal RAM            | Refuted                  | `config/lv_conf.h:69-84`: `LV_MEM_CUSTOM 1`, `LV_MEM_CUSTOM_ALLOC ps_malloc`. Widgets live in PSRAM.                                                                                                                                                                                                                           |
| The message view grows without bound                           | Refuted                  | `msg_list_trim_view()` (`lv_obj_functions.cpp:2738`) caps the view at `MSG_TAB_MAX_MESSAGES` = 50 (TD-03 fix).                                                                                                                                                                                                                 |
| Per-tab bubble history grows without bound                     | Refuted                  | `msg_tabs_trim_history()` (`:2754`) caps each tab at 50 bubbles.                                                                                                                                                                                                                                                               |
| `persisted_msgs` is the holder                                 | Confirmed by code review | Grows one entry per non-System message (`:2989`), cap 1000 (`:2990`) is never reached before the heap is exhausted. The boot reload at `:3716` is dead code (`#ifdef T_DECK_SPIFFS`, macro undefined), so a reboot empties it, which matches evidence 4. Field confirmation via `instrument_persisted_msg_count()` still open. |
| `messages.json` is written and reloaded at boot                | Refuted                  | `save_persisted_messages()` and `load_persisted_messages()` bodies are inside `#ifdef T_DECK_SPIFFS`; the macro is defined nowhere. Both are empty stubs in every build.                                                                                                                                                       |
| Per-message SD logging contributes                             | Refuted                  | `log_message_to_sd()` body is inside `#ifdef HEAP_TEST`, undefined. Compiled out.                                                                                                                                                                                                                                              |
| Event-data structs of the bubbles leak                         | Refuted                  | `HeaderEventData`, `ReplyEventData`, `DeleteEventData` are deleted in their `LV_EVENT_DELETE` handlers (`:3312-3402`), bounded by the 50-bubble view.                                                                                                                                                                          |
| Number of tabs grows (one per new group / DM partner)          | Not excluded             | `msg_tabs_get_or_create_entry()` (`:2794`) has no limit. Small, and rebuilt at boot from `persisted_msgs`, so it is unlikely to explain the drift.                                                                                                                                                                             |
| WiFi/lwIP buffers can move to PSRAM                            | Partly                   | `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP 1`, but the 8 static RX buffers and DMA descriptors stay internal, and `CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC 1` keeps TLS internal (see AU-13 TLS finding).                                                                                                                                 |
| The node stayed up during loss 1                               | Confirmed                | uptime `ms` continues across 22:26-23:13; `got_ip_n` 1 -> 2 (one re-association).                                                                                                                                                                                                                                              |

## Root cause (current state)

**Mechanism, confirmed:** something allocated per received chat message stays in internal RAM for the
lifetime of the boot. Under sustained chat traffic (about 250 messages in 7.5 h, and 4-5 per minute at
peak) the free internal heap falls below about 30 KB. At that point the network stack can no longer keep
the console TCP stream and the WiFi/mDNS presence alive: delivery stalls, the console connection is
closed, and the node disappears from the network for up to 47 minutes while LoRa keeps running.

**Allocating site (code review 2026-10-25): `persisted_msgs`.** `msg_tabs_add_message()` pushes a full
copy of every non-System message into `persisted_msgs` (`:2989`). The container was designed as the
staging buffer for `/messages.json` and sized for that (1000 entries). The flash path never compiled
(`T_DECK_SPIFFS` undefined), so the container is a RAM-only duplicate of the model with no reload at boot
and a cap that lies above the whole internal heap. Per message it holds three to four heap-backed
Strings plus a 96 B vector element, about 150 to 250 B, which matches the measured ~220 B.

Second-order contributors, not the drift:

1. Fragmentation from per-message `String` churn (bubble copy into the tab, `persisted_msgs` copy,
   three event-data structs per visible bubble). Lowers the largest free block, which is what lwIP
   needs.
2. Network-side growth while the console TCP client is slow (pbufs queued for a 200 B/s console
   stream), which matches the +11 KB recovery when the connection died.

Ruled out: `log_message_to_sd()` (compiled out), the LVGL view and the per-tab model (both capped at
50), the TXP W1c code (the leak predates it).

**Fix options**

- **Preferred:** compile `persisted_msgs` and its push only under `T_DECK_SPIFFS`, and let
  `tdeck_reset_msg_tabs()` rebuild the view from `msg_tab_entries`, which already holds the last 50
  messages per tab. Removes the duplicate with no visible change.
- **Minimal:** keep the container but cap it at the model's order (50 total, or 50 per tab) so the worst
  case is a known few KB.
- Either way, add the largest free internal block to STAT (next steps, item 4).

## Contributing factors

- The T-Deck Plus runs with the smallest internal heap margin of the fleet (LVGL, display, audio, SD,
  WiFi, BLE). The same thin margin already broke the auto-update TLS check (AU-13).
- The meshlogger run itself keeps `--loradebug` and `--txcapture` on, which adds console TX volume
  (~200 B/s) and network buffer pressure. It does not create the drift (heap is flat in windows without
  messages, despite the same debug volume), but it lowers the margin.
- WiFi signal is marginal at the bench position (RSSI -69 to -75).
- Recovery took 47 min: the WiFi watchdog in `checkWifiPing()` (`udp_functions.cpp`) only acts while
  the station is not `WL_CONNECTED`. If the link stays associated but the IP stack is starved, nothing
  forces a restart.

## Gaps in this analysis

- The node's own log during the outages was not captured (the console was the capture path). Whether
  the station disassociated at 22:26 or stayed associated is unknown.
- STAT reports total free internal heap, not the largest free block or the per-capability split
  (DMA, PSRAM). Fragmentation cannot be separated from growth with this data.
- One run, one node, one build. The holder is identified by code review and by the matching per-message
  cost, not yet by a field counter (`instrument_persisted_msg_count()`).

## Next steps

1. **Confirm in the field:** run DK5EN-14 with an INSTRUMENT build and log, per STAT window,
   `heap_caps_get_free_size` and `heap_caps_get_largest_free_block` for INTERNAL and DMA, the minimum
   free heap, and `[INSTR-GUI]` (`msg_list_children`, `active_tab_bubbles`, `persisted_msgs`). Expected:
   `persisted_msgs` rises one per message while the other two stay at or below 50.
2. **Fix:** implement the preferred option (see "Fix options") with a host regression test that pushes
   N messages and asserts the container stays bounded; A/B the heap slope on the bench before and after.
3. **Reproduce on the bench:** inject a fixed message stream (group `TEST`, own nodes only) at
   4-5 messages per minute and watch the heap slope, instead of waiting for field chat.
4. **Add an early-warning line:** include the largest free internal block in STAT so the next field log
   shows fragmentation directly.
5. **Watchdog gap:** consider a restart path when WiFi is associated but no IP traffic has succeeded for
   N minutes, so a starved node does not stay away for 47 minutes.
