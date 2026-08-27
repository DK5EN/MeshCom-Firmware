# MeshCom Stability Changelog

Release: `v4.35p.08.27.2-stability` (2026-08-27), based on official MeshCom
4.35p, upstream `dev` at `fc83554e` — the state **after** upstream merged this
fork's changes.

**Items 1-103 below are now in official MeshCom.** The ICSSW maintainers merged
[PR #1102](https://github.com/icssw-org/MeshCom-Firmware/pull/1102) (82 changes)
and [PR #1103](https://github.com/icssw-org/MeshCom-Firmware/pull/1103) into
upstream `dev` on 27 August 2026. This document is kept as the record of what
was done and why; it is no longer a list of differences from the official
firmware.

> **`v4.35p.08.21-stability` has been withdrawn and deleted.** It put any node
> with GPS enabled into a permanent boot loop (item 81 below). If you still have
> it installed, update — and note that an affected node can only be recovered
> over USB.

## About this release

MeshCom is a wonderful project, and this release exists because we use it every
day and want it to be as dependable as the idea deserves. Everything below is
the result of a long, careful quality pass over the 4.35p codebase: reading the
code, testing on real hardware (Heltec V3, T-Beam v1.2 and WisBlock RAK4631 on
the bench, soak tests over hours), and improving what we found — always in the
smallest way that works.

Three things this release is **not**:

- **It is not the official firmware.** The official MeshCom firmware is
  maintained by the ICSSW team at
  [icssw-org/MeshCom-Firmware](https://github.com/icssw-org/MeshCom-Firmware),
  and that is where the project lives and grows.
- **It is not a feature release.** A node running this build interoperates with
  official 4.35p on the air and toward the apps. The changes are about
  stability, robustness, input hardening, and the test infrastructure to keep
  it that way; the additions are maintenance and diagnostic aids (a
  bootloader-entry command, a boot-time reset-reason log, a raw-frame capture
  switch, developer tooling). Three fixes do change what a node puts on the air
  or how it reports itself, and every one of them is a correction rather than a
  feature — they are listed under **What changes on the air** below, so that
  nobody meets them by surprise.
- **It is not a fork in spirit.** The changes are deliberately small and
  surgical, and they are offered back upstream as individual pull requests, as
  many of our earlier improvements already have been. This release simply
  makes the whole collection available in one place for field testing while
  that process runs its course.

The items below reference the engineering logs in this repository:
[docs/architecture/08-defect-catalogue.md](architecture/08-defect-catalogue.md)
(IDs like N-xx, SEC-xx, CONC-xx) and
[docs/code-audit-fixes-20260627.md](code-audit-fixes-20260627.md) (IDs like
A1, B2, C3), where the findings are documented with evidence. Every fix is one
focused commit in this repository's history.

## What changes on the air

Almost everything in this changelog is invisible from outside the node. Three
items in this release are not, and they are listed here so that nobody has to
discover them by surprise:

- **`/B=000` is now transmitted** when the battery is measured and empty (item
  90). Previously nothing was sent below one percent, which made "empty" and
  "no battery fitted" indistinguishable. A missing `/B=` tag now means the node
  has no battery hardware to report.
- **Implausible frames in the ACK path are no longer relayed** (item 91).
  Measured against 8741 field frames, this drops 5.7% of what reached that
  path — not one of which acknowledged a message the node had actually heard.
  They were previously re-transmitted into the mesh at priority 1.
- **ESP32 channel-utilisation figures drop sharply** (item 94), because they
  were computed from a fixed 255-byte length. Nothing about the radio changed;
  the number is simply correct now. Expect roughly 7% where the same node used
  to report 18%.

## New in v4.35p.08.27.2-stability

A rebase-and-repackage release. The base moved from merge-base `8114d7ae` to
upstream `dev` at `fc83554e`, so this build also carries everything upstream
added in between: SD-card offline map tiles for the T-Deck Plus, the T-Echo
BME280 fix, the extended Mheard JSON (per-hop RSSI/SNR in the HEY link chain,
originating callsign, gateway identifier), the build date in the Info JSON, and
the new BLE TYPE `I` field `FWDATE`. Two items are ours:

102. **`FWDATE` was truncated by one character.** Upstream's new BLE TYPE `I`
     field is built with `snprintf(cfwdate, sizeof(cfwdate), "%s %s", __DATE__,
__TIME__)` into a `char cfwdate[20]`. `__DATE__` is 11 characters
     (`Mmm dd yyyy`), the separator 1, `__TIME__` 8, the terminator 1 — 21
     bytes. `snprintf` truncated silently, so the last digit of the seconds was
     lost from every reported build timestamp. The buffer is now 24 bytes. Found
     by `-Wformat-truncation`, which the default warning level does not enable;
     returned upstream as PR #1103 and merged.

103. **A probe flag was declared outside the block that uses it.** In
     `batt_functions.cpp` the one-shot guard `battProbeDone` for the ADC_CTRL
     polarity probe (item 89) sat outside `#if defined(ADC_CTRL_PIN)`, while
     every use of it sits inside. On boards without that pin — E22-DevKitC,
     t_deck, t_deck_plus — it compiled as an unused variable. Declaration moved
     into the block. No behaviour change on any board.

## New in v4.35p.08.27-stability

Six defects fixed, one new diagnostic, and a test layer that replays real field
traces through the shipping code instead of through a re-implementation of it.

88. **Updating a node no longer erases its configuration.** The reset condition
    was `node_fversion != FLASH_VERSION`, and `FLASH_VERSION` is a date that is
    raised for every release. Every update therefore discarded the stored
    callsign, WiFi credentials, and sensor and network settings of every node —
    even when the layout of `struct s_meshcom_settings` had not changed at all.
    That is exactly what happened on the 20260724 → 20260821 step: the commit
    that raised the number touched neither flash header. Build identity and
    layout generation are now two separate things. `FLASH_VERSION` remains the
    release stamp shown in `--info`; `FLASH_STRUCT_VERSION` names the settings
    layout and is the only value `clear_flash()` looks at. It stands at
    20260724, the last real layout change. Nodes that stored 20260821 under the
    old semantics are grandfathered in and are not reset. Verified by updating
    three nodes — Heltec V3 and T-Beam over OTA, RAK4631 over DFU — each
    keeping its callsign and network configuration. `--flash-reset` still
    resets, as it should.
89. **The battery divider polarity is measured at boot instead of guessed at
    compile time.** The switch for the battery voltage divider was chosen by
    `#if defined(BOARD_HELTEC_V31) || defined(BOARD_WIRELESS_PAPER)` — and
    `BOARD_HELTEC_V31` is defined **nowhere** in the tree, neither in a
    variant's `configuration.h` nor in any build flag. The active-LOW branch
    was therefore unreachable in every image ever built, and every Heltec V3
    was driven active-HIGH regardless of whether the board is really a 3.0 or a
    3.1. The polarity is now probed once at boot. The signature is unambiguous
    and was measured on the device: enable=HIGH gives 902–906 counts,
    enable=LOW gives 1–4, and the threshold sits at 50. On a board that was
    already correct the change is a no-op, confirmed on a Heltec V3 with a real
    battery. The compile-time value stays as the seed and the fallback, so
    boards where the probe cannot run behave exactly as before.
90. **An empty battery is now distinguishable from no battery, on the air.**
    `mv_to_percent()` clamps to zero below `BAT_MIN_VOLTAGE`, and the `/B=` tag
    was only written `if(global_proz > 0)` — so a nearly empty pack reported
    nothing at all, and the battery graph went blank precisely when it
    mattered. A survey across 1230 stations found real nodes that report
    battery only in a six-hour window per day, purely because the pack crosses
    the 3.3 V line. The two `/B=` producers in `PositionToAPRS()` also
    disagreed with each other: the INA226 branch wrote `/B=%i` unchecked, the
    normal branch `/B=%03d` above zero. Both now write the tag whenever battery
    hardware is present. A transmitted `/B=000` means "measured, and the pack is
    empty"; a missing tag means "this node has no battery to report". Decoding
    is unaffected — the receiver reads the field with `sscanf("%d")`, so all
    three spellings yield the same integer. Fixed in the same pass: the PMU
    failure path on ESP32 zeroed both values without marking them unmeasurable,
    which would have made a T-Beam with a dead PMU claim an empty battery
    forever.
91. **Text fragments were being accepted as ACKs and flooded into the mesh.**
    `handleACK()` checked only `payload[0] == 0x41` and a minimum length. 0x41
    is the ASCII letter `A`, so any fragment of a text or position packet
    beginning with it ran through ACK processing: bytes 1–4 were taken as its
    message id, it entered the dedup ring, it was queued for transmission at
    priority 1 — evicting a heartbeat from a full queue — and it was relayed.
    Byte 5 is the reliable discriminator: radio ACKs are generated only as
    `0x80 | max_hop` and are only ever decremented on the relay path. Measured
    over 32.7 hours and 8741 frames on three field nodes, three independent
    criteria separate the populations identically, and **not one** of the 506
    implausible frames acknowledged a message the node had actually heard. The
    check lives as a pure function in `ack_functions.h` so it is testable
    without hardware, and a rejected frame is logged as `ACK_REJECT` rather
    than dropped silently, so the filter stays measurable in the field.
92. **`{SET}` range-checks max_hop.** `sscanf` wrote both values straight into
    the settings. A typo such as `{SET}44;2;` put 44 into the hop field of every
    packet the node sent — and the relay path only checks `(byte5 & 0x7F) > 0`
    before decrementing, so nothing bounded it from above. Values outside
    0…`MAX_HOP_LIMIT` now leave the previous setting standing. The old leniency
    is intact: each field is still applied as soon as `sscanf` has read it, so
    `{SET}4;` still sets only `max_hop_text`.
93. **New: raw frame capture at runtime (`--txcapture`).** Until now the log
    showed frames only **decoded** — the output of our own parser. A frame the
    decoder reads wrongly appears wrongly in the log, and nothing in it reveals
    what was actually on the channel. `captureFrame()` now copies raw frames
    into a 768-byte ring and `captureDrain()` prints them from the loop.
    Receive follows `--loradebug`; transmit has its own new switch,
    `--txcapture on/off`, which is persisted. The ring is not incidental:
    dumping directly from the radio callback needs about 900 B of stack — the
    nRF52 timer task has 1 KB — and would put roughly 48 ms of serial time into
    the RX path, or, on the transmit side, sit between the CAD "channel free"
    decision and `startTransmit()` and invalidate the very measurement the send
    timing rests on. Decoupled through the ring, capture costs one `memcpy`.
    Dropped frames are reported as `[MC-TEST] CAPTURE_DROPPED n= serial_bytes=`,
    because a capture goes patchy exactly when the channel is busy — the
    situation you switched it on for. Cost on RAK4631: +1400 B RAM,
    +1616 B flash.
94. **ESP32 channel utilisation was overstated by a factor of 1.8 to 4.1
    (N-29).** Found by the new capture on its first run on real hardware.
    `checkRX()` passed `UDP_TX_BUF_SIZE` (255) to `radio.readData()` as the
    length — but RadioLib takes that by value and never writes the real length
    back; it is an upper bound only, and the SX126x header says plainly that
    `getPacketLength()` must be called first. Every receive therefore reported
    255 bytes, with uninitialised stack content behind the actual frame, and
    `checkRX()` booked `getTimeOnAir(255)` into the channel-load statistic. The
    log showed exactly `rx=2476ms` after every single receive — the airtime of
    a 255-byte packet at SF11/BW250/CR4:6. The real frames ran 608 ms (48 B) to
    1394 ms (133 B). The counter-check sits in the same log line: `tx=701ms`
    matches the 60-byte transmit frame to the byte, because the transmit path
    knows its own length. A reported `util=18%` was really about 7%. Every
    ESP32 utilisation figure this firmware has ever printed was too high, and
    none of them were ever comparable with nRF52 figures, where the radio
    callback supplies the length. The length is now read from the chip before
    `readData()`, capped at `UDP_TX_BUF_SIZE`, and a zero-length read is no
    longer treated as a frame.
95. **`%%` was doubled in every log line containing a percent sign (N-30).**
    The `%%` branch wrote two percent signs and then fell through into the
    general copy, which appended the same one again; the next pass appended the
    second one once more. `util=18%%` and `BATT 100 %%` in the field were this.
    It broke `loganalyse.sh` and `logharvest.py` along with the logs. The
    rewrite logic now lives in `src/printfdeb_format.h` so it can be tested
    without Arduino — the same separation as `isPlausibleAckFrame()` — and a
    leading `;` no longer reads one byte before the buffer.
96. **`--info` printed passwords in clear text to the open network console
    (N-31).** `node_passwd`, `node_webpwd` and `node_pwd` were printed unmasked.
    That output goes through `printfdeb()` and therefore also over TCP port
    2323, which requires no authentication at all unless `node_passwd` is set.
    Reproduced end to end: `nc <node> 2323`, then `--info`, and the WiFi PSK is
    on screen — and in every log capture anyone shares. A set password now
    prints as `***`; empty stays empty, so it remains readable _whether_ one is
    set. The settings JSON sent to the app is untouched, since it needs the
    real value to display and change it. This does not replace `--passwd`; it
    takes the most rewarding find away from an open port.
97. **The shipping code is now replayed against real field traces.**
    `tools/traceharvest.py` harvests the decision sequence of running nodes from
    their debug output — 48 node-hours across four field stations — and feeds it
    to the actual functions rather than a model of them: `is_new_packet()` and
    `addLoraRxBuffer()` against 5647 verdicts and 6869 slot assignments,
    `getMessagePriority()` against 505 classifications covering all five
    classes, and `isPlausibleAckFrame()` against 30 ACKs the field nodes
    actually honoured. Zero deviations in all three. The ACK suite answers the
    question a retrofitted filter always raises — does it cut into healthy
    traffic? — by replaying frames whose _effect_ is logged, each of which
    closed a waiting ring slot. It does not cut. All three suites are
    mutation-checked, so it is demonstrated that they test anything at all.
98. **The dedup ring stays at 100 — measured, not assumed.** An analysis report
    recommended raising it to 300. The counter-check over the same 48 node-hours
    counted every message id that was evicted and then re-flooded: 112
    returners, of which exactly **one** was a genuine duplicate. 96 were id
    reuse — a different message carrying the same id, clustering at 180–210
    minutes apart. A larger ring buys that one frame; from 500 upwards it starts
    discarding legitimate messages (52 of them at 500, 96 at 1000). The
    diagnosis in the report was right — the window is about 38 minutes against a
    longest observed packet lifetime of 36.7 minutes, so there is no margin —
    but almost nothing falls through it, and 1 kB of RAM for one event in 48
    node-hours is not a trade. The measurement is written down where the
    constant is defined, and the replay test makes any change to the number fail
    loudly.
99. **An interop oracle and a fuzz corpus, both built from real traffic.**
    `tools/logharvest.py` harvested 8965 distinct CRC-rejected frames, 2981 ACK
    frames and 42110 re-encode vectors out of 2.83 million log lines from
    production nodes. `test_aprs_reencode` rebuilds frames from the logged
    fields and compares length and byte sum against the values computed by the
    **sender** — not circular, because the decoder reads that checksum out of
    the frame and rejects the frame if it does not match the wire bytes.
    `encodeAPRS()` reproduces the byte sum of 2422 distinct real frames without
    a single exception, across eleven hardware ids and two firmware
    generations. The one length deviation comes from a sender that omits the
    0x7E end marker; it is documented behaviour and frozen as the expected
    number. `test_aprs_fuzz` puts 500 genuinely corrupted frames through the
    decoder under AddressSanitizer and UndefinedBehaviorSanitizer, plus a fill
    differential that catches any read past the end of a frame.
100.  **`tools/meshlogger.py` records a node's network console to disk** for days
      at a time, so a rare event can be caught without sitting at a terminal. The
      console serves one client at a time, so the tool honours a `PAUSE` file and
      hands the port over on request.
101.  **`tools/loganalyse.sh` was itself audited and fixed (TOOL-01…06).** It
      counted CSMA backoff as a state-machine error, mis-attributed drop reasons
      to the wrong bucket, took the hop count from the wrong field, choked on
      stray bytes in its input, and could not read raw firmware logs without hand
      editing. It now reads them directly, and a regression suite covers the
      three counting bugs. A verdict from a broken instrument is worth nothing;
      several conclusions in this release rest on this tool.

## New in v4.35p.08.22-stability

This release exists to correct a defect we introduced ourselves in 08.21, plus
two sensor fixes and a build fix found while testing it. Everything below was
verified on real hardware for this release, not carried over.

81. **The GPS boot loop is fixed (N-25).** Arming the ESP32 task watchdog at
    the very start of `esp32setup()` — our own change in 08.21 — exposed a
    ~16-second block that upstream has always had but never watched: the GPS
    baud-rate scan, which runs from the main loop and never feeds the
    watchdog. Any node with `--gps on` aborted two or three baud steps in and
    rebooted, forever, because the setting is persisted before the crash.
    There was no command window, so the node could not be reached over the
    air, by BLE, or over the network console — only a USB reflash recovered
    it. Reproduced on a Heltec V3 and confirmed fixed there, with the crash
    backtrace decoded against its own ELF. Four parts: the watchdog is armed
    at the **end** of setup rather than the start (which also removes the
    USB-CDC boot wait, the display timeouts and eleven `while(true);` error
    paths from the watched region); the GPS init path feeds the watchdog
    behind a single helper that compiles to nothing on nRF52; the scan stops
    at the first NMEA sentence with a **valid checksum** instead of running
    all eight baud rates; and the baud table is ordered by likelihood.
82. **The baud scan no longer invents a baud rate (B-15).** It used to pick
    whichever rate produced the most characters, with no minimum — and the GPS
    RX pin is configured as an input with no pull-up. A single noise byte in
    the matched character set could therefore "detect" a rate on a board with
    no module attached, after which the probe ran against nothing. Detection
    now requires a complete, checksum-valid sentence; if none arrives, the
    node says so. Verified on a second Heltec V3 with no GPS fitted.
83. **GPS detection is dramatically faster.** Stopping at the first valid
    sentence, and trying 38400 before 9600 because `SetupUBLOX()` switches
    modules to 38400 permanently on first init, took detection from
    **12 000 ms to 321 ms** on a Heltec V3 and as low as 24 ms on a T-Beam.
84. **The dead second baud-detection implementation was removed (A-1…A-4).**
    `gps_functions.cpp` carried an unreachable interrupt-based variant, which
    is how the two implementations had silently drifted apart — it contained
    two `return -1` statements from a function returning `unsigned long`
    (wrapping to 4294967295, a value the caller's `> 0` test accepts) and a
    regression of our own. The reason it was unreachable turned out to be a
    single unconditional `#define` overriding a per-variant option in 15
    board configurations. Flash size is byte-identical before and after,
    which is the proof that only dead code went.
85. **The BME680 driver no longer mistakes an I2C acknowledgement for a chip
    ID (N-27).** BME280, BMP280 and BME680 share addresses 0x76 and 0x77, and
    only `begin()` reads the chip ID — but its return value was discarded and
    the sensor was marked present based on the address alone. A board with a
    BME280 reported `BME680: on (found)` and then logged a read failure on
    every cycle, forever. Verified both ways: on a board with a BME280 at
    0x76, and on one with nothing at either address.
86. **`--bmx off` now turns off the BME680 as its help text always
    promised (N-28).** It cleared BMP280, BME280 and BMP390 but never the
    BME680, so following the help and then enabling a BMx280 produced
    "can't be used together" and left the node with no sensor at all.
87. **Flashing targets the board you asked for.** The per-board upload
    commands carried no `--port`, so `esptool` chose one itself; with two
    boards attached, `pio run --target upload` could flash or disturb the
    wrong one. `--upload-port` had no effect because PlatformIO only forwards
    it through `$UPLOAD_PORT`, which was absent. All 27 commands now pass it.

## Input handling — RF, BLE, and network paths

A mesh node parses input from the air, from Bluetooth, and from the local
network. These changes make sure malformed or unexpected input in any of those
paths is length-checked and handled safely at every stage:

1. Received payload bytes are no longer interpreted as a printf format string
   in the debug logger (SEC-02).
2. The BLE WiFi-configuration message (0x55) now validates its length fields
   before copying SSID and password (SEC-03).
3. The URL-decode loop for messages can no longer write past its 200-byte
   buffer (SEC-04).
4. UDP receive: an off-by-one write at the buffer end was corrected (SEC-05).
5. UDP receive: the zero-byte scan can no longer read past the received length
   (SEC-06, BUG-12).
6. The BLE text-message handler (0xA0) rejects length underflows that produced
   oversized messages (BUG-07).
7. `handleACK` verifies a minimum frame length before its 12-byte copy
   (BUG-10).
8. APRS trailer and FCS fields are only read after checking they are actually
   inside the received frame (BUG-13).
9. The CONF configuration handler no longer zero-fills up to 251 bytes past a
   stack buffer (N-03).
10. A length underflow in the RF receive path could turn one short frame into
    an oversized `memcpy`; the whole chain is now length-checked (N-04,
    BUG-08), including the zero-length edge case in the phone-forwarding path.
11. An mheard path-update routine could read past a heap buffer; it is now
    bounds-checked (N-05).
12. A web-interface parameter was used as an unbounded array index (N-06).
13. The UDP transmit decode used the total frame length instead of the APRS
    payload length, over-reading its conversion buffer at two call sites.
14. Debug logs now mask passwords, and a legacy plaintext-comparison fallback
    in the authentication path was removed.
15. Oversized APRS frames are rejected at a single maximum-frame check before
    any parsing begins.
16. The `--symid` command validates the APRS symbol table character again.

## Crash and freeze fixes

These were the "node stopped responding" and "node rebooted" class of
findings, each reproduced (where hardware-dependent) on a real device before
and after the fix:

17. A stack overflow in the loop task on the message path could crash nRF52
    gateways; the buffer now lives off-stack (N-22).
18. `sendExtern()` allocated two 500-byte JSON buffers on a stack that is only
    4 kB on nRF52; they are static there now (ESP32 keeps its stack
    allocation), ending crashes on position sends.
19. On gateway-configured nodes without an Ethernet link, the W5100S UDP send
    and receive paths could stall the main loop for seconds to minutes — long
    enough to look like a dead node. Those paths now check the link state
    first, and a soak test with fault injection (link loss) verified the loop
    keeps running; further hardening of the remaining wait loops is tracked in
    the catalogue (N-20).
20. Enabling `--extudp` without an initialized network left the node frozen
    on every boot, because the setting persists across reflashes. EXTUDP now
    starts only once the node actually has an IP address (N-23).
21. `startNetwork()` could block longer than the task watchdog's first feed
    interval, producing a boot loop on gateway nodes (N-17).
22. Per-log-line heap allocations could starve the BLE stack while a phone
    connection was being established; logging on that hot path now avoids
    them, and connections are reliable again (N-18).
23. A 10-byte buffer was too small for the `{mcp}` message reformatting and
    corrupted adjacent memory (BUG-11).
24. If radio initialization fails at boot, the node now logs the cause and
    reboots instead of halting silently — a stuck node becomes a
    self-recovering one (A2).
25. The nRF52 `--dfu` command no longer hangs on its way into the UF2
    bootloader; it uses the GPREGRET register as the SoftDevice documentation
    intends (N-19).
26. A priority eviction in the TX ring could orphan an occupied slot,
    permanently losing one of the transmit slots (N-24) — found by the new
    TX-ring test suite, not in the field.
27. Clearing a TX-ring slot now clears the whole slot, closing a gap where
    stale bytes could survive into the next use.

## Concurrency correctness

The firmware runs radio, Bluetooth, and the main loop on more than one task on
nRF52. This group makes shared data crossings explicit and safe — and, just as
deliberately, removes synchronization where analysis showed none is needed:

28. The external-UDP queue is now a correct single-producer/single-consumer
    ring with the right memory ordering.
29. Phone commands received over BLE are queued to the main loop instead of
    executing inside the BLE callback (CONC-14).
30. The BLE outbound ring is written under a short lock, and the sender takes
    a consistent snapshot instead of re-reading a live slot (CONC-15,
    CONC-18).
31. The UDP outbound ring received the same treatment on both writer and
    reader side, verified live on a RAK4631 gateway with Ethernet (CONC-16).
32. BLE-received settings are staged in a private buffer and applied once per
    loop pass, instead of being copied live into the active configuration
    (CONC-17).
33. The network-console mutex could be re-created while held; ownership is now
    respected (CONC-19).
34. Enqueueing into the nRF52 TX ring is now a single operation under one
    lock — all sixteen call sites go through it, so two senders can no longer
    interleave in the same slot (N-14).
35. `Radio.Send()` is no longer called inside a FreeRTOS critical section
    (N-16).
36. The LoRa scan and CAD completion flags shared between ISR and loop are
    atomic (B2, B3); where ring buffers genuinely cross tasks, the locking
    work above covers them.
37. Where analysis proved single-task access on ESP32, unnecessary atomics and
    locks were removed — fourteen candidates plus the ring indices (N-13).

## Timing robustness

38. `millis()` comparisons throughout the codebase were converted to the
    subtraction idiom that survives the 49-day counter wraparound — in the
    ESP32 main loop, the nRF52 main loop, the LoRa/ring/GPS paths, and every
    deadline check found (N-08, A1).
39. A GPS ISR off-by-one was corrected (B5).
40. The ESP32 task watchdog is enabled and fed properly (C3); together with
    the `startNetwork()` fix above, a future hang recovers by reboot instead
    of by power cycle.
41. `AT_PRINTF` output is assembled with bounded `snprintf` (D3).

## Performance and resource use

42. NimBLE is configured server-only, saving 792 bytes of DRAM and 7.9 kB of
    flash on ESP32.
43. `read_batt()` no longer stops the main loop for ~100 ms every half second
    (BATT-01).
44. Debug output on nRF52 no longer blocks when the USB serial FIFO is full —
    a bounded wait, then the line is dropped, and the node keeps meshing
    (part of the N-20 hardening).
45. Interrupt-context LoRa logging is compiled out unless explicitly enabled
    (B1), and ring-overflow events are logged at the right severity (C2).

## Correctness and consistency

Smaller findings where two copies of the same logic had drifted apart:

46. The two HDOP variables are merged; display and web interface previously
    read different values (SIMP-30).
47. The I2C bus-reset guard is defined centrally; one board variant was
    missing it at exactly the two call sites that address the sensor
    (DRY-25).
48. "Node is unconfigured" is defined once and used by all three images
    (ALT-34).
49. Two byte-identical conditional branches were merged (ALT-33), and the
    display-refresh flag was separated from the button flag it had been
    overloaded with (ALT-35).
50. The nRF52 ACK handling now sets the same acknowledgement level for own
    messages as the ESP32 code path — the two copies had drifted (DRY-21).
51. The nRF52 serial command parser received the NUL-byte protection and
    self-healing check its ESP32 twin already had (DRY-22).
52. Gateways now deliver the HEY signal-quality report to the server over UDP
    as well, so coverage reporting works the same on both transport paths.
53. Board identity macros test for definedness instead of doing arithmetic on
    product names, which silently matched the wrong boards (N-10).
54. The nRF52 ADC and battery guards key on the actual platform instead of a
    "not RAK4630" exclusion, unblocking the other nRF52 boards.
55. The node's IP address is logged in dotted-decimal form instead of as a
    raw integer.
56. Duplicate source files and an editor artifact were removed from the
    compiled tree (SIMP-29).

## Build system

57. Continuous integration builds all board environments on every push and
    pull request — a change that breaks any board is caught before it lands
    (TEST-38).
58. `-Wall -Wextra -Werror` (with `-Wformat=2`) is enforced for the project's
    own sources on all 23 mainline ESP32 environments and on the RAK4631; the
    warnings this surfaced were fixed.
59. The nRF52 platform package is pinned to a known-good version, so builds
    are reproducible.
60. Three identically-named `[nrf52]` configuration sections silently
    overrode each other across board variants; each variant now has its own
    explicit base section (CFG-01).
61. The safeboot image builds reliably after mainline builds — the two
    platforms no longer fight over a shared framework directory — and the CI
    cache keys were tightened so stale artifacts cannot leak between jobs.
62. `--flash-reset` actually resets the persisted configuration now, and a
    size check protects against silent settings-layout drift (N-12; the
    catalogue tracks the remaining part of this finding — unifying the two
    settings layouts — as open, deliberately left for an upstream-coordinated
    change).

## Test infrastructure

The largest single investment of this effort: an automated safety net for the
firmware's core logic, so improvements — ours and anyone else's — stay
verified:

63. A native (host-side) test environment with the Unity framework runs the
    firmware's parsing code on a development machine, no hardware needed
    (TEST-37), and runs in the same CI gate as the builds.
64. A frame-capture hook records real LoRa frames from the air into a test
    corpus (oracle stage 1).
65. A differential runner replays that corpus of real captured frames through
    the native decoder and flags any change in behavior (oracle stage 2).
66. Specification-derived test vectors from the wire-format reference document
    check the decoder against what the format says, not just against what the
    code did yesterday (oracle stage 3).
67. The TX-ring core was made natively testable and covered with eleven unit
    tests — which promptly found N-24 above.
68. A mock MeshCom server provides a test double for the node↔server protocol
    (port 1990), so gateway logic can be exercised without a live server.
69. A resource watcher tracks RAM and flash usage per board against a baseline
    and reports the delta in CI, so size regressions are visible in every
    pull request.
70. A soak-test harness ran the RAK4631 gateway for hours under fault
    injection (Ethernet link loss) with a heartbeat watch — the N-20 fix is
    verified by endurance, not just by review.
71. The test suite itself was audited: eight weaknesses in the tests were
    fixed and the regression fence hardened, with three follow-up points
    closed in a second pass.

## Diagnostics and tooling

The developer toolbox in `tools/` that grew alongside this work:

72. `--dfu` reboots an nRF52 node into the UF2 bootloader on command — nodes
    in enclosures can be flashed without reaching the reset button.
73. The nRF52 logs its reset reason (RESETREAS) at boot, so an unexpected
    reboot tells you why.
74. `tools/loganalyse.sh` grew into a full log-analysis suite — 35 sections
    covering heap trends, CRC forensics, CSMA timing, and BLE-to-LoRa
    latency.
75. `tools/serial_monitor.py` watches a node's debug stream, tracks the LoRa
    state machine, and raises alerts with periodic summaries.
76. `tools/webflash.py` flashes a node over WiFi through the built-in OTA
    mechanism, end to end, with MD5 verification.
77. `tools/hmac_connect.py` connects to the network debug console with
    challenge-response authentication.

## Documentation

78. A complete architecture documentation set was written for this codebase —
    system overview, build matrix, dependencies, concurrency map, buffer
    inventory, and test strategy (`docs/architecture/`).
79. A byte-level wire-format reference documents the LoRa frame, the server
    UDP protocol, the EXTUDP JSON sideband, and the BLE phone protocol as
    actually implemented, with code anchors and captured example frames
    (doc 11).
80. Every finding above is written up in the engineering logs with evidence,
    status, and verification notes — including the claims we investigated and
    **refuted**, so nobody re-chases them (doc 08).

## Thank you

To the MeshCom maintainers and the ICSSW team: this project is a gift to the
amateur radio community, and all of this work happened because we think it is
worth polishing. We hope every one of these changes finds its way home.
