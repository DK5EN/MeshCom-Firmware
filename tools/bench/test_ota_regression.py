#!/usr/bin/env python3
"""Regression tests for ota_regression.py (TM-40).

    python3 -m unittest tools/bench/test_ota_regression.py

No hardware and no network: `run()` is driven against an injectable fake
serial port (`FakeNodeSerial`, answers our bench commands like the firmware
would) and injectable fake `get`/`poster` callables (`FakeNode`, answers like
the node's HTTP server would) that stand in for `webflash.http_get` /
`webflash.upload_multipart`. Four scenarios per the brief: a clean pass, the
node never coming back after the upload, a settings drift across the flash,
and a hardware mismatch caught before anything is triggered.
"""

import argparse
import os
import sys
import tempfile
import time
import unittest
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ota_regression as otr  # noqa: E402
import webflash  # noqa: E402  (path already added by ota_regression's own sys.path.insert)


# --- canned firmware answers -------------------------------------------------


def info_block(*, ver="4.35p", build="Aug 30 2026 / 19:07:23", call="DK5EN-92",
                node_id="12345678", hw_idx="4", hardware="TBEAM", webserver="on"):
    return (
        f"--MeshCom {ver} (build: {build})\n"
        f"...UPDATE: none\n"
        f"...Call: <{call}> ...ID {node_id} ...NODE {hw_idx} <{hardware}> "
        f"...UTC-OFF 1.000000 [gps]\n"
        f"...BATT 3.85 V ...BATT 62 % ...MAXV 4.200 V\n"
        f"...TIME 12345 ms\n"
        f"...Flash-Version 1\n"
        f"...Webserver  {webserver}\n"
    )


def maxhop_line(text="3", pos="6"):
    return f"[MAXHOP];text;{text};pos;{pos}\n"


def lora_block(freq="433.1750", power="14", bw="125", sf="11", cr="5"):
    return (
        f"--MeshCom 4.35p\n"
        f"...LoRa RF-Frequ: <{freq} MHz>\n"
        f"...LoRa RF-Power: <{power} dBm>\n"
        f"...LoRa RF-BW:    <{bw} kHz>\n"
        f"...LoRa RF-SF:    <{sf}>\n"
        f"...LoRa RF-CR:    <4/{cr}>\n"
    )


def wifistat_line(ssid="ORBI63", ip="192.168.1.90"):
    return (f"[WIFI];stat;ssid;{ssid};localip;{ip};hostip;;iWlanWait;0;wd_stage;0;"
            f"policy;1;scan_pending;0;bringups;1;bringup_ms;9000;last_got_ip_ms;9000;"
            f"last_disc_ms;0\n")


def build_responses(**kw):
    info_kw = {k: v for k, v in kw.items()
               if k in ("ver", "build", "call", "node_id", "hw_idx", "hardware", "webserver")}
    lora_kw = {k: v for k, v in kw.items() if k in ("freq", "power", "bw", "sf", "cr")}
    hop_kw = {k: v for k, v in kw.items() if k in ("text", "pos")}
    wifi_kw = {k: v for k, v in kw.items() if k in ("ssid", "ip")}
    return {
        "--info": info_block(**info_kw),
        "--maxhop": maxhop_line(**hop_kw),
        "--lora": lora_block(**lora_kw),
        "--wifistat": wifistat_line(**wifi_kw),
    }


# --- fake serial --------------------------------------------------------------


class FakeNodeSerial:
    """Fake pyserial port that answers `--info`/`--maxhop`/`--lora`/`--wifistat`
    like the firmware would, and lets a test push asynchronous lines (boot
    markers) the way the real node would print them unprompted."""

    def __init__(self, responses, boot_lines: str = "CLIENT STARTED\n"):
        self._out = bytearray(boot_lines.encode())
        self._cmdbuf = ""
        self.responses = responses
        self.written: list[str] = []
        self.closed = False

    def read(self, n: int = 4096) -> bytes:
        time.sleep(0.01)
        if not self._out:
            return b""
        chunk = bytes(self._out[:n])
        del self._out[:n]
        return chunk

    def write(self, data) -> int:
        if isinstance(data, str):
            data = data.encode()
        for byte in data:
            ch = chr(byte)
            if ch in "\r\n":
                if self._cmdbuf:
                    cmd = self._cmdbuf.strip()
                    self.written.append(cmd)
                    self._cmdbuf = ""
                    resp = self.responses.get(cmd)
                    if resp:
                        self._out += resp.encode()
            else:
                self._cmdbuf += ch
        return len(data)

    def flush(self) -> None:
        pass

    def close(self) -> None:
        self.closed = True

    def push(self, text: str) -> None:
        """Test-only: inject lines the node prints on its own (boot output)."""
        self._out += text.encode()


# --- fake HTTP node ------------------------------------------------------------


