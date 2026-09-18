# G2 expected diffs against G1

Seeded 2026-09-12 from the drift-matrix decisions after the Fable review
(`docs/testplan/drift-matrix-review-verdict-20260912.md`), **before** any
unification wave has run, so that every prediction below can be shown wrong by
the capture rather than explained away after it. `test/golden/drift_matrix_lint.py`
fails if a row with `after_expect != identical` is not named here.

## What G2 compares

Under operator decision 5 (narrow G1), G1 captured `H4` BLE, `H6` UDP-1990
inbound, `H8` EXTUDP and the `H11` T-Deck checklist on all four nodes. Those are
the only steps with a G1 baseline, so those are the only steps G2 can compare.
`H1`, `H2`, `H3`, `H5`, `H7`, `H9` and `H10` rows in `protocol-after.md` are
filled `n/a` with the reason "no G1 baseline"; a change that only reaches one of
those surfaces is recorded below as **not comparable**, which is a statement
about the gate, not a claim that nothing changed.

## Rows that move a surface

Every `after_expect != identical` row, what it moves, and whether G2 can see it.
"Fault path" means the behaviour only appears under link-down, socket-failure or
error-limit conditions that no H-step injects; those rows are covered by the U2
native twin alone.

| Row   | Change                                                             | Surface                                            | G1 baseline? | Prediction at G2                                                                                                                                                                               |
| ----- | ------------------------------------------------------------------ | -------------------------------------------------- | ------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| DR-02 | nRF52 RX-01 guard on inbound GATE                                  | RAK-90 `H6` relay count                            | yes          | **No diff.** Every corpus callsign is `DK5EN-*`; the guard cannot fire                                                                                                                         |
| DR-03 | nRF52 35 s / 65 s heartbeat diagnostic; ESP32 age keyed `hb_age_s` | console only, after 35 s server silence            | no           | not comparable (`H3`); nothing at `H4`/`H6`/`H8`                                                                                                                                               |
| DR-04 | nRF52 honours `bGATEWAY_NOPOS`                                     | RAK-90 `H6` relay of position frames               | yes          | **No diff** with the default setting (`node_sset2 & 0x0100` clear on RAK-90)                                                                                                                   |
| DR-05 | nRF52 `sendDisplayPosition()` on relayed positions                 | RAK-90 display / BLE                               | H4 yes       | **No diff** at `H4`: the BLE capture runs without inbound GATE traffic                                                                                                                         |
| DR-06 | ESP32 stops on a failed decode                                     | `H6` on ESP32 nodes                                | yes          | **No diff.** Every corpus frame decodes                                                                                                                                                        |
| DR-07 | nRF52 outer `hasExternIPaddress` guard (style)                     | none on the wire                                   | --           | **No diff**                                                                                                                                                                                    |
| DR-08 | nRF52 CONF zero-address guard                                      | `H6` CONF on RAK-90                                | yes          | **No diff.** The corpus CONF frame has a resolved, matching source                                                                                                                             |
| DR-09 | nRF52 ACK phone frame via `buildAckPhoneFrame()`                   | RAK-90 `H4` BLE ack frame                          | H4 yes       | **No diff** at `H4`: the BLE corpus sends no message, so no `:ack` is received during the capture                                                                                              |
| DR-12 | keyed settings store (W3)                                          | `H2` settings JSON, BLE settings characteristic    | no           | not comparable at G2; gated by the W3 round-trip and the characteristic byte-compare (testplan 9.3)                                                                                            |
| DR-13 | schema-driven persistence, compat migration (W3)                   | `H2`, migration test                               | no           | not comparable at G2; gated by testplan 9.4                                                                                                                                                    |
| DR-16 | nRF52 `bTeleFirst`                                                 | RAK-90 first telemetry ~15 s earlier               | no (`H7`)    | not comparable; timestamps are masked anyway, only line order could show it                                                                                                                    |
| DR-18 | nRF52 gate-then-forward parity; JSON ack via `queueExtern()`       | `H8` `extudp-received.txt` on all four nodes       | yes          | **DIFF expected:** one new `{"type":"ack",...}` datagram per received `:ack` on every node; nothing else in the EXTUDP stream changes (the three-type set is unchanged, duplicates still pass) |
| DR-19 | nRF52 drops the `(uint8_t)` cast                                   | none (clamp at 255)                                | --           | **No diff**                                                                                                                                                                                    |
| DR-20 | ESP32 handler returns a status; `getMeshComUDP()` resets           | fault path (too-many-zeros)                        | no           | not comparable; U1 twin only                                                                                                                                                                   |
| DR-21 | nRF52 unresolved-destination return; `udp` ring overflow logged    | fault path                                         | no           | not comparable; U2 twin only                                                                                                                                                                   |
| DR-22 | nRF52 prints the frame after a failed send                         | fault path                                         | no           | not comparable; U2 twin only. Note: `TX-UDP` then means "encoded", not "sent"                                                                                                                  |
| DR-24 | ESP32 keys the failure on `endPacket()`, counts, drops             | fault path                                         | no           | not comparable; U2 twin only                                                                                                                                                                   |
| DR-25 | `[TX];leak;unconfigured` on both drains                            | instrument log                                     | H6 partial   | **No diff.** Every corpus callsign is `DK5EN-*`                                                                                                                                                |
| DR-26 | `TX-UDP` print: info gates, via decorates                          | console                                            | no           | not comparable; no G1 capture contains `TX-UDP`                                                                                                                                                |
| DR-28 | mHeard rendered most-recent-first (three renderers)                | `H3` console, `H11` T-Deck MHeard screen, BLE `MH` | H11 yes      | **H11:** the MHeard screen lists most-recent-first; `H4`: the `MH` frames are excluded as volatile, so no compared diff                                                                        |
| TS-01 (toggle-soak-20260918, not a drift-matrix row) | `--tempoff in/out` clamped to -50..50 °C via `cmdStoreFloat()`     | `H3` console (commands corpus)                     | no (`H3`)    | not comparable; `--tempoff in 999999` / `--tempoff out 999999` now answer `tempoff in/out <value> out of range (-50..50 °C), ignored` and leave the offset untouched (was: stored, `TEMP:` off `999999.000`); nothing at `H4`/`H6`/`H8`/`H11`                                                     |

## The one prediction that must be verified by hand before the run

**DR-18 at H8.** The JSON ack is the only change that moves a fully baselined
surface on all four nodes. `test/golden/compare_extudp.py` compares
`"src_type":"node"` + `"type":"msg"` lines only, so the new `"type":"ack"` line
must be added to its comparison set (or explicitly excluded with this file as
the reason) before G2, and `normalize.py` renumbers msg_ids in order of first
appearance, so an added datagram shifts the `<IDn>` sequence -- the pitfall
`compare_extudp.py:23-29` already documents.
