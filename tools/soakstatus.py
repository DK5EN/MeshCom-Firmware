#!/usr/bin/env python3
"""Status report for the 24h PN-XOR-retry soak (DK5EN-1 <-> DK5EN-98, feature-snf).

Usage::

    python3 tools/soakstatus.py --since "2026-09-27 17:39" [--until "..."] \\
        [--fetch] [--out report.md] [--json report.json]
    python3 tools/soakstatus.py --self-test

Reads two node captures (DK5EN-1's local USB serial log, DK5EN-98's net-console
log fetched from the rpizero logger) plus the sender log written by
``tools/bench/soak_dm.py``, and reports (every node is optional: ``--nodes DK5EN-98``
or ``--dk1-dir none`` evaluates a single-node soak, e.g. DK5EN-98 only):

  * per node: capture liveness (gaps between lines AND from --since to the
    first line / from the last line to --until or now; a node with no line in
    the window is reported as "capture missing"), reboots, heap drift, WiFi
    downs, GW keepalive count, TX count, RX errors, channel utilization,
    the summed DM setlog counters, [RETX]/[RX] marker counts, and NBR
    matrix CHECK violations (via ``tools/nbrlog.py``, called as a
    subprocess on a time-sliced copy of the log -- this script does not
    re-implement NBR parsing).
  * per test DM (from soak_dm.py's log): sender msg-id/NNN, transmissions
    (original + PNRETRY), ack yes/no + path (LoRa vs server/UDP) + latency,
    give-up yes/no; receiver: received yes/no, copy count. The ack path is the
    EARLIEST of the server ack (``[UDP-MSGID] ack_msg_id:<orig> ACK...02``) and the
    LoRa ack (``[ACK-MSGID] ack_msg_id:<orig>``); the ``[RETX]`` markers are only a
    fallback (they are not printed when the ack beats the first LoRa TX). Latency
    counts from the sender's own first TX line of that DM (the sender log only has
    whole seconds). Copies are real receptions: RX-UDP lines plus RF ``[LOG] NNN``
    reception lines (the MH-LoRa/RX-LoRa2 lines of the same reception do not count).

Both node logs share one host-timestamp convention, "YYYY-MM-DD HH:MM:SS.mmm"
followed by two spaces then the device's own line (see
``tools/bench/serial_capture.py`` and ``tools/meshlogger.py``). All log
locations are globbed and then time-filtered by that host timestamp, so
older, unrelated files sitting in the same directory (previous captures)
are excluded by the --since/--until window itself, not by filename.

The firmware's periodic "[LOG] DM sent=... " counters reset to zero on every
print (dm_stats.cpp: dmStatFormat() reads each counter via
``std::atomic::exchange(0)``) -- each line is a delta since the previous
print (every PRIO_STAT_INTERVAL_S = 300 s under --setlog on), not a
cumulative total. This script therefore SUMS all such lines across the
window rather than taking "the latest" one, which would only cover the
last five minutes of a 24 h run.

Stdlib only.
"""

from __future__ import annotations

import argparse
import gzip
import json
import re
import statistics
import subprocess
import sys
import tempfile
from collections import Counter
from dataclasses import dataclass, field
from datetime import datetime, timedelta
from pathlib import Path
from typing import Any

# --------------------------------------------------------------------------
# Constants -- fixed soak setup (docs/archive/soak-xor-20260927.md documents these).
# --------------------------------------------------------------------------

HOME = Path.home()
DEFAULT_DK1_DIR = HOME / "meshlog" / "dk5en-1"
DEFAULT_DK98_DIR = HOME / "meshlog" / "dk5en-98-local"
DEFAULT_SOAK_LOG = HOME / "meshlog" / "soak-dm-20260927.log"
DEFAULT_FETCH_HOST = "rpizero.local"
DEFAULT_FETCH_REMOTE = "meshlog/dk5en-98/"
NBRLOG_PATH = Path(__file__).resolve().parent / "nbrlog.py"

NODE_A = "DK5EN-1"
NODE_B = "DK5EN-98"

#: Host-timestamp prefix written by tools/bench/serial_capture.py and
#: tools/meshlogger.py: "YYYY-MM-DD HH:MM:SS.mmm" + two spaces + the line.
RE_PREFIX = re.compile(
    r"^(?P<host>\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3})\s\s(?P<rest>.*)$"
)

#: A dead capture must be caught: any gap between two consecutive host
#: timestamps longer than this counts as a liveness gap (matches
#: tools/nbrlog.py's GAP_THRESHOLD_S, so both tools agree on what a gap is).
GAP_THRESHOLD_S = 120.0
#: BLUF acceptance: no gap longer than this in either capture.
BLUF_GAP_MAX_S = 600.0
#: BLUF acceptance: last heap sample may drop at most this much below the
#: first sample taken after --since (heap "baseline").
HEAP_DRIFT_BUDGET_B = 1024
#: How far past a SEND we look in the sender/receiver logs for that DM's
#: frames (TX attempts, acks, give-up). The retry ladder is 3 x 40 s max
#: (src/lora_functions.cpp MAX_RETRANSMIT / threshold 0x15 ticks); minutes
#: of slack cover a slow store-and-forward custody path too.
DEFAULT_MATCH_WINDOW_MIN = 20.0

# --------------------------------------------------------------------------
# Firmware line markers -- each verified against src/ and a real capture
# (see the report's "markers" section for what was NOT found).
# --------------------------------------------------------------------------

# SL-05, src/setlog_lines.h + src/loop_functions.cpp setlogFillStat():
# "STAT util=<pct> rx=<ms> tx=<ms> newid=<n> dup=<n> err=<n> txn=<n>
#  txfail=<n> ringmax=<n>/<MAX> drop=.. mh=<n> heap=<bytes> trk=<s>/<n>
#  fw=<maj><sub>/<flash> up=<s> t=<ms>"
RE_STAT = re.compile(
    r"\[LOG\] STAT util=(?P<util>\d+) rx=(?P<rx>\d+) tx=(?P<tx>\d+) "
    r"newid=(?P<newid>\d+) dup=(?P<dup>\d+) err=(?P<err>\d+) txn=(?P<txn>\d+) "
    r"txfail=(?P<txfail>\d+) ringmax=(?P<ringmax>\d+)/(?P<ringsize>\d+) "
    r"drop=(?P<drop>[\d/]+) mh=(?P<mh>\d+) heap=(?P<heap>\d+)"
)

# dm_stats.cpp dmStatFormat() -- printed right after STAT, same --setlog
# gate. Every field is an interval counter that resets on read (exchange(0)):
# this line is a DELTA since the previous print, not a running total.
RE_DM = re.compile(
    r"\[LOG\] DM sent=(?P<sent>\d+) echo=(?P<echo>\d+) gwack=(?P<gwack>\d+) "
    r"ack=(?P<ack>\d+) giveup=(?P<giveup>\d+) giveuph=(?P<giveuph>\d+) "
    r"att=(?P<att>\d+) reack=(?P<reack>\d+)/(?P<reack_lim>\d+) "
    r"rtt=(?P<rtt>[\d/]+) ring=enq:(?P<enq>\d+) ovw:(?P<ovw>\d+)"
)

# src/lora_functions.cpp / esp32_main.cpp CHANNEL_UTIL debug line (bLORADEBUG).
RE_CHANNEL_UTIL = re.compile(r"CHANNEL_UTIL rx=(?P<rx>\d+)ms tx=(?P<tx>\d+)ms util=(?P<util>\d+)%")

# src/setlog_lines.h SL-04 / src/lora_functions.cpp: "[LOG] ERR rssi=.. ...".
RE_ERR_LINE = re.compile(r"\[LOG\] ERR ")

# src/setlog_lines.h SL-03 / src/loop_functions.cpp setlogFormatTx() call site:
# "[LOG] TX x<id> <typ> H<hop> prio=<n> src=<o|r|g> wait=<ms> q=<n> cad=<n>
#  len=<bytes> t=<ms>".
RE_TX_LOG = re.compile(r"\[LOG\] TX x(?P<msgid>[0-9A-Fa-f]{8})\b.*?\bsrc=(?P<src>[org])\b")

# src/udp_functions.cpp wifiLinkLog(): "[WIFI];link;down;...".
RE_WIFI_DOWN = re.compile(r"\[WIFI\];\w+;down;")
# src/udp_functions.cpp: "[GW];keep;tx;ok;..." -- no failure variant exists
# in this firmware (TM-39 comment: the ring-enqueue "ok" is unconditional,
# on-wire delivery is tracked separately by --udpstat). See report deviation.
RE_GW_KEEP = re.compile(r"\[GW\];keep;tx;ok;")

