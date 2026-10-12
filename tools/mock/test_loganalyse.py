#!/usr/bin/env python3
"""Regression tests for tools/loganalyse.sh — TOOL-01, TOOL-02, TOOL-03.

Stdlib unittest only -- pytest is NOT available in this environment.

Expected values are derived from the specification (BACKLOG §2.3), never
from running the script under test.

Assertions are scoped to the relevant output *section* (not the whole
stdout), because tokens such as the payload string "H19" or the
"replaced_by_prio=1" field legitimately appear verbatim in other sections
(OVERVIEW echoes the last raw line; RING_OVERFLOW/DROPPED_PACKETS echo raw
drop lines). A whole-stdout match would give false failures.

Run with:
    python3 -m unittest discover -s tools/mock -p 'test_loganalyse.py'
    python3 tools/mock/test_loganalyse.py
"""

from __future__ import annotations

import os
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

REPO_ROOT: Path = Path(__file__).resolve().parents[2]
SCRIPT: Path = REPO_ROOT / "tools" / "loganalyse.sh"
FIXTURE: Path = REPO_ROOT / "tools" / "testdata" / "loganalyse_sm_sample.txt"
CORRUPT_FIXTURE: Path = REPO_ROOT / "tools" / "testdata" / "loganalyse_corrupt_byte.txt"
RAW_FIXTURE: Path = REPO_ROOT / "tools" / "testdata" / "loganalyse_raw_sample.txt"
SAMPLE_440A: Path = REPO_ROOT / "tools" / "testdata" / "loganalyse_440a_sample.txt"


