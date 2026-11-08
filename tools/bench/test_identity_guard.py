#!/usr/bin/env python3
"""Unit tests for the offline waiver of tools/bench/identity_guard.py (no port, no network)."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import identity_guard as ig  # noqa: E402

GOOD = (ig.TESTDATA / "tbeam_ok_info.txt").read_text()
# ESP32 without WLAN: no IP, web server off, clock never set
OFFLINE = (GOOD.replace("hasIpAddress: yes", "hasIpAddress: no")
           .replace("Webserver  on", "Webserver  off")
           .replace("...UPDATE: 2026-09-26 09:21:51", "...UPDATE: 1970-01-01 00:00:05"))
NODE = "t-beam-92"


class OfflineWaiverTest(unittest.TestCase):
    def setUp(self) -> None:
        self.fleet = ig.load_fleet()

    def test_offline_refused_by_default(self) -> None:
        with self.assertRaises(ig.IdentityError) as cm:
            ig.require(OFFLINE, NODE, self.fleet)
        self.assertIn("no WiFi IP", str(cm.exception))

    def test_offline_accepted_with_waiver_and_listed(self) -> None:
        info = ig.require(OFFLINE, NODE, self.fleet, offline_ok=True)
        self.assertEqual(info.waived, ["wifi", "web", "clock"])

    def test_online_node_has_no_waiver_even_with_flag(self) -> None:
        self.assertEqual(ig.require(GOOD, NODE, self.fleet, offline_ok=True).waived, [])

    def test_foreign_or_placeholder_call_still_refused(self) -> None:
        for call in ("XX0XXX-00", "OE1XAR-92", "DK5EN-93"):
            with self.assertRaises(ig.IdentityError, msg=call):
                ig.require(OFFLINE.replace("<DK5EN-92>", f"<{call}>"), NODE, self.fleet, offline_ok=True)

    def test_txpower_above_limit_still_refused(self) -> None:
        with self.assertRaises(ig.IdentityError) as cm:
            ig.require(OFFLINE.replace("TXPWR 2 dBm", "TXPWR 10 dBm"), NODE, self.fleet, offline_ok=True)
        self.assertIn("TX power 10 dBm", str(cm.exception))

    def test_require_node_forwards_flag(self) -> None:
        from unittest import mock

        with mock.patch.object(ig, "read_info_serial", return_value=OFFLINE):
            with self.assertRaises(ig.IdentityError):
                ig.require_node(NODE, port="/dev/fake", fleet=self.fleet)
            self.assertEqual(ig.require_node(NODE, port="/dev/fake", fleet=self.fleet, offline_ok=True).waived,
                             ["wifi", "web", "clock"])

    def test_cli_prints_waiver(self) -> None:
        import contextlib
        import io
        import tempfile

        with tempfile.TemporaryDirectory() as d:
            f = Path(d) / "info.txt"
            f.write_text(OFFLINE)
            for flag, rc in (([], 1), (["--offline-ok"], 0)):
                buf = io.StringIO()
                with contextlib.redirect_stdout(buf):
                    self.assertEqual(ig.main(["--node", NODE, "--info-file", str(f), *flag]), rc)
                if rc == 0:
                    self.assertIn("ok (offline waiver: wifi, web, clock)", buf.getvalue())


if __name__ == "__main__":
    unittest.main()
