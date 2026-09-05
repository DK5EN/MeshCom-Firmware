# T-Deck Plus: cursor dead time while rolling the trackball -- RCA 2026-09-05

Operator report (DK5EN-14, build 11:03 with TD-12/TD-13): after new messages
have arrived and the menu bar was collapsed once, rolling the trackball freezes
the cursor for a fraction of a second every couple of seconds.

## Verdict (revised 13:30, after the operator isolated the trigger)

**Root cause CDC-01: Serial prints block the main loop once the USB host is
gone.** On the ESP32-S3 boards `Serial` is the native USB-JTAG/CDC (HWCDC).
arduino-esp32 2.0.14 (`cores/esp32/HWCDC.cpp`) starts with a TX timeout of
0, raises it to 100 ms on the first successful host read
(`hw_cdc_isr_handler`, `initial_empty`) and never lowers it again. With the
cable pulled or the terminal closed nobody drains the 256 B TX ring, so every
print that does not fit blocks `write()` for 100 ms per call: the `[BALL]`
line per cursor step, the four GPS lines every 3 s, `[LOG]` lines. The
trackball cursor and the touch input freeze in that rhythm because both are
read from the same main loop. That is exactly the operator's observation:
"it starts stalling after the USB serial connection is closed".

Fix (`net_console.cpp`, `MeshSerialClass::begin()`, mirrored in
`esp32_main.cpp` for builds without the net console): `setTxTimeoutMs(0)`
right after `Serial.begin()` -- the core honours an explicit request
(`tx_timeout_change_request`) -- plus `setTxBufferSize(4096)` so bench logs
with a connected host do not lose lines now that a full ring drops instead
of waiting.

Proof on DK5EN-14 (`tools/bench/tdeck_cdc_unplug.py`, loop-gap counters
carried across the port-open reset in RTC memory, `[INSTR-PREV]`):

| Build           | Cable away | Loop gaps > 250 ms | Longest gap           |
| --------------- | ---------- | ------------------ | --------------------- |
| without the fix | 44 s       | 7                  | 1812 ms               |
| with the fix    | 33 min     | 1                  | 586 ms, in lvgl (map) |

The one remaining gap is a map recomposition (section lvgl, 470-750 ms
class), see below -- not a print.

Why the harness could not reproduce it: pausing the host reader
(`cdc_backpressure` scenario) does not stop macOS from issuing USB IN
transfers, the driver keeps buffering, and the node never sees a full ring.
Only a physically absent host does. The scenario stays as a guard for the
opposite regression (a future core that blocks even with a host present).

## Secondary finding: map recompositions (TD-14)

The dead times captured while the cable was still connected are SD map
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
