# Concept: time source ranking and a forward-only `{CET}` filter (CET-SCATTER-01)

**The scatter has one origin: in S3 both nodes set the same payload on all 11 beacons, and the T-Beam lagged by exactly its later arrival (0.8 to 6.9 s). Fix: `{CET}` only ratchets the clock forward to the freshest beacon, steps back only on a quorum, and yields to GPS or NTP for a holdover time.**

Recommendation: implement Part A (forward-only `{CET}` filter, pure header, host tests) first, then Part B (holdover after higher sources, `clock_drift.py` `[CET]` capture). Gateway behaviour stays unchanged. Backlog row `CET-SCATTER-01`; analysis only, no source changed.

## Key facts

### The `{CET}` apply path today

- `{CET}` is applied in `sendDisplayText()` (`src/loop_functions.cpp:3067`), reached from LoRa RX via `queueDisplayText()` (`src/lora_functions.cpp:2222-2231`) and from the gateway UDP path (`src/esp32/udp_frame_esp32.cpp:392-395`, `src/nrf52/udp_frame_nrf52.cpp:330`). Same code on ESP32 and nRF52.
- Gate: only when `!bRTCON && !posinfo_fix && !bNTPDateTimeValid` (`src/loop_functions.cpp:3075`). INSTRUMENT `--timesrc cet` bypasses it (`:3073`, `clockInstrCetOnly()`).
- The payload has whole seconds only (`src/cet_age.h:63-75`) and carries UTC (`src/loop_functions.cpp:3084-3087`). It is set as is at arrival: no transit-age correction.
- CET-AGE-01 guard: `cetStepAllowed()` (`src/cet_age.h:155-160`) refuses a step more than 5 s back within 30 min of the last accepted beacon. Call site and the `[CET];skip;back_s;<n>;since_s;<n>` line: `src/loop_functions.cpp:3099-3126`.
- Every source ends in `Clock::setCurrentTime()` (`src/clock.cpp:549-577`), whole seconds, then `SetClock()` re-anchors the second at `millis()` (`src/clock.cpp:456-465`). The fraction of the arrival second is lost on every set.
- CLK-01 F3: the free-running clock now carries the tick remainder (`clockTickStep()`, `src/clock_tick.h:36`, called at `src/clock.cpp:114`). Night run: `lost_ms` at most 9 ms, no growth. A forward-only filter depends on this.

### The other sources

| Source                   | Set site                                                                                                                           | Gate today                                                                                                                                       |
| ------------------------ | ---------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------ |
| GPS with fix             | ESP32 `src/gps_functions.cpp:1138-1142`; nRF52 `src/nrf52/nrf52_main.cpp:3414-3416`; T-Deck Pro `src/t-deck-pro/tdeck_pro.cpp:635` | none: every plausible GPS read sets the clock. Fix flag: `src/gps_functions.cpp:1106-1108`, cleared at `:1124`; nRF52 `nrf52_main.cpp:3385-3392` |
| GPS without fix          | ESP32 only, `src/gps_functions.cpp:1281-1287`                                                                                      | only while `!bNTPDateTimeValid`. Rank comment at `:1280`: "GPS mit Fix, NTP, GPS ohne Fix"                                                       |
| NTP, ESP32               | `src/esp32/esp32_main.cpp:4283-4328`, every loop pass                                                                              | only with an IP and without GPS fix (`:4283`). Valid flag cleared on no IP (`:4380-4382`) and every 5 min while a phone is ready (`:4822-4825`)  |
| NTP, nRF52               | `src/nrf52/nrf52_main.cpp:1486-1545`, `HAS_NRF_ETH` only                                                                           | not gated by GPS fix (platform difference). Cleared at `:1538`, `:1544`, `:2281`                                                                 |
| RTC                      | ESP32 `src/esp32/esp32_main.cpp:4332-4372`; nRF52 `src/nrf52/nrf52_main.cpp:1459-1482`                                             | only without IP and without GPS fix. `bRTCON` is set once and never cleared (`src/rtc_functions.cpp:60`)                                         |
| Phone                    | `src/phone_commands.cpp:394-399`                                                                                                   | none. The `{CET}` gate does not consider phone time (`src/loop_functions.cpp:3069-3070`)                                                         |
| `--settime`, soft serial | `src/command_functions.cpp:1381`, `src/softser_functions.cpp:98`                                                                   | none                                                                                                                                             |
| Boot restore             | `src/time_functions.cpp:143`, `SetClock()` directly, no `CLK_SRC`                                                                  | a guess from the saved time; S4 showed it 550 s behind                                                                                           |

