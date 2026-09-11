# T-Deck-14 UI checklist — G0 (test plan step H11)

Date: 2026-09-11 · Base: `dry-base-20260911` · Image: `INSTRUMENT_ENABLED`
Filled by: **\_\_\_\_\_\_\_\_** · Repeat verbatim at G1 and G2.

## Already automated — do not re-do by hand

`tools/bench/tdeck_harness.py --scenario tabs,nav,input` ran green on this
node; the log is `harness-tabs-nav-input.txt` next to this file.

| Scenario | Covers                                               | Result |
| -------- | ---------------------------------------------------- | ------ |
| `tabs`   | switching through every tab, repaint per tab         | PASS   |
| `nav`    | drawer → tab → drawer over all tabs, settings scroll | PASS   |
| `input`  | keyboard keys and trackball through the LVGL indev   | PASS   |

Those prove the UI **responds**. They cannot prove what it **shows**: the panel
readback probe is void on this hardware, so nothing can read the framebuffer
back. That is why the four items below need eyes.

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

Result: `________` Notes: `________________________________`

### 2. TRACK no-fix text

With no GPS fix (indoors is usually enough — check the GPS tab first and note
whether it reports a fix).

- [ ] The TRACK display shows its **no-fix** text rather than stale or blank
      coordinates.
- [ ] Write down the exact wording. The wording is the thing being compared at
      G2, so transcribe it character for character.

GPS had a fix: `yes / no` · Exact text: `________________________________`

### 3. Keyboard character map, types 1–4

Open a message input field and type through each keyboard layer.

- [ ] Layer 1 (lowercase) — every key produces the character printed on it.
- [ ] Layer 2 (uppercase / shift).
- [ ] Layer 3 (numbers / symbols).
- [ ] Layer 4 (the remaining symbol layer).

**Known limitation, expect it:** this unit has old keyboard-controller
firmware and never answers the raw-mode probe, so `TD-10` (key auto-repeat)
cannot be proven on this bench at all. If a _layer_ is unreachable rather than
a single key being wrong, that is the same limitation, not a regression — note
it and move on.

Result: `________` Any key that produced the wrong character: `____________`

### 4. APRS symbol dropdown round trip

In settings, open the APRS symbol selector.

- [ ] The dropdown opens and lists symbols.
- [ ] Pick a symbol **different** from the current one, confirm, leave the
      screen, come back.
- [ ] The new symbol is still selected — it survived the round trip.
- [ ] Set it back to the original (`/` group, `#` code on this node) and
      confirm that sticks too.

Symbol chosen: `______` · Survived: `yes / no` · Restored: `yes / no`

## After the checklist

If anything failed, note it here rather than opening a backlog row — the point
of G0 is to record the state as it is, including defects, so that G1 and G2
compare against reality rather than against an ideal.

Failures observed: `________________________________________________`
