# Soak 2026-09-27/28 (DK5EN-98 + DK5EN-1) -- Fable Verdict

Evaluated 2026-09-29. Review only: nothing in `src/` was changed. Method: five read-only
analysis agents, then one adversarial verifier per finding cluster (Fable/Opus), each told to
refute. Only claims that survived are listed as findings. Scratch scripts and cut logs sit in
the session scratchpad and are not needed to act on this doc.

## BLUF

- **The neighbour matrix works.** DK5EN-98: 0 consistency errors in 1558 `[NBR]|CHECK` minutes,
  102/102 snapshots complete, hop-1/hop-2 invariant holds, 0 text-borne edges, no table
  pressure (max 36/128 rows). An independent replay reproduces every ROW field in P2/P3. The
  10 % share rule (`NBR_SHARE_PCT`, live since d8adf932) holds: DB0ED-99 stayed MESH in all
  102 snapshots, so the 2026-09-25 single-hit decay does not recur.
- **Relay decisions follow the design.** 0 cancelled frames sent anyway, 20 s case-B hold exact
  (min 20056 ms), no ring clogging, 0 RING_DROP_NEW.
- **Stable.** No crash or unplanned reboot on DK5EN-98 in 26 h (two operator OTAs). No heap
  leak. DMs and ACKs worked in every phase.
- **No firmware regression** between the three builds; the P2/P3 switch to 100 % case A is a
  bench-setup effect (Finding 9).
- **Firmware patches:** none is urgent. Three small ones are warranted (F1, F2, F3), one is
  optional (F4), and two need an owner decision (F5, F6). Four host tools give wrong answers
  (F8).

## Window and phases

| Node     | Phase | Window (host time)             | Build                                       |
| -------- | ----- | ------------------------------ | ------------------------------------------- |
| DK5EN-98 | P1    | 27.09 17:38 -> 28.09 09:11     | ce9bf157 (feature-snf XOR retry), "t"       |
| DK5EN-98 | P2    | 28.09 09:12 -> 14:25           | v4.35u.09.28-neo (fed0eb0e)                 |
| DK5EN-98 | P3    | 28.09 14:26 -> 19:38           | v4.35u.09.28.2-neo                          |
| DK5EN-1  | --    | 27.09 17:34 -> 18:52, then gap | ce9bf157; capture died 18:52                |
| DK5EN-1  | S0-S4 | 28.09 09:44 -> 18:29           | 08:39:50, 14:04:27, 17:14:17 (x2), 18:05:30 |

The NBR sources are identical in all DK5EN-98 phases. Both DK5EN-98 reboots (09:11:44,
14:26:00) were OTA flashes. The planned 24 h two-node XOR soak covers only 73 min
(17:39-18:52): the test-DM sender (`soak_dm.py`, "END sent=5") and DK5EN-1's
`serial_capture.py` were killed by the same host-side signal at 18:52; the firmware kept
running (DK5EN-98 heard DK5EN-1 1347 times afterwards).

## Finding 1: DM `ack=`/`rtt=` counters ignore ACKs that arrive via the server

- **File:** `src/lora_functions.cpp:1639-1640` (only call sites of `dmstat_peer_ack` /
  `dmStatNoteAck`); UDP ACK branches `src/esp32/udp_frame_esp32.cpp:339-351`,
  `src/nrf52/udp_frame_nrf52.cpp:302-318` skip both.
- **Severity:** medium (diagnostic counter; no delivery effect).
- **Failure scenario:** P1: 14 of 15 own DMs acked, `ack=` sums to 7 (6 LoRa-acked DMs plus one
  duplicate LoRa ACK for NNN 339); the 8 UDP-only ACKs are missing. RTT is taken from the later
  LoRa ACK: NNN 821 UDP +12.0 s recorded as +57.2 s (bucket 2); P2 38A +30.6 s as +82 s.
- **Fix:** `dmStatNoteAck()` returns `bool` (true only when it finds and clears the entry); count
  `dmstat_peer_ack` only on true, at the LoRa site and inside `iackcheck >= 0` in both UDP
  handlers. Do NOT gate on `own_msg_id[..][4] == 0x02`: the gateway-ACK path also writes 0x02
  (`lora_functions.cpp:495`), and the UDP copy never enters the LoRa dedup ring
  (`udp_frame_esp32.cpp:511`), so a naive add counts 19 in P1 against 14 true.
