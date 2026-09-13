#!/usr/bin/env python3
"""Automated OTA abort bench (TM-49) -- proves the safeboot completion gate:
a truncated, stalled or superseded upload must abort cleanly, never switch
partitions, and the node must still recover via the fallback-to-app window.

Binding contract: docs/safeboot-ota-contract.md (``/ota/state`` JSON, serial
markers, abort reasons). Procedure this automates: docs/bench-ota-regression.md
section "TM-49 arm".

Holds ONE USB serial session open for the whole run (like
``tools/bench/ota_regression.py`` -- native-USB S3 boards reboot on port
open, so this waits for ``CLIENT STARTED``/``[BOOT];ready`` before typing
anything) and drives the node over HTTP via ``tools/webflash.py``. Each
scenario gets its own ``<out>/ota_abort_<scenario>_<ts>.log`` slice of that
one session's serial line log, plus a ``<out>/ota_abort_<scenario>_<ts>.json``
summary with a pass/fail per assertion.

    python3 tools/bench/ota_abort.py --host 192.168.1.93 \\
        --port /dev/cu.usbserial-XXXX --env heltec_wifi_lora_32_V3 \\
        --fw .pio/build/heltec_wifi_lora_32_V3/firmware.bin --scenario kill50

    python3 tools/bench/ota_abort.py --host dk5en-93.local --port /dev/cu.usbserial-0001 \\
        --env heltec_wifi_lora_32_V3 --all --out tools/bench/runs/

Scenarios (see each `scenario_*` function's docstring for the exact
assertions, mirrored from the contract and the bench doc):

  control      the good path: HTTP 200 upload, verify/end;success on serial,
               app back within 120s, answers GET /.
  kill50       kill the upload's TCP connection at ~50% of the body.
  stall        pause the upload at ~50% for longer than the firmware's 30s
               stall watchdog.
  doublestart  start a second /ota/start while the first upload is still
               half-fed (never closed) -- the first session must be dropped
               as stale, the second must complete normally.
  cancel       GET /ota/cancel with no upload running (must succeed and fall
               back to the app), and again while an upload is in progress
               (must be refused, HTTP 400).

`--all` runs control, kill50, stall, doublestart, cancel, control -- waiting
between each for the node to be back in the app and answering GET / (up to
``--between-wait-s``, since a node left in an aborted/abandoned safeboot
session by one scenario needs its own fallback window to recover before the
next scenario can trigger safeboot again).

Exit code: 0 all assertions passed, 1 an assertion failed, 2 harness error
(bad arguments, firmware missing, serial port would not open, or the node
never came back between scenarios).
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import sys
import threading
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable, Optional

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ota_regression as otr  # noqa: E402
import urllib.error
import webflash  # noqa: E402  (path already added by ota_regression's own sys.path.insert)

RUN_PREFIX = "ota_abort"

# --- serial markers (docs/safeboot-ota-contract.md "Serial markers") --------

RE_OTA_ABORT = re.compile(r"\[SAFEBOOT\];ota;abort;reason;(?P<reason>[A-Za-z_]+)")
RE_OTA_VERIFY_OK = re.compile(r"\[SAFEBOOT\];ota;verify;result;ok")
RE_OTA_END_SUCCESS = re.compile(r"\[SAFEBOOT\];ota;end;result;success")
RE_FALLBACK_TIMEOUT = re.compile(r"\[SAFEBOOT\];fallback;reason;timeout")
RE_FALLBACK_TIMEOUT_LEGACY = re.compile(r"OTA Start Timeout")
RE_FALLBACK_CANCEL = re.compile(r"\[SAFEBOOT\];fallback;reason;cancel")
RE_FALLBACK_CANCEL_LEGACY = re.compile(r"Rebooting to app partition")
# Single app slot (docs/safeboot-ota-contract.md, bench finding 2026-09-13):
# printed at boot and after every abort. `RE_OTA_UPDATE_BANNER` is the plain
# boot banner a node re-entering safeboot prints (real capture,
# tools/bench/runs/ota_abort_kill50_20260913-154513.log) -- its presence
# after an abort with app_valid false means the firmware is (wrongly)
# looping through the old fallback/reboot cycle instead of holding still.
RE_APP_IMAGE = re.compile(r"\[SAFEBOOT\];app;image;(?P<result>valid|invalid);rc;(?P<rc>-?\d+)")
RE_OTA_UPDATE_BANNER = re.compile(r"OTA UDATE started")

# reasons /ota/state and the abort marker may report for a client-side kill
# vs stall (docs/safeboot-ota-contract.md's `reason` enum)
KILL_REASONS = frozenset({"client_disconnected", "incomplete_upload", "stalled"})
# A stalled client is closed by the async server's own RX timeout (~3 s,
# AsyncTCP) long before the safeboot's 30 s stall watchdog can fire, so the
# node reports client_disconnected; "stalled" stays accepted for a client that
# keeps the socket alive without data (measured Heltec V3 2026-09-13: abort
# 4 s after the last chunk).
STALL_REASONS = frozenset({"stalled", "client_disconnected"})
STALE_SESSION_REASONS = frozenset({"stale_session"})


@dataclass
class Assertion:
    name: str
    passed: bool
    detail: str = ""


@dataclass
class ScenarioResult:
    name: str
    assertions: list[Assertion]
    started: str
    extra: dict[str, Any] = field(default_factory=dict)

    @property
    def passed(self) -> bool:
        return all(a.passed for a in self.assertions)


# --- small HTTP/serial helpers shared by every scenario ---------------------


def safe_get(get: webflash.GetFn, url: str, timeout: float = 5.0) -> tuple[Optional[int], str]:
    """`get()` raises on a connection error/timeout (that is the expected
    outcome of half of what this bench does to the node) -- turn that into a
    (None, detail) pair instead of an exception so scenario code reads the
    same either way."""
    try:
        return get(url, timeout)
    except urllib.error.HTTPError as e:
        # 4xx/5xx is an answer from the node (e.g. /ota/cancel -> 409
        # app_invalid), not a transport failure: hand the status back.
        try:
            body = e.read().decode(errors="replace")
        except Exception:  # noqa: BLE001
            body = str(e)
        return e.code, body
    except Exception as e:  # noqa: BLE001 - urllib.error.URLError, OSError, ...
        return None, str(e)


def wait_safeboot_up(target: str, get: webflash.GetFn, timeout: float) -> bool:
    def check() -> bool:
        status, _ = safe_get(get, f"http://{target}/update", 5.0)
        return status == 200
    return bool(webflash.poll(check, timeout, interval=3.0))


def wait_app_back(target: str, get: webflash.GetFn, timeout: float) -> bool:
    def check() -> bool:
        return webflash.app_back(webflash.node_info(target, get=get))
    return bool(webflash.poll(check, timeout, interval=3.0))


def check_root(target: str, get: webflash.GetFn, timeout: float = 60.0) -> bool:
    """The app answers GET / only after its own WiFi join, which follows
    [BOOT];ready by several seconds -- poll instead of a single shot."""
    def check() -> bool:
        status, _ = safe_get(get, f"http://{target}/", 5.0)
        return status == 200
    return bool(webflash.poll(check, timeout, interval=2.0))


def poll_state(target: str, get: webflash.GetFn, timeout: float,
                want_state: Optional[str] = None) -> Optional[dict[str, Any]]:
    """Poll GET /ota/state until it reports `want_state` (any state at all
    when None), or the timeout passes. Returns the last state seen (even a
    non-matching one) so a failed assertion can still show what the node
    actually reported."""
    last: dict[str, Any] = {}

    def check() -> Optional[dict[str, Any]]:
        state = webflash.fetch_ota_state(target, get=get)
        if state is None:
            return None
        last.update(state)
        if want_state is not None and state.get("state") != want_state:
            return None
        return state

    result = webflash.poll(check, timeout, interval=1.0)
    return result if result is not None else (last or None)


def wait_for_ts(sess: "otr.SerialSession", pattern: "re.Pattern[str]", timeout: float,
                 since_ts: float = 0.0) -> Optional[tuple["re.Match[str]", float]]:
    """Like `SerialSession.wait_for` but also returns the wall-clock
    timestamp of the matching line, so scenarios can measure e.g. the time
    from abort to the app coming back up."""
    deadline = time.time() + timeout
    while True:
        with sess.lock:
            for t, line in sess.lines:
                if t >= since_ts:
                    m = pattern.search(line)
                    if m:
                        return m, t
        if time.time() >= deadline:
            return None
        time.sleep(0.1)


def wait_for_any_ts(sess: "otr.SerialSession", patterns: list["re.Pattern[str]"], timeout: float,
                      since_ts: float = 0.0) -> Optional[tuple["re.Match[str]", float]]:
    deadline = time.time() + timeout
    while True:
        with sess.lock:
            for t, line in sess.lines:
                if t >= since_ts:
                    for pattern in patterns:
                        m = pattern.search(line)
                        if m:
                            return m, t
        if time.time() >= deadline:
            return None
        time.sleep(0.1)


def not_seen(sess: "otr.SerialSession", pattern: "re.Pattern[str]", since_ts: float) -> bool:
    """True when `pattern` has not appeared in the session's log since
    `since_ts` -- checked once against what has already been logged, no
    waiting (the caller has usually just finished waiting out the window
    this needs to hold over)."""
    with sess.lock:
        return not any(t >= since_ts and pattern.search(line) for t, line in sess.lines)


# Type of webflash.upload_multipart_controlled -- richer than PostFn (extra
# keyword-only knobs), kept injectable so tests can fake the raw-socket
# upload the same way `get`/`poster` fake the plain HTTP calls.
ControlledPostFn = Callable[..., "tuple[int, str]"]


def _run_controlled_upload_bg(url: str, fw: Path, *, controlled_poster: ControlledPostFn,
                                **kwargs: Any) -> tuple[dict[str, Any], threading.Thread]:
    """Run `controlled_poster` (default `webflash.upload_multipart_controlled`)
    in a background thread -- needed whenever the scenario wants to keep
    driving the node (polling /ota/state, watching serial) while the upload
    is deliberately paused or abandoned. OSError from the socket erroring
    out once the node has already dropped the connection (the expected
    outcome after a stall/stale-session abort) is swallowed into the result
    dict rather than raised in the thread."""
    result: dict[str, Any] = {}

    def _run() -> None:
        try:
            status, body = controlled_poster(url, fw, **kwargs)
        except OSError as e:
            status, body = 0, str(e)
        result["status"] = status
        result["body"] = body

    th = threading.Thread(target=_run, daemon=True, name="ota-abort-upload")
    th.start()
    return result, th


def assert_abort_and_recover(sess: "otr.SerialSession", target: str, get: webflash.GetFn,
                               poster: webflash.PostFn, md5: str, fw: Path, t_trigger: float,
                               args: argparse.Namespace, assertions: list[Assertion],
                               *, expected_reasons: frozenset, abort_wait_s: float) -> dict[str, Any]:
    """Shared tail for kill50/stall: the node must abort with one of
    `expected_reasons` (both on /ota/state and on serial), never print the
    success markers, and then recover -- either through a full upload on the
    same safeboot session (`app_valid` false: the expected outcome on every
    one of our boards, docs/safeboot-ota-contract.md "Single app slot" --
    the aborted upload already wrote into the single app slot, so there is
    nothing left to fall back to) or through the 180s fallback-to-app window
    (`app_valid` true: an abort before any chunk was written, or a future
    dual-slot board). An older safeboot image that predates `app_valid`
    takes the fallback path too, but fails one assertion so it stands out in
    the report. Returns the scenario's `extra` fields (`app_valid`,
    `bytes_before_kill`, `abort_to_boot_s` where observed)."""
    abort = wait_for_ts(sess, RE_OTA_ABORT, abort_wait_s, since_ts=t_trigger)
    assertions.append(Assertion(
        f"serial ota;abort;reason;<r> within {abort_wait_s:.0f}s, reason in {sorted(expected_reasons)}",
        abort is not None and abort[0].group("reason") in expected_reasons,
        (abort[0].group("reason") if abort else "not seen")))
    abort_ts = abort[1] if abort else None

    state = poll_state(target, get, 40.0, want_state="aborted")
    ok_state = state is not None and state.get("reason") in expected_reasons
    assertions.append(Assertion("/ota/state aborted with expected reason", ok_state, f"state={state}"))

    has_app_valid = state is not None and "app_valid" in state
    app_valid = state.get("app_valid") if state is not None else None
    assertions.append(Assertion(
        "app_valid reported", has_app_valid,
        "" if has_app_valid else f"missing from /ota/state -- older safeboot image (state={state})"))

    extra: dict[str, Any] = {"app_valid": app_valid if has_app_valid else "missing"}
    if state is not None and state.get("received") is not None:
        extra["bytes_before_kill"] = state.get("received")

    if app_valid is False:
        boot_ts = _assert_app_invalid_and_recover(
            sess, target, get, poster, md5, fw, t_trigger, state, args, assertions)
    else:
        boot_ts = _assert_fallback_recovery(sess, target, get, t_trigger, args, assertions)

    if abort_ts is not None and boot_ts is not None:
        extra["abort_to_boot_s"] = round(boot_ts - abort_ts, 1)
    return extra


def _assert_fallback_recovery(sess: "otr.SerialSession", target: str, get: webflash.GetFn,
                                t_trigger: float, args: argparse.Namespace,
                                assertions: list[Assertion]) -> Optional[float]:
    """`app_valid` true (or missing -- an older safeboot image): today's
    fallback-to-app expectations, unchanged."""
    time.sleep(args.post_abort_settle_s)
    status_upd, _ = safe_get(get, f"http://{target}/update", 5.0)
    assertions.append(Assertion(
        f"node stays in safeboot {args.post_abort_settle_s:.0f}s after abort (/update still 200)",
        status_upd == 200, f"HTTP {status_upd}"))

    remaining = max(1.0, args.fallback_total_wait_s - (time.time() - t_trigger))
    fallback = wait_for_any_ts(sess, [RE_FALLBACK_TIMEOUT, RE_FALLBACK_TIMEOUT_LEGACY],
                                remaining, since_ts=t_trigger)
    assertions.append(Assertion("serial fallback;reason;timeout (or legacy 'OTA Start Timeout')",
                                 fallback is not None))

    remaining2 = max(1.0, args.fallback_total_wait_s - (time.time() - t_trigger))
    boot = wait_for_ts(sess, otr.RE_BOOT_READY, remaining2, since_ts=t_trigger)
    assertions.append(Assertion("[BOOT];ready after the fallback", boot is not None))

    assertions.append(Assertion("no serial ota;verify;result;ok", not_seen(sess, RE_OTA_VERIFY_OK, t_trigger)))
    assertions.append(Assertion("no serial ota;end;result;success", not_seen(sess, RE_OTA_END_SUCCESS, t_trigger)))

    return boot[1] if boot else None


def _assert_app_invalid_and_recover(sess: "otr.SerialSession", target: str, get: webflash.GetFn,
                                      poster: webflash.PostFn, md5: str, fw: Path, t_trigger: float,
                                      state: Optional[dict[str, Any]], args: argparse.Namespace,
                                      assertions: list[Assertion]) -> Optional[float]:
    """`app_valid` false: the single app slot holds an incomplete image
    (docs/safeboot-ota-contract.md "Single app slot"). The node must hold
    still in safeboot -- no fallback timer, no reboot loop -- until a
    complete upload recovers it; that recovery upload is part of this
    scenario's PASS, not a separate scenario."""
    invalid_marker = wait_for_ts(sess, RE_APP_IMAGE, args.serial_timeout_s, since_ts=t_trigger)
    assertions.append(Assertion(
        "serial app;image;invalid after the abort",
        invalid_marker is not None and invalid_marker[0].group("result") == "invalid",
        (invalid_marker[0].group(0) if invalid_marker else "not seen")))

    assertions.append(Assertion(
        "fallback_in_ms == -1 (app invalid, no fallback possible)",
        state is not None and state.get("fallback_in_ms") == -1,
        f"fallback_in_ms={state.get('fallback_in_ms') if state else None}"))

    time.sleep(args.app_invalid_settle_s)
    status_upd, _ = safe_get(get, f"http://{target}/update", 5.0)
    assertions.append(Assertion(
        f"node stays in safeboot {args.app_invalid_settle_s:.0f}s with app invalid (/update still 200)",
        status_upd == 200, f"HTTP {status_upd}"))
    assertions.append(Assertion(
        "no OTA UDATE started boot banner in that window (no reboot loop)",
        not_seen(sess, RE_OTA_UPDATE_BANNER, t_trigger)))
    assertions.append(Assertion(
        "no serial fallback;reason;timeout while app invalid (no reboot loop)",
        not_seen(sess, RE_FALLBACK_TIMEOUT, t_trigger)))

    status_cancel, body_cancel = safe_get(get, f"http://{target}/ota/cancel", 10.0)
    assertions.append(Assertion(
        "/ota/cancel 409 app_invalid",
        status_cancel == 409 and "app_invalid" in body_cancel,
        f"HTTP {status_cancel} {body_cancel}"))

    assertions.append(Assertion("no serial ota;verify;result;ok", not_seen(sess, RE_OTA_VERIFY_OK, t_trigger)))
    assertions.append(Assertion("no serial ota;end;result;success", not_seen(sess, RE_OTA_END_SUCCESS, t_trigger)))

    # -- recovery: a full upload (control flow) on the same safeboot session
    # must succeed and bring the app back; this is part of the scenario's PASS.
    status_start, body_start = safe_get(get, f"http://{target}/ota/start?mode=fr&hash={md5}", 10.0)
    assertions.append(Assertion("recovery /ota/start 200", status_start == 200,
                                 f"HTTP {status_start} {body_start}"))
    t_recover = time.time()
    up_status, up_body = poster(f"http://{target}/ota/upload", fw)
    assertions.append(Assertion("recovery upload HTTP 200", up_status == 200,
                                 f"HTTP {up_status} {up_body}"))
    verify_ok = wait_for_ts(sess, RE_OTA_VERIFY_OK, args.serial_timeout_s, since_ts=t_recover)
    assertions.append(Assertion("recovery serial ota;verify;result;ok", verify_ok is not None))
    end_ok = wait_for_ts(sess, RE_OTA_END_SUCCESS, args.serial_timeout_s, since_ts=t_recover)
    assertions.append(Assertion("recovery serial ota;end;result;success", end_ok is not None))
    boot = wait_for_any_ts(sess, [otr.RE_BOOT_READY, otr.RE_CLIENT_STARTED], args.reboot_poll_s,
                            since_ts=t_recover)
    assertions.append(Assertion("app back after recovery upload ([BOOT];ready)", boot is not None))
    assertions.append(Assertion("GET / answers after recovery", check_root(target, get)))

    return boot[1] if boot else None


