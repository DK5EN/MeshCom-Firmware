# T-Deck: trackball button fires two LVGL clicks per press (TD-11)

Status: plan, not implemented. Branch to work on: `fork-main` (worktree
`mellow-marinating-lightning`).

## Symptom

Cursor on the hamburger icon, press the trackball: the drawer opens. Release
the trackball: the drawer closes again immediately. The row below the icon can
never be reached with the trackball.

## Root cause

`mouse_read()` in `src/t-deck/tdeck_main.cpp` feeds the trackball push button
(`TDECK_BOOT_PIN`, index 4 of `dir_pins`) through the same level-compare branch
as the four roll directions:

```c
if (dir != last_dir[i]) { last_dir[i] = dir; steps = 1; }
...
case 4: left_button_down = true; activity_detected = true; break;
```

`left_button_down` is a local that resets to `false` on every poll, so the pin
is reported as a one-poll pulse on **every** edge. The press edge and the
release edge each produce a `PRESSED` followed by `RELEASED`, and LVGL turns
each pair into a full `LV_EVENT_CLICKED`.

| Poll (30 ms) | Pin  | `dir != last_dir` | Reported | LVGL event              |
| ------------ | ---- | ----------------- | -------- | ----------------------- |
| press edge   | low  | yes               | PRESSED  | PRESSED                 |
| next poll    | low  | no                | RELEASED | CLICKED (drawer opens)  |
| held         | low  | no                | RELEASED | none                    |
| release edge | high | yes               | PRESSED  | PRESSED                 |
| next poll    | high | no                | RELEASED | CLICKED (drawer closes) |

The hamburger handler `tab_menu_button_event_cb()` in
`src/t-deck/lv_obj_functions.cpp` toggles on every click, so the second click
undoes the first. One-way buttons (send, sendpos, zoom, clear) execute twice
instead; nobody noticed because the second run is idempotent or cheap.

Inherited from upstream. The TM-18 interrupt edge counter (`s_ball_edge_mode`)
is gated on `i < 4` and does not touch the button. The AceButton handler on the
same pin only sets the global `clicked` flag and is unrelated.

## Required behaviour

The drawer must open on click, stay open while the button is held, and stay
open after release. Click delivery on press (as today) must be kept; moving it
to release is not acceptable.

## Fix

Emit the pulse on the press edge only. Pin is `INPUT_PULLUP`, active low.

`src/t-deck/tdeck_main.cpp`, level-compare branch in `mouse_read()`:

```c
if (dir != last_dir[i])
{
    last_dir[i] = dir;
    // TD-11: the push button is a pulse on the press edge only; the
    // release edge produced a second LVGL click that toggled the drawer shut.
    steps = (i == 4 && dir) ? 0 : 1;
}
```

Resulting sequence per physical press:

| Poll (30 ms) | Pin  | Edge        | Reported | LVGL event             |
| ------------ | ---- | ----------- | -------- | ---------------------- |
| before       | high | none        | RELEASED | none                   |
| press edge   | low  | high to low | PRESSED  | PRESSED                |
| next poll    | low  | none        | RELEASED | CLICKED (drawer opens) |
| held         | low  | none        | RELEASED | none (drawer stays)    |
| release edge | high | low to high | RELEASED | none (drawer stays)    |
| next poll    | high | none        | RELEASED | none                   |

One click per press, delivered one poll after the press edge. No other button
changes behaviour; each fires once instead of twice.

### Bench harness follow-up (same change)

`tdeck_dbg_inject_ball()` queues a click as `s_dbg_ball_pending[4]++`, and the
consumer flips `dir = !last_dir[i]`. That alternates press and release edges, so
after the fix `--ball click 2` would deliver one click instead of two. Make an
injected click always a press edge, independent of the pin level:

```c
if (s_dbg_ball_pending[i] > 0)
{
    s_dbg_ball_pending[i]--;
    dir = (i == 4) ? false : !last_dir[i];   // button: always a press edge
}
```

`last_dir[4]` is then set to `false` by the edge compare, so the real release
edge that follows a real press still lands as a no-op. If the injected click
lands while the physical button is not pressed, the next poll reads `high`,
sets `last_dir[4] = true`, and with the fix produces no click.

Not touched: the interrupt edge path, `--balledge`, AceButton, the
`activity_detected` cursor timeout (still set on the press pulse only, which
is the same as today's press edge).

## What the fix does not give

No LVGL `LONG_PRESSED` or `PRESS_LOST` on the trackball. Those need a real
level, nothing on the T-Deck listens for them, and the level approach would
move every click to release. Rejected for that reason.

## Verification

1. Build: `pio run -e tdeck_plus` (and `tdeck`), zero new warnings.
2. Bench, DK5EN-14 on `/dev/cu.usbmodem1101` (opening the port reboots the
   node, wait for the GUI):
   - `--drawer 0`, cursor onto the hamburger via `--ball`, then `--ball click 1`.
     Expect one `[DRAWER];1` and `drawer_is_open()` true, no `[DRAWER];0`
     afterwards.
   - `--ball click 2` on the hamburger: expect open, then closed (two clicks).
   - Physical: press and hold on the hamburger, drawer opens and stays; release,
     drawer stays; roll down one row, press, tab switches and drawer hides
     (`tabview_event_cb` calls `tdeck_hide_tab_menu()`).
   - Send position button via trackball: exactly one `--sendpos` line in the
     log per press (was two).
   - Touch screen unaffected: tap hamburger opens, tap again closes.
3. Regression note in `docs/08-defect-catalogue.md` as TD-11 and in resume.md.

## Commit

Single commit on `fork-main`:

`fix(tdeck): trackball button pulses on the press edge only -- release edge fired a second LVGL click (TD-11)`

Upstream PR candidate: yes, two lines in `tdeck_main.cpp`; the harness hunk is
fork-only. German PR text to be written from the root cause section above.
