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

- Idle free heap is unchanged (about 200 B) between the 10-17 and 10-18 builds. The 8-13 KB loss of the
  heap verdict was measured at the update check (BLE paused). Next: `[AU];ble;pause;heap` at
  `--update check` on both builds.
