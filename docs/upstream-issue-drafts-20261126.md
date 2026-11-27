# Upstream issue drafts (2026-11-26): library patches and FORMAL-01 fixes

Issues for icssw-org/MeshCom-Firmware, English, assigned to DK5EN, one per library or topic (operator 2026-11-26).
Wording stays neutral: hardening and backports, no attack description. Filed on 2026-11-26 as #1330 to #1340; the numbers are in the
headings. The net console (F2-10 and FORMAL-NETCON-MARK) followed the same day as #1339, worded as announced hardening work
(operator 2026-11-26).

Later the same day the "Planned PR" line of #1330 to #1339 got the target "around 2026-12-11" (wave 6 of the release
plan, only after the bench checks); #1309 got a status line and the same target; #1311 a status comment. The texts
below are the first versions.

Sources: [campaign-formal-01-20261123.md](campaign-formal-01-20261123.md),
[review/formal01-20261123/README.md](review/formal01-20261123/README.md), `specs/tla/RESULTS.md`,
`specs/cbmc/RESULTS-quick.md`.

---

## #1330 (draft 1): NimBLE-Arduino 2.2.3

**Title:** NimBLE-Arduino 2.2.3: backport upstream fixes, harden the BLE write and restart paths

**Body:**

- **What:** the ESP32 firmware pins NimBLE-Arduino 2.2.3. Later releases carry fixes that apply to our peripheral
  path, and a review of the 2.2.3 sources found further robustness gaps in the write path and in stopping and
  restarting the BLE stack (BLE pause during Auto Update).
- **Planned change:** extend the existing pre-script `tools/nimble_patch.py` (it already applies AU-19 and AU-20):
  - NB-01 to NB-03: backports of NimBLE-Arduino upstream commits 2b2a56c8 (zero-length write, with a NULL check that
    upstream lacks), 7d0a3202 (failed realloc in `NimBLEAttValue::append()`), 83d43378 (ACL-full retry delay).
  - NB-04 to NB-07: deinit after a failed stop, unwind of a failed init with a bounded sync wait, host task handover,
    stop counter reset.
  - Firmware side: characteristic maximum length 300 bytes, onWrite buffer static instead of on the host task stack,
    prepare-write queue limited (`MAX_PREP_ENTRIES=2`, `QUEUED_WRITE_TMO=5000`; long writes need MTU >= 155).
- **Status:** in the fork since 2026-11-23, patch script with regression tests (`tools/tests/test_nimble_patch.py`),
  all ESP32 envs build. Not yet on the bench.
- **Planned PR:** library patch wave, after a bench check: phone long write 300 bytes, BLE pause and resume around
  an Auto Update.

---

## #1331 (draft 2): arduino-esp32 2.0.17

**Title:** arduino-esp32 2.0.17: HTTPClient, WiFiUDP/WiFiClient and hostByName fixes by build pre-script

**Body:**

- **What:** three spots in the arduino-esp32 2.0.17 core that the firmware uses:
  - AE-01 `HTTPClient`: header lines are read without a length cap or deadline. AE-01 v2 also fixes the Auto Update
    download that failed with connection error -7 on long header lines (fork ref AU-28).
  - AE-02 `WiFiUDP` / `WiFiClient`: an allocation failure ends in an abort instead of an error return.
  - AE-03 `WiFi.hostByName()`: after a timeout the DNS callback can still write to the caller's stack.
- **Planned change:** a new pre-script `tools/arduino_esp32_patch.py`, same pattern as `tools/nimble_patch.py`:
  patches the installed framework in place, idempotent, refuses an unknown file form.
- **Status:** in the fork since 2026-11-23, regression tests in `tools/tests/test_arduino_esp32_patch.py`. Auto
  Update downloads with AE-01 v2 ran on the bench (Heltec V3, T-Beam, T-Deck Plus, 2026-11-24/25).
- **Planned PR:** library patch wave.

---

## #1332 (draft 3): ESP-IDF 4.4 mDNS, DHCP server and lwIP

