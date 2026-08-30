#!/usr/bin/env python3
"""Regression tests for apreboot.py (TM-38).

    python3 -m unittest tools/bench/experiments/test_apreboot.py

Two layers:
  * synthetic transcripts through the offline `report` path -- a passing run of
    all four bench nodes, a board that reboots during recovery, a board whose
    server traffic never comes back, and a run where the APs were never
    cycled;
  * the phase machine end to end against a scripted fake serial port, so
    SETTLE -> PROMPT -> RECOVERY -> verdict runs in seconds with no hardware.
"""

import json
import os
import sys
import tempfile
import time
import unittest
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import apreboot  # noqa: E402

BASE = time.mktime(time.strptime("2026-08-30 12:00:00", apreboot.WALL_FMT))
SETTLE, WINDOW, RECOVERY = 180.0, 180.0, 600.0
T0 = 200.0          # seconds after BASE: the first link-down edge


def eth_hb(rx_n: int, ms: int, ip: str = "192.168.1.90", link: int = 1) -> str:
    return ("[ETH];link;up;link;%d;link_age_s;58;ip;%s;dest;44.143.8.143;hb_age_s;5;"
            "got_ip_n;1;downs;0;renews;0;renew_fail;0;resets;0;rx_n;%d;rx_max_ms;3;"
            "tx_fail;0;tx_max_ms;20;ms;%d" % (link, ip, rx_n, ms))


def wifi_hb(ms: int) -> str:
    return ("[WIFI];link;up;rssi;-61;bssid;5A:AF:97:2E:2B:8B;chan;3;age_s;51;"
            "got_ip_n;1;ip;1;ms;%d" % ms)


def esp32_script(name: str, *, reboot_at=None, rx_after=True, outage=True,
                 first_disc_at=T0):
    """A healthy ESP32 bench node, with the two failure modes as options."""
    s = [
        (0.0, "rst:0x1 (POWERON_RESET),boot:0x8"),
        (5.2, "[WIFI];event;connected;ms;5200"),
        (9.1, "[WIFI];event;got_ip;ms;9100"),
        (10.2, "[BOOT];ready;ms;10200;ip;1"),
        (11.0, "[UDP];log;1"),
        (20.0, "[UDP];tx;ip;44.143.8.143;port;1799;len;35;ok;1"),
        (41.0, "[UDP];rx;ip;44.143.8.143;port;1799;len;22;head;42454154"),
        (60.3, wifi_hb(60300)),
        (95.0, "[UDP];tx;ip;44.143.8.143;port;1799;len;35;ok;1"),
        (101.0, "[NTP];ok;epoch;1788084488;rtt;106"),
        (120.4, wifi_hb(120400)),
        (141.0, "[UDP];rx;ip;44.143.8.143;port;1799;len;22;head;42454154"),
        (180.5, wifi_hb(180500)),
    ]
    if not outage:
        s += [(240.6, wifi_hb(240600)), (300.7, wifi_hb(300700)),
              (360.8, wifi_hb(360800))]
        return s
    s += [
        (first_disc_at, "[WIFI];event;disconnected;reason;200;ms;200400"),
        (T0 + 5, "[WIFI];link;down;sta;1;status;6;down_s;5;last_reason;200;"
                 "got_ip_n;1;ip;0;ms;205400"),
        (T0 + 31, "[WIFI];event;disconnected;reason;201;ms;231000"),
        (T0 + 60, "[WIFI];event;connected;ms;260000"),
        (T0 + 63, "[WIFI];event;got_ip;ms;263000"),
        (T0 + 72, "[UDP];tx;ip;44.143.8.143;port;1799;len;35;ok;1"),
    ]
    if rx_after:
        s += [(T0 + 88, "[UDP];rx;ip;44.143.8.143;port;1799;len;22;head;42454154")]
    s += [(T0 + 120, wifi_hb(320000)), (T0 + 180, wifi_hb(380000))]
    if reboot_at is not None:
        s += [(T0 + reboot_at, "rst:0x10 (RTCWDT_RTC_RESET),boot:0x8"),
              (T0 + reboot_at + 10, "[BOOT];ready;ms;10100;ip;1")]
    return sorted(s)


