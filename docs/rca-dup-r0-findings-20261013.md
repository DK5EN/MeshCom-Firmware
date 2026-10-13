# RCA R0 findings: DUP counter gap (Issue A), 2026-10-13

Analysis only (wave R0-A). No source, tool or test file was changed.

## Outcome

The 43-line gap is not a firmware counting defect and not a timing race. Of the 43 lines, 39 are RX lines of server-injected (GWI) text messages that the node hears back over LoRa before its own `TX ... src=g` line is logged. The firmware correctly skips `setlogCountDedup()` for them (the id is in `own_msg_id[]`, branch `heardIcheck >= 0`). `tools/loganalyse.sh` fails to exclude them because it only learns an "own/gateway id" from a `TX x... src=o|g` line, which in these 39 cases comes after the echo. The other 4 are the lost STAT line of the logger outage. Hypothesis A4 holds: the script needs the fix.

## Verdicts

| Hyp.                                             | Verdict                                                      | Evidence                                                                                                                                                                                                                                                                                                                                                          |
| ------------------------------------------------ | ------------------------------------------------------------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| A1 different predicates for `DUP:d` and counter  | REFUTED                                                      | One `is_new_packet()` per frame: ACK path `lora_functions.cpp:526` (verdict feeds both `bIsNew` for the tail and `setlogCountDedup`), main path `:1067-1068` (`rx_is_new`, `rx_dup = !rx_is_new`); counter reuses `rx_is_new` at `:1615`. Capture: `RX_DEDUP_DUP` (ring lookups, loradebug) 1502 = `DUP:d` 1502, soak 8013 vs 8014.                               |
| A2 an RX path prints `DUP:d` but skips the count | CONFIRMED for exactly one path, REFUTED for the listed paths | Skip path: `heardIcheck >= 0` branch `lora_functions.cpp:1552` (own/GWI-injected msg id), counted only in the PN-retry sub-case `:1548`. Not skipping: ACK frames (`:526`), PNREPEAT (rx_is_new true, counted at `:1615`), relayed own frames (id not in `own_msg_id[]`, 466 `DUP:d OWN:e` lines all balance), frames dropped by later filters (count is before). |
| A3 one frame produces two RX lines               | REFUTED                                                      | Only two `[LOG]` RX print sites exist (`lora_functions.cpp:533`, `:1092/:1095`). `RX_DEDUP_DUP` count equals `DUP:d` count, and no two RX lines share a `t=` value (0 repeats in 1502+1150 lines).                                                                                                                                                                |
| A4 script exclusion misses a class               | CONFIRMED                                                    | 39 lines of class "GWI-injected id echoed before `TX src=g`", see below. With the class excluded, 103 of 104 STAT windows balance to 0.                                                                                                                                                                                                                           |
| UDP 0x21 lead (`udp_frame_esp32.cpp:268-272`)    | REFUTED as cause of A, UNVERIFIED as a latent defect         | The lead touches `stat_newid`/ring, never `stat_dup`, and produces no RX line. NEW side balances (diff 3). Whether non-new UDP 0x21 frames occur in the field cannot be read from any log line (no GWI line for them). ESP32 only, see side findings.                                                                                                             |

## Root cause

- Firmware (the skip is a consequence of where the counter is attached; no evidence it was a deliberate design choice, see the decision below): `src/esp32/udp_frame_esp32.cpp:550` (nRF52: `src/nrf52/udp_frame_nrf52.cpp:544`) calls `insertOwnTx(msg_id)` for a server frame that is forwarded to LoRa, then prints `GWI` and queues the frame (`addTxRingEntry`, TX line only when it is actually sent). `addLoraRxBuffer` puts the id in the dedup ring at the same time. When another node relays the same frame before the gateway transmits it, OnRxDone sees `checkOwnTx(msg_id) >= 0` (`lora_functions.cpp:1063`), logs `DUP:d` (ring hit), and takes the `heardIcheck >= 0` branch (`:1552`) instead of the `else if(setlogCountDedup(rx_is_new))` at `:1615`. No stat_dup increment. This split exists since the counter was introduced (`51c3c317`, "Logik unveraendert"), present in fork-dev.
- Script: `tools/loganalyse.sh:749` builds `own_ids` only from `[LOG] TX x... src=o|g`, `:718` applies it to RX lines already seen. A GWI-injected id echoed before its own TX line is missed (`:757` GWI handler does not register the id).

