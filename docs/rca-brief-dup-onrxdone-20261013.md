# RCA brief: DUP counter gap and slow OnRxDone (2026-10-13)

Start document for an RCA campaign in a fresh session. Nothing here is a conclusion: the sections
below hold the observations, the code anchors and the hypotheses to test. Run the campaign with
`/orchestrate-waves`; keep this file as its resume point and update it per wave.

## Mandate

- **Deliverable:** root cause and fix for both issues. Each fix ships with a regression test that
  fails before and passes after, and is bench-verified on DK5EN-1.
- **Bench node:** DK5EN-1, Heltec V3, USB serial `0001` (`/dev/cu.usbserial-0001` on 2026-10-13,
  resolve by serial number). It is the board closest to the gateway DK5EN-98. The operator granted
  permission to flash it at any time (2026-10-13).
- **DK5EN-98** (production gateway) is read-only for this campaign: evidence comes from captures
  (`tools/meshlogger.py` on rpizero can be armed again), never from flashing it.
- Bench rules from `CLAUDE.md` apply unchanged: test traffic only to group TEST or a DM to an own
  node, TX power at most 2 dBm, `tools/bench/identity_guard.py` before every test, `lsof` before
  opening a port, `--airgap on` instead of power-off.

## Evidence base

| Capture                                                                                 | What it is                                                                                  |
| --------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------- |
| `~/meshlog/rpizero/dk5en-98/run-20261012-2310.log`                                      | DK5EN-98, 2026-10-12 23:10 to 2026-10-13 08:00, FW 40a/20261011, setlog+loradebug+txcapture |
| `~/meshlog/soak-20260929/98.log`                                                        | DK5EN-98 soak, no reboot, DUP balance 0.86 % (same sign, smaller)                           |
| `~/meshlog/rpizero/dk5en-{1,14,92,98}/`                                                 | Net-console corpus 09-02 to 10-05                                                           |
| `~/meshlog/soak-20261001-440a/2026-10-01.log`, `~/meshlog/dk5en-90/serial-20261001.log` | ESP32 4.40a soak; RAK4631 (nRF52) for the platform comparison                               |

Analysis tool: `tools/loganalyse.sh <log>` (sections DEDUP_BALANCE, ONRXDONE_HEALTH). Line formats
and emitters: `~/Desktop/how-to-read-meshcom-logs.md`.

## Issue A: DUP counter differs from the logged duplicates

**Observation (overnight capture):** `DUP_STAT_BALANCE: MISMATCH (diff 43 > tolerance 12)`:
STAT `dup=` summed 1178, logged `DUP:d` 1221 (HN reports, own and gateway echoes already excluded).
The NEW side balances (`STAT_newid=1110` vs `DUP:n + GWI = 1113`). No reboot (`up=` continuous).

- **Not the capture gap.** One logger outage (05:36:17-05:38:52, node closed the socket, mDNS
  failed, `up=` continuous) lost one STAT line; that window explains 7 of the 43.
- **Rises with traffic.** Raw per-window `DUP:d` minus STAT `dup=` (raw, echoes included) is a
  steady 3-5 per 5-min window overnight and 11-14 between 07:15 and 07:50.
- Same sign at 0.86 % on `soak-20260929/98.log`.

**Code anchors:**

- Counter: `setlogCountDedup()` `src/lora_functions.cpp:456-465` increments `stat_newid` /
  `stat_dup` "exactly once per frame, where the decision falls"; read and reset by
  `stat_dup.exchange(0)` `src/loop_functions.cpp:3394`.
- Log flag: `DUP:%c` in the RX tail, `src/setlog_lines.cpp:33`; contract in `src/setlog_lines.h:38`
  ("msg_id was already in the dedup ring").
- Known related lead: `src/esp32/udp_frame_esp32.cpp:268-272` calls `addLoraRxBuffer` and
  `stat_newid++` for every UDP position frame (0x21), also when `bUdpMsgIsNew` is false (W2a
  advisor, not verified).

**Hypotheses to test (each needs evidence, none is established):**

1. The `DUP:d` flag and `setlogCountDedup()` use different predicates (ring lookup at print time
   vs the dedup verdict), so a frame can be logged `DUP:d` without passing through the counter.
2. Some RX paths emit an RX line with `DUP:d` but return before `setlogCountDedup()` (ACK frames,
   PNREPEAT, relayed own frames, frames dropped by a later filter).