# [RETX]/[RX]/[MC-DBG] markers from the PN-XOR retry ladder
# (src/lora_functions.cpp, src/nrf52/udp_frame_nrf52.cpp,
# src/esp32/udp_frame_esp32.cpp). All gated by bDisplayRetx (--loradebug on
# also sets this) except PNREPEAT, which is bDisplayInfo.
RE_PNRETRY = re.compile(r"\[RETX\] PNRETRY k=(?P<k>\d+) msg-id:(?P<msgid>[0-9A-Fa-f]{8})")
RE_PN_ECHO_RESTART = re.compile(r"\[RETX\] PN echo, wait restarted retid:\d+")
RE_PN_ALREADY_ACKED = re.compile(
    r"\[RETX\] PN already acked, stop retid:(?P<retid>\d+) msg-id:(?P<msgid>[0-9A-Fa-f]{8})"
)
# Server/UDP-path ack: src/nrf52/udp_frame_nrf52.cpp + src/esp32/udp_frame_esp32.cpp.
RE_SERVER_ACK = re.compile(
    r"\[RETX\] server ACK for retid:(?P<retid>\d+) stop retransmit msg-id:(?P<msgid>[0-9A-Fa-f]{8})"
)
# Direct-LoRa-path ack: src/lora_functions.cpp (the destination's own ack,
# heard directly).
RE_LORA_ACK = re.compile(
    r"\[RETX\] DM-ACK for retid:(?P<retid>\d+) stop retransmit msg-id:(?P<msgid>[0-9A-Fa-f]{8})"
)
# Ack lines that exist whether or not the LoRa retry ladder printed its [RETX]
# markers: the server ack (udp_frame_*.cpp, "[UDP-MSGID] ack_msg_id:<orig> ACK...02")
# and the LoRa ack (lora_functions.cpp, glued behind the device clock:
# "17:54:36[ACK-MSGID] ack_msg_id:<orig>"). Both carry the ORIGINAL msg-id.
RE_UDP_ACK = re.compile(r"\[UDP-MSGID\]\s*ack_msg_id:(?P<msgid>[0-9A-Fa-f]{8})\b.*?\bACK\W*02")
RE_LORA_ACK_ID = re.compile(r"\[ACK-MSGID\]\s*ack_msg_id:(?P<msgid>[0-9A-Fa-f]{8})\b")
RE_PNREPEAT = re.compile(r"\[RX\] PNREPEAT msg-id:(?P<msgid>[0-9A-Fa-f]{8})")
RE_GIVEUP_DM = re.compile(r"\[MC-DBG\] RETRANSMIT_GIVEUP_DM msg_id=(?P<msgid>[0-9A-Fa-f]{8}) dest=(?P<dest>\S+)")

# Reboot: src/esp32/esp32_main.cpp setup() prints this once per boot,
# unconditionally (printlndeb("CLIENT SETUP")), followed a few lines later
# by "[BOOT] RESET_REASON=<n> <name>". Either alone in-window is a reboot;
# both together (as at a real boot) count once.
RE_CLIENT_SETUP = re.compile(r"^CLIENT SETUP$")
RE_BOOT_RESET = re.compile(r"\[BOOT\] RESET_REASON=")

# Own-frame TX lines that carry the payload text (used to correlate a test
# DM to its msg-id and NNN): NEW-TXT (src/loop_functions.cpp, own new
# message), TX-LoRa / TX-UDP (src/lora_functions.cpp, src/*/udp_drain_*.cpp,
# printBuffer_aprs on the outgoing path -- same msg-id as the frame that
# created it, incl. a PNRETRY-rewritten one).
RE_TX_TAG = re.compile(r"\b(NEW-TXT|TX-LoRa\d?|TX-UDP)\b")
# Receiver-side REAL receptions carrying the payload text: one RX-UDP line per
# server copy, one RF "[LOG] NNN : x<id> ..." line per LoRa reception. The
# MH-LoRa / RX-LoRa2 lines printed for the same LoRa reception are not extra
# copies and are not counted.
RE_RX_UDP = re.compile(r"\bRX-UDP\b")
RE_RX_RF = re.compile(r"\[LOG\]\s+\d+\s+\S+\s+x[0-9A-Fa-f]{8}\b")
# The msg-id embedded in every printBuffer_aprs line: " x<8 hex> ".
RE_LINE_MSGID = re.compile(r"\sx([0-9A-Fa-f]{8})\b")
# The APRS message-number suffix on an outgoing DM's payload -- no closing
# brace ("Text{NNN", not "Text{NNN}"; deliberately not zero-padding NNN in
# the regex, {NNN} widths vary in the field, e.g. "{804" vs "{28").
RE_NNN = re.compile(r"\{(\d{1,3})(?=[\s]|$)")

# soak_dm.py's own log: "<iso>  SEND <seq> <from> <to> <http> <answer>" and
# the START line carrying the run tag.
RE_SOAK_SEND = re.compile(
    r"^(?P<iso>\S+)\s+SEND (?P<seq>\d+) (?P<frm>\S+) (?P<to>\S+) (?P<http>\d+) (?P<answer>\S+)\s*$"
)
RE_SOAK_START = re.compile(r"^\S+\s+START tag=(?P<tag>\S+)")


# --------------------------------------------------------------------------
# Time parsing helpers
# --------------------------------------------------------------------------


def parse_when(text: str) -> datetime:
    """Parses --since/--until: "YYYY-MM-DD HH:MM[:SS[.ffffff]]" or ISO."""
    text = text.strip()
    for fmt in ("%Y-%m-%d %H:%M:%S.%f", "%Y-%m-%d %H:%M:%S", "%Y-%m-%d %H:%M", "%Y-%m-%d"):
        try:
            return datetime.strptime(text, fmt)
        except ValueError:
            continue
    try:
        return datetime.fromisoformat(text)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(f"unrecognized date/time: {text!r}") from exc


def dir_or_none(text: str) -> Path | None:
    """--dk1-dir / --dk98-dir: a directory, or "none" to leave that node out of the run."""
    return None if text.strip().lower() in ("none", "-", "") else Path(text)


def select_nodes(nodes_arg: str | None, dirs: dict[str, Path | None]) -> dict[str, Path]:
    """Nodes to evaluate -> capture directory. Default (both nodes, both dirs) is the two-node soak;
    ``--nodes DK5EN-98`` or ``--dk1-dir none`` narrows it. Raises ValueError on an unknown name or an
    empty selection."""
    wanted = list(dirs)
    if nodes_arg:
        by_upper = {n.upper(): n for n in dirs}
        wanted = []
        for tok in (t.strip() for t in nodes_arg.split(",") if t.strip()):
            if tok.upper() not in by_upper:
                raise ValueError(f"unknown node {tok!r} (choose from {', '.join(dirs)})")
            wanted.append(by_upper[tok.upper()])
    chosen = {n: d for n, d in dirs.items() if n in wanted and d is not None}
    if not chosen:
        raise ValueError("no node left to evaluate (--nodes / --dk1-dir / --dk98-dir)")
    return chosen


def fmt_dt(d: datetime) -> str:
    return d.strftime("%Y-%m-%d %H:%M:%S")


# --------------------------------------------------------------------------
# Log loading + time slicing
# --------------------------------------------------------------------------


def open_maybe_gz(path: Path):
    if path.suffix == ".gz":
        return gzip.open(path, "rt", encoding="utf-8", errors="replace")
    return open(path, "r", encoding="utf-8", errors="replace")


def collect_log_files(directory: Path) -> list[Path]:
    if not directory.is_dir():
        return []
    return sorted(p for p in directory.iterdir() if p.is_file() and p.suffix in (".log", ".gz"))


@dataclass
class SlicedLog:
    """The subset of one node's capture inside [since, until), in host-time order."""

    lines: list[tuple[datetime, str]] = field(default_factory=list)
    #: files that actually contributed at least one in-window line.
    files: list[str] = field(default_factory=list)
    #: files present in the directory but outside [since, until) entirely
    #: (older captures, or a not-yet-started rotation) -- listed by name only.
    files_skipped: list[str] = field(default_factory=list)
    #: lines whose prefix didn't match RE_PREFIX (informational only).
    unparsed: int = 0

    def write_sliced_copy(self, path: Path) -> None:
        """Writes the sliced lines back out with their original host prefix,
        for tools/nbrlog.py to read as a subprocess (never re-parsed here)."""
        with open(path, "w", encoding="utf-8") as fh:
            for host, rest in self.lines:
                fh.write(f"{host.strftime('%Y-%m-%d %H:%M:%S.%f')[:-3]}  {rest}\n")