- **Test:** `test/test_dm_stats` (second NoteAck on the same NNN returns false);
  `test/test_udp_frame_twin` `test_regression_server_ack_own_dm_stops_ring_on_both` (~:1567)
  extended with `dmStatNoteSent` -> one UDP ACK gives peer_ack 1 + one RTT sample; today 0.

## Finding 2: CRC / RETX hex dumps and CAD `[CHECK]` lines end without a newline

- **File:** `src/esp32/esp32_main.cpp:4288` (CRC_PAYLOAD), `:4327` (ERR_PAYLOAD),
  `src/lora_functions.cpp:3115` (RETX ASCII dump, ESP32 + nRF52); `[CHECK]
radio.scanChannel() / 1` `esp32_main.cpp:2780`, `/ 2` `:2805`, T-ETH-Elite `:2786`.
- **Severity:** low (logging), but it costs analysis data.
- **Failure scenario:** `printfdeb()` never appends `\n` (`src/printfdeb_functions.cpp:80-158`),
  so `printfdeb("")` writes nothing. 771/771 CRC dumps have the next line glued on, including
  55 NBR lines (CHECK 49, RPTTX 3, ECHO 2, SNAP 1) that `tools/nbrlog.py` then drops, one
  `[LOG] DM sent=` line, and 3307/3307 CAD_SCAN lines.
- **Origin:** upstream 65351163 (Serial.println -> printfdeb("")); upstream/dev still has it ->
  clean upstream PR candidate.
- **Fix:** `printfdeb("\n")` at the three dump sites; `\n` in the three `[CHECK]` formats.
  Leave the `printfdeb("")` no-ops after `printBuffer_aprs` alone (they already end in `\n`).
  No host parser depends on the glued form (checked: logharvest, traceharvest, serial_monitor,
  loganalyse.sh, nbr* tools).
- **Test:** none on host (print path); verify on hardware by a CRC_PAYLOAD line followed by a
  line-start tag.

## Finding 3: unplanned TASK_WDT reset on DK5EN-1 (build 17:14:17), cause not provable

- **File:** `~/meshlog/dk5en-1/2026-09-28-u.log:65323-65351`; loopTask WDT add/feed
  `src/esp32/esp32_main.cpp:2035/2118`.
- **Severity:** medium (one reset in every capture we hold: RESET_REASON histogram POWERON 7 /
  SW 13 / TASK_WDT 1).
- **Facts:** 42 s after an operator OTA boot (the "48156" in the IDF line is not uptime;
  uptime ~42.2 s). loopTask was blocked, not spinning (CPU1 ran IDLE1; `yield()` never lets
  IDLE run). Block started between 34.9 and 37.2 s. Coredump failed ("Not enough space", 4 KB
  partition); the backtrace covers only the core-0 ISR; the ELF (SHA ce40bfc1) is gone.
- **Only surviving candidate:** the web serve path. `WiFiClient::write` retries 10 x
  `select(1 s)` (framework `WiFiClient.cpp:389-440`), so one write can block up to 10 s, and
  the page senders (884 `web_client.print` sites, scaffold >= 41.7 kB) never feed the WDT. A
  WebUI tab was open on the node (autorefresh 30 s), and the link was impaired in that boot
  (both async DNS lookups timed out after 14 s vs 73 ms in the next boot). Ruled out with
  evidence: NTP, DNS, gateway service (off), UDP drain, net console (never started),
  checkWifiPing, Serial, radio delays. Still reachable on HEAD 1e5db6a7.
- **Fix (warranted):** forensic only. The existing instrument (`src/instrument.h`) is compiled
  out (`INSTRUMENT_ENABLED 0`) and records only completed gaps, with no web section. Add an
  entry breadcrumb (section name + millis in RTC_NOINIT, set on entry, cleared on exit), a
  "web" section around `loopWebserver()`, printed unconditionally after RESET_REASON=6.
- **Not warranted now:** a root-cause fix (trigger unobserved); enlarging the coredump
  partition (USB-only rollout, and a dump needs the matching ELF, which releases do not keep).
- **Optional hardening:** bound or feed the web send path; non-trivial (884 sites plus the
  Ethernet `CommonWebClient` twin).

## Finding 4: `att=` counts every text TX, not DM attempts