# --- scenarios ---------------------------------------------------------------


def scenario_control(target: str, sess: "otr.SerialSession", fw: Path, args: argparse.Namespace,
                       get: webflash.GetFn, poster: webflash.PostFn,
                       controlled_poster: ControlledPostFn) -> ScenarioResult:
    """Full webflash-style flow: HTTP 200 on upload, serial
    verify;result;ok and end;result;success, node back in the app
    ([BOOT];ready or CLIENT STARTED) within `--reboot-poll-s`, answers
    GET /. The control arm for every abort scenario -- run first (nothing
    upstream to blame) and last (nothing broken by the abort scenarios)."""
    del controlled_poster  # unused: control never drives the raw-socket path
    assertions: list[Assertion] = []
    t0 = time.time()
    expect_hw = webflash.ENV_HARDWARE.get(args.env, args.env.upper())

    ota = webflash.flash(target, fw, expect_hw=expect_hw, force=args.force, get=get,
                          poster=poster, poll_interval=args.poll_interval,
                          safeboot_poll_s=args.safeboot_poll_s, reboot_poll_s=args.reboot_poll_s)
    assertions.append(Assertion("full OTA flow ok (HTTP 200 on upload)", ota.ok, ota.error or ""))

    verify_ok = wait_for_ts(sess, RE_OTA_VERIFY_OK, args.serial_timeout_s, since_ts=t0)
    assertions.append(Assertion("serial ota;verify;result;ok", verify_ok is not None))
    end_ok = wait_for_ts(sess, RE_OTA_END_SUCCESS, args.serial_timeout_s, since_ts=t0)
    assertions.append(Assertion("serial ota;end;result;success", end_ok is not None))

    boot = wait_for_any_ts(sess, [otr.RE_BOOT_READY, otr.RE_CLIENT_STARTED], args.reboot_poll_s, since_ts=t0)
    assertions.append(Assertion(
        f"node back in the app ([BOOT];ready or CLIENT STARTED) within {args.reboot_poll_s:.0f}s",
        boot is not None))

    assertions.append(Assertion("GET / answers", check_root(target, get)))

    return ScenarioResult("control", assertions, otr.wall_now(t0))