class TestLoganalyse(unittest.TestCase):
    """Shell-out tests for tools/loganalyse.sh against the serial-monitor
    sample fixture. The script is invoked once via setUpClass() to avoid
    redundant subprocess spawns.

    Fixture (loganalyse_sm_sample.txt) contains, by construction:
      - 4 benign rc=0 MC-SM transitions
      - 2 CSMA-backoff lines:  [MC-SM] TX_PREPARE -> IDLE rc=-1
      - 1 genuine fault:       [MC-SM] TX_ACTIVE -> TX_DONE rc=-2
      - 1 [MC-STAT] line (unlocks the PRIORITY_DISTRIBUTION drop breakdown)
      - 1 RING_DROP_PRIO  prio=5 type=40 replaced_by_prio=1
      - 1 RING_DROP_NEW   prio=5 type=40
      - 1 MH-LoRa line with real hop H02 and payload telemetry token H19
    """

    _stdout: str = ""
    _stderr: str = ""

    @classmethod
    def setUpClass(cls) -> None:
        result: subprocess.CompletedProcess[str] = subprocess.run(
            ["bash", str(SCRIPT), str(FIXTURE)],
            capture_output=True,
            text=True,
            cwd=str(REPO_ROOT),
        )
        cls._stdout = result.stdout
        cls._stderr = result.stderr

    def _slice(self, start_marker: str, stop_prefixes: tuple[str, ...]) -> str:
        """Return the output block beginning at the line containing
        start_marker, up to (but excluding) the next line whose start
        matches any of stop_prefixes."""
        out: list[str] = []
        capturing = False
        for line in self._stdout.splitlines():
            if not capturing:
                if start_marker in line:
                    capturing = True
                continue
            if any(line.startswith(p) for p in stop_prefixes):
                break
            out.append(line)
        return "\n".join(out)

    # ------------------------------------------------------------------
    # TOOL-01: rc=-1 is CSMA back-off; rc=-2 is a genuine SM error.
    # The corrected script splits them into separate counters.
    # Fixture: 2x rc=-1, 1x rc=-2  =>  MC_SM_CSMA_BACKOFF: 2, MC_SM_ERRORS: 1
    # (MC_SM_ERRORS / MC_SM_CSMA_BACKOFF are unique tokens -> whole-stdout ok.)
    # ------------------------------------------------------------------

    def test_tool01_csma_backoff_split(self) -> None:
        stdout = self._stdout
        self.assertIn(
            "MC_SM_ERRORS: 1",
            stdout,
            msg=f"Expected 'MC_SM_ERRORS: 1' (only the rc=-2 fault).\nstdout:\n{stdout}",
        )
        self.assertIn(
            "MC_SM_CSMA_BACKOFF: 2",
            stdout,
            msg=f"Expected 'MC_SM_CSMA_BACKOFF: 2' (the two rc=-1 backoffs).\nstdout:\n{stdout}",
        )

    # ------------------------------------------------------------------
    # TOOL-02: drop breakdown groups RING_DROP_PRIO and RING_DROP_NEW by the
    # DROPPED packet's own prio+type. replaced_by_prio must NOT leak in.
    # Fixture: both drops prio=5 type=40 => combined count 2; replaced_by_prio=1
    # must not appear as a prio=1 breakdown entry.
    # Scoped to the "Dropped packets by prio+type" block only.
    # ------------------------------------------------------------------

    def test_tool02_drop_breakdown_by_prio_type(self) -> None:
        self.assertIn(
            "Dropped packets by prio+type",
            self._stdout,
            msg=f"Expected the 'Dropped packets by prio+type' block.\nstdout:\n{self._stdout}",
        )
        block = self._slice(
            "--- Dropped packets by prio+type ---", ("--- ", "=== ")
        )
        self.assertRegex(
            block,
            r"(?m)^\s*2\s+prio=5 type=40\s*$",
            msg=(
                "Expected 'prio=5 type=40' counted 2 (RING_DROP_PRIO + "
                f"RING_DROP_NEW) in the breakdown block.\nblock:\n{block}"
            ),
        )
        self.assertNotIn(
            "prio=1",
            block,
            msg=(
                "replaced_by_prio=1 leaked into the drop breakdown "
                f"(double-count bug).\nblock:\n{block}"
            ),
        )

    # ------------------------------------------------------------------
    # TOOL-03: hop extracted from the positional field, not payload tokens.
    # Fixture MH-LoRa line: real hop H02, payload telemetry token H19.
    # Scoped to the HOP_DISTRIBUTION section (H19 also appears in OVERVIEW's
    # echoed raw last line, which must not count).
    # ------------------------------------------------------------------

    def test_tool03_hop_excludes_payload(self) -> None:
        hop = self._slice("=== HOP_DISTRIBUTION ===", ("=== ",))
        self.assertIn(
            "H02",
            hop,
            msg=f"Expected hop 'H02' in HOP_DISTRIBUTION.\nsection:\n{hop}",
        )
        self.assertNotIn(
            "H19",
            hop,
            msg=(
                "H19 is a payload telemetry token, not a hop; it must not "
                f"appear in HOP_DISTRIBUTION.\nsection:\n{hop}"
            ),
        )

    # ------------------------------------------------------------------
    # TOOL-04: stray non-UTF8 bytes must be stripped -- not passed through,
    # and not aborting awk mid-file (which silently truncates later sections).
    # Fixture: a 0xA0 byte on line 1, distinctive callsign DK9ZZ-7 after it.
    # Captured as bytes because the pre-fix output may contain the raw byte.
    # ------------------------------------------------------------------

    def test_tool04_sanitizes_corrupt_bytes(self) -> None:
        result = subprocess.run(
            ["bash", str(SCRIPT), str(CORRUPT_FIXTURE)],
            capture_output=True,
            cwd=str(REPO_ROOT),
        )
        self.assertNotIn(
            b"\xa0",
            result.stdout,
            msg="The 0xA0 corruption byte must be stripped from the output.",
        )
        text = result.stdout.decode("utf-8", "replace")
        self.assertIn(
            "DK9ZZ-7",
            text,
            msg="Lines after the corrupt byte must still be processed (no awk truncation).",
        )

    # ------------------------------------------------------------------
    # TOOL-05: raw firmware timestamps [EPOCHDAY.HHh.MMm.SSs.mmm] must be
    # auto-detected and converted to serial_monitor format so all
    # time-based sections parse. Epoch day 20688 -> 2026-08-23.
    # ------------------------------------------------------------------

    def test_tool05_converts_raw_bracket_format(self) -> None:
        result = subprocess.run(
            ["bash", str(SCRIPT), str(RAW_FIXTURE)],
            capture_output=True,
            text=True,
            cwd=str(REPO_ROOT),
        )
        out = result.stdout
        self.assertIn(
            "2026-08-23",
            out,
            msg="Raw [EPOCHDAY.HHh...] timestamps must convert (day 20688 -> 2026-08-23).",
        )
        self.assertIn(
            "MC_SM_CSMA_BACKOFF: 2",
            out,
            msg="After conversion, downstream sections must parse (2 backoff lines).",
        )


