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
    python3 tools/webflash.py --self-test               # offline parser checks, no network

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
import http.client
import json
import re
import socket
import sys
import time
import urllib.error
import urllib.request
import uuid
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable, Optional
from urllib.parse import urlsplit

APP_INVALID_NOTE = ("node reports: firmware image incomplete -- the node stays in the bootloader, "
                     "upload a complete firmware to recover")

DEFAULT_HOST = "dk5en-98.local"
DEFAULT_ENV = "heltec_wifi_lora_32_V3"
SAFEBOOT_POLL_S = 120
REBOOT_POLL_S = 120

# TM-40/TM-47: PlatformIO env -> hardware string the firmware reports, both on
# the web info page (`Hardware</td><td>%s`, src/web_functions/web_functions.cpp)
# and over serial (`getHardwareLong(BOARD_HARDWARE)` in
# src/mheard_functions.cpp, the HardWare[] table). BOARD_HARDWARE values come
# from `#define MODUL_HARDWARE ...` in each variant's configuration.h and the
# numeric ids in src/configuration_global.h (TBEAM=4, HELTEC_V3=43,
# T_DECK_PLUS=46 -> HardWare[] index 17). mheard_functions.cpp actually keeps
# TWO HardWare[] tables and picks one at compile time on
# `#if defined(BOARD_T_DECK) || defined(BOARD_T_DECK_PLUS)`; index 17 is
# "TDECK+" in that conditional table and "TDECK_PLUS" in the default one used
# by every other env. variants/t_deck_plus/platformio.ini defines
# BOARD_T_DECK_PLUS, so its build takes the conditional table and reports
# "TDECK+" -- match that here, not the default table's spelling.
ENV_HARDWARE: dict[str, str] = {
    "t_deck_plus": "TDECK+",
    "heltec_wifi_lora_32_V3": "HELTEC_V3",
    "ttgo_tbeam": "TBEAM",
}

def hardware_matches(reported: Optional[str], expected: str) -> bool:
    """`TBEAM` matches `TBEAM_AXP2101` (v1.2 PMU variant) but not `TBEAM_SUPREME`-style
    names is not needed: the variants of one env share the prefix, other envs differ before it."""
    if not reported:
        return False
    return reported == expected or reported.startswith(expected + "_")


def _normalize_hw(s: str) -> str:
    """Formatting-insensitive form of a hardware string, e.g. 'TDECK+' and
    'TDECK_PLUS' both fold to 'TDECKPLUS'. Used only to spot a likely
    ENV_HARDWARE mapping bug (TM-47), not to decide the actual match."""
    return re.sub(r"[^A-Z0-9]", "", s.upper().replace("+", "PLUS"))


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
    # TM-47: character class must include '+' -- src/mheard_functions.cpp's
    # HardWare[] table has entries like "TDECK+" (T-Deck Plus, conditional
    # table index 17); a class of only [A-Z0-9_-] silently truncated it to
    # "TDECK", which then collided with the plain T-Deck's "TDECK" entry.
    # TM-46 retry case: after an aborted upload the node is still (or again) in
    # safeboot and serves the OTA page instead of the app -- there is no
    # hardware row to check against, but the OTA session is directly reachable.
    if "<title>MeshCom OTA</title>" in body:
        info["safeboot"] = "1"
    if m := re.search(r"Hardware</td><td>([A-Z0-9_+-]+)", body):
        info["hardware"] = m.group(1)
    if m := re.search(r"build: ([^<)]+)", body):
        info["build"] = m.group(1).strip()
    if m := re.search(r"Meshcom ([0-9]+\.[0-9]+[a-z]*)<br>\(build", body):
        info["version"] = m.group(1)
    return info


