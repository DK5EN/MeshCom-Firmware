#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["bleak>=0.22"]
# ///
"""Regression check for the `{pong}`-to-BLE fix (src/lora_functions.cpp,
the `{pong}` branch of the DM-to-self handler).

A `{pong}` DM addressed to the node itself is shown on the display but was
never handed to the BLE client -- the plain-DM `else` branch ~90 lines
further down already had the `addBLEOutBuffer(RcvBuffer, size)` call that the
`{pong}` branch lacked. This script proves the regression both ways: it must
FAIL (exit 1) on an unfixed image and PASS (exit 0) on the fixed one, and it
must first prove its own instrument works (a plain DM reaching BLE) before
saying anything about the `{pong}` case -- otherwise a broken BLE/injectraw
path would silently look like "the fix is missing".

Connects exactly like `ble_golden.py` (same NUS UUIDs, same hello/timesync
handshake so `isPhoneReady` gets set -- `src/phone_commands.cpp:326`), then
injects two crafted raw LoRa text frames through the node's real RX path via
`--injectraw <hex>` (`src/command_functions.cpp:4525`,
`src/test_inject.cpp:351`) and watches for each probe's unique token to come
back over the NUS notify characteristic:

    control: <src>><own>:pbc<8 random hex>          -- a plain DM
    pong:    <src>><own>:{pong}{<random 1-10 digits>}

Both are addressed to the node's own callsign and use a fresh random msg id
each run -- the node's dedup ring would otherwise swallow a repeat send.

**Needs an instrumented image.** `--injectraw` lives inside `#if
INSTRUMENT_ENABLED` (`src/command_functions.cpp:4389`), so a production build
answers `...wrong command --injectraw` on its console and this script reports
VOID. Build the image under test with
`PLATFORMIO_BUILD_FLAGS="-DINSTRUMENT_ENABLED=1"` (no space after `-D`), for
the "before" image as well as the "after" one.

**ESP32 only.** `test_inject_raw()` refuses outright on `BOARD_RAK4630`
(`src/test_inject.cpp:353-362`, `"[INJ];raw;err;unsupported_on_this_board"`)
-- a RAK4631/nRF52 bench node cannot run this check at all, instrumented or
not. Point `--name`/`--address` at an ESP32 target.

Frame layout is the one in `test/golden/mc_frame.py` / doc 11 section 1.1,
verified against `test/test_aprs_corpus/corpus.txt` f002 (see `--self-test`
and `test_pong_ble_check.py`). hw/mod/trailer are copied from corpus f007,
a genuine direct (single-hop, unrelayed) text DM (`DK5EN-98 > DK5EN-90`).

**Server bit (0x80): always set, not an option.** The bench target can be a
node running `--gateway on`; a gateway uploads every non-server-flagged RX
text frame to the MeshCom server (`src/lora_functions.cpp:1735`,
`if(bGATEWAY && (!aprsmsg.msg_server || ...)) addNodeData(...)`), and the
server redistributes it to other gateways' RF. A server-flagged frame skips
that upload -- and there is no server check on the DM-to-self branch -- so
the fix path is exercised identically either way, and there is no case where
clearing the bit is the safer choice.

**Hop nibble (max_hop, the low 4 bits of the flags byte): always 0.** Nothing
between `decodeAPRS()` and the DM-to-self branch reads it for a frame
destined at the node's own call, but if `--own-call` is wrong (a case
mismatch is enough -- the `strcmp()` at `src/lora_functions.cpp:1332` is
case-sensitive) the frame becomes a third-party DM instead: `checkMesh()`
returns mesh-eligible for a destination with no via path
(`src/via_functions.cpp:114`), and the relay guard at
`src/lora_functions.cpp:1802` only checks `(max_hop & 0x0F) > 0` -- with a
non-zero hop nibble the node would TRANSMIT both probes. Hop 0 makes that
guard refuse regardless of what `--own-call` turned out to match. The server
bit does not gate relaying, so it cannot substitute for this.

**Hard rule**: `--src-call` must start with `DK5EN-`. Never a foreign
callsign as a frame source in anything that can transmit.

Exit codes:
    0   both probes arrived over BLE (the fix is present)
    1   the control DM arrived, the pong did not (the fix is absent --
        the expected "before" result)
    2   usage error, --src-call refused, or the node was not found by
        --name/--address within the scan window
    3   the control DM did not arrive: the instrument itself is void
        (BLE/injectraw path broken, a non-instrumented image, or a
        RAK4631/nRF52 board) -- no statement about the fix

Run with uv, same as ble_golden.py:

    uv run --with bleak tools/bench/pong_ble_check.py --name DK5EN-93 \\
        --own-call DK5EN-93 --src-call DK5EN-97
    python3 tools/bench/pong_ble_check.py --self-test   # frame builders only, no BLE
"""