def rak_script(*, outage=True):
    s = [
        (2.1, "[ETH];event;link;up;ip;0;ms;2100"),
        (8.3, "[ETH];event;got_ip;192.168.1.90;ms;8300"),
        (9.1, "[BOOT];ready;ms;9100;ip;1;eth;1"),
        (11.0, "[UDP];log;1"),
        (60.2, eth_hb(12, 60200)),
        (120.2, eth_hb(25, 120200)),
        (180.2, eth_hb(38, 180200)),
    ]
    if not outage:
        return s + [(240.2, eth_hb(51, 240200)), (300.2, eth_hb(64, 300200)),
                    (360.2, eth_hb(77, 360200))]
    return s + [
        (T0 + 1, "[ETH];event;link;down;ip;1;ms;201000"),
        (T0 + 40, "[ETH];event;link;up;ip;0;ms;240100"),
        (T0 + 50, "[ETH];event;dhcp;rc;1;ms;250000"),
        (T0 + 52, "[ETH];event;got_ip;192.168.1.90;ms;252000"),
        (T0 + 100, eth_hb(40, 300200)),
        (T0 + 160, eth_hb(53, 360200)),
    ]


def write_run(rundir: Path, scripts, *, outage=True):
    """Materialise scripts as `<board>.log` files plus meta.json."""
    for name, script in scripts.items():
        with open(rundir / f"{name}.log", "w", encoding="utf-8") as fh:
            fh.write(f"{apreboot.wall_now(BASE)} ## opened /dev/fake-{name} (#1)\n")
            for off, line in script:
                fh.write(f"{apreboot.wall_now(BASE + off)} {line}\n")
            fh.write(f"{apreboot.wall_now(BASE + 11.0)} ## >> --udplog on\n")
    meta = dict(label="unittest", rundir=str(rundir), started=BASE, settle=SETTLE,
                cycle_window=WINDOW, recovery=RECOVERY, prompt_ts=BASE + SETTLE,
                boards=[dict(name=n, port=f"/dev/fake-{n}",
                             kind=apreboot.KIND_RAK if "rak" in n else apreboot.KIND_ESP32)
                        for n in scripts])
    (rundir / "meta.json").write_text(json.dumps(meta) + "\n", encoding="utf-8")
    return meta


def report(rundir: Path):
    states = apreboot.states_from_logs(sorted(rundir.glob("*.log")))
    meta = json.loads((rundir / "meta.json").read_text(encoding="utf-8"))
    by_name = {b["name"]: b["kind"] for b in meta["boards"]}
    for st in states:
        if not st.kind_locked:
            st.kind = by_name.get(st.name, st.kind)
    return states, meta, apreboot.analyse_run(states, meta)


def four_boards(**kw):
    # the T-Deck loses the AP first, so t0 is unambiguous at 1 s log resolution
    return {"tdeck": esp32_script("tdeck", **kw),
            "heltec": esp32_script("heltec", first_disc_at=T0 + 3, **kw),
            "tbeam": esp32_script("tbeam", first_disc_at=T0 + 4, **kw),
            "rak": rak_script(outage=kw.get("outage", True))}


class TranscriptTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.rundir = Path(self.tmp.name)
        self.addCleanup(self.tmp.cleanup)

    def test_passing_run_of_four_boards(self):
        write_run(self.rundir, four_boards())
        states, meta, run = report(self.rundir)
        self.assertEqual(run["verdict"], "PASS", run["fails"])
        self.assertEqual(run["t0_board"], "tdeck")
        self.assertAlmostEqual(run["t0"], BASE + T0, delta=1.0)
        by = {r.name: r for r in run["results"]}

        for name in ("tdeck", "heltec", "tbeam"):
            r = by[name]
            self.assertEqual(r.kind, apreboot.KIND_ESP32)
            for m in ("link_up", "got_ip", "udp_tx", "udp_rx"):
                self.assertEqual(r.after_state[m], "ok", f"{name}/{m}")
            self.assertAlmostEqual(r.after["got_ip"], 63.0, delta=1.5)
            self.assertAlmostEqual(r.after["udp_rx"], 88.0, delta=1.5)
            # a refresh was not due again inside the 600 s window
            self.assertTrue(r.after_state["ntp_ok"].startswith("n/a"),
                            r.after_state["ntp_ok"])
            self.assertEqual(r.reboots_after, 0)

        rak = by["rak"]
        self.assertEqual(rak.kind, apreboot.KIND_RAK)
        self.assertEqual(rak.after_state["link_up"], "ok")
        self.assertEqual(rak.after_state["got_ip"], "ok")
        # the nRF52 UDP path has no per-datagram print: n/a, never a failure
        self.assertIn("n/a", rak.after_state["udp_tx"])
        # ... rx comes from the [ETH];link heartbeat's rx_n delta instead
        self.assertEqual(rak.after_state["udp_rx"], "ok")
        self.assertAlmostEqual(rak.after["udp_rx"], 100.0, delta=1.5)

        text = apreboot.render_summary(states, meta, run)
        self.assertIn("verdict            PASS", text)
        self.assertIn("== rak (rak)   PASS", text)

    def test_board_that_reboots_during_recovery_fails(self):
        write_run(self.rundir, four_boards(reboot_at=95))
        _, _, run = report(self.rundir)
        self.assertEqual(run["verdict"], "FAIL")
        by = {r.name: r for r in run["results"]}
        self.assertEqual(by["heltec"].reboots_after, 2)   # rst:0x + [BOOT];ready
        self.assertTrue(any("rebooted after t0" in f for f in by["heltec"].fails))
        self.assertTrue(any(f.startswith("heltec: rebooted after t0")
                            for f in run["fails"]), run["fails"])
        self.assertTrue(by["rak"].passed)

    def test_board_without_udp_rx_after_t0_fails(self):
        scripts = four_boards()
        scripts["tbeam"] = esp32_script("tbeam", rx_after=False)
        write_run(self.rundir, scripts)
        _, _, run = report(self.rundir)
        self.assertEqual(run["verdict"], "FAIL")
        by = {r.name: r for r in run["results"]}
        self.assertEqual(by["tbeam"].after_state["udp_rx"], "missing")
        self.assertIn("udp_rx missing", by["tbeam"].fails)
        # it did have server traffic before the outage -- that is what makes
        # the missing one a failure rather than an n/a
        self.assertEqual(by["tbeam"].before["udp_rx"], 2)
        self.assertTrue(by["tdeck"].passed)
        self.assertTrue(by["heltec"].passed)

    def test_no_outage_fails_the_whole_run(self):
        write_run(self.rundir, four_boards(outage=False), outage=False)
        _, _, run = report(self.rundir)
        self.assertIsNone(run["t0"])
        self.assertEqual(run["verdict"], "FAIL")
        self.assertIn("no outage detected", run["fails"])
        for r in run["results"]:
            self.assertIn("no outage detected", r.fails)

    def test_report_writes_summary_and_events(self):
        write_run(self.rundir, four_boards())
        rc = apreboot.main(["report", str(self.rundir)])
        self.assertEqual(rc, 0)
        summary = (self.rundir / "summary.txt").read_text(encoding="utf-8")
        self.assertIn("verdict            PASS", summary)
        rows = (self.rundir / "events.csv").read_text(encoding="utf-8").splitlines()
        self.assertEqual(rows[0], ",".join(apreboot.EVENT_FIELDS)
                         if hasattr(apreboot, "EVENT_FIELDS")
                         else ",".join(apreboot.wifisoak.EVENT_FIELDS))
        self.assertTrue(any(",udp_rx," in r for r in rows[1:]))
        self.assertTrue(any("t0+" in r or "t0-" in r for r in rows[1:]))

    def test_report_on_bare_logs_without_meta(self):
        write_run(self.rundir, four_boards())
        (self.rundir / "meta.json").unlink()
        rc = apreboot.main(["report", "--no-write", str(self.rundir / "tdeck.log")])
        self.assertEqual(rc, 0)   # single board, outage present, everything back

    def test_serial_command_after_t0_fails_the_board(self):
        write_run(self.rundir, four_boards())
        with open(self.rundir / "tdeck.log", "a", encoding="utf-8") as fh:
            fh.write(f"{apreboot.wall_now(BASE + T0 + 30)} ## >> --reboot\n")
        _, _, run = report(self.rundir)
        self.assertEqual(run["verdict"], "FAIL")
        by = {r.name: r for r in run["results"]}
        self.assertTrue(any("serial command was sent after t0" in f
                            for f in by["tdeck"].fails), by["tdeck"].fails)