class FakeNode:
    """Stands in for the node's HTTP server across `webflash.http_get`/
    `upload_multipart`; wired to a `FakeNodeSerial` so the upload can push the
    matching serial reboot markers the way the real safeboot/app would."""

    def __init__(self, serial: FakeNodeSerial, *, hardware="TBEAM", version="4.35p",
                 build="Aug 30 2026 / 19:07:23", come_back=True, on_upload=None):
        self.serial = serial
        self.hardware = hardware
        self.version = version
        self.build = build
        self.come_back = come_back
        self.on_upload = on_upload
        self.safeboot_started = False
        self.upload_done = False

    def _index_html(self) -> str:
        return (f"<tr><td>Firmware</td><td>Meshcom {self.version}"
                f"<br>(build: {self.build})<br>(flash-version 1)</td></tr>"
                f"<tr><td>Hardware</td><td>{self.hardware}</td></tr>")

    def get(self, url: str, timeout: float = 5.0):
        if url.endswith("/callfunction/?otaupdate"):
            self.safeboot_started = True
            raise OSError("connection reset (node rebooting into safeboot)")
        if url.endswith("/update"):
            return (200, "safeboot") if self.safeboot_started else (404, "not found")
        if "/ota/start" in url:
            return 200, "OK"
        if url.endswith("/"):
            if not self.upload_done:
                return 200, self._index_html()
            return (200, self._index_html()) if self.come_back else (599, "")
        raise OSError(f"unexpected URL in test: {url}")

    def poster(self, url: str, path: Path):
        self.upload_done = True
        if self.on_upload:
            self.on_upload(self)
        if self.come_back:
            self.serial.push("OTA update finished successfully!\n")
            self.serial.push("[BOOT];ready;ms;15000;ip;1\n")
            self.serial.push("[WIFI];event;got_ip;ms;14000\n")
        return 200, "Update Success! Rebooting..."


def make_args(port="/dev/fake", env="ttgo_tbeam", host=None, ip="192.168.1.90",
              settings_check=False, **overrides) -> argparse.Namespace:
    ns = argparse.Namespace(
        port=port, env=env, host=host, ip=ip, bin=None, settings_check=settings_check,
        force=False, runs_dir="", boot_wait_s=0.5, snapshot_timeout_s=1.0,
        safeboot_poll_s=0.5, reboot_poll_s=0.5, post_ota_timeout_s=1.0,
    )
    for k, v in overrides.items():
        setattr(ns, k, v)
    return ns


class OtaRegressionTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.fw = Path(self.tmp.name) / "firmware.bin"
        self.fw.write_bytes(b"\x00\x01\x02\x03" * 1024)

    def _run(self, args, node: FakeNode):
        args.bin = self.fw
        args.runs_dir = str(Path(self.tmp.name) / "runs")

        def opener(_port):
            return node.serial

        rc = otr.run(args, opener=opener, get=node.get, poster=node.poster)
        rundirs = sorted((Path(self.tmp.name) / "runs").glob("ota_*"))
        self.assertEqual(len(rundirs), 1)
        summary = (rundirs[0] / "summary.json")
        import json
        return rc, json.loads(summary.read_text(encoding="utf-8")), rundirs[0]

    # -- pass ------------------------------------------------------------

    def test_pass(self):
        responses = build_responses(hardware="TBEAM")
        serial = FakeNodeSerial(responses)
        node = FakeNode(serial, hardware="TBEAM", come_back=True)
        args = make_args(env="ttgo_tbeam")

        rc, result, rundir = self._run(args, node)

        self.assertEqual(rc, 0, result.get("fails"))
        self.assertEqual(result["verdict"], "PASS")
        self.assertEqual(result["fails"], [])
        self.assertTrue(result["ota"]["ok"])
        self.assertTrue(result["boot_ready"])
        self.assertTrue(result["wifi_rejoined"])
        self.assertEqual(result["before"]["hardware"], "TBEAM")
        self.assertEqual(result["after"]["hardware"], "TBEAM")
        self.assertEqual(result["settings_diffs"], [])
        self.assertTrue((rundir / "serial.log").exists())
        self.assertIn("OTA update finished successfully!",
                      (rundir / "serial.log").read_text(encoding="utf-8"))

    # -- node never returns -----------------------------------------------

    def test_node_never_returns(self):
        responses = build_responses(hardware="TBEAM")
        serial = FakeNodeSerial(responses)
        node = FakeNode(serial, hardware="TBEAM", come_back=False)
        args = make_args(env="ttgo_tbeam")

        rc, result, _ = self._run(args, node)

        self.assertEqual(rc, 1)
        self.assertEqual(result["verdict"], "FAIL")
        self.assertFalse(result["ota"]["ok"])
        self.assertEqual(result["ota"]["stage"], "reboot_timeout")
        self.assertFalse(result["boot_ready"])
        self.assertTrue(any("BOOT" in f for f in result["fails"]))
        self.assertTrue(any("reboot_timeout" in f for f in result["fails"]))

    # -- settings changed across the flash ---------------------------------

    def test_settings_changed_warns_by_default(self):
        responses = build_responses(hardware="TBEAM", sf="11")
        serial = FakeNodeSerial(responses)

        def drift(_node):
            serial.responses["--lora"] = lora_block(sf="7")  # SF11 -> SF7

        node = FakeNode(serial, hardware="TBEAM", come_back=True, on_upload=drift)
        args = make_args(env="ttgo_tbeam", settings_check=False)

        rc, result, _ = self._run(args, node)

        self.assertEqual(rc, 0, result.get("fails"))
        self.assertEqual(result["verdict"], "PASS")
        self.assertTrue(any("lora_sf" in d for d in result["settings_diffs"]))
        self.assertTrue(any("settings changed" in w for w in result["warnings"]))

    def test_settings_changed_fails_with_settings_check(self):
        responses = build_responses(hardware="TBEAM", sf="11")
        serial = FakeNodeSerial(responses)

        def drift(_node):
            serial.responses["--lora"] = lora_block(sf="7")

        node = FakeNode(serial, hardware="TBEAM", come_back=True, on_upload=drift)
        args = make_args(env="ttgo_tbeam", settings_check=True)

        rc, result, _ = self._run(args, node)

        self.assertEqual(rc, 1)
        self.assertEqual(result["verdict"], "FAIL")
        self.assertTrue(any("settings changed" in f for f in result["fails"]))

    # -- hardware mismatch, caught before anything is triggered -------------

    def test_hardware_mismatch(self):
        responses = build_responses(hardware="HELTEC_V3")
        serial = FakeNodeSerial(responses)
        node = FakeNode(serial, hardware="HELTEC_V3", come_back=True)
        args = make_args(env="ttgo_tbeam")  # expects TBEAM, node reports HELTEC_V3

        rc, result, _ = self._run(args, node)

        self.assertEqual(rc, 1)
        self.assertEqual(result["verdict"], "FAIL")
        self.assertFalse(result["ota"]["ok"])
        self.assertEqual(result["ota"]["stage"], "hardware_mismatch")
        self.assertFalse(node.safeboot_started, "safeboot must not be triggered on a mismatch")
        self.assertFalse(node.upload_done, "no upload must happen on a mismatch")
        self.assertTrue(any("hardware mismatch" in f for f in result["fails"]))


