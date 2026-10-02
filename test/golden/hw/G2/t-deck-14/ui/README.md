# G2 H11 -- t-deck-14, UI checklist, final image e64ce346 + INSTRUMENT_ENABLED (2026-09-17 23:20)

The harness drives instrument-only commands (`--uistat`, `--tab`, `--drawer`,
key/ball injection), so the T-Deck runs the instrument build of the final
image, as G0 did.

## Automated half -- `tools/bench/tdeck_harness.py --scenario tabs,nav,input`

| scenario | result | detail                                                                 |
| -------- | ------ | ---------------------------------------------------------------------- |
| tabs     | PASS   | 8 tabs, every one repainted (`harness-tabs-nav-input.txt`)             |
| nav      | PASS   | 34/34 steps acked and repainted, settings scroll 415 -> 0, no crash    |
| input    | PASS   | first run refused (KEYLOCK on, TD-18 precondition works); after `--keylock off`: keys 7/7, p50 86 ms, p95 166 ms; trackball 4 x 10/10 steps, 70/70 px (`harness-input-rerun.txt`) |

## Manual half -- owed, by eye, operator

Per `test/golden/hw/G0/t-deck-14/ui/tdeck-checklist.md` §2:

1. MHeard screen, seven columns -- **DR-28 prediction: rows now most-recent-first.** Check the order against `--mheard` on the console.
2. TRACK no-fix text, wording with GPS off.
3. Keyboard layers 1-4, every key, auto-repeat (KEYLOCK is off now).
4. APRS symbol dropdown round trip.

Framebuffer readback is void (`screencrc`), so these stay human checks.

## Node state left behind

Instrument image, KEYLOCK off. Reflash stock `t_deck_plus` when the manual
checks are done.