**Title:** ESP32 (IDF 4.4 prebuilt): mDNS and DHCP server backports, floor for the DHCP MTU option

**Body:**

- **What:** arduino-esp32 2.0.17 links prebuilt IDF 4.4.7 components. A review found:
  - mDNS: name decompression without a depth limit (fixed in later IDF, GHSA-7fpr-vjw5-2c86) and missing length
    checks in the answer parser. mDNS is on by default.
  - DHCP server (softAP setup mode): an allocation is not checked; leases are not reused when the pool is full.
  - lwIP DHCP client: option 26 (interface MTU) is taken without a lower bound.
- **Planned change:**
  - `src/esp32/idf_override/`: patched copies of `mdns.c` and `dhcpserver.c` from IDF v4.4.7, built in place of the
    archive objects (IDF 4 app builds only; safeboot, nRF52 and native builds never see them). Every change is
    marked `MESHCOM-PATCH` and listed in the README of that folder.
  - DHCP MTU floor of 576 through a link-time wrap (`src/esp32/lwip_mtu_guard.*`, `tools/lwip_wrap.py`).
- **Status:** in the fork since 2026-11-23, host tests for the MTU guard. Not yet on the bench.
- **Planned PR:** library patch wave, after a bench check of softAP setup and mDNS.

---

## #1333 (draft 4): DNS lookups outside the lwIP thread

**Title:** ESP32: DNS lookups from the loop run outside the lwIP tcpip thread

**Body:**

- **What:** `dns_gethostbyname()` is called from the main loop (UDP server link, EXTUDP partner, APRS-IS) and from
  the Auto Update task. With lwIP core locking off, the lwIP API must run in the tcpip thread.
- **Planned change:** `src/esp32/mc_dns.*` runs the lookup in the tcpip thread (`tcpip_callback` plus a semaphore);
  the callers switch to it.
- **Status:** in the fork since 2026-11-23. Not yet on the bench.
- **Planned PR:** library patch wave.

---

## #1334 (draft 5): TLS of the Auto Update

**Title:** Auto Update TLS: check the certificate validity period, minimum TLS 1.2

**Body:**