class TestLoganalyse440a(unittest.TestCase):
    """Wave-1 correctness tests against loganalyse_440a_sample.txt, a cut of
    a real 4.40a ESP32 console capture plus a few minimal lines in the exact
    src format (cited below) for markers absent from the real capture.

    Firmware print formats (authoritative emitters):
      - packet lines: printBuffer_aprs, src/loop_functions.cpp
        "%s %s %03i %c x%08X ..." -> `MH-LoRa 085 @ x58581370 ...`, no colon
        after the tag since c78adcc5. ACK frames are `TX-Lora ` (lowercase),
        src/lora_functions.cpp ~3064.
      - RX_IRQ_STALE_EARLY: src/esp32/esp32_main.cpp:3008
      - CAD_ABORT_BY_RX: src/lora_functions.cpp:886
      - [MC-WDT] TX_WATCHDOG fired: src/esp32/esp32_main.cpp:2931
      - RING_OVERFLOW buf=tx / buf=udp: src/txring_functions.cpp:886,
        src/udp_functions.cpp:1306
      - [HEAP];ts;cur;min;max_alloc;(mon): src/esp32/loop_actions_esp32.cpp:111;
        [HEAP]Cnew/Cdis: src/web_functions/web_functions.cpp:863,1158
      - [WIFI];event;* / [WIFI];stall;*: src/udp_functions.cpp:333,383-395
      - RX_BUF_OVERWRITE: src/lora_functions.cpp:859; RING_REJECT:
        src/txring_functions.cpp:655
      - RETRANSMIT*/ACK_*: src/lora_functions.cpp:511,522,985,1047,3288-3309

    Expected literals were counted on the fixture with /usr/bin/grep -cE
    (pattern in the comment next to each value).
    """

    _stdout: str = ""
    _stderr: str = ""
    _rc: int = 0

    @classmethod
    def setUpClass(cls) -> None:
        result = subprocess.run(
            ["bash", str(SCRIPT), str(SAMPLE_440A)],
            capture_output=True,
            text=True,
            cwd=str(REPO_ROOT),
        )
        cls._stdout = result.stdout
        cls._stderr = result.stderr
        cls._rc = result.returncode

    def _sec(self, name: str) -> str:
        """Output block of `=== name ===` up to the next `=== ` marker."""
        out: list[str] = []
        capturing = False
        for line in self._stdout.splitlines():
            if capturing and line.startswith("=== "):
                break
            if capturing:
                out.append(line)
            elif line.strip() == f"=== {name} ===":
                capturing = True
        return "\n".join(out)

    def _kv(self, block: str, key: str) -> str:
        m = re.search(rf"(?m)^\s*{re.escape(key)}:\s*(\S+)", block)
        self.assertIsNotNone(m, msg=f"{key!r} not found in:\n{block}")
        assert m is not None
        return m.group(1)

    def test_clean_run(self) -> None:
        self.assertEqual(self._rc, 0)
        self.assertEqual(self._stderr, "")

    # F-01: tag without colon. grep -cE 'MH-LoRa:? ' = 31
    def test_f01_mh_packets_and_nodes(self) -> None:
        nodes = self._sec("NODES")
        self.assertEqual(self._kv(nodes, "TOTAL_MH_PACKETS"), "31")
        # distinct originators: first callsign of the route field = 17
        self.assertEqual(self._kv(nodes, "UNIQUE_NODES"), "17")

    # MESSAGE_TYPES by payload type field (awk over MH-LoRa lines, token after
    # the length): 9 '@', 13 '!', 9 ':', 0 other; total 31.
    def test_f01_message_types(self) -> None:
        mt = self._sec("MESSAGE_TYPES")
        self.assertRegex(mt, r"HEARTBEAT: 9 ")
        self.assertRegex(mt, r"POSITION: 13 ")
        self.assertRegex(mt, r"TEXT: 9 ")
        self.assertRegex(mt, r"OTHER: 0 ")
        self.assertEqual(self._kv(mt, "TOTAL"), "31")

    # F-04: grep -E 'RX-LoRa2:? +[0-9]{3} :' | grep -vE 'H@R|HG@R' = 4 text lines
    def test_f04_notable_texts_from_rx_lora2(self) -> None:
        block = self._slice_after("--- NOTABLE_TEXTS ---")
        lines = [ln for ln in block.splitlines() if ln.strip()]
        self.assertEqual(len(lines), 4, msg=block)
        self.assertTrue(all("RX-LoRa2" in ln for ln in lines), msg=block)

    def _slice_after(self, marker: str) -> str:
        out: list[str] = []
        capturing = False
        for line in self._stdout.splitlines():
            if capturing and (line.startswith("--- ") or line.startswith("=== ")):
                break
            if capturing:
                out.append(line)
            elif marker in line:
                capturing = True
        return "\n".join(out)

    # F-01 hop sections: MH 31 lines all have a hop; RX-LoRa2 17; TX 11.
    # F-02: grep -cE 'TX-LoRa:? +[0-9]{3} ' = 11 (hops: 3xH00 4xH01 3xH02 1xH03);
    # grep -cE 'TX-Lora +[0-9]{3} ' = 3 ACK frames, counted separately.
    def test_f02_tx_lora_counts(self) -> None:
        hop = self._sec("HOP_DISTRIBUTION")
        tx = hop.split("--- TX-LoRa (Gesendet) ---")[1]
        counts = [int(x) for x in re.findall(r"(?m)^\s*(\d+)\s+H\d\d\s*$", tx)]
        self.assertEqual(sum(counts), 11, msg=tx)
        rx = hop.split("--- RX-LoRa2 (Akzeptiert) ---")[1].split("--- TX-LoRa")[0]
        self.assertEqual(
            sum(int(x) for x in re.findall(r"(?m)^\s*(\d+)\s+H\d\d\s*$", rx)), 17
        )
        self.assertIn("TOTAL_TX: 11", self._stdout)
        self.assertIn("TX_ACK_FRAMES (TX-Lora): 3", self._stdout)

    # F-03: S0/S1 is the server flag, not own-vs-relay. Must not be labelled so.
    # grep -E 'TX-LoRa:? +[0-9]{3} ' | grep -c ' S0 ' = 1, ' S1 ' = 10
    def test_f03_server_flag_not_own_relay(self) -> None:
        self.assertNotIn("OWN_ORIGIN", self._stdout)
        self.assertNotIn("OWN_RELAY", self._stdout)
        self.assertNotIn("RELAY_RATIO", self._stdout)
        self.assertIn("TX_SERVER_FLAG_S0: 1", self._stdout)
        self.assertIn("TX_SERVER_FLAG_S1: 10", self._stdout)

    # F-06: grep -c 'RX_IRQ_STALE rssi=' = 4; 'RX_IRQ_STALE_EARLY' = 2;
    # 'RX_IRQ_STALE forced restart' = 4. Three separate counters.
    def test_f06_rx_irq_stale_split(self) -> None:
        d = self._sec("DROPPED_PACKETS")
        self.assertEqual(self._kv(d, "RX_IRQ_STALE"), "4")
        self.assertEqual(self._kv(d, "RX_IRQ_STALE_EARLY"), "2")
        self.assertEqual(self._kv(d, "RX_IRQ_STALE_FORCED_RESTART"), "4")

    # F-19: grep -c CAD_BUSY_1 = 3; 'CAD_BUSY attempt=' = 1; CAD_FALSE_POSITIVE = 2
    def test_f19_cad_busy_split(self) -> None:
        add = self._sec("ADDITIONAL")
        self.assertEqual(self._kv(add, "CAD_BUSY_1"), "3")
        self.assertEqual(self._kv(add, "CAD_BUSY_CONFIRMED"), "1")
        self.assertEqual(self._kv(add, "CAD_FALSE_POSITIVE"), "2")
        self.assertEqual(self._kv(add, "CAD_ABORT_BY_RX"), "2")
        enh = self._sec("CAD_ENHANCED") or self._stdout.split("=== CAD_ENHANCED ===")[1]
        self.assertEqual(self._kv(enh, "CAD_BUSY_1"), "3")
        self.assertEqual(self._kv(enh, "CAD_BUSY_CONFIRMED"), "1")
        # false-positive rate = 2 / 3 = 66.7 %
        self.assertRegex(enh, r"66[.,]7%")

    # F-17 / rework 2: CRASH_COUNT = RESET_REASON lines with a crash reason.
    # Fixture: RESET_REASON=1 POWERON, =3 SW (benign), =6 TASK_WDT (1 crash);
    # plus noise that must NOT count: CAD_ABORT_BY_RX (2), [MC-WDT]
    # TX_WATCHDOG (1), "Rebooting..." (every OTA), "rst:0x" ROM banner,
    # Guru Meditation + Backtrace (listed as samples only).
    def test_f17_crash_count_ignores_cad_abort_and_wdt(self) -> None:
        add = self._sec("ADDITIONAL")
        self.assertEqual(self._kv(add, "CRASH_COUNT"), "1")
        self.assertEqual(self._kv(add, "TX_WATCHDOG_FIRED"), "1")

    def test_crash_histogram_and_samples(self) -> None:
        add = self._sec("ADDITIONAL")
        self.assertRegex(add, r"(?m)^\s*1 RESET_REASON=6 TASK_WDT\s*$")
        self.assertRegex(add, r"(?m)^\s*1 RESET_REASON=3 SW\s*$")
        self.assertRegex(add, r"(?m)^\s*1 RESET_REASON=1 POWERON\s*$")
        crashes = add.split("--- CRASHES ---")[1].split("TX_WATCHDOG_FIRED")[0]
        self.assertIn("Guru Meditation", crashes)
        self.assertIn("Backtrace", crashes)
        self.assertNotIn("Rebooting", crashes)
        self.assertNotIn("rst:0x", crashes)

    # F-11: grep -c 'RX_DEDUP_DUP msg_id=' = 17; grep -c RX_DEDUP_NEW = 21
    def test_f11_dedup(self) -> None:
        dd = self._sec("DEDUP")
        self.assertEqual(self._kv(dd, "DEDUP_EXPLICIT"), "17")
        self.assertEqual(self._kv(dd, "RX_DEDUP_NEW"), "21")
        self.assertEqual(self._kv(dd, "MH_LORA_HEARD"), "31")
        self.assertEqual(self._kv(dd, "RX_LORA_ACCEPTED"), "17")

    # F-10: grep -c 'RING_OVERFLOW buf=tx' = 1, 'buf=udp' = 1
    def test_f10_ring_overflow_by_buf(self) -> None:
        ro = self._sec("RING_OVERFLOW")
        self.assertRegex(ro, r"(?m)^\s*1 RING_OVERFLOW buf=tx\s*$")
        self.assertRegex(ro, r"(?m)^\s*1 RING_OVERFLOW buf=udp\s*$")

    # F-18 / rework 1: all three [HEAP] forms are samples ([HEAP]Cnew / Cdis
    # excluded): CSV `[HEAP];ts;f;min;max;(tag)` x3, default space form
    # `[HEAP] ts f min max (tag)` x1, nRF52 `ts;[HEAP];f;min;max;(tag)` x1.
    # max_alloc fields: 110580, 110580, 98304, 90112 (space), 120000 (nRF52).
    def test_f18_heap(self) -> None:
        h = self._sec("HEAP_MONITORING")
        self.assertIn("SAMPLES: 5 ", h)
        self.assertRegex(h, r"MAX_ALLOC_BLOCK: MIN=90112\s+MAX=120000")
        self.assertRegex(h, r"FREE_HEAP: MIN=212900 ")
        self.assertNotIn("HIGH_WATERMARK", h)

    def test_heap_samples_count_all_forms(self) -> None:
        add = self._sec("ADDITIONAL")
        self.assertEqual(self._kv(add, "HEAP_SAMPLES"), "5")

    # F-16: events disconnected/got_ip/stall = 1/1/1; "Client disconnected"
    # (web client) is not a WiFi event.
    def test_f16_wifi_events(self) -> None:
        s = self._sec("SERVER_CONNECTION")
        self.assertRegex(s, r"WIFI_CONNECT: 1\b")
        self.assertRegex(s, r"WIFI_DISCONNECT: 1\b")
        self.assertRegex(s, r"WIFI_STALL: 1\b")
        self.assertNotIn("CLIENT_DISCONNECT", s)

    # F-21: relays = commas before '>'; relays > 3 (i.e. > 4 callsigns) = 5
    def test_f21_relays(self) -> None:
        d = self._sec("DROPPED_PACKETS")
        self.assertIn("RELAYS_GT_3: 5", d)
        self.assertNotIn("PATH_LENGTH_GT_4", self._stdout)
        hh = self._sec("HIGH_HOP_PACKETS")
        self.assertEqual(len(re.findall(r"relays=\d+", hh)), 5, msg=hh)

    # F-07/08/22: dead markers must no longer be reported.
    def test_dead_markers_removed(self) -> None:
        for marker in (
            "ACK_FAST_QUEUED",
            "ACK_FAST_TX",
            "ACK_RX_CANCEL",
            "GW_ACK_DEDUP",
            "ACK_FWD_DEDUP",
            "ACK_SLOT_SKIP",
            "ACK_TIMEOUT",
            "CAD_GIVEUP",
            "EST_WAIT_MS",
            "ack_qlen",
        ):
            self.assertNotIn(marker, self._stdout, msg=marker)

    # Live ACK markers (one line each in the fixture tail):
    # ACK_RECEIVED retid= 1, ACK_SKIP_READY slot= 1, RETRANSMIT retry= 1,
    # RETRANSMIT_GIVEUP retries= 1
    def test_live_ack_markers(self) -> None:
        a = self._sec("ACK_ANALYSIS")
        self.assertEqual(self._kv(a, "ACK_RECEIVED"), "1")
        self.assertEqual(self._kv(a, "ACK_SKIP_READY"), "1")
        m = self._sec("MISSING_ACKS")
        self.assertEqual(self._kv(m, "RETRANSMIT"), "1")
        self.assertEqual(self._kv(m, "RETRANSMIT_GIVEUP"), "1")

    # BUFFER_DROPS: RING_DROP_|RING_REJECT|RX_BUF_OVERWRITE|CAPTURE_DROPPED
    # fixture: 1 RING_REJECT + 1 RX_BUF_OVERWRITE = 2
    def test_buffer_drops(self) -> None:
        add = self._sec("ADDITIONAL")
        self.assertEqual(self._kv(add, "BUFFER_DROPS"), "2")

    # Temp files: no fixed /tmp paths, and everything under TMPDIR is cleaned.
    def test_no_fixed_tmp_and_cleanup(self) -> None:
        fixed = Path("/tmp/channel_util.txt")
        existed = fixed.exists()
        with tempfile.TemporaryDirectory() as td:
            subprocess.run(
                ["bash", str(SCRIPT), str(SAMPLE_440A)],
                capture_output=True,
                text=True,
                cwd=str(REPO_ROOT),
                env={**os.environ, "TMPDIR": td},
            )
            self.assertEqual(os.listdir(td), [])
        if not existed:
            self.assertFalse(fixed.exists())

    # Backward compatibility: pre-c78adcc5 colon form must still be counted.
    def test_legacy_colon_form(self) -> None:
        legacy = (
            "2026-08-23 10:00:00.100  MH-LoRa: 081 @ xAABBCCDD H02 S1 T0 M01 "
            "DB0XYZ-1,DL1AAA-2,DL1BBB-3>H@R2;6,121,-9; HW:43 MOD:8/8 FCS:11E7 FW:35:p LH:AA\n"
            "2026-08-23 10:00:01.100  RX-LoRa2: 090 : xAABBCC01 H02 S1 T0 M01 "
            "DK5XX-1>9:hello HW:43 MOD:8/8 FCS:11E7 FW:35:p LH:AA\n"
            "2026-08-23 10:00:02.100  TX-LoRa: 075 @ xAABBCCDD H01 S0 T0 M01 "
            "DB0XYZ-1>H@R2; HW:43 MOD:8/8 FCS:11E7 FW:35:p LH:AA\n"
            "2026-08-23 10:00:03.100  NEW-TXT: 059 : xEA25A136 H04 S0 T0 M00 "
            "DK5EN-1>9:#1173 test HW:43 MOD:8/8 FCS:0EDA FW:35:u LH:AB\n"
        )
        with tempfile.TemporaryDirectory() as td:
            lf = Path(td) / "legacy.txt"
            lf.write_text(legacy)
            out = subprocess.run(
                ["bash", str(SCRIPT), str(lf)],
                capture_output=True,
                text=True,
                cwd=str(REPO_ROOT),
            ).stdout
        self.assertIn("TOTAL_MH_PACKETS: 1", out)
        self.assertIn("UNIQUE_NODES: 1", out)
        self.assertIn("RX_LORA_ACCEPTED: 1", out)
        self.assertIn("TOTAL_TX: 1", out)
        self.assertIn("RX-LoRa2: 090 : xAABBC", out.split("--- NOTABLE_TEXTS ---")[1])
        # route DB0XYZ-1,DL1AAA-2,DL1BBB-3 -> 2 relays
        summ = out.split("--- PATH_LENGTH_SUMMARY ---")[1]
        self.assertRegex(summ, r"(?m)^2\s+1\s+100[.,]0\s*$")
        self.assertIn("NEW-TXT", out)


