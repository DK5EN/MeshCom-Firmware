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

Usage:
    python3 tools/webflash.py                          # dk5en-98.local, heltec V3 build
    python3 tools/webflash.py oe0xyz-1.local
    python3 tools/webflash.py --file path/to/firmware.bin --expect-hw HELTEC_V3
    python3 tools/webflash.py --force                  # skip hardware check
"""

import argparse
import hashlib
import re
import sys
import time
import urllib.error
import urllib.request
import uuid
from pathlib import Path

DEFAULT_HOST = "dk5en-98.local"
DEFAULT_ENV = "heltec_wifi_lora_32_V3"
SAFEBOOT_POLL_S = 120
REBOOT_POLL_S = 120


def http_get(url: str, timeout: float = 5.0) -> tuple[int, str]:
    with urllib.request.urlopen(url, timeout=timeout) as resp:
        return resp.status, resp.read().decode(errors="replace")


def node_info(host: str, timeout: float = 5.0) -> dict[str, str] | None:
    """Fetch the app's index page and extract firmware/hardware info."""
    try:
        status, body = http_get(f"http://{host}/", timeout=timeout)
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


def poll(check, deadline_s: int, interval: float = 3.0, label: str = ""):
    """Poll check() until it returns a truthy value or the deadline passes."""
    start = time.monotonic()
    while time.monotonic() - start < deadline_s:
        result = check()
        if result:
            return result
        print(f"  ... waiting for {label} ({int(time.monotonic() - start)}s)")
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


def main() -> int:
    parser = argparse.ArgumentParser(description="Flash a MeshCom node over WiFi (safeboot OTA)")
    parser.add_argument("host", nargs="?", default=DEFAULT_HOST, help=f"node hostname (default {DEFAULT_HOST})")
    parser.add_argument("--env", default=DEFAULT_ENV, help=f"PlatformIO env for the default bin path (default {DEFAULT_ENV})")
    parser.add_argument("--file", type=Path, default=None, help="firmware.bin path (default .pio/build/<env>/firmware.bin)")
    parser.add_argument("--expect-hw", default="HELTEC_V3", help="abort unless the node reports this hardware (default HELTEC_V3)")
    parser.add_argument("--force", action="store_true", help="skip the hardware check")
    args = parser.parse_args()

    fw = args.file or Path(".pio/build") / args.env / "firmware.bin"
    if not fw.is_file():
        print(f"error: firmware binary not found: {fw} (build it first: pio run -e {args.env})")
        return 1
    md5 = hashlib.md5(fw.read_bytes()).hexdigest()
    print(f"Firmware: {fw} ({fw.stat().st_size} bytes, md5 {md5})")

    print(f"Checking node http://{args.host}/ ...")
    info = node_info(args.host, timeout=10.0)
    if info is None:
        print("error: node not reachable")
        return 1
    print(f"  node up: Meshcom {info.get('version', '?')} build {info.get('build', '?')}, hardware {info.get('hardware', '?')}")
    if not args.force and info.get("hardware") != args.expect_hw:
        print(f"error: hardware mismatch (expected {args.expect_hw}) — use --force to flash anyway")
        return 1

    print("Triggering safeboot (callfunction/?otaupdate) ...")
    try:
        http_get(f"http://{args.host}/callfunction/?otaupdate", timeout=10.0)
    except (urllib.error.URLError, OSError):
        pass  # node reboots before answering; a dropped connection is the normal case

    def safeboot_up() -> bool:
        try:
            status, _ = http_get(f"http://{args.host}/update", timeout=5.0)
            return status == 200
        except (urllib.error.URLError, OSError):
            return False

    if not poll(safeboot_up, SAFEBOOT_POLL_S, label="safeboot web server"):
        print("error: safeboot web server did not come up — node should fall back to the app by itself")
        return 1
    print("  safeboot is up")

    print("Starting OTA session ...")
    status, body = http_get(f"http://{args.host}/ota/start?mode=fr&hash={md5}", timeout=10.0)
    if status != 200:
        print(f"error: /ota/start failed: HTTP {status} {body.strip()}")
        return 1

    print("Uploading firmware ...")
    status, body = upload_multipart(f"http://{args.host}/ota/upload", fw)
    if status != 200:
        print(f"error: upload failed: HTTP {status} {body.strip()}")
        return 1
    print(f"  upload accepted ({body.strip()}), node reboots into the app")

    new_info = poll(lambda: node_info(args.host), REBOOT_POLL_S, label="app reboot")
    if not new_info:
        print("warning: node did not come back within the timeout — check it manually")
        return 1
    print(f"Done: Meshcom {new_info.get('version', '?')} build {new_info.get('build', '?')}, hardware {new_info.get('hardware', '?')}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