## Per-class counts, `run-20261012-2310.log` (1502 `DUP:d` lines, all classes)

| Class                                                                                         | Lines                              |
| --------------------------------------------------------------------------------------------- | ---------------------------------- |
| Counted by firmware and by script, in STAT windows (466 of all `DUP:d` carry `OWN:e`)         | 1182                               |
| Counted, outside the STAT range (before first / after last STAT)                              | 4 + 4                              |
| Skipped by firmware, excluded by script (GWI id, `TX src=g` before the echo)                  | 270 in windows + 3 after last STAT |
| Skipped by firmware, NOT excluded by script (GWI id, echo before `TX src=g`), all payload `:` | 39                                 |
| HN reports (`>HN@`), unconfigured-call and decode-error drops, `src=o` echoes                 | 0 (none heard in this capture)     |

Arithmetic: 1182 + 39 = 1221 (script tail). STAT sum 1178. 1221 - 1178 = 43 = 39 (class) + 4 (STAT line lost 05:36-05:38, window 05:42 has tail 18 vs dup 14). The brief's "7 of 43" for the outage is 4 here (window arithmetic); the remaining 3 are not found, treat the 7 as unverified.

The 39 lines: echo heard 0.9 to 22.4 s after the GWI line and 3.9 to 10.7 s before the gateway's own `TX src=g` line. The gateway therefore still transmits the frame after having heard it relayed (side finding).

Per-window check (my own reconstruction, 104 windows from the 2nd STAT): excluding HN, `TX src=o|g` ids and GWI ids gives tail = STAT dup in 103 windows; the one deviation is the window after the lost STAT (+4). NEW side: STAT newid 1110 vs tail `DUP:n` + GWI 1113 (unchanged, balanced).

## Soak `soak-20260929/98.log` (FW 35v, no reboot, 686 STAT lines)

Same class, 62 lines (`:` text, GWI before, `TX src=g` after). Script tail 6121 vs STAT dup 6069 (diff 52 = 0.86 %). An emulation of the node's `own_msg_id[]` (a FIFO of `MAX_RING` = 20 on this node, confirmed by `ringmax=n/20` in its STAT lines; `configuration_global.h` sets 10 only for the T-Beam developer variant; filled by GWI lines and `TX src=o` lines) reproduces STAT dup exactly: 6069 = 6069. With FIFO depth 40 the result is 6060; excluding every id that has a `TX src=o|g` line anywhere in the run (unbounded, global) gives 6037. So the script has a second, smaller bias of opposite sign: its unbounded `own_ids` set excludes late echoes whose id has already left the 20-slot FIFO, which the firmware does count (about 10 lines on the soak, 62 - 10 = 52). On the overnight capture the FIFO(20) emulation gives 1182 (gap 4 only). 22 further lines on the soak have a `TX src=g` later but no GWI before; the firmware counted them (the emulation balances without them); the reason is unverified.

## Proposed fix (not implemented)

Script only, `tools/loganalyse.sh`, DEDUP_BALANCE windowing:

1. Register the id at the GWI line (`/\[LOG\] GWI x/` handler, `:757`) the same way a `TX src=o|g` does today (`own_ids[msgid()] = 1`). This is where the firmware inserts it (`insertOwnTx` precedes the GWI print).
2. Bound the set like the firmware: keep the last `MAX_RING` inserted ids (GWI and `TX src=o`) in a ring and test membership against that, not against an ever-growing set. Take the depth from the STAT `ringmax=n/<MAX_RING>` denominator, not a hardcoded 20 (`configuration_global.h` sets 10 on one board variant, 20 elsewhere). Caveat: the emulation inserts `TX src=o` at transmit time while the firmware inserts at enqueue, and some `insertOwnTx` sites print no `TX src=o` (8 in `loop_functions.cpp`, `msgstore_glue.cpp:212`, `test_inject.cpp:186`). It matched both captures exactly; it is an approximation, and the test must be built on that ordering. The soak result above shows step 2 is needed for an exact balance on long captures; step 1 alone removes the 43-line mismatch on the overnight run.

