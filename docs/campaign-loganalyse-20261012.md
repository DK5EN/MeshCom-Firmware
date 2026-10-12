# Campaign: tools/loganalyse.sh for firmware 4.40a (2026-10-12)

Resume document. A fresh session can continue from here without the transcript.

## Why

On real 4.40a logs the script exits 0 without errors but reports 0 in every packet section
(NODES, MESSAGE_TYPES, HOP_DISTRIBUTION, DEDUP, SIGNAL_PROFILING, TOP_TALKERS, ...). Root
cause: firmware prints `MH-LoRa 085 @ x...` without a colon since `c78adcc5` (2026-08-03,
`--setlog`); the script and its fixtures still expect `MH-LoRa:`. The fixtures carry the old
form, so `tools/mock/test_loganalyse.py` stayed green.

## Evidence

- Real logs: `~/meshlog/soak-20261001-440a/2026-10-01.log` (ESP32 4.40a, 2323 console, 0
  `MH-LoRa:` vs 5149 `MH-LoRa `), `~/meshlog/dk5en-90/serial-20261001.log` (RAK4631),
  `~/meshlog/dk5en-1/2026-09-28-u.log` (ESP32 4.35u).
- Marker audit: 27 findings (F-01..F-27) and 9 coverage gaps (M-01..M-09), each with script
  line and `src/` emitter. Verified by the orchestrator: F-01 colon, F-15 awk `\|` never
  matches, F-17 crash grep hits `CAD_ABORT_BY_RX`, F-03 `S0/S1` is the server flag.

## Waves

| Wave | Content                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  | State |
| ---- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----- |
| W0   | Read-only audit (marker contract vs src) and runs on real logs                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           | done  |
| W1   | Correctness: colon-tolerant tags (F-01/02/05), dead markers out or `n/a` (F-04/07/08/22), RX_IRQ_STALE split (F-06), crash false positives (F-17), CAD busy double count (F-19), WiFi events (F-16), awk `\|` (F-15), S-field semantics (F-03), dedup count (F-11), RING_OVERFLOW (F-10), heap label (F-18), relay count (F-21), stale CSMA estimate (F-20), mktemp; new 4.40a fixture + fail-before tests. Advisor rework round: default `[HEAP] ` space form and nRF52 form parsed, CRASH_COUNT from `RESET_REASON=` (PANIC/WDT/BROWNOUT), WIFI_ISSUE_COUNT on `;event;`, path tables ordered, message types by the `%c` field, RETRANSMIT_GIVEUP/UDP_RESET/BLE latency fixed. 39 tests, advisor APPROVED                                                                                                                              | done  |
| W2a  | `--setlog` `[LOG]` lines as primary source (M-01): RX tail RSSI/SNR (nRF52 gets RSSI, F-13), DUP rate, own echo; RLY reason histogram; TX own/relay/gw split with wait stats; `[LOG] ERR` as RX-loss source (F-14) and no CRC_NRF52 label on ESP32; STAT section (util, sums, drops, heap, reboots); GWI/GWU; dedup balance (M-07) windowed to the STAT range, without HN reports and own/gw echoes, GWI-corrected, tolerance max(5, 1 %). Setlog-only captures (`--info off`) use the `[LOG]` RX lines as packet lines (no ACK/HN). `export LC_ALL=C` (de_DE awk parsed `1.5` as 1). Shared `PAYLOAD_LINE_RE` keeps chat text (MH/RX/TX-LoRa, RX/TX-UDP, `[EXT] Out`, `[LOG]`) out of WiFi/crash/UDP-reset counts. INTERFERER n/a without ferr. `docs/loradebug-serial-output.md` colon form fixed. 75 tests; two advisor rework rounds | done  |
| W2b  | Collision logic rewrite (F-09: one sender per path head, collision = two different path heads per dump), pre-f2d6532f channel-util warning (F-12; real fixtures `logs/dj8meh/2026-08-23-dj8meh-41-putty-{1,2,3}.log`, negatives in `logs/dk5en-98/2026-08-2[5-7].log`), heap `RATING: n/a` without `(mon)` samples, MC-STAT continuation lines (F-23), M-02..M-06, M-08, M-09, dedup-balance window re-anchored after a reboot (`up=` drop)                                                                                                                                                                                                                                                                                                                                                                                              | open  |

## Open leads (not this campaign)

- Firmware: `src/esp32/udp_frame_esp32.cpp:268-272` calls `addLoraRxBuffer` and `stat_newid++` for every UDP position frame (0x21), also when `bUdpMsgIsNew` is false (ring slot plus inflated STAT newid on duplicates). Inferred by the W2a advisor, not verified on hardware.
- DUP_STAT on `soak-20260929/98.log` sits at 0.86 % against the 1 % tolerance with no reboot: an unmodelled bias of about 0.9 % in dup. Look before relying on the tolerance for long captures.

## Out of scope

- The logauswertung skill (`.claude/skills/logauswertung/`) carries caveats for the old script
  behaviour; drop them once W1/W2 land.