def load_and_slice(directory: Path, since: datetime, until: datetime) -> SlicedLog:
    """Globs every *.log/*.gz in ``directory``, keeps lines with a host
    timestamp in [since, until). Unrelated older files in the same directory
    are excluded by their timestamps, not by filename -- rotation-safe."""
    out = SlicedLog()
    all_lines: list[tuple[datetime, str]] = []
    for path in collect_log_files(directory):
        contributed = 0
        with open_maybe_gz(path) as fh:
            for line in fh:
                line = line.rstrip("\n").rstrip("\r")
                if not line:
                    continue
                m = RE_PREFIX.match(line)
                if not m:
                    out.unparsed += 1
                    continue
                host = datetime.strptime(m.group("host"), "%Y-%m-%d %H:%M:%S.%f")
                if since <= host < until:
                    all_lines.append((host, m.group("rest")))
                    contributed += 1
        if contributed:
            out.files.append(str(path))
        else:
            out.files_skipped.append(str(path))
    all_lines.sort(key=lambda t: t[0])
    out.lines = all_lines
    return out


# --------------------------------------------------------------------------
# nbrlog.py subprocess -- reboots, gaps, NBR CHECK violations. Never
# re-implemented here; a time-sliced copy of the node's log is fed to it.
# --------------------------------------------------------------------------


def run_nbrlog(sliced_log_path: Path, workdir: Path) -> dict[str, Any]:
    json_out = workdir / "nbrlog_out.json"
    cmd = [sys.executable, str(NBRLOG_PATH), str(sliced_log_path), "--json", str(json_out)]
    result = subprocess.run(cmd, capture_output=True, text=True, check=False)
    if result.returncode != 0 or not json_out.exists():
        raise RuntimeError(
            f"tools/nbrlog.py failed (rc={result.returncode}):\n{result.stderr[-2000:]}"
        )
    with open(json_out, "r", encoding="utf-8") as fh:
        return json.load(fh)


# --------------------------------------------------------------------------
# Per-node metrics (everything nbrlog.py doesn't already cover)
# --------------------------------------------------------------------------


@dataclass
class NodeMetrics:
    node: str
    files: list[str]
    files_skipped: list[str]
    line_count: int
    unparsed_lines: int
    first_ts: datetime | None
    last_ts: datetime | None
    reboots_boot_marker: int
    reboot_events: list[str]
    heap_samples: list[int]
    heap_first: int | None
    heap_last: int | None
    heap_min: int | None
    heap_max: int | None
    heap_drift: int | None  # last - first, negative = shrank
    stat_util_samples: list[int]
    channel_util_samples: list[int]
    tx_count: int
    tx_own: int
    tx_relay: int
    tx_gw: int
    err_count: int
    wifi_down_count: int
    gw_keep_count: int
    dm_sums: dict[str, int]
    dm_line_count: int
    dm_last_raw: str | None
    retx_counts: dict[str, int]
    nbr_reboots: int
    nbr_reboot_events: list[dict[str, Any]]
    nbr_gaps: list[dict[str, Any]]
    nbr_check_total: int
    nbr_check_violations: list[dict[str, Any]]
    #: seconds from the window start (--since) to the first line, and from the
    #: last line to the window end (--until, or now while the window is open).
    #: A capture that dies mid-window has no gap BETWEEN lines -- only these
    #: two see it. With no line at all, both equal the whole window.
    lead_gap_s: float = 0.0
    tail_gap_s: float = 0.0

    @property
    def capture_missing(self) -> bool:
        """No line at all in the window (capture never started, wrong dir, or dead before --since)."""
        return self.line_count == 0

    @property
    def reboots_total(self) -> int:
        # Both detectors watch the same window for the same thing (a reset);
        # a real reboot lights up both, so this is a max, not a sum.
        return max(self.reboots_boot_marker, self.nbr_reboots)

    @property
    def gaps_over_bluf_threshold(self) -> list[dict[str, Any]]:
        return [g for g in self.nbr_gaps if g["dauer_s"] > BLUF_GAP_MAX_S]

    @property
    def edge_gaps(self) -> list[str]:
        """Human-readable start/end gaps above the BLUF threshold."""
        out = []
        if self.lead_gap_s > BLUF_GAP_MAX_S:
            out.append(f"no line for the first {self.lead_gap_s:.0f}s of the window")
        if self.tail_gap_s > BLUF_GAP_MAX_S:
            out.append(f"no line for the last {self.tail_gap_s:.0f}s of the window")
        return out

    @property
    def alive(self) -> bool:
        return not self.capture_missing and not self.gaps_over_bluf_threshold and not self.edge_gaps

    @property
    def heap_ok(self) -> bool:
        if self.heap_first is None or self.heap_last is None:
            return False
        return self.heap_last >= self.heap_first - HEAP_DRIFT_BUDGET_B


DM_SUM_FIELDS = ["sent", "echo", "gwack", "ack", "giveup", "giveuph", "att", "reack", "reack_lim", "enq", "ovw"]


def _edge_gap(start: datetime | None, end: datetime | None) -> float:
    if start is None or end is None:
        return 0.0
    return max(0.0, (end - start).total_seconds())


def compute_node_metrics(
    node: str,
    sliced: SlicedLog,
    nbr_json: dict[str, Any],
    since: datetime | None = None,
    until: datetime | None = None,
) -> NodeMetrics:
    heap_samples: list[int] = []
    stat_util: list[int] = []
    chan_util: list[int] = []
    dm_sums: dict[str, int] = {k: 0 for k in DM_SUM_FIELDS}
    dm_line_count = 0
    dm_last_raw: str | None = None
    retx_counts: Counter = Counter()
    tx_count = 0
    tx_src: Counter = Counter()
    err_count = 0
    wifi_down = 0
    gw_keep = 0
    reboot_markers = 0
    reboot_events: list[str] = []
    seen_client_setup = False

    for host, rest in sliced.lines:
        m = RE_STAT.search(rest)
        if m:
            heap_samples.append(int(m.group("heap")))
            stat_util.append(int(m.group("util")))
        m = RE_DM.search(rest)
        if m:
            dm_line_count += 1
            dm_last_raw = rest
            for key in DM_SUM_FIELDS:
                dm_sums[key] += int(m.group(key))
        m = RE_CHANNEL_UTIL.search(rest)
        if m:
            chan_util.append(int(m.group("util")))
        if RE_ERR_LINE.search(rest):
            err_count += 1
        m = RE_TX_LOG.search(rest)
        if m:
            tx_count += 1
            tx_src[m.group("src")] += 1
        if RE_WIFI_DOWN.search(rest):
            wifi_down += 1
        if RE_GW_KEEP.search(rest):
            gw_keep += 1
        for pattern, key in (
            (RE_PNRETRY, "PNRETRY"),
            (RE_PN_ECHO_RESTART, "PN_echo_wait_restarted"),
            (RE_PN_ALREADY_ACKED, "PN_already_acked"),
            (RE_SERVER_ACK, "server_ACK"),
            (RE_LORA_ACK, "DM_ACK_lora"),
            (RE_PNREPEAT, "PNREPEAT"),
            (RE_GIVEUP_DM, "RETRANSMIT_GIVEUP_DM"),
        ):
            if pattern.search(rest):
                retx_counts[key] += 1
        if RE_CLIENT_SETUP.match(rest):
            if seen_client_setup:
                reboot_markers += 1
                reboot_events.append(fmt_dt(host))
            seen_client_setup = True
        elif RE_BOOT_RESET.search(rest) and reboot_markers == 0 and not seen_client_setup:
            # RESET_REASON without a preceding CLIENT SETUP in-window (log
            # started mid-boot) still counts as one boot event.
            reboot_markers += 1
            reboot_events.append(fmt_dt(host))

    rahmen = nbr_json.get("1_rahmen", {})
    druck = nbr_json.get("6_tabellendruck", {})

    return NodeMetrics(
        node=node,
        files=sliced.files,
        files_skipped=sliced.files_skipped,
        line_count=len(sliced.lines),
        unparsed_lines=sliced.unparsed,
        first_ts=sliced.lines[0][0] if sliced.lines else None,
        last_ts=sliced.lines[-1][0] if sliced.lines else None,
        reboots_boot_marker=reboot_markers,
        reboot_events=reboot_events,
        heap_samples=heap_samples,
        heap_first=heap_samples[0] if heap_samples else None,
        heap_last=heap_samples[-1] if heap_samples else None,
        heap_min=min(heap_samples) if heap_samples else None,
        heap_max=max(heap_samples) if heap_samples else None,
        heap_drift=(heap_samples[-1] - heap_samples[0]) if heap_samples else None,
        stat_util_samples=stat_util,
        channel_util_samples=chan_util,
        tx_count=tx_count,
        tx_own=tx_src.get("o", 0),
        tx_relay=tx_src.get("r", 0),
        tx_gw=tx_src.get("g", 0),
        err_count=err_count,
        wifi_down_count=wifi_down,
        gw_keep_count=gw_keep,
        dm_sums=dm_sums,
        dm_line_count=dm_line_count,
        dm_last_raw=dm_last_raw,
        retx_counts=dict(retx_counts),
        nbr_reboots=rahmen.get("reboots", 0),
        nbr_reboot_events=rahmen.get("reboot_ereignisse", []),
        nbr_gaps=rahmen.get("luecken", []),
        nbr_check_total=druck.get("check_gesamt", 0),
        nbr_check_violations=druck.get("check_verstoesse", []),
        lead_gap_s=_edge_gap(since, sliced.lines[0][0] if sliced.lines else until),
        tail_gap_s=_edge_gap(sliced.lines[-1][0] if sliced.lines else since, until),
    )