Every named source announces itself with `CLK_SRC("<name>")` right before its set. The macro is a no-op outside INSTRUMENT builds (`src/clock_instrument.h:17`, `:45`).

### The gateway path

- A gateway logs `[CET];gw_rx` (`src/esp32/udp_frame_esp32.cpp:288-296`) and applies the UDP copy through the same gate. With NTP the gate keeps it out.
- It injects the beacon into LoRa only with a trusted clock and an age within 150 s (`src/gw_udp_ring.h:37-43`, `src/cet_age.h:120-136`). Relays apply the same test (`src/lora_functions.cpp:2617-2619`).
- A node without a trusted clock never relays `{CET}` (`src/cet_age.h:129`). The payload is never rewritten, so a copy's age is server leg plus every queue wait.

## Root-cause assessment

### Proven (night run 2026-11-15/16, `tools/bench/runs/harness/clock-*-20261115-*.jsonl`)

Offset below means `new` epoch of each `[CLOCK];set;src;CET` minus the host stamp minus `node_utcoff` (1 h).

| Run, node                  | Offsets after each CET set (s)                                    | Range          |
| -------------------------- | ----------------------------------------------------------------- | -------------- |
| S3 DK5EN-1 (gateway, WLAN) | -0.77 -1.24 -0.94 -0.66 -0.49 -0.32 -0.15 -0.18 -0.23 -0.84 -0.46 | -1.24 to -0.15 |
| S3 DK5EN-92 (T-Beam)       | -2.47 -8.18 -2.63 -5.00 -1.29 -3.17 -0.95 -3.94 -2.21 -5.95 -2.18 | -8.18 to -0.95 |
| S4 DK5EN-1 (offline)       | -4.71 -1.47 -2.18 -7.63 -5.94 -1.67                               | -7.63 to -1.47 |

- **Every CET set leaves the clock behind true time.** No offset is positive. The clock is set to the beacon's age, so it lags by that age.
- **One origin in S3.** Per beacon, both nodes set the same payload epoch (difference 0 on all 11). The T-Beam set 0.80 to 6.94 s later, and its offset is lower by exactly that lag (residual 0.00 s on all 11).
- **The sign alternation is arithmetic, not two origins.** Each delta is the previous age minus the new age. Differences of independent ages alternate in sign; the running sum stays bounded (T-Beam: -7.6 to -0.4 s).
- **The CET-AGE-01 boundary works as designed.** -5716 and -5449 ms are whole-second -5 steps plus a fraction, which `cetStepAllowed()` accepts (`src/cet_age.h:159`).
- **The freshest beacon is the best estimate.** Delay only makes a payload older, so the largest payload seen is closest to true time.

### Hypothesis (not proven)

- The T-Beam heard the LoRa copy that a gateway sent after a CSMA queue wait of 0.8 to 6.9 s. Which transmitter (bench gateway or another) is open; `[CET];rx` hop and path would show it, but `clock_drift.py` did not record it.
- S4 has no reference node, so one origin is not shown there. The spread (-7.63 to -1.47 s) matches S3.
- A second server stream with 20 to 115 s old beacons exists (`docs/rca-cet-age-01-20261108.md`). It did not appear as a set in this run.

### Platform check

The decision code is shared (`src/loop_functions.cpp`), so ESP32 and nRF52 behave alike for `{CET}`. Differences sit in the higher sources: nRF52 NTP needs Ethernet and is not gated by GPS fix; the GPS-without-fix branch exists only on ESP32.

## Priority and overtake rules

Higher rank always wins. A lower source may step the clock only after every higher rank's holdover has run out. Within its own rank, `{CET}` follows the filter below. The order among GPS, NTP, RTC and phone stays as the code has it today; only the `{CET}` side changes.

