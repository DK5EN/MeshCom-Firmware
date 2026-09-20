#!/usr/bin/env python3
"""Regressionstest TM-50: meshlogger erkennt eine Zombie-TCP-Verbindung.

Nachgestellt wird der Vorfall vom Overnight-Soak 2026-09-01: der Node
verschwindet (WLAN-Ausfall) OHNE FIN -- der Socket bleibt halb offen, recv()
laeuft in endlose Timeouts. Vor TM-50 sass der Logger so 2,4 h still
(reconnects=0) und konnte am Ende die Debug-Flags nicht restaurieren
(Broken pipe).

Der Fake-Server spielt beide Phasen:
  Verbindung 1: Banner, --info-Antwort, zwei Datenzeilen -- dann STILLE bei
                offenem Socket (Zombie).
  Verbindung 2: Banner, zeichnet empfangene Kommandos auf, sendet periodisch
                Zeilen bis zum Ende, bleibt lebendig (Restore-Ziel).

fails-before (gegen den Stand vor TM-50 verifiziert): ohne Stille-Watchdog
haengt der Logger bis zur Deadline auf Verbindung 1 -- kein Zombie-Marker,
kein Re-Apply, reconnects=0; die Asserts 1-3 unten sind rot.

    python3 tools/bench/test_meshlogger.py
"""

import os
import re
import socket
import subprocess
import sys
import tempfile
import threading
import time
import unittest

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
MESHLOGGER = os.path.join(REPO, "tools", "meshlogger.py")


class FakeConsole(threading.Thread):
    """Sequenzieller Ein-Client-Server wie net_console.h -- Phase 1 Zombie,
    ab Phase 2 lebendig mit Kommando-Mitschrift."""

    def __init__(self):
        super().__init__(daemon=True)
        self.srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.srv.bind(("127.0.0.1", 0))
        self.srv.listen(2)
        self.port = self.srv.getsockname()[1]
        self.commands = []          # [(conn_nr, kommando), ...]
        self.conn_count = 0
        self.stop = threading.Event()

    def run(self):
        # Ein Handler-Thread pro Verbindung: die Zombie-Phase von Verbindung 1
        # haelt ihren Socket offen, waehrend der Accept-Loop Verbindung 2
        # trotzdem annimmt (der echte net_console kickt den Alt-Client; fuer
        # den Test genuegt paralleles Bedienen).
        while not self.stop.is_set():
            try:
                self.srv.settimeout(1.0)
                conn, _ = self.srv.accept()
            except socket.timeout:
                continue
            except OSError:
                return
            self.conn_count += 1
            nr = self.conn_count

            def handle(c=conn, n=nr):
                try:
                    self._serve(c, n)
                except OSError:
                    pass
                finally:
                    try:
                        c.close()
                    except OSError:
                        pass

            threading.Thread(target=handle, daemon=True).start()

    def _recv_commands(self, conn, nr, duration):
        conn.settimeout(0.2)
        end = time.time() + duration
        buf = b""
        while time.time() < end and not self.stop.is_set():
            try:
                chunk = conn.recv(1024)
            except socket.timeout:
                continue
            except OSError:
                return
            if not chunk:
                return
            buf += chunk
            while b"\n" in buf:
                line, _, buf = buf.partition(b"\n")
                cmd = line.decode(errors="replace").strip()
                if cmd:
                    self.commands.append((nr, cmd))
                    if cmd == "--info":
                        conn.sendall(b"...LORADEBUG off\n")

    def _serve(self, conn, nr):
        conn.sendall(b"MeshCom Console\nType --help for commands\n")
        if nr == 1:
            # --info + Flag-Kommandos einsammeln, zwei Datenzeilen liefern,
            # dann ZOMBIE: Socket offen halten, nie wieder senden.
            self._recv_commands(conn, nr, 7.0)
            conn.sendall(b"[MC-DBG] RING_STATUS queued=0\n[BEAT]...alive\n")
            while not self.stop.is_set():
                time.sleep(0.2)   # Stille bei offenem Socket
        else:
            # Lebendige Verbindung: Kommandos mitschreiben, periodisch senden.
            end = time.time() + 60
            last = 0.0
            conn.settimeout(0.2)
            buf = b""
            while time.time() < end and not self.stop.is_set():
                now = time.time()
                if now - last >= 0.5:
                    last = now
                    try:
                        conn.sendall(b"[BEAT]...alive\n")
                    except OSError:
                        return
                try:
                    chunk = conn.recv(1024)
                except socket.timeout:
                    continue
                except OSError:
                    return
                if not chunk:
                    return
                buf += chunk
                while b"\n" in buf:
                    line, _, buf = buf.partition(b"\n")
                    cmd = line.decode(errors="replace").strip()
                    if cmd:
                        self.commands.append((nr, cmd))


class TestMeshloggerZombie(unittest.TestCase):
    def test_zombie_erkannt_reconnect_flags_und_restore(self):
        srv = FakeConsole()
        srv.start()
        outdir = tempfile.mkdtemp(prefix="meshlogger_tm50_")

        # 36 s Lauf: ~11 s Phase 1 (Banner/Flags/Zombie), Watchdog 3 s,
        # Backoff 5 s, Rest Phase 2, dann Restore.
        proc = subprocess.run(
            [sys.executable, MESHLOGGER, "127.0.0.1",
             "--port", str(srv.port), "--outdir", outdir,
             "--hours", str(36.0 / 3600.0), "--stall-timeout", "3"],
            capture_output=True, timeout=120)
        srv.stop.set()
        self.assertEqual(proc.returncode, 0, proc.stderr.decode(errors="replace"))

        log = ""
        for name in sorted(os.listdir(outdir)):
            if name.endswith(".log"):
                log += open(os.path.join(outdir, name), errors="replace").read()

        # 1) Der Watchdog hat den Zombie erkannt und benannt.
        self.assertIn("assuming zombie connection", log)
        # 2) Nach dem Reconnect wurden die Flags erneut gesetzt.
        self.assertIn("flags re-applied after reconnect", log)
        # 3) Der Reconnect ist gezaehlt (Endstatus).
        status = open(os.path.join(outdir, "status.txt")).read()
        m = re.search(r"reconnects=(\d+)", status)
        self.assertIsNotNone(m, status)
        self.assertGreaterEqual(int(m.group(1)), 1, status)
        # 4) Verbindung 2 hat Re-Apply UND Restore wirklich empfangen.
        conn2 = [c for (n, c) in srv.commands if n >= 2]
        self.assertIn("--txcapture on", conn2)
        self.assertIn("--loradebug on", conn2)
        self.assertIn("--txcapture off", conn2)
        self.assertIn("--loradebug off", conn2)
        # 5) Restore lief auf der lebendigen Verbindung, nicht in den Fehler.
        self.assertIn("flags restored", log)
        self.assertNotIn("could NOT be restored", log)


if __name__ == "__main__":
    unittest.main(verbosity=2)