# --------------------------------------------------------------------------
# Per-test-DM correlation
# --------------------------------------------------------------------------


@dataclass
class SoakSend:
    seq: int
    frm: str
    to: str
    sent_ts: datetime
    http: int
    answer: str


@dataclass
class DmResult:
    seq: int
    frm: str
    to: str
    sent_ts: datetime
    send_ok: bool
    msgid: str | None
    nnn: str | None
    transmissions: int
    tx_msgids: list[str]
    giveup: bool
    giveup_ts: datetime | None
    acked: bool
    ack_ts: datetime | None
    ack_path: str | None
    ack_latency_s: float | None
    received: bool
    receiver_copies: int
    receiver_first_ts: datetime | None
    note: str
    #: host time of the sender's own first TX line for this DM (latency reference).
    tx_ts: datetime | None = None


def parse_soak_log(path: Path, since: datetime, until: datetime) -> tuple[list[SoakSend], str | None]:
    sends: list[SoakSend] = []
    tag: str | None = None
    if not path.is_file():
        return sends, tag
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        for line in fh:
            line = line.rstrip("\n")
            m = RE_SOAK_START.match(line)
            if m:
                tag = m.group("tag")
                continue
            m = RE_SOAK_SEND.match(line)
            if not m:
                continue
            try:
                ts = datetime.fromisoformat(m.group("iso"))
            except ValueError:
                continue
            if not (since <= ts < until):
                continue
            sends.append(
                SoakSend(
                    seq=int(m.group("seq")),
                    frm=m.group("frm"),
                    to=m.group("to"),
                    sent_ts=ts,
                    http=int(m.group("http")),
                    answer=m.group("answer"),
                )
            )
    return sends, tag


def correlate_dm(
    send: SoakSend,
    tag: str,
    node_lines: dict[str, list[tuple[datetime, str]]],
    match_window: timedelta,
) -> DmResult:
    text = f"soak {tag} #{send.seq} {send.frm}>{send.to}"
    sender_lines = node_lines.get(send.frm, [])
    receiver_lines = node_lines.get(send.to, [])
    window_end = send.sent_ts + match_window
    window_start = send.sent_ts - timedelta(seconds=5)

    if send.answer != "ok":
        return DmResult(
            send.seq, send.frm, send.to, send.sent_ts, False, None, None, 0, [], False, None,
            False, None, None, None, False, 0, None,
            f"send itself did not confirm (http={send.http} answer={send.answer})",
        )

    # -- sender side: find every TX-tagged line carrying this DM's payload.
    tx_msgids: list[str] = []
    msgid_first_seen: dict[str, datetime] = {}
    nnn: str | None = None
    for host, rest in sender_lines:
        if not (window_start <= host <= window_end):
            continue
        if text not in rest:
            continue
        if not RE_TX_TAG.search(rest):
            continue
        mid_m = RE_LINE_MSGID.search(rest)
        if not mid_m:
            continue
        mid = mid_m.group(1).upper()
        if mid not in msgid_first_seen:
            msgid_first_seen[mid] = host
            tx_msgids.append(mid)
        if nnn is None:
            nnn_m = RE_NNN.search(rest)
            if nnn_m:
                nnn = nnn_m.group(1)

    original_msgid = tx_msgids[0] if tx_msgids else None

    # -- give-up: any RETRANSMIT_GIVEUP_DM for one of this DM's msg-ids.
    giveup = False
    giveup_ts: datetime | None = None
    for host, rest in sender_lines:
        if not (window_start <= host <= window_end):
            continue
        m = RE_GIVEUP_DM.search(rest)
        if m and m.group("msgid").upper() in tx_msgids:
            giveup = True
            giveup_ts = host
            break

    # -- ack: both ack lines carry the reconstructed ORIGINAL msg-id, so compare
    # against original_msgid, not the whole tx_msgids set. The path is whichever
    # ack line came FIRST: the server ack ("[UDP-MSGID] ack_msg_id:<orig> ACK...02")
    # or the LoRa ack ("[ACK-MSGID] ack_msg_id:<orig>"). The [RETX] markers are
    # printed only when the ack arrives after the first LoRa TX, so they are just
    # a fallback for logs that lack the two lines above.
    acked = False
    ack_ts: datetime | None = None
    ack_path: str | None = None
    already_acked_only = False
    if original_msgid:
        candidates: list[tuple[datetime, str]] = []
        fallback: list[tuple[datetime, str]] = []
        for host, rest in sender_lines:
            if not (window_start <= host <= window_end):
                continue
            for pattern, path_name, bucket in (
                (RE_UDP_ACK, "server/UDP", candidates),
                (RE_LORA_ACK_ID, "LoRa", candidates),
                (RE_SERVER_ACK, "server/UDP", fallback),
                (RE_LORA_ACK, "LoRa", fallback),
            ):
                m = pattern.search(rest)
                if m and m.group("msgid").upper() == original_msgid:
                    bucket.append((host, path_name))
        found = candidates or fallback
        if found:
            ack_ts, ack_path = min(found, key=lambda c: c[0])  # min() is stable: ties keep log order
            acked = True
        else:
            # Last resort: a later retry's "already acked" cleanup confirms an
            # earlier ack whose own log line fell outside our window/log.
            for host, rest in sender_lines:
                if not (window_start <= host <= window_end):
                    continue
                m = RE_PN_ALREADY_ACKED.search(rest)
                if m and m.group("msgid").upper() == original_msgid:
                    acked, ack_ts, ack_path = True, host, "unknown (seen only via PN-already-acked cleanup)"
                    already_acked_only = True
                    break

    # Latency counts from the sender's own first TX line (host time of the same
    # capture); the soak log only has whole seconds and is written after the
    # node's HTTP answer. Falls back to the soak log's send time.
    tx_ts = msgid_first_seen.get(original_msgid) if original_msgid else None
    ack_latency = (ack_ts - (tx_ts or send.sent_ts)).total_seconds() if ack_ts else None

    # -- receiver side: REAL receptions carrying this DM's payload -- one RX-UDP
    # line per server copy, one RF "[LOG] NNN" line per LoRa reception. The
    # MH-LoRa / RX-LoRa2 lines of the same LoRa reception are not extra copies.
    receiver_copies = 0
    receiver_first_ts: datetime | None = None
    for host, rest in receiver_lines:
        if not (window_start <= host <= window_end):
            continue
        if text not in rest:
            continue
        if not (RE_RX_UDP.search(rest) or RE_RX_RF.search(rest)):
            continue
        receiver_copies += 1
        if receiver_first_ts is None:
            receiver_first_ts = host
    received = receiver_copies > 0

    note = ""
    if not tx_msgids:
        note = "own TX frame not found in sender log (window/text mismatch or capture gap)"
    elif send.to not in node_lines:
        note = "receiver capture not part of this run (received/copies not evaluated)"
    elif not received and not acked:
        note = "no trace at receiver and no ack seen -- likely lost or outside match window"
    elif not received and acked:
        note = "acked but no matching RX line at receiver (receiver log gap, or ack via a path this parser doesn't see)"
    elif already_acked_only:
        note = "ack time is approximate (only the later cleanup line was in range)"

    return DmResult(
        send.seq, send.frm, send.to, send.sent_ts, True, original_msgid, nnn,
        len(tx_msgids), tx_msgids, giveup, giveup_ts, acked, ack_ts, ack_path,
        ack_latency, received, receiver_copies, receiver_first_ts, note, tx_ts,
    )


# --------------------------------------------------------------------------
# Fetch (DK5EN-98 only -- DK5EN-1 is a local USB capture already on disk)
# --------------------------------------------------------------------------


