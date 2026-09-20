#!/usr/bin/env python3
# /// script
# requires-python = ">=3.11"
# dependencies = ["bleak>=0.22"]
# ///
"""Connect to a MeshCom node over BLE like the app does, hold, disconnect, repeat.

Each cycle: scan by advertised name (`MC-<id>-<call>`, see ble_golden.py for
why not by NUS UUID), connect, subscribe to the NUS notify characteristic,
send the app's hello frame (`04 10 20 30`, or the hashed form with --pin),
hold for --hold seconds, disconnect, pause --gap seconds. Prints one line per
event with a wall-clock stamp so the serial/net-console logs can be aligned.

Usage:
  uv run --with bleak tools/bench/ble_cycle.py --name DK5EN-1 [--cycles 3] [--hold 30] [--gap 10] [--pin 123456]
  uv run --with bleak tools/bench/ble_cycle.py --name DK5EN-1 --cycles 1 --send "--mesh on" --send "--mesh off" --hold 120

With --send, each command is written after the hello as the app's 0xA0 text
frame ([len][0xA0][utf-8], len = payload+2, phone_commands.cpp case 0xA0)
and the connection is held --hold seconds after each one before the next.

Exit code: 0 every cycle connected and disconnected cleanly, 1 otherwise.
"""
import argparse
import asyncio
import datetime as dt
import hashlib
import sys

NUS_TX_CHAR = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
NUS_RX_CHAR = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"


def stamp() -> str:
    return dt.datetime.now().isoformat(timespec="milliseconds")


def say(text: str) -> None:
    print(f"{stamp()}  {text}", flush=True)


def text_frame(text: str) -> bytes:
    payload = text.encode()
    return bytes([len(payload) + 2, 0xA0]) + payload


def hello(pin: int | None) -> bytes:
    if pin is None:
        return bytes([0x04, 0x10, 0x20, 0x30])
    return bytes([0x23, 0x10, 0x20, 0x30]) + hashlib.sha256(("%06u" % pin).encode()).digest()


async def run(a: argparse.Namespace) -> int:
    from bleak import BleakClient, BleakScanner

    failures = 0
    for i in range(1, a.cycles + 1):
        say(f"cycle {i}/{a.cycles} scanning for {a.address or a.name}")
        if a.address:
            # macOS caches advertised names (a renamed node keeps showing its
            # old callsign for hours), so the CoreBluetooth UUID from
            # `ble_golden.py --scan` is the only reliable handle there.
            dev = await BleakScanner.find_device_by_address(a.address, timeout=a.scan_seconds)
        else:
            dev = await BleakScanner.find_device_by_filter(
                # exact callsign match: "DK5EN-1" must not pick MC-xxxx-DK5EN-14
                lambda d, ad: (d.name or ad.local_name or "").upper().endswith("-" + a.name.upper()),
                timeout=a.scan_seconds,
            )
        if dev is None:
            say(f"cycle {i} FAIL not found in {a.scan_seconds:.0f} s")
            failures += 1
            await asyncio.sleep(a.gap)
            continue
        notified = 0

        def on_notify(_h, data: bytearray) -> None:
            nonlocal notified
            notified += 1

        try:
            async with BleakClient(dev, timeout=20) as c:
                say(f"cycle {i} connected {dev.address}")
                await c.start_notify(NUS_RX_CHAR, on_notify)
                await asyncio.sleep(1.5)
                await c.write_gatt_char(NUS_TX_CHAR, hello(a.pin), response=True)
                if a.send:
                    say(f"cycle {i} hello sent, {len(a.send)} commands, hold {a.hold:.0f} s each")
                    await asyncio.sleep(2.0)
                    for cmd in a.send:
                        await c.write_gatt_char(NUS_TX_CHAR, text_frame(cmd), response=True)
                        say(f"cycle {i} sent {cmd!r}")
                        await asyncio.sleep(a.hold)
                        say(f"cycle {i} after {cmd!r}: notifications={notified} still_connected={c.is_connected}")
                        if not c.is_connected:
                            failures += 1
                            break
                else:
                    say(f"cycle {i} hello sent, holding {a.hold:.0f} s")
                    await asyncio.sleep(a.hold)
                say(f"cycle {i} notifications={notified} still_connected={c.is_connected}")
                if not c.is_connected:
                    failures += 1
            say(f"cycle {i} disconnected")
        except Exception as e:  # bleak raises many concrete types; log and count
            say(f"cycle {i} FAIL {type(e).__name__}: {e}")
            failures += 1
        await asyncio.sleep(a.gap)
    say(f"=== ble cycles done failures={failures}")
    return 1 if failures else 0


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--name", default="", help="callsign, exact suffix match on MC-<id>-<call>")
    ap.add_argument("--address", default="", help="peripheral address/UUID from ble_golden.py --scan (preferred on macOS)")
    ap.add_argument("--cycles", type=int, default=3)
    ap.add_argument("--hold", type=float, default=30)
    ap.add_argument("--gap", type=float, default=10)
    ap.add_argument("--scan-seconds", type=float, default=15)
    ap.add_argument("--pin", type=int, default=None)
    ap.add_argument("--send", action="append", default=[], help="text command to send as a 0xA0 frame (repeatable)")
    a = ap.parse_args()
    if not (a.name or a.address):
        ap.error("--name or --address")
    return asyncio.run(run(a))


if __name__ == "__main__":
    sys.exit(main())
