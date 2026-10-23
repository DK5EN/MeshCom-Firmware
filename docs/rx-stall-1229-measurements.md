# LoRa RX during slow web clients: measurements (WEB-RX-01, upstream #1229)

**Bottom line:** a slow browser on the web GUI made an ESP32 node lose **36 % of the LoRa frames** that a
neighbour received (16 of 44). With the fix (`5fdd4b8d`, packet park) the same test loses **none (45 of 45)**.
Nothing changes without a web client. Measured on real hardware, over the air, with a reference receiver,
before and after, in three load conditions.

Date: 2026-10-23. Branch: `fork-dev`. Campaign and decisions:
[campaign-rx-stall-1229.md](campaign-rx-stall-1229.md). Defect: WEB-RX-01 in
[08-defect-catalogue.md](architecture/08-defect-catalogue.md) section 2h.

## 1. Result

Frames received out of 45 sent per arm (8 s interval):

| Load on the node's web server                | Before: node | Before: reference | Before: lost only at node | After: node | After: reference | After: lost only at node |
| -------------------------------------------- | ------------ | ----------------- | ------------------------- | ----------- | ---------------- | ------------------------ |
| A: no web client                             | 44           | 44                | 0                         | 44          | 44               | 0                        |
| B: stalled browser (2.5 s per reply)         | 44           | 45                | **1**                     | 45          | 45               | **0**                    |
| C: slow but moving browser (1.2 s per reply) | 28           | 44                | **16 (35.6 pp)**          | 45          | 45               | **0**                    |

- The one frame missing in both A runs was missing at the reference too: lost on air, not in the node.
- "Lost only at node" is the number that matters: frames the reference receiver got and the node did not.

## 2. Why it happened

- **Mechanism:** on ESP32 the radio interrupt only sets a flag; the packet is read off the SX126x by the main
  loop. The chip holds one packet. While the loop is busy sending a web page, a second frame overwrites the
  first.
- **How long the loop was blocked:** the send buffer from upstream PR #1224 already bounds a page:
  - a browser that stops reading is cut after **2 s** (arm B: 55 loop gaps of about 2.0-2.5 s),
  - a browser that keeps reading slowly never trips that, so each page runs up to the **15 s** budget
    (arm C: 25 loop gaps of up to 15.0 s, 342 s blocked in a 7-minute run).
- **Read delay before the fix** (`irq_to_read_us`, time from radio interrupt to packet read):

  | Arm | Reads > 100 ms | Median     | Worst  |
  | --- | -------------- | ---------- | ------ |
  | A   | 0 of 86        | 3.8 ms     | 7.6 ms |
  | B   | 22 of 100      | 5.0 ms     | 2.07 s |
  | C   | 25 of 38       | about 10 s | 14.8 s |

## 3. The fix

- The web send buffer already calls a feed function in every socket wait. That function now also checks the
  radio: if a packet is pending and no transmission runs, it reads the packet off the chip into a 4-slot park
  and restarts reception. It does not process the packet.
- The loop processes parked packets first, oldest first, through the normal receive path.
- The web block itself is unchanged (arm C after the fix: still 24 gaps of up to 15.1 s). What changed is that
  the packets no longer wait on the chip: in arm B after the fix **0 of 86** reads took longer than 100 ms
  (before: 22 of 100).
- Park counters after the three arms: 97 packets parked, **0 overflows**, 0 clamped. Four slots were enough.
- Cost: +1088 B static RAM on a classic ESP32 (T-Beam). nRF52 is untouched (it already receives in its own
  task).

## 4. How it was measured

**Setup (all over the air, transmit power 2 dBm, test group `TEST` only):**

| Role               | Node     | Board       | Firmware                                           |
| ------------------ | -------- | ----------- | -------------------------------------------------- |
| Node under test    | DK5EN-1  | Heltec V3   | INSTRUMENT build of `fork-dev`, before / after fix |
| Sender             | DK5EN-92 | T-Beam v1.2 | INSTRUMENT build (`--loratx` burst command)        |
| Reference receiver | DK5EN-14 | T-Deck Plus | `--setlog on` for the run                          |
| Background relay   | DK5EN-98 | Heltec V3   | production gateway, relays every frame once        |

**Procedure per arm:**

1. Identity check on all three nodes (callsign, 2 dBm, IP) before anything is sent.
2. Console capture of node and reference over the authenticated network console (port 2323).
3. Sender: 3 x `--loratx 20 8000` = 45 numbered frames to group `TEST`.
4. Arms B and C: a continuous HTTP GET loop on the node's main page, slowed by a dummynet pipe on the Mac
   (`tools/bench/web_throttle.sh`, 2500 ms or 1200 ms delay per reply).
5. Frames matched by message ID across sender, node and reference; loop gaps and read delays from the
   INSTRUMENT lines.

**Pre-registered before the first run:** H1 no loss without a client (held), H2 read delays in the seconds
range with a client (held), H3 extra loss with a client (held, 35.6 pp in arm C), go/no-go at 5 pp.

**Tooling:** `tools/bench/rx_stall_bench.py` (live capture and offline analysis, 17 host tests on fixture
logs). Every number above can be recomputed from the summaries with `--analyse`.

## 5. Measurement problems found and fixed on the way

Each would have produced a wrong headline number:

- **The sender could not keep up at 3 s.** Frames were queued every 3 s but went on air only every 5 s
  (DK5EN-98's relays fill the channel). The first arm A "lost" 30 % that were never sent. Fix: count only frames
  the sender logged as transmitted, and use an 8 s interval.
- **The reference printed nothing.** The T-Deck had receive logging off; the first run showed 0 % reception there.
  Fix: `--setlog on` for the run.
- **Loop gaps were invisible.** The console prints `;` as spaces, so the gap parser matched nothing ("no
  gaps" with 55 gaps in the log). Fix: accept both separators, with a test.
- **Read delay is not a reliable metric after the fix.** With several parked packets the INSTRUMENT read stamp
  is one global value, so the comparison uses "lost only at node", not read delay, as the decision metric.

## 6. Review and gate

- Independent review of the fix by a second model (Opus) before the commit: "changes required"; all applied
  (a full park now leaves the packet on the chip instead of dropping it, stack and attribution fixes, sharper
  tests).
- Gate: 78 host test environments (2753 cases), golden selftest, 1207 tool tests, 33 of 34 board builds (the
  34th fails on plain upstream too).

## 7. Not covered

- One node type under test (Heltec V3). T-Beam and T-Deck run the same code path, not measured.
- No soak with normal traffic yet (campaign step W3, second half).
- Stack depth of the web path with the hook was not measured with `-fstack-usage`.

## 8. Evidence

Summaries in `tools/bench/runs/harness/`:

| Arm | Before                            | After                             |
| --- | --------------------------------- | --------------------------------- |
| A   | `rx_stall_A_20261023-170154.json` | `rx_stall_A_20261023-183947.json` |
| B   | `rx_stall_B_20261023-171724.json` | `rx_stall_B_20261023-182852.json` |
| C   | `rx_stall_C_20261023-172652.json` | `rx_stall_C_20261023-182059.json` |
