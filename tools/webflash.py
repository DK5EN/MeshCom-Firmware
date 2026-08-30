#!/usr/bin/env python3
"""MeshCom OTA web flasher — flash a node over WiFi via its safeboot partition.

Drives the firmware's built-in OTA flow (src/safeboot/, ElegantOTA):
  1. GET  http://<host>/callfunction/?otaupdate   -> reboot into safeboot
  2. poll http://<host>/update                    -> safeboot web server up
  3. GET  /ota/start?mode=fr&hash=<md5>           -> open update session
  4. POST /ota/upload (multipart firmware.bin)    -> write + verify + reboot
  5. poll http://<host>/                          -> back in app, report build

The node must already run a MeshCom firmware with the safeboot partition
installed (flashed once over USB with the full layout). Safeboot reboots
back to the app if the OTA is not started within 180 s, so steps 2-4 run
without user interaction.

CLI usage:
    python3 tools/webflash.py                          # dk5en-98.local, heltec V3 build
    python3 tools/webflash.py oe0xyz-1.local
    python3 tools/webflash.py --env ttgo_tbeam 192.168.1.90
    python3 tools/webflash.py --bin path/to/firmware.bin --expect-hw HELTEC_V3
    python3 tools/webflash.py --force                  # skip hardware check

Library usage (TM-40, tools/bench/ota_regression.py): import `flash()` and
call it directly -- it returns a structured `OtaResult` and never calls
sys.exit(), so a caller can run it against a live node under its own timeout
and retry policy and fold the per-phase timings into a bench report. `get`
and `poster` are injectable (default `http_get` / `upload_multipart`) so
tests can drive the whole state machine without a network or a node.
"""

from __future__ import annotations

import argparse
import hashlib
import re
import sys
import time
import urllib.error
import urllib.request
import uuid
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable, Optional

DEFAULT_HOST = "dk5en-98.local"
DEFAULT_ENV = "heltec_wifi_lora_32_V3"
SAFEBOOT_POLL_S = 120
REBOOT_POLL_S = 120

# TM-40: PlatformIO env -> hardware string the firmware reports, both on the
# web info page (`Hardware</td><td>%s`, src/web_functions/web_functions.cpp)
# and over serial (`getHardwareLong(BOARD_HARDWARE)` in
# src/mheard_functions.cpp, the HardWare[] table). BOARD_HARDWARE values come
# from `#define MODUL_HARDWARE ...` in each variant's configuration.h and the
# numeric ids in src/configuration_global.h (TBEAM=4, HELTEC_V3=43,
# T_DECK_PLUS=46 -> HardWare[] index 17 "TDECK_PLUS").
ENV_HARDWARE: dict[str, str] = {
    "t_deck_plus": "TDECK_PLUS",
    "heltec_wifi_lora_32_V3": "HELTEC_V3",
    "ttgo_tbeam": "TBEAM",
}

def hardware_matches(reported: Optional[str], expected: str) -> bool:
    """`TBEAM` matches `TBEAM_AXP2101` (v1.2 PMU variant) but not `TBEAM_SUPREME`-style
    names is not needed: the variants of one env share the prefix, other envs differ before it."""
    if not reported:
        return False
    return reported == expected or reported.startswith(expected + "_")


GetFn = Callable[[str, float], "tuple[int, str]"]
PostFn = Callable[[str, Path], "tuple[int, str]"]


def http_get(url: str, timeout: float = 5.0) -> tuple[int, str]:
    with urllib.request.urlopen(url, timeout=timeout) as resp:
        return resp.status, resp.read().decode(errors="replace")


def node_info(host: str, timeout: float = 5.0, get: GetFn = http_get) -> Optional[dict[str, str]]:
    """Fetch the app's index page and extract firmware/hardware info."""
    try:
        status, body = get(f"http://{host}/", timeout)
    except (urllib.error.URLError, OSError):
        return None
    if status != 200:
        return None
    info: dict[str, str] = {}
    if m := re.search(r"Hardware</td><td>([A-Z0-9_-]+)", body):
        info["hardware"] = m.group(1)
    if m := re.search(r"build: ([^<)]+)", body):
        info["build"] = m.group(1).strip()
    if m := re.search(r"Meshcom ([0-9]+\.[0-9]+[a-z]*)<br>\(build", body):
        info["version"] = m.group(1)
    return info


def poll(check: Callable[[], Any], deadline_s: float, interval: float = 3.0,
         label: str = "", on_wait: Optional[Callable[[str], None]] = None) -> Any:
    """Poll check() until it returns a truthy value or the deadline passes."""
    start = time.monotonic()
    while time.monotonic() - start < deadline_s:
        result = check()
        if result:
            return result
        if on_wait:
            on_wait(f"waiting for {label} ({int(time.monotonic() - start)}s)")
        time.sleep(interval)
    return None