- **What:** the TLS client of the Auto Update (merged with #1328) relies on the prebuilt mbedTLS for the
  certificate dates, which is built without time support, and accepts the library's protocol defaults.
- **Planned change:**
  - A verify callback checks the validity period of the server chain (`src/fw_tls_time.h`). "Now" is the node
    clock when it is valid and not older than the build date, otherwise the build date; "not yet valid" is only
    checked with a usable clock. A node without a clock can therefore still update.
  - Minimum protocol version TLS 1.2; cipher suites unchanged.
- **Status:** in the fork since 2026-11-23, host tests (`test/test_fw_tls_time`). Auto Update downloads against
  GitHub ran on the bench with it.
- **Planned PR:** library patch wave, together with the Auto Update follow-ups (#1309).

---

## #1335 (draft 6): RAK13800-W5100S

**Title:** RAK4631 Ethernet (RAK13800-W5100S): socket index guards from RAKWireless upstream

**Body:**

- **What:** RAKWireless fixed missing socket index checks in their W5100S library (commit ebea549: 20
  `s >= MAX_SOCK_NUM` guards in `socket.cpp`, `EthernetUdp.cpp`, `EthernetClient.cpp`). Our pinned version does not
  have them.
- **Planned change:** ETH-07 in the existing pre-script `tools/w5100s_patch.py` (next to ETH-06 from #1287).
  RAK4631 flash +128 bytes.
- **Status:** in the fork since 2026-11-23, regression tests in `tools/tests/test_w5100s_patch.py`. Not yet on the
  bench.
- **Planned PR:** library patch wave, after a RAK4631 Ethernet check.

---

## #1336 (draft 7): Safeboot web server library

**Title:** Safeboot: move to ESP32Async/ESPAsyncWebServer 3.12.1 and AsyncTCP 3.6.1

**Body:**

- **What:** the Safeboot pins `mathieucarbou/ESPAsyncWebServer` 3.3.23 and `mathieucarbou/AsyncTCP` 3.2.14. Later
  releases fix the multipart upload that Safeboot uses for the firmware upload (GHSA-4phx-fcj6-46r4, fixed 3.11.1;
  GHSA-8m8p-vhxc-jmjw, fixed 3.11.2) and a use-after-free in `AsyncTCP` abort (fixed 3.5.0). The libraries moved to
  the ESP32Async organisation.
- **Planned change:** Safeboot envs on ESP32Async/ESPAsyncWebServer 3.12.1 and ESP32Async/AsyncTCP 3.6.1, no code
  change. The Safeboot image gets smaller (classic ESP32 690,208 of 720,896 bytes).
- **Status:** in the fork since 2026-11-23, builds. Not yet on the bench.
- **Planned PR:** library patch wave, with the stricter Safeboot checks of #1309, after a bench check: upload of a
  real image on classic ESP32 and S3, abort mid-upload and a second upload, rejection of a bad image.

---

## #1337 (draft 8): UDP output and EXTUDP queue under concurrency

**Title:** Gateway UDP output and EXTUDP queue: frame sent twice, lost or torn under concurrency

**Body:**

- **What:**
  - UDP drain (ESP32 and nRF52): the reader checks the frame generation and pops in two separate lock sections. An
    eviction in between makes it send a frame twice or drop a frame that was never sent.
  - EXTUDP queue (`queueExtern`): the writer copies into a slot before checking that it is free; a frame can be torn
    or duplicated, and a full queue drops without counting.
- **Planned change:**
  - Drain: `bf_peek_gen()` and `bf_pop_if()` (both already in `byte_fifo`), one lock section each.
  - EXTUDP: `src/ext_queue_core.h`, the writer checks the slot first; drops are counted (`externQueueDrops()`).
- **Evidence:** TLA+ models `bytefifo.tla` and `extqueue.tla` (fork `specs/tla/`, run 2026-11-26): the old reader
  and writer violate the no-duplicate, no-loss and no-torn-frame properties, the new ones hold them. Host tests
  `native_byte_fifo`, `native_ext_queue`.
- **Status:** in the fork since 2026-11-23. Not yet on the bench under load.
- **Planned PR:** after a bench check of the UDP drain and the EXTUDP queue under load.

---

## #1338 (draft 9): APRS encoder bound and update asset URLs

**Title:** APRS encoder copy bound; reject control bytes in Auto Update asset URLs

**Body:**

- **What:**
  - `encodeStartAPRS()` caps the copy at `UDP_TX_BUF_SIZE - 1` but writes it behind the 6-byte header, so the cap
    does not match the room left in the buffer.
  - The asset scan of the Auto Update release list accepts a NUL or control byte inside a JSON string, so the URL
    that is used can differ from the one that was checked.
- **Planned change:** copy capped at 249 bytes; a NUL or control byte in a JSON string fails the asset scan.
- **Evidence:** bounded model checking (ESBMC) of both functions, run 2026-11-26 (fork `specs/cbmc/aprs/`,
  `specs/cbmc/fw_assets/`): the fixed code passes, the old form fails. Host tests `native_aprs`, `native_aprs_fuzz`,
  `native_fw_update_assets`.
- **Status:** in the fork since 2026-11-23.
- **Planned PR:** library patch wave or the next small fix PR.

---

## #1339 (draft 10): Net console handshake

Operator 2026-11-26: file now as announced hardening work, although the handshake mark race (FORMAL-NETCON-MARK) is
not fixed yet. The text names the work, not the weakness.

**Title:** Net console (port 2323): harden the login handshake and the session handover

**Body:**

- **What:** the HMAC net console (`src/net_console.cpp`, ESP32) has one handshake slot and one session. A review
  with a TLA+ model of the handshake and session bookkeeping found places where it can be made more robust:
  - a peer that connects and stays silent holds the handshake slot until the 10 s handshake deadline;
  - the bookkeeping between a handshake task that is replaced and the next one (activity mark, socket ownership,
    session install) needs tighter rules.
- **Planned change:**
  - A new connection may take the handshake slot from a holder that has been silent for 3 s (generation counter,
    `src/net_console_slot.h`). In the fork since 2026-11-23.
  - Only the current slot holder updates the handshake bookkeeping; a replaced task leaves it alone. Planned.
  - Regression tests in the native suite `test_net_console_gate`.
- **Evidence:** TLA+ model `netconsole.tla` (fork `specs/tla/`, run 2026-11-26), with mutations that each break one
  property.
- **Planned PR:** after both changes are in the fork and a bench check with several console connections.

---

## #1340 (draft 11): `{CET}` restamp

Filed 2026-11-26 after the operator's answers to the three questions of
[concept-cet-restamp-01-20261116.md](concept-cet-restamp-01-20261116.md).

**Title:** {CET} time beacons: nodes with NTP or GPS restamp the beacon right before transmit

**Body:**

- **What:** `{CET}` time beacons carry the server's time (UTC) and age on their way: server leg, gateway queue, every relay queue. A node without NTP or GPS sets its clock from the beacon at arrival and runs behind by that age; bench 2026-11-15: 0.8 to 6.9 s.
- **Planned change:** a node whose clock comes from NTP or GPS with a fix rewrites the 19 time characters in its own TX copy right before the radio starts: its current time plus the frame's airtime, rounded to the nearest second, FCS recomputed.
  - Gateways and relays alike, NTP or GPS with fix; never on RTC, phone, manual or `{CET}` time.
  - On by default.
  - Frame length and msg_id stay the same, so dedup is unchanged and no byte is added on air. Only the outgoing TX copy changes; the queued slot, retries and supersede see the original.
  - Receivers need no change: any node that sets its clock from `{CET}` simply gets a more accurate time (expected error under 0.5 s).
- **Open point:** a restamped copy uploaded by another gateway keeps the original msg_id; the server should treat it as a duplicate. To be checked against a test server before the PR.
- **Plan:** pure helper with host tests (rounding, date rollover, FCS), the hook at the TX point (ESP32 and nRF52 share it), bench with a NTP gateway and a LoRa-only node; PR after field time in the fork. (fork ref `CET-RESTAMP-01`)

## Comment on #1309 (2026-11-26)

Two more Safeboot items go with the Safeboot PR: the upload page served gzip-compressed (36 KB to 12 KB, about 25 KB
more Safeboot flash headroom) and the web upload dropping a staged Auto Update image (`onOTAStart()`).

---

## #1341 (draft 12): deep sleep wake pin without pull-up

Filed 2026-11-26 after an operator report from a test board (BACKLOG `SLEEP-PULLUP-01`).

**Title:** ESP32: --deepsleep wakes at once when the button is off and its GPIO has no external pull-up

**Body:**

- **What happens:** `--deepsleep` on an ESP32 board with the user button switched off (`--button off`) reboots the node at once instead of putting it to sleep, if the button GPIO has no external pull-up.
- **Cause:**
  - `esp32EnterDeepSleep()` arms the button GPIO as ext1 wake source (wake on LOW) whenever a button pin is set (`iButtonPin != 99`), whether the button is on or off (`src/esp32/esp32_sleep.cpp`, ext1 block).
  - The internal pull-up is set only by `init_onebutton()`, and only with `--button on` (`btn.setup(iButtonPin, INPUT_PULLUP, ...)` inside `if (bButtonCheck)`, `src/onebutton_functions.cpp`).
  - With the button off the pin floats; the LOW wake condition holds at once and the chip wakes again.
- **Boards:** most boards have an external pull-up on the button and are not affected. The code comment in `esp32_sleep.cpp` names boards without one: E22-DevKitC, E22_XML, ttgo-lora32-v21 (GPIO12), LilyGo T-Beam-1W (GPIO17). Seen on a test board without external pull-up.
- **Planned fix:** set `INPUT_PULLUP` on the wake pin right before arming the ext1 wake, so the pin is in the same state as with `--button on` (the RTC peripheral domain already stays powered during sleep). Regression test on a small decision helper, bench check with `--button off` on a board without external pull-up. (fork ref `SLEEP-PULLUP-01`)