class SpecTest(unittest.TestCase):
    def test_board_spec_kinds(self):
        self.assertEqual(apreboot.parse_board_spec("tdeck=/dev/cu.usbmodem1101"),
                         ("tdeck", "/dev/cu.usbmodem1101", apreboot.KIND_ESP32))
        # the RAK profile is picked by name (DTR high, [ETH] markers) ...
        self.assertEqual(apreboot.parse_board_spec("rak=/dev/cu.usbmodem201301"),
                         ("rak", "/dev/cu.usbmodem201301", apreboot.KIND_RAK))
        # ... and can be forced either way
        self.assertEqual(apreboot.parse_board_spec("gw=/dev/ttyUSB0:rak"),
                         ("gw", "/dev/ttyUSB0", apreboot.KIND_RAK))
        self.assertEqual(apreboot.parse_board_spec("rak2=/dev/ttyUSB0:esp32"),
                         ("rak2", "/dev/ttyUSB0", apreboot.KIND_ESP32))
        with self.assertRaises(SystemExit):
            apreboot.parse_board_spec("/dev/ttyUSB0")


class ScriptedSerial:
    """Fake pyserial port: emits its script on the wall clock, records writes."""

    def __init__(self, script):
        self.script = sorted(script)
        self.i = 0
        self.opened = time.monotonic()
        self.written = bytearray()
        self.closed = False

    def read(self, _n=1):
        time.sleep(0.02)
        now = time.monotonic() - self.opened
        out = b""
        while self.i < len(self.script) and self.script[self.i][0] <= now:
            out += (self.script[self.i][1] + "\r\n").encode()
            self.i += 1
        return out

    def write(self, data):
        self.written += data
        return len(data)

    def flush(self):
        pass

    def close(self):
        self.closed = True


def fast_esp32(*, outage_at=2.3, udp=True):
    s = [(0.0, "rst:0x1 (POWERON_RESET),boot:0x8"),
         (0.25, "[WIFI];event;connected;ms;250"),
         (0.35, "[WIFI];event;got_ip;ms;350"),
         (0.45, "[BOOT];ready;ms;450;ip;1")]
    if udp:
        s += [(0.9, "[UDP];tx;ip;44.143.8.143;port;1799;len;35;ok;1"),
              (1.1, "[UDP];rx;ip;44.143.8.143;port;1799;len;22;head;42454154")]
    if outage_at is None:
        return s + [(3.0, wifi_hb(3000)), (5.0, wifi_hb(5000))]
    s += [
        (outage_at, "[WIFI];event;disconnected;reason;200;ms;2300"),
        (outage_at + 0.6, "[WIFI];event;connected;ms;2900"),
        (outage_at + 0.7, "[WIFI];event;got_ip;ms;3000"),
    ]
    if udp:
        s += [(outage_at + 0.9, "[UDP];tx;ip;44.143.8.143;port;1799;len;35;ok;1"),
              (outage_at + 1.2, "[UDP];rx;ip;44.143.8.143;port;1799;len;22;head;42454154")]
    return s