def fetch_dk98(dest_dir: Path, host: str, remote_dir: str, since: datetime, until: datetime) -> None:
    """scp's just the net-console logs that can cover [since, until) from the
    rpizero down to ``dest_dir``: tools/meshlogger.py rotates one file per day
    named "YYYY-MM-DD.log", so this pulls only those date-stamped files (plus
    status.txt) instead of the whole remote directory -- months of unrelated
    history sit next to them there (verified 2026-09-27: >100 MB of old
    logs), and a blind "*.log" glob times out trying to pull all of it.

    Never opens the node's own console (single-client; would kick the
    logger) -- this only copies files the logger already wrote to disk.
    stdin is /dev/null so a stuck SSH prompt can't hang the run.
    """
    dest_dir.mkdir(parents=True, exist_ok=True)
    remote = remote_dir if remote_dir.endswith("/") else remote_dir + "/"

    day = since.date()
    day_files = []
    while day <= until.date():
        day_files.append(f"{remote}{day.isoformat()}.log")
        day = day + timedelta(days=1)

    sources = day_files + [f"{remote}status.txt"]
    cmd = ["scp", "-q"] + [f"{host}:{s}" for s in sources] + [str(dest_dir) + "/"]
    printable = " ".join(cmd)
    print(f"fetching DK5EN-98 logs: {printable}")
    try:
        with open("/dev/null", "rb") as devnull:
            result = subprocess.run(cmd, stdin=devnull, capture_output=True, text=True, timeout=90)
        if result.returncode != 0:
            # scp aborts the whole batch if ANY source is missing (e.g.
            # status.txt, or tomorrow's file before midnight) -- that's
            # expected, not fatal; whatever it did copy before failing is
            # already on disk.
            print(
                f"NOTE: scp reported rc={result.returncode} (likely one missing source, "
                f"e.g. a not-yet-rotated day file): {result.stderr.strip()}\n"
                "  continuing with whatever was copied plus the local cache.",
                file=sys.stderr,
            )
    except subprocess.TimeoutExpired:
        print(
            "WARNING: fetch timed out after 90s (host unreachable or waiting on a prompt) -- "
            "continuing with whatever is already in the local cache.",
            file=sys.stderr,
        )


# --------------------------------------------------------------------------
# BLUF + report assembly
# --------------------------------------------------------------------------


def build_bluf(nodes: dict[str, NodeMetrics], dm_results: list[DmResult]) -> list[dict[str, Any]]:
    checks: list[dict[str, Any]] = []

    # A node with no line in the window is its own FAIL ("capture missing"); the
    # per-node checks below skip it instead of failing again on empty data.
    missing = [n.node for n in nodes.values() if n.capture_missing]
    present = {k: n for k, n in nodes.items() if not n.capture_missing}
    checks.append(
        {
            "criterion": "every capture present (>= 1 line in the window)",
            "passed": not missing,
            "detail": (
                "; ".join(f"{n.node}: {n.line_count} lines" for n in nodes.values())
                if not missing
                else "capture missing: " + ", ".join(missing)
            ),
        }
    )

    total_reboots = sum(n.reboots_total for n in present.values())
    reboot_detail = "; ".join(
        f"{n.node}: {n.reboots_total} (boot-marker={n.reboots_boot_marker}, nbr-up-wrap={n.nbr_reboots})"
        for n in present.values()
    )
    checks.append(
        {
            "criterion": "no reboot",
            "passed": total_reboots == 0,
            "detail": reboot_detail,
        }
    )

    heap_ok = all(n.heap_ok for n in present.values())
    heap_detail = "; ".join(
        f"{n.node}: first={n.heap_first} last={n.heap_last} drift={n.heap_drift}"
        for n in present.values()
    )
    checks.append({"criterion": f"heap last >= baseline - {HEAP_DRIFT_BUDGET_B} B", "passed": heap_ok, "detail": heap_detail})

    unacked = [d for d in dm_results if d.send_ok and not d.acked]
    checks.append(
        {
            "criterion": "every test DM acked or explained",
            "passed": len(unacked) == 0,
            "detail": (
                "all acked" if not unacked else
                "; ".join(f"#{d.seq} {d.frm}>{d.to}: {d.note or 'unacked, no explanation found'}" for d in unacked)
            ),
        }
    )

    total_violations = sum(len(n.nbr_check_violations) for n in present.values())
    checks.append(
        {
            "criterion": "0 NBR CHECK violations",
            "passed": total_violations == 0,
            "detail": f"{total_violations} violation(s) across {sum(n.nbr_check_total for n in present.values())} CHECK lines",
        }
    )

    alive_ok = all(n.alive for n in present.values())
    gap_detail = "; ".join(
        f"{n.node}: "
        + (
            "no gap > 10 min (incl. window start and end)"
            if n.alive
            else ", ".join(
                [f"{g['von']}..{g['bis']} ({g['dauer_s']}s)" for g in n.gaps_over_bluf_threshold] + n.edge_gaps
            )
        )
        for n in present.values()
    )
    checks.append(
        {
            "criterion": "every capture alive (no gap > 10 min, incl. window start and end)",
            "passed": alive_ok,
            "detail": gap_detail,
        }
    )

    return checks


