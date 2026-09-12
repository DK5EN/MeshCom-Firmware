# Bench session 2026-09-12: W3 upgrade baseline and the EXT-01 confirmation

Two jobs, one session, on the USB bench fleet. Nothing here transmitted: mesh
and gateway were off on the node under test, every injection went in on the RX
path with `--injectraw`, no frame was sourced from a foreign callsign, and no
destination was `*` (group `9999` and the telemetry path `100001` only).

## 1. The pre-cutover settings baseline

`W3`'s acceptance criterion is that a node configured on the previous firmware
keeps every setting across the upgrade. That is only checkable against a
recorded "before", so here it is -- the full `configExportJson` output of each
node while it is still running a `FLASH_STRUCT_VERSION 20260724` image:

| node       | board     | file                            | layout   | fw    | fields |
| ---------- | --------- | ------------------------------- | -------- | ----- | -----: |
| `DK5EN-90` | RAK4631   | `rak90-config-20260912.json`    | 20260724 | 4.35t |    103 |
| `DK5EN-92` | T-Beam    | `tbeam92-config-20260912.json`  | 20260724 | 4.35t |    107 |
| `DK5EN-93` | Heltec V3 | `heltec93-config-20260912.json` | 20260724 | 4.35t |    107 |

**These counts are also an independent check on the `W3` schema.** The export
walks `CFG_FIELD_LIST`, and the row counts derived from the source
(`624e8f50`) are 101 common + 2 nRF52 / 6 ESP32 platform rows -- so 103 on the
RAK and 107 on both ESP32 boards is exactly what the table predicts. Live
hardware and the static count agree.

After the cutover, re-export from the same three nodes and diff against these
files. A field that changes value, disappears, or arrives with a different
spelling is a migration defect.

## 2. `EXT-01` -- confirmed, and its impact is smaller than the row claimed

Injected three frames into `DK5EN-93` (`--extudp on`, EXT IP `192.168.68.58`),
built from corpus `f006` with fresh message ids; the frame builder's self-check
is that rebuilding `f006` unchanged reproduces its committed FCS.

| #   | frame             | expected                     | observed                            |
| --- | ----------------- | ---------------------------- | ----------------------------------- |
| A   | `DK5EN-98>9999`   | datagram on 1799             | datagram, `[EXT] Out: ... Len: 153` |
| B   | `DK5EN-98>100001` | `[EXT] Out:  Len: 0` + reset | **exactly that**, no datagram       |
| C   | `DK5EN-98>9999`   | (the open question)          | datagram, `Len: 151`                |

**The mechanism is confirmed as read.** A `0x3A` frame addressed to the
telemetry path `100001` builds no JSON, reaches `UdpExtern.write(c_json, 0)`
with an empty buffer, prints the predicted marker, and the failed write calls
`resetExternUDP()` -- visible in the log as the socket immediately
re-announcing itself.

**But it does not leave EXTUDP dead.** The row said the reset "tears the
socket down and clears `hasExternIPaddress`", which reads as an outage. It is
not one: `resetExternUDP()` clears the flag and then calls `startExternUDP()`
again, guarded by `bEXTUDP && strlen(node_extern) > 7` -- the same condition
that was required to reach the bug in the first place, so the re-establish
always fires. Frame C proves it end to end: the very next message goes out
normally.

So the real cost is **a full UDP socket teardown and rebuild for every
telemetry text frame the node hears**, not a loss of export. On a gateway in a
telemetry-carrying mesh that is continuous churn, and it opens a `stop()`/
`begin()` window on a socket the node also listens on -- worth fixing, but it
is not the fleet-visible outage the row implied. The fix is unchanged: an
early return before the send block.

Serial capture: `ext01-heltec93-serial-20260912.txt`.

## Bench state left behind

- `DK5EN-93` carries an `INSTRUMENT_ENABLED=1` build (needed for
  `--injectraw`), not a stock image. `--extudp` was set back to `off` and mesh
  stayed off, so its settings are as found.
- `DK5EN-90` and `DK5EN-92` were read only -- not reflashed, not reconfigured.
  **Leave `DK5EN-90` on its current image**: it is the article for the `W3`
  upgrade proof and has to make that jump from a real 20260724 node.