def scenario_kill50(target: str, sess: "otr.SerialSession", fw: Path, args: argparse.Namespace,
                      get: webflash.GetFn, poster: webflash.PostFn,
                      controlled_poster: ControlledPostFn) -> ScenarioResult:
    """Kill the upload's TCP connection at ~50% of the body. The POST must
    end without a 200 (status 0 -- we closed it -- or a 4xx), the node must
    abort (client_disconnected/incomplete_upload/stalled) rather than switch
    partitions, and must then recover -- via a full upload on the same
    session on our single-app-slot boards (`app_valid` false), or via the
    fallback window otherwise (see `assert_abort_and_recover`)."""
    assertions: list[Assertion] = []
    t0 = time.time()
    md5 = hashlib.md5(fw.read_bytes()).hexdigest()

    safe_get(get, f"http://{target}/callfunction/?otaupdate", 10.0)
    up = wait_safeboot_up(target, get, args.safeboot_poll_s)
    assertions.append(Assertion("safeboot web server up", up))
    if not up:
        return ScenarioResult("kill50", assertions, otr.wall_now(t0))

    status, body = safe_get(get, f"http://{target}/ota/start?mode=fr&hash={md5}", 10.0)
    assertions.append(Assertion("/ota/start 200", status == 200, f"HTTP {status} {body}"))

    t_upload = time.time()
    up_status, up_body = controlled_poster(
        f"http://{target}/ota/upload", fw, stop_after_fraction=0.5, stall_after_fraction=None,
        stall_seconds=0.0)
    assertions.append(Assertion("upload ends without a 200 (client killed at ~50%)",
                                 up_status == 0 or 400 <= up_status < 500,
                                 f"status={up_status} body={up_body!r}"))

    extra = assert_abort_and_recover(
        sess, target, get, poster, md5, fw, t_upload, args, assertions,
        expected_reasons=KILL_REASONS, abort_wait_s=40.0)

    return ScenarioResult("kill50", assertions, otr.wall_now(t0), extra=extra)


