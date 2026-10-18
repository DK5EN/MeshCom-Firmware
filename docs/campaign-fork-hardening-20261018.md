# Campaign 2026-10-18: publication docs, production setting lock, heap, BMx280, LED

Resume point of this multi-wave campaign. Branch `fork-dev`. Operator approvals 2026-10-18: plan, the
locked-settings draft (`docs/production-locked-settings.md`), flash permission for DK5EN-92, DK5EN-1,
DK5EN-90, DK5EN-14 (DK5EN-98 untouched), the heap bisect. BMx280 is field tested (no bench sensor).

## Waves

| Wave | Content                                                                                        | Status          |
| ---- | ---------------------------------------------------------------------------------------------- | --------------- |
| W0   | Docs: fork vs upstream, locked settings spec, time zones (+ test), RM1 signature, RM1 cmds     | done            |
| W0h  | T-Beam heap bisect c9e64344..85b0c457, idle-heap method                                        | pending         |
| W1   | BMx280 presence helper, LED core helper, server reply core, heap gate tool + mock option       | done `b8aa4354` |
| W2   | LED surfaces (console, ESP32 main/sleep/PMU/RM, web)                                           | pending         |
| W3   | Server reply integration (UDP frames, LoRa gates, settings); heap levers (stack, rings, marks) | pending         |
| W4   | LED nRF52 + variants; production lock incl. `--stor` and `--autoupdate auto`                   | pending         |
| W5   | Bench: T-Beam heap, LED, server reply on DK5EN-1 and DK5EN-90; docs; restore                   | pending         |

## Decisions carried into the waves

- Auto update: code stays; the `auto` value must be refused on every surface (console, BLE, config
  import, web). Notify stays the only mode. Remote `upd go <tag>` (manual remote install) stays.
- Mailbox announcement (`--stor`): must not be switchable in production builds (W4 lock).
- `{SET}` over LoRa: production accepts text hop 1..6 only and ignores the position hop (spec in
  `docs/production-locked-settings.md`, open points listed there).
- Phone rings freed during the BLE pause: messages for the phone during an update check are dropped
  (accepted).

## W0 result

- New: `docs/fork-dev-vs-upstream-20261018.md`, `docs/production-locked-settings.md`,
  `docs/timezones.md` (42 strings pinned by `test_doc_table` in `test/test_tz_rule`),
  `docs/rm1-hmac.md` (vector verified), `docs/rm1-commands.md` (25 commands).
- Corrected: `docs/adr-remote-hmac.md` (vector counts, allowlist, sync limiter strict-only),
  `docs/rm-gui/unification/inventory-rm1.md` (25 commands).
- Found in W0: `{SET}` over LoRa is not password-gated and accepts 0..7; `--stor on` and
  `--autoupdate auto` are settable today; `--ptime` fallback outside its range; `--postime` dead
  branch; `--spec*` settings overwritten by the web spectrum page. All go to W4.

## W1 result

- `b8aa4354`: sens presence from detected sensors (12 regression cases, 5 failed before the fix),
  LED core helpers, `tools/bench/heap_idle.py`, mock `--keep-reply`, registration helper.
- Advisor: one blocker (heap tool regex expected `;`; printfdeb prints spaces) plus 8 items, all
  fixed by a rework round; second advisor pass approved. Gate PASS 74 envs / 2632 cases, 6 builds.
- Bench nodes run `--setlog on`: then no `[HEAP] (mon)` line exists, the heap comes in the 300 s
  `[LOG] STAT ... heap=` field. The heap tool reads both.
- Follow-up for W5 docs: `docs/rm1-commands.md` describes `sens` before this fix ("planned change").

## W2 result

- LED surfaces: console (`--info` lines, SN/SN1 not-fitted, charge rows refresh the phone, `--board led on`
  refused while OneWire owns the pad), ESP32 (`boardLedTick()` replaces the inline flasher, deep sleep
  forces the LED dark and reverts an RM charge-LED override), RM (`chgled` bare read
  `ok chgled=<mode>[ rm]`, `led` answers `err pinbusy` while OneWire owns the pad), web setup and Info.
- The compile-time OneWire exclusion of the RM LED pad is gone; every T-Beam GPIO4 write is gated at run
  time (`boardLedPinUsable()`, `remoteLedPinUsable()`). Heltec V3/V4 RM `led` unchanged (own pad).
- Advisor: one blocker (`--board led off` wrote GPIO4 unguarded) plus a producer-lint violation (web
  switch on boards without the handler) and 5 should-fix items; rework round, second pass approved.
- Gate PASS 74 envs / 2635 cases, JS GUI tests, 8 board builds, string scans (T-Beam `err pinbusy`,
  Heltec `ok led=`, T-Deck `LEDNF`).
- Open for W4 (LED platform): NeoPixel-only boards need `sw.ledSupported` and a toggle row after LED-13.

## W0h heap bisect (DK5EN-92, release images, 2323 console, BLE up, no phone)

| Build      | STAT heap at 300 s / 600 s |
| ---------- | -------------------------- |
| `c9e64344` | 70356 / 68524 B            |
| `85b0c457` | 70100 / 68336 B            |

- Idle free heap is unchanged (about 200 B) between the 10-17 and 10-18 builds.
- Update check, BLE paused (`[AU];ble;pause;heap`, three checks each): `c9e64344` 76.8-81.5 KB,
  `85b0c457` 77.1-81.0 KB, `e31940e8` 76.3-80.0 KB (release images); `85b0c457` INSTRUMENT 73.6-77.8 KB
  (idle 66.6 KB).