3. One received frame produces two RX lines (for example the relay re-print or the gateway
   forward), each tagged `DUP:d`.
4. The script's exclusion rules (HN, own echo, gateway echo) miss a class of lines that the
   counter also skips. Then the script needs fixing, not the firmware.

The traffic correlation favours a per-frame path over a timing race: a race at the STAT boundary
would net out over a run, but this excess only grows.

## Issue B: OnRxDone is slow

**Observation (overnight capture, ESP32 gateway):** 2652 `ONRXDONE_TIME` samples: p50 59 ms, p90
112 ms, p99 123 ms, max 133 ms. 1857 `ONRXDONE_SLOW` warnings (70 % of samples over the 50 ms
threshold). 468 `RX_IRQ_STALE` lines in the same capture.

**Operator concerns:**

1. **Lost or late packets.** Does the time spent in OnRxDone cost receptions: is the radio back in
   RX during processing, or does a packet arriving meanwhile get lost or picked up late
   (`RX_IRQ_STALE`, `CHECKRX`, the RX buffer switch)?
2. **Threshold sanity.** Is 50 ms (`ONRXDONE_WARN_MS`, `src/configuration_global.h:406`) the right
   warning level, given the gateway median is already 59 ms?

**Code anchors:** measurement `src/lora_functions.cpp:144-145` (`onrxdone_max_ms`, "always active,
not behind bLORADEBUG"), early exits printing `ONRXDONE_TIME` at `:948-950` and `:1165`, the main
exit at `:2583-2594`; per-10-s summary `[MC-DBG] ONRXDONE_STATS` `src/esp32/esp32_main.cpp:2849`,
`src/nrf52/nrf52_main.cpp:1543`. On nRF52 OnRxDone runs in the 16 KB LORA task (project memory).

**Questions the RCA must answer:**

- What does OnRxDone spend the time on? Break it down per phase (decode, dedup, relay decision,
  UDP/gateway send, display, logging) with the bench node, not by reading code alone.
- How much of it is the debug output itself? `--loradebug`, `--setlog` and `--txcapture` all print
  from this path, and printing over the 2323 console costs time. Measure with the flags off.
- Is the radio receiving while OnRxDone runs? Correlate slow events with `RX_IRQ_STALE`, CRC
  errors and gaps in the RX sequence in the overnight capture, then reproduce on DK5EN-1.
- Gateway vs node: DK5EN-98 is a gateway (UDP send in the RX path). Compare DK5EN-1 as a node and
  as a gateway, and the nRF52 capture.

## Evidence rules (from orchestrate-waves, analysis writers)

- Every cited commit is still in `fork-dev` and not reverted (`git branch --contains`,
  `git log --grep=Revert`).
- Every platform claim is checked per platform (ESP32 and nRF52 differ in this path).
- Every "already fixed" claim names the commit and branch.
- A verdict from a broken instrument is void: prove an instrumented build carries the new
  markers (string scan of the image, `INSTRUMENT_ENABLED=1` without a space).

## Proposed waves

| Wave | Content                                                                                                       | Agents                                     |
| ---- | ------------------------------------------------------------------------------------------------------------- | ------------------------------------------ |
| R0   | Read-only: trace both paths in `src/`, test hypotheses A1-A4 against the captures, phase map of OnRxDone      | 2 analysis writers (Sonnet high), parallel |
| R1   | Instrumented build on DK5EN-1: per-phase timing, DUP flag vs counter per frame; bench run, debug flags on/off | 1 implementer + serialized bench gate      |
| R2   | Fixes with fail-before/pass-after tests (native Unity env or golden), threshold decision, bench re-verify     | implementer(s) per issue, file-disjoint    |
| R3   | Field check: meshlogger capture on DK5EN-98 after an operator-run update, `loganalyse.sh` balance and health  | orchestrator                               |

Advisor gate (Opus medium) after R0 and after every behavioural wave.

## R0 status (2026-10-13, done)

- Findings: `docs/rca-dup-r0-findings-20261013.md` (Issue A), `docs/rca-onrxdone-r0-findings-20261013.md`
  (Issue B). Analysis writers Sonnet high; advisor gate Opus medium returned a rework list of 9 wording
  and evidence-labelling items, applied by the orchestrator (docs only, no re-gate).
- **Issue A:** hypotheses A1 and A3 refuted, A2 confirmed for one path (own-msg_id branch
  `lora_functions.cpp:1552`), A4 confirmed. 39 of the 43 lines are GWI echoes heard before the node's
  own `TX src=g` line, 4 are the lost STAT window. Fix options: (a) `tools/loganalyse.sh`
  registers ids at the GWI handler and bounds the set to `MAX_RING`; (b) firmware counts in the
  `heardIcheck >= 0` branch so `dup=` equals `RX_DEDUP_DUP`. Needs the operator's call.
- **Issue B:** handler time is about 91 % serial printing (UART-bridge boards block at baud rate);
  `RX_IRQ_STALE` is 234 events, not 468; no reception loss shown, but no loss measurement exists.
  R1 plan: flags matrix A-D plus arm F (UART TX buffer), `micros()` accumulator in
  `MeshSerialClass::write`, IRQ-to-`readData` latency, reference receiver.
- Side findings, not yet in BACKLOG: gateway transmits server-injected frames after hearing a relay
  (39 per night, overhear-cancel unverified); ESP32 `udp_frame_esp32.cpp:268-272` over-counts
  `newid`; early exits skip the `onrxdone` counters; `--loradebug on` persists `bDisplayInfo` and
  `bDisplayRetx`.
- Next: R1 (instrumented DK5EN-1 build and bench) in a fresh session.

## R1 status (2026-10-13, built and gated, bench run pending)

- Firmware (INSTRUMENT only, compiled out otherwise): `[INS] ONRX seq= total_us= print_us= bytes= len= irq_to_read_us= ph= uart_us= tcp_us= lost= txbuf=<bytes>/<ok>` from the loop
  (`instrument_onrx_drain()`), print time accumulated in `MeshSerialClass::write`, IRQ stamp consumed once
  (`-1` = no stamp), `INSTR_UART_TXBUF` compile-time switch for arm F. Files: `src/instrument.{h,cpp}`,
  `src/net_console.cpp`, `src/lora_functions.cpp`, `src/esp32/esp32_main.cpp`.
- Driver and analyser: `tools/bench/onrx_matrix.py --host <ip> --arm A|B|C|D|F --minutes N` (offline:
  `--analyse <log>`), tests in `tools/mock/test_onrx_matrix.py` (unittest). Sends only settings commands,
  restores them in a `finally` with one reconnect retry; password only from `MC_CONSOLE_PW`.
- Advisor (Opus) returned rework S1-S4 plus nits; the host gate caught a pytest import under the golden
  selftest. All applied by two fresh Sonnet-high implementers; second gate green (Unity 2371/2371,
  pytest 1036, board links ok). Rework diff read by the orchestrator, not re-gated by an advisor.
- Images: plain and INSTRUMENT build green; INSTRUMENT image carries `ONRX_READY`, `txbuf=`,
  `irq_to_read_us=%ld` (string scan); arm F image carries `UARTTXBUF`. Plain image has none of them.
  Nothing has run on hardware yet.
- Next: flash DK5EN-1 (INSTRUMENT image), run arms A, B, C, D, then the arm F image; write the numbers into
  `docs/rca-onrxdone-r0-findings-20261013.md` (R1 section) and decide R2.

## R1 bench result (2026-10-13, DK5EN-1)

- Arms A-D and F ran, numbers in `docs/rca-onrxdone-r0-findings-20261013.md` ("R1 bench results").
  Handler time is print time: arm A p50 77.9 ms, B 25.1, C 13.5, D 1.5; arm F (A plus a 4096 B UART TX
  ring) p50 4.4 ms, max 9.0 ms, none over 50 ms.
- Driver bug found on first contact and fixed: the node key was `dk5en-1`, the fleet key is `heltec-1`
  (test pins it to `fleet.json`).
- Open: R2 (production fix: UART TX buffer per board, regression check, threshold decision), the DK5EN-98
  field check. On stock firmware DK5EN-98 prints `ONRXDONE_TIME` only with `--loradebug on`, so a
  production-normal measurement needs a lean image (R3); DK5EN-98 itself was only read (`--info`) and
  recorded for 3 minutes, no setting changed.
- DK5EN-1 now runs the arm F image (INSTRUMENT + 4096 B TX ring); settings restored.

## Open questions for the operator

- R2 touches the RX hot path. Should the fixes go upstream as a PR afterwards, or stay fork-only?
- Issue A: script fix (a) or firmware fix (b)? See R0 status.
- Threshold: if 50 ms is wrong, should `ONRXDONE_WARN_MS` change per platform or per role (gateway)?
