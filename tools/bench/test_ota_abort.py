#!/usr/bin/env python3
"""Tests for tools/bench/ota_abort.py (TM-49 abort bench) and the webflash.py
/ota/state enrichment it depends on.

    python3 -m unittest tools/bench/test_ota_abort.py

No hardware and no network for the scenario tests: a fake serial port
(`FakeSerial`, only ever pushed to -- ota_abort.py never sends serial
commands, unlike ota_regression.py's fake) and a fake HTTP node (`FakeNode`,
a small state machine standing in for docs/safeboot-ota-contract.md's
/callfunction, /update, /ota/start, /ota/upload (both the plain `poster` and
the raw-socket `controlled_poster` slot), /ota/state, /ota/cancel and /
endpoints) drive every scenario's assertion logic directly. Each scenario
gets one test that must PASS against a clean fake node and one that must
FAIL against a fake node with a deliberately injected firmware bug (e.g.
kill50 against a node that wrongly prints ota;end;result;success after the
abort -- exactly the case the brief calls out).

`upload_multipart_controlled` is separately exercised against a real local
`http.server` thread (no fake, no hardware) since that function talks to a
raw socket rather than going through the injectable get/poster.
"""

import argparse
import contextlib
import http.server
import io
import json
import os
import sys
import tempfile
import threading
import time
import unittest
import urllib.error
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ota_abort as oab  # noqa: E402
import ota_regression as otr  # noqa: E402
import webflash  # noqa: E402  (path already added by ota_regression's own sys.path.insert)


# --- fake serial -------------------------------------------------------------


class FakeSerial:
    """Fake pyserial port for ota_abort.py: it is only ever read from and
    pushed to (the fake HTTP node below pushes lines the way the real
    firmware prints boot/abort/fallback markers on its own); ota_abort.py
    never sends bench commands over serial the way ota_regression.py's fake
    does."""

    def __init__(self, boot_lines: str = "CLIENT STARTED\n"):
        self._out = bytearray(boot_lines.encode())
        self.closed = False

    def read(self, n: int = 4096) -> bytes:
        time.sleep(0.01)
        if not self._out:
            return b""
        chunk = bytes(self._out[:n])
        del self._out[:n]
        return chunk

    def write(self, data) -> int:
        return len(data) if isinstance(data, (bytes, bytearray)) else len(data.encode())

    def flush(self) -> None:
        pass

    def close(self) -> None:
        self.closed = True

    def push(self, text: str) -> None:
        self._out += text.encode()


# --- fake HTTP node ------------------------------------------------------------


