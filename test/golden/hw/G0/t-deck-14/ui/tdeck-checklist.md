# T-Deck-14 UI checklist — G0 (test plan step H11)

Date: 2026-09-11 · Base: `dry-base-20260911` · Image: `INSTRUMENT_ENABLED`
Filled by: **DK5EN (operator, by eye)**, 2026-09-11 20:15 · Repeat verbatim
at G1 and G2.

## Already automated — do not re-do by hand

`tools/bench/tdeck_harness.py --scenario tabs,nav,input` ran green on this
node; the log is `harness-tabs-nav-input.txt` next to this file.

| Scenario | Covers                                               | Result   |
| -------- | ---------------------------------------------------- | -------- |
| `tabs`   | switching through every tab, repaint per tab         | PASS     |
| `nav`    | drawer → tab → drawer over all tabs, settings scroll | PASS     |
| `input`  | keyboard keys and trackball through the LVGL indev   | **VOID** |

Those prove the UI **responds**. They cannot prove what it **shows**: the panel
readback probe is void on this hardware, so nothing can read the framebuffer
back. That is why the four items below need eyes.

**The `input` result above is void, and that is the point (TD-18).** This run
was taken while the `TD-19` keyboard lock was still on, so the keyboard was
dead to the operator while the scenario reported `keys=7/7`: it counts the
`[KEY]` line, printed at the top of `keypad_read()`, and the lock drops the key
at the bottom of the same function. The scenario now reads `--info` first and
refuses to run unless `KEYLOCK off`; the keyboard half must be re-run on the
unlocked node before this checklist counts as the G0 record. The trackball half
was never affected -- `mouse_read()` is not gated by the lock.

## Manual — four checks, about five minutes

Work down the list on the device itself. Write `pass` or `fail` plus what you
saw; "looks fine" is not a result anyone can compare at G2.

### 1. MHeard screen — seven columns

Open the **mHeard** tab.

- [ ] The header row shows **seven** columns and each has a heading.
- [ ] At least one row of real data is present (the node has been hearing
      traffic all evening; if the list is empty, say so — that is itself a
      finding).
- [ ] No column is cut off at the right edge, and no heading overlaps its
      neighbour.

Result: `pass` Notes: `operator confirmed the mHeard tab by eye`

### 2. TRACK no-fix text

With no GPS fix (indoors is usually enough — check the GPS tab first and note
whether it reports a fix).

- [ ] The TRACK display shows its **no-fix** text rather than stale or blank
      coordinates.
- [ ] Write down the exact wording. The wording is the thing being compared at
      G2, so transcribe it character for character.

GPS had a fix: `yes` (`--pos` reported fix:yes sat:10 hdop:2.1 during the
session, flapping to fix:no sat:9 hdop:6.1) · Result: `pass, TRACK is working`

**Partial: the no-fix wording was not transcribed.** The node held a fix
indoors for most of the session, so the no-fix branch was not reliably on
screen, and no character-for-character text was recorded. What G2 can compare
here today is "TRACK renders and is working", not the wording. Transcribing it
is owed, and needs the GPS off (`--gps off`) rather than waiting for the fix to
drop.

### 3. Keyboard character map, types 1–4

Open a message input field and type through each keyboard layer.

- [ ] Layer 1 (lowercase) — every key produces the character printed on it.
- [ ] Layer 2 (uppercase / shift).
- [ ] Layer 3 (numbers / symbols).
- [ ] Layer 4 (the remaining symbol layer).

**~~Known limitation, expect it:~~ withdrawn 2026-09-11, see the result
below.** This said that the unit has old keyboard-controller firmware, never
answers the raw-mode probe, and that `TD-10` (key auto-repeat) therefore cannot
be proven on this bench at all. The probe had simply never been allowed to run:
it is gated on `!node_keyboardlock`, and the TD-19 lock was on. With the lock
cleared the controller answers `support;1` on the first try.

Result: `pass, layers 1-4` · Any key that produced the wrong character: `none`

**And the known limitation above is now disproved.** The operator also saw key
auto-repeat working, and the node backs that up: the firmware printed
`[KBD];rawprobe;00 00 00 01 00;key;75;support;1` at 20:11:38 (`support;1` =
`KBD_RAW_YES`) and `--info` now reports `KBD raw-mode yes` where it reported
`unknown` all evening. The probe had never run before, not because the
controller is old, but because `eligible` in `keypad_read()`
(`src/t-deck/tdeck_main.cpp:1125`) requires `!node_keyboardlock` -- the TD-19
lock suppressed the very probe whose silence was read as "this unit cannot do
raw mode". TD-10 is provable on this bench after all.

### 4. APRS symbol dropdown round trip

In settings, open the APRS symbol selector.

- [ ] The dropdown opens and lists symbols.
- [ ] Pick a symbol **different** from the current one, confirm, leave the
      screen, come back.
- [ ] The new symbol is still selected — it survived the round trip.
- [ ] Set it back to the original (`/` group, `#` code on this node) and
      confirm that sticks too.

Symbol chosen: `not recorded` · Survived: `yes` · Restored: `not confirmed`

Operator: "APRS symbol dropdown is working and symbol stuck". The round trip is
the thing being tested and it passed. Which symbol was picked, and whether it
was set back to `/` `#`, were not recorded -- so read this node's current symbol
before the G1 run rather than assuming it is the G0 one.

## After the checklist

If anything failed, note it here rather than opening a backlog row — the point
of G0 is to record the state as it is, including defects, so that G1 and G2
compare against reality rather than against an ideal.

Failures observed: `none`

Open, and deliberately left as text rather than as a backlog row: the TRACK
no-fix wording (item 2) and the keyboard half of the automated `input` scenario
(TD-18), which is void because it ran under the TD-19 lock.
