#!/usr/bin/env python3
# /// script
# requires-python = ">=3.11"
# dependencies = ["bleak>=0.22"]
# ///
"""Connect to a MeshCom node over BLE like the app does, hold, disconnect, repeat.

Each cycle: scan by advertised name (`MC-<id>-<call>`, see ble_golden.py for
why not by NUS UUID), connect, subscribe to the NUS notify characteristic,
send the app's hello frame (`04 10 20 30`, or the hashed form with --pin),
hold for --hold seconds, disconnect, then (unless --no-advert-wait) scan until
the node advertises again and record the delay, pause --gap seconds. Prints one
line per event with a wall-clock stamp so the serial/net-console logs can be
aligned.

Usage:
  uv run --with bleak tools/bench/ble_cycle.py --name DK5EN-1 [--cycles 3] [--hold 30] [--gap 10] [--pin 123456]
  uv run --with bleak tools/bench/ble_cycle.py --name DK5EN-1 --cycles 1 --send "--mesh on" --send "--mesh off" --hold 120
  uv run --with bleak tools/bench/ble_cycle.py --name DK5EN-1 --cycles 50 --hold 2 --gap 0
  uv run --with bleak tools/bench/ble_cycle.py --name DK5EN-1 --cycles 20 --hold 2 --gap 0 --drop unclean

With --send, each command is written after the hello as the app's 0xA0 text
frame ([len][0xA0][utf-8], len = payload+2, phone_commands.cpp case 0xA0)
and the connection is held --hold seconds after each one before the next.

--gap 0: the next connect starts right after the previous disconnect has been
handled. With the advert wait on (default) that means right after the node's
first advert was seen again, i.e. the earliest moment it is demonstrably
connectable. Add --no-advert-wait for a blind reconnect straight after the
disconnect (the first attempt may then legitimately fail, see below).

--drop unclean: abrupt teardown. After the connect completes (--abort-ms later,
default 0) the session task is cancelled in the middle of the handshake: no
settle time, no hold, the hello is written without response and never
acknowledged, no explicit disconnect() call. Limits on macOS: bleak/CoreBluetooth
offer no way to leave a link without cancelPeripheralConnection, so the library
still terminates the link itself during cleanup (the node sees a normal local
terminate, reason 0x13/0x16-class, not a supervision timeout 0x08). A real
silent drop needs the radio to go quiet (switch Bluetooth off or move out of
range). What this mode does exercise is the node's session reset when the
central vanishes before the hello was processed, with an immediate reconnect.

Metric disconnect-to-first-advert: timer starts when disconnect() returns (a
fresh scan is started then, so scanner start-up latency, tens of ms on macOS, is
inside the figure; treat it as an upper bound). The per-run summary prints
min/median/max, adverts never seen, connects that failed on every attempt, and
connects that failed only on the first attempt (retried up to --attempts). Only a link that
never came up is retried; an error after the link was up (node dropped it during the hold or
--send) is a session failure: not retried, counted as other_failures, fails the run.

Exit code: 0 every cycle connected on some attempt, disconnected, and the node
advertised again; 1 otherwise. First-attempt-only failures do not fail the run.
"""
import argparse
import asyncio
import contextlib
import datetime as dt
import hashlib
import statistics
import sys
import time
from collections.abc import Sequence

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


# ---------------------------------------------------------------------------
# Pure parts (no BLE, no I/O): unit-tested in test_ble_cycle.py.
# ---------------------------------------------------------------------------


def validate_args(a: argparse.Namespace) -> list[str]:
    """Return a list of human-readable problems; empty means the args are usable."""
    errs: list[str] = []
    if not (a.name or a.address):
        errs.append("--name or --address")
    if a.cycles < 1:
        errs.append("--cycles must be >= 1")
    if a.hold < 0:
        errs.append("--hold must be >= 0")
    if a.gap < 0:
        errs.append("--gap must be >= 0 (0 = reconnect immediately)")
    if a.scan_seconds <= 0:
        errs.append("--scan-seconds must be > 0")
    if a.attempts < 1:
        errs.append("--attempts must be >= 1")
    if a.retry_delay < 0:
        errs.append("--retry-delay must be >= 0")
    if a.abort_ms < 0:
        errs.append("--abort-ms must be >= 0")
    if a.pin is not None and not 0 <= a.pin <= 999999:
        errs.append("--pin must be 0..999999")
    if a.drop not in ("clean", "unclean"):
        errs.append("--drop must be clean or unclean")
    if a.drop == "unclean" and a.send:
        errs.append("--drop unclean cannot be combined with --send")
    return errs


