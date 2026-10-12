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

| Wave | Content                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     | State |
| ---- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----- |
| W0   | Read-only audit (marker contract vs src) and runs on real logs                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                              | done  |
| W1   | Correctness: colon-tolerant tags (F-01/02/05), dead markers out or `n/a` (F-04/07/08/22), RX_IRQ_STALE split (F-06), crash false positives (F-17), CAD busy double count (F-19), WiFi events (F-16), awk `\|` (F-15), S-field semantics (F-03), dedup count (F-11), RING_OVERFLOW (F-10), heap label (F-18), relay count (F-21), stale CSMA estimate (F-20), mktemp; new 4.40a fixture + fail-before tests. Advisor rework round: default `[HEAP] ` space form and nRF52 form parsed, CRASH_COUNT from `RESET_REASON=` (PANIC/WDT/BROWNOUT), WIFI_ISSUE_COUNT on `;event;`, path tables ordered, message types by the `%c` field, RETRANSMIT_GIVEUP/UDP_RESET/BLE latency fixed. 39 tests, advisor APPROVED | done  |
| W2   | New analyses: `--setlog` lines as primary source (M-01: RX tail RSSI/SNR/DUP, RLY reasons, TX own/relay split, ERR, STAT, GWI/GWU), collision logic rewrite (F-09), pre-f2d6532f channel-util warning (F-12), nRF52 RSSI via `[LOG]` (F-13), `[LOG] ERR` as CRC source (F-14), MC-STAT continuation lines (F-23), M-02..M-09, heap `RATING: n/a` when there are no `(mon)` samples                                                                                                                                                                                                                                                                                                                          | open  |

## Out of scope

- `docs/loradebug-serial-output.md:210` still documents the colon form; fix with W2.
- The logauswertung skill (`.claude/skills/logauswertung/`) carries caveats for the old script
  behaviour; drop them once W1/W2 land.