def upload_multipart(url: str, path: Path, timeout: float = 180.0) -> tuple[int, str]:
    boundary = uuid.uuid4().hex
    head = (
        f"--{boundary}\r\n"
        f'Content-Disposition: form-data; name="file"; filename="{path.name}"\r\n'
        f"Content-Type: application/octet-stream\r\n\r\n"
    ).encode()
    tail = f"\r\n--{boundary}--\r\n".encode()
    body = head + path.read_bytes() + tail
    req = urllib.request.Request(
        url,
        data=body,
        headers={"Content-Type": f"multipart/form-data; boundary={boundary}"},
        method="POST",
    )
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return resp.status, resp.read().decode(errors="replace")


def resolve_firmware(env: str, file: Optional[Path]) -> Path:
    return file if file is not None else Path(".pio/build") / env / "firmware.bin"


@dataclass
class OtaResult:
    """Structured outcome of one `flash()` run -- no sys.exit anywhere above."""

    ok: bool
    stage: str  # "done" on success; otherwise where it stopped, see `flash()`
    fw_path: str
    fw_size: int
    md5: str
    before: Optional[dict[str, str]] = None
    after: Optional[dict[str, str]] = None
    error: Optional[str] = None
    timings: dict[str, float] = field(default_factory=dict)

    def line(self) -> str:
        if self.ok:
            a = self.after or {}
            return (f"OTA ok: Meshcom {a.get('version', '?')} build {a.get('build', '?')}, "
                    f"hardware {a.get('hardware', '?')} (total {self.timings.get('total_s', 0):.0f}s)")
        return f"OTA FAILED at {self.stage}: {self.error}"


def flash(host: str, fw: Path, *, expect_hw: Optional[str] = None, force: bool = False,
          safeboot_poll_s: float = SAFEBOOT_POLL_S, reboot_poll_s: float = REBOOT_POLL_S,
          precheck_timeout: float = 10.0,
          get: GetFn = http_get, poster: PostFn = upload_multipart,
          on_phase: Optional[Callable[[str, str], None]] = None,
          on_wait: Optional[Callable[[str], None]] = None) -> OtaResult:
    """Run the OTA flow against an already-reachable node.

    Pure library function: every failure path returns an `OtaResult(ok=False,
    ...)` rather than raising or exiting, so a caller (CLI or a bench script)
    decides what to do next. `get`/`poster` are injectable for tests.
    """
    def phase(name: str, detail: str = "") -> None:
        if on_phase:
            on_phase(name, detail)

    t_all = time.monotonic()
    timings: dict[str, float] = {}

    if not fw.is_file():
        return OtaResult(ok=False, stage="firmware_missing", fw_path=str(fw), fw_size=0,
                          md5="", error=f"firmware binary not found: {fw}")

    data = fw.read_bytes()
    md5 = hashlib.md5(data).hexdigest()
    fw_size = len(data)
    phase("firmware", f"{fw} ({fw_size} bytes, md5 {md5})")

    phase("precheck", f"http://{host}/")
    t = time.monotonic()
    info = node_info(host, timeout=precheck_timeout, get=get)
    timings["precheck_s"] = time.monotonic() - t
    if info is None:
        timings["total_s"] = time.monotonic() - t_all
        return OtaResult(ok=False, stage="not_reachable", fw_path=str(fw), fw_size=fw_size,
                          md5=md5, timings=timings, error=f"node not reachable at http://{host}/")

    if not force and expect_hw and not hardware_matches(info.get("hardware"), expect_hw):
        timings["total_s"] = time.monotonic() - t_all
        return OtaResult(
            ok=False, stage="hardware_mismatch", fw_path=str(fw), fw_size=fw_size, md5=md5,
            before=info, timings=timings,
            error=f"hardware mismatch: node reports {info.get('hardware')!r}, "
                  f"expected {expect_hw!r} (--force to flash anyway)")

    phase("trigger_safeboot", "callfunction/?otaupdate")
    t = time.monotonic()
    try:
        get(f"http://{host}/callfunction/?otaupdate", 10.0)
    except (urllib.error.URLError, OSError):
        pass  # node reboots before answering; a dropped connection is the normal case
    timings["trigger_safeboot_s"] = time.monotonic() - t

    phase("safeboot_wait", "")
    t = time.monotonic()

    def safeboot_up() -> bool:
        try:
            status, _ = get(f"http://{host}/update", 5.0)
            return status == 200
        except (urllib.error.URLError, OSError):
            return False

    if not poll(safeboot_up, safeboot_poll_s, label="safeboot web server", on_wait=on_wait):
        timings["safeboot_wait_s"] = time.monotonic() - t
        timings["total_s"] = time.monotonic() - t_all
        return OtaResult(
            ok=False, stage="safeboot_timeout", fw_path=str(fw), fw_size=fw_size, md5=md5,
            before=info, timings=timings,
            error=f"safeboot web server did not come up within {safeboot_poll_s:.0f}s "
                  "-- node should fall back to the app by itself")
    timings["safeboot_wait_s"] = time.monotonic() - t

    phase("ota_start", "")
    t = time.monotonic()
    try:
        status, body = get(f"http://{host}/ota/start?mode=fr&hash={md5}", 10.0)
    except (urllib.error.URLError, OSError) as e:
        timings["ota_start_s"] = time.monotonic() - t
        timings["total_s"] = time.monotonic() - t_all
        return OtaResult(ok=False, stage="ota_start_failed", fw_path=str(fw), fw_size=fw_size,
                          md5=md5, before=info, timings=timings, error=f"/ota/start failed: {e}")
    timings["ota_start_s"] = time.monotonic() - t
    if status != 200:
        timings["total_s"] = time.monotonic() - t_all
        return OtaResult(ok=False, stage="ota_start_failed", fw_path=str(fw), fw_size=fw_size,
                          md5=md5, before=info, timings=timings,
                          error=f"/ota/start failed: HTTP {status} {body.strip()}")

    phase("upload", f"{fw_size} bytes")
    t = time.monotonic()
    try:
        status, body = poster(f"http://{host}/ota/upload", fw)
    except (urllib.error.URLError, OSError) as e:
        timings["upload_s"] = time.monotonic() - t
        timings["total_s"] = time.monotonic() - t_all
        return OtaResult(ok=False, stage="upload_failed", fw_path=str(fw), fw_size=fw_size,
                          md5=md5, before=info, timings=timings, error=f"upload failed: {e}")
    timings["upload_s"] = time.monotonic() - t
    if status != 200:
        timings["total_s"] = time.monotonic() - t_all
        return OtaResult(ok=False, stage="upload_failed", fw_path=str(fw), fw_size=fw_size,
                          md5=md5, before=info, timings=timings,
                          error=f"upload failed: HTTP {status} {body.strip()}")

    phase("reboot_wait", "")
    t = time.monotonic()
    new_info = poll(lambda: node_info(host, get=get), reboot_poll_s, label="app reboot",
                     on_wait=on_wait)
    timings["reboot_wait_s"] = time.monotonic() - t
    timings["total_s"] = time.monotonic() - t_all
    if not new_info:
        return OtaResult(ok=False, stage="reboot_timeout", fw_path=str(fw), fw_size=fw_size,
                          md5=md5, before=info, timings=timings,
                          error=f"node did not come back within {reboot_poll_s:.0f}s "
                                "-- check it manually")

    return OtaResult(ok=True, stage="done", fw_path=str(fw), fw_size=fw_size, md5=md5,
                      before=info, after=new_info, timings=timings)