| Source                     | Rank | May step a clock last set by                                | Holdover after its last set                        | Deadband                                                                                                 | Log line                                                 |
| -------------------------- | ---- | ----------------------------------------------------------- | -------------------------------------------------- | -------------------------------------------------------------------------------------------------------- | -------------------------------------------------------- |
| GPS (fix; module time too) | 5    | any (unchanged)                                             | 6 h                                                | none, sets every read (unchanged)                                                                        | `[CLOCK];set;src;GPS` (INSTRUMENT)                       |
| NTP                        | 4    | any except GPS with fix on ESP32 (unchanged)                | 6 h                                                | none, sets every loop pass (unchanged)                                                                   | `[CLOCK];set;src;NTP`                                    |
| Phone, `--settime`, serial | 3    | any (unchanged)                                             | 1 h                                                | none (unchanged)                                                                                         | `[CLOCK];set;src;PHONE\|CMD\|SER`                        |
| RTC                        | 2    | boot restore and `{CET}` (unchanged)                        | 6 h (in practice permanent, `bRTCON` never clears) | none (unchanged)                                                                                         | `[CLOCK];set;src;RTC`                                    |
| `{CET}` over LoRa or UDP   | 1    | boot restore; `{CET}`; any higher source after its holdover | none                                               | forward: at least 1 s. Back: never, except a quorum of 3 beacons each more than 5 s behind within 30 min | `[CET];apply`, `[CET];hold`, `[CET];skip;back_s` (below) |
| Boot restore               | 0    | nothing (internal)                                          | none                                               | n/a                                                                                                      | `[CLOCK];set;src;OTHER`                                  |

### The `{CET}` filter

| Case                                                      | Verdict  | Clock                             |
| --------------------------------------------------------- | -------- | --------------------------------- |
| No reference yet (first beacon since boot, no higher set) | `first`  | set to the payload, any direction |
| `delta_s >= +1`                                           | `fwd`    | set to the payload                |
| `-5 <= delta_s <= 0`                                      | `band`   | unchanged; quorum counter reset   |
| `delta_s < -5`, fewer than 3 such beacons in 30 min       | `back`   | unchanged (the CET-AGE-01 skip)   |
| `delta_s < -5`, third such beacon in a row within 30 min  | `quorum` | set to the current payload        |

`delta_s` is payload UTC minus node clock UTC, whole seconds, as computed today (`src/loop_functions.cpp:3104-3112`). A forward step can never overshoot true time, because a payload is never newer than true time.

### Answers to the operator's points

- **Slew or step:** step. The clock counts whole seconds and `setCurrentTime()` takes whole seconds; a slew needs ms phase access in `CheckEvent()`, which CLK-01 just fixed. With forward-only steps of 1 to 2 s, a slew gains little.
- **Filtering:** the forward ratchet is a running maximum of beacon payloads, which is the right estimator when delay only adds. A median would centre on the average delay (about 3.5 s late on the T-Beam).
- **Boot with no source:** the boot restore (`src/time_functions.cpp:143`) is a guess. The first `{CET}` sets the clock in any direction, as today. A stale first beacon is corrected by the next fresher one.
- **CET-AGE-01:** subsumed. A beacon more than 5 s behind is still never applied alone, and the `[CET];skip;back_s` line keeps its prefix. The 30-min "accept anything afterwards" release goes: with forward-only steps a good clock can stay unstepped for hours.
- **CLK-01:** the filter needs F3 (no tick loss). The S3/S4 criterion `|delta| <= 2 s` holds after the first beacon, except one possible convergence step (see the bench recheck).
- **Gateway:** unchanged. Its NTP clock (rank 4) keeps `{CET}` out; it logs `[CET];hold;why;rank;by;NTP` instead of staying silent. It relays the payload unchanged.

### Log lines

| Line                                                                               | When                                                                   |
| ---------------------------------------------------------------------------------- | ---------------------------------------------------------------------- |
| `[CET];apply;why;<first\|fwd\|quorum>;delta_s;<n>;src;<call>;ms;<millis>`          | the beacon set the clock (Part A)                                      |
| `[CET];hold;why;band;delta_s;<n>;src;<call>;ms;<millis>`                           | inside the deadband (Part A)                                           |
| `[CET];skip;back_s;<n>;since_s;<n>;quorum;<k>;src;<call>;ms;<millis>`              | more than 5 s behind, quorum `k` of 3 (Part A; CET-AGE-01 prefix kept) |
| `[CET];hold;why;rank;by;<GPS\|NTP\|MANUAL\|RTC>;held_s;<n>;src;<call>;ms;<millis>` | a higher source or its holdover blocks (Part B)                        |