def advert_matches(name: str | None, local_name: str | None, address: str | None,
                   want_name: str, want_address: str) -> bool:
    """Does this advert belong to the node we cycle against?

    --address wins and is compared case-insensitively (macOS UUIDs, Linux MACs).
    Otherwise exact callsign match on the MC-<id>-<call> suffix: "DK5EN-1" must
    not pick MC-xxxx-DK5EN-14.
    """
    if want_address:
        return (address or "").upper() == want_address.upper()
    if not want_name:
        return False
    return (name or local_name or "").upper().endswith("-" + want_name.upper())


def summarize_delays(delays_ms: Sequence[float | None]) -> dict:
    """min/median/max over the seen adverts; None entries are adverts never seen."""
    seen = [d for d in delays_ms if d is not None]
    out: dict = {"n": len(delays_ms), "seen": len(seen), "missed": len(delays_ms) - len(seen),
                 "min": None, "median": None, "max": None}
    if seen:
        out["min"] = min(seen)
        out["median"] = statistics.median(seen)
        out["max"] = max(seen)
    return out


def summarize_connects(attempts: Sequence[Sequence[bool]]) -> dict:
    """attempts[i] is the ordered list of connect-attempt outcomes of cycle i.

    failed: cycles where no attempt succeeded (empty list = never found, counts too).
    first_only: cycles whose first attempt failed but a later one succeeded.
    """
    failed = sum(1 for c in attempts if not any(c))
    first_only = sum(1 for c in attempts if c and not c[0] and any(c))
    return {"cycles": len(attempts), "failed": failed, "first_only": first_only}


def format_summary(delays: dict, connects: dict, other_failures: int) -> list[str]:
    def fmt(v: float | None) -> str:
        return "n/a" if v is None else f"{v:.0f}"

    return [
        f"=== advert delay ms: min={fmt(delays['min'])} median={fmt(delays['median'])} "
        f"max={fmt(delays['max'])} seen={delays['seen']}/{delays['n']} missed={delays['missed']}",
        f"=== connects: cycles={connects['cycles']} failed={connects['failed']} "
        f"first_attempt_only_failed={connects['first_only']} other_failures={other_failures}",
    ]


def exit_code(delays: dict, connects: dict, other_failures: int) -> int:
    """First-attempt-only failures are tolerated (stack may still be busy for a few ms)."""
    return 1 if (connects["failed"] or delays["missed"] or other_failures) else 0


# ---------------------------------------------------------------------------
# BLE parts (bleak imported lazily so the pure parts import without it).
# ---------------------------------------------------------------------------


async def find_node(a: argparse.Namespace):
    from bleak import BleakScanner

    if a.address:
        # macOS caches advertised names (a renamed node keeps showing its
        # old callsign for hours), so the CoreBluetooth UUID from
        # `ble_golden.py --scan` is the only reliable handle there.
        return await BleakScanner.find_device_by_address(a.address, timeout=a.scan_seconds)
    return await BleakScanner.find_device_by_filter(
        lambda d, ad: advert_matches(d.name, ad.local_name, d.address, a.name, ""),
        timeout=a.scan_seconds,
    )