def scenario_stall(target: str, sess: "otr.SerialSession", fw: Path, args: argparse.Namespace,
                     get: webflash.GetFn, poster: webflash.PostFn,
                     controlled_poster: ControlledPostFn) -> ScenarioResult:
    """Pause the upload at ~50% for longer than the firmware's 30s stall
    watchdog (docs/safeboot-ota-contract.md's state machine constant). The
    node must abort with reason `stalled` on its own, then recover exactly
    like kill50 (see `assert_abort_and_recover`)."""
    assertions: list[Assertion] = []
    t0 = time.time()
    md5 = hashlib.md5(fw.read_bytes()).hexdigest()

    safe_get(get, f"http://{target}/callfunction/?otaupdate", 10.0)
    up = wait_safeboot_up(target, get, args.safeboot_poll_s)
    assertions.append(Assertion("safeboot web server up", up))
    if not up:
        return ScenarioResult("stall", assertions, otr.wall_now(t0))

    status, body = safe_get(get, f"http://{target}/ota/start?mode=fr&hash={md5}", 10.0)
    assertions.append(Assertion("/ota/start 200", status == 200, f"HTTP {status} {body}"))

    t_upload = time.time()
    _, upload_thread = _run_controlled_upload_bg(
        f"http://{target}/ota/upload", fw, controlled_poster=controlled_poster,
        stop_after_fraction=None, stall_after_fraction=0.5, stall_seconds=args.stall_seconds)

    extra = assert_abort_and_recover(
        sess, target, get, poster, md5, fw, t_upload, args, assertions,
        expected_reasons=STALL_REASONS, abort_wait_s=args.stall_seconds)

    upload_thread.join(timeout=max(5.0, args.stall_seconds))

    return ScenarioResult("stall", assertions, otr.wall_now(t0), extra=extra)


