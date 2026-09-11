# G1 expected diffs against G0

Written **before** the G1 run, 2026-09-11, so that every prediction below can
be shown wrong by the capture rather than explained away after it.

G0's stated pass criterion is "every normalized capture identical to G0; any
diff stops the plan until explained". That criterion is no longer automatically
right: the tree has moved past `dry-base-20260911` by more than the carve-outs.
This file lists everything that changed and states, per change, whether it can
reach a captured surface.

## What is captured at G1

Under operator decision 5 (narrow G1), the same four steps as G0 and no more:
`H4` BLE, `H6` UDP-1990 inbound, `H8` EXTUDP, `H11` the T-Deck UI checklist.
`H1`, `H2`, `H5`, `H7` and `H9` are not captured at either end, so they cannot
diff. `H3` (console) has no G0 baseline on three of four nodes and is out.

## Changes between the tag and this run

| #   | Change                                           | Reaches a captured surface?                                                           |
| --- | ------------------------------------------------ | ------------------------------------------------------------------------------------- |
| 1   | `C1`..`C5` carve-outs                            | **No diff expected.** Behaviour-neutral by construction                               |
| 2   | `RF-01`, `RF-02`, `RF-05` radio-unit write paths | **No.** Only `--txbw`/`--txcr`/`--txfreq` reach them, and none is in a G1 corpus      |
| 3   | `RF-03` manual country validation                | **No.** Only `--setctry 7` reaches it; `--setctry` is manual-only and not in a corpus |
| 4   | `RF-06` `--info` frequency readout               | **Possible on RAK-90 only** — see below, this is the one to watch                     |
| 5   | `--keylock on/off` (TD-16)                       | **No.** Two new ladder entries, driven by no G1 corpus                                |
| 6   | `MEM-05` extra `INADDR_NONE` copy                | **No.** RAM layout only, nothing observable on a wire                                 |
| 7   | `--setctry` held back from the console script    | **No.** `H3` only, and `H3` is not in G1                                              |

## The one prediction that could be wrong: RF-06 on RAK-90

`--info` **is** in the BLE corpus, so its output is compared. `RF-06` changed
how the frequency readout is normalized:

```c
- #ifdef BOARD_RAK4630
-     node_qrg = node_qrg / 1000000.0;      // float / double -> double -> float
- #endif
+ node_qrg = radioFreqStoredToMhz(node_qrg, radioUnitsIndexed());   // float / float
```

On the three ESP32 boards both forms are a no-op and the output cannot change.
On **RAK-90** both divide, but the old form promoted to `double` and the new
one stays in `float`, so the results can differ by one ulp. `433175000.0f` is
not exactly representable as a `float` to begin with.

Printed with `%.4f` (`command_functions.cpp:5951`) a one-ulp difference should
not be visible: 433.1750 either way. **Predicted: no diff.** If the RAK-90 BLE
capture shows a changed `FREQ` line and nothing else, that is this and it is
cosmetic — but it must be recorded as a real diff, not waved through, because
it would mean the float/double change is observable and the same reasoning
applies to `getFreq()` elsewhere.

Reference, captured from the nodes **before** flashing (G0 firmware):

```
rak-90     ...CTRY EU8  ...FREQ 433.1750 MHz TXPWR 22 dBm RXBOOST off
heltec-93  ...CTRY EU8  ...FREQ 433.1750 MHz TXPWR 2 dBm RXBOOST off
```

Full pre-flash `--info` dumps are in the run directory.

## Node coverage

The bench has three usable USB slots, not four: plugging the T-Deck in
displaces the T-Beam and vice versa. G1 is therefore captured in two passes.
Whichever node is not captured is named here rather than left implied.

## What a diff means

A diff that is **not** in this table stops the plan. A diff that is in it is
explained, and the explanation was written down first.