`src` is `aprsmsg.msg_source_call`, the origin pseudo-call of the stream, which answers the origin question on the bench. One line per beacon, about one per 10 min.

## Options

| Choice                  | Option                                                                                                  | For                                                         | Against                                                                                                     | Pick  |
| ----------------------- | ------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------- | ----- |
| CET estimator           | F1 forward ratchet plus back quorum                                                                     | matches "delay only adds"; replay: T-Beam 3 sets, S4 2 sets | a fast crystal can sit up to 5 s ahead before the quorum pulls it back                                      | yes   |
|                         | F2 median of the last 5 beacons                                                                         | robust to one outlier                                       | centres on the mean delay (about 3.5 s late); still steps every beacon                                      | no    |
|                         | F3 deadband only (ignore steps under 3 s)                                                               | one line                                                    | keeps the clock at whichever beacon came first; drifts with the delay mix                                   | no    |
| Quorum back step target | Q1 the current payload                                                                                  | no epoch-to-fields path needed                              | may land on the stalest of the three; the next fresher beacon ratchets forward                              | yes   |
|                         | Q2 the least-behind payload of the quorum                                                               | smaller back step                                           | needs a set-from-epoch helper at the call site                                                              | no    |
| Holdover hook           | H1 extend `CLK_SRC()` to note the source in all builds                                                  | one header, all set sites already call it                   | INSTRUMENT `--timesrc` ignores sets after the note; the `--timesrc cet` bypass covers it                    | yes   |
|                         | H2 one call at every set site                                                                           | explicit                                                    | 8 sites in 6 files                                                                                          | no    |
| Gateway                 | G1 unchanged                                                                                            | no wire or upstream change                                  | queue delay stays in the payload                                                                            | yes   |
|                         | G2 re-stamp the payload at LoRa TX from the trusted clock (`src/lora_functions.cpp:3011-3035` finds it) | removes the 0.8 to 6.9 s queue delay at the source          | changes what the payload means, hides age from the 150 s gate, every gateway must run it; upstream decision | later |
| Slew                    | S1 step                                                                                                 | no clock core change                                        | visible 1 s jumps, rare with F1                                                                             | yes   |
|                         | S2 slew via `clockTickStep()`                                                                           | smooth display                                              | touches the CLK-01 fix; ms phase is not exposed (`u32Start_m` is protected, `src/clock.h:49-60`)            | no    |

## Implementation plan

Two ordered parts, each about 25 minutes. Part B's firmware half edits the same `{CET}` block as Part A, so it runs after A. Part B's tool half (`clock_drift.py`) has disjoint files and may run in parallel with A.

No pio on a busy tree: one `pio` process at a time (CLAUDE.md). Never bare `pio test`.

### Part A: forward-only `{CET}` filter

Exclusive files:

- `src/cet_filter.h` (new, pure, header-only, no Arduino)
- `src/loop_functions.cpp` (include near `:34`, the `{CET}` block `:3097-3127` only)
- `test/test_cet_age/test_cet_age.cpp` (new cases, retarget the anchors of `test_step_guard`)

`src/cet_filter.h`:

```cpp
#ifndef CET_FILTER_H
#define CET_FILTER_H
#include <stdint.h>
#include "cet_age.h"          // CET_BACKSTEP_MAX_S (5), CET_BACKSTEP_WINDOW_MS (30 min)

#ifndef CET_FWD_MIN_S
#define CET_FWD_MIN_S 1
#endif
#ifndef CET_BACK_QUORUM
#define CET_BACK_QUORUM 3
#endif

enum CetVerdict : uint8_t { CET_V_FIRST = 0, CET_V_FWD, CET_V_QUORUM, CET_V_BAND, CET_V_BACK };

struct CetFilter {
    bool     have_ref;                    // clock set by {CET} or a higher source since boot
    uint8_t  n_back;                      // consecutive beacons more than 5 s behind
    uint32_t back_ms[CET_BACK_QUORUM];    // their millis()
};

static inline void cetFilterInit(CetFilter *f);         // all zero
static inline void cetFilterNoteRef(CetFilter *f);      // have_ref = true, n_back = 0 (Part B caller)
static inline CetVerdict cetFilterDecide(CetFilter *f, int32_t delta_s, uint32_t now_ms);
static inline bool cetVerdictApplies(CetVerdict v);     // FIRST, FWD, QUORUM
static inline const char *cetVerdictName(CetVerdict v); // "first","fwd","quorum","band","back"
#endif
```