def scenario_doublestart(target: str, sess: "otr.SerialSession", fw: Path, args: argparse.Namespace,
                           get: webflash.GetFn, poster: webflash.PostFn,
                           controlled_poster: ControlledPostFn) -> ScenarioResult:
    """Start an upload, stop feeding it at ~30% *without closing the
    socket*, then call /ota/start again on a fresh connection. The first
    session must be dropped as stale (serial abort;reason;stale_session) and
    the second /ota/start must still answer 200; a full upload on that
    second session must then succeed like the control scenario."""
    assertions: list[Assertion] = []
    t0 = time.time()
    md5 = hashlib.md5(fw.read_bytes()).hexdigest()

    safe_get(get, f"http://{target}/callfunction/?otaupdate", 10.0)
    up = wait_safeboot_up(target, get, args.safeboot_poll_s)
    assertions.append(Assertion("safeboot web server up", up))
    if not up:
        return ScenarioResult("doublestart", assertions, otr.wall_now(t0))

    status, body = safe_get(get, f"http://{target}/ota/start?mode=fr&hash={md5}", 10.0)
    assertions.append(Assertion("first /ota/start 200", status == 200, f"HTTP {status} {body}"))

    t_first = time.time()
    # Deliberately abandoned: a stall_seconds far longer than this scenario
    # runs means the background thread never gets to resume sending on its
    # own -- the socket just sits open, half-fed, until the second
    # /ota/start below forces the node to drop it as stale.
    _run_controlled_upload_bg(
        f"http://{target}/ota/upload", fw, controlled_poster=controlled_poster,
        stop_after_fraction=None, stall_after_fraction=0.3, stall_seconds=args.doublestart_abandon_s)
    time.sleep(args.doublestart_settle_s)

    status2, body2 = safe_get(get, f"http://{target}/ota/start?mode=fr&hash={md5}", 10.0)
    assertions.append(Assertion("second /ota/start 200", status2 == 200, f"HTTP {status2} {body2}"))

    stale = wait_for_ts(sess, RE_OTA_ABORT, args.serial_timeout_s, since_ts=t_first)
    assertions.append(Assertion(
        "serial ota;abort;reason;stale_session",
        stale is not None and stale[0].group("reason") in STALE_SESSION_REASONS,
        (stale[0].group("reason") if stale else "not seen")))

    # Single app slot (docs/safeboot-ota-contract.md): the abandoned first
    # session already wrote into it, so app_valid should have gone false --
    # not re-asserted here (the second session below overwrites and
    # revalidates it), but an older safeboot image that predates the field
    # entirely should still be visible in the report.
    state_after_stale = poll_state(target, get, args.serial_timeout_s)
    has_app_valid = state_after_stale is not None and "app_valid" in state_after_stale
    assertions.append(Assertion(
        "app_valid reported (first session)", has_app_valid,
        "" if has_app_valid else f"missing from /ota/state -- older safeboot image "
                                  f"(state={state_after_stale})"))

    # Control-style assertions for the full upload on the (now sole) second session.
    t_second = time.time()
    up_status, up_body = poster(f"http://{target}/ota/upload", fw)
    assertions.append(Assertion("second upload HTTP 200", up_status == 200, f"HTTP {up_status} {up_body}"))

    verify_ok = wait_for_ts(sess, RE_OTA_VERIFY_OK, args.serial_timeout_s, since_ts=t_second)
    assertions.append(Assertion("serial ota;verify;result;ok (second session)", verify_ok is not None))
    end_ok = wait_for_ts(sess, RE_OTA_END_SUCCESS, args.serial_timeout_s, since_ts=t_second)
    assertions.append(Assertion("serial ota;end;result;success (second session)", end_ok is not None))

    boot = wait_for_any_ts(sess, [otr.RE_BOOT_READY, otr.RE_CLIENT_STARTED], args.reboot_poll_s,
                            since_ts=t_second)
    assertions.append(Assertion(f"node back in the app within {args.reboot_poll_s:.0f}s", boot is not None))
    assertions.append(Assertion("GET / answers", check_root(target, get)))

    extra = {"app_valid_first_session": state_after_stale.get("app_valid") if has_app_valid else "missing"}
    return ScenarioResult("doublestart", assertions, otr.wall_now(t0), extra=extra)


