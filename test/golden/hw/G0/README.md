# G0 — hardware goldens at `dry-base-20260911`

Captured 2026-09-11 on the tagged base, before any code moves. The pass
criterion for G1 (after the carve-out) is that these compare clean; for G2
(after the unification) that they compare clean except where the drift matrix
says a value was deliberately changed.

| Node        | Board            | Image      | BLE stack | Reproducible |
| ----------- | ---------------- | ---------- | --------- | ------------ |
| `heltec-93` | Heltec V3 (S3)   | instrument | NimBLE    | 19 frames    |
| `rak-90`    | RAK4631 (nRF52)  | instrument | Bluefruit | 19 frames    |
| `t-beam-92` | T-Beam v1.2      | instrument | NimBLE    | 19 frames    |
| `t-deck-14` | T-Deck Plus (S3) | instrument | NimBLE    | 17 frames    |

All four run `INSTRUMENT_ENABLED` images (operator decision 2 of 2026-09-11),
built into `.pio/build-instr` so the shipping tree in `.pio/build` survives
alongside them. Each "reproducible" figure is two independent runs of that
node, each preceded by a configuration restore, compared and found identical —
not a count of what was recorded. `rak-90` is captured with `--pin 100000`;
the PIN never reaches a capture, the recorded hello is redacted.

Consequence of decision 2 that must not be dropped: because the goldens are
taken on instrument images, the string scan of the command-name list across
all 32 **shipping** images is now the only evidence about the shipping command
set. Done 2026-09-11, 0 unexplained absences on the four shipping envs.

## The branch has moved one command past this tag

`--keylock` was added after `dry-base-20260911` (the `TD-16` fix, commit
`76302aab`). The command corpus in `test/golden/corpus/commands/` holds **305**
ladder entries and does not contain it, which is correct for the tag and stale
for the branch head. That is a deliberate divergence, not corpus rot:

- a G1 console capture on the branch head will show one command the G0 capture
  does not. It belongs on the expected-diff list; it does not invalidate G0.
- the shipping-image command-name scan predates `--keylock` too and has to be
  re-run before it can be cited again as evidence about the shipping command
  set.
- regenerate with `python3 test/golden/build_corpus.py` when G1 is taken, not
  before -- regenerating now would leave G0's own baseline describing a tree
  that no capture was taken on.

Compare a later run against these with:

```sh
python3 tools/bench/ble_golden.py --compare \
    test/golden/hw/G0/heltec-93/ble-frames.bin \
    test/golden/hw/G1/heltec-93/ble-frames.bin
```

## What is compared, and what is not

`ble-frames.bin` holds only the frames that repeat: replies attributed to a
write, with `msg_id` excluded (minted from `millis()`) and the values of live
readings masked. Everything else is recorded in `ble-frames.txt` and
`ble-burst.txt` but kept out of the comparison, each for a measured reason:

| Excluded                    | Why                                                                                                     |
| --------------------------- | ------------------------------------------------------------------------------------------------------- |
| the pre-corpus burst        | the node sends it unasked after hello; it cannot be attributed to a write                               |
| inbound mesh traffic        | frames whose path **originator** is not the node under test — live traffic that will not repeat         |
| the `MH` register           | its content is the mheard table, different callsigns every run — not just different values              |
| the binary ack (`0x41`)     | asynchronous; it lands inside a reply window in one run and after the last write in the next            |
| repeated echoes             | a node echoes an outgoing message once or twice depending on whether it hears its own transmission back |
| values of live-reading keys | `BATV`, `TEMP`, `LAT`, `SAT`, `DATE` and the rest of `VOLATILE_JSON_KEYS`; the **keys** stay compared   |

Each of those was found by capturing twice and diffing, not by reasoning:
before the fixes the two runs differed in 14 places, all of them artefacts.

## Restore before every run — not optional

The corpora mutate the node. The BLE corpus alone contains `--maxhop 5`, and
the command script drives `--<cmd> 1 / 999999 / abc` for every setter in the
ladder. Measured on T-Beam-92: one capture moved `max_hop_text` from 4 to 5
permanently, and the next run's frames carried the new hop budget in their
flags byte — two runs of the same firmware, differing because of the first
one. A capture that does not restore first measures the previous run's
leftovers.

```sh
python3 test/golden/backup_nodes.py --restore t-beam-92=192.168.68.72
```

It POSTs the unmasked vault backup, the node reboots (so the HTTP response
usually never arrives — that is success), and the tool then re-reads the node
and reports any field that did not come back. `node_lat/lon/alt` are excluded:
the GPS rewrites them and no import can restore them.

With restore in front of each run, all four nodes reproduce exactly.

## UDP-1990, captured on all four nodes

All four read the same corpus the same way — the classification agrees on every
entry, and the counts differ only where the KEEP cadence fell:

| Node        | Platform      | DATA | BEAT | CET | OTHER | CONF | own uploads     |
| ----------- | ------------- | ---: | ---: | --: | ----: | ---: | --------------- |
| `heltec-93` | ESP32-S3      |   24 |    7 |   3 |     2 |    1 | 3 KEEP          |
| `rak-90`    | nRF52         |   26 |    4 |   3 |     2 |    1 | 2 KEEP + 2 DATA |
| `t-beam-92` | ESP32 classic |   24 |    8 |   3 |     2 |    1 | 3 KEEP + 2 DATA |
| `t-deck-14` | ESP32-S3      |   24 |    5 |   3 |     2 |    1 | 4 KEEP          |

The `DATA` uploads are acks for the corpus DM, which is addressed to
`DK5EN-90` — so only a node that considers itself the addressee generates one,
and `t-deck-14` rightly stays quiet. That asymmetry is only visible because the
surface was captured per node rather than once, and a shared `D1-01` handler
has to preserve it.

`rak-90` is the case the nRF52 `--srvip` hook was written for: before
`ee545088` that board could not be pointed at a stub at all.

## Not yet captured

The BLE surface only, on all four nodes. Still owed from the test plan's
section 6: the console command golden over USB and TCP 2323, UDP-1990 through
the stub server (now driveable on every node — the nRF52 `--srvip` hook landed
2026-09-11), EXTUDP, and the T-Deck UI checklist, which is manual.
