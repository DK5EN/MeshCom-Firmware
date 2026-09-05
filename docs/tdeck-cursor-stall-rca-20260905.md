# T-Deck Plus: cursor dead time while rolling the trackball -- RCA 2026-09-05

Operator report (DK5EN-14, build 11:03 with TD-12/TD-13): after new messages
have arrived and the menu bar was collapsed once, rolling the trackball freezes
the cursor for a fraction of a second every couple of seconds.

## Verdict

**No regression.** The dead times that were captured are SD map
recompositions (`sdmap_refresh()`: tile read from SD plus PNG decode,
470-750 ms each, main loop blocked, LVGL and cursor frozen). They are the
known TD-09 cost, triggered on the map tab by:

| Trigger                                                                      | Rebuilds | Evidence                                                    |
| ---------------------------------------------------------------------------- | -------- | ----------------------------------------------------------- |
| Picking the map tab from the tab bar (the bar collapses, view 140 -> 182 px) | 2        | `[ SDMAP ]` 294x140 then 294x182, 616 + 635 ms, 0.6 s apart |
| Keyboard zoom keys (`[KEY];67/68`) or the zoom buttons                       | 1 each   | six rebuilds 474-753 ms, one per key line, zoom 4,5,4,5,6   |
| Own-position beacon received (`tdeck_add_pos_point`, bHome)                  | 1        | code, `lv_obj_functions.cpp:3854`                           |
| 30 s tile-boundary check, map pan, map-set switch                            | 1        | code                                                        |

Before TD-13 a trackball click on a zoom button produced two clicks and
therefore two rebuilds (about 1.5 s dead); after TD-13 it is one.

On the message tab the sequence "3 messages, menu open, menu collapse, roll"
shows **no stall and no slowdown** in three instrumented runs: main-loop
average 6.3 ms before and after (ratio 1.01), no `[INSTR-LOOP]` gap above
250 ms, no `[BALL]` read gap above 250 ms, `lvgl` section 5-7 % of the loop.

## Instrument: `tools/bench/tdeck_harness.py --scenario msg_roll`

Sequence: roll (control), inject `--msgroll-msgs` messages, `--drawer on`,
`--drawer off`, roll again. Per phase it reports `[BALL]` read gaps on the
device clock, `[INSTR-LOOP]` gaps by section, loop average from two `--instr`
snapshots, per-section share, SD map rebuilds, and garbled serial commands.
Verdict: control clean, no stall in the second roll, loop average within
1.5x. `--msgroll-tab 3` runs the same on the map tab and reports the tile
rebuild on entry in `sdmap_rebuilds_total_ms`.

Two artefacts the first version of this scenario produced, kept here so
nobody re-chases them:

- **Serial overrun.** `checkSerialCommand()` drains one byte per main-loop
  iteration. `--ball` commands every 60 ms (267 B/s) exceed the ~140 B/s the
  loop drains at 7 ms per iteration; the RX buffer overruns, command echoes
  arrive as fragments (`--ball le--ball le`) and a burst of
  `[BALL];err;usage` follows. It looked like a 9.5 s stall. Cadence is now
  200 ms (`--msgroll-cadence-ms`), and garbled commands are counted.
- **Cursor at the screen edge.** `mouse_read()` clamps at the border and a
  clamped step reports no activity, so a roll that reaches x = 310 looks like
  a 0.5-1.2 s read gap. The roll now parks the cursor at a known x and stays
  inside +-120 px.

## Logs

Manual captures and scenario runs of 2026-09-05 (roll_msg.log with the
`[KEY]`/`[ SDMAP ]` sequence; tdeck_run_20260905-11*.log) live in the session
scratchpad and `tools/bench/`; the symbolized invalidation backtraces before
each loop gap all end in `add_map_point()` (marker font/x restyle) and the
map image, which is how the map was identified as the object being rebuilt.
