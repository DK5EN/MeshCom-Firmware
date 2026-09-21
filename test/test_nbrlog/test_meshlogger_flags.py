#!/usr/bin/env python3
# /// script
# requires-python = ">=3.11"
# dependencies = []
# ///
"""Testet die Flaglogik von ``tools/meshlogger.py`` gegen einen Mock -- kein Netz.

Nicht unter ``tools/testdata/nbr/`` abgelegt, weil es um ``meshlogger.py``
geht, nicht um ``nbrlog.py``-Fixtures; ``nbrlog.py`` selbst prueft sich per
``--self-test`` gegen die Fixtures dort.

Ersetzt ``socket.create_connection`` durch einen In-Memory-Socket, der die
HMAC-freie Verbindung (kein Passwort) und eine ``--info``-Antwort simuliert,
und laesst ``meshlogger.main()`` mit winzigem ``--hours`` real durchlaufen --
der Weg ueber die Deadline ist derselbe, den ein echter Lauf am Laufzeitende
nimmt, nur ohne Netz und ohne echte Wartezeit (``time.sleep`` genoppt).

Usage::

    uv run test/test_nbrlog/test_meshlogger_flags.py
"""

from __future__ import annotations

import importlib.util
import socket as socket_module
import sys
import tempfile
import time
from pathlib import Path
from unittest import mock

TOOLS_DIR = Path(__file__).resolve().parents[2] / "tools"


def load_meshlogger():
    spec = importlib.util.spec_from_file_location("meshlogger", TOOLS_DIR / "meshlogger.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


class FakeSocket:
    """Minimaler Socket-Mock: Handshake + eine --info-Antwort, danach still."""

    def __init__(self, info_reply: bytes) -> None:
        self._script = [b"OK\n", info_reply]
        self.sent: list[str] = []

    def settimeout(self, _t):
        pass

    def setsockopt(self, *_a, **_kw):
        pass

    def recv(self, _n):
        if self._script:
            return self._script.pop(0)
        raise socket_module.timeout("no more scripted data")

    def sendall(self, data: bytes) -> None:
        self.sent.append(data.decode().strip())

    def close(self):
        pass


def run_meshlogger(meshlogger, argv: list[str], info_reply: bytes) -> FakeSocket:
    fake = FakeSocket(info_reply)
    with tempfile.TemporaryDirectory() as tmpdir:
        full_argv = [
            "meshlogger.py", "dummyhost.local",
            "--hours", "0.0006",  # ~2.2 s wall clock -- enough for one pass
            "--outdir", tmpdir,
        ] + argv
        with mock.patch.object(sys, "argv", full_argv), \
             mock.patch("socket.create_connection", return_value=fake), \
             mock.patch("time.sleep", lambda _s: None):
            meshlogger.main()
    return fake


def sent_flag_commands(fake: FakeSocket) -> list[str]:
    return [s for s in fake.sent if s.startswith("--") and s != "--info"]


def main() -> int:
    meshlogger = load_meshlogger()
    failures: list[str] = []

    # -- 1) Standard setzt weiterhin genau txcapture,loradebug --
    fake_default = run_meshlogger(
        meshlogger, [], info_reply=b"...TXCAPTURE off\n...LORADEBUG off\n"
    )
    on_cmds = [c for c in sent_flag_commands(fake_default) if c.endswith(" on")]
    if on_cmds != ["--txcapture on", "--loradebug on"]:
        failures.append(f"Standardflags: bekommen {on_cmds!r}, erwartet ['--txcapture on', '--loradebug on']")
    off_cmds = [c for c in sent_flag_commands(fake_default) if c.endswith(" off")]
    if off_cmds != ["--txcapture off", "--loradebug off"]:
        failures.append(f"Standard-Restore: bekommen {off_cmds!r}, erwartet beide Flags auf 'off' zurueckgesetzt")

    # -- 2) --flags nbrdebug setzt genau nbrdebug; nicht im --info gefunden
    #    (Firmware-Seite existiert noch nicht) => beim Beenden unangetastet --
    fake_nbr = run_meshlogger(
        meshlogger, ["--flags", "nbrdebug"], info_reply=b"...LORADEBUG off\n"
    )
    cmds = sent_flag_commands(fake_nbr)
    if cmds != ["--nbrdebug on"]:
        failures.append(
            f"--flags nbrdebug: bekommen {cmds!r}, erwartet genau ['--nbrdebug on'] "
            "(auf on gesetzt, aber beim Beenden NICHT ausgeschaltet, da Zustand unbekannt)"
        )

    if failures:
        print(f"FEHLGESCHLAGEN ({len(failures)}):")
        for f in failures:
            print(f"  - {f}")
        return 1
    print("test_meshlogger_flags: alle Pruefungen bestanden.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