`cetFilterDecide()` rules, in this order:

1. `!have_ref`: return FIRST, set `have_ref`, `n_back = 0`.
2. `delta_s >= CET_FWD_MIN_S`: FWD, `n_back = 0`.
3. `delta_s >= -CET_BACKSTEP_MAX_S`: BAND, `n_back = 0`.
4. Else drop `back_ms` entries with `(uint32_t)(now_ms - back_ms[i]) > CET_BACKSTEP_WINDOW_MS` (keep order), append `now_ms` (shift out the oldest when full). If `n_back >= CET_BACK_QUORUM`: QUORUM, `n_back = 0`. Else BACK.

Call site (`src/loop_functions.cpp`):

- File scope after the includes: `static CetFilter s_cetFilter;` (zero-initialised; Part B needs file scope).
- Keep the parse, the `Year > 2023` test and the `cetDeltaS` computation (`:3097-3112`).
- Replace `cetAllow = cetStepAllowed(...)` with exactly `CetVerdict cetV = cetFilterDecide(&s_cetFilter, (int32_t)cetDeltaS, millis());`.
- If the epoch conversion fails (clock never set), use `CET_V_FIRST` and call `cetFilterNoteRef(&s_cetFilter)`.
- Gate the existing set with exactly `if(cetVerdictApplies(cetV))`; keep `CLK_SRC("CET")`, `setCurrentTime`, `cTimeSource`, `cetHaveLast`, `cetLastAcceptMs`.
- Print the three Part A lines from the log table. `since_s` stays `cetSinceMs / 1000`.
- `cetStepAllowed()` keeps its definition and its value test; it loses its only caller.

Tests (env `native_cet_age`, `platformio.ini:1108`, run `pio test -e native_cet_age`). Before the fix the suite fails to compile (`cet_filter.h` missing), as in CET-AGE-01.

Every case except `first_any_direction` and the replays starts from a filter that already has a reference (one FIRST call).

| Case                                   | Asserts                                                                                                                                                                           |
| -------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `test_filter_first_any_direction`      | fresh filter: -114 gives FIRST; another fresh filter: +550 gives FIRST                                                                                                            |
| `test_filter_forward`                  | after a reference: +1 and +25 give FWD                                                                                                                                            |
| `test_filter_band`                     | 0, -1, -4, -5 give BAND (CET-AGE-01 boundary kept); -6 gives BACK                                                                                                                 |
| `test_filter_quorum`                   | -20 at 0, 600 000 and 1 200 000 ms: BACK, BACK, QUORUM; the next -20 gives BACK again                                                                                             |
| `test_filter_quorum_needs_consecutive` | -20, -20, then 0 (BAND), then -20: BACK                                                                                                                                           |
| `test_filter_fwd_resets_quorum`        | -20, -20, +2 (FWD), -20: BACK                                                                                                                                                     |
| `test_filter_quorum_window`            | -20 at 0, 600 000, 1 800 001 ms: third is BACK (first expired)                                                                                                                    |
| `test_filter_millis_wrap`              | -20 at 0xFFFFF000, 0xFFFFF000 + 600 000 (wrapped), + 1 200 000: QUORUM                                                                                                            |
| `test_filter_replay_tbeam_s3`          | replay below with ages 2470 8180 2630 5000 1290 3170 950 3940 2210 5950 2180 ms: verdicts FIRST BACK BAND BAND FWD BAND FWD BAND BAND BAND BAND, 3 applies, final offset -1000 ms |
| `test_filter_replay_heltec_s4`         | ages 4710 1470 2180 7630 5940 1670: FIRST FWD BAND BACK BAND BAND, 2 applies, final offset -2000 ms                                                                               |
| `test_old_rule_oracle_tbeam`           | same replay with `cetStepAllowed(d, have, 607000)`: 10 applies of 11 (pins why the old rule fails; green before and after)                                                        |
| `test_step_guard` (changed)            | anchors retargeted to `cetFilterDecide(&s_cetFilter, (int32_t)cetDeltaS` and `if(cetVerdictApplies(cetV))`, order call, gate, `setCurrentTime` unchanged                          |