def fetch_ota_state(host: str, timeout: float = 5.0, get: GetFn = http_get) -> Optional[dict[str, Any]]:
    """GET /ota/state (docs/safeboot-ota-contract.md) and return the parsed JSON,
    or None when the node is unreachable, answers non-200 (including a 404 --
    tolerated here for nodes that predate this endpoint), or the body does
    not parse as a JSON object. Used to enrich a failed upload's OtaResult
    with the node's own abort reason (TM-49/abort bench)."""
    try:
        status, body = get(f"http://{host}/ota/state", timeout)
    except (urllib.error.URLError, OSError):
        return None
    if status != 200:
        return None
    try:
        data = json.loads(body)
    except ValueError:
        return None
    return data if isinstance(data, dict) else None


def _state_fields(state: Optional[dict[str, Any]]) -> dict[str, Any]:
    """`/ota/state` JSON -> the subset of OtaResult kwargs it fills in, empty
    when there was no state to fetch (old node, or connection never got that
    far). `app_valid` is absent from `state` on a safeboot image that
    predates the field (docs/safeboot-ota-contract.md "Single app slot") --
    that comes through as `None`, same as a node that reported it explicitly
    unknown; only an explicit `false` means the single app slot holds an
    incomplete image."""
    if not state:
        return {}
    return dict(state=state.get("state"), reason=state.get("reason") or None,
                received=state.get("received"), total=state.get("total"),
                fallback_in_ms=state.get("fallback_in_ms"), app_valid=state.get("app_valid"))


def app_back(info: Optional[dict[str, str]]) -> bool:
    """True only for the app's own index page after the OTA reboot.

    The reboot poll used to accept any HTTP 200 -- including the safeboot's
    OTA page, which node_info() returns as {"safeboot": "1"} without a build
    string. On a T-Deck Plus (2026-09-12) that ended the wait 25 s after the
    upload with "Done: Meshcom ? build ?" while the node was still in
    safeboot. Require the app page (no safeboot flag) with a build string.
    """
    return info is not None and not info.get("safeboot") and bool(info.get("build"))


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