- **File:** `src/lora_functions.cpp:2885` vs `src/dm_stats.h:25`.
- **Severity:** low (metric).
- **Failure scenario:** `att=` equals the TX-LoRa `:` frame count in 308/310 intervals; P1
  att 434 against 20 own DM TX (15 DMs + 5 PNRETRY).
- **Fix:** add `&& pnFrameIsOwnPn(lora_tx_buffer, sendlng, _GW_ID, node_call)` (predicate
  already covered by `test/test_pn_retry`), or correct the header to the as-built meaning.

## Finding 5: "has the frame" (HatF) trusts a single stray hit -- owner decision

- **File:** `src/nbr_matrix.cpp:1678-1686` (bare edge), cover/provider use `nbrICovers` with the
  share (`:1342-1358`, `:1731-1737`, `:1786`).
- **Severity:** low. Implemented as specified: concept `03-kern.html:164`, header
  `nbr_matrix.h:657-663`, test `test_nbr_matrix.cpp:2149-2153`. But it contradicts the concept's
  own intent (`03-kern.html:201` "safe direction", `:221` nothing suppressed on suspicion): one
  stray hit of a path token drops a station from `need`, so it can never be "alone" and a cancel
  becomes possible.
- **Measured impact (P1, independent replay):** share-gated HatF changes 72/1110 case-B lines
  (21 B->A) and prevents 28/206 cancels, all about ONE dependent, OE7XWT-12 (direct at -127 dBm
  / SNR -18..-20, never seen hearing DK5EN-98). No missed delivery demonstrable either way.
  Cost of the change: about +3 % relays.
- **Fix if decided:** hoist `nbrICtx` above the HatF loop and use `nbrICovers(m, c, ed)` there;
  update header, concept, `docs/nbr-logformat.md`, and flip the test at `:2149-2153` (new case:
  listener with cnt 1 vs 50 stays in `need` and `alone`).

## Finding 6: the NBR gateway flag is learned once and never cleared -- owner decision

- **File:** `src/nbr_matrix.cpp:1273-1277` (`NBR_FLAG_GW` set only when a HEY goes to "HG");
  cleared only by row reset (`:659`) or matrix init.
- **Severity:** low; field impact unverified. Found by the C7 verifier, code-confirmed.
- **Failure scenario:** (a) after a reboot, a gateway neighbour counts as a dependent until its
  next "HG" HEY -- HEYs are trickle-suppressed (DK5EN-98 received none from DK5EN-1 for 15.6 h)
  -> extra case-A relays, safe direction. (b) A neighbour that switches its gateway off stays
  flagged until the receiver reboots -> excluded from `dep`, cancels possible for a real
  dependent, unsafe direction.
- **Fix if decided:** a HEY to plain "H" from the same sender clears the flag (check what
  `dest_gw` means for the other '@' frames first, e.g. `nbr_report`).

## Finding 7: an overheard copy never removes its own relayer from `need` (cosmetic)