class PhaseMachineTest(unittest.TestCase):
    """The whole SETTLE -> PROMPT -> RECOVERY machine, no hardware, ~5 s."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.rundir = Path(self.tmp.name)
        self.addCleanup(self.tmp.cleanup)

    def _run(self, scripts, **kw):
        ports = {n: ScriptedSerial(s) for n, s in scripts.items()}
        boards = [(n, f"/dev/fake-{n}",
                   apreboot.KIND_RAK if "rak" in n else apreboot.KIND_ESP32)
                  for n in scripts]

        def opener(port, _kind):
            return ports[port.rsplit("-", 1)[1]]

        kwargs = dict(settle=1.6, cycle_window=1.6, recovery=2.2, label="fake",
                      opener=opener, status_every=0.4, udplog_after=0.2,
                      do_notify=False)
        kwargs.update(kw)
        rc = apreboot.run_session(self.rundir, boards, **kwargs)
        return rc, ports

    def test_end_to_end_pass(self):
        rc, ports = self._run({"tdeck": fast_esp32(), "heltec": fast_esp32(outage_at=2.5)})
        summary = (self.rundir / "summary.txt").read_text(encoding="utf-8")
        self.assertEqual(rc, 0, summary)
        self.assertIn("verdict            PASS", summary)
        # the prompt really was raised, and exactly one command went out per
        # board -- before t0, never during or after the outage
        self.assertTrue((self.rundir / "PROMPT").exists())
        for name, p in ports.items():
            self.assertEqual(p.written.decode().strip(), "--udplog on", name)
            self.assertTrue(p.closed, name)
        self.assertIn("commands after t0       none", summary)
        # the live artefacts are all there
        for f in ("status.txt", "events.csv", "meta.json", "tdeck.log", "heltec.log"):
            self.assertTrue((self.rundir / f).exists(), f)
        self.assertIn("DONE (recovery window elapsed)",
                      (self.rundir / "status.txt").read_text(encoding="utf-8"))
        meta = json.loads((self.rundir / "meta.json").read_text(encoding="utf-8"))
        self.assertEqual(meta["verdict"], "PASS")
        self.assertIsNotNone(meta["t0"])

    def test_end_to_end_no_outage(self):
        rc, _ = self._run({"tdeck": fast_esp32(outage_at=None)})
        summary = (self.rundir / "summary.txt").read_text(encoding="utf-8")
        self.assertEqual(rc, 1)
        self.assertIn("verdict            FAIL", summary)
        self.assertIn("no outage detected", summary)
        self.assertIn("outage t0          none seen", summary)

    def test_non_gateway_board_warns_but_the_run_goes_on(self):
        # DK5EN-93 on the bench has Gateway off: no KEEP, so no [UDP];tx at all.
        # That is a warning, not a failure -- the marker is then n/a after t0.
        rc, _ = self._run({"tdeck": fast_esp32(udp=False)})
        summary = (self.rundir / "summary.txt").read_text(encoding="utf-8")
        self.assertEqual(rc, 0, summary)
        self.assertIn("WARN tdeck: no [UDP];tx during settle", summary)
        self.assertTrue((self.rundir / "PROMPT").exists())
        self.assertIn("n/a (none before t0)", summary)

    def test_strict_udp_stops_a_non_gateway_at_the_settle_gate(self):
        rc, _ = self._run({"tdeck": fast_esp32(udp=False)}, strict_udp=True)
        summary = (self.rundir / "summary.txt").read_text(encoding="utf-8")
        self.assertEqual(rc, 1)
        self.assertIn("settle incomplete -- tdeck: no [UDP];tx during settle", summary)
        self.assertFalse((self.rundir / "PROMPT").exists())

    def test_rak_is_never_sent_a_serial_command(self):
        # --udplog is not in the nRF52 command table ("...wrong command")
        rak = [(0.1, "[ETH];event;link;up;ip;1;ms;100"),
               (0.2, "[ETH];event;got_ip;192.168.68.72;ms;200"),
               (0.5, eth_hb(766, 500, ip="192.168.68.72")),
               (1.0, eth_hb(770, 1000, ip="192.168.68.72")),
               (2.3, "[ETH];event;link;down;ip;1;ms;2300"),
               (3.0, "[ETH];event;link;up;ip;0;ms;3000"),
               (3.2, "[ETH];event;got_ip;192.168.68.72;ms;3200"),
               (3.6, eth_hb(780, 3600, ip="192.168.68.72")),
               (4.1, eth_hb(795, 4100, ip="192.168.68.72"))]
        rc, ports = self._run({"rak": rak})
        summary = (self.rundir / "summary.txt").read_text(encoding="utf-8")
        self.assertEqual(ports["rak"].written, bytearray())
        self.assertEqual(rc, 0, summary)
        self.assertIn("== rak (rak)   PASS", summary)
        self.assertNotIn("WARN", summary)   # the RAK is exempt from the udp_tx warning

    def test_settle_gate_stops_a_board_that_never_gets_an_ip(self):
        rc, _ = self._run({"tdeck": [(0.0, "rst:0x1 (POWERON_RESET),boot:0x8"),
                                     (0.3, "[WIFI];event;disconnected;reason;201;ms;300")]})
        summary = (self.rundir / "summary.txt").read_text(encoding="utf-8")
        self.assertEqual(rc, 1)
        self.assertIn("settle incomplete", summary)
        # the operator is never sent to the APs for a run that cannot pass
        self.assertFalse((self.rundir / "PROMPT").exists())
        # a run that never prompted has no t0 -- the boot-time [WIFI];link;down
        # of every ESP32 board must not be mistaken for the AP outage
        self.assertIn("outage t0          none seen", summary)
        meta = json.loads((self.rundir / "meta.json").read_text(encoding="utf-8"))
        self.assertIsNone(meta["prompt_ts"])
        self.assertIsNone(meta["t0"])


if __name__ == "__main__":
    unittest.main()