def upload_multipart_controlled(url: str, path: Path, *, stop_after_fraction: Optional[float] = None,
                                 stall_after_fraction: Optional[float] = None, stall_seconds: float = 0.0,
                                 chunk: int = 4096, timeout: float = 180.0) -> tuple[int, str]:
    """Like `upload_multipart`, but streams the body over a raw socket with
    injectable mid-transfer misbehaviour for the abort bench
    (docs/bench-ota-regression.md TM-49, docs/safeboot-ota-contract.md abort
    reasons `client_disconnected`/`incomplete_upload`/`stalled`):

    - `stop_after_fraction`: stop sending and hard-close the socket once this
      fraction of the multipart body has gone out (kill scenario -- a client
      that vanishes mid-upload). There is no HTTP response to read once we
      killed the connection ourselves, so this returns `(0, "")`.
    - `stall_after_fraction`/`stall_seconds`: pause for `stall_seconds` after
      this fraction of the body has gone out, without closing the socket,
      then resume sending the rest and read the real response (stall
      scenario). A caller that wants to abandon the connection mid-stall
      (the doublestart scenario: leave the socket open, half-fed, and start a
      second session on a separate connection) runs this in a background
      thread with a `stall_seconds` longer than it needs and never joins it.

      Passing neither `stop_after_fraction` nor `stall_after_fraction` is a
      plain (if slower, one-socket-write-per-chunk) upload.
    """
    boundary = uuid.uuid4().hex
    head = (
        f"--{boundary}\r\n"
        f'Content-Disposition: form-data; name="file"; filename="{path.name}"\r\n'
        f"Content-Type: application/octet-stream\r\n\r\n"
    ).encode()
    tail = f"\r\n--{boundary}--\r\n".encode()
    body = head + path.read_bytes() + tail
    total = len(body)

    parts = urlsplit(url)
    if parts.hostname is None:
        raise ValueError(f"upload_multipart_controlled: url has no host: {url!r}")
    path_qs = parts.path or "/"
    if parts.query:
        path_qs += "?" + parts.query
    request_head = (
        f"POST {path_qs} HTTP/1.1\r\n"
        f"Host: {parts.hostname}\r\n"
        f"Content-Type: multipart/form-data; boundary={boundary}\r\n"
        f"Content-Length: {total}\r\n"
        f"Connection: close\r\n\r\n"
    ).encode()

    stop_at = int(total * stop_after_fraction) if stop_after_fraction is not None else None
    stall_at = int(total * stall_after_fraction) if stall_after_fraction is not None else None

    sock = socket.create_connection((parts.hostname, parts.port or 80), timeout=timeout)
    try:
        sock.sendall(request_head)
        sent = 0
        stalled = False
        while sent < total:
            if stop_at is not None and sent >= stop_at:
                sock.close()
                return 0, ""
            end = min(sent + chunk, total)
            if stop_at is not None and end > stop_at:
                end = stop_at
            if stall_at is not None and not stalled and end > stall_at:
                end = stall_at
            sock.sendall(body[sent:end])
            sent = end
            if stall_at is not None and not stalled and sent >= stall_at:
                stalled = True
                time.sleep(stall_seconds)
        # Full body sent (possibly after a stall) -- read the real response
        # the same way http.client would, reusing its status/header/chunked
        # body handling rather than reimplementing it.
        resp = http.client.HTTPResponse(sock, method="POST")
        resp.begin()
        resp_body = resp.read()
        return resp.status, resp_body.decode(errors="replace")
    finally:
        try:
            sock.close()
        except OSError:
            pass


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
    note: Optional[str] = None  # non-fatal observation, e.g. same build as before
    timings: dict[str, float] = field(default_factory=dict)
    # Populated from GET /ota/state (docs/safeboot-ota-contract.md) after a
    # failed upload -- None on older nodes that predate the endpoint, or when
    # the upload never got that far (e.g. hardware_mismatch, safeboot_timeout).
    state: Optional[str] = None
    reason: Optional[str] = None
    received: Optional[int] = None
    total: Optional[int] = None
    fallback_in_ms: Optional[int] = None
    # True/False from /ota/state's app_valid (docs/safeboot-ota-contract.md
    # "Single app slot"); None when the node predates the field or no state
    # was fetched at all.
    app_valid: Optional[bool] = None

    def node_state_line(self) -> Optional[str]:
        """Human line for the node's own /ota/state at the time of failure, e.g.
        'node reports: aborted (incomplete_upload), 1048576/2182465 bytes,
        fallback in 151 s'. None when no state was fetched (old node, or the
        failure happened before an OTA session existed)."""
        if not self.state:
            return None
        line = f"node reports: {self.state}"
        if self.reason:
            line += f" ({self.reason})"
        if self.received is not None and self.total is not None:
            line += f", {self.received}/{self.total} bytes"
        if self.fallback_in_ms is not None and self.fallback_in_ms >= 0:
            line += f", fallback in {self.fallback_in_ms / 1000:.0f} s"
        return line

    def line(self) -> str:
        if self.ok:
            a = self.after or {}
            return (f"OTA ok: Meshcom {a.get('version', '?')} build {a.get('build', '?')}, "
                    f"hardware {a.get('hardware', '?')} (total {self.timings.get('total_s', 0):.0f}s)")
        out = f"OTA FAILED at {self.stage}: {self.error}"
        extra = self.node_state_line()
        if extra:
            out += f" -- {extra}"
        return out