Replay harness (int64 ms): `T = 1794779000000 + k * 607000`; `payload_s = (T - age_ms) / 1000`; `now_s = (T + c) / 1000`; `delta = payload_s - now_s`; start `c = 0`, fresh filter; on an applying verdict `c = payload_s * 1000 - T`. The expected values above come from this exact harness.

### Part B: holdover and the bench tool

Exclusive files:

- `src/time_rank.h` (new, pure)
- `src/clock_instrument.h` (the two `CLK_SRC` defines, `:17` and `:45`, plus one declaration)
- `src/loop_functions.cpp` (the `{CET}` gate `:3070-3076` and one glue function)
- `test/test_cet_age/test_cet_age.cpp` (new cases)
- `tools/bench/clock_drift.py`, `tools/bench/test_clock_drift.py`

`src/time_rank.h`:

```cpp
enum TimeRank : uint8_t { TR_NONE = 0, TR_CET, TR_RTC, TR_MANUAL, TR_NTP, TR_GPS, TR_COUNT };
// TIME_HOLD_GPS_MS, TIME_HOLD_NTP_MS, TIME_HOLD_RTC_MS = 6 h; TIME_HOLD_MANUAL_MS = 1 h (#ifndef, overridable)
struct TimeRankState { uint32_t last_ms[TR_COUNT]; uint8_t seen; };   // bit r set once rank r was noted

static inline TimeRank timeRankOf(const char *clk_src);   // "GPS","NTP","RTC","CET"; "PHONE","CMD","SER" -> TR_MANUAL; else TR_NONE
static inline uint32_t timeRankHoldoverMs(TimeRank r);    // 0 for TR_NONE, TR_CET
static inline void timeRankNote(TimeRankState *s, TimeRank r, uint32_t now_ms);   // ignores r < TR_RTC
// Highest rank r >= TR_RTC with seen bit and (now_ms - last_ms[r]) < holdover(r); clears expired seen
// bits; *held_ms = now_ms - last_ms[r]. TR_NONE if none.
static inline TimeRank timeRankBlockingCet(TimeRankState *s, uint32_t now_ms, uint32_t *held_ms);
static inline const char *timeRankName(TimeRank r);       // "GPS","NTP","MANUAL","RTC","CET","NONE"
```

Glue:

- `src/clock_instrument.h`: declare `void timeRankNoteSrc(const char *src);` outside the `#if`. INSTRUMENT: `#define CLK_SRC(s) (clockInstrSrc(s), timeRankNoteSrc(s))`. Normal: `#define CLK_SRC(s) timeRankNoteSrc(s)`.
- `src/loop_functions.cpp`: `static TimeRankState s_timeRank;` and `void timeRankNoteSrc(const char *s) { timeRankNote(&s_timeRank, timeRankOf(s), millis()); }`.
- `{CET}` gate: unless `clockInstrCetOnly()` (INSTRUMENT), `by` is the live flag rank (`posinfo_fix` GPS, else `bNTPDateTimeValid` NTP, else `bRTCON` RTC), else `timeRankBlockingCet()`. If `by != TR_NONE`: `cetFilterNoteRef(&s_cetFilter)`, print `[CET];hold;why;rank;...`, return. Else run Part A.

Tests in `native_cet_age` (compile failure before the header exists):

- `test_rank_of`: the mapping above, `NULL` and `"OTHER"` give TR_NONE.
- `test_rank_holdover_edges`: NTP noted at 0: blocking at 6 h minus 1 ms, not at 6 h; MANUAL at 1 h likewise.
- `test_rank_highest_wins`: GPS at 0, NTP at 1 h, query at 2 h: TR_GPS.
- `test_rank_cet_never_blocks`: CET and NONE notes never block.
- `test_rank_wrap`: note at 0xFFFFF000, query at 0x00001000: blocked.
- `test_rank_hook_anchor`: `src/clock_instrument.h` contains `timeRankNoteSrc(s)` twice (both branches), using `fileContains()` (`test/test_cet_age/test_cet_age.cpp:128`).

`clock_drift.py` extension:

- Add `CET` to `EVENT_RE` (`tools/bench/clock_drift.py:85`) and to the prefilter in `NodeSink.line()` (`:365`).
- `parse_event()`: for tag CET, `kind = tok[0]`, fields from `_pairs(args)`; numeric values via `_num()`, `na` stays a string. Any kind is recorded, never malformed.
- `NodeStats`: keep a CET event list. Summary keys: `cet_by_kind`, `cet_by_why` (apply and hold), `cet_rx_by_src` (origin call to count), `cet_skip_back_count`, `cet_apply` (t, why, delta_s, src).
- Offline boot: `BOOT_READY_OFFLINE = b"[WIFI]...no connection possible"` (`src/esp32/esp32_main.cpp:2559`). `run_live(..., offline_ok: bool = False)` releases on it only when `offline_ok`; `live_worker` and `main` pass `args.offline_ok`.

Pytest (`tools/bench/test_clock_drift.py`, run by stage 2 `pytest -q tools/bench`, `tools/regression.sh:199-200`):

- `test_cet_lines_recorded`: replay `[CET];rx`, `gw_rx`, `apply`, `hold`, `skip` lines; counts per kind, origin and why; `malformed_lines == 0`.
- `test_cet_skip_back_keeps_old_format`: the CET-AGE-01 form `[CET];skip;back_s;7;since_s;60` still counts.
- `test_offline_boot_releases_on_wifi_fail`: like `test_startup_waits_for_boot_ready` (`:282`), the WiFi marker at 8 s, `offline_ok=True`: command sent.
- `test_offline_marker_needs_offline_ok`: same stream without `offline_ok`: `BootWaitTimeout`.

### Gate and bench recheck

- Gate: `tools/regression.sh --stage 1,2`; every board env builds (the `CLK_SRC` change reaches all of them).
- Bench, INSTRUMENT images: S3 2 h on DK5EN-92 (`--timesrc cet`, `--gps off`) and DK5EN-1; S4 1 h on DK5EN-1 offline with `--offline-ok`. 2 dBm, mesh off, identity guard first.
- PASS for CLK-01 S3/S4: after the first CET set, every `[CLOCK];set;src;CET` has `|delta_ms| <= 2000`, with at most one forward convergence step inside the first 3 beacons (the S4 replay predicts +3 s). No CET set below -1000 ms unless it follows `[CET];apply;why;quorum`.
- Origin check: `cet_rx_by_src` in the summary names the origins heard.
- Holdover check (Part B): build with `-D TIME_HOLD_GPS_MS=600000`, `--timesrc all`, GPS on until fix, then `--gps off`. Expect `[CET];hold;why;rank;by;GPS` for 10 min, then `band` or `fwd`, no backward set.

## Not verified / gaps

- Which transmitter's LoRa copy the T-Beam heard, and that the 0.8 to 6.9 s lag is a queue wait. Needs `[CET];rx` hop and path, which Part B records.
- S4 has no reference node; one origin is shown only for S3.
- The per-beacon match uses host stamps of two serial ports; equal port latency is assumed.
- Free-running crystal drift after F3 is not measured (S5 after the fix was not run). The 6 h holdover assumes at most 20 ppm (0.43 s in 6 h).
- nRF52 was not benched under `--timesrc cet`; its decision code is the same, its sources differ.
- No compile run: the claim that no native env compiles a `CLK_SRC` call site rests on a grep of `platformio.ini`.
- That `[WIFI]...no connection possible` prints without debug flags is taken from the night-run wrapper, not from `printlndeb()` itself.
- The phone path stays unconditional (`src/phone_commands.cpp:394-399`); out of scope.
- The replay uses a whole-second model of the node clock; the bench will show sub-second phase effects.

## Open operator questions

1. Holdover: 6 h after GPS or NTP, 1 h after phone or `--settime`. Shorter means `{CET}` takes over sooner after a WLAN drop; longer trusts the crystal more.
2. GPS module time without a fix (ESP32, `src/gps_functions.cpp:1281-1287`) uses `CLK_SRC("GPS")`, so it would block `{CET}` like a fix. Keep that (recommended, it already sets the clock every read), or rank it below `{CET}`?
3. CLK-01 criterion: accept one convergence step above 2 s within the first 3 beacons after boot?
4. Gateway re-stamp (G2) as a later upstream proposal, or drop it?
5. Quorum of 3 beacons more than 5 s behind within 30 min: keep, or require 4?