def scenario_cancel(target: str, sess: "otr.SerialSession", fw: Path, args: argparse.Namespace,
                      get: webflash.GetFn, poster: webflash.PostFn,
                      controlled_poster: ControlledPostFn) -> ScenarioResult:
    """Part A: GET /ota/cancel with no upload running must succeed (200) and
    fall back to the app (serial fallback;reason;cancel, or the legacy
    "Rebooting to app partition" line). Part B: GET /ota/cancel while an
    upload is in progress (stalled at ~50%) must be refused (400); recovery
    from that abandoned session is left to the next scenario's
    between-scenario wait rather than re-checked here. Part C: after a kill
    has left the single app slot invalid (docs/safeboot-ota-contract.md),
    /ota/cancel must be refused with 409 app_invalid -- then a full recovery
    upload on the same session must succeed."""
    assertions: list[Assertion] = []
    t0 = time.time()
    md5 = hashlib.md5(fw.read_bytes()).hexdigest()

    # -- Part A: cancel with nothing running -------------------------------
    safe_get(get, f"http://{target}/callfunction/?otaupdate", 10.0)
    up = wait_safeboot_up(target, get, args.safeboot_poll_s)
    assertions.append(Assertion("safeboot web server up (part A)", up))
    if up:
        t_cancel = time.time()
        status, body = safe_get(get, f"http://{target}/ota/cancel", 10.0)
        assertions.append(Assertion("/ota/cancel 200 (no upload running)", status == 200,
                                     f"HTTP {status} {body}"))

        fallback = wait_for_any_ts(sess, [RE_FALLBACK_CANCEL, RE_FALLBACK_CANCEL_LEGACY],
                                    args.cancel_wait_s, since_ts=t_cancel)
        assertions.append(Assertion(
            "serial fallback;reason;cancel (or legacy 'Rebooting to app partition')",
            fallback is not None))

        remaining = max(1.0, args.cancel_wait_s - (time.time() - t_cancel))
        boot = wait_for_any_ts(sess, [otr.RE_BOOT_READY, otr.RE_CLIENT_STARTED], remaining, since_ts=t_cancel)
        assertions.append(Assertion(f"app boot within {args.cancel_wait_s:.0f}s", boot is not None))
    else:
        for name in ("/ota/cancel 200 (no upload running)",
                     "serial fallback;reason;cancel (or legacy 'Rebooting to app partition')",
                     f"app boot within {args.cancel_wait_s:.0f}s"):
            assertions.append(Assertion(name, False, "skipped: safeboot never came up"))

    # -- Part B: cancel while an upload is running must be refused ---------
    safe_get(get, f"http://{target}/callfunction/?otaupdate", 10.0)
    up2 = wait_safeboot_up(target, get, args.safeboot_poll_s)
    assertions.append(Assertion("safeboot web server up (part B)", up2))
    if up2:
        status_start, body_start = safe_get(get, f"http://{target}/ota/start?mode=fr&hash={md5}", 10.0)
        assertions.append(Assertion("/ota/start 200 (part B)", status_start == 200,
                                     f"HTTP {status_start} {body_start}"))
        _, upload_thread = _run_controlled_upload_bg(
            f"http://{target}/ota/upload", fw, controlled_poster=controlled_poster,
            stop_after_fraction=None, stall_after_fraction=0.5, stall_seconds=args.stall_seconds)
        time.sleep(args.doublestart_settle_s)  # let the stall actually engage first

        status_cancel, body_cancel = safe_get(get, f"http://{target}/ota/cancel", 10.0)
        assertions.append(Assertion("/ota/cancel 400 while an upload is in progress",
                                     status_cancel == 400, f"HTTP {status_cancel} {body_cancel}"))
        upload_thread.join(timeout=max(5.0, args.stall_seconds))
    else:
        assertions.append(Assertion("/ota/start 200 (part B)", False, "skipped: safeboot never came up"))
        assertions.append(Assertion("/ota/cancel 400 while an upload is in progress", False,
                                     "skipped: safeboot never came up"))

    # -- Part C: cancel after a kill (app_valid false) must be refused, then
    # a full recovery upload on the same session must succeed ---------------
    safe_get(get, f"http://{target}/callfunction/?otaupdate", 10.0)
    up3 = wait_safeboot_up(target, get, args.safeboot_poll_s)
    assertions.append(Assertion("safeboot web server up (part C)", up3))
    if up3:
        status_start3, body_start3 = safe_get(get, f"http://{target}/ota/start?mode=fr&hash={md5}", 10.0)
        assertions.append(Assertion("/ota/start 200 (part C)", status_start3 == 200,
                                     f"HTTP {status_start3} {body_start3}"))

        t_kill = time.time()
        up_status3, up_body3 = controlled_poster(
            f"http://{target}/ota/upload", fw, stop_after_fraction=0.5, stall_after_fraction=None,
            stall_seconds=0.0)
        assertions.append(Assertion(
            "part C upload ends without a 200 (client killed at ~50%)",
            up_status3 == 0 or 400 <= up_status3 < 500, f"status={up_status3} body={up_body3!r}"))

        abort3 = wait_for_ts(sess, RE_OTA_ABORT, args.serial_timeout_s, since_ts=t_kill)
        assertions.append(Assertion(
            "part C serial ota;abort;reason;<r>", abort3 is not None,
            (abort3[0].group("reason") if abort3 else "not seen")))

        status_cancel3, body_cancel3 = safe_get(get, f"http://{target}/ota/cancel", 10.0)
        assertions.append(Assertion(
            "/ota/cancel 409 app_invalid (part C, after a kill)",
            status_cancel3 == 409 and "app_invalid" in body_cancel3,
            f"HTTP {status_cancel3} {body_cancel3}"))

        status_start4, body_start4 = safe_get(get, f"http://{target}/ota/start?mode=fr&hash={md5}", 10.0)
        assertions.append(Assertion("part C recovery /ota/start 200", status_start4 == 200,
                                     f"HTTP {status_start4} {body_start4}"))
        t_recover = time.time()
        up_status4, up_body4 = poster(f"http://{target}/ota/upload", fw)
        assertions.append(Assertion("part C recovery upload HTTP 200", up_status4 == 200,
                                     f"HTTP {up_status4} {up_body4}"))
        verify_ok4 = wait_for_ts(sess, RE_OTA_VERIFY_OK, args.serial_timeout_s, since_ts=t_recover)
        assertions.append(Assertion("part C recovery serial ota;verify;result;ok", verify_ok4 is not None))
        end_ok4 = wait_for_ts(sess, RE_OTA_END_SUCCESS, args.serial_timeout_s, since_ts=t_recover)
        assertions.append(Assertion("part C recovery serial ota;end;result;success", end_ok4 is not None))
        boot4 = wait_for_any_ts(sess, [otr.RE_BOOT_READY, otr.RE_CLIENT_STARTED], args.reboot_poll_s,
                                 since_ts=t_recover)
        assertions.append(Assertion("part C app back after recovery ([BOOT];ready)", boot4 is not None))
        assertions.append(Assertion("GET / answers after part C recovery", check_root(target, get)))
    else:
        for name in ("/ota/start 200 (part C)", "part C upload ends without a 200 (client killed at ~50%)",
                     "part C serial ota;abort;reason;<r>", "/ota/cancel 409 app_invalid (part C, after a kill)",
                     "part C recovery /ota/start 200", "part C recovery upload HTTP 200",
                     "part C recovery serial ota;verify;result;ok", "part C recovery serial ota;end;result;success",
                     "part C app back after recovery ([BOOT];ready)", "GET / answers after part C recovery"):
            assertions.append(Assertion(name, False, "skipped: safeboot never came up"))

    return ScenarioResult("cancel", assertions, otr.wall_now(t0))