# --- pure-function tests -----------------------------------------------------


class SnapshotParsingTest(unittest.TestCase):
    def test_extract_snapshot_full_block(self):
        blob = "\n".join([build_responses()[k] for k in
                          ("--info", "--maxhop", "--lora", "--wifistat")])
        snap, problems = otr.extract_snapshot(blob)
        self.assertEqual(problems, [])
        self.assertEqual(snap["hardware"], "TBEAM")
        self.assertEqual(snap["webserver"], "on")
        self.assertEqual(snap["maxhop_text"], "3")
        self.assertEqual(snap["lora_sf"], "11")
        self.assertEqual(snap["wifi_localip"], "192.168.1.90")

    def test_extract_snapshot_missing_fields_are_reported(self):
        snap, problems = otr.extract_snapshot("garbage, no markers here")
        self.assertEqual(snap, {})
        self.assertGreaterEqual(len(problems), 5)

    def test_diff_settings(self):
        before, _ = otr.extract_snapshot("\n".join(build_responses(sf="11").values()))
        after, _ = otr.extract_snapshot("\n".join(build_responses(sf="7").values()))
        diffs = otr.diff_settings(before, after)
        self.assertEqual(len(diffs), 1)
        self.assertIn("lora_sf", diffs[0])


class ParseOnlyTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)

    def _write_log(self, before: str, after: str) -> Path:
        wall = otr.wall_now()
        lines = []
        for block, label in ((before, "before"), (after, "after")):
            for line in block.splitlines():
                lines.append(f"{wall} {line}")
        path = Path(self.tmp.name) / "serial.log"
        path.write_text("\n".join(lines) + "\n", encoding="utf-8")
        return path

    def test_parse_only_pass(self):
        before = "\n".join(build_responses(hardware="TBEAM").values())
        after = ("OTA update finished successfully!\n[BOOT];ready;ms;15000;ip;1\n"
                  "[WIFI];event;got_ip;ms;14000\n"
                  + "\n".join(build_responses(hardware="TBEAM").values()))
        path = self._write_log(before, after)
        args = make_args()
        args.parse_only = str(path)
        args.no_write = True
        args.expect_hw = "TBEAM"

        rc = otr.cmd_parse_only(args)
        self.assertEqual(rc, 0)

    def test_parse_only_no_split_marker(self):
        path = Path(self.tmp.name) / "serial.log"
        path.write_text(f"{otr.wall_now()} nothing interesting here\n", encoding="utf-8")
        args = make_args()
        args.parse_only = str(path)
        args.no_write = True
        args.expect_hw = None

        rc = otr.cmd_parse_only(args)
        self.assertEqual(rc, 1)


if __name__ == "__main__":
    unittest.main()