def render_report(
    since: datetime,
    until: datetime,
    nodes: dict[str, NodeMetrics],
    dm_results: list[DmResult],
    bluf: list[dict[str, Any]],
) -> str:
    lines: list[str] = []
    lines.append(f"# Soak status -- {fmt_dt(since)} to {fmt_dt(until)}")
    lines.append("")
    lines.append("## BLUF")
    lines.append("")
    overall = "PASS" if all(c["passed"] for c in bluf) else "FAIL"
    lines.append(f"**Overall: {overall}**")
    lines.append("")
    for c in bluf:
        mark = "PASS" if c["passed"] else "FAIL"
        lines.append(f"- [{mark}] {c['criterion']} -- {c['detail']}")
    lines.append("")

    for node, m in nodes.items():
        lines.append(f"## {node}")
        lines.append("")
        lines.append(f"- Files in window: {', '.join(Path(f).name for f in m.files) or '(none found)'}")
        if m.files_skipped:
            lines.append(f"  ({len(m.files_skipped)} other file(s) in the directory outside the window, skipped)")
        lines.append(
            f"- Window coverage: {fmt_dt(m.first_ts) if m.first_ts else 'n/a'} .. "
            f"{fmt_dt(m.last_ts) if m.last_ts else 'n/a'} ({m.line_count} lines, {m.unparsed_lines} unparsed)"
        )
        lines.append(f"- Reboots: {m.reboots_total} (boot-marker={m.reboots_boot_marker}, nbr-up-wrap={m.nbr_reboots})")
        if m.reboot_events or m.nbr_reboot_events:
            for e in m.reboot_events:
                lines.append(f"  - boot marker at {e}")
            for e in m.nbr_reboot_events:
                lines.append(f"  - NBR up-counter wrap at {e['host']} ({e['up_vorher']} -> {e['up_nachher']})")
        if m.capture_missing:
            lines.append("- **CAPTURE MISSING**: no line at all in the window")
        else:
            lines.append(
                f"- Window edges: first line {m.lead_gap_s:.0f}s after --since, last line {m.tail_gap_s:.0f}s before "
                f"the window end" + (f" -- **{'; '.join(m.edge_gaps)}**" if m.edge_gaps else "")
            )
        lines.append(
            f"- Gaps > {GAP_THRESHOLD_S:.0f}s: {len(m.nbr_gaps)}"
            + (f", of which > 10 min: {len(m.gaps_over_bluf_threshold)}" if m.nbr_gaps else "")
        )
        for g in m.nbr_gaps:
            lines.append(f"  - {g['von']} .. {g['bis']} ({g['dauer_s']}s)")
        if m.heap_samples:
            lines.append(
                f"- Heap: first={m.heap_first} last={m.heap_last} min={m.heap_min} max={m.heap_max} "
                f"drift={m.heap_drift} ({len(m.heap_samples)} STAT samples)"
            )
        else:
            lines.append("- Heap: no STAT samples in window")
        if m.channel_util_samples:
            lines.append(
                f"- Channel util (CHANNEL_UTIL, per-sample %): "
                f"min={min(m.channel_util_samples)} median={statistics.median(m.channel_util_samples)} "
                f"max={max(m.channel_util_samples)} (n={len(m.channel_util_samples)})"
            )
        if m.stat_util_samples:
            lines.append(
                f"- Channel util (STAT, 5-min window %): "
                f"min={min(m.stat_util_samples)} median={statistics.median(m.stat_util_samples)} "
                f"max={max(m.stat_util_samples)} (n={len(m.stat_util_samples)})"
            )
        lines.append(f"- TX count (own+relay+gw): {m.tx_count} (own={m.tx_own} relay={m.tx_relay} gw={m.tx_gw})")
        lines.append(f"- RX ERR count: {m.err_count}")
        lines.append(f"- WiFi link-down events: {m.wifi_down_count}")
        lines.append(f"- GW keepalive sent (all logged as ok -- no fail marker exists in this firmware): {m.gw_keep_count}")
        lines.append(f"- NBR CHECK lines: {m.nbr_check_total}, violations: {len(m.nbr_check_violations)}")
        for v in m.nbr_check_violations:
            lines.append(f"  - {v['host']}: extra={v['extra']} missing={v['missing']} bad={v['bad']} dup={v['dup']}")
        if m.dm_line_count:
            s = m.dm_sums
            lines.append(
                f"- DM setlog, summed over {m.dm_line_count} interval lines "
                f"(each line is a delta, see module docstring): "
                f"sent={s['sent']} echo={s['echo']} gwack={s['gwack']} ack={s['ack']} "
                f"giveup={s['giveup']} giveuph={s['giveuph']} att={s['att']} "
                f"reack={s['reack']}/{s['reack_lim']} ring=enq:{s['enq']} ovw:{s['ovw']}"
            )
            lines.append(f"  - latest raw line: `{m.dm_last_raw}`")
        else:
            lines.append("- DM setlog: no lines in window")
        if m.retx_counts:
            lines.append(
                "- [RETX]/[RX] markers: "
                + ", ".join(f"{k}={v}" for k, v in sorted(m.retx_counts.items()))
            )
        lines.append("")

    lines.append("## Test DMs (tools/bench/soak_dm.py)")
    lines.append("")
    if not dm_results:
        lines.append("_no test DMs in this window_")
    else:
        headers = ["seq", "from>to", "sent", "msg-id", "NNN", "tx#", "acked", "path", "latency_s", "giveup", "received", "copies", "note"]
        lines.append("| " + " | ".join(headers) + " |")
        lines.append("| " + " | ".join(["---"] * len(headers)) + " |")
        for d in dm_results:
            lines.append(
                "| "
                + " | ".join(
                    str(x)
                    for x in [
                        d.seq,
                        f"{d.frm}>{d.to}",
                        fmt_dt(d.sent_ts),
                        d.msgid or "-",
                        d.nnn or "-",
                        d.transmissions,
                        "yes" if d.acked else "no",
                        d.ack_path or "-",
                        f"{d.ack_latency_s:.1f}" if d.ack_latency_s is not None else "-",
                        "yes" if d.giveup else "no",
                        "yes" if d.received else "no",
                        d.receiver_copies,
                        d.note or "",
                    ]
                )
                + " |"
            )
        lines.append("")
        acked_n = sum(1 for d in dm_results if d.acked)
        sent_ok_n = sum(1 for d in dm_results if d.send_ok)
        latencies = [d.ack_latency_s for d in dm_results if d.ack_latency_s is not None]
        retry_hist = Counter(d.transmissions for d in dm_results if d.send_ok)
        lines.append(f"- Sent (confirmed by the node's HTTP answer): {sent_ok_n}/{len(dm_results)}")
        lines.append(f"- Acked: {acked_n}/{sent_ok_n} ({100.0 * acked_n / sent_ok_n:.0f}%)" if sent_ok_n else "- Acked: n/a")
        if latencies:
            lines.append(
                f"- Ack latency: median={statistics.median(latencies):.1f}s max={max(latencies):.1f}s (n={len(latencies)})"
            )
        lines.append(
            "- Transmissions histogram (1 = no retry needed): "
            + ", ".join(f"{k}x{v}" for k, v in sorted(retry_hist.items()))
        )
    lines.append("")
    return "\n".join(lines)


def to_json(
    since: datetime,
    until: datetime,
    nodes: dict[str, NodeMetrics],
    dm_results: list[DmResult],
    bluf: list[dict[str, Any]],
) -> dict[str, Any]:
    def node_dict(m: NodeMetrics) -> dict[str, Any]:
        d = dict(m.__dict__)
        d["first_ts"] = fmt_dt(m.first_ts) if m.first_ts else None
        d["last_ts"] = fmt_dt(m.last_ts) if m.last_ts else None
        d["reboots_total"] = m.reboots_total
        d["alive"] = m.alive
        d["capture_missing"] = m.capture_missing
        d["edge_gaps"] = m.edge_gaps
        d["heap_ok"] = m.heap_ok
        return d

    def dm_dict(d: DmResult) -> dict[str, Any]:
        e = dict(d.__dict__)
        e["sent_ts"] = fmt_dt(d.sent_ts)
        e["giveup_ts"] = fmt_dt(d.giveup_ts) if d.giveup_ts else None
        e["ack_ts"] = fmt_dt(d.ack_ts) if d.ack_ts else None
        e["receiver_first_ts"] = fmt_dt(d.receiver_first_ts) if d.receiver_first_ts else None
        e["tx_ts"] = fmt_dt(d.tx_ts) if d.tx_ts else None
        return e

    return {
        "since": fmt_dt(since),
        "until": fmt_dt(until),
        "bluf": bluf,
        "bluf_overall": "PASS" if all(c["passed"] for c in bluf) else "FAIL",
        "nodes": {k: node_dict(v) for k, v in nodes.items()},
        "dm_results": [dm_dict(d) for d in dm_results],
    }


# --------------------------------------------------------------------------
# Self-test -- small fixtures copied verbatim from the real 2026-09-27 capture.
# --------------------------------------------------------------------------


FIXTURE_DK1 = """\
2026-09-27 17:33:56.505    ============
2026-09-27 17:33:56.505  CLIENT SETUP
2026-09-27 17:33:56.505  ============
2026-09-27 17:33:57.507  [BOOT] RESET_REASON=1 POWERON
2026-09-27 17:38:56.516  16:38:54 [LOG] STAT util=6 rx=11770 tx=7993 newid=10 dup=0 err=0 txn=11 txfail=0 ringmax=2/20 drop=0/0/0/0/0 mh=1 heap=133040 trk=240/1 fw=35t/20260927 up=300 t=300003
2026-09-27 17:38:56.516  16:38:54 [LOG] DM sent=0 echo=0 gwack=0 ack=0 giveup=0 giveuph=0 att=0 reack=0/0 rtt=0/0/0/0/0/0 ring=enq:11 ovw:0
2026-09-27 17:39:25.617  16:39:24 NEW-TXT 069 : xEA25A324 H04 S0 T0 M00 DK5EN-1>DK5EN-98:soak 20260927 #1 DK5EN-1>DK5EN-98{804 HW:43 MOD:8/8 FCS:0FF6 FW:35:t LH:AB
2026-09-27 17:39:25.617  16:39:24 TX-UDP   069 : xEA25A324 H04 S0 T0 M01 DK5EN-1>DK5EN-98:soak 20260927 #1 DK5EN-1>DK5EN-98{804 HW:43 MOD:8/8 FCS:0FF6 FW:35:t LH:AB
2026-09-27 17:39:26.622  16:39:24 TX-LoRa  069 : xEA25A324 H04 S0 T0 M01 DK5EN-1>DK5EN-98:soak 20260927 #1 DK5EN-1>DK5EN-98{804 HW:43 MOD:8/8 FCS:0FF6 FW:35:t LH:AB
2026-09-27 17:39:26.622  16:39:24 [LOG] TX xEA25A324 : H04 prio=5 src=o wait=12 q=1 cad=0 len=69 t=339541
2026-09-27 17:39:29.633  16:39:28 checkOwnTx:EA25A324 own_msg_id:EA25A324 <EA25A324> 00
2026-09-27 17:39:29.633  [RETX] server ACK for retid:12 stop retransmit msg-id:EA25A324
2026-09-27 17:41:26.011  [GW];keep;tx;ok;1;ms;450064
2026-09-27 17:41:26.011  [MC-DBG] CHANNEL_UTIL rx=952ms tx=1085ms util=20%
2026-09-27 17:42:00.000  [WIFI];link;down;sta;0;status;6;down_s;12;last_reason;8;got_ip_n;3;ip;0;ms;500000
2026-09-27 17:42:05.000  [LOG] ERR rssi=-110 snr=-8 len=0 ferr=0 t=500500
"""