def flash(host: str, fw: Path, *, expect_hw: Optional[str] = None, force: bool = False,
          safeboot_poll_s: float = SAFEBOOT_POLL_S, reboot_poll_s: float = REBOOT_POLL_S,
          precheck_timeout: float = 10.0, poll_interval: float = 3.0,
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

    # TM-46 retry case: the node is already sitting in safeboot (aborted or
    # stale OTA session). The app page -- and with it the hardware row -- is
    # not available, and triggering safeboot again is pointless. Skip both and
    # go straight to the (now self-cleaning, TM-46) OTA session.
    in_safeboot = bool(info.get("safeboot"))
    if in_safeboot:
        phase("safeboot_resume", "")

    if not in_safeboot and not force and expect_hw and not hardware_matches(info.get("hardware"), expect_hw):
        timings["total_s"] = time.monotonic() - t_all
        reported = info.get("hardware")
        hint = ""
        if reported and _normalize_hw(reported) == _normalize_hw(expect_hw):
            hint = (" -- these differ only by formatting (e.g. 'TDECK+' vs 'TDECK_PLUS'), "
                    "likely an ENV_HARDWARE mapping bug rather than the wrong board")
        return OtaResult(
            ok=False, stage="hardware_mismatch", fw_path=str(fw), fw_size=fw_size, md5=md5,
            before=info, timings=timings,
            error=f"hardware mismatch: node reports {reported!r}, "
                  f"expected {expect_hw!r}{hint} (--force to flash anyway)")

    if not in_safeboot:
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

    if not poll(safeboot_up, safeboot_poll_s, interval=poll_interval,
                label="safeboot web server", on_wait=on_wait):
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
        # An HTTPError carries the safeboot's response body -- that is where
        # Update.printError() lands, i.e. the actual reason. Do not drop it.
        detail = ""
        if isinstance(e, urllib.error.HTTPError):
            try:
                detail = " -- " + e.read().decode(errors="replace").strip()
            except OSError:
                pass
        state = fetch_ota_state(host, get=get)
        return OtaResult(ok=False, stage="upload_failed", fw_path=str(fw), fw_size=fw_size,
                          md5=md5, before=info, timings=timings,
                          error=f"upload failed: {e}{detail}", **_state_fields(state))
    timings["upload_s"] = time.monotonic() - t
    if status != 200:
        timings["total_s"] = time.monotonic() - t_all
        state = fetch_ota_state(host, get=get)
        return OtaResult(ok=False, stage="upload_failed", fw_path=str(fw), fw_size=fw_size,
                          md5=md5, before=info, timings=timings,
                          error=f"upload failed: HTTP {status} {body.strip()}", **_state_fields(state))

    phase("reboot_wait", "")
    t = time.monotonic()
    def app_page() -> Optional[dict[str, str]]:
        i = node_info(host, get=get)
        return i if app_back(i) else None

    new_info = poll(app_page, reboot_poll_s, interval=poll_interval, label="app reboot",
                     on_wait=on_wait)
    timings["reboot_wait_s"] = time.monotonic() - t
    timings["total_s"] = time.monotonic() - t_all
    if not new_info:
        return OtaResult(ok=False, stage="reboot_timeout", fw_path=str(fw), fw_size=fw_size,
                          md5=md5, before=info, timings=timings,
                          error=f"node did not come back into the app within {reboot_poll_s:.0f}s "
                                "-- check it manually")

    note = None
    if info.get("build") and new_info.get("build") == info.get("build"):
        note = (f"build string unchanged ({new_info['build']}) -- same firmware as before, "
                "or the upload did not take")
    return OtaResult(ok=True, stage="done", fw_path=str(fw), fw_size=fw_size, md5=md5,
                      before=info, after=new_info, note=note, timings=timings)


# --- self-test --------------------------------------------------------


# TM-47: minimal fragments of the app index page's info table
# (src/web_functions/web_functions.cpp, ~line 1832 onward), just enough to
# exercise node_info()'s three regexes -- not full pages.
_FIXTURE_TDECK_PLUS = (
    "<tr><td>Hardware</td><td>TDECK+</td></tr>\n"
    "Meshcom 4.35p<br>(build: Aug 31 2026 12:00:00)\n"
)

_FIXTURE_HELTEC_V3 = (
    "<tr><td>Hardware</td><td>HELTEC_V3</td></tr>\n"
    "Meshcom 4.35p<br>(build: Aug 31 2026 12:00:00)\n"
)

_FIXTURE_SAFEBOOT = (
    "<title>MeshCom OTA</title>\n"
    "<h1>Safeboot OTA</h1>\n"
)


def run_self_test() -> int:
    """Offline parser checks (pattern: tools/resource_watch.py --self-test).
    No network, no serial -- runs node_info() against embedded HTML
    fragments and checks the ENV_HARDWARE lookups directly."""
    failures: list[str] = []

    def check(name: str, cond: bool) -> None:
        if not cond:
            failures.append(name)

    def fake_get(body: str) -> GetFn:
        return lambda url, timeout=5.0: (200, body)

    # regression: node_info() must keep the '+' in "TDECK+", not truncate to
    # "TDECK" (that value collides with the plain T-Deck's own HardWare[]
    # entry, src/mheard_functions.cpp line 84/86, index 8).
    tdeck_plus = node_info("fixture", get=fake_get(_FIXTURE_TDECK_PLUS))
    check("tdeck_plus info parsed", tdeck_plus is not None)
    if tdeck_plus is not None:
        check("tdeck_plus hardware keeps '+'", tdeck_plus.get("hardware") == "TDECK+")
        check("tdeck_plus version", tdeck_plus.get("version") == "4.35p")
        check("tdeck_plus build", tdeck_plus.get("build") == "Aug 31 2026 12:00:00")

    heltec_v3 = node_info("fixture", get=fake_get(_FIXTURE_HELTEC_V3))
    check("heltec_v3 info parsed", heltec_v3 is not None)
    if heltec_v3 is not None:
        check("heltec_v3 hardware", heltec_v3.get("hardware") == "HELTEC_V3")
        check("heltec_v3 not safeboot", heltec_v3.get("safeboot") is None)

    # TM-46 retry case: the safeboot OTA page must be recognized so flash()
    # can skip the (impossible) hardware check and the redundant trigger.
    sb = node_info("fixture", get=fake_get(_FIXTURE_SAFEBOOT))
    check("safeboot page parsed", sb is not None)
    if sb is not None:
        check("safeboot page detected", sb.get("safeboot") == "1")
        check("safeboot page has no hardware", sb.get("hardware") is None)

    # ENV_HARDWARE must match what each env's own build reports (TM-47: was
    # "TDECK_PLUS", the *other* HardWare[] table's spelling -- BOARD_T_DECK_PLUS
    # is defined for this env, so the conditional table applies, index 17
    # "TDECK+").
    check("ENV_HARDWARE t_deck_plus", ENV_HARDWARE.get("t_deck_plus") == "TDECK+")
    check("ENV_HARDWARE heltec_wifi_lora_32_V3",
          ENV_HARDWARE.get("heltec_wifi_lora_32_V3") == "HELTEC_V3")

    if tdeck_plus is not None:
        check("tdeck_plus reported matches ENV_HARDWARE",
              hardware_matches(tdeck_plus.get("hardware"), ENV_HARDWARE["t_deck_plus"]))
    if heltec_v3 is not None:
        check("heltec_v3 reported matches ENV_HARDWARE",
              hardware_matches(heltec_v3.get("hardware"), ENV_HARDWARE["heltec_wifi_lora_32_V3"]))

    # formatting-only mismatch hint: 'TDECK+' vs 'TDECK_PLUS' is not an
    # actual hardware_matches() match, but should normalize equal so the
    # error message can flag it as a likely ENV_HARDWARE bug.
    check("TDECK+ vs TDECK_PLUS not a real match",
          not hardware_matches("TDECK+", "TDECK_PLUS"))
    check("TDECK+ vs TDECK_PLUS normalize equal",
          _normalize_hw("TDECK+") == _normalize_hw("TDECK_PLUS"))
    # a genuine mismatch (different board) must not trigger the hint
    check("TBEAM vs HELTEC_V3 normalize differ",
          _normalize_hw("TBEAM") != _normalize_hw("HELTEC_V3"))

    # reboot-wait acceptance (2026-09-12 T-Deck Plus race): the safeboot page
    # and a page without a build string must not end the wait; the app page must.
    check("app_back rejects None", not app_back(None))
    check("app_back rejects safeboot page", not app_back({"safeboot": "1"}))
    check("app_back rejects page without build", not app_back({"hardware": "TDECK+"}))
    check("app_back accepts app page", app_back({"hardware": "TDECK+", "build": "x", "version": "4.35t"}))

    # whole flow against a scripted node: app (old build) -> trigger -> safeboot
    # /update up -> ota/start ok -> upload ok -> reboot poll sees the safeboot
    # page twice, then the app with the new build. Before the fix the first
    # safeboot page ended the poll with after={"safeboot": "1"}.
    import tempfile
    old_page = _FIXTURE_TDECK_PLUS
    new_page = old_page.replace("Aug 31 2026 12:00:00", "Sep 12 2026 22:07:05")
    root_pages = [old_page, _FIXTURE_SAFEBOOT, _FIXTURE_SAFEBOOT, new_page]
    seen_safeboot_in_reboot_poll = [0]

    def scripted_get(url: str, timeout: float = 5.0) -> tuple[int, str]:
        if url.endswith("/"):
            page = root_pages.pop(0) if len(root_pages) > 1 else root_pages[0]
            if page is _FIXTURE_SAFEBOOT:
                seen_safeboot_in_reboot_poll[0] += 1
            return 200, page
        return 200, "ok"

    def scripted_post(url: str, path: Path) -> tuple[int, str]:
        return 200, "ok"

    with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as tmp:
        tmp.write(b"\xe9firmware")
        fw_path = Path(tmp.name)
    try:
        res = flash("fixture", fw_path, expect_hw="TDECK+", poll_interval=0.0,
                    get=scripted_get, poster=scripted_post)
    finally:
        fw_path.unlink(missing_ok=True)
    check("flow ok", res.ok)
    check("flow saw safeboot page during reboot wait", seen_safeboot_in_reboot_poll[0] == 2)
    check("flow after is the app page", (res.after or {}).get("build") == "Sep 12 2026 22:07:05")
    check("flow before is the old build", (res.before or {}).get("build") == "Aug 31 2026 12:00:00")
    check("flow no same-build note", res.note is None)

    # same build re-flashed: still ok, but flagged
    root_pages[:] = [old_page, _FIXTURE_SAFEBOOT, old_page]
    with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as tmp:
        tmp.write(b"\xe9firmware")
        fw_path = Path(tmp.name)
    try:
        res2 = flash("fixture", fw_path, expect_hw="TDECK+", poll_interval=0.0,
                     get=scripted_get, poster=scripted_post)
    finally:
        fw_path.unlink(missing_ok=True)
    check("same-build flow ok", res2.ok)
    check("same-build flow flagged", bool(res2.note) and "unchanged" in (res2.note or ""))

    if failures:
        print(f"SELF-TEST FAILED ({len(failures)}): {', '.join(failures)}", file=sys.stderr)
        return 1
    print("SELF-TEST OK (all assertions passed)")
    return 0


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
    parser.add_argument("--self-test", action="store_true",
                         help="run offline parser self-tests (no network, no serial) and exit")
    return parser


def main(argv: Optional[list[str]] = None) -> int:
    args = build_parser().parse_args(argv)
    if args.self_test:
        return run_self_test()
    fw = resolve_firmware(args.env, args.bin)
    expect_hw = args.expect_hw or ENV_HARDWARE.get(args.env, "HELTEC_V3")

    def on_phase(name: str, detail: str) -> None:
        msg = {
            "firmware": f"Firmware: {detail}",
            "precheck": f"Checking node {detail} ...",
            "safeboot_resume": "Node is already in safeboot (aborted session?) -- "
                               "skipping hardware check and trigger ...",
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
        print(f"  node before OTA: Meshcom {b.get('version', '?')} build {b.get('build', '?')}, "
              f"hardware {b.get('hardware', '?')}")
    if not result.ok:
        print(f"error: {result.error}")
        state_line = result.node_state_line()
        if state_line:
            print(f"  {state_line}")
        if result.app_valid is False:
            print(f"  {APP_INVALID_NOTE}")
        return 1
    a = result.after or {}
    print(f"Done: Meshcom {a.get('version', '?')} build {a.get('build', '?')}, "
          f"hardware {a.get('hardware', '?')} (total {result.timings.get('total_s', 0):.0f}s)")
    if result.note:
        print(f"  note: {result.note}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