from __future__ import annotations

import argparse
import asyncio
import random
import sys
import time
from pathlib import Path
from typing import List, Optional, Tuple

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ble_golden as bg  # noqa: E402  (path insert must come first)
from identity_guard import add_guard_args, enforce  # noqa: E402

# --------------------------------------------------------------- frame layout

# doc 11 section 1.1 / test/golden/mc_frame.py. Every probe here is a text
# message ':' -- the only payload type this script builds.
FRAME_TYPE_TEXT = 0x3A

# flags byte bits (src/aprs_functions.cpp:182-192):
#   0x80 server   0x40 track   0x20 app_offline   0x10 mesh   0x0F max_hop
FLAG_SERVER = 0x80

# max_hop nibble 0, track/offline/mesh bits clear. NOT copied from f007
# (which carries hop 4): a wrong or wrong-case --own-call turns both probes
# into a third-party DM instead of a DM-to-self, and with a non-zero hop
# nibble the relay guard at src/lora_functions.cpp:1802 would TRANSMIT them
# (see the module docstring). Hop 0 keeps that guard closed no matter what
# --own-call actually matched.
FLAGS_DIRECT_BASE = 0x00

# The server bit is always set -- see the module docstring ("Server bit").
PROBE_FLAGS = FLAG_SERVER | FLAGS_DIRECT_BASE

# Copied from corpus f007 (test/test_aprs_corpus/corpus.txt): a genuine
# direct (single-hop, unrelayed) text DM, DK5EN-98 -> DK5EN-90. Only hw/mod/
# trailer are taken from it; flags are not (see FLAGS_DIRECT_BASE above).
HW_ID = 0x2B
MOD_BYTE = 0x88
TRAILER = bytes.fromhex("23ab707e")   # FW=0x23, LASTHW=0xab, FW-sub=0x70, 0x7e end

CONTROL_PREFIX = "pbc"


def build_frame(msg_id: int, flags: int, path: List[str], dest: bytes,
                 payload: bytes, hw: int = HW_ID, mod: int = MOD_BYTE,
                 trailer: bytes = TRAILER) -> bytes:
    """One raw text-message LoRa frame, doc 11 section 1.1.

    Mirrors `test/golden/mc_frame.py`'s `Frame.head()`/`encode()` exactly:
    type + msg_id (4B LE) + flags + "path,path>dest" + type-byte-as-
    terminator + payload + 0x00 + hw + mod + FCS (2B BE, plain byte sum of
    everything before it, mod 0x10000) + trailer. The destination terminator
    is a REPEAT of the type byte -- 0x3A is also ASCII ':', which is why the
    frame reads as "srcpath>dst:payload" in plain text.
    """
    head = (
        bytes([FRAME_TYPE_TEXT])
        + (msg_id & 0xFFFFFFFF).to_bytes(4, "little")
        + bytes([flags])
        + ",".join(path).encode("ascii")
        + b">"
        + dest
        + bytes([FRAME_TYPE_TEXT])
        + payload
        + b"\x00"
        + bytes([hw, mod])
    )
    fcs = sum(head) & 0xFFFF
    return head + fcs.to_bytes(2, "big") + trailer