async def wait_for_advert(a: argparse.Namespace, t0: float):
    """Scan until the node advertises; return (delay_ms | None, device | None). t0 is time.monotonic()."""
    from bleak import BleakScanner

    seen = asyncio.Event()
    hit: list = []

    def cb(d, ad) -> None:
        if not hit and advert_matches(d.name, ad.local_name, d.address, a.name, a.address):
            hit.append((time.monotonic(), d))
            seen.set()

    async with BleakScanner(detection_callback=cb):
        with contextlib.suppress(asyncio.TimeoutError):
            await asyncio.wait_for(seen.wait(), timeout=a.scan_seconds)
    if not hit:
        return None, None
    return (hit[0][0] - t0) * 1000.0, hit[0][1]


class ConnectFailed(Exception):
    """The link could not be established. Only this is retried and counted as a connect failure."""


async def open_client(dev):
    """Connect a BleakClient; any error here is a ConnectFailed (keeps the cause)."""
    from bleak import BleakClient

    c = BleakClient(dev, timeout=20)
    try:
        await c.connect()
    except Exception as e:
        raise ConnectFailed(f"{type(e).__name__}: {e}") from e
    return c


async def close_client(c) -> None:
    try:
        await c.disconnect()
    except Exception as e:  # teardown trouble is not a session verdict; the caller checks is_connected
        say(f"disconnect raised {type(e).__name__}: {e}")


async def connect_session(a: argparse.Namespace, dev, i: int) -> bool:
    """One connect attempt with a full clean session.

    Returns True when the link stayed up for the whole session. Raises ConnectFailed when the
    link never came up; any other exception means the link broke after it was up (session failure).
    """
    notified = 0

    def on_notify(_h, data: bytearray) -> None:
        nonlocal notified
        notified += 1

    ok = True
    c = await open_client(dev)
    try:
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
                    ok = False
                    break
        else:
            say(f"cycle {i} hello sent, holding {a.hold:.0f} s")
            await asyncio.sleep(a.hold)
        say(f"cycle {i} notifications={notified} still_connected={c.is_connected}")
        if not c.is_connected:
            ok = False
    finally:
        await close_client(c)
    say(f"cycle {i} disconnected")
    return ok


async def connect_unclean(a: argparse.Namespace, dev, i: int) -> bool:
    """Connect, write the hello without response, then cancel the session task mid-handshake.

    Same contract as connect_session: ConnectFailed (cause kept) for a link that never came up,
    any other exception for a failure after it was up.
    """
    connected = asyncio.Event()

    async def session() -> None:
        c = await open_client(dev)
        try:
            say(f"cycle {i} connected {dev.address}")
            connected.set()
            await c.write_gatt_char(NUS_TX_CHAR, hello(a.pin), response=False)
            await asyncio.sleep(3600)  # never reached: cancelled below
        finally:
            await close_client(c)

    task = asyncio.ensure_future(session())
    waiter = asyncio.ensure_future(connected.wait())
    await asyncio.wait({task, waiter}, return_when=asyncio.FIRST_COMPLETED, timeout=25)
    waiter.cancel()
    if not connected.is_set():
        if task.done() and not task.cancelled() and task.exception() is not None:
            exc = task.exception()
            raise exc if isinstance(exc, ConnectFailed) else ConnectFailed(f"{type(exc).__name__}: {exc}") from exc
        task.cancel()
        with contextlib.suppress(asyncio.CancelledError, Exception):
            await task
        raise ConnectFailed("connect did not complete in 25 s")
    await asyncio.sleep(a.abort_ms / 1000.0)
    if task.done() and not task.cancelled() and task.exception() is not None:
        raise task.exception()  # the link broke on its own before we aborted: session failure
    say(f"cycle {i} abort (cancel mid-handshake, no disconnect() call)")
    task.cancel()
    with contextlib.suppress(asyncio.CancelledError, Exception):
        await task
    say(f"cycle {i} dropped")
    return True


