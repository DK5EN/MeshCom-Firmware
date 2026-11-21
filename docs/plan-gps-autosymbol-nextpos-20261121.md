# GPS autosymbol and NEXT position counter: fix implementation plan

Date: 2026-11-21. Branch: `fork-dev` at `067d29cd`. Problem files (read-only, in the Telegram triage store):
`problems/P-0166`, `P-0167`, `P-0383`, `P-0081`. Their RCA cites `src/` lines at pin `96aaa67c72` (P-0167 at
`8913b2cbe0`); every line below was re-read at HEAD. Nothing is implemented, no `pio` command was run, nothing was
sent anywhere. Style model: `docs/meshcom-lora-qrt-fix-plan-20261018.md`.

## 1. Overview

| Ticket | Defect                                                        | Fix                                                       | Backlog Id      | Platforms                                  | Upstream                      |
| ------ | ------------------------------------------------------------- | --------------------------------------------------------- | --------------- | ------------------------------------------ | ----------------------------- |
| P-0383 | nRF52 never restores `bGPSAutosymbol` at boot                 | one line in `nrf52_main.cpp`                              | GPS-AUTOSYM-01  | nRF52 only (RAK4631, T114, T-Echo)         | upstream PR (same gap there)  |
| P-0166 | stationary early return runs before the autosymbol block      | extract symbol logic to `src/autosymbol.h`, call it first | GPS-AUTOSYM-02  | shared code, ESP32 and nRF52               | upstream PR (same code there) |
| P-0167 | statics start at 0: first 120 s after boot count as car speed | "never seen" state in the same header                     | GPS-AUTOSYM-03  | shared code, ESP32 and nRF52               | upstream PR (same code there) |
| P-0081 | NEXT counter wraps (unsigned subtraction), six sites          | one clamped helper `src/posinfo_next.h`, six call sites   | POSINFO-NEXT-01 | shared code, both platforms, 3 display UIs | upstream PR (same code there) |

P-0081 part 2 (duplicate positions at rest) has no fix in this plan: no log separates the candidates (section 6).

Order and coupling:

- GPS-AUTOSYM-02 creates `src/autosymbol.h`; GPS-AUTOSYM-03 changes its state and so follows it (same files, same
  suite). They can ship in one commit, but keep two backlog rows and two red runs, because the defects differ.
- GPS-AUTOSYM-01 is independent of everything. It does make the other two reachable on nRF52 after a reboot, so land it
  first or with them.
- POSINFO-NEXT-01 touches different code (display of the countdown) and is independent.
- Disjoint file sets: AUTOSYM-01 `src/nrf52/nrf52_main.cpp`; AUTOSYM-02/03 `src/loop_functions.cpp`, `src/autosymbol.h`;
  NEXT `src/command_functions.cpp`, `src/web_functions/web_functions.cpp`, `src/t-deck/lv_obj_functions.cpp`,
  `src/t-deck-pro/ui_deckpro.cpp`, `src/loop_functions.cpp` (line 2075 only, far from the autosymbol block, but the same
  file as AUTOSYM-02: serialise those two or give them one implementer).

New suites go into the `[env:native]` `test_filter` list in `platformio.ini` (header-only seams need no
`build_src_filter` entry) and into `docs/test-suite-map.md`. Run each with `pio test -e native -f <suite>`; never bare
`pio test`, one `pio` process at a time.

## 2. Shared facts (checked at HEAD `067d29cd`)

- Changes since the pin: `git log --oneline 96aaa67c72..HEAD -- src/loop_functions.cpp src/command_functions.cpp
src/nrf52/nrf52_main.cpp src/esp32/esp32_main.cpp src/web_functions/web_functions.cpp
src/t-deck/lv_obj_functions.cpp src/gps_functions.cpp` lists `8b4d509b`, `ce05f38c`, `f4af55b0`, `9283e6a3`
  (ETH-06, AppLink, CMD-DASH-01, PONG-SIGN-01). None touches the code in this plan:
  `git log -S<sym> 96aaa67c72..HEAD -- src` is empty for `bGPSAutosymbol`, `last_car_speed_ts`, `posinfo_interval`,
  `posinfo_timer_min`, `posinfo_interval * 1000`. Only line numbers moved (+4 to +14 against the pin).
- Provenance (all ancestors of HEAD and of `upstream/dev`, none reverted: `git log -i --grep='^Revert'` has no hit on
  symbol, posinfo or gps code): `d65dacef` (2025-12-11, "Gps fix (#21) (#22)") introduced the stationary gate and the
  two statics; `0e6fcb23` (2026-04-08, "v4.35p GPS aprs symbol auto/fix-switch") added the `bGPSAutosymbol` block and
  the ESP32 restore; `5b29f284` (2025-08-15) and `ae20661d` (2024-02-04) wrote the NEXT expressions.