FIXTURE_DK98 = """\
2026-09-27 17:39:29.000  16:39:28 RX-UDP  069 : xEA25A324 H02 S0 T0 M01 DK5EN-1>DK5EN-98:soak 20260927 #1 DK5EN-1>DK5EN-98{804 HW:43 MOD:8/8 FCS:0FF6 FW:35:t LH:AB
2026-09-27 17:39:29.100  16:39:28 [LOG] 069 : xEA25A324 H02 S0 T0 M01 DK5EN-1>DK5EN-98:soak 20260927 #1 DK5EN-1>DK5EN-98{804 HW:43 MOD:8/8 FCS:0FF6 FW:35:t LH:AB RSSI:-80 SNR:5 DUP:n OWN:- t=1
2026-09-27 17:39:29.100  16:39:28 MH-LoRa 069 : xEA25A324 H02 S0 T0 M01 DK5EN-1>DK5EN-98:soak 20260927 #1 DK5EN-1>DK5EN-98{804 HW:43 MOD:8/8 FCS:0FF6 FW:35:t LH:AB
2026-09-27 17:39:29.100  16:39:28 RX-LoRa2 069 : xEA25A324 H02 S0 T0 M01 DK5EN-1>DK5EN-98:soak 20260927 #1 DK5EN-1>DK5EN-98{804 HW:43 MOD:8/8 FCS:0FF6 FW:35:t LH:AB
2026-09-27 17:38:56.516  16:38:54 [LOG] STAT util=5 rx=9000 tx=6000 newid=8 dup=0 err=0 txn=9 txfail=0 ringmax=1/20 drop=0/0/0/0/0 mh=1 heap=140000 trk=240/1 fw=35t/20260927 up=300 t=300003
2026-09-27 17:38:56.516  16:38:54 [LOG] DM sent=0 echo=0 gwack=0 ack=1 giveup=0 giveuph=0 att=1 reack=0/0 rtt=0/1/0/0/0/0 ring=enq:9 ovw:0
2026-09-27 17:39:33.500  [NBR]|CHECK|300|10|20|0|0|0|0
2026-09-27 18:00:00.000  16:59:59 NEW-TXT 070 : xAABBCCDD H04 S0 T0 M00 DK5EN-98>DK5EN-1:soak 20260927 #2 DK5EN-98>DK5EN-1{805 HW:43 MOD:8/8 FCS:0000 FW:35:t LH:AB
2026-09-27 18:00:41.000  16:59:59 TX-LoRa  070 : x11223344 H04 S0 T0 M01 DK5EN-98>DK5EN-1:soak 20260927 #2 DK5EN-98>DK5EN-1{805 HW:43 MOD:8/8 FCS:0000 FW:35:t LH:AB
2026-09-27 18:00:41.500  [RETX] PNRETRY k=1 msg-id:11223344
2026-09-27 18:03:00.000  [MC-DBG] RETRANSMIT_GIVEUP_DM msg_id=11223344 dest=DK5EN-1
"""


def _check(label: str, got: Any, want: Any, failures: list[str]) -> None:
    if got != want:
        failures.append(f"{label}: expected {want!r}, got {got!r}")


def run_self_test() -> int:
    failures: list[str] = []
    with tempfile.TemporaryDirectory() as tmp:
        tmp_path = Path(tmp)
        dk1_dir = tmp_path / "dk5en-1"
        dk98_dir = tmp_path / "dk5en-98-local"
        dk1_dir.mkdir()
        dk98_dir.mkdir()
        (dk1_dir / "2026-09-27-xor.log").write_text(FIXTURE_DK1, encoding="utf-8")
        (dk98_dir / "2026-09-27.log").write_text(FIXTURE_DK98, encoding="utf-8")
        soak_log = tmp_path / "soak-dm.log"
        soak_log.write_text(
            "2026-09-27T17:39:24  START tag=20260927 DK5EN-1=192.168.68.62 DK5EN-98=dk5en-98.local "
            "interval_min=30.0 hours=24.0\n"
            "2026-09-27T17:39:25  SEND 1 DK5EN-1 DK5EN-98 200 ok\n"
            "2026-09-27T17:59:59  SEND 2 DK5EN-98 DK5EN-1 200 ok\n",
            encoding="utf-8",
        )

        since = datetime(2026, 9, 27, 17, 30, 0)
        until = datetime(2026, 9, 27, 19, 0, 0)

        sliced_a = load_and_slice(dk1_dir, since, until)
        sliced_b = load_and_slice(dk98_dir, since, until)
        _check("dk1 unparsed lines (all fixture lines carry the host prefix)", sliced_a.unparsed, 0, failures)

        with tempfile.TemporaryDirectory() as work:
            work_path = Path(work)
            sliced_a_path = work_path / "a.log"
            sliced_b_path = work_path / "b.log"
            sliced_a.write_sliced_copy(sliced_a_path)
            sliced_b.write_sliced_copy(sliced_b_path)
            nbr_a = run_nbrlog(sliced_a_path, work_path)
            nbr_b = run_nbrlog(sliced_b_path, work_path)

        metrics_a = compute_node_metrics(NODE_A, sliced_a, nbr_a, since, until)
        metrics_b = compute_node_metrics(NODE_B, sliced_b, nbr_b, since, until)

        _check("dk1 heap first", metrics_a.heap_first, 133040, failures)
        _check("dk1 heap last", metrics_a.heap_last, 133040, failures)
        _check("dk1 heap_ok", metrics_a.heap_ok, True, failures)
        _check("dk1 dm sent sum", metrics_a.dm_sums["sent"], 0, failures)
        _check("dk1 dm line count", metrics_a.dm_line_count, 1, failures)
        _check("dk1 tx count", metrics_a.tx_count, 1, failures)
        _check("dk1 tx own", metrics_a.tx_own, 1, failures)
        _check("dk1 err count", metrics_a.err_count, 1, failures)
        _check("dk1 wifi down", metrics_a.wifi_down_count, 1, failures)
        _check("dk1 gw keep", metrics_a.gw_keep_count, 1, failures)
        _check("dk1 retx server_ACK", metrics_a.retx_counts.get("server_ACK"), 1, failures)
        _check("dk1 channel util samples", metrics_a.channel_util_samples, [20], failures)
        _check("dk1 reboots (no second CLIENT SETUP)", metrics_a.reboots_boot_marker, 0, failures)
        # the fixture ends 17:42, the window at 19:00: no gap between lines, but a dead tail
        _check("dk1 tail gap (last line -> --until)", round(metrics_a.tail_gap_s), 4675, failures)
        _check("dk1 alive (dead tail)", metrics_a.alive, False, failures)
        _check("dk1 capture_missing", metrics_a.capture_missing, False, failures)

        _check("dk98 heap first", metrics_b.heap_first, 140000, failures)
        _check("dk98 dm ack sum", metrics_b.dm_sums["ack"], 1, failures)
        _check("dk98 nbr check total", metrics_b.nbr_check_total, 1, failures)
        _check("dk98 retx PNRETRY", metrics_b.retx_counts.get("PNRETRY"), 1, failures)
        _check("dk98 retx giveup", metrics_b.retx_counts.get("RETRANSMIT_GIVEUP_DM"), 1, failures)
        _check("dk98 nbr check violations", len(metrics_b.nbr_check_violations), 0, failures)

        # -- reboot detection: a second CLIENT SETUP counts as one reboot.
        reboot_fixture = (
            "2026-09-27 17:30:00.000  CLIENT SETUP\n"
            "2026-09-27 17:35:00.000  CLIENT SETUP\n"
            "2026-09-27 17:35:00.100  [BOOT] RESET_REASON=2 SOFTWARE\n"
        )
        reboot_dir = tmp_path / "reboot"
        reboot_dir.mkdir()
        (reboot_dir / "2026-09-27.log").write_text(reboot_fixture, encoding="utf-8")
        sliced_r = load_and_slice(reboot_dir, since, until)
        with tempfile.TemporaryDirectory() as work2:
            work2_path = Path(work2)
            sliced_r_path = work2_path / "r.log"
            sliced_r.write_sliced_copy(sliced_r_path)
            nbr_r = run_nbrlog(sliced_r_path, work2_path)
        metrics_r = compute_node_metrics("REBOOT-TEST", sliced_r, nbr_r, since, until)
        _check("reboot fixture reboots_boot_marker", metrics_r.reboots_boot_marker, 1, failures)
        _check("reboot fixture reboots_total", metrics_r.reboots_total, 1, failures)

        # -- gap detection: a lone STAT line an hour later than the previous
        # one must show up as a >10min gap (BLUF-relevant).
        gap_fixture = (
            "2026-09-27 17:30:00.000  16:29:59 [LOG] STAT util=1 rx=0 tx=0 newid=0 dup=0 err=0 txn=0 "
            "txfail=0 ringmax=0/20 drop=0/0/0/0/0 mh=0 heap=100000 trk=240/1 fw=35t/20260927 up=0 t=0\n"
            "2026-09-27 18:45:00.000  17:44:59 [LOG] STAT util=1 rx=0 tx=0 newid=0 dup=0 err=0 txn=0 "
            "txfail=0 ringmax=0/20 drop=0/0/0/0/0 mh=0 heap=99000 trk=240/1 fw=35t/20260927 up=4500 t=4500000\n"
        )
        gap_dir = tmp_path / "gap"
        gap_dir.mkdir()
        (gap_dir / "2026-09-27.log").write_text(gap_fixture, encoding="utf-8")
        sliced_g = load_and_slice(gap_dir, since, until)
        with tempfile.TemporaryDirectory() as work3:
            work3_path = Path(work3)
            sliced_g_path = work3_path / "g.log"
            sliced_g.write_sliced_copy(sliced_g_path)
            nbr_g = run_nbrlog(sliced_g_path, work3_path)
        metrics_g = compute_node_metrics("GAP-TEST", sliced_g, nbr_g, since, until)
        _check("gap fixture gap count", len(metrics_g.nbr_gaps), 1, failures)
        _check("gap fixture alive (>10min gap)", metrics_g.alive, False, failures)
        _check("gap fixture heap_ok (drift 1000B < budget)", metrics_g.heap_ok, True, failures)

        # -- DM correlation, using the real per-node lines (not sliced-copy
        # roundtrip -- correlate_dm reads the in-memory tuples directly).
        node_lines = {NODE_A: sliced_a.lines, NODE_B: sliced_b.lines}
        sends, tag = parse_soak_log(soak_log, since, until)
        _check("soak sends parsed", len(sends), 2, failures)
        _check("soak tag parsed", tag, "20260927", failures)
        results = [correlate_dm(s, tag or "20260927", node_lines, timedelta(minutes=DEFAULT_MATCH_WINDOW_MIN)) for s in sends]

        d1 = results[0]
        _check("dm1 msgid", d1.msgid, "EA25A324", failures)
        _check("dm1 nnn", d1.nnn, "804", failures)
        _check("dm1 transmissions", d1.transmissions, 1, failures)
        _check("dm1 acked", d1.acked, True, failures)
        _check("dm1 ack_path", d1.ack_path, "server/UDP", failures)
        # counted from the sender's own first TX line (17:39:25.617), not the whole-second soak-log time
        _check("dm1 ack_latency", round(d1.ack_latency_s or -1, 1), 4.0, failures)
        _check("dm1 received", d1.received, True, failures)
        # RX-UDP + one RF reception (its [LOG]/MH-LoRa/RX-LoRa2 lines are ONE copy) = 2
        _check("dm1 receiver_copies", d1.receiver_copies, 2, failures)
        _check("dm1 giveup", d1.giveup, False, failures)

        d2 = results[1]
        _check("dm2 transmissions (orig + 1 retry)", d2.transmissions, 2, failures)
        _check("dm2 giveup", d2.giveup, True, failures)
        _check("dm2 acked", d2.acked, False, failures)
        _check("dm2 received", d2.received, False, failures)

        bluf = build_bluf({NODE_A: metrics_a, NODE_B: metrics_b}, results)
        _check("bluf has 6 criteria", len(bluf), 6, failures)
        unacked_check = next(c for c in bluf if c["criterion"] == "every test DM acked or explained")
        _check("bluf unacked check fails (dm2 unacked)", unacked_check["passed"], False, failures)

        # -- report + JSON rendering must not raise.
        report = render_report(since, until, {NODE_A: metrics_a, NODE_B: metrics_b}, results, bluf)
        if "BLUF" not in report:
            failures.append("render_report: missing BLUF section")
        payload = to_json(since, until, {NODE_A: metrics_a, NODE_B: metrics_b}, results, bluf)
        json.dumps(payload)  # must be JSON-serializable

        # -- parse_when
        _check("parse_when date+time", parse_when("2026-09-27 17:39"), datetime(2026, 9, 27, 17, 39), failures)

    if failures:
        print(f"SELF-TEST FAILED ({len(failures)}):")
        for f in failures:
            print(f"  - {f}")
        return 1
    print("SELF-TEST OK")
    return 0


