# MeshCom Stability Changelog

Release: `v4.35p.08.22-stability` (2026-08-22), based on official MeshCom 4.35p
(upstream `dev`, merge-base `8114d7ae`).

> **`v4.35p.08.21-stability` has been withdrawn and deleted.** It put any node
> with GPS enabled into a permanent boot loop (item 81 below). If you installed
> it, update — and note that an affected node can only be recovered over USB.

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
- **It is not a feature release.** A node running this build behaves like
  official 4.35p on the air and toward the apps. The changes are about
  stability, robustness, input hardening, and the test infrastructure to keep
  it that way; the only additions are a few small maintenance and diagnostic
  aids (a bootloader-entry command, a boot-time reset-reason log, developer
  tooling) — nothing that changes meshing, messaging, or the user experience.
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