def random_msg_id() -> int:
    """A fresh 32-bit id; the node's dedup ring would swallow a repeat."""
    return random.getrandbits(32)


def random_hex(n: int) -> str:
    return "".join(random.choice("0123456789abcdef") for _ in range(n))


def random_pong_digits() -> str:
    """An unsigned decimal, 1 to 10 digits (str() never pads, so `0` is 1
    digit and the ceiling is 9999999999, 10 digits)."""
    return str(random.randint(0, 10**10 - 1))


def build_control_probe(src_call: str, own_call: str,
                         msg_id: Optional[int] = None) -> Tuple[bytes, str]:
    """A plain DM the node forwards to BLE today (the already-fixed branch).

    No `{` in the payload: a `{NNN` suffix on a DM makes the node key an ACK
    on air (src/lora_functions.cpp, `iEnqPos`/`SendAckMessage`), which this
    probe must not trigger. The prefix also guarantees a non-hex character
    (`p`) in the token -- see `build_pong_probe()`.
    """
    token = CONTROL_PREFIX + random_hex(8)
    assert "{" not in token
    frame = build_frame(msg_id if msg_id is not None else random_msg_id(),
                         PROBE_FLAGS, [src_call],
                         own_call.upper().encode("ascii"), token.encode("ascii"))
    return frame, token


def build_pong_probe(src_call: str, own_call: str,
                      msg_id: Optional[int] = None) -> Tuple[bytes, str]:
    """The frame the `{pong}` branch must also hand to BLE once fixed.

    The token always contains `{`/`}`/letters, i.e. characters that cannot
    appear in a hex string. That matters: a production (non-instrumented)
    image can echo a rejected `--injectraw <hex>` command back over BLE
    (`...wrong command --injectraw <hex>`); if the token were hex-only it
    could match inside that echoed hex string and turn a broken instrument
    into a false PASS. See `test_frame_contains_token` /
    `TestControlProbe.test_no_open_brace_in_control_payload` and
    `TestPongProbe.test_token_shape` for the pinned invariant.
    """
    token = "{pong}{" + random_pong_digits() + "}"
    frame = build_frame(msg_id if msg_id is not None else random_msg_id(),
                         PROBE_FLAGS, [src_call],
                         own_call.upper().encode("ascii"), token.encode("ascii"))
    return frame, token


# --------------------------------------------------------------- src-call guard


def check_src_call(src_call: str) -> Optional[str]:
    """None if `src_call` is allowed as a frame source, else the refusal message.

    Hard rule: never a foreign callsign as a frame source in anything that
    can transmit (this repo's `never-use-foreign-callsign-oe1xar` rule).
    """
    if not src_call.upper().startswith("DK5EN-"):
        return (f"--src-call must start with 'DK5EN-' (never a foreign "
                f"callsign as a frame source): got {src_call!r}")
    return None


# --------------------------------------------------------------- verdict


VERDICT_MESSAGES = {
    0: "PASS: both the control DM and the {pong} DM reached BLE -- the fix is present",
    1: "FAIL: the control DM reached BLE, the {pong} DM did not -- the fix is absent",
    3: "VOID: the control DM never reached BLE -- the instrument itself is broken, "
       "no statement about the {pong} fix (a production image answers "
       "'wrong command --injectraw': flash an INSTRUMENT_ENABLED=1 build)",
}


def verdict(control_seen: bool, pong_seen: bool) -> int:
    """(control seen, pong seen) -> exit code, per the brief's table."""
    if not control_seen:
        return 3
    return 0 if pong_seen else 1


# --------------------------------------------------------------- BLE run


def frame_contains_token(frame: bytes, token: str) -> bool:
    """True if `frame` is a 0x40 text/pos notification whose decoded LoRa
    frame's payload contains `token`."""
    if not frame or frame[0] != 0x40:
        return False
    sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "test" / "golden"))
    import mc_frame
    parsed = mc_frame.parse(frame[1:])
    if parsed is None:
        return False
    return token.encode("ascii") in parsed.payload


