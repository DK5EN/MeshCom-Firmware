# BLE golden, T-Deck Plus-14 (test plan step H4) -- G2

Attempted 2026-09-17 on `dry-unification` `HEAD`, the same stock image as the
Heltec-93 attempt in this directory's sibling. Two-phase protocol per operator
decision: two captures of the **unchanged** image must agree with each other
before either is compared to `test/golden/hw/G1/t-deck-14/`.

## Result: phase 1 did NOT agree -- stopped before phase 2

No G1 comparison was run. `phase1-run1/` and `phase1-run2/` are the two
disagreeing attempts, kept as evidence; there is no canonical G2 BLE capture
for this node.

```
tools/bench/ble_golden.py --compare phase1-run1/ble-frames.bin phase1-run2/ble-frames.bin
phase1-run1/ble-frames.bin: 18 frames
phase1-run2/ble-frames.bin: 18 frames
  differ at 14, 15, 16  (of 18 shared frames)
3 difference(s)
```

## Root cause: MAXHOP state carried over between the two runs -- a procedural
## gap in this capture, not inherent bench jitter

The three differing frames are the local `--wrong command` responses to
`--infoX`, `--heap` and `--heap tag`. Their JSON/text content is byte-identical
between the runs; only `parsed.flags` differs, 34 (`0x22`) in run 1 vs. 37
(`0x25`) in run 2 -- the low nibble of the flags byte is documented in
`test/golden/mc_frame.py` as `flags + hop nibble`, i.e. `2` in run 1 and `5` in
run 2. The raw bytes make it unambiguous:

```
run1 --infoX reply:  ...3a c0 f7 03 00 22...   byte[5]=0x22, hop nibble=2
run2 --infoX reply:  ...3a c2 1b 05 00 25...   byte[5]=0x25, hop nibble=5
```

The corpus (`test/golden/corpus/ble/writes.txt`) ends with `--maxhop` (a query)
then **`--maxhop 5` (a real setter)**, run near the end of every pass. In run 1
the node's persisted MAXHOP was still its pre-capture value, **2** (confirmed
by `--info` before this session: `MAXHOP text 2 / pos 2`), for every entry up
to and including `--heap tag`, because `--maxhop 5` had not executed yet in
that same run. Run 1's own `--maxhop 5` write then set it to 5 -- and it was
never restored before run 2 started, because run 2 was launched immediately
back-to-back as the second phase-1 attempt. So by the time run 2 reached
`--infoX`/`--heap`/`--heap tag`, MAXHOP was already 5, and the hop nibble the
firmware stamps into every locally-generated response frame reflects that.

This is **not** the same class of finding as the Heltec-93 CONFFIN race (a
genuine timing race inherent to the capture tool/BLE stack that would recur on
a perfectly clean re-run). Here the two "unchanged-image" runs were not
actually run from the same node state: the corpus itself contains a setter,
and nothing between `phase1-run1` and `phase1-run2` restored the value it set.
That is a gap in how this session executed the protocol, not evidence the
image behaves non-deterministically. It still means the two baselines
disagree, so per the standing instruction the run stops here without a G1
comparison -- but the fix is procedural (restore `--maxhop` to its pre-capture
value between the two phase-1 attempts, not just after the whole session) and
the resulting BLE surface itself was not shown to be non-deterministic on this
node.

## What was NOT done

- No comparison against `test/golden/hw/G1/t-deck-14/`.
- No canonical `ble-frames.bin` etc. was promoted to this directory's root;
  only the two phase-1 attempts exist, under `phase1-run1/` and
  `phase1-run2/`.

## Destination audit

Zero destinations went out over RF from this node's own initiative in either
run. `verify_no_broadcast()` reported nothing in either run (exit code 0). A
follow-up scan of every notification in both runs for a `path` originator
starting `DK5EN-14` found zero such frames of any kind -- the corpus contains
no message-send entries and no spontaneous position beacon fired during either
~55 s window. The `dst":"*"` entries present (3 per run) are all local
`--wrong command` response echoes (`path=['response']`), never transmitted
over LoRa.

## Node state -- MAXHOP had to be restored

`--info` before this session: `MAXHOP text 2 / pos 2`. After both phase-1 runs
(which each execute the corpus's `--maxhop 5` setter once, and neither run
undid the other's), a serial `--info` read back `MAXHOP text 5 / pos 2`.
Restored with `--maxhop 2` over the serial console
(`tools/bench/serial_session.py /dev/cu.usbmodem1101 --wait-boot --listen 8
"--maxhop 2" "--info"`), confirmed back to `MAXHOP text 2 / pos 2`. Every
other field in the before/after `--info` dumps is identical except `TIME`
(uptime), which resets because opening the port to run `--info` resets the
board and is expected.

## Owed

A clean two-agreeing-run G2 BLE baseline for this node: re-run phase 1 with a
`--maxhop 2` restore issued between `run1` and `run2` (not just after the
session), so both runs start from the same persisted MAXHOP. If they then
still disagree, that would be a genuine finding rather than this session's
procedural gap.