**Operator decision (script fix vs firmware fix).** The firmware's own contract points the other way: `src/lora_functions.cpp:456-457` (from `51c3c317`) says the counters "muessen zu RX_DEDUP_NEW/RX_DEDUP_DUP passen", and `docs/setlog-instrumentation-impl-plan-20260902.md:275` says the counter is incremented where the dedup verdict falls. The capture breaks that: `RX_DEDUP_DUP` 1502 against STAT dup 1178 plus the excluded lines. The "no HN/own/gw echoes" wording exists only as a description in the comment at `tools/loganalyse.sh:689-691` and in the brief, not as a firmware spec.

- (a) Script fix, as proposed above. No firmware change; STAT `dup=` keeps its present meaning (own-id echoes excluded).
- (b) Firmware fix: count in the `heardIcheck >= 0` branch (or once before the branch and drop the `:1548` call). STAT `dup` would then equal `RX_DEDUP_DUP`, and the script needs no own-id FIFO emulation, no `MAX_RING` dependency and no insert-order approximation for DUP. Cost: about 312 more `dup=` per night on this gateway, a semantics change of the STAT field, not a correctness problem.

Recommendation: (b) is the cleaner contract; (a) is the smaller change. Needs the operator's call before R2.

Regression test: `tools/mock/test_loganalyse.py`, next to `test_dedup_balance_ok_on_gateway_artefacts` (`:1022`, helper `_gateway_log` `:983`). Add a case in which `GWI x<id>`, then an `RX ... DUP:d` with the same id, then `TX x<id> ... src=g` (the order that fails today); STAT dup excludes it. Fails before (MISMATCH or off by the injected count), passes after. A second case with more than 20 intervening insertions pins the FIFO bound. Runs in `tools/regression.sh --stage 2` (pytest over `tools/mock`). A native Unity env cannot host this: OnRxDone is not natively testable, and the firmware does not change.

## Side findings

- ESP32-only latent: `udp_frame_esp32.cpp:268-272` runs `addLoraRxBuffer` and `stat_newid.fetch_add(1)` for every UDP 0x21 frame, also when `bUdpMsgIsNew` is false (read at `:213`). Effect would be an over-counted `newid` and a second ring slot for the same id. nRF52 gates both on `is_new_packet()` (`udp_frame_nrf52.cpp:503`, `:556-557`) and is not affected. Not visible in the capture (NEW diff 3). Unverified in the field.
- The gateway transmits server-injected frames (`src=g`, `udp_rx`, prio 0xFF) after it has already heard another node relay them (39 cases overnight). Whether an overhear cancel exists for `udp_rx` ring entries was not traced. Unverified; airtime waste, not a counter issue.
- The mechanism is in shared code, so an nRF52 gateway (RAK4631 with Ethernet, `udp_frame_nrf52.cpp:544-551`) behaves the same by code trace. No nRF52 setlog capture with GWI was analysed: unverified on hardware.

## Evidence checklist

Cited commits, all on `fork-dev` only (`git branch --contains`), none reverted (`git log --grep` for the shas returns nothing; the `Revert` hits touching `lora_functions.cpp` are upstream reverts of unrelated features, and the working tree at HEAD carries the cited code):

- `51c3c317` (2026-09-03) introduces `setlogCountDedup` and the else-branch count.
- `d507e822` (2026-09-27) adds `heardIcheck` and the PN-retry count at `:1548`.
- `6966c527` (2026-10-01) fork delta 4.40a.

"Already fixed": none claimed.

Commands used (read-only): `grep -n`, `sed -n` ranges on `src/lora_functions.cpp`, `src/setlog_lines.cpp`, `src/dedup_functions.cpp`, `src/loop_functions.cpp`, `src/esp32/udp_frame_esp32.cpp`, `src/nrf52/udp_frame_nrf52.cpp`, `tools/loganalyse.sh`; `tools/loganalyse.sh <log>` on both captures (DEDUP_BALANCE: overnight `DUP_STAT 1178 vs 1221`, soak `6069 vs 6121`); Python reconstructions (per STAT window, class split, FIFO emulation) in the session scratchpad over `~/meshlog/rpizero/dk5en-98/run-20261012-2310.log` and `~/meshlog/soak-20260929/98.log`; `git log -S`, `git branch --contains`, `git log --grep`.

## Deviations from the brief

- `~/Desktop/how-to-read-meshcom-logs.md` was not read; the line formats were taken from the captures and `tools/loganalyse.sh`.
- The outage share (brief: 7 of 43) was recomputed as 4.