class Listener:
    def __init__(self) -> None:
        self.notifications: List[bytes] = []

    def on_notify(self, _sender, data: bytearray) -> None:
        self.notifications.append(bytes(data))


async def wait_for_token(listener: Listener, start_index: int, token: str,
                          timeout: float) -> bool:
    """Poll `listener.notifications[start_index:]` for `token` up to `timeout` s."""
    deadline = time.monotonic() + timeout
    checked = start_index
    while True:
        while checked < len(listener.notifications):
            if frame_contains_token(listener.notifications[checked], token):
                return True
            checked += 1
        if time.monotonic() >= deadline:
            return False
        await asyncio.sleep(0.1)


PERMISSION_HELP = bg.PERMISSION_HELP


async def run(args: argparse.Namespace) -> int:
    from bleak import BleakClient, BleakScanner

    target = args.address
    if target is None:
        device = await BleakScanner.find_device_by_filter(
            lambda d, ad: (d.name or "").upper().find(args.name.upper()) >= 0,
            timeout=args.scan_seconds,
        )
        if device is None:
            print(f"node {args.name!r} not found in {args.scan_seconds:.0f} s; "
                  f"the node holds one connection -- disconnect the phone first",
                  file=sys.stderr)
            return 2   # usage/setup failure, not "control never arrived"
        target = device.address
        print(f"found {args.name} at {target}", file=sys.stderr)

    listener = Listener()

    async with BleakClient(target) as client:
        await client.start_notify(bg.NUS_RX_CHAR, listener.on_notify)
        await asyncio.sleep(args.connect_wait)

        async def send(frame: bytes) -> None:
            await client.write_gatt_char(bg.NUS_TX_CHAR, frame, response=True)

        # Same handshake as ble_golden.py, so isPhoneReady gets set
        # (src/phone_commands.cpp:326) and the node treats us as a phone.
        await send(bg.build_hello(args.pin))
        await asyncio.sleep(1.0)
        await send(bg.build_timesync())

        # Settle before the first text command -- ble_golden.py measured the
        # node dropping an --info sent too soon after timesync.
        await asyncio.sleep(2.5)

        control_frame, control_token = build_control_probe(
            args.src_call, args.own_call)
        pong_frame, pong_token = build_pong_probe(
            args.src_call, args.own_call)

        print(f"control probe token: {control_token}", file=sys.stderr)
        print(f"pong probe token:    {pong_token}", file=sys.stderr)

        start = len(listener.notifications)
        await send(bg.build_text(f"--injectraw {control_frame.hex()}"))
        control_seen = await wait_for_token(listener, start, control_token, args.timeout)
        print(f"control DM: {'arrived' if control_seen else 'DID NOT ARRIVE'} on BLE",
              file=sys.stderr)

        start = len(listener.notifications)
        await send(bg.build_text(f"--injectraw {pong_frame.hex()}"))
        pong_seen = await wait_for_token(listener, start, pong_token, args.timeout)
        print(f"pong DM:    {'arrived' if pong_seen else 'DID NOT ARRIVE'} on BLE",
              file=sys.stderr)

        await client.stop_notify(bg.NUS_RX_CHAR)

    code = verdict(control_seen, pong_seen)
    print(VERDICT_MESSAGES[code], file=sys.stderr)
    return code


async def run_guarded(args: argparse.Namespace) -> int:
    try:
        return await run(args)
    except Exception as exc:                      # bleak's own class, imported late
        if type(exc).__name__ == "BleakBluetoothNotAvailableError":
            print(f"{exc}\n\n{PERMISSION_HELP}", file=sys.stderr)
            return 2
        raise


# --------------------------------------------------------------- self-test