SCENARIO_FNS: dict[str, Callable[[str, "otr.SerialSession", Path, argparse.Namespace, webflash.GetFn,
                                   webflash.PostFn, ControlledPostFn], ScenarioResult]] = {
    "control": scenario_control,
    "kill50": scenario_kill50,
    "stall": scenario_stall,
    "doublestart": scenario_doublestart,
    "cancel": scenario_cancel,
}

ALL_SCENARIO_SEQUENCE = ["control", "kill50", "stall", "doublestart", "cancel", "control"]


# --- reporting ---------------------------------------------------------------


def render_scenario(result: ScenarioResult) -> str:
    lines = [f"scenario           {result.name}",
             f"verdict            {'PASS' if result.passed else 'FAIL'}"]
    for a in result.assertions:
        mark = "OK  " if a.passed else "FAIL"
        lines.append(f"  [{mark}] {a.name}" + (f"  ({a.detail})" if a.detail else ""))
    for k, v in result.extra.items():
        lines.append(f"  {k}: {v}")
    return "\n".join(lines)


def write_scenario_result(out_dir: Path, name: str, ts: str, result: ScenarioResult) -> Path:
    payload = {
        "scenario": result.name,
        "started": result.started,
        "verdict": "PASS" if result.passed else "FAIL",
        "assertions": [dict(name=a.name, passed=a.passed, detail=a.detail) for a in result.assertions],
        "extra": result.extra,
    }
    path = out_dir / f"{RUN_PREFIX}_{name}_{ts}.json"
    path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    return path


def default_out_dir() -> Path:
    return Path(__file__).resolve().parent / "runs"


def _switch_log(sess: "otr.SerialSession", log_path: Path) -> None:
    """Point the one held-open session's per-line log at a new file. The
    port itself is opened exactly once for the whole run (see the module
    docstring); this only redirects where subsequent lines get written so
    each scenario still gets its own <out>/ota_abort_<scenario>_<ts>.log.
    The old file handle is left open rather than closed here to avoid a
    write-to-closed-file race against the session's background reader
    thread; it is small and short-lived enough not to matter for a bench run."""
    sess.log_fh = open(log_path, "a", buffering=1, encoding="utf-8")


# --- the run -----------------------------------------------------------------