# --------------------------------------------------------------------------
# main
# --------------------------------------------------------------------------


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--since", type=parse_when, help="window start, e.g. \"2026-09-27 17:39\"")
    ap.add_argument("--until", type=parse_when, default=None, help="window end (default: now)")
    ap.add_argument("--fetch", action="store_true", help="scp DK5EN-98's logs from the rpizero logger first")
    ap.add_argument("--fetch-host", default=DEFAULT_FETCH_HOST)
    ap.add_argument("--fetch-remote", default=DEFAULT_FETCH_REMOTE)
    ap.add_argument(
        "--nodes",
        default=None,
        help=f"comma-separated nodes to evaluate ({NODE_A},{NODE_B}); default both. "
        f"A single-node soak: --nodes {NODE_B}",
    )
    ap.add_argument("--dk1-dir", type=dir_or_none, default=DEFAULT_DK1_DIR, help="DK5EN-1 capture dir, or 'none' to skip that node")
    ap.add_argument("--dk98-dir", type=dir_or_none, default=DEFAULT_DK98_DIR, help="DK5EN-98 capture dir, or 'none' to skip that node")
    ap.add_argument("--soak-log", type=Path, default=DEFAULT_SOAK_LOG)
    ap.add_argument("--match-window-min", type=float, default=DEFAULT_MATCH_WINDOW_MIN)
    ap.add_argument("--out", type=Path, default=None, help="write the Markdown report here")
    ap.add_argument("--json", dest="json_out", type=Path, default=None, help="write the JSON result here")
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args(argv)

    if args.self_test:
        return run_self_test()

    if args.since is None:
        ap.error("--since is required (or use --self-test)")

    until = args.until or datetime.now()

    try:
        node_dirs = select_nodes(args.nodes, {NODE_A: args.dk1_dir, NODE_B: args.dk98_dir})
    except ValueError as exc:
        ap.error(str(exc))

    if args.fetch:
        if NODE_B in node_dirs:
            fetch_dk98(node_dirs[NODE_B], args.fetch_host, args.fetch_remote, args.since, until)
        else:
            print(f"NOTE: --fetch skipped, {NODE_B} is not part of this run", file=sys.stderr)

    sliced = {node: load_and_slice(d, args.since, until) for node, d in node_dirs.items()}

    with tempfile.TemporaryDirectory(prefix="soakstatus-") as work:
        work_path = Path(work)
        nbr_json: dict[str, dict[str, Any]] = {}
        for node, sl in sliced.items():
            nbr_json[node] = {}
            if not sl.lines:
                continue  # capture missing: nothing to hand to nbrlog.py
            sliced_path = work_path / f"{node}.log"
            sl.write_sliced_copy(sliced_path)
            try:
                nbr_json[node] = run_nbrlog(sliced_path, work_path)
            except RuntimeError as exc:
                print(f"WARNING: tools/nbrlog.py failed for {node}: {exc}", file=sys.stderr)

        nodes = {
            node: compute_node_metrics(node, sliced[node], nbr_json[node], args.since, until) for node in sliced
        }

    node_lines = {node: sl.lines for node, sl in sliced.items()}
    sends, tag = parse_soak_log(args.soak_log, args.since, until)
    # A test DM whose SENDER is not part of this run cannot be judged (no TX/ack lines to read).
    skipped = [s for s in sends if s.frm not in node_lines]
    sends = [s for s in sends if s.frm in node_lines]
    if skipped:
        print(
            f"NOTE: {len(skipped)} test DM(s) from a node outside this run left out: "
            + ", ".join(f"#{s.seq} {s.frm}>{s.to}" for s in skipped),
            file=sys.stderr,
        )
    dm_results = [
        correlate_dm(s, tag or args.since.strftime("%Y%m%d"), node_lines, timedelta(minutes=args.match_window_min))
        for s in sends
    ]

    bluf = build_bluf(nodes, dm_results)
    report = render_report(args.since, until, nodes, dm_results, bluf)
    print(report)

    if args.out:
        args.out.write_text(report, encoding="utf-8")
        print(f"\nwrote {args.out}", file=sys.stderr)
    if args.json_out:
        payload = to_json(args.since, until, nodes, dm_results, bluf)
        args.json_out.write_text(json.dumps(payload, indent=2), encoding="utf-8")
        print(f"wrote {args.json_out}", file=sys.stderr)

    return 0 if all(c["passed"] for c in bluf) else 1


if __name__ == "__main__":
    sys.exit(main())