- **File:** `src/lora_functions.cpp:1128-1132`, `src/nbr_matrix.cpp:1786/1795`.
- **Severity:** cosmetic, safe direction. 1 event in P1 (E9F11388: need = {OE7XWT-12},
  OE7XWT-12's own relay heard at 00:54:16, DK5EN-98 relayed anyway at 00:54:41).
- **Fix (optional):** clear the relayer's own row bit from `ringNeed` in the scan at
  `lora_functions.cpp:1128ff`, not inside `nbrCoverMask` (`cover_gate` relies on its emptiness).

## Finding 8: host analysis tools give wrong answers

- **T1** `tools/nbrhopcheck.py:36-49` static `KNOWN_SERVER` (from the 23.09 evaluation) holds
  four RF POS/HEY senders (DF2AP-99, DF4ND-99, DO1TFS-99, DO5DMF-99) -> 24 false SERVER flags in
  P1. Fix: `server = (KNOWN_SERVER | udp_src) - rf_posthey`. Also classify stale rows (hearers 0
  or age >= 720, verdict UNK; hearer itself hop 2) as "stale", not FAIL -- the snapshot logs
  every occupied row by contract (`docs/nbr-logformat.md:49`).
- **T2** `tools/nbrlog.py`, `tools/nbrsnap.py`: no share rule, no 90-min halving -> false "1
  Abweichung DB0ED-99" (P2, P3) and 17 snapshots with 0 needed nodes next to firmware MESH.
  With a 10 % share: 0 deviations. Also drops the 55 glued NBR lines (F2).
- **T3** `tools/soakstatus.py`: ack path/latency only from `[RETX]` markers (`:597-616`), which
  are not printed when the ACK beats the first LoRa TX -> DM #2 and #3 mislabelled; copies
  column counts `MH-LoRa` + `RX-LoRa2` twice; Overall FAIL whenever DK5EN-1 has no data.
  **Worst:** a capture that dies mid-window reads as alive -- gaps are only measured between
  lines, never against `--since`/`--until` (a run ending 09:00 PASSes although DK5EN-1 died at
  18:52). This is how the 18:52 loss went unnoticed.
- Self-tests of both tools pass; none covers these cases. Each fix needs a fixture test.

## Finding 9 (process): the P2/P3 data cannot test case B / CANCEL

- No firmware cause: the ce9bf157..fed0eb0e and ..09.28.2-neo diffs touch no NBR, relay, ring
  or settings-layout code (`FLASH_STRUCT_VERSION` still 20260724); DK5EN-98 kept its gateway
  flag (row 0 flags 15) and symmetry.
- Cause: DK5EN-98's 09:11 reboot cleared DK5EN-1's gateway flag, and DK5EN-1 now runs with
  gateway off ("Gateway: 0" on 28.09, HEY "H" from 09:14:42) -> it sits in the alone mask of
  459/480 (P2) and 401/426 (P3) NEED lines. DK5EN-90 (RAK, first frame 10:17:37, never relays)
  adds 357/390. Masking both: P3 39 A / 387 B; P2 still 318 A (DL2JA-73 two hits at -124/-127
  dBm kept alone 3.6 h, DL2JA-1, new field station DG7RJ-5) -- the matrix warm-up after a reboot
  takes about 3 h.
- **Next NBR soak needs:** no DK5EN-98 reflash during the run; DK5EN-1 and DK5EN-90 off or out
  of range, or gateway on with `--sendhey` after every DK5EN-98 boot (check flags=15 in their
  first ROW); `--info` after every flash; reject any hour in which one bench call sits in more
  than 50 % of the alone masks.

## Expected / external (no action)

- Server heartbeat gaps (P1 18, P3 33 missing BEATs): 31/31 coincide with hub restarts
  (mcmap `gateway_availability`); node keepalive every 30 s, 0 WiFi drops.
- ONRXDONE_SLOW on ~60 % of RX (median 90 ms): cost of the debug flags; flat across builds.
- CRC errors ~9 % of RX attempts at median -99..-106 dBm, RX_IRQ_STALE at the noise floor: flat.
- DK5EN-98 re-associated to another BSSID after the 14:26 OTA (RSSI -52 -> -71 dBm), no drops.
- One DM give-up (NNN 816 to DK5EN-2) was a deliberate timeout test.
- DK5EN-1 -> DK5EN-98 is the weak direction (73-78 % heard vs 97-99 % the other way); half the
  misses coincide with a CRC error at DK5EN-98 (2 dBm leaf).

## Refuted claims (do not re-investigate)

- "Case-B relays break the 60 s cap" (max 136 s) -- the cap only bounds the CSMA backoff
  (301d9b08); selection is prio then FIFO by design. 99.1 % of 904 B relays left within 70 s,
  the late ones cost ~40 s airtime per day and are deduped fleet-wide. Not a defect.
- "HN report drop is an NBR defect" -- 1/146 lost to generic prio-5 starvation behind gateway
  text (BP-03 stale drop, 180 s); the NBR hold can only let an HN go earlier. Optional
  short-search for own prio-5 slots after 60 s is sketched in the C3 notes; not recommended.
- "Snapshot rows beyond hop 2 are a firmware bug" -- `nbrLogSnapshot` logs every occupied row
  as documented; only the hop-check tool is wrong (T1).
- "CANCEL `<inferred>` understates symmetry" -- the field reports only cover-step bits by
  design; DL2JA-1 was HASF, never in `need`.
- "P2/P3 case-A flip is a firmware regression" -- see Finding 9.
- "The share rule is not implemented" (brief error) -- live since d8adf932.
- Finder counts corrected: HatF 311 lines / 45 cancels -> 72 / 28; KNOWN_SERVER 19 -> 24
  snapshots; WDT "48156 ms uptime" -> ~42.2 s.
- DM-03 (ACK before the first LoRa TX does not stop it; 4 cases, one extra TX each) -- known
  M1, accepted; stopping READY slots would reopen the doTX race.