def _self_test() -> int:
    failures = 0

    corpus_path = (Path(__file__).resolve().parents[2]
                   / "test" / "test_aprs_corpus" / "corpus.txt")
    f002 = None
    for line in corpus_path.read_text().splitlines():
        line = line.strip()
        if line.startswith("f002 "):
            f002 = bytes.fromhex(line.split()[1])
    assert f002 is not None
    rebuilt = build_frame(
        msg_id=0x6A8825A4, flags=0xB0,
        path=["OE1XAR-62", "DK4YU-77", "DL2JA-2", "DK5EN-91"], dest=b"*",
        payload=b"{CET}2026-08-21 10:57:28", hw=0x00, mod=0x88,
        trailer=bytes.fromhex("00ab237e"),
    )
    if rebuilt != f002:
        failures += 1
        print(f"FAIL: f002 reproduction: {rebuilt.hex()} != {f002.hex()}")

    control_frame, control_token = build_control_probe("DK5EN-97", "DK5EN-93")
    if control_frame[5] & FLAG_SERVER == 0:
        failures += 1
        print("FAIL: control probe does not have the server bit set")
    if control_frame[5] & 0x0F != 0:
        failures += 1
        print("FAIL: control probe has a non-zero hop nibble")
    if "{" in control_token:
        failures += 1
        print("FAIL: control token contains '{'")

    pong_frame, pong_token = build_pong_probe("DK5EN-97", "DK5EN-93")
    if pong_frame[5] & FLAG_SERVER == 0:
        failures += 1
        print("FAIL: pong probe does not have the server bit set")
    if pong_frame[5] & 0x0F != 0:
        failures += 1
        print("FAIL: pong probe has a non-zero hop nibble")
    if not pong_token.startswith("{pong}{") or not pong_token.endswith("}"):
        failures += 1
        print(f"FAIL: pong token shape: {pong_token!r}")

    if check_src_call("OE1XAR-62") is None:
        failures += 1
        print("FAIL: a non-DK5EN- src-call was not refused")
    if check_src_call("DK5EN-97") is not None:
        failures += 1
        print("FAIL: a DK5EN- src-call was refused")

    if (verdict(True, True), verdict(True, False), verdict(False, False),
            verdict(False, True)) != (0, 1, 3, 3):
        failures += 1
        print("FAIL: verdict() exit-code mapping")

    print("pong_ble_check.py self-test: " + ("ok" if failures == 0 else f"{failures} failure(s)"))
    return 1 if failures else 0


# --------------------------------------------------------------- main


def main(argv: Optional[List[str]] = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--name", default=None, help="node name to scan for, e.g. DK5EN-93")
    ap.add_argument("--address", default=None, help="BLE address, skips the scan")
    ap.add_argument("--pin", type=int, default=None,
                    help="six-digit app PIN; required when the node's bt_code > 0")
    ap.add_argument("--pin-file", type=Path, default=None,
                    help="read the PIN from a file instead of the command line")
    ap.add_argument("--own-call", default=None,
                    help="the node's own callsign; used as the destination of both "
                         "probe DMs (required unless --self-test)")
    ap.add_argument("--src-call", default="DK5EN-97",
                    help="frame source callsign for both probes; must start with "
                         "'DK5EN-' (default: DK5EN-97)")
    ap.add_argument("--timeout", type=float, default=8.0,
                    help="seconds to wait for each probe's notification (default: 8)")
    ap.add_argument("--connect-wait", type=float, default=2.0,
                    help="seconds after subscribe before the hello (default: 2)")
    ap.add_argument("--scan-seconds", type=float, default=10.0,
                    help="seconds to scan for --name (default: 10)")
    ap.add_argument("--self-test", action="store_true",
                    help="run the frame-builder self-test and exit, no BLE")
    add_guard_args(ap)
    args = ap.parse_args(argv)

    if args.self_test:
        return _self_test()

    # Operator rule 2026-09-26: no test on a node without a valid identity
    # (tools/bench/identity_guard.py); checked over the text console, before BLE.
    enforce(ap, args)

    err = check_src_call(args.src_call)
    if err:
        ap.error(err)   # exits 2

    if not args.own_call:
        ap.error("--own-call is required")

    if not (args.name or args.address):
        ap.error("pass --name or --address")

    if args.pin_file is not None:
        args.pin = int(args.pin_file.read_text().strip())

    return asyncio.run(run_guarded(args))


if __name__ == "__main__":
    raise SystemExit(main())