class FakeNode:
    """Stands in for the node's HTTP server + firmware behaviour across
    webflash's `get`/`poster` and ota_abort's `controlled_poster` slot,
    wired to a FakeSerial so aborts/fallbacks/reboots push the matching
    serial markers the way the real safeboot would. Constructor knobs after
    `hardware` are all bug injectors for the "must FAIL" test variants --
    every one defaults to the correct (contract-following) behaviour."""

    def __init__(self, serial: FakeSerial, *, hardware: str = "HELTEC_V3",
                 kill_reason: str = "incomplete_upload",
                 stall_watchdog_fire_s: float = 0.2,
                 fallback_delay_s: float = 0.15,
                 reboot_delay_s: float = 0.1,
                 suppress_fallback: bool = False,
                 print_verify_ok: bool = True,
                 print_end_success: bool = True,
                 kill_bug_print_success: bool = False,
                 stall_wrong_reason: bool = False,
                 cancel_bug_allow_during_upload: bool = False,
                 doublestart_bug_no_stale_marker: bool = False,
                 report_app_valid: bool = True,
                 reboot_loop_bug_when_invalid: bool = False):
        self.serial = serial
        self.hardware = hardware
        self.kill_reason = kill_reason
        self.stall_watchdog_fire_s = stall_watchdog_fire_s
        self.fallback_delay_s = fallback_delay_s
        self.reboot_delay_s = reboot_delay_s
        self.suppress_fallback = suppress_fallback
        self.print_verify_ok = print_verify_ok
        self.print_end_success = print_end_success
        self.kill_bug_print_success = kill_bug_print_success
        self.stall_wrong_reason = stall_wrong_reason
        self.cancel_bug_allow_during_upload = cancel_bug_allow_during_upload
        self.doublestart_bug_no_stale_marker = doublestart_bug_no_stale_marker
        # Single app slot (docs/safeboot-ota-contract.md): `report_app_valid`
        # False emulates an old safeboot image that predates the field
        # entirely (omitted from /ota/state, not just false).
        # `reboot_loop_bug_when_invalid` emulates the pre-fix firmware
        # defect the 2026-09-13 kill50 hardware capture caught: it still
        # runs the old fallback/reboot cycle after the app went invalid,
        # looping back into safeboot instead of holding still.
        self.report_app_valid = report_app_valid
        self.reboot_loop_bug_when_invalid = reboot_loop_bug_when_invalid

        self.lock = threading.Lock()
        self.in_app = True
        self.gen = 0
        self.epoch = 0  # bumped on every fresh /callfunction; guards a
                         # delayed reboot/fallback callback from a PREVIOUS
                         # safeboot session against firing late into a new
                         # one under thread-scheduling jitter (cancel's
                         # part A -> part B back-to-back safeboot entries)
        self.upload_active = False
        self.state = "idle"
        self.reason = ""
        self.received = 0
        self.total = 0
        # True until any chunk is accepted by an aborted session (single
        # app slot); true again only after a completed upload.
        self.app_valid = True

    # -- html fragments, just enough for webflash.node_info()'s regexes --

    def _app_html(self) -> str:
        return (f"<tr><td>Hardware</td><td>{self.hardware}</td></tr>"
                f"Meshcom 4.35t<br>(build: Sep 13 2026 12:00:00)")

    def _safeboot_html(self) -> str:
        return "<title>MeshCom OTA</title><h1>Safeboot OTA</h1>"

    # -- GET dispatcher (webflash.GetFn) --

    def get(self, url: str, timeout: float = 5.0):
        if url.endswith("/callfunction/?otaupdate"):
            with self.lock:
                self.in_app = False
                self.epoch += 1
                self.upload_active = False
                self.state = "idle"
                self.reason = ""
            raise OSError("connection reset (node rebooting into safeboot)")
        if url.endswith("/update"):
            with self.lock:
                in_app = self.in_app
            return (404, "not found") if in_app else (200, "safeboot page")
        if "/ota/start" in url:
            return self._handle_ota_start()
        if url.endswith("/ota/state"):
            with self.lock:
                # -1 while an upload is in progress, and -1 while app_valid
                # is false (no fallback possible) -- docs/safeboot-ota-
                # contract.md.
                fallback_ms = -1 if (self.upload_active or not self.app_valid) else 150000
                payload = dict(state=self.state, reason=self.reason, generation=self.gen,
                                received=self.received, total=self.total,
                                image_valid=self.state == "done", fallback_in_ms=fallback_ms,
                                uptime_ms=1000)
                if self.report_app_valid:
                    payload["app_valid"] = self.app_valid
            return 200, json.dumps(payload)
        if url.endswith("/ota/cancel"):
            return self._handle_cancel()
        if url.endswith("/"):
            with self.lock:
                in_app = self.in_app
            return (200, self._app_html()) if in_app else (200, self._safeboot_html())
        raise OSError(f"unexpected URL in test: {url}")

    def _handle_ota_start(self):
        with self.lock:
            if self.in_app:
                return 404, "not in safeboot"
            was_active = self.upload_active
            self.gen += 1
            self.upload_active = False
            self.state = "receiving"
            self.reason = ""
            self.received = 0
            self.total = 0
            if was_active:
                self.app_valid = False  # the abandoned session already wrote
                                         # into the single app slot
        if was_active and not self.doublestart_bug_no_stale_marker:
            self.serial.push("[SAFEBOOT];ota;abort;reason;stale_session\n")
        return 200, "OK"

    def _handle_cancel(self):
        with self.lock:
            if self.in_app:
                return 404, "not in safeboot"
            if self.upload_active and not self.cancel_bug_allow_during_upload:
                return 400, "cancel refused: upload in progress"
            if not self.app_valid:
                return 409, "cancel refused: app_invalid"
            my_epoch = self.epoch
        self.serial.push("[SAFEBOOT];fallback;reason;cancel\n")
        threading.Thread(target=self._reboot_to_app_after, args=(self.reboot_delay_s, my_epoch),
                          daemon=True).start()
        return 200, "OK"

    # -- poster (webflash.PostFn): the plain, uninterrupted upload --

    def poster(self, url: str, path: Path):
        with self.lock:
            my_gen = self.gen
        return self._do_full_upload(path, my_gen)

    def _do_full_upload(self, path: Path, my_gen: int):
        total = path.stat().st_size
        with self.lock:
            if self.gen != my_gen:
                return 400, "stale session"
            self.state = "verifying"
            my_epoch = self.epoch
        if self.print_verify_ok:
            self.serial.push("[SAFEBOOT];ota;verify;result;ok\n")
        if self.print_end_success:
            self.serial.push("[SAFEBOOT];ota;end;result;success\n")
        with self.lock:
            self.state = "done"
            self.received = total
            self.total = total
            self.app_valid = True
        threading.Thread(target=self._reboot_to_app_after, args=(self.reboot_delay_s, my_epoch),
                          daemon=True).start()
        return 200, "Update Success! Rebooting..."

    def _reboot_to_app_after(self, delay: float, my_epoch: int):
        time.sleep(delay)
        with self.lock:
            if self.epoch != my_epoch:
                return  # a new safeboot session started before this fired
        self.serial.push("[BOOT];ready;ms;15000;ip;1\n")
        with self.lock:
            self.in_app = True

    # -- controlled_poster (webflash.upload_multipart_controlled's shape) --

    def controlled_poster(self, url: str, path: Path, *, stop_after_fraction=None,
                           stall_after_fraction=None, stall_seconds: float = 0.0, chunk: int = 4096):
        total = path.stat().st_size
        with self.lock:
            my_gen = self.gen
            self.upload_active = True
        try:
            if stop_after_fraction is not None:
                received = int(total * stop_after_fraction)
                self._abort_if_current(my_gen, self.kill_reason, received, total)
                return 0, ""
            if stall_after_fraction is not None:
                received = int(total * stall_after_fraction)
                time.sleep(min(stall_seconds, self.stall_watchdog_fire_s))
                reason = "incomplete_upload" if self.stall_wrong_reason else "stalled"
                self._abort_if_current(my_gen, reason, received, total)
                remaining = stall_seconds - self.stall_watchdog_fire_s
                if remaining > 0:
                    time.sleep(remaining)
                return 0, ""
            return self._do_full_upload(path, my_gen)
        finally:
            with self.lock:
                if self.gen == my_gen:
                    self.upload_active = False

    def _abort_if_current(self, my_gen: int, reason: str, received: int, total: int) -> bool:
        with self.lock:
            if self.gen != my_gen:
                return False  # already superseded (doublestart) -- that
                               # path pushed its own stale_session abort
            self.state = "aborted"
            self.reason = reason
            self.received = received
            self.total = total
            self.app_valid = False  # single app slot: a chunk was written
            my_epoch = self.epoch
        self.serial.push(f"[SAFEBOOT];ota;abort;reason;{reason}\n")
        if self.kill_bug_print_success:
            self.serial.push("[SAFEBOOT];ota;end;result;success\n")
        self.serial.push("[SAFEBOOT];app;image;invalid;rc;-1\n")
        if self.reboot_loop_bug_when_invalid:
            threading.Thread(target=self._reboot_loop_bug_after, args=(self.fallback_delay_s, my_epoch),
                              daemon=True).start()
        elif not self.suppress_fallback and self.app_valid:
            # correct firmware: app_valid is already false above, so this
            # never fires -- the fallback timer only runs while the app is
            # valid (docs/safeboot-ota-contract.md "Single app slot").
            threading.Thread(target=self._fallback_after, args=(self.fallback_delay_s, my_epoch),
                              daemon=True).start()
        return True

    def _fallback_after(self, delay: float, my_epoch: int):
        time.sleep(delay)
        with self.lock:
            if self.epoch != my_epoch:
                return  # a new safeboot session started before this fired
        self.serial.push("[SAFEBOOT];fallback;reason;timeout\n")
        time.sleep(0.02)
        with self.lock:
            if self.epoch != my_epoch:
                return
        self.serial.push("[BOOT];ready;ms;15000;ip;1\n")
        with self.lock:
            self.in_app = True

    def _reboot_loop_bug_after(self, delay: float, my_epoch: int):
        """Firmware defect the 2026-09-13 kill50 hardware capture caught
        (tools/bench/runs/ota_abort_kill50_20260913-154513.log): the
        fallback timer fires and the node tries to reboot to the app even
        though the single app slot is invalid, so it loops back into
        safeboot (prints the boot banner again) instead of holding still or
        actually recovering."""
        time.sleep(delay)
        with self.lock:
            if self.epoch != my_epoch:
                return
        self.serial.push("[SAFEBOOT];fallback;reason;timeout\n")
        time.sleep(0.02)
        with self.lock:
            if self.epoch != my_epoch:
                return
        self.serial.push("OTA UDATE started\n")