# --- CLI ------------------------------------------------------------------


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Flash a MeshCom node over WiFi (safeboot OTA)")
    parser.add_argument("host", nargs="?", default=DEFAULT_HOST,
                         help=f"node hostname or IP (default {DEFAULT_HOST})")
    parser.add_argument("--env", default=DEFAULT_ENV,
                         help=f"PlatformIO env for the default bin path and the expected "
                              f"hardware, see ENV_HARDWARE (default {DEFAULT_ENV})")
    parser.add_argument("--bin", "--file", dest="bin", type=Path, default=None,
                         help="firmware.bin path (default .pio/build/<env>/firmware.bin)")
    parser.add_argument("--expect-hw", default=None,
                         help="abort unless the node reports this hardware "
                              "(default: looked up from --env via ENV_HARDWARE, "
                              "falling back to HELTEC_V3)")
    parser.add_argument("--force", action="store_true", help="skip the hardware check")
    return parser


def main(argv: Optional[list[str]] = None) -> int:
    args = build_parser().parse_args(argv)
    fw = resolve_firmware(args.env, args.bin)
    expect_hw = args.expect_hw or ENV_HARDWARE.get(args.env, "HELTEC_V3")

    def on_phase(name: str, detail: str) -> None:
        msg = {
            "firmware": f"Firmware: {detail}",
            "precheck": f"Checking node {detail} ...",
            "trigger_safeboot": "Triggering safeboot (callfunction/?otaupdate) ...",
            "safeboot_wait": "Waiting for the safeboot web server ...",
            "ota_start": "Starting OTA session ...",
            "upload": f"Uploading firmware ({detail}) ...",
            "reboot_wait": "Waiting for the node to reboot back into the app ...",
        }.get(name)
        if msg:
            print(msg)

    def on_wait(msg: str) -> None:
        print(f"  ... {msg}")

    result = flash(args.host, fw, expect_hw=expect_hw, force=args.force,
                    on_phase=on_phase, on_wait=on_wait)

    if result.stage == "firmware_missing":
        print(f"error: {result.error} (build it first: pio run -e {args.env})")
        return 1
    if result.before:
        b = result.before
        print(f"  node up: Meshcom {b.get('version', '?')} build {b.get('build', '?')}, "
              f"hardware {b.get('hardware', '?')}")
    if not result.ok:
        print(f"error: {result.error}")
        return 1
    a = result.after or {}
    print(f"Done: Meshcom {a.get('version', '?')} build {a.get('build', '?')}, "
          f"hardware {a.get('hardware', '?')} (total {result.timings.get('total_s', 0):.0f}s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
