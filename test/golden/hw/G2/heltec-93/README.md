# BLE golden, Heltec-93 (test plan step H4) -- G2

Attempted 2026-09-17 on `dry-unification` `HEAD` (the same stock,
non-instrument image the `extudp/` G2 capture in this directory was taken
against). Two-phase protocol per operator decision: two captures of the
**unchanged** image must agree with each other before either is compared to
`test/golden/hw/G1/heltec-93/`.

## Result: phase 1 did NOT agree -- stopped before phase 2

No G1 comparison was run. There is no canonical G2 BLE capture for this node
in this directory; `phase1-run1/` and `phase1-run2/` are the two disagreeing
attempts, kept as evidence.

```
tools/bench/ble_golden.py --compare phase1-run1/ble-frames.bin phase1-run2/ble-frames.bin
phase1-run1/ble-frames.bin: 19 frames
phase1-run2/ble-frames.bin: 20 frames
  differ at 12..18  (7 of 19 shared frames)
1 trailing frame in the longer capture (not counted as a failure on its own)
7 difference(s)
```

## Root cause: the `--conffin` reply races its own write-timestamp log

Frame 12 is the reply to the `--conffin` corpus entry. In `phase1-run1` it
never appears as a `reply` at all -- it is bumped to `ble-burst.txt`'s sibling,
the excluded/`spontaneous` section of `ble-frames.txt`, at `t=34.642`. In
`phase1-run2` the identical `{"TYP":"CONFFIN"}` reply lands as an ordinary
`reply <- '--conffin'` at `t=34.927`. Everything from frame 12 onward is
shifted by exactly one position between the two runs as a result -- the same
failure mode `Capture.attribute()`'s docstring already describes for spontaneous
position beacons, but triggered here by ordinary reply-vs-write timing instead.

The write log for `--conffin` in run 1 reads:

```
34.643 text '--conffin'          0ba02d2d636f6e6666696e
```

and the notification that answers it was time-stamped `34.642` -- **one
millisecond before its own write was logged**. `Capture.on_write()` logs the
write's timestamp only after `await client.write_gatt_char(...)` returns, but
the node's BLE stack evidently answered fast enough that the notify callback
fired before that await resolved and the log line was appended. `attribute()`
then looks for the most recent write at or before `t=34.642`; the `--conffin`
write is not yet in `self.writes` at that instant, so the preceding write it
finds is `--aprsset` at `32.482`, `34.642 - 32.482 = 2.16 s` is outside the
`1.9 s` reply window, and the frame is classified `spontaneous` and dropped
from the compared `.bin`. In run 2 the same reply arrived 0.285 s later in
wall-clock terms relative to the corpus start and landed after its write was
logged, so it was attributed normally.

This is bench jitter in the capture harness's write/notify race, not a
firmware behavioural difference -- the reply content itself
(`{"TYP":"CONFFIN"}`) is identical in both runs; only which write it gets
filed under differs. It still means the two baselines disagree exactly as
the operator's rule anticipates, so per the standing instruction the run
stops here rather than proceeding to a G1 diff that could not distinguish
this artifact from a real regression.

## What was NOT done

- No comparison against `test/golden/hw/G1/heltec-93/`.
- No canonical `ble-frames.bin` etc. was promoted to this directory's root;
  only the two phase-1 attempts exist, under `phase1-run1/` and
  `phase1-run2/`.

## Destination audit

Zero destinations went out over RF from this node's own initiative in either
run. The tool's own `verify_no_broadcast()` guard (which fires on any
own-originated frame addressed to `*`) reported nothing in either run (exit
code 0, no `*** BROADCAST TRANSMITTED ***` block). A follow-up scan of every
notification in both runs for a `path` originator starting `DK5EN-93` found
**zero** such frames of any kind -- the BLE write corpus
(`test/golden/corpus/ble/writes.txt`) contains no message-send entries, and no
spontaneous position beacon happened to fire during either ~60 s capture
window. The `dst":"*"` entries that do appear in the captures (5 in run 1, 5
in run 2) are all local command-response echoes to the adversarial
`--wrong command ...` corpus entries -- `path=['response']`, never
transmitted over LoRa; this is the same shape the tool's `own_source()` and
`verify_no_broadcast()` deliberately treat as non-RF (`originator == "response"`).

## Node state

`--maxhop 5` is one of the corpus's adversarial entries and is a real setter.
The node's `--info` already showed `MAXHOP text 5` before this run (left over
from an earlier capture on this bench), so nothing needed restoring here. See
`test/golden/hw/G2/t-deck-14/README.md` for the node where this setting *did*
have to be restored. Full before/after `--info` dumps (serial, independent of
the BLE path) confirm no other field changed; `TIME` (uptime) differs only
because opening the CP2102 serial port to fetch `--info` resets the board,
which is expected and does not touch persisted settings.

## Owed

A clean two-agreeing-run G2 BLE baseline for this node, either by widening the
reply window past ~2.2 s (at the cost of maybe re-admitting the
already-documented position-beacon race) or by timestamping the write at
*send* time inside `write_gatt_char`'s call rather than after it returns.