class TestLoganalyseRework(unittest.TestCase):
    """Wave-1 rework: small synthetic logs, one behaviour per test. Every log
    line uses the exact firmware format (timestamp prefix added by the
    capture tool, printBuffer_aprs packet lines)."""

    @staticmethod
    def _analyse(text: str, *extra: str) -> str:
        with tempfile.TemporaryDirectory() as td:
            lf = Path(td) / "log.txt"
            lf.write_text(text)
            res = subprocess.run(
                ["bash", str(SCRIPT), str(lf), *extra],
                capture_output=True,
                text=True,
                cwd=str(REPO_ROOT),
            )
        assert res.returncode == 0, res.stderr
        assert res.stderr == "", res.stderr
        return res.stdout

    @staticmethod
    def _pkt(i: int, tag: str, ptype: str, route: str, payload: str, sec: int = 0) -> str:
        """Packet line in the 4.40a format (no colon after the tag)."""
        hh, mm, ss = 10, sec // 60, sec % 60
        return (
            f"2026-10-01 {hh:02d}:{mm:02d}:{ss:02d}.100  {hh:02d}:{mm:02d}:{ss:02d} "
            f"{tag} 060 {ptype} xA{i:07X} H01 S1 T0 M01 {route}{payload} "
            f"HW:43 MOD:8/8 FCS:11E7 FW:35:p LH:AA\n"
        )

    @staticmethod
    def _line(sec: int, body: str) -> str:
        return f"2026-10-01 10:{sec // 60:02d}:{sec % 60:02d}.100  {body}\n"

    def _vals(self, out: str, key: str) -> list[str]:
        return re.findall(rf"(?m)^\s*{re.escape(key)}:\s*(\S+)", out)

    # 2: fallback only when the log has no RESET_REASON line at all
    def test_crash_fallback_without_reset_reason(self) -> None:
        log = (
            self._line(1, "Guru Meditation Error: Core  1 panic'ed (LoadProhibited)")
            + self._line(2, "Backtrace: 0x400d1234:0x3ffb1f50")
            + self._line(3, "abort() was called at PC 0x400d0000 on core 1")
            + self._line(4, "Task watchdog got triggered. The following tasks did not reset")
            + self._line(5, "Rebooting...")
            + self._line(6, "rst:0x3 (SW_RESET),boot:0x13 (SPI_FAST_FLASH_BOOT)")
            + self._line(7, "[MC-DBG] CAD_ABORT_BY_RX")
        )
        out = self._analyse(log)
        self.assertEqual(self._vals(out, "CRASH_COUNT"), ["3"])

    def test_crash_reset_reason_present_disables_fallback(self) -> None:
        log = (
            self._line(1, "[BOOT] RESET_REASON=1 POWERON")
            + self._line(2, "Guru Meditation Error: Core  1 panic'ed (LoadProhibited)")
            + self._line(3, "Rebooting...")
            + self._line(4, "rst:0x1 (POWERON_RESET),boot:0x13 (SPI_FAST_FLASH_BOOT)")
        )
        out = self._analyse(log)
        self.assertEqual(self._vals(out, "CRASH_COUNT"), ["0"])

    def test_crash_counts_each_crash_reason(self) -> None:
        log = "".join(
            self._line(i, f"[BOOT] RESET_REASON={n} {name}")
            for i, (n, name) in enumerate(
                [(4, "PANIC"), (5, "INT_WDT"), (6, "TASK_WDT"), (7, "WDT"), (15, "BROWNOUT"), (3, "SW"), (8, "DEEPSLEEP")],
                start=1,
            )
        )
        out = self._analyse(log)
        self.assertEqual(self._vals(out, "CRASH_COUNT"), ["5"])

    # 3: WIFI_ISSUE_COUNT counts events, not every line mentioning disconnect
    def test_wifi_issue_count_events_only(self) -> None:
        log = (
            self._line(1, "[WIFI];event;disconnected;reason;8;ms;1000")
            + self._line(2, "[WIFI];assoc;disconnected;ssid;X;chan;3;reason;8;ms;1000")
            + self._line(3, "[SAFEBOOT];wifi;event;disconnected;reason;8")
            + self._line(4, "[WIFI-DBG] WiFi disconnected, flags reset for reconnect")
            + self._line(5, "[WIFI];stall;udp_loop;ms;850;task;loopTask")
            + self._line(6, "[WIFI];event;got_ip;ms;2000")
            + self._line(7, "Client disconnected.")
            + self._pkt(1, "NEW-TXT", ":", "DK5EN-1>", "9:wifi disconnected again", 8)
            + self._pkt(2, "MH-LoRa", ":", "DK5EN-1>", "*:reconnect wifi fail", 9)
        )
        out = self._analyse(log)
        self.assertEqual(self._vals(out, "WIFI_ISSUE_COUNT"), ["2"])
        self.assertEqual(self._vals(out, "WIFI_DISCONNECT"), ["1"])

    def test_wifi_issue_count_legacy_without_event_lines(self) -> None:
        log = (
            self._line(1, "[WIFI-DBG] WiFi disconnected, flags reset for reconnect")
            + self._line(2, "[WIFI]...disconnect")
            + self._line(3, "Client disconnected.")
            + self._line(4, "[LOGGER] end (finished: signal) lines=10 bytes=100 reconnects=0")
            + self._pkt(1, "RX-LoRa2", ":", "DK5EN-1>", "9:wifi disconnected again", 4)
        )
        out = self._analyse(log)
        self.assertEqual(self._vals(out, "WIFI_ISSUE_COUNT"), ["2"])

    # 4: path tables print in relay order, header first, trailer last
    def _path_log(self) -> str:
        log = ""
        for n, relays in enumerate([0, 0, 1, 2, 2, 2, 10, 11], start=1):
            route = ",".join(f"DL1A{k}-{k + 1}" for k in range(relays + 1)) + ">"
            log += self._pkt(n, "MH-LoRa", "@", route, "HG@R2;9,103,0;", n)
        return log

    def test_path_length_summary_order(self) -> None:
        out = self._analyse(self._path_log())
        blk = out.split("--- PATH_LENGTH_SUMMARY ---\n")[1].splitlines()
        self.assertTrue(blk[0].split()[0] == "RELAYS", msg=blk)
        rows = []
        for ln in blk[1:]:
            if ln.startswith("TOTAL:"):
                break
            rows.append(int(ln.split()[0]))
        self.assertEqual(rows, [0, 1, 2, 10, 11])
        self.assertEqual(blk[1 + len(rows)], "TOTAL: 8")

    def test_path_length_distribution_order(self) -> None:
        out = self._analyse(self._path_log())
        blk = out.split("--- PATH_LENGTH_DISTRIBUTION ---\n")[1].splitlines()
        self.assertEqual(blk[0].split()[:2], ["RELAYS", "COUNT"], msg=blk)
        rows = []
        for ln in blk[1:]:
            if not ln.strip() or ln.startswith("RELAYS_GT_3"):
                break
            rows.append(int(ln.split()[0]))
        self.assertEqual(rows, [0, 1, 2, 10, 11])
        self.assertIn("RELAYS_GT_3: 2", blk)

    # 5: message type = payload type field only
    def _types_log(self) -> str:
        return (
            self._pkt(1, "MH-LoRa", "@", "AA1AA-1,BB1BB-2>", "HG@R10;1,2,3;", 1)
            + self._pkt(2, "MH-LoRa", "@", "AA1AA-1,BB1BB-2>", "HN@R1;4,5,6;", 2)
            + self._pkt(3, "MH-LoRa", "!", "AA1AA-1>", "*!48.1/11.2", 3)
            + self._pkt(4, "MH-LoRa", ":", "AA1AA-1>", "*:look at H@R and *! here", 4)
            + self._pkt(5, "MH-LoRa", ":", "AA1AA-1>", "9:wow *! done", 5)
            + self._pkt(6, "MH-LoRa", "A", "AA1AA-1>", "9:ack", 6)
            + self._pkt(7, "MH-LoRa", ":", "A1-1,B2-2,C3-3,D4-4,E5-5,F6-6>", "*:H@R in text", 7)
        )

    def test_message_types_by_payload_type_field(self) -> None:
        out = self._analyse(self._types_log())
        mt = out.split("=== MESSAGE_TYPES ===")[1].split("--- NOTABLE_TEXTS ---")[0]
        self.assertRegex(mt, r"HEARTBEAT: 2 ")
        self.assertRegex(mt, r"POSITION: 1 ")
        self.assertRegex(mt, r"TEXT: 3 ")
        self.assertRegex(mt, r"OTHER: 1 ")
        self.assertRegex(mt, r"TOTAL: 7")

    def test_high_hop_type_by_payload_type_field(self) -> None:
        out = self._analyse(self._types_log())
        hh = out.split("=== HIGH_HOP_PACKETS ===")[1].split("--- PATH_LENGTH_SUMMARY ---")[0]
        self.assertEqual(len(re.findall(r"relays=5 ", hh)), 1, msg=hh)
        self.assertIn("type=TXT", hh)
        self.assertNotIn("type=HB", hh)

    # 6: RETRANSMIT_GIVEUP does not include RETRANSMIT_GIVEUP_DM
    def test_retransmit_giveup_not_dm(self) -> None:
        log = (
            self._line(1, "[MC-DBG] RETRANSMIT_GIVEUP retries=3 msg_id=EA25A136")
            + self._line(2, "[MC-DBG] RETRANSMIT_GIVEUP_DM msg_id=EA25A137 dst=DK5EN-1")
        )
        out = self._analyse(log)
        vals = self._vals(out, "RETRANSMIT_GIVEUP")
        self.assertGreaterEqual(len(vals), 2, msg=out)
        self.assertEqual(set(vals), {"1"}, msg=out)
        self.assertEqual(self._vals(out, "RETRANSMIT_GIVEUP_DM"), ["1"])

    # 7: one UDP reset = one count (firmware: udp_functions.cpp:1280 prints
    # once per resetMeshComUDP(); udp_drain_esp32.cpp:107 precedes it)
    def test_udp_reset_counted_once_per_reset(self) -> None:
        log = (
            self._line(1, "[WIFI-DBG] UDP TX error limit (5) reached, calling resetMeshComUDP")
            + self._line(2, "[WIFI-DBG] resetMeshComUDP: WiFi disconnected, flags reset for reconnect")
            + self._line(3, "[UDP] WiFi down - resetting")
            + self._line(4, "[WIFI-DBG] resetMeshComUDP: WiFi disconnected, flags reset for reconnect")
            + self._pkt(1, "RX-LoRa2", ":", "DK5EN-1>", "9:UDP reset test", 5)
        )
        out = self._analyse(log)
        self.assertEqual(self._vals(out, "UDP_RESET"), ["2"])

    # 8: BLE latency is measured to the FIRST TX, not a later retransmission
    def test_ble_latency_first_tx_only(self) -> None:
        log = (
            self._line(0, "[MC-DBG] RING_WRITE src=user_msg msg_id=0A000001 queued=1")
            + self._line(0, "00:00:00 NEW-TXT 059 : x0A000001 H04 S0 T0 M00 DK5EN-1>9:first HW:43 MOD:8/8 FCS:0EDA FW:35:u LH:AB")
            + self._line(2, "[MC-DBG] RING_TX_READ msg_id=0A000001 lat=1800")
            + self._line(3, "00:00:03 TX-LoRa  059 : x0A000001 H04 S0 T0 M00 DK5EN-1>9:first HW:43 MOD:8/8 FCS:0EDA FW:35:u LH:AB")
            + self._line(33, "00:00:33 TX-LoRa  059 : x0A000001 H04 S0 T0 M00 DK5EN-1>9:first HW:43 MOD:8/8 FCS:0EDA FW:35:u LH:AB")
        )
        out = self._analyse(log)
        row = [ln for ln in out.splitlines() if ln.startswith("0A000001")]
        self.assertEqual(len(row), 1, msg=out)
        self.assertRegex(row[0], r"\s3000ms\s*$")

    # nit: CROSS_CORRELATION prints the input names, not temp copies
    def test_cross_correlation_prints_input_names(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            a, b = Path(td) / "first.log", Path(td) / "second.log"
            a.write_text(self._pkt(1, "MH-LoRa", "@", "AA1AA-1>", "HG@R2;1,2,3;", 1))
            b.write_text(self._pkt(1, "MH-LoRa", "@", "AA1AA-1>", "HG@R2;1,2,3;", 2))
            res = subprocess.run(
                ["bash", str(SCRIPT), str(a), str(b)],
                capture_output=True,
                text=True,
                cwd=str(REPO_ROOT),
            )
        self.assertEqual(res.returncode, 0, msg=res.stderr)
        self.assertIn("LOG1: " + str(a), res.stdout)
        self.assertIn("LOG2: " + str(b), res.stdout)


if __name__ == "__main__":
    unittest.main()