def run(args: argparse.Namespace, opener: Callable[[str], Any] = otr.real_opener,
        get: webflash.GetFn = webflash.http_get, poster: webflash.PostFn = webflash.upload_multipart,
        controlled_poster: ControlledPostFn = webflash.upload_multipart_controlled) -> int:
    target = args.host
    fw = args.fw if args.fw is not None else webflash.resolve_firmware(args.env, None)
    if not fw.is_file():
        print(f"error: firmware not found: {fw}")
        return 2

    out_dir = Path(args.out) if args.out else default_out_dir()
    out_dir.mkdir(parents=True, exist_ok=True)

    scenarios = ALL_SCENARIO_SEQUENCE if args.all else [args.scenario]

    first_ts = time.strftime("%Y%m%d-%H%M%S")
    sess = otr.SerialSession(args.port, out_dir / f"{RUN_PREFIX}_{scenarios[0]}_{first_ts}.log", opener=opener)
    sess.start()
    if sess.error:
        print(f"error: serial open failed: {sess.error}")
        sess.close()  # release the log file handle even though the port never opened
        return 2

    t_open = time.time()
    if not sess.wait_for(otr.RE_CLIENT_STARTED, args.boot_wait_s, since_ts=t_open):
        print(f"  ... did not see CLIENT STARTED after opening the port within "
              f"{args.boot_wait_s:.0f}s -- continuing anyway")
    time.sleep(1.0)

    overall_pass = True
    ran: list[tuple[str, ScenarioResult]] = []
    harness_error = False

    for i, name in enumerate(scenarios):
        ts = time.strftime("%Y%m%d-%H%M%S")
        if i > 0:
            _switch_log(sess, out_dir / f"{RUN_PREFIX}_{name}_{ts}.log")

        print(f"Waiting for the node to be back in the app before '{name}' ...")
        if not wait_app_back(target, get, args.between_wait_s):
            print(f"error: node never came back to the app before scenario '{name}' "
                  f"within {args.between_wait_s:.0f}s")
            harness_error = True
            break

        print(f"--- scenario: {name} ---")
        result = SCENARIO_FNS[name](target, sess, fw, args, get, poster, controlled_poster)
        write_scenario_result(out_dir, name, ts, result)
        print(render_scenario(result))
        ran.append((name, result))
        if not result.passed:
            overall_pass = False

    sess.close()

    if len(scenarios) > 1 and ran:
        print("\n=== overall ===")
        for name, result in ran:
            print(f"  {name:<12} {'PASS' if result.passed else 'FAIL'}")

    if harness_error:
        return 2
    return 0 if overall_pass else 1


# --- CLI -----------------------------------------------------------------


def build_parser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", required=True, help="node hostname or IP")
    ap.add_argument("--port", required=True, help="serial port, e.g. /dev/cu.usbserial-XXXX")
    ap.add_argument("--env", required=True,
                     help="PlatformIO env, e.g. heltec_wifi_lora_32_V3, ttgo_tbeam, t_deck_plus")
    ap.add_argument("--fw", "--bin", dest="fw", type=Path, default=None,
                     help="firmware.bin path (default .pio/build/<env>/firmware.bin)")
    ap.add_argument("--scenario", choices=sorted(SCENARIO_FNS), default=None,
                     help="run a single scenario")
    ap.add_argument("--all", action="store_true",
                     help="run control, kill50, stall, doublestart, cancel, control")
    ap.add_argument("--out", default=None, help=f"default: {default_out_dir()}")
    ap.add_argument("--force", action="store_true", help="skip webflash's hardware check")
    ap.add_argument("--boot-wait-s", type=float, default=25.0,
                     help="how long to wait for CLIENT STARTED after opening the port")
    ap.add_argument("--safeboot-poll-s", type=float, default=webflash.SAFEBOOT_POLL_S,
                     help="how long to wait for the safeboot web server to come up")
    ap.add_argument("--reboot-poll-s", type=float, default=webflash.REBOOT_POLL_S,
                     help="how long to wait for the app to answer HTTP again after a good upload")
    ap.add_argument("--poll-interval", type=float, default=3.0,
                     help="webflash.flash()'s HTTP poll interval (control scenario only)")
    ap.add_argument("--serial-timeout-s", type=float, default=20.0,
                     help="per-marker serial wait timeout for markers expected promptly")
    ap.add_argument("--stall-seconds", type=float, default=45.0,
                     help="how long the stall/cancel-during-upload scenarios pause the upload "
                          "(> the firmware's 30s stall watchdog)")
    ap.add_argument("--fallback-total-wait-s", type=float, default=240.0,
                     help="kill50/stall: total time budget from the abort trigger to the app "
                          "rebooting via the 180s fallback window")
    ap.add_argument("--post-abort-settle-s", type=float, default=10.0,
                     help="kill50/stall: how long to wait after the abort before confirming the "
                          "node is still holding in safeboot (/update still 200) -- app_valid true path")
    ap.add_argument("--app-invalid-settle-s", type=float, default=60.0,
                     help="kill50/stall: how long to hold after the abort confirming no reboot "
                          "loop (/update still 200, no OTA UDATE started banner, no "
                          "fallback;reason;timeout) before the recovery upload -- app_valid false path")
    ap.add_argument("--cancel-wait-s", type=float, default=60.0,
                     help="cancel part A: time budget for the fallback + app boot")
    ap.add_argument("--doublestart-settle-s", type=float, default=1.0,
                     help="pause after starting a paused/abandoned upload before the next HTTP call, "
                          "so the fraction has actually gone out")
    ap.add_argument("--doublestart-abandon-s", type=float, default=9999.0,
                     help="stall_seconds for the doublestart scenario's abandoned first session "
                          "-- long enough it never resumes on its own within the scenario")
    ap.add_argument("--between-wait-s", type=float, default=240.0,
                     help="max wait for the node to be back in the app between scenarios")
    return ap


def main(argv: Optional[list[str]] = None) -> int:
    args = build_parser().parse_args(argv)
    if not args.all and not args.scenario:
        print("error: pass --scenario <name> or --all")
        return 2
    return run(args)


if __name__ == "__main__":
    sys.exit(main())