async def attempt_cycle(a: argparse.Namespace, dev, i: int, fn=None) -> tuple[list[bool], bool]:
    """Run the connect attempts of one cycle. Returns (attempt outcomes, session_ok).

    Only ConnectFailed is retried (up to --attempts). Any other exception means the link was up and
    then broke: no retry, the cycle counts as connected with a session failure (session_ok False).
    """
    if fn is None:
        fn = connect_unclean if a.drop == "unclean" else connect_session
    outcomes: list[bool] = []
    for n in range(1, a.attempts + 1):
        try:
            session_ok = await fn(a, dev, i)
        except ConnectFailed as e:
            say(f"cycle {i} attempt {n}/{a.attempts} connect FAIL {e}")
            outcomes.append(False)
            if n < a.attempts:
                await asyncio.sleep(a.retry_delay)
            continue
        except Exception as e:  # bleak raises many concrete types; the link was up, so no retry
            say(f"cycle {i} attempt {n}/{a.attempts} SESSION FAIL {type(e).__name__}: {e}")
            outcomes.append(True)
            return outcomes, False
        outcomes.append(True)
        return outcomes, session_ok
    return outcomes, True


async def run(a: argparse.Namespace) -> int:
    attempts_log: list[list[bool]] = []
    delays: list[float | None] = []
    other_failures = 0
    dev = None
    for i in range(1, a.cycles + 1):
        if dev is None:
            say(f"cycle {i}/{a.cycles} scanning for {a.address or a.name}")
            dev = await find_node(a)
        else:
            say(f"cycle {i}/{a.cycles} reusing handle {dev.address}")
        if dev is None:
            say(f"cycle {i} FAIL not found in {a.scan_seconds:.0f} s")
            attempts_log.append([])
            await asyncio.sleep(a.gap)
            continue
        outcomes, session_ok = await attempt_cycle(a, dev, i)
        attempts_log.append(outcomes)
        if outcomes[-1] and not session_ok:
            other_failures += 1
        t0 = time.monotonic()
        if outcomes[-1] and not a.no_advert_wait:
            delay, seen_dev = await wait_for_advert(a, t0)
            delays.append(delay)
            if delay is None:
                say(f"cycle {i} FAIL no advert within {a.scan_seconds:.0f} s after disconnect")
            else:
                say(f"cycle {i} advert after {delay:.0f} ms")
                dev = seen_dev
        await asyncio.sleep(a.gap)
    d = summarize_delays(delays)
    c = summarize_connects(attempts_log)
    for line in format_summary(d, c, other_failures):
        say(line)
    rc = exit_code(d, c, other_failures)
    say(f"=== ble cycles done failures={c['failed'] + d['missed'] + other_failures}")
    return rc


def build_parser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--name", default="", help="callsign, exact suffix match on MC-<id>-<call>")
    ap.add_argument("--address", default="", help="peripheral address/UUID from ble_golden.py --scan (preferred on macOS)")
    ap.add_argument("--cycles", type=int, default=3)
    ap.add_argument("--hold", type=float, default=30)
    ap.add_argument("--gap", type=float, default=10, help="seconds between cycles; 0 = reconnect immediately")
    ap.add_argument("--scan-seconds", type=float, default=15)
    ap.add_argument("--pin", type=int, default=None)
    ap.add_argument("--send", action="append", default=[], help="text command to send as a 0xA0 frame (repeatable)")
    ap.add_argument("--drop", choices=("clean", "unclean"), default="clean",
                    help="clean: normal disconnect after the hold. unclean: cancel the session mid-handshake "
                         "without calling disconnect(); on macOS bleak still terminates the link itself, so the "
                         "node sees a local terminate, never a supervision timeout (see module doc)")
    ap.add_argument("--abort-ms", type=float, default=0, help="--drop unclean: delay after connect before the abort")
    ap.add_argument("--attempts", type=int, default=3, help="connect attempts per cycle (a failed first attempt is counted separately)")
    ap.add_argument("--retry-delay", type=float, default=0.25, help="seconds between connect attempts")
    ap.add_argument("--no-advert-wait", action="store_true",
                    help="skip the disconnect-to-first-advert measurement and reconnect blind after --gap")
    return ap


def main() -> int:
    ap = build_parser()
    a = ap.parse_args()
    errs = validate_args(a)
    if errs:
        ap.error("; ".join(errs))
    return asyncio.run(run(a))


if __name__ == "__main__":
    sys.exit(main())
