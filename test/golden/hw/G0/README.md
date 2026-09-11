# G0 — hardware goldens at `dry-base-20260911`

Captured 2026-09-11 on the tagged base, before any code moves. The pass
criterion for G1 (after the carve-out) is that these compare clean; for G2
(after the unification) that they compare clean except where the drift matrix
says a value was deliberately changed.

| Node        | Board           | BLE stack | Capture                                                   |
| ----------- | --------------- | --------- | --------------------------------------------------------- |
| `heltec-93` | Heltec V3 (S3)  | NimBLE    | `ble-frames.{bin,txt}`, `ble-burst.txt`, `ble-writes.txt` |
| `rak-90`    | RAK4631 (nRF52) | Bluefruit | same, captured with `--pin 100000`                        |

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

## Reproducibility, measured

Two independent runs per node with the final tool:

- `heltec-93`: 21 frames, identical
- `rak-90`: 20 frames, identical

That is what makes G1 and G2 meaningful: a difference there is the refactor,
not the bench.

## Not yet captured

The BLE surface only. The console, UDP-1990, EXTUDP and T-Deck UI steps of the
test plan's section 6 are still owed, as are the `t-beam-92` and `t-deck-14`
nodes.