- `upstream/dev` is `5698913a` (2026-11-21, PR #1285 merged) and is an ancestor of HEAD.
- `setSMartBeaconing()` (`src/loop_functions.cpp:6736`, declared `src/loop_functions.h:178`) is the only function that
  touches the symbol state. Callers: `src/gps_functions.cpp:1271` (every board with `ENABLE_GPS`, which
  `src/configuration_default.h:124` defines unless a variant says `ENABLE_GPS_DISABLED`: all ESP32 GPS boards, Heltec T114
  and T-Echo), `src/t-deck-pro/tdeck_pro.cpp:702` (T-Deck Pro), `src/nrf52/nrf52_main.cpp:3452` (`getGPS()` under
  `ENABLE_RAK_GPS`, RAK4631 only).
- `loop_functions.cpp` is compiled by no native env (grep of `platformio.ini` for `+<loop_functions.cpp>` is empty), which
  is why both fixes need a header seam.

## 3. P-0383 / GPS-AUTOSYM-01: nRF52 does not restore the autosymbol opt-in

Symptom. `--gps autosymbol` sets `bGPSAutosymbol` and bit 0x1000 of `node_sset3` and saves
(`src/command_functions.cpp:2595-2610`; `fixsymbol` clears both at `:2613`). After a reboot the nRF52 runs with the flag
false (default `src/loop_functions.cpp:430`) while the stored setting still says on. The operator must resend the command
after every boot. Code reading only, not run on hardware.

Root cause at HEAD.

- ESP32 restores: `src/esp32/esp32_main.cpp:1629` `bGPSAutosymbol = meshcom_settings.node_sset3 & 0x1000;`.
- nRF52 does not: the `node_sset3` restore block `src/nrf52/nrf52_main.cpp:813-821` handles 0x0002, 0x0004, 0x0008,
  0x0010, 0x0020, 0x0080, 0x2000, 0x4000 and has no 0x1000 line. `grep -n bGPSAutosymbol src/nrf52/nrf52_main.cpp` is
  empty. The only other assignments are the two command writers.
- The app settings sync does not repair it: `nsetdoc1["ASYM"] = bGPSAutosymbol` (`src/command_functions.cpp:8887`) only
  reports the flag; the problem file's `git grep ASYM` found no reader, and nothing in the firmware parses it back.
- Changed since the pin: no (the ESP32 line moved 1625 to 1629, the nRF52 block 818 to 820).

Platforms. nRF52 only; the ESP32 mains (all boards, T-Deck Pro included) have the line. `nrf52_main.cpp` serves RAK4631,
Heltec T114 and T-Echo, so all three are affected. No `#ifdef` is needed: `bGPSAutosymbol` is defined unconditionally
(`src/loop_functions.cpp:430`, `extern` at `src/loop_functions_extern.h:162`), and the neighbouring `bGPSUBLOX`/`bGPSL76K`
lines in the same block are unguarded too.

Fix design (minimal). Add after line 821 in `src/nrf52/nrf52_main.cpp`:

```cpp
bGPSAutosymbol = meshcom_settings.node_sset3 & 0x1000;   // --gps autosymbol (as on ESP32)
```

Rejected: moving the whole `node_sset3` restore into a shared function (the problem file's option). It rewrites the boot
path of both platforms for a one-bit gap, and `--gps autosymbol` is an if-chain command, not a row of the toggle table
(`src/command_functions.cpp:712ff`), so a table-driven restore would not cover it either. Reuse: nothing to reuse; this is
the ESP32 line copied into the sibling block (the established twin pattern, see the `bGPSUBLOX` line with "as on ESP32").

Test (fails before, passes after). New suite `test/test_sset3_restore/test_sset3_restore.cpp`, env `native`, run
`pio test -e native -f test_sset3_restore`. Source-pin style, the precedent is `test_ping_budget`
(`test/test_ping_budget/test_ping_budget.cpp`, `slurp()` via `__FILE__`).

- `test_both_mains_restore_autosymbol_bit`: for `src/esp32/esp32_main.cpp` and `src/nrf52/nrf52_main.cpp`, the file must
  contain `bGPSAutosymbol = meshcom_settings.node_sset3 & 0x1000`. Fails today on the nRF52 file (string absent), with the
  path as the assertion message. Passes after.
- `test_writers_use_the_same_bit`: `src/command_functions.cpp` contains `bGPSAutosymbol = true;` followed by
  `node_sset3 |= 0x1000` and `bGPSAutosymbol = false;` followed by `node_sset3 &= ~0x1000`. Green before and after; it
  stops a later bit change from breaking only the writers.

Why not a behavioural test: `esp32loop`/`nrf52setup` are not host-compilable; the restore is straight-line code in a
board `setup()`. The pin is the honest seam. Residual risk: a pin proves the text, not the run; the bench step covers that.

Bench (RAK4631 from the bench fleet, load `/bench` first; no TX involved, `--pos` only prints). Steps: `--gps autosymbol`,
`--reboot`, `--pos`; expect `...SYMB: / <c> ..Auto on` (the line is `src/command_functions.cpp:8625`). Then `--gps
fixsymbol`, `--reboot`, `--pos`; expect `Auto off`. Before the fix the first reboot gives `Auto off`. Identity guard first,
own call sign only.

Upstream. `git show upstream/dev:src/nrf52/nrf52_main.cpp | grep -n Autosymbol` is empty while
`upstream/dev:src/esp32/esp32_main.cpp:1535` has it: same gap, so upstream PR (minimal port: the one line; the source-pin
suite is fork-only unless the maintainers want tests, as with the earlier PRs).

Risks. Behaviour change for existing nRF52 nodes that ever sent `--gps autosymbol`: after the update they start
auto-switching on reboot, which is what they set. That is the intended contract. Combined with P-0166/P-0167 (below) the
car symbol in the first two minutes would become visible on nRF52 nodes, which is another reason to land AUTOSYM-03 in the
same release.

## 4. P-0166 / GPS-AUTOSYM-02: symbol stays on bicycle while stationary

Symptom. With `--gps autosymbol` the symbol goes runner to bicycle after one fast sample and does not go back to the
runner while the node stands still; it stays until the node moves again. Code reading, no log, no report with the opt-in
set (uid 3345 predates it).

Root cause at HEAD, `src/loop_functions.cpp`:

- `:6788` `if (speed_mps < 1.0)` with `:6790` `if(distance < 100)` and `:6797` `return posinfo_last_rate;`: the stationary
  early return.
- `:6805-6806` statics `last_car_speed_ts` / `last_bike_speed_ts`; `:6809-6817` timestamp updates (> 7.0 m/s car and bike,
  > 1.4 m/s bike).
- `:6823` `if(bGPSAutosymbol)`, the selection with the 120 s hysteresis `:6833-6846`; the only writer of `node_symcd` in
  the auto path is `:6845`.
- The early return at `:6797` precedes both the timestamp update and the selection. A stationary node (speed under 1.0 m/s
  and under 100 m from the last beacon position) never evaluates the expiry that would set `[`, so a `b` or `>` set by
  one sample sticks until a sample passes the early return.
- Second gate, same effect: the TRACK check at `:6738-6745` returns `POSINFO_INTERVAL` before anything, so with TRACK off
  no symbol is ever evaluated. With TRACK on this is the P-0166 case; a node whose TRACK was switched off while on the
  bicycle also keeps `b`. See the design decision below.
- Changed since the pin: no.

Platforms. The function is shared; both platforms call it (section 2). ESP32: every GPS board plus T-Deck Pro. nRF52:
RAK4631, T114, T-Echo, but only once the flag survives a reboot (GPS-AUTOSYM-01); before that, nRF52 exhibits it only
between sending the command and the next reboot. No `#ifdef` inside the affected block except the unrelated
`#if defined(ESP32)` WiFi relax (`:6856`), which comes after the selection and stays after it.

Fix design. Extract the hysteresis into a header with no Arduino dependency, `src/autosymbol.h` (static inline, the
`src/beacon_rate.h` pattern: pure, `uint32_t` arithmetic):

```cpp
#define AUTOSYM_BIKE_MPS 1.4
#define AUTOSYM_CAR_MPS 7.0
#define AUTOSYM_HOLD_MS 120000UL
struct AutoSymbolState { uint32_t car_ms; uint32_t bike_ms; bool car_seen; bool bike_seen; };   // zero-init = nothing seen
static inline void autoSymbolNote(AutoSymbolState *s, double speed_mps, uint32_t now_ms);
static inline char autoSymbolPick(const AutoSymbolState *s, char symid, char symcd, uint32_t now_ms);
```

`autoSymbolNote` is the `:6809-6817` update. `autoSymbolPick` returns the symbol to use: `symcd` unchanged unless
`symid == '/'` and `symcd` is one of `[`, `b`, `>`, otherwise `>` (car seen within the hold), `b` (bike seen), `[`. In
`setSMartBeaconing()`:

- keep one `static AutoSymbolState s_autoSym;` in place of the two statics;
- call `autoSymbolNote(&s_autoSym, speed_mps, millis())` and, under `if(bGPSAutosymbol)`, the pick (and the existing
  `printfdeb("Auto-Symbol switch to '%c'\n", ...)` on change) immediately after `speed_mps` is known and before the
  stationary check at `:6788`. The timestamp update stays unconditional as today (history is kept while the opt-in is
  off, so switching it on mid-trip behaves as before).
- delete the old blocks `:6804-6846`.

Behaviour that does not change: stationary samples never pass the note thresholds (speed under 1.0 is below 1.4), so
moving the note ahead of the early return cannot alter the timestamps; the rate logic, drift suppression, distance
one-shot and WiFi relax are untouched; `node_symcd` is still RAM-only and `--symcd` writes still win while the node
stands still on a non-switchable symbol.

Design decisions for the operator (recommendation in brackets):

1. TRACK off (`:6738`): evaluate the symbol before the TRACK gate too? [No. Keep autosymbol a TRACK-mode feature, place
   the call after the TRACK gate. The ticket is about the stationary order; changing the TRACK semantics is a separate
   decision. If wanted later it is a one-line move of the same call.]
2. On-air effect: after the symbol returns to `[` while stationary, the next beacon is up to `POSINFO_INTERVAL` (1800 s)
   away, so aprs.fi keeps the bicycle for at most that long. Set `posinfo_shot = true` on the transition to `[`? [No:
   minimal, no extra airtime; the symbol is correct from the next scheduled beacon, and the problem was "stays forever".]

Test. New suite `test/test_autosymbol/test_autosymbol.cpp`, env `native`, `pio test -e native -f test_autosymbol`.
Header comment in German or English per the neighbouring suites; the plan language is English.

- `test_stationary_after_bike_burst_returns_to_runner`: state zero, one note of 3.0 m/s at t = 200000, pick at 200000
  gives `b`; then notes of 0.0 m/s at 10 s steps up to t = 330000, picks give `b` from 210000 to 310000 (hold not expired: 310000
  is 110000 ms after the burst) and `[` from 320000 (exactly 120000 ms after the burst; the strict `< AUTOSYM_HOLD_MS`
  is already false) through 330000. Pure unit, green in the unit.
- `test_setsmartbeaconing_calls_pick_before_stationary_return` (the P-0166 pin): slurp `src/loop_functions.cpp`, take the
  body of `setSMartBeaconing(`, assert `autoSymbolPick(` occurs and its offset is smaller than the offset of
  `if (speed_mps < 1.0)`; assert the old statics are gone (`static unsigned long last_car_speed_ts` absent). Fails today:
  the call does not exist and the statics are present.
- `test_note_thresholds`: 1.4 exactly is not bike (strict `>`), 1.41 is bike, 7.0 exactly is not car, 7.01 is car, as in
  the current code.
- `test_not_applicable_symbols_untouched`: `symid != '/'` or `symcd` outside `[ b >` returns `symcd` unchanged.
- `test_millis_rollover`: note at `0xFFFFFF00`, pick at `0x00000100` (512 ms later) still inside the hold, pick at
  `0x0001D3BF` (119999 ms later) still inside, pick at `0x0001D3C0` (exactly 120000 ms later, `0xFFFFFF00 + 120000`
  wraps to `0x0001D3C0`) outside (uint32 subtraction, the idiom of `test_millis_rollover`).

Red run (do it, as the fork requires a before-run): first create the header with a verbatim copy of today's logic (state
zero-init, no seen flags, no call-order change in `loop_functions.cpp`), add the suite, run it. Expected red: the pin test
(call absent, statics present); the unit tests pass because the header re-evaluates every sample, which is exactly why the
pin exists. The pin is the only firmware-red test of P-0166. This verbatim-copy run is mandatory: the P-0166 and P-0167
helper tests go red only through it (without the header copy there is nothing to run them against), and the P-0166
source pin is the only test that fails against the unchanged firmware. Then make the `loop_functions.cpp` change and run green. Record both outputs in the report.

Bench (optional, field only: the sample speed cannot be produced on the desk). INSTRUMENT build of a GPS board
(`docs/bench-airgap.md`), `--airgap on` so the TRACK beacons do not go on air, `--track on`, `--gps autosymbol`,
`--symcd [`, `--gpsdebug on`. Walk or cycle outdoors above 1.4 m/s for a minute (log shows `Auto-Symbol switch to 'b'`),
then stand still more than 130 s; `--pos` must show `SYMB: / [`. Before the fix it stays `b`. Own call sign, TEST group
only if anything is ever sent, max 2 dBm.

Upstream. `git show upstream/dev:src/loop_functions.cpp` has the same order (stationary return before `if(bGPSAutosymbol)`,
the statics at `:6641`): upstream PR. Cut from `upstream/dev`, port `autosymbol.h` and the call-site change only, German
PR body. The new test suite can go with it.

Risks.

- GPS speed jitter above 1.4 m/s at rest still refreshes the bike timestamp and re-selects `b` for 120 s each time; the fix
  makes the symbol return after 120 s of quiet, it does not filter spikes. A speed filter or a consecutive-sample rule is a
  separate decision.
- The RAK fallback speed `distance / gps_refresh_intervall` (`:6783`) uses the distance from the last beacon position, so
  it can read high on a node that has drifted (P-0080 area). The evaluation now runs on those samples too, so a drifting
  RAK may flip `[` to `b`; before the fix this was invisible only because it stuck.
- Extraction must stay byte-equal in thresholds and the `symid == '/'` guard; the tests above pin them.

## 5. P-0167 / GPS-AUTOSYM-03: car symbol in the first two minutes after boot

Symptom. With `--gps autosymbol` on, any sample that passes the stationary gate in the first 120 s after boot selects the
car symbol `>`, whatever the speed (a 1.2 m/s walk, a 2 m/s cycle). Found by code reading, no user report; the problem is
closed as a duplicate of P-0166, but the defect stands and has its own fix.

Root cause at HEAD. `src/loop_functions.cpp:6805-6806`
`static unsigned long last_car_speed_ts = 0; last_bike_speed_ts = 0;`. The check `if ((millis() - last_car_speed_ts) <
120000)` at `:6830` is true for `millis() < 120000` before any speed was seen, so `target_symbol = '>'` is chosen at
`:6832` as soon as a sample passes the stationary return (speed >= 1.0 m/s, or a jump over 100 m). Note the slow-walk case:
at 1.0 to 1.4 m/s neither timestamp is updated, so the symbol is `>` with no speed above 1.4 ever seen. Same defect for the
bike timestamp (it only matters after the car branch, so the visible effect is `>`).
Changed since the pin: no (`git log -S'last_car_speed_ts' 96aaa67c72..HEAD -- src` is empty).

Platforms. Shared function, both platforms (section 2). The first 120 s after boot is the same on ESP32 and nRF52;
`millis()` starts at 0 on both. No `#ifdef`.

Fix design. The `car_seen`/`bike_seen` flags of the `AutoSymbolState` in section 4: `autoSymbolNote` sets them,
`autoSymbolPick` requires `car_seen && (uint32_t)(now - car_ms) < AUTOSYM_HOLD_MS` (same for bike). Zero-init (`static`)
means "nothing seen". Chosen over "initialise the timestamps to a value older than 120 s" because that value is
rollover-dependent: `millis() - ts` as `uint32_t` has no safe "old" sentinel at boot and after 49.7 days.

Test, same suite `test/test_autosymbol/test_autosymbol.cpp`:

- `test_boot_window_slow_walk_is_runner`: zero state, note 1.2 m/s at t = 30000, pick at 30000 expects `[`. Fails today
  (returns `>`: `(30000 - 0) < 120000`).
- `test_boot_window_bike_is_bike_not_car`: zero state, note 2.0 m/s at t = 30000, pick expects `b`. Fails today (`>`).
- `test_boot_window_car_is_car`: note 8.0 m/s at t = 30000, pick expects `>`. Green before and after (guards the
  other direction).
- `test_hold_expires_from_the_sample_not_from_boot`: bike sample at t = 30000, pick at 149999 gives `b`, at 150000 gives
  `[`.

Red run: with the verbatim-copy header (flags ignored) the first two assertions fail with `expected '[' / 'b', was '>'`.
Then add the flags and run green.

Bench (optional): cold boot, `--gps autosymbol`, `--track on`, walk at about 1.2 m/s within two minutes of boot with
`--gpsdebug on` and `--airgap on`; expect no `Auto-Symbol switch to '>'`. Needs outdoor movement; the host test is the
evidence of record.

Upstream. Same initialisation upstream (`upstream/dev:src/loop_functions.cpp:6641`): upstream PR, together with
GPS-AUTOSYM-02 because both edit the same block.

Risks. A node that really drives within 120 s of boot now shows `b` until the first sample above 7.0 m/s, instead of `>`
at once; accuracy improves, latency of the car symbol is one sample at 15 s or 12 s rate.

## 6. P-0081 / POSINFO-NEXT-01: NEXT counter wraps, and the duplicate-position half

Symptom. The "Next POS" value jumps (10 to 20 s while carried, 1800 at rest, and a seven-digit number; the OLED shows a
small negative number). Not RAK-specific; the reporter's `--info`-style app value, the web page and the T-Deck view share
the expression.

Root cause at HEAD. `posinfo_timer` and `posinfo_interval` are `unsigned long` (`src/loop_functions.cpp:932`, `:909`),
32 bit on ESP32 and nRF52. Six display sites compute the remaining time without a clamp:

| Site                                         | Expression                                                           | Overdue by d ms shows |
| -------------------------------------------- | -------------------------------------------------------------------- | --------------------- |
| `src/command_functions.cpp:8622` (`--pos`)   | `(int)(((posinfo_timer + (posinfo_interval*1000)) - millis())/1000)` | about 4,294,967       |
| `src/command_functions.cpp:8788` (JSON NEXT) | same                                                                 | about 4,294,967       |
| `src/web_functions/web_functions.cpp:1942`   | same                                                                 | about 4,294,967       |
| `src/t-deck/lv_obj_functions.cpp:4931`       | same                                                                 | about 4,294,967       |
| `src/loop_functions.cpp:2075` (OLED/E-Ink)   | `posinfo_interval - ((millis() - (int)posinfo_timer)) / 1000`        | small negative (-d/s) |
| `src/t-deck-pro/ui_deckpro.cpp:2148`         | same as the OLED line                                                | small negative (-d/s) |

Scout list correction: `command_functions.cpp:8622` (`--pos` print) and `ui_deckpro.cpp:2148` were not in the problem
file; `src/command_functions.cpp:8788` is the JSON `NEXT` the app shows. The unsigned form gives `2^32 - d` ms, divided by
1000 and cast: 4,294,967 for d = 1 ms.

Why the overdue window is long (code reading, not bench-measured). In both mains the beacon condition is
`(millis() - posinfo_timer) >= interval*1000 || (bPosFirst window) || posinfo_shot`
(`src/esp32/esp32_main.cpp:5008`, `src/nrf52/nrf52_main.cpp:2324`); inside it a 30 s floor
(`(millis() - posinfo_timer_min) >= 30000`, `esp32_main.cpp:5011`, `nrf52_main.cpp:2327`) gates the send, and the `else`
branch of the outer `if` refreshes `posinfo_timer_min = millis()` on every pass where the condition is false
(`esp32_main.cpp:5081`, `nrf52_main.cpp:2400`; the send path refreshes it at `:5076`, `:2395`). So when the condition
first turns true the floor was refreshed one pass ago and the send follows about 30 s later: NEXT is negative or huge for
about 30 s per cycle on both platforms. The same 30 s lag applies to `posinfo_shot` beacons (see "Other bugs").
Changed since the pin: no (`git log -S` on `posinfo_interval`, `posinfo_timer_min` is empty).

Platforms. nRF52 and ESP32 checked separately: the two mains carry the identical condition and floor. Display sites:
`command_functions.cpp` and `web_functions.cpp` (web guarded `!defined(NRF52_SERIES) || HAS_NRF_ETH`, so nRF52 only with
Ethernet) all boards; `lv_obj_functions.cpp` T-Deck and T-Deck Plus; `ui_deckpro.cpp` T-Deck Pro; `loop_functions.cpp:2075`
every board with a text display on the GPS page (RAK4631 OLED, T-Echo `NEXT:%5i`, T114, WP_DISP E-Ink variants). All
ESP32 and nRF52 builds affected; it is not RAK-specific (the reporter's "only on the RAK" is not supported by the code).

Fix design. A pure header `src/posinfo_next.h` (reuse pattern of `src/beacon_rate.h`; grep for `posinfoNext`,
`pos_seconds`, `NEXT` over `src`, `test` and `tools` found no existing helper):

```cpp
static inline int posinfoNextSeconds(uint32_t now_ms, uint32_t timer_ms, uint32_t interval_s)
{
    uint32_t due_ms = timer_ms + interval_s * 1000u;            // wraps like millis()
    int32_t  left   = (int32_t)(due_ms - now_ms);
    return left > 0 ? (int)(left / 1000) : 0;                   // clamp at 0, never negative or huge
}
```

Call at all six sites as `posinfoNextSeconds(millis(), (uint32_t)posinfo_timer, (uint32_t)posinfo_interval)`. `pos_seconds`
at the two OLED sites keeps its name and type. Intentional display change: the small negatives on the OLED and T-Deck Pro
become 0. Valid while `interval_s * 1000` is below 2^31 ms (24 days; the interval is at most a few hours). Out of scope:
making the display count to the real send time (the 30 s lag); it would need the loop change below.

Test (fails before, passes after). New suite `test/test_posinfo_next/test_posinfo_next.cpp`, env `native`,
`pio test -e native -f test_posinfo_next`. It carries a `legacyNextExpr(uint32_t now, uint32_t timer, uint32_t interval)`
copy of the old unsigned expression in `uint32_t` (host `unsigned long` is 64 bit, so the legacy copy must use `uint32_t`
to model the boards).

- `test_overdue_by_one_ms_is_zero`: timer 100000, interval 20, now 120001, expects 0. Fails with the legacy body
  (4294967).
- `test_overdue_30s_window_is_zero`: now 150000 (the floor window), expects 0 (legacy 4294937).
- `test_normal_countdown`: now 105000 gives 15; now exactly due gives 0; interval 1800 at `now == timer` gives 1800.
- `test_millis_rollover`: timer `0xFFFFFF00`, interval 20, now 1000 gives 18 (green before and after: mod 2^32 arithmetic
  was already right); overdue across the rollover (now 20000) gives 0.
- `test_legacy_expression_wraps` (premise, always green): `legacyNextExpr` for d = 1 ms equals 4294967, proving the test
  models the board arithmetic.
- `test_all_six_sites_use_the_helper`: slurp the six files; each contains `posinfoNextSeconds(`, none contains the old
  `posinfo_interval * 1000)) - millis()` or `- (int)posinfo_timer)` expression. Fails today on all six.

Red run: create the header with the legacy body (`return (int)(((timer + interval*1000u) - now)/1000);`), run the suite,
record the failures of the first two and the six-site pin; then swap the body to the clamped version and rerun green.

Bench (optional): any GPS node, INSTRUMENT build, `--airgap on` (TX refused in `doTX()`, `docs/bench-airgap.md`), poll
`--pos` every 5 s across one beacon cycle; expect `...NEXT:` never above the `RATE` and never negative or seven-digit,
and 0 for about 30 s at the end of the cycle. Own node only, no radio traffic.

Upstream. All six expressions are identical on `upstream/dev` (`loop_functions.cpp:2063`, `command_functions.cpp:8133` and
`:8299`, `web_functions.cpp:1913`, `lv_obj_functions.cpp:4823`, `ui_deckpro.cpp:2139`): upstream PR, minimal port of the
header and six call sites.

Risks. Display only; no wire format change, no settings change. The JSON `NEXT` the app reads changes from a seven-digit
value to 0 in the overdue window; the iOS and Android apps are not known to depend on the garbage value (the maintainer
called the NEXT oddity harmless, uid 971). One reader risk: any consumer that treated a negative OLED value as "overdue"
now sees 0.

P-0081 part 2 (duplicate positions at rest): no fix planned. Candidates at HEAD: the neighbour-count shot
(`src/nrf52/nrf52_main.cpp:2308-2318`, ESP32 `src/esp32/esp32_main.cpp:4997-5004`, 60 s floor, not TRACK gated) and
SmartBeaconing jitter in TRACK mode (`src/loop_functions.cpp:6788` stationary gate, `:6897` distance one-shot, `:6783`
RAK speed fallback). A serial log with `--setinfo on` and `--gpsdebug on` on a charging RAK4631 (sat count, HDOP, NCNT
changes) is the next step; then one test per cause (`beaconDue(...)` over a changing neighbour count; the stationary gate
fed with 1.2 m/s jitter). The problem file proposes a new problem for it; that is the operator's call (section 8).

## 7. Backlog rows

For the "Implement (firmware and tooling)" table of `docs/BACKLOG.md` (columns Id, Area, Item, Status, Next step). Do not
edit `BACKLOG.md` from this plan; the orchestrator inserts the rows.
`grep -rn "GPS-AUTOSYM-0\|POSINFO-NEXT-0" docs/BACKLOG.md docs/architecture/08-defect-catalogue.md docs/archive/` is empty.

| Id              | Area              | Item                                                                                                                                                                                                                                                                                                                                           | Status                                          | Next step                                                                                                                             |
| --------------- | ----------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------- |
| GPS-AUTOSYM-01  | GPS / nRF52       | nRF52 boot does not restore `--gps autosymbol` (`node_sset3` 0x1000): automatic symbol selection is off after every reboot on RAK4631, T114 and T-Echo; ESP32 restores it (`esp32_main.cpp:1629`). One line in `nrf52_main.cpp`. Plan [plan-gps-autosymbol-nextpos-20261121.md](plan-gps-autosymbol-nextpos-20261121.md). Problem file P-0383. | Open 2026-11-21: plan written, not implemented. | Implement, source-pin suite `test_sset3_restore`; bench: RAK4631 `--gps autosymbol`, `--reboot`, `--pos` shows `Auto on`; upstream PR |
| GPS-AUTOSYM-02  | GPS / auto symbol | Auto symbol stays on bicycle while the node stands still: the stationary early return in `setSMartBeaconing()` runs before the autosymbol block, so the 120 s expiry is never evaluated. Plan [plan-gps-autosymbol-nextpos-20261121.md](plan-gps-autosymbol-nextpos-20261121.md). Problem file P-0166.                                         | Open 2026-11-21: plan written, not implemented. | Implement: `src/autosymbol.h` seam, call before the stationary return, suite `test_autosymbol`; upstream PR                           |
| GPS-AUTOSYM-03  | GPS / auto symbol | Auto symbol is the car for the first two minutes after boot: the speed timestamps start at 0, so `(millis() - 0) < 120000`. Plan [plan-gps-autosymbol-nextpos-20261121.md](plan-gps-autosymbol-nextpos-20261121.md). Problem file P-0167.                                                                                                      | Open 2026-11-21: plan written, not implemented. | Implement after GPS-AUTOSYM-02 (same header, `car_seen`/`bike_seen` flags); upstream PR together with it                              |
| POSINFO-NEXT-01 | Position beacon   | NEXT counter wraps: unsigned subtraction shows about 4,294,967 s (app, web, `--pos`, T-Deck) or a small negative (OLED, T-Deck Pro) for the roughly 30 s between due and send. Six sites, both platforms. Plan [plan-gps-autosymbol-nextpos-20261121.md](plan-gps-autosymbol-nextpos-20261121.md). Problem file P-0081.                        | Open 2026-11-21: plan written, not implemented. | Implement: `src/posinfo_next.h` clamped helper at six sites, suite `test_posinfo_next`; upstream PR                                   |

## 8. Open questions for the operator

1. P-0166 design: evaluate the symbol only in TRACK mode (recommended, after the TRACK gate) or also with TRACK off?
2. P-0166 on-air: leave the corrected symbol to the next scheduled beacon (recommended) or force `posinfo_shot` on the
   transition to `[`?
3. P-0081 part 2: open a new problem for the duplicate positions at rest (needs a log from the reporter's RAK), as the
   problem file proposes, and ask for the log?
4. Does the 30 s lag between "due" and "sent" (section 6) count as a defect to fix, or is it the intended floor? The plan
   leaves it alone.
5. P-0167 is closed in the triage store as a duplicate of P-0166 although its defect needs its own fix; reopen or keep it
   documented here only?

## 9. Evidence checklist

- Every commit cited is in `fork-dev` (`git merge-base --is-ancestor <c> HEAD`): `d65dacef`, `0e6fcb23`, `5b29f284`,
  `ae20661d`, `17c0969c`, `6966c527`, `dec799a6`; the first three also in `upstream/dev`. No revert found
  (`git log -i --grep='^Revert'` filtered for symbol, posinfo, gps is empty).
- Every `path:line` was read at HEAD `067d29cd` (see the sections). Lines that differ from the scout notes: the
  `--pos` print at `command_functions.cpp:8622` and `ui_deckpro.cpp:2148` are additional NEXT sites.
- Platform claims: P-0383 nRF52 only (grep of `src/nrf52` and `src/esp32`); P-0166/P-0167 shared function, callers checked
  per platform and board (`gps_functions.cpp:1271`, `tdeck_pro.cpp:702`, `nrf52_main.cpp:3452`); P-0081 both mains read
  separately (`esp32_main.cpp:5008-5081`, `nrf52_main.cpp:2324-2400`), display sites per board guard.
- Not verified: any run, any hardware, any test (no `pio` command was run in this session); behaviour of the 30 s lag on a
  board; the airgap bench behaviour for `posinfo_timer`.

## 10. Other bugs seen (not fixed, not in any row)

- The 30 s floor makes every position beacon (periodic and `posinfo_shot`, including the TRACK 20 s rate and turn beacons)
  wait about 30 s after it is due, because the `else` branch refreshes `posinfo_timer_min` on every pass before the
  condition turns true (`esp32_main.cpp:5008-5081`, `nrf52_main.cpp:2324-2400`). Effective cadence is interval plus about
  30 s. Code reading only; worth a bench measurement and its own ticket.
- nRF52 also does not restore other `node_sset3` flags that its code reads: `bWXDEBUG` (used at
  `nrf52_main.cpp:3052,3071`), `bINA226ON` (`src/nrf52/loop_actions_nrf52.cpp:165`), and the ESP32-restored
  `bSOFTSERDEBUG`, `bAnalogFilter`, `bSHT21ON` (not checked whether the nRF52 builds use them). Same class as P-0383.
- With TRACK off, `setSMartBeaconing()` returns at `:6738` before any symbol logic, so `--gps autosymbol` silently needs
  `--track on`; the help text (`src/command_functions.cpp:1778`) does not say so.