- Verdict: no firmware regression between 10-17 and 10-18. The 66-72 KB of the heap verdict came from
  INSTRUMENT builds (about 3.5 KB) plus the matrix conditions (net console session, logging sampler).
  The release T-Beam is already near the 76000 B gate; the W3 levers add about 7 KB on top.

## W3 result

- Heap: phone rings (5 KB) leave static RAM on classic ESP32 and live only while BLE runs (T-Beam
  static RAM 107688 -> 102584 B); byte_fifo is now locked (portMUX) on ESP32; update task stack
  12288 -> 10240 B, not 8192: real TLS jobs leave 5692-5800 B free of 12288 (the 9.5 KB-free figures
  came from jobs refused at the gate); `[HEAP]` boot marks for ble, wifi, udp, web, mdns, kiss,
  netconsole, also under `--setlog`.
- Gateway server reply handling (W3 F): implemented on ESP32 and nRF52 (details off the public docs).
- Advisors: heap two rounds (stack too small, ring free raced the producers, BLE mutex ordering);
  server reply three rounds (twin link, frame filter, unconfigured callsign). All fixed.
- Gate PASS 74 envs / 2647 cases; builds heltec V3, ttgo_tbeam, RAK4631, T-Deck Plus, E22 S3 DevKitC,
  T5 ePaper, T114, T-Echo, heltec V3 INSTRUMENT.
- Open, operator: on classic ESP32 every update check now clears the web message history (it reads the
  phone ring). Accept, or keep the history ring static (about 2 KB less gain).

## W4 result

- Production setting lock (`src/prod_lock.h`, spec `docs/production-locked-settings.md`, status implemented):
  one gate in the inner `commandAction` (console, 2323, web manualcommand and setup, BLE, T-Deck text, RM),
  normaliser on config import, boot load (ESP32, nRF52) and the nRF52 BLE blob; `{SET}` over LoRa accepts
  text hop 1..6 and ignores the position hop in release builds; refusal `[ERR];locked;<name>`.
- All builds: `--autoupdate auto` refused; import accepts 0..2 and normalises 2 -> 1 (old backups stay
  importable). Release builds: `--stor on` refused, node_stor forced 0.
- Setter bugs fixed: `--postime` dead branch and bare sscanf, `--ptime` fallback (runtime fallback now 15,
  was 30 h), `--pingtime` floor 30, `--pingmax max` = 5; the web spectrum page no longer persists node_spec*.
- LED platform: nRF52 board LED heartbeat and RM `led` (RAK4631, T114, T-Echo), off level at init and
  BLE-off, sleep forces dark; variants per the LED plan (T-Echo pins 33/35/14 active low, T114 active low,
  Heltec V3/V4 BOARD_LED 35, V2 25, WSL V3 35, SX1268 4, DevKitC NeoPixel 38, T-ETH-Elite 38).
- Goldens: `variant-macros.txt`, `variant-macros-effective.txt` (also catches up unrelated drift since
  10-02: version, ring sizes, CONSOLE_UART_TXBUF, MC_ENV_NAME, ENABLE_MSGSTORE), `variant-ini-effective.json`;
  `variant_macros_lint.py` now pins the nRF52 variant.h LED macros.
- Advisors: three rounds (import refused old backups, untested call sites, stale golden, autoupd range).
- Gate PASS 75 envs; 14 board builds incl. heltec V3 INSTRUMENT.

## Resume point: W5 (not started, operator stop 2026-10-18 evening)

1. Bench, serialized, `identity_guard.py` first, 2 dBm, test traffic only to TEST or own DMs, DK5EN-98
   untouched. Flash permission: DK5EN-92, -1, -90, -14.
   - DK5EN-92 (T-Beam, OTA via `tools/webflash.py`, console 2323 with HMAC login): release image of the
     campaign head: idle `STAT heap=` and `[AU];ble;pause;heap` at `--update check` (expected about 7 KB above
     the 76-81 KB measured on c9e64344/85b0c457/e31940e8), `stk` on a check that passes the gate (keep above
     3 KB with the 10240 B stack), `[HEAP]` boot marks; LED tests of the LED plan W4.
   - DK5EN-1 (Heltec V3, USB serial 0001): LED (`--board led`, RM led), gateway server reply test (internal
     note), production lock spot checks (`--nbrrelay on` refused, `--autoupdate auto` refused).
   - DK5EN-90 (RAK4631, USB 230D6EBB3266D20E): LED heartbeat, server reply test on the nRF52 path.
   - DK5EN-14 (T-Deck Plus): LED not fitted in the phone app (SN without BLED/CHGLED, SN1 LEDNF).
2. Release gate: add the idle-heap step (`tools/bench/heap_idle.py --host <T-Beam> --baseline
tools/heap_baseline.json --board ttgo_tbeam`) to `.claude/commands/release-firmware.md`, create the
   baseline from the W5 measurement.
3. Docs: `docs/rm1-commands.md` (sens now from detected sensors, bare `chgled`, `err pinbusy`),
   `docs/fork-dev-vs-upstream-20261018.md` rows for W1-W4 (LED parity, sens fix, setting lock, heap levers),
   defect catalogue (SENS-01/02, LED-01..17), BACKLOG (SENS-03 DHT, Tracker LED, V4R8 env), RESUME.
4. Open operator decisions: web message history empty after each update check on classic ESP32 (accepted
   or keep the history ring static); spectrum import ranges still refuse out-of-range backups; `{SET}`
   password question (spec open point); field confirmation of T-Echo, T114, T-ETH-Elite, DevKitC LED pins.