def make_args(**overrides) -> argparse.Namespace:
    ns = argparse.Namespace(
        host="fixture", port="/dev/fake", env="heltec_wifi_lora_32_V3", fw=None,
        scenario=None, all=False, out=None, force=False,
        boot_wait_s=0.2, safeboot_poll_s=1.0, reboot_poll_s=1.0, poll_interval=0.02,
        serial_timeout_s=1.0, stall_seconds=0.5, fallback_total_wait_s=3.0,
        post_abort_settle_s=0.1, app_invalid_settle_s=0.3, cancel_wait_s=2.0,
        doublestart_settle_s=0.1, doublestart_abandon_s=1.5, between_wait_s=3.0,
    )
    for k, v in overrides.items():
        setattr(ns, k, v)
    return ns


def assertion_map(result: "oab.ScenarioResult") -> dict:
    return {a.name: a.passed for a in result.assertions}


class ScenarioTestBase(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.fw = Path(self.tmp.name) / "firmware.bin"
        self.fw.write_bytes(b"\x00\x01\x02\x03" * 4096)

    def _session(self, serial: FakeSerial) -> "otr.SerialSession":
        log_path = Path(self.tmp.name) / "serial.log"
        sess = otr.SerialSession("/dev/fake", log_path, opener=lambda _port: serial)
        sess.start()
        self.addCleanup(sess.close)
        return sess


# --- control scenario ----------------------------------------------------------


class ControlScenarioTest(ScenarioTestBase):
    def test_pass(self):
        serial = FakeSerial()
        node = FakeNode(serial)
        sess = self._session(serial)
        args = make_args()
        result = oab.scenario_control("fixture", sess, self.fw, args, node.get, node.poster,
                                       node.controlled_poster)
        self.assertTrue(result.passed, result.assertions)

    def test_fail_no_verify_marker(self):
        serial = FakeSerial()
        node = FakeNode(serial, print_verify_ok=False)
        sess = self._session(serial)
        args = make_args()
        result = oab.scenario_control("fixture", sess, self.fw, args, node.get, node.poster,
                                       node.controlled_poster)
        self.assertFalse(result.passed)
        self.assertFalse(assertion_map(result)["serial ota;verify;result;ok"])


# --- kill50 scenario -------------------------------------------------------------


class Kill50ScenarioTest(ScenarioTestBase):
    def test_pass(self):
        """Single app slot (docs/safeboot-ota-contract.md, bench finding
        2026-09-13): a kill leaves app_valid false, so the node must hold
        still (no fallback, no reboot loop) and recover through a full
        upload on the same session -- that recovery is part of PASS."""
        serial = FakeSerial()
        node = FakeNode(serial)
        sess = self._session(serial)
        args = make_args()
        result = oab.scenario_kill50("fixture", sess, self.fw, args, node.get, node.poster,
                                      node.controlled_poster)
        self.assertTrue(result.passed, result.assertions)
        self.assertEqual(result.extra.get("app_valid"), False)
        self.assertIn("bytes_before_kill", result.extra)
        self.assertIn("abort_to_boot_s", result.extra)

    def test_fail_wrongly_prints_success(self):
        """The exact case called out in the brief: a fake that wrongly
        prints end;result;success after an abort must FAIL the scenario."""
        serial = FakeSerial()
        node = FakeNode(serial, kill_bug_print_success=True)
        sess = self._session(serial)
        args = make_args()
        result = oab.scenario_kill50("fixture", sess, self.fw, args, node.get, node.poster,
                                      node.controlled_poster)
        self.assertFalse(result.passed)
        self.assertFalse(assertion_map(result)["no serial ota;end;result;success"])

    def test_fail_reboot_loop_when_invalid(self):
        """Real hardware finding, 2026-09-13
        (tools/bench/runs/ota_abort_kill50_20260913-154513.log): a node that
        still runs the old fallback/reboot cycle after app_valid went false
        loops back into safeboot instead of holding still -- the "no reboot
        loop" assertions must catch it."""
        serial = FakeSerial()
        node = FakeNode(serial, reboot_loop_bug_when_invalid=True, fallback_delay_s=0.05)
        sess = self._session(serial)
        args = make_args(app_invalid_settle_s=0.3)
        result = oab.scenario_kill50("fixture", sess, self.fw, args, node.get, node.poster,
                                      node.controlled_poster)
        self.assertFalse(result.passed)
        names = assertion_map(result)
        self.assertFalse(names["no OTA UDATE started boot banner in that window (no reboot loop)"])
        self.assertFalse(names["no serial fallback;reason;timeout while app invalid (no reboot loop)"])

    def test_fail_app_valid_missing(self):
        """An old safeboot image that predates `app_valid` in /ota/state
        takes the legacy fallback code path, but must fail the dedicated
        "app_valid reported" assertion so it stands out in the report."""
        serial = FakeSerial()
        node = FakeNode(serial, report_app_valid=False, fallback_delay_s=0.05)
        sess = self._session(serial)
        args = make_args(fallback_total_wait_s=2.0, post_abort_settle_s=0.1)
        result = oab.scenario_kill50("fixture", sess, self.fw, args, node.get, node.poster,
                                      node.controlled_poster)
        self.assertFalse(result.passed)
        self.assertFalse(assertion_map(result)["app_valid reported"])
        self.assertEqual(result.extra.get("app_valid"), "missing")


# --- stall scenario --------------------------------------------------------------


class StallScenarioTest(ScenarioTestBase):
    def test_pass(self):
        serial = FakeSerial()
        node = FakeNode(serial, stall_watchdog_fire_s=0.2, fallback_delay_s=1.0)
        sess = self._session(serial)
        args = make_args(stall_seconds=0.5)
        result = oab.scenario_stall("fixture", sess, self.fw, args, node.get, node.poster,
                                     node.controlled_poster)
        self.assertTrue(result.passed, result.assertions)

    def test_fail_wrong_reason(self):
        serial = FakeSerial()
        node = FakeNode(serial, stall_watchdog_fire_s=0.2, fallback_delay_s=1.0, stall_wrong_reason=True)
        sess = self._session(serial)
        args = make_args(stall_seconds=0.5)
        result = oab.scenario_stall("fixture", sess, self.fw, args, node.get, node.poster,
                                     node.controlled_poster)
        self.assertFalse(result.passed)
        names = assertion_map(result)
        self.assertFalse(names["/ota/state aborted with expected reason"])


# --- doublestart scenario ---------------------------------------------------------


class DoublestartScenarioTest(ScenarioTestBase):
    def test_pass(self):
        serial = FakeSerial()
        node = FakeNode(serial, stall_watchdog_fire_s=3.0)
        sess = self._session(serial)
        args = make_args(doublestart_settle_s=0.1, doublestart_abandon_s=1.5)
        result = oab.scenario_doublestart("fixture", sess, self.fw, args, node.get, node.poster,
                                           node.controlled_poster)
        self.assertTrue(result.passed, result.assertions)

    def test_fail_no_stale_marker(self):
        serial = FakeSerial()
        node = FakeNode(serial, stall_watchdog_fire_s=3.0, doublestart_bug_no_stale_marker=True)
        sess = self._session(serial)
        args = make_args(doublestart_settle_s=0.1, doublestart_abandon_s=1.5)
        result = oab.scenario_doublestart("fixture", sess, self.fw, args, node.get, node.poster,
                                           node.controlled_poster)
        self.assertFalse(result.passed)
        self.assertFalse(assertion_map(result)["serial ota;abort;reason;stale_session"])


# --- cancel scenario ---------------------------------------------------------------


class CancelScenarioTest(ScenarioTestBase):
    def test_pass(self):
        serial = FakeSerial()
        node = FakeNode(serial, stall_watchdog_fire_s=2.0)
        sess = self._session(serial)
        args = make_args(stall_seconds=1.0, cancel_wait_s=2.0, doublestart_settle_s=0.1)
        result = oab.scenario_cancel("fixture", sess, self.fw, args, node.get, node.poster,
                                      node.controlled_poster)
        self.assertTrue(result.passed, result.assertions)

    def test_fail_cancel_allowed_during_upload(self):
        serial = FakeSerial()
        node = FakeNode(serial, stall_watchdog_fire_s=2.0, cancel_bug_allow_during_upload=True)
        sess = self._session(serial)
        args = make_args(stall_seconds=1.0, cancel_wait_s=2.0, doublestart_settle_s=0.1)
        result = oab.scenario_cancel("fixture", sess, self.fw, args, node.get, node.poster,
                                      node.controlled_poster)
        self.assertFalse(result.passed)
        self.assertFalse(assertion_map(result)["/ota/cancel 400 while an upload is in progress"])


# --- run() orchestration ----------------------------------------------------------


class RunOrchestrationTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.fw = Path(self.tmp.name) / "firmware.bin"
        self.fw.write_bytes(b"\x00\x01" * 2048)

    def test_run_single_scenario_pass_writes_log_and_json(self):
        serial = FakeSerial()
        node = FakeNode(serial)
        out_dir = Path(self.tmp.name) / "runs"
        args = make_args(scenario="control", fw=self.fw, out=str(out_dir))

        rc = oab.run(args, opener=lambda _port: serial, get=node.get, poster=node.poster,
                     controlled_poster=node.controlled_poster)

        self.assertEqual(rc, 0)
        logs = sorted(out_dir.glob("ota_abort_control_*.log"))
        jsons = sorted(out_dir.glob("ota_abort_control_*.json"))
        self.assertEqual(len(logs), 1)
        self.assertEqual(len(jsons), 1)
        payload = json.loads(jsons[0].read_text(encoding="utf-8"))
        self.assertEqual(payload["verdict"], "PASS")
        self.assertIn("CLIENT STARTED", logs[0].read_text(encoding="utf-8"))

    def test_run_scenario_fail_exit_code(self):
        serial = FakeSerial()
        node = FakeNode(serial, print_verify_ok=False)
        out_dir = Path(self.tmp.name) / "runs"
        args = make_args(scenario="control", fw=self.fw, out=str(out_dir))

        rc = oab.run(args, opener=lambda _port: serial, get=node.get, poster=node.poster,
                     controlled_poster=node.controlled_poster)

        self.assertEqual(rc, 1)

    def test_run_serial_open_failure_is_harness_error(self):
        def bad_opener(_port):
            raise RuntimeError("port busy")

        out_dir = Path(self.tmp.name) / "runs"
        args = make_args(scenario="control", fw=self.fw, out=str(out_dir))
        rc = oab.run(args, opener=bad_opener, get=lambda *a, **k: (200, ""),
                     poster=lambda *a, **k: (200, ""), controlled_poster=lambda *a, **k: (0, ""))
        self.assertEqual(rc, 2)

    def test_run_missing_firmware_is_harness_error(self):
        out_dir = Path(self.tmp.name) / "runs"
        args = make_args(scenario="control", fw=Path(self.tmp.name) / "nope.bin", out=str(out_dir))
        rc = oab.run(args)
        self.assertEqual(rc, 2)

    def test_main_requires_scenario_or_all(self):
        rc = oab.main(["--host", "fixture", "--port", "/dev/fake", "--env", "heltec_wifi_lora_32_V3"])
        self.assertEqual(rc, 2)


# --- upload_multipart_controlled against a real local http.server ----------------


class UploadMultipartControlledTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.fw = Path(self.tmp.name) / "firmware.bin"
        self.fw.write_bytes(b"X" * 200_000)

        self.received_len = None
        self.elapsed = None
        self.received_event = threading.Event()

        outer = self

        class Handler(http.server.BaseHTTPRequestHandler):
            def do_POST(self):
                length = int(self.headers.get("Content-Length", 0))
                t0 = time.monotonic()
                data = self.rfile.read(length)
                outer.elapsed = time.monotonic() - t0
                outer.received_len = len(data)
                outer.received_event.set()
                self.send_response(200)
                self.send_header("Content-Length", "2")
                self.end_headers()
                self.wfile.write(b"ok")

            def log_message(self, *a):  # noqa: D401 - silence request logging
                pass

        self.server = http.server.HTTPServer(("127.0.0.1", 0), Handler)
        self.port = self.server.server_address[1]
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.addCleanup(self.server.shutdown)

    def test_kill_at_half_status_zero_and_truncated_body(self):
        fw_size = self.fw.stat().st_size
        status, body = webflash.upload_multipart_controlled(
            f"http://127.0.0.1:{self.port}/ota/upload", self.fw,
            stop_after_fraction=0.5, stall_after_fraction=None, stall_seconds=0.0)

        self.assertEqual(status, 0)
        self.assertEqual(body, "")
        self.assertTrue(self.received_event.wait(timeout=5.0),
                         "server never saw the truncated request")
        self.assertLess(self.received_len, fw_size)

    def test_stall_pauses_then_completes(self):
        status, body = webflash.upload_multipart_controlled(
            f"http://127.0.0.1:{self.port}/ota/upload", self.fw,
            stop_after_fraction=None, stall_after_fraction=0.5, stall_seconds=0.5)

        self.assertEqual(status, 200)
        self.assertEqual(body, "ok")
        self.assertTrue(self.received_event.wait(timeout=5.0))
        self.assertGreaterEqual(self.elapsed, 0.4, "server-side read should show the stall pause")

    def test_plain_upload_completes(self):
        fw_size = self.fw.stat().st_size
        status, body = webflash.upload_multipart_controlled(
            f"http://127.0.0.1:{self.port}/ota/upload", self.fw,
            stop_after_fraction=None, stall_after_fraction=None, stall_seconds=0.0)
        self.assertEqual(status, 200)
        self.assertEqual(body, "ok")
        self.assertTrue(self.received_event.wait(timeout=5.0))
        # full body: fw bytes plus the (small, fixed-format) multipart head/tail
        self.assertGreater(self.received_len, fw_size)
        self.assertLess(self.received_len - fw_size, 300)


# --- webflash.py's /ota/state enrichment ------------------------------------------


class WebflashOtaStateTest(unittest.TestCase):
    def test_fetch_ota_state_parses_json(self):
        def fake_get(url, timeout=5.0):
            self.assertTrue(url.endswith("/ota/state"))
            return 200, json.dumps(dict(state="aborted", reason="incomplete_upload",
                                         received=1048576, total=2182465, fallback_in_ms=151000))

        state = webflash.fetch_ota_state("fixture", get=fake_get)
        self.assertEqual(state["state"], "aborted")
        self.assertEqual(state["reason"], "incomplete_upload")

    def test_fetch_ota_state_tolerates_404(self):
        def fake_get(url, timeout=5.0):
            raise urllib.error.HTTPError(url, 404, "not found", {}, None)

        self.assertIsNone(webflash.fetch_ota_state("fixture", get=fake_get))

    def test_fetch_ota_state_tolerates_non_200_tuple(self):
        def fake_get(url, timeout=5.0):
            return 404, "not found"

        self.assertIsNone(webflash.fetch_ota_state("fixture", get=fake_get))

    def test_fetch_ota_state_tolerates_bad_json(self):
        def fake_get(url, timeout=5.0):
            return 200, "not json"

        self.assertIsNone(webflash.fetch_ota_state("fixture", get=fake_get))

    def test_ota_result_node_state_line(self):
        result = webflash.OtaResult(ok=False, stage="upload_failed", fw_path="fw.bin", fw_size=1,
                                     md5="x", error="upload failed", state="aborted",
                                     reason="incomplete_upload", received=1048576, total=2182465,
                                     fallback_in_ms=151000)
        self.assertEqual(result.node_state_line(),
                          "node reports: aborted (incomplete_upload), 1048576/2182465 bytes, "
                          "fallback in 151 s")

    def test_ota_result_node_state_line_none_when_no_state(self):
        result = webflash.OtaResult(ok=False, stage="not_reachable", fw_path="fw.bin", fw_size=1, md5="x")
        self.assertIsNone(result.node_state_line())

    def test_flash_populates_state_on_upload_failure(self):
        def fake_get(url, timeout=5.0):
            if url.endswith("/update"):
                return 200, "safeboot"
            if "/ota/start" in url:
                return 200, "OK"
            if url.endswith("/ota/state"):
                return 200, json.dumps(dict(state="aborted", reason="incomplete_upload",
                                             received=500, total=1000, fallback_in_ms=151000))
            if url.endswith("/"):
                return 200, "<tr><td>Hardware</td><td>HELTEC_V3</td></tr>Meshcom 4.35t<br>(build: x)"
            raise OSError(f"unexpected {url}")

        def fake_poster(url, path):
            return 400, "Upload incomplete: image never verified"

        with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as tmp:
            tmp.write(b"\x00" * 100)
            fw_path = Path(tmp.name)
        try:
            result = webflash.flash("fixture", fw_path, expect_hw="HELTEC_V3", get=fake_get,
                                     poster=fake_poster, poll_interval=0.0)
        finally:
            fw_path.unlink(missing_ok=True)

        self.assertFalse(result.ok)
        self.assertEqual(result.stage, "upload_failed")
        self.assertEqual(result.state, "aborted")
        self.assertEqual(result.reason, "incomplete_upload")
        self.assertEqual(result.received, 500)
        self.assertEqual(result.total, 1000)
        self.assertEqual(result.fallback_in_ms, 151000)
        self.assertIsNone(result.app_valid)  # this fake node predates the field
        self.assertIn("node reports: aborted (incomplete_upload), 500/1000 bytes, fallback in 151 s",
                      result.line())

    def test_flash_populates_app_valid_false_on_upload_failure(self):
        """Single app slot (docs/safeboot-ota-contract.md): app_valid false
        must come through on the OtaResult so the CLI can print the
        recovery note."""
        def fake_get(url, timeout=5.0):
            if url.endswith("/update"):
                return 200, "safeboot"
            if "/ota/start" in url:
                return 200, "OK"
            if url.endswith("/ota/state"):
                return 200, json.dumps(dict(state="aborted", reason="incomplete_upload",
                                             received=500, total=1000, fallback_in_ms=-1,
                                             app_valid=False))
            if url.endswith("/"):
                return 200, "<tr><td>Hardware</td><td>HELTEC_V3</td></tr>Meshcom 4.35t<br>(build: x)"
            raise OSError(f"unexpected {url}")

        def fake_poster(url, path):
            return 400, "Upload incomplete: image never verified"

        with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as tmp:
            tmp.write(b"\x00" * 100)
            fw_path = Path(tmp.name)
        try:
            result = webflash.flash("fixture", fw_path, expect_hw="HELTEC_V3", get=fake_get,
                                     poster=fake_poster, poll_interval=0.0)
        finally:
            fw_path.unlink(missing_ok=True)

        self.assertFalse(result.ok)
        self.assertFalse(result.app_valid)


class WebflashCliAppInvalidNoteTest(unittest.TestCase):
    """CLI-level check (requirement 4): when the node reports app_valid
    false after a failed upload, `webflash.main()` must print the recovery
    note verbatim."""

    def test_main_prints_app_invalid_note_on_failure(self):
        fake_result = webflash.OtaResult(
            ok=False, stage="upload_failed", fw_path="fw.bin", fw_size=1, md5="x",
            error="upload failed", state="aborted", reason="incomplete_upload", app_valid=False)

        with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as tmp:
            tmp.write(b"\x00" * 10)
            fw_path = Path(tmp.name)

        orig_flash = webflash.flash
        webflash.flash = lambda *a, **k: fake_result
        buf = io.StringIO()
        try:
            with contextlib.redirect_stdout(buf):
                rc = webflash.main(["--bin", str(fw_path), "--force"])
        finally:
            webflash.flash = orig_flash
            fw_path.unlink(missing_ok=True)

        self.assertEqual(rc, 1)
        self.assertIn(webflash.APP_INVALID_NOTE, buf.getvalue())

    def test_main_omits_app_invalid_note_when_app_valid_true(self):
        fake_result = webflash.OtaResult(
            ok=False, stage="upload_failed", fw_path="fw.bin", fw_size=1, md5="x",
            error="upload failed", state="aborted", reason="stalled", app_valid=True)

        with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as tmp:
            tmp.write(b"\x00" * 10)
            fw_path = Path(tmp.name)

        orig_flash = webflash.flash
        webflash.flash = lambda *a, **k: fake_result
        buf = io.StringIO()
        try:
            with contextlib.redirect_stdout(buf):
                rc = webflash.main(["--bin", str(fw_path), "--force"])
        finally:
            webflash.flash = orig_flash
            fw_path.unlink(missing_ok=True)

        self.assertEqual(rc, 1)
        self.assertNotIn(webflash.APP_INVALID_NOTE, buf.getvalue())


if __name__ == "__main__":
    unittest.main()
